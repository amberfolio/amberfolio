// SPDX-License-Identifier: AGPL-3.0-only
//
// M2-D1 (#46): the minimal 8259A this machine needs — one controller, no
// slave, existing for exactly one reason: the timer interrupt
// service_floor.h already delivers has to come from somewhere. PLAN.md
// §3 does not name the 8259 at all; it is infrastructure the PIT's IRQ0
// needs, not a device the game family programs for its own sake the way
// it programs the PIT or the EGA.
//
// "Minimal" is a scope, not an excuse: what is here is exact. What is
// left out is left out because PLAN.md §3's discipline says an
// unimplemented register is a loud log line and a clean stop, never a
// silently faked answer — never because faking it looked cheaper.
//
//
// Why this is `pic::controller` and not `class pic`
// --------------------------------------------------
//
// service_floor.h already opened a namespace called `pic` for the two
// facts the default timer handler needed before this device existed —
// the command port and the EOI byte. A device class cannot share that
// name with the namespace it lives in, and the constants belong next to
// it rather than duplicated, so the class is `pic::controller` and this
// file reopens the same namespace to add it. `service_floor.cpp`'s
// `pic::master_command_port` and `pic::end_of_interrupt` are untouched.
//
//
// One controller, no slave, no priority scheme
// ----------------------------------------------
//
// A real PC wires two 8259As, master and slave, and the slave does not
// exist in this machine — nothing in PLAN.md's device list needs IRQ8
// upward, and the scope this issue was given is explicit: "no priority
// rotation, no special mask, no slave." IRQ1 (the keyboard) stays
// unwired for the same reason M2-D8's own issue gives: keyboard input
// goes through the BIOS buffer, and a program that hooks INT 09h
// directly is an M3 discovery, not an M2 one.
//
// So there is exactly one interrupt request line this controller ever
// sees raised — IRQ0, PIT channel 0's output edge (pit.h) — and exactly
// one behaviour worth building for the request/service/acknowledge
// cycle: request, deliver if nothing is in the way, hold if something
// is, and release on a plain EOI. IRR and ISR are each one bit because
// there is only ever one line to have an opinion about.
//
//
// Simplification: "in service" starts at hand-off, not at INTA
// ---------------------------------------------------------------
//
// A real 8259A only sets its in-service bit at the CPU's interrupt
// acknowledge cycle — if IF stays clear for a long time after the
// request is raised, the request sits in IRR, not ISR, however many
// more edges arrive in the meantime. `cpu::processor` does not expose
// that moment: `raise_intr()` asserts a level and the processor's own
// acknowledgement (auto-clearing it on delivery) is internal
// bookkeeping this controller cannot observe (processor.h).
//
// So `try_deliver()` sets `isr0_` the moment it calls `raise_intr()`,
// not the moment the CPU actually takes the vector. The two diverge only
// while IF is clear and a request is outstanding, and in that window
// every observable fact still comes out the same: a second edge before
// delivery just leaves the level asserted (`raise_intr` on an already
// asserted line changes nothing), so it is coalesced exactly as it
// would be coalesced by real IRR/ISR bookkeeping — this controller never
// queues more than one pending request, and neither does the real chip.
// The two models can only disagree about *when* ISR became true, and
// nothing in this machine can ask that question.
//
//
// The stock BIOS-shaped init sequence, and nothing else
// ---------------------------------------------------------
//
// A real ROM BIOS's POST programs the master 8259A once, at boot, before
// DOS or the game ever runs — and this machine has no ROM BIOS POST to
// run (service_floor.h lays the vector table down as data, not as 8086
// code). So the ICW sequence below has to come from the emulated
// program itself, or nothing ever leaves `init_state::uninitialized` and
// IRQ0 is never deliverable. Real DOS-era software occasionally does
// reprogram the master 8259A defensively — which is the whole reason
// this has to parse an incoming ICW sequence rather than assume one.
//
// "Stock BIOS-shaped" names the literal historical sequence: cascade
// mode with an ICW4 to follow (`icw1_edge_cascade_icw4`, 11h), vector
// base 8 (`expected_vector_base`), an ICW3 (unused — there is no slave,
// but a real BIOS sends one because it does not know that, and this
// controller consumes and ignores it rather than refuse a byte that
// changes nothing observable), and ICW4 selecting 8086 mode and nothing
// else (`icw4_8086_mode`, 01h). `icw1_edge_single_icw4` (13h) is accepted
// too — single/no-slave mode skips the ICW3 step — because it says the
// identical thing about this controller's own one IRQ line; a real dual-
// chip PC never sends it, so it exists here mainly for a test that wants
// the shorter sequence.
//
// Every other ICW1 bit is decoded rather than compared byte-for-byte
// (LTIM, SNGL, IC4), because those bits really are the ones an 8086-mode
// PC cares about and the rest — the interrupt-vector-address bits that
// only mean anything in 8080/8085 mode — are documented don't-cares.
// Comparing the whole byte against a fixed constant would make this
// controller reject a real BIOS's exact bytes for reasons that have
// nothing to do with what it can and cannot do.
//
// What gets rejected, always with `report_fault()` (device.h, #65) and
// never with an invented answer: level-triggered mode (`LTIM=1` — the
// PC wires edge-triggered), no ICW4 (`IC4=0` — this controller only
// understands 8086 mode, which ICW4 is what selects), a vector base
// other than 8, an ICW4 that asks for auto-EOI, buffered mode or special
// fully-nested mode (this controller answers to a plain EOI and nothing
// fancier), the 8254 read-back command (SC=11 on OCW3 addressing — this
// is an 8253-family machine, PLAN.md §3), any OCW2 other than
// non-specific EOI (rotation and specific EOI are the "no priority
// rotation" half of scope), and any OCW3 (status/poll reads of IRR or
// ISR — nothing in this project's plan reads them).
//
//
// What is deliberately not here
// ------------------------------
//
// IRQ1 and the slave controller: see above. Priority rotation and the
// special mask mode: PLAN.md §3's line for this issue rules them out by
// name, and with one IRQ line there is no priority question to rotate
// in the first place. A read of IRR/ISR through OCW3: nothing in the
// plan needs it, and PLAN.md §3's discipline is exactly for the case
// where "just return something plausible" is on the table and wrong.

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/machine/device.h"
#include "amberfolio/machine/service_floor.h"

