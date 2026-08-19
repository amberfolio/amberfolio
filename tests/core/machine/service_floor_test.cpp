// SPDX-License-Identifier: AGPL-3.0-only
//
// The service floor: what the vector table and the BDA look like once the
// machine has laid them down, what happens when an interrupt reaches one
// of our stubs, and — the two that matter most — what happens when the
// program has hooked the vector, and when nothing implements it.
//
// Almost every test here runs a real instruction stream rather than
// calling the mechanism directly, because almost every claim this layer
// makes is about a program that cannot tell. A test that reached in and
// called a handler would prove nothing about the path a program takes to
// it.
//
// Every byte of every program below is written here, in this file, from
// the encoding — the clean-content rule applies to test data exactly as
// it does to everything else (PLAN.md §6).

#include "amberfolio/machine/service_floor.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::device_access;
using test::recording_device;
using test::recording_diagnostics;

/// Where the programs below are assembled and run, and where their stack
/// is: the same segment, because none of them needs two.
constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t stack_top = 0x1000;

/// A vector no BIOS or DOS service of the era used, so that a test can
/// install a handler on it without standing on anything M2-D will want.
constexpr std::uint8_t test_vector = 0x80;

/// A vector this machine will never implement.
constexpr std::uint8_t absent_vector = 0x60;

/// What the native handlers below saw.
///
/// A service handler is a plain function pointer with nowhere to keep
/// state — core/ carries no `<functional>` and allocates nothing — so a
/// test's observations live here, and the rig clears them.
struct observation {
  unsigned calls{};
  std::uint8_t vector{};
  cpu::registers regs{};
  std::uint16_t caller_cs{};
  std::uint16_t caller_ip{};
  std::uint16_t caller_flags{};
};

observation seen;

/// Records everything a handler can see about the call that reached it.
void watch(service_floor& floor, std::uint8_t vector) {
  ++seen.calls;
  seen.vector = vector;
  seen.regs = floor.box().processor().regs();
  seen.caller_cs = floor.caller_cs();
  seen.caller_ip = floor.caller_ip();
  seen.caller_flags = floor.caller_flags();
}

/// DOS's failure convention: an error code in AX and CF set — in the
/// flags image on the stack, which is the one the IRET is about to pop.
void report_failure(service_floor& floor, std::uint8_t /*vector*/) {
  ++seen.calls;
  floor.box().processor().regs()[cpu::reg16::ax] = 0x0002;
  floor.set_caller_carry(true);
}

/// The other half of the convention, and the one that proves the edit is
/// an edit and not a set: CF clear on success, over a caller that had it
/// set going in.
void report_success(service_floor& floor, std::uint8_t /*vector*/) {
  ++seen.calls;
  floor.set_caller_carry(false);
}

/// A machine, the sink watching it, and the programs it runs. On the
/// heap: a machine has a megabyte inside it.
struct rig {
  explicit rig(memory_layout layout = memory_layout::pc)
      : box(std::make_unique<machine>(layout, &log)) {
    seen = {};
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  /// Put bytes at `code_segment:at` through memory().ram() — the machine
  /// loading a program, not the program writing memory (memory_map.h).
  void poke(std::uint16_t at, std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t p = cpu::physical_address(code_segment, at);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[p] = byte;
      ++p;
    }
  }

  /// Load an instruction stream at `code_segment:0000` and point the
  /// processor at it, with a stack, because everything here takes an
  /// interrupt.
  void program(std::initializer_list<std::uint8_t> bytes) const {
    poke(0, bytes);

    box->processor().reset();
    cpu::registers& regs = box->processor().regs();
    regs[cpu::sreg::cs] = code_segment;
    regs[cpu::sreg::ss] = code_segment;
    regs[cpu::reg16::sp] = stack_top;
    regs.ip = 0;
  }

  /// Point a vector somewhere, the way a program's own hooking code
  /// would — four bytes in the table, offset first.
  void hook(std::uint8_t vector, std::uint16_t segment,
            std::uint16_t offset) const {
    const std::uint32_t entry = cpu::physical_address(
        cpu::vector_table_segment, cpu::vector_table_offset(vector));
    const std::span<std::uint8_t> ram = box->memory().ram();
    ram[entry] = static_cast<std::uint8_t>(offset);
    ram[entry + 1] = static_cast<std::uint8_t>(offset >> 8u);
    ram[entry + 2] = static_cast<std::uint8_t>(segment);
    ram[entry + 3] = static_cast<std::uint8_t>(segment >> 8u);
  }

