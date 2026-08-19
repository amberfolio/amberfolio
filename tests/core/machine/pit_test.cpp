// SPDX-License-Identifier: AGPL-3.0-only
//
// The 8253 PIT: divisor-to-period timing for modes 0, 2 and 3, the write-
// order and count-latch state machines, the mid-cycle reload rule modes
// 2 and 3 share, the channel-2 gate line, the 0-means-65536 divisor, and
// — this issue's exit criterion — a program that hooks INT 1Ch and counts
// real, PIT-driven ticks over an exactly computable span.
//
// Every test drives virtual time with `rig::advance()`, which steps a
// halted CPU at one tick per step (`machine::set_step_cost`) rather than
// running any particular instruction stream: what is under test is the
// PIT's arithmetic against virtual time, not anything about the
// processor, and HLT is the shortest path to "time passes and nothing
// else does." The exit-criterion test is the one exception — it needs a
// real program to hook a vector, so it runs one, the same way
// service_floor_test.cpp's own exit-criterion test does.
//
// The controller's own state machine (ICW init, masking, EOI) is
// pic_test.cpp's file, tested against `raise_irq0()` called directly;
// here it is wired up once, normally, and left alone, so what these
// tests observe through it is purely a fact about the PIT's timing.

#include "amberfolio/machine/pit.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/service_floor.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x2000;

/// A machine with a PIT and a PIC wired up exactly as the wiring code in
/// pit.h's own doc comment describes, plus the stock ICW sequence
/// already sent — every test here is about PIT timing, not about
/// whether the controller behind it is ready, and pic_test.cpp is where
/// that gets exercised on its own.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        irq(*box),
        timer(*box, irq) {
    box->attach(irq);
    box->attach(timer);
    box->schedule(timer.channel0_deadline());
    box->schedule(timer.channel2_deadline());

    box->write_port8(pic::master_command_port, pic::icw1_edge_single_icw4);
    box->write_port8(pic::data_port, pic::expected_vector_base);
    box->write_port8(pic::data_port, pic::icw4_8086_mode);

    box->processor().reset();
    box->processor().regs()[cpu::sreg::cs] = code_segment;
    box->processor().regs()[cpu::sreg::ss] = code_segment;
    box->processor().regs().ip = 0;
    box->memory().ram()[cpu::physical_address(code_segment, 0)] = 0xF4;  // HLT
    box->set_step_cost(1);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  /// Advance virtual time by exactly `n` ticks: a halted CPU, stepped at
  /// one tick each, so every deadline this issue's tests care about
  /// lands on the exact tick it should.
  void advance(ticks n) const { box->run(box->time() + n); }

  void enable_interrupts() const {
    box->processor().regs().set_flag(cpu::flag::if_, true);
  }

  /// 43h then one or two data-port writes: a control word selecting
  /// `port`'s channel with `access` and `mode`, followed by `value`'s
  /// bytes in the order the access mode calls for.
  void program_channel(std::uint16_t port, pit_access access, pit_mode mode,
                       std::uint16_t value) const {
    const std::uint8_t select = port == pit_channel0_port   ? 0
                                : port == pit_channel1_port ? 1
                                                            : 2;
    const auto mode_bits =
        static_cast<std::uint8_t>(mode == pit_mode::mode0   ? 0
                                  : mode == pit_mode::mode2 ? 2
                                                            : 3);
    const auto access_bits = static_cast<std::uint8_t>(access);
    box->write_port8(pit_control_port, static_cast<std::uint8_t>(
                                           (select << 6) | (access_bits << 4) |
                                           (mode_bits << 1)));
    if (access == pit_access::lsb || access == pit_access::both) {
      box->write_port8(port, static_cast<std::uint8_t>(value));
    }
    if (access == pit_access::msb || access == pit_access::both) {
      box->write_port8(port, static_cast<std::uint8_t>(value >> 8u));
    }
  }

  /// 43h's RW=00 for `port`'s channel.
  void latch(std::uint16_t port) const {
    const std::uint8_t select = port == pit_channel0_port   ? 0
                                : port == pit_channel1_port ? 1
                                                            : 2;
    box->write_port8(pit_control_port, static_cast<std::uint8_t>(select << 6));
  }

  /// Two byte reads of `port`, low then high — the order RW=both always
  /// reads in.
  [[nodiscard]] std::uint16_t read16(std::uint16_t port) const {
    const std::uint16_t low = box->read_port8(port);
    const std::uint16_t high = box->read_port8(port);
    return static_cast<std::uint16_t>(low | (high << 8u));
  }

  /// The BIOS tick count at 40:6C, the way a program reads it —
  /// service_floor_test.cpp's own rig has the identical helper, restated
  /// rather than shared for the same reason clock_test.cpp restates its
  /// rig.
  [[nodiscard]] std::uint32_t bios_ticks() const {
    const std::uint32_t at =
        cpu::physical_address(bda::segment, bda::timer_ticks);
    const std::span<const std::uint8_t> ram = box->memory().ram();
    return static_cast<std::uint32_t>(ram[at]) |
           (static_cast<std::uint32_t>(ram[at + 1]) << 8u) |
           (static_cast<std::uint32_t>(ram[at + 2]) << 16u) |
           (static_cast<std::uint32_t>(ram[at + 3]) << 24u);
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  pic::controller irq;
  pit timer;
};

// --- Divisor to period, channel 0 through the real IRQ0 path -------------
//
// `advance(n)` moves virtual time forward by exactly `n`; a deadline due
// at tick D is only *dispatched* by a step whose own start is at or past
// D (scheduler.h), and the default timer handler's chain — delivery,
// the handler body, the chained INT 1Ch, its stub, the continuation's
// EOI — takes a handful more steps after that before `bios_ticks()`
// reflects it (service_floor_test.cpp's own timer tests take five). So
// every checkpoint below that expects a *new* tick to have landed adds a
// small fixed slack past the boundary it is checking; a checkpoint that
// expects nothing new needs none, because "not enough time has passed
// yet" is true regardless of how close it is.

/// Comfortably more than the handler chain's five steps, and
/// comfortably less than any divisor used below, so it never lands past
/// the *next* boundary while confirming the current one.
constexpr ticks chain_slack = 8;

TEST(pit_channel0, delivers_one_timer_interrupt_per_divisor_ticks_in_mode2) {
  rig r;
  r.enable_interrupts();
  constexpr ticks divisor = 20;
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode2,
                    static_cast<std::uint16_t>(divisor));

  r.advance(divisor + chain_slack);
  EXPECT_EQ(r.bios_ticks(), 1u);
  r.advance(divisor);
  EXPECT_EQ(r.bios_ticks(), 2u);
  r.advance(divisor * 3);
  EXPECT_EQ(r.bios_ticks(), 5u);
}

