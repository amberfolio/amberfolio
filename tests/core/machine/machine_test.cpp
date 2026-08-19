// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine: what a bus cycle becomes, what attaching a device does,
// what RESET does, and how a stop gets out.
//
// The whole-program proof that the bus is transparent to the CPU lives in
// tests/core/cpu/program_test.cpp — that is M2-F1's exit criterion and it
// belongs beside the programs it runs. This file is the other half: the
// cases a program cannot reach on purpose, which is most of the ones that
// matter here, because every one of them is about what the machine does
// when it is asked for something that is not there.

#include "amberfolio/machine/machine.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/registers.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::device_access;
using test::recording_device;
using test::recording_diagnostics;

/// Where the test programs below are assembled and run.
constexpr std::uint16_t code_segment = 0x1000;

/// A machine and the sink watching it, kept together because every test
/// wants both. On the heap: a machine has a megabyte inside it.
struct rig {
  explicit rig(memory_layout layout = memory_layout::pc)
      : box(std::make_unique<machine>(layout, &log)) {}

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  /// Put an instruction stream at `code_segment:0000` and point the
  /// processor at it — through memory().ram(), because this is the
  /// machine loading a program rather than the program writing memory.
  void program(std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[at] = byte;
      ++at;
    }

    box->processor().reset();
    box->processor().regs()[cpu::sreg::cs] = code_segment;
    box->processor().regs().ip = 0;
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- Memory ------------------------------------------------------------

TEST(machine_memory, reads_back_what_was_written_to_conventional_ram) {
  const rig r;

  r.pc().write_memory(0x00500, 0x42);
  r.pc().write_memory(0x9FFFF, 0x99);

  EXPECT_EQ(r.pc().read_memory(0x00500), 0x42);
  EXPECT_EQ(r.pc().read_memory(0x9FFFF), 0x99);
  EXPECT_EQ(r.pc().memory().ram()[0x00500], 0x42);
  EXPECT_TRUE(r.log.notices.empty());
}

TEST(machine_memory, floats_high_and_says_so_where_nothing_is_mapped) {
  const rig r;

  EXPECT_EQ(r.pc().read_memory(0xC0000), open_bus_value);
  r.pc().write_memory(0xC8000, 0x11);

  // The write went nowhere — including into the storage behind the map,
  // which the flat layout would have mapped and this one does not.
  EXPECT_EQ(r.pc().memory().ram()[0xC8000], 0);

  ASSERT_EQ(r.log.notices.size(), 2u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::unmapped_memory_read);
  EXPECT_EQ(r.log.notices[0].at, 0xC0000u);
  EXPECT_EQ(r.log.notices[1].what, notice_kind::unmapped_memory_write);
  EXPECT_EQ(r.log.notices[1].at, 0xC8000u);
  EXPECT_EQ(r.log.notices[1].value, 0x11);
}

TEST(machine_memory, says_it_once_per_page_and_not_once_per_cycle) {
  const rig r;

  for (int i = 0; i < 32; ++i) {
    EXPECT_EQ(r.pc().read_memory(0xC0000 + static_cast<std::uint32_t>(i)),
              open_bus_value);
  }
  EXPECT_EQ(r.log.notices.size(), 1u);

  // A different absent thing is a different line, and 4 KiB is fine
  // enough that two of them do not share one.
  EXPECT_EQ(r.pc().read_memory(0xC1000), open_bus_value);
  EXPECT_EQ(r.log.notices.size(), 2u);
}

TEST(machine_memory, names_the_instruction_that_touched_nothing) {
  const rig r;

  // MOV AL, [0000] with DS at C000: a read of C0000, which is the hole.
  r.program({0xA0, 0x00, 0x00});
  r.pc().processor().regs()[cpu::sreg::ds] = 0xC000;

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);

  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].at, 0xC0000u);
  EXPECT_EQ(r.log.notices[0].cs, code_segment);
  EXPECT_EQ(r.log.notices[0].ip, 0x0000);

  // And the program got the bus's answer, not a stop: FF is what the
  // hardware gives, so there is nothing here to refuse.
  EXPECT_EQ(r.pc().processor().regs().get(cpu::reg8::al), open_bus_value);
  EXPECT_FALSE(r.pc().stopped());
}