  /// Where `vector` points now.
  [[nodiscard]] std::uint32_t vector_at(std::uint8_t vector) const {
    const std::uint32_t entry = cpu::physical_address(
        cpu::vector_table_segment, cpu::vector_table_offset(vector));
    const std::span<const std::uint8_t> ram = box->memory().ram();
    return static_cast<std::uint32_t>(ram[entry]) |
           (static_cast<std::uint32_t>(ram[entry + 1]) << 8u) |
           (static_cast<std::uint32_t>(ram[entry + 2]) << 16u) |
           (static_cast<std::uint32_t>(ram[entry + 3]) << 24u);
  }

  /// The BIOS tick count, read out of the BDA the way a program reads it.
  [[nodiscard]] std::uint32_t ticks() const {
    const std::uint32_t at =
        cpu::physical_address(bda::segment, bda::timer_ticks);
    const std::span<const std::uint8_t> ram = box->memory().ram();
    return static_cast<std::uint32_t>(ram[at]) |
           (static_cast<std::uint32_t>(ram[at + 1]) << 8u) |
           (static_cast<std::uint32_t>(ram[at + 2]) << 16u) |
           (static_cast<std::uint32_t>(ram[at + 3]) << 24u);
  }

  void set_ticks(std::uint32_t value) const {
    const std::uint32_t at =
        cpu::physical_address(bda::segment, bda::timer_ticks);
    const std::span<std::uint8_t> ram = box->memory().ram();
    for (unsigned i = 0; i < 4; ++i) {
      ram[at + i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
  }

  /// Step until the program halts or the machine stops. The cap is a
  /// test-failure device, not a scheduling one.
  void run(unsigned cap = 2000) const {
    unsigned steps = 0;
    while (!box->processor().halted() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- What power-on puts in memory --------------------------------------

TEST(service_reset, points_every_vector_at_a_stub_that_is_a_real_iret) {
  const rig r;

  for (unsigned v = 0; v < service::vector_stubs; ++v) {
    const auto vector = static_cast<std::uint8_t>(v);
    const std::uint16_t offset = service::stub_offset(vector);
    const auto entry = static_cast<std::uint32_t>(
        (static_cast<std::uint32_t>(service::stub_segment) << 16u) | offset);

    ASSERT_EQ(r.vector_at(vector), entry) << "vector " << v;
    ASSERT_EQ(r.pc().memory().ram()[cpu::physical_address(service::stub_segment,
                                                          offset)],
              service::iret_opcode)
        << "vector " << v;
  }

  // And the continuation stubs, which no vector points at and which a
  // handler reaches by setting IP.
  for (unsigned slot = 0; slot < service::continuation_stubs; ++slot) {
    ASSERT_EQ(r.pc().memory().ram()[cpu::physical_address(
                  service::stub_segment, service::continuation_offset(slot))],
              service::iret_opcode);
  }
}

TEST(service_reset, lays_out_the_bios_data_area) {
  const rig r;
  const std::uint32_t area = cpu::physical_address(bda::segment, 0);

  EXPECT_EQ(r.ticks(), 0u);
  EXPECT_EQ(r.pc().memory().ram()[area + bda::timer_rollover], 0);

  // 640, in KiB, from the memory map rather than from a second opinion.
  const std::uint16_t kb =
      static_cast<std::uint16_t>(
          r.pc().memory().ram()[area + bda::memory_size_kb]) |
      static_cast<std::uint16_t>(
          r.pc().memory().ram()[area + bda::memory_size_kb + 1] << 8u);
  EXPECT_EQ(kb, conventional_ram_size / 1024u);
}

TEST(service_reset, rebuilds_the_table_but_keeps_what_was_installed) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  // A program has hooked the vector and left a tick count behind.
  r.hook(test_vector, 0x1234, 0x5678);
  r.set_ticks(99);

  r.pc().reset();

  // The self test wrote the table and the BDA again...
  EXPECT_EQ(r.vector_at(test_vector),
            (static_cast<std::uint32_t>(service::stub_segment) << 16u) |
                service::stub_offset(test_vector));
  EXPECT_EQ(r.ticks(), 0u);

  // ...and the handler behind it is still there, because what a service
  // layer installed is wiring and not state.
  r.program({0xCD, test_vector, 0xF4});
  r.run();
  EXPECT_EQ(seen.calls, 1u);
}

TEST(service_floor_layout, is_absent_from_a_machine_that_is_not_a_pc) {
  const rig r(memory_layout::flat);

  EXPECT_FALSE(r.pc().services().enabled());

  // Nothing was written anywhere: a flat machine is a megabyte of RAM
  // that belongs entirely to the program (memory_map.h).
  EXPECT_EQ(r.vector_at(test_vector), 0u);
  EXPECT_EQ(r.pc().memory().ram()[cpu::physical_address(service::stub_segment,
                                                        service::stub_base)],
            0);
}

// --- Dispatch ----------------------------------------------------------

TEST(service_dispatch, reaches_the_native_handler_with_the_registers_intact) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  r.program({0xCD, test_vector, 0xF4});  // INT 80h ; HLT
  r.regs()[cpu::reg16::ax] = 0x3D02;
  r.regs()[cpu::reg16::bx] = 0x1234;
  r.regs()[cpu::reg16::cx] = 0xBEEF;
  r.regs()[cpu::sreg::ds] = 0x4321;

  // The INT instruction. Delivery lands on the stub and nothing native
  // has run yet: the callout is at the *next* step boundary.
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(seen.calls, 0u);
  EXPECT_EQ(r.regs()[cpu::sreg::cs], service::stub_segment);
  EXPECT_EQ(r.regs().ip, service::stub_offset(test_vector));

  // The service: the handler, then the stub's IRET, in one step.
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);

  EXPECT_EQ(seen.calls, 1u);
  EXPECT_EQ(seen.vector, test_vector);
  EXPECT_EQ(seen.regs[cpu::reg16::ax], 0x3D02);
  EXPECT_EQ(seen.regs[cpu::reg16::bx], 0x1234);
  EXPECT_EQ(seen.regs[cpu::reg16::cx], 0xBEEF);
  EXPECT_EQ(seen.regs[cpu::sreg::ds], 0x4321);

  // And what the frame said about the caller: the address after the INT,
  // which is what the 8086 pushes.
  EXPECT_EQ(seen.caller_cs, code_segment);
  EXPECT_EQ(seen.caller_ip, 0x0002);
}

TEST(service_dispatch, returns_through_the_stubs_own_iret) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  r.program({0xCD, test_vector, 0xF4});
  r.regs().set_flag(cpu::flag::if_ | cpu::flag::sf, true);
  const std::uint16_t sp_before = r.regs()[cpu::reg16::sp];
  const std::uint16_t flags_before = r.regs().flags;

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);