namespace amberfolio::machine {

class machine;

namespace pic {

// master_command_port (20h) and end_of_interrupt (20h) already live here,
// added by service_floor.h for the default timer handler's EOI write.

/// The data/mask port, 21h — ICW2 through ICW4 arrive here during init,
/// and OCW1 (the mask register) after it.
inline constexpr std::uint16_t data_port = 0x21;

/// ICW1 with SNGL=0 (cascade — the real, historical PC/AT master byte)
/// and ICW1 with SNGL=1 (single — the identical statement about this
/// controller's own one IRQ line, without the unused ICW3 step). Both
/// select edge-triggered mode and "ICW4 will follow," which this
/// controller requires either way.
inline constexpr std::uint8_t icw1_edge_cascade_icw4 = 0x11;
inline constexpr std::uint8_t icw1_edge_single_icw4 = 0x13;

/// The only ICW2 (vector base) this controller accepts — IRQ0 lands on
/// vector 8, `service::timer_vector` (service_floor.h).
inline constexpr std::uint8_t expected_vector_base = 0x08;

/// The only ICW4 this controller accepts: 8086/88 mode, normal EOI,
/// non-buffered, not specially nested. Nothing else in this five-bit
/// register is supported.
inline constexpr std::uint8_t icw4_8086_mode = 0x01;

/// The 8259A at 20h-21h, minimal on purpose (this file's top comment).
/// One IRQ line — channel 0's output edge, raised through `raise_irq0()`
/// — an init state machine that accepts the stock BIOS-shaped ICW
/// sequence and nothing else, and a mask register that is otherwise
/// exactly what OCW1 makes it.
class controller final : public device {
 public:
  /// `box` must outlive this. Not the fault channel — that is
  /// `report_fault()`, device.h's own door (#65) — but the processor's
  /// `raise_intr()`, for delivering IRQ0.
  explicit controller(machine& box) noexcept;

  static constexpr port_range port_window{.first = master_command_port,
                                          .last = data_port};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&port_window, 1)};
  }

  void reset() override;
  void save_state(state_sink& out) const override;

  [[nodiscard]] std::uint8_t read_port(std::uint16_t port) override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  /// PIT channel 0's output edge (pit.h): IRQ0 requested. Delivered
  /// immediately if the controller is initialized, IRQ0 is unmasked and
  /// nothing is already in service; otherwise the request waits — in
  /// IMR's shadow until unmasked, or behind ISR until the handler's EOI
  /// — for `try_deliver()` to find it later. Never queues more than one
  /// outstanding request, which is what the real chip's one IRR bit
  /// does too.
  void raise_irq0() noexcept;

 private:
  enum class init_state : std::uint8_t {
    /// No ICW1 has ever arrived. IRQ0 can be raised (pit.h does not know
    /// or care whether the controller is ready) but nothing is ever
    /// delivered from here.
    uninitialized,
    expect_icw2,
    /// Only reached when ICW1 selected cascade mode (SNGL=0) — see
    /// `icw1_edge_cascade_icw4` above.
    expect_icw3,
    expect_icw4,
    /// The steady state: 20h takes OCW2 (EOI), 21h takes OCW1 (the
    /// mask), and IRQ0 can be delivered.
    ready,
  };

  /// 20h with bit 4 set: ICW1, which can restart initialization at any
  /// time — the same thing a real 8259A does, and the reason this is not
  /// folded into `write_port`'s ordinary dispatch.
  void begin_init(std::uint8_t icw1) noexcept;

  /// 21h, whatever the current init state makes of it: ICW2, ICW3,
  /// ICW4, or — once `ready` — a plain OCW1 mask write.
  void write_data(std::uint8_t value) noexcept;

  /// IRR/ISR/IMR resolved into "does IRQ0 go out right now": only once,
  /// on whichever of raise_irq0()/EOI/unmask/ICW4-completion could have
  /// changed the answer, never polled.
  void try_deliver() noexcept;

  machine* box_;
  init_state state_{init_state::uninitialized};

  /// Set from ICW1's SNGL bit, consulted once — when the following ICW2
  /// write completes, to know whether an ICW3 is still owed before ICW4.
  bool expects_icw3_{false};

  std::uint8_t vector_base_{0};

  /// OCW1: the mask register. Only bit 0 (IRQ0) is ever meaningful here,
  /// but the whole byte is stored and read back, because a program that
  /// reads its own mask back is entitled to every bit it wrote.
  std::uint8_t imr_{0};

  /// IRR and ISR, one bit each — there is only ever one IRQ line here.
  bool irr0_{false};
  bool isr0_{false};
};

}  // namespace pic
}  // namespace amberfolio::machine