TEST(pit_channel0, delivers_one_timer_interrupt_per_divisor_ticks_in_mode3) {
  rig r;
  r.enable_interrupts();
  constexpr ticks divisor = 20;
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode3,
                    static_cast<std::uint16_t>(divisor));

  r.advance(divisor * 5 + chain_slack);
  EXPECT_EQ(r.bios_ticks(), 5u);
}

TEST(pit_channel0,
     the_default_divisor_produces_exactly_18_ticks_over_one_virtual_second) {
  rig r;
  r.enable_interrupts();
  // 0 => 65536, the divisor the real PC BIOS programs for its 18.2 Hz
  // tick (clock.h: pit_input_hz is exactly this chip's input clock).
  r.program_channel(pit_channel0_port, pit_access::both, pit_mode::mode3, 0);

  r.advance(pit_input_hz);

  // floor(1,193,182 / 65,536) = 18 — computed here, once, rather than
  // asserted as a bare literal, so the test states its own reasoning.
  EXPECT_EQ(r.bios_ticks(), pit_input_hz / 0x10000u);
  EXPECT_EQ(r.bios_ticks(), 18u);
}

TEST(pit_channel0, defers_a_mid_cycle_rewrite_to_the_cycles_own_boundary) {
  rig r;
  r.enable_interrupts();
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode2, 30);

  r.advance(15);  // well inside the first cycle
  EXPECT_EQ(r.bios_ticks(), 0u);

  // A new count, same control word: modes 2 and 3 hold this until the
  // cycle already running finishes on its own terms.
  r.pc().write_port8(pit_channel0_port, 10);

  r.advance(15 + chain_slack);  // completes the original, unperturbed
                                // 30-tick cycle: past tick 30 by slack
  EXPECT_EQ(r.bios_ticks(), 1u);

  // The next boundary is 10 ticks past that one (the rewrite, now in
  // force) — 2 more ticks to reach it from here, plus slack again to
  // clear its own chain, and comfortably short of the boundary after
  // that (at 10 more again).
  r.advance(2 + chain_slack);
  EXPECT_EQ(r.bios_ticks(), 2u);
}

