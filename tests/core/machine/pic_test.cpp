// SPDX-License-Identifier: AGPL-3.0-only
//
// The 8259 on its own: the stock BIOS-shaped ICW sequence and what gets
// refused instead of guessed at, IRQ0's request/service/acknowledge
// cycle through masking and EOI, and the one thing this controller
// exists to prove — that a raised IRQ0 reaches the CPU on the vector the
// init sequence gave it.
//
// pit_test.cpp is the PIT's own file; the two meet only in the divisor-
// to-period and 18.2 Hz tests there, which need a real edge source to be
// worth anything. Everything about the controller's own state machine is
// exercised here against `raise_irq0()` called directly, which is what
// keeps this file honest about what belongs to the 8259 rather than to
// whatever happens to be driving it.

#include "amberfolio/machine/pic.h"

#include <cstdint>
#include <memory>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/service_floor.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine::pic {
namespace {

using test::recording_diagnostics;

/// A machine and the controller attached to it, on the heap because a
/// machine has a megabyte inside it.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)), irq(*box) {
    box->attach(irq);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  /// The literal stock BIOS sequence: cascade mode, vector base 8, the
  /// unused ICW3 a real BIOS always sends, 8086-mode ICW4.
  void stock_init_cascade() const {
    box->write_port8(master_command_port, icw1_edge_cascade_icw4);
    box->write_port8(data_port, expected_vector_base);
    box->write_port8(data_port, 0x04);  // ICW3: IRQ2 has a slave (ignored)
    box->write_port8(data_port, icw4_8086_mode);
  }

  /// The shorter, single-mode form: identical statement about this
  /// controller's own one IRQ line, no ICW3 step.
  void stock_init_single() const {
    box->write_port8(master_command_port, icw1_edge_single_icw4);
    box->write_port8(data_port, expected_vector_base);
    box->write_port8(data_port, icw4_8086_mode);
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  controller irq;
};

// --- Initialization ------------------------------------------------------

TEST(pic_init, accepts_the_cascade_shaped_stock_sequence) {
  rig r;
  r.stock_init_cascade();

  // Ready: an unmasked IRQ0 is deliverable.
  r.irq.raise_irq0();
  EXPECT_TRUE(r.pc().processor().intr_pending());
  EXPECT_FALSE(r.pc().stopped());
}

TEST(pic_init, accepts_the_single_mode_stock_sequence) {
  rig r;
  r.stock_init_single();

  r.irq.raise_irq0();
  EXPECT_TRUE(r.pc().processor().intr_pending());
  EXPECT_FALSE(r.pc().stopped());
}

TEST(pic_init, refuses_level_triggered_mode) {
  rig r;
  r.pc().write_port8(master_command_port, 0x19);  // LTIM=1, single, ICW4

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
  EXPECT_EQ(r.pc().stop().at, master_command_port);
}

TEST(pic_init, refuses_an_icw1_with_no_icw4_to_follow) {
  rig r;
  r.pc().write_port8(master_command_port, 0x12);  // single, IC4=0

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
}

TEST(pic_init, refuses_a_vector_base_other_than_8) {
  rig r;
  r.pc().write_port8(master_command_port, icw1_edge_single_icw4);
  r.pc().write_port8(data_port, 0x70);  // the real slave's base, not this one's

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
  EXPECT_EQ(r.pc().stop().at, data_port);
}

TEST(pic_init, refuses_an_icw4_that_asks_for_auto_eoi) {
  rig r;
  r.pc().write_port8(master_command_port, icw1_edge_single_icw4);
  r.pc().write_port8(data_port, expected_vector_base);
  r.pc().write_port8(data_port, 0x03);  // 8086 mode + AEOI

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
}

TEST(pic_init, ignores_the_unused_icw3_whatever_it_says) {
  rig r;
  r.pc().write_port8(master_command_port, icw1_edge_cascade_icw4);
  r.pc().write_port8(data_port, expected_vector_base);
  r.pc().write_port8(data_port, 0xFF);  // any byte at all — no slave reads it
  r.pc().write_port8(data_port, icw4_8086_mode);

  EXPECT_FALSE(r.pc().stopped());
  r.irq.raise_irq0();
  EXPECT_TRUE(r.pc().processor().intr_pending());
}

TEST(pic_data_port, refuses_a_write_before_any_icw1_has_arrived) {
  rig r;
  r.pc().write_port8(data_port, 0x00);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
  EXPECT_EQ(r.pc().stop().at, data_port);
}

// --- The command port after init -----------------------------------------

TEST(pic_command_port, refuses_anything_but_a_non_specific_eoi) {
  rig r;
  r.stock_init_single();

  r.pc().write_port8(master_command_port, 0x60);  // specific EOI, IRQ0

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
  EXPECT_EQ(r.pc().stop().at, master_command_port);
}

TEST(pic_command_port, refuses_a_status_read_of_20h) {
  rig r;
  r.stock_init_single();

  EXPECT_EQ(r.pc().read_port8(master_command_port), open_bus_value);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_device);
}