TEST(machine_memory, reads_the_bios_region_and_refuses_writes_to_it) {
  const rig r;

  // The machine's own writer reaches it; this is how M2-F3 will put the
  // vector stubs there.
  r.pc().memory().ram()[0xFF000] = 0xCF;
  EXPECT_EQ(r.pc().read_memory(0xFF000), 0xCF);

  r.pc().write_memory(0xFF000, 0x00);

  EXPECT_EQ(r.pc().read_memory(0xFF000), 0xCF);
  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::rom_write);
  EXPECT_EQ(r.log.notices[0].at, 0xFF000u);
}

TEST(machine_memory, hands_a_claimed_window_to_its_device) {
  const rig r;
  // A mode is "set" here purely to keep this generic-routing test's own
  // claim — "nothing to report: a device answered" — decoupled from the
  // unrelated mode-discipline notice M2-D3 (#48) added for this exact
  // window (machine.h, "Video mode discipline"): this stand-in device
  // occupies the real EGA's future address range on purpose, to set up
  // the claimed-vs-unclaimed-remainder check below, and without this a
  // program (or a stand-in) writing there before a mode is programmed is
  // now legitimately something to report.
  r.pc().note_video_mode_set();

  recording_device ega;
  ega.wants(memory_window{.first = 0xA0000, .last = 0xAFFFF});
  ASSERT_TRUE(r.pc().attach(ega));

  EXPECT_EQ(r.pc().read_memory(0xA0010), ega.answer);
  r.pc().write_memory(0xAFFFF, 0x7E);

  EXPECT_EQ(ega.accesses.size(), 2u);
  EXPECT_EQ(ega.accesses[0],
            (device_access{.what = device_access::kind::read_memory,
                           .at = 0xA0010,
                           .value = ega.answer}));
  EXPECT_EQ(ega.accesses[1],
            (device_access{.what = device_access::kind::write_memory,
                           .at = 0xAFFFF,
                           .value = 0x7E}));

  // Nothing to report: a device answered.
  EXPECT_TRUE(r.log.notices.empty());

  // What the card left unclaimed is still nothing.
  EXPECT_EQ(r.pc().read_memory(0xB0000), open_bus_value);
  EXPECT_EQ(r.log.notices.size(), 1u);
}

// --- Ports -------------------------------------------------------------

TEST(machine_ports, dispatch_to_the_device_that_claimed_them) {
  const rig r;
  recording_device pit;
  pit.wants(port_range{.first = 0x40, .last = 0x43});
  ASSERT_TRUE(r.pc().attach(pit));

  EXPECT_EQ(r.pc().read_port8(0x40), pit.answer);
  r.pc().write_port8(0x43, 0x36);

  ASSERT_EQ(pit.accesses.size(), 2u);
  EXPECT_EQ(pit.accesses[0].what, device_access::kind::read_port);
  EXPECT_EQ(pit.accesses[0].at, 0x40u);
  EXPECT_EQ(pit.accesses[1].what, device_access::kind::write_port);
  EXPECT_EQ(pit.accesses[1].at, 0x43u);
  EXPECT_EQ(pit.accesses[1].value, 0x36);

  EXPECT_EQ(r.pc().read_port8(0x44), open_bus_value);
}

// bus.h's default, left alone deliberately (machine.h): an 8-bit bus does
// a word in two byte cycles, low half first, and nothing here answers one
// in a single transfer.
TEST(machine_ports, do_a_word_in_two_byte_cycles) {
  const rig r;
  recording_device pit;
  pit.wants(port_range{.first = 0x40, .last = 0x43});
  ASSERT_TRUE(r.pc().attach(pit));

  r.pc().write_port16(0x40, 0xBEEF);

  ASSERT_EQ(pit.accesses.size(), 2u);
  EXPECT_EQ(pit.accesses[0].at, 0x40u);
  EXPECT_EQ(pit.accesses[0].value, 0xEF);
  EXPECT_EQ(pit.accesses[1].at, 0x41u);
  EXPECT_EQ(pit.accesses[1].value, 0xBE);
}