TEST(pit_channel0, mode0_is_a_one_shot_edge_not_a_periodic_one) {
  rig r;
  r.enable_interrupts();
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode0, 20);

  r.advance(20 + chain_slack);
  EXPECT_EQ(r.bios_ticks(), 1u);

  // Nothing rearms it: the count keeps running (below), but the output
  // edge that drove IRQ0 happened once.
  r.advance(200);
  EXPECT_EQ(r.bios_ticks(), 1u);
}

// --- Masking, end to end through the real controller ----------------------

TEST(pit_channel0, a_masked_irq0_holds_the_tick_and_delivers_it_on_unmask) {
  rig r;
  r.enable_interrupts();
  r.pc().write_port8(pic::data_port, 0x01);  // mask IRQ0
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode2, 20);

  // Past the output edge itself — the PIT does not know or care whether
  // IRQ0 is masked — but nothing was ever delivered.
  r.advance(20 + chain_slack);
  EXPECT_EQ(r.bios_ticks(), 0u);

  r.pc().write_port8(pic::data_port, 0x00);  // unmask

  // The request was already outstanding, so it is delivered at the very
  // next step boundary; `chain_slack` is enough for the default
  // handler's chain — delivery, the handler body, the chained INT 1Ch
  // and its stub, the continuation's EOI — to finish and be back at HLT
  // (service_floor_test.cpp's own timer tests take the identical steps).
  r.advance(chain_slack);
  EXPECT_EQ(r.bios_ticks(), 1u);
}

// --- Channel 1: counts, and nothing consumes it ---------------------------

TEST(pit_channel1, counts_down_on_demand_without_ever_being_scheduled) {
  rig r;
  r.program_channel(pit_channel1_port, pit_access::both, pit_mode::mode2, 100);

  EXPECT_EQ(r.read16(pit_channel1_port), 100u);
  r.advance(37);
  EXPECT_EQ(r.read16(pit_channel1_port), 63u);
  r.advance(63);  // exactly one full cycle: back to the top
  EXPECT_EQ(r.read16(pit_channel1_port), 100u);

  // No IRQ0 was ever raised by any of this, and nothing was logged.
  EXPECT_FALSE(r.pc().stopped());
  EXPECT_TRUE(r.log.notices.empty());
}

// --- The count-latch command ------------------------------------------------

TEST(pit_latch, freezes_the_count_while_counting_continues_underneath) {
  rig r;
  r.program_channel(pit_channel1_port, pit_access::both, pit_mode::mode2, 100);
  r.advance(10);

  r.latch(pit_channel1_port);
  r.advance(20);  // the live count keeps moving; the latch must not

  EXPECT_EQ(r.read16(pit_channel1_port), 90u);

  // Unlatched again after both bytes were read: the next read is live.
  EXPECT_EQ(r.read16(pit_channel1_port), 70u);
}

TEST(pit_latch, a_second_latch_before_the_first_is_read_is_ignored) {
  rig r;
  r.program_channel(pit_channel1_port, pit_access::both, pit_mode::mode2, 100);
  r.advance(10);
  r.latch(pit_channel1_port);  // latches 90

  r.advance(5);
  r.latch(pit_channel1_port);  // ignored: 90 is still unread

  EXPECT_EQ(r.read16(pit_channel1_port), 90u);
}

// --- The 0-means-65536 divisor ---------------------------------------------

TEST(pit_divisor, zero_means_65536) {
  rig r;
  r.program_channel(pit_channel1_port, pit_access::both, pit_mode::mode2, 0);

  // A full-scale register reads back as 0000h the instant it is loaded —
  // the same 16 bits mean "just loaded 65536" and "just loaded 0", which
  // is the whole reason the rule exists.
  EXPECT_EQ(r.read16(pit_channel1_port), 0u);
  r.advance(1);
  EXPECT_EQ(r.read16(pit_channel1_port), 0xFFFFu);
}

// --- The channel-2 gate line ------------------------------------------------