// --- The mask register (OCW1) ---------------------------------------------

TEST(pic_mask, reads_back_exactly_what_was_written) {
  rig r;
  r.stock_init_single();

  r.pc().write_port8(data_port, 0xA5);

  EXPECT_EQ(r.pc().read_port8(data_port), 0xA5);
}

TEST(pic_mask, holds_a_masked_irq0_pending_and_delivers_it_on_unmask) {
  rig r;
  r.stock_init_single();
  r.pc().write_port8(data_port, 0x01);  // mask IRQ0

  r.irq.raise_irq0();
  EXPECT_FALSE(r.pc().processor().intr_pending());

  r.pc().write_port8(data_port, 0x00);  // unmask
  EXPECT_TRUE(r.pc().processor().intr_pending());
}

TEST(pic_mask, coalesces_edges_that_arrive_while_masked) {
  rig r;
  r.stock_init_single();
  r.pc().write_port8(data_port, 0x01);  // mask IRQ0

  r.irq.raise_irq0();
  r.irq.raise_irq0();
  r.irq.raise_irq0();

  r.pc().write_port8(data_port, 0x00);  // unmask
  ASSERT_TRUE(r.pc().processor().intr_pending());

  // Acknowledge the one request the three edges became...
  r.pc().processor().clear_intr();
  r.pc().write_port8(master_command_port, end_of_interrupt);

  // ...and there is nothing left behind it: three edges made one
  // outstanding request, not three, which is exactly what a single IRR
  // bit means (this file's top comment).
  EXPECT_FALSE(r.pc().processor().intr_pending());
}

// --- EOI and the service cycle ---------------------------------------------

TEST(pic_service_cycle, holds_a_second_edge_until_the_first_is_acknowledged) {
  rig r;
  r.stock_init_single();

  r.irq.raise_irq0();
  ASSERT_TRUE(r.pc().processor().intr_pending());

  // A second edge before the first has even been delivered: it is
  // coalesced onto the same still-outstanding request, not queued.
  r.irq.raise_irq0();
  EXPECT_TRUE(r.pc().processor().intr_pending());
}

TEST(pic_service_cycle, requires_eoi_before_the_next_delivery) {
  rig r;
  r.stock_init_single();

  r.irq.raise_irq0();
  ASSERT_TRUE(r.pc().processor().intr_pending());

  // Deliver it: the processor's own model drops the level the instant it
  // takes the vector (processor.h), which is what this simulates without
  // needing a full instruction stream — the point under test is the
  // controller's ISR bit, not delivery itself.
  r.pc().processor().clear_intr();

  // A fresh edge, no EOI yet: still in service, so it is held rather
  // than re-requested.
  r.irq.raise_irq0();
  EXPECT_FALSE(r.pc().processor().intr_pending());

  r.pc().write_port8(master_command_port, end_of_interrupt);
  EXPECT_TRUE(r.pc().processor().intr_pending());
}

// --- The exit-relevant fact: the right vector, end to end -----------------

TEST(pic_delivery, an_unmasked_irq0_reaches_the_cpu_on_the_init_vector_base) {
  rig r;
  r.stock_init_single();
  r.pc().processor().reset();
  r.pc().processor().regs().set_flag(cpu::flag::if_, true);

  r.irq.raise_irq0();
  ASSERT_EQ(r.pc().step(), cpu::step_status::serviced);

  // Landed on the stub for vector 8 — service_floor.h's `service::
  // timer_vector` — which is what `expected_vector_base` says this
  // controller's IRQ0 has to mean.
  EXPECT_EQ(r.pc().processor().regs()[cpu::sreg::cs], service::stub_segment);
  EXPECT_EQ(r.pc().processor().regs().ip,
            service::stub_offset(expected_vector_base));
}

}  // namespace
}  // namespace amberfolio::machine::pic