TEST(machine_ports, float_high_and_are_said_once_each) {
  const rig r;

  EXPECT_EQ(r.pc().read_port8(0x3DA), open_bus_value);
  EXPECT_EQ(r.pc().read_port8(0x3DA), open_bus_value);
  r.pc().write_port8(0x3DA, 0x01);
  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::unclaimed_port_read);
  EXPECT_EQ(r.log.notices[0].at, 0x3DAu);

  // One bit per port, not per block of them: an absent card next door is
  // its own line.
  EXPECT_EQ(r.pc().read_port8(0x3DB), open_bus_value);
  EXPECT_EQ(r.log.notices.size(), 2u);
}

// --- Attaching ---------------------------------------------------------

TEST(machine_attach, refuses_a_second_device_that_wants_the_same_ports) {
  const rig r;
  recording_device pit;
  pit.wants(port_range{.first = 0x40, .last = 0x43});
  recording_device impostor;
  impostor.wants(port_range{.first = 0x43, .last = 0x47});

  ASSERT_TRUE(r.pc().attach(pit));
  EXPECT_FALSE(r.pc().attach(impostor));

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(
      r.pc().stop(),
      (stop_record{.reason = stop_reason::conflicting_claim, .at = 0x43}));
  ASSERT_EQ(r.log.stops.size(), 1u);
  EXPECT_EQ(r.log.stops[0], r.pc().stop());

  // The device that was there first still answers, and the one that was
  // refused answers for nothing.
  EXPECT_EQ(r.pc().read_port8(0x43), pit.answer);
  EXPECT_TRUE(impostor.accesses.empty());
}

TEST(machine_attach, refuses_a_second_device_that_wants_the_same_window) {
  const rig r;
  recording_device ega;
  ega.wants(memory_window{.first = 0xA0000, .last = 0xAFFFF});
  recording_device impostor;
  impostor.wants(memory_window{.first = 0xA8000, .last = 0xBFFFF});

  ASSERT_TRUE(r.pc().attach(ega));
  EXPECT_FALSE(r.pc().attach(impostor));

  EXPECT_EQ(
      r.pc().stop(),
      (stop_record{.reason = stop_reason::conflicting_claim, .at = 0xA8000}));
  EXPECT_EQ(r.pc().read_memory(0xA8000), ega.answer);
}

TEST(machine_attach, refuses_more_devices_than_it_has_room_for) {
  const rig r;
  std::array<recording_device, machine::max_devices> fitted;
  for (recording_device& dev : fitted) {
    EXPECT_TRUE(r.pc().attach(dev));
  }

  recording_device one_too_many;
  EXPECT_FALSE(r.pc().attach(one_too_many));
  EXPECT_EQ(r.pc().stop().reason, stop_reason::conflicting_claim);
}

// --- Reset -------------------------------------------------------------

TEST(machine_reset, resets_the_processor_and_every_device) {
  const rig r;
  recording_device pit;
  pit.wants(port_range{.first = 0x40, .last = 0x43});
  ASSERT_TRUE(r.pc().attach(pit));

  r.pc().processor().regs()[cpu::reg16::ax] = 0x1234;
  r.pc().memory().ram()[0x00500] = 0x42;

  r.pc().reset();

  EXPECT_EQ(pit.resets, 1u);
  EXPECT_EQ(r.pc().processor().regs()[cpu::reg16::ax], 0x0000);
  EXPECT_EQ(r.pc().processor().regs()[cpu::sreg::cs], 0xFFFF);

  // RAM keeps what it held: that is what the line does, and a machine
  // that must start from nothing is constructed rather than reset.
  EXPECT_EQ(r.pc().memory().ram()[0x00500], 0x42);

  // Devices stay attached — what they claimed is wiring, not state.
  EXPECT_EQ(r.pc().read_port8(0x40), pit.answer);
}