  // Back where the INT came from, with the stack unwound and the flag
  // word restored — including IF, which delivery cleared. Nothing here
  // imitates any of that: the IRET at the stub did it.
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
  EXPECT_EQ(r.regs().ip, 0x0002);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], sp_before);
  EXPECT_EQ(r.regs().flags, flags_before);

  // The handler saw the caller's flags on the stack, not the ones
  // delivery had already cleared IF in.
  EXPECT_EQ(seen.caller_flags, flags_before);
  EXPECT_FALSE(seen.regs.flag_set(cpu::flag::if_));
}

TEST(service_dispatch, lets_a_handler_report_failure_in_the_callers_carry) {
  const rig r;
  r.pc().services().provide(test_vector, &report_failure);

  r.program({0xCD, test_vector, 0xF4});
  ASSERT_FALSE(r.regs().flag_set(cpu::flag::cf));

  r.run();

  EXPECT_EQ(seen.calls, 1u);
  EXPECT_TRUE(r.regs().flag_set(cpu::flag::cf));
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x0002);
}

TEST(service_dispatch, lets_a_handler_clear_a_carry_the_caller_arrived_with) {
  const rig r;
  r.pc().services().provide(test_vector, &report_success);

  r.program({0xCD, test_vector, 0xF4});
  r.regs().set_flag(cpu::flag::cf, true);

  r.run();

  EXPECT_EQ(seen.calls, 1u);
  EXPECT_FALSE(r.regs().flag_set(cpu::flag::cf));
}

TEST(service_dispatch, is_bypassed_entirely_by_a_vector_the_program_hooked) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  // The program hooks INT 80h itself and then calls it. Nothing tells the
  // machine it has done so; the entry simply stops pointing at our stub.
  //
  //   0000  31 C0              XOR AX, AX
  //   0002  8E D8              MOV DS, AX
  //   0004  C7 06 00 02 11 00  MOV word [0200], 0011h   ; 80h * 4
  //   000A  8C 0E 02 02        MOV [0202], CS
  //   000E  CD 80              INT 80h
  //   0010  F4                 HLT
  //   0011  45                 INC BP                   ; the hook
  //   0012  CF                 IRET
  r.program({0x31, 0xC0, 0x8E, 0xD8, 0xC7, 0x06, 0x00, 0x02, 0x11, 0x00, 0x8C,
             0x0E, 0x02, 0x02, 0xCD, 0x80, 0xF4, 0x45, 0xCF});

  r.run();

  EXPECT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::reg16::bp], 1);

  // Not "the handler declined to run" — the handler was never reachable.
  EXPECT_EQ(seen.calls, 0u);
  EXPECT_TRUE(r.log.calls.empty());
}

