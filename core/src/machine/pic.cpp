// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/pic.h"

#include <cstdint>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/machine/machine.h"

namespace amberfolio::machine::pic {
namespace {

/// ICW1's bits that matter in 8086 mode. The address bits (D7-D5) and
/// ADI (D2) are 8080/8085-only and are don't-cares here — decoding just
/// these three is what lets both the historical cascade byte and the
/// single-mode one through without a byte-for-byte comparison that would
/// reject either for reasons that have nothing to do with what this
/// controller can and cannot do (pic.h).
constexpr std::uint8_t icw1_level_triggered = 0x08;
constexpr std::uint8_t icw1_single = 0x02;
constexpr std::uint8_t icw1_icw4_follows = 0x01;

/// The one IMR bit this controller ever consults.
constexpr std::uint8_t irq0_mask_bit = 0x01;

}  // namespace

controller::controller(machine& box) noexcept : box_(&box) {}

void controller::reset() {
  state_ = init_state::uninitialized;
  expects_icw3_ = false;
  vector_base_ = 0;
  imr_ = 0;
  irr0_ = false;
  isr0_ = false;
}

std::uint8_t controller::read_port(std::uint16_t port) {
  if (port == data_port) {
    return imr_;
  }

  // 20h: OCW3 status reads (poll, IRR, ISR) are not implemented — nothing
  // in PLAN.md's device list reads them, and answering with a guessed
  // byte is exactly what "log, don't fake" rules out.
  report_fault(port);
  return open_bus_value;
}

void controller::write_port(std::uint16_t port, std::uint8_t value) {
  if (port == data_port) {
    write_data(value);
    return;
  }

  // 20h. ICW1 can restart initialization from any state — a real 8259A
  // accepts it at any time — so it is checked before anything else.
  if ((value & 0x10) != 0) {
    begin_init(value);
    return;
  }

  if (state_ != init_state::ready) {
    // A command before the stock init sequence has finished. Nothing
    // sensible to do with it: the controller has no vector base yet to
    // deliver anything with.
    report_fault(port, value);
    return;
  }

  if (value != end_of_interrupt) {
    // Specific EOI, rotate-on-EOI, OCW3's poll/status selects — none of
    // them are implemented (pic.h: "no priority rotation, no special
    // mask, no slave").
    report_fault(port, value);
    return;
  }

  isr0_ = false;
  try_deliver();
}

void controller::begin_init(std::uint8_t icw1) noexcept {
  const bool level_triggered = (icw1 & icw1_level_triggered) != 0;
  const bool single = (icw1 & icw1_single) != 0;
  const bool icw4_follows = (icw1 & icw1_icw4_follows) != 0;

  if (level_triggered || !icw4_follows) {
    // The PC wires this chip edge-triggered, and this controller only
    // understands the 8086-mode behaviour ICW4 selects — without one
    // there is nothing to fall back to that is not a guess.
    report_fault(master_command_port, icw1);
    return;
  }

  expects_icw3_ = !single;
  irr0_ = false;
  isr0_ = false;
  imr_ = 0;
  state_ = init_state::expect_icw2;
}

void controller::write_data(std::uint8_t value) noexcept {
  switch (state_) {
    case init_state::uninitialized:
      // A mask write, or a stray OCW-shaped byte, before ICW1 has ever
      // arrived. Real hardware would happily store it — the IMR is not
      // tied to the init state machine — but this controller has no
      // vector base yet, so accepting it would only be pretending the
      // controller is further along than it is.
      report_fault(data_port, value);
      return;
    case init_state::expect_icw2:
      vector_base_ = value;
      if (vector_base_ != expected_vector_base) {
        report_fault(data_port, value);
        return;
      }
      state_ =
          expects_icw3_ ? init_state::expect_icw3 : init_state::expect_icw4;
      return;
    case init_state::expect_icw3:
      // Which IRQs have a slave attached. Meaningless here — there is no
      // slave — but the stock sequence always sends it, and consuming it
      // costs nothing (pic.h).
      state_ = init_state::expect_icw4;
      return;
    case init_state::expect_icw4:
      if (value != icw4_8086_mode) {
        report_fault(data_port, value);
        return;
      }
      state_ = init_state::ready;
      // A request may already have been raised while uninitialized
      // (pit.h does not wait for the controller); this is the first
      // moment it can go out.
      try_deliver();
      return;
    case init_state::ready:
      imr_ = value;
      try_deliver();
      return;
  }
}

void controller::raise_irq0() noexcept {
  irr0_ = true;
  try_deliver();
}

void controller::try_deliver() noexcept {
  if (state_ != init_state::ready || isr0_ || !irr0_) {
    return;
  }
  if ((imr_ & irq0_mask_bit) != 0) {
    return;
  }

  irr0_ = false;
  isr0_ = true;
  box_->processor().raise_intr(vector_base_);
}

}  // namespace amberfolio::machine::pic