TEST(machine_reset, starts_noticing_absent_things_again) {
  const rig r;

  EXPECT_EQ(r.pc().read_memory(0xC0000), open_bus_value);
  EXPECT_EQ(r.pc().read_port8(0x3DA), open_bus_value);
  ASSERT_EQ(r.log.notices.size(), 2u);

  r.pc().reset();

  EXPECT_EQ(r.pc().read_memory(0xC0000), open_bus_value);
  EXPECT_EQ(r.pc().read_port8(0x3DA), open_bus_value);
  EXPECT_EQ(r.log.notices.size(), 4u);
}

// --- Stepping and stopping ---------------------------------------------

TEST(machine_step, runs_one_instruction_against_the_map) {
  const rig r;

  // MOV AX, 1234h — fetched out of conventional RAM through the map.
  r.program({0xB8, 0x34, 0x12});

  EXPECT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().processor().regs()[cpu::reg16::ax], 0x1234);
  EXPECT_FALSE(r.pc().stopped());
  EXPECT_TRUE(r.log.notices.empty());
}

TEST(machine_step, surfaces_a_processor_stop_as_a_machine_stop) {
  const rig r;

  // A prefix run longer than any real instruction: the one thing the
  // interpreter refuses outright now that every opcode has a handler
  // (cpu/diagnostics.h explains why it is bounded at all). What it is
  // does not matter here — that the machine notices does.
  r.program({});
  for (std::uint32_t i = 0; i < 300; ++i) {
    r.pc().memory().ram()[cpu::physical_address(
        code_segment, static_cast<std::uint16_t>(i))] = 0xF2;
  }

  EXPECT_EQ(r.pc().step(), cpu::step_status::stopped);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::processor);
  EXPECT_EQ(r.pc().stop().at, cpu::physical_address(code_segment, 0));

  // One line, and it is the processor's own record, because that is the
  // one that names what was refused.
  ASSERT_EQ(r.log.processor_stops.size(), 1u);
  EXPECT_EQ(r.log.processor_stops[0].reason,
            cpu::stop_reason::prefix_chain_too_long);
  EXPECT_TRUE(r.log.stops.empty());
}

TEST(machine_step, stays_stopped_and_stops_touching_anything) {
  const rig r;
  recording_device ega;
  ega.wants(memory_window{.first = 0xA0000, .last = 0xAFFFF});
  ASSERT_TRUE(r.pc().attach(ega));

  r.program({});
  for (std::uint32_t i = 0; i < 300; ++i) {
    r.pc().memory().ram()[cpu::physical_address(
        code_segment, static_cast<std::uint16_t>(i))] = 0xF2;
  }
  ASSERT_EQ(r.pc().step(), cpu::step_status::stopped);

  EXPECT_EQ(r.pc().step(), cpu::step_status::stopped);
  EXPECT_EQ(r.pc().step(), cpu::step_status::stopped);

  // A stop is an event, not a state to be re-reported on every step a
  // caller takes past it — and nothing executed, so the IP the record
  // named is still where it says.
  EXPECT_EQ(r.log.processor_stops.size(), 1u);
  EXPECT_EQ(r.pc().processor().regs().ip, 0x0000);
}

TEST(machine_step, runs_again_after_a_reset) {
  const rig r;

  r.program({});
  for (std::uint32_t i = 0; i < 300; ++i) {
    r.pc().memory().ram()[cpu::physical_address(
        code_segment, static_cast<std::uint16_t>(i))] = 0xF2;
  }
  ASSERT_EQ(r.pc().step(), cpu::step_status::stopped);

  r.pc().reset();
  r.program({0xB8, 0x34, 0x12});  // MOV AX, 1234h — over the prefix run

  EXPECT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().processor().regs()[cpu::reg16::ax], 0x1234);
}

}  // namespace
}  // namespace amberfolio::machine