TEST(service_dispatch, stops_readably_on_a_vector_nothing_implements) {
  const rig r;

  r.program({0xCD, absent_vector, 0xF4});
  r.regs()[cpu::reg16::ax] = 0x4C00;

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().step(), cpu::step_status::stopped);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_service);
  EXPECT_EQ(r.pc().stop().at,
            cpu::physical_address(service::stub_segment,
                                  service::stub_offset(absent_vector)));

  // The line a human reads: which interrupt, which function, and who
  // asked for it.
  ASSERT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.calls[0].vector, absent_vector);
  EXPECT_EQ(r.log.calls[0].function(), 0x4C);
  EXPECT_EQ(r.log.calls[0].ax, 0x4C00);
  EXPECT_EQ(r.log.calls[0].caller_cs, code_segment);
  EXPECT_EQ(r.log.calls[0].caller_ip, 0x0002);
  EXPECT_EQ(r.log.calls[0].outcome, service_outcome::unimplemented);

  ASSERT_EQ(r.log.stops.size(), 1u);
  EXPECT_EQ(r.log.stops[0], r.pc().stop());

  // Nothing was invented and nothing ran: the stub's IRET is still the
  // next thing that would execute, so the machine can be inspected right
  // where it gave up.
  EXPECT_EQ(r.regs().ip, service::stub_offset(absent_vector));

  // And a stop stays one line however many steps a caller takes past it.
  EXPECT_EQ(r.pc().step(), cpu::step_status::stopped);
  EXPECT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.stops.size(), 1u);
}

TEST(service_dispatch, traces_a_call_that_was_answered) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  r.program({0xCD, test_vector, 0xF4});
  r.regs()[cpu::reg16::ax] = 0x0900;
  r.run();

  ASSERT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.calls[0],
            (service_call{.vector = test_vector,
                          .ax = 0x0900,
                          .caller_cs = code_segment,
                          .caller_ip = 0x0002,
                          .outcome = service_outcome::handled}));
}

TEST(service_dispatch, waits_for_a_boundary_that_owes_no_interrupt) {
  const rig r;
  r.pc().services().provide(test_vector, &watch);

  // A program being single-stepped over an INT. The trap is owed by the
  // INT instruction itself, so it falls due at the stub — between the
  // native handler and the IRET that ends it. The service must still
  // happen exactly once, which it does by not starting until the
  // boundary is clear.
  r.program({0xCD, test_vector, 0xF4});
  r.poke(0x0010, {0xCF});  // the debugger's trap handler: IRET
  r.hook(cpu::interrupt_vector::single_step, code_segment, 0x0010);
  r.regs().set_flag(cpu::flag::tf, true);

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);  // INT 80h
  EXPECT_EQ(seen.calls, 0u);

  ASSERT_EQ(r.pc().step(), cpu::step_status::serviced);  // the owed trap
  EXPECT_EQ(seen.calls, 0u);
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);  // the trap's IRET
  EXPECT_EQ(seen.calls, 0u);
  EXPECT_EQ(r.regs()[cpu::sreg::cs], service::stub_segment);

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);  // now the service
  EXPECT_EQ(seen.calls, 1u);
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
  EXPECT_EQ(r.regs().ip, 0x0002);
}

// --- The default timer service -----------------------------------------

TEST(timer_service, ticks_the_bios_count_and_chains_the_user_vector) {
  const rig r;
  r.pc().services().provide(service::user_tick_vector, &watch);

  r.program({0xF4});  // HLT — the machine waits for the interrupt
  r.regs().set_flag(cpu::flag::if_, true);
  r.pc().processor().raise_intr(service::timer_vector);

  // Four steps, and they are the whole mechanism: the interrupt is
  // delivered onto the stub; the timer handler runs and chains into 1Ch,
  // whose own stub IRETs back; the continuation runs and IRETs back to
  // the program; and the HLT the program never got to execute finally
  // does. Only then is there nothing left to do.
  ASSERT_EQ(r.pc().step(), cpu::step_status::serviced);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().step(), cpu::step_status::halted);

  EXPECT_EQ(r.ticks(), 1u);
  EXPECT_EQ(seen.calls, 1u);
  EXPECT_EQ(seen.vector, service::user_tick_vector);

  // The user tick was a real interrupt push, so it returns to the
  // continuation — the second half of the timer handler — exactly as an
  // `INT 1Ch` instruction in a BIOS would return to the instruction after
  // it.
  EXPECT_EQ(seen.caller_cs, service::stub_segment);
  EXPECT_EQ(seen.caller_ip,
            service::continuation_offset(service::timer_continuation));

  // Back where the program was, with its flags.
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
  EXPECT_TRUE(r.regs().flag_set(cpu::flag::if_));
}