TEST(pit_gate, low_freezes_channel2_and_high_resumes_it_in_mode0) {
  rig r;
  r.program_channel(pit_channel2_port, pit_access::both, pit_mode::mode0, 100);
  r.advance(20);
  ASSERT_EQ(r.read16(pit_channel2_port), 80u);

  r.timer.set_gate2(false);
  r.advance(1000);  // however long the gate stays low, the count holds
  EXPECT_EQ(r.read16(pit_channel2_port), 80u);

  r.timer.set_gate2(true);  // mode 0: resumes, does not reload
  EXPECT_EQ(r.read16(pit_channel2_port), 80u);
  r.advance(5);
  EXPECT_EQ(r.read16(pit_channel2_port), 75u);
}

TEST(pit_gate, a_rising_edge_reloads_channel2_in_mode2) {
  rig r;
  r.program_channel(pit_channel2_port, pit_access::both, pit_mode::mode2, 100);
  r.advance(30);
  ASSERT_EQ(r.read16(pit_channel2_port), 70u);

  r.timer.set_gate2(false);
  r.timer.set_gate2(true);  // modes 2/3: the rising edge is a reload

  EXPECT_EQ(r.read16(pit_channel2_port), 100u);
}

// --- The exit criterion -----------------------------------------------------

// A program hooks INT 1Ch and counts N real, PIT-driven ticks over an
// exactly computable virtual span — the fact this whole issue exists to
// make true. Mirrors service_floor_test.cpp's own exit-criterion test,
// with a real PIT and PIC behind the interrupt instead of a hand-raised
// one.
TEST(pit_exit_criterion, a_program_hooks_int_1ch_and_counts_pit_driven_ticks) {
  rig r;

  //   0000  31 C0              XOR AX, AX
  //   0002  8E D8              MOV DS, AX
  //   0004  C7 06 70 00 17 00  MOV word [0070], 0017h   ; 1Ch * 4, to
  //                                                      ; this program's
  //                                                      ; own hook below
  //   000A  8C 0E 72 00        MOV [0072], CS
  //   000E  31 ED              XOR BP, BP
  //   0010  FB                 STI
  //   0011  83 FD 05           CMP BP, 5                ; wait:
  //   0014  72 FB              JB wait
  //   0016  F4                 HLT
  //   0017  45                 INC BP                   ; the tick handler
  //   0018  CF                 IRET
  const std::initializer_list<std::uint8_t> program{
      0x31, 0xC0, 0x8E, 0xD8, 0xC7, 0x06, 0x70, 0x00, 0x17,
      0x00, 0x8C, 0x0E, 0x72, 0x00, 0x31, 0xED, 0xFB, 0x83,
      0xFD, 0x05, 0x72, 0xFB, 0xF4, 0x45, 0xCF};
  std::uint32_t at = cpu::physical_address(code_segment, 0);
  for (const std::uint8_t byte : program) {
    r.pc().memory().ram()[at] = byte;
    ++at;
  }
  r.pc().processor().regs().ip = 0;

  // Comfortably larger than the default handler's own chain (delivery,
  // handler body, the chained INT 1Ch, its stub, the continuation's EOI
  // — a double-digit number of steps): with a divisor close to that
  // cost, an edge can become due while the CPU is still mid-chain on the
  // one before it, or before the loop below has had a spin-loop step to
  // notice BP has already reached the threshold and fall through to
  // HLT — not a bug in either the PIT or the loop, just too little idle
  // time between ticks for a test that wants an exact count.
  constexpr ticks divisor = 50;
  constexpr ticks wanted_ticks = 5;
  r.program_channel(pit_channel0_port, pit_access::lsb, pit_mode::mode2,
                    static_cast<std::uint16_t>(divisor));

  // Stepped rather than run(): HLT resumes and re-halts on every further
  // tick indefinitely (channel 0 is periodic), so the loop has to stop
  // at the program's own first HLT rather than at a fixed virtual time —
  // exactly the loop service_floor_test.cpp's own exit-criterion test
  // uses, for the identical reason.
  unsigned steps = 0;
  while (!r.pc().processor().halted() && !r.pc().stopped() && steps < 4000) {
    r.pc().step();
    ++steps;
  }

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.pc().processor().regs()[cpu::reg16::bp], wanted_ticks);
  EXPECT_EQ(r.bios_ticks(), wanted_ticks);

  // Exact and computable: `divisor` ticks per interrupt, `wanted_ticks`
  // of them, is the least virtual time this could have taken, and the
  // program does nothing but wait for exactly that between them.
  EXPECT_GE(r.pc().time(), divisor * wanted_ticks);
}

}  // namespace
}  // namespace amberfolio::machine