TEST(timer_service, rolls_the_count_over_at_midnight) {
  const rig r;
  r.set_ticks(bda::ticks_per_day - 1);

  r.program({0xF4});
  r.regs().set_flag(cpu::flag::if_, true);
  r.pc().processor().raise_intr(service::timer_vector);
  r.run();

  EXPECT_EQ(r.ticks(), 0u);
  EXPECT_EQ(r.pc().memory().ram()[cpu::physical_address(bda::segment,
                                                        bda::timer_rollover)],
            1);
}

TEST(timer_service, acknowledges_the_interrupt_controller) {
  const rig r;
  recording_device controller;
  controller.wants(port_range{.first = 0x20, .last = 0x21});
  ASSERT_TRUE(r.pc().attach(controller));

  r.program({0xF4});
  r.regs().set_flag(cpu::flag::if_, true);
  r.pc().processor().raise_intr(service::timer_vector);
  r.run();

  ASSERT_EQ(controller.accesses.size(), 1u);
  EXPECT_EQ(controller.accesses[0],
            (device_access{.what = device_access::kind::write_port,
                           .at = pic::master_command_port,
                           .value = pic::end_of_interrupt}));
}

TEST(timer_service, says_so_when_there_is_no_controller_to_acknowledge) {
  const rig r;

  r.program({0xF4});
  r.regs().set_flag(cpu::flag::if_, true);
  r.pc().processor().raise_intr(service::timer_vector);
  r.run();

  // There is no 8259 in this machine yet (M2-D1, #46). The EOI is a real
  // write to a real port, so the port map reports that nothing answers
  // for it rather than the handler pretending it did not happen.
  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::unclaimed_port_write);
  EXPECT_EQ(r.log.notices[0].at, pic::master_command_port);
  EXPECT_FALSE(r.pc().stopped());
}

// --- The exit criterion ------------------------------------------------

TEST(timer_service, a_program_hooks_int_1ch_counts_ticks_and_reads_the_bda) {
  const rig r;

  //   0000  31 C0              XOR AX, AX
  //   0002  8E D8              MOV DS, AX
  //   0004  C7 06 70 00 1F 00  MOV word [0070], 001Fh   ; 1Ch * 4
  //   000A  8C 0E 72 00        MOV [0072], CS
  //   000E  31 ED              XOR BP, BP
  //   0010  FB                 STI
  //   0011  83 FD 03           CMP BP, 3                ; wait:
  //   0014  72 FB              JB wait
  //   0016  B8 40 00           MOV AX, 0040h
  //   0019  8E D8              MOV DS, AX
  //   001B  A1 6C 00           MOV AX, [006C]           ; the tick count
  //   001E  F4                 HLT
  //   001F  45                 INC BP                   ; the tick handler
  //   0020  CF                 IRET
  r.program({0x31, 0xC0, 0x8E, 0xD8, 0xC7, 0x06, 0x70, 0x00, 0x1F, 0x00, 0x8C,
             0x0E, 0x72, 0x00, 0x31, 0xED, 0xFB, 0x83, 0xFD, 0x03, 0x72, 0xFB,
             0xB8, 0x40, 0x00, 0x8E, 0xD8, 0xA1, 0x6C, 0x00, 0xF4, 0x45, 0xCF});

  // IRQ0, injected: there is no PIT yet (M2-D1, #46), so the line is
  // driven by hand. Exactly three requests, and the next one is not
  // raised until the tick count says the last one has been counted —
  // the line drops when the processor takes the vector, which is a step
  // before the handler that does the counting runs.
  constexpr std::uint32_t wanted = 3;
  std::uint32_t raised = 0;
  unsigned steps = 0;
  while (!r.pc().processor().halted() && !r.pc().stopped() && steps < 4000) {
    if (raised < wanted && r.ticks() == raised) {
      r.pc().processor().raise_intr(service::timer_vector);
      ++raised;
    }
    r.pc().step();
    ++steps;
  }

  ASSERT_TRUE(r.pc().processor().halted());

  // The program's own hook ran, once per tick...
  EXPECT_EQ(r.regs()[cpu::reg16::bp], wanted);
  // ...the BIOS count agrees...
  EXPECT_EQ(r.ticks(), wanted);
  // ...and the program read the same number out of 40:6C for itself.
  EXPECT_EQ(r.regs()[cpu::reg16::ax], wanted);
}

}  // namespace
}  // namespace amberfolio::machine
