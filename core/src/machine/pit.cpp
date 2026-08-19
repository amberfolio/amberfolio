// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/pit.h"

#include <cstdint>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"

namespace amberfolio::machine {
namespace {

/// The control word's four fields (43h), as the bit positions the Intel
/// 8253 datasheet gives them.
constexpr std::uint8_t control_select_shift = 6;
constexpr std::uint8_t control_access_shift = 4;
constexpr std::uint8_t control_mode_shift = 1;
constexpr std::uint8_t control_field_mask = 0x03;
constexpr std::uint8_t control_mode_mask = 0x07;
constexpr std::uint8_t control_bcd_bit = 0x01;

/// SC=11: the 8254's read-back command. This machine has an 8253
/// (PLAN.md §3), which has no such thing.
constexpr std::uint8_t select_read_back = 0x03;

}  // namespace

// --- pit_channel ---------------------------------------------------------

void pit_channel::reset() noexcept {
  mode_ = pit_mode::none;
  access_ = pit_access::none;
  awaiting_msb_ = false;
  partial_write_ = 0;
  next_read_is_msb_ = false;
  active_divisor_ = 0;
  active_since_ = 0;
  has_pending_ = false;
  pending_divisor_ = 0;
  gate_ = true;
  gate_paused_at_ = 0;
  latched_ = false;
  latched_value_ = 0;
  box_->deadlines().disarm(*this);
}

void pit_channel::write_control(pit_access access, pit_mode mode) noexcept {
  access_ = access;
  mode_ = mode;
  awaiting_msb_ = false;
  partial_write_ = 0;
  next_read_is_msb_ = false;
  latched_ = false;
  active_divisor_ = 0;
  has_pending_ = false;
  box_->deadlines().disarm(*this);
}

void pit_channel::latch() noexcept {
  if (latched_) {
    // Ignored, not restarted: whatever is mid-read stays mid-read (this
    // file's top comment, and the Intel 8253 datasheet's own rule).
    return;
  }

  const ticks at = now();
  catch_up(at);
  latched_value_ = static_cast<std::uint16_t>(live_count(at));
  latched_ = true;
  next_read_is_msb_ = false;
}

std::uint8_t pit_channel::read_data() noexcept {
  std::uint16_t value;
  if (latched_) {
    value = latched_value_;
  } else {
    const ticks at = now();
    catch_up(at);
    value = static_cast<std::uint16_t>(live_count(at));
  }

  // A latch is consumed by the read that completes its sequence — one
  // read for LSB-only or MSB-only, two for both — whichever access mode
  // is in force; `sequence_complete` is that fact, shared by the latched
  // and the live case alike.
  std::uint8_t result = 0;
  bool sequence_complete = true;
  switch (access_) {
    case pit_access::lsb:
      result = static_cast<std::uint8_t>(value);
      break;
    case pit_access::msb:
      result = static_cast<std::uint8_t>(value >> 8u);
      break;
    case pit_access::both:
      if (!next_read_is_msb_) {
        result = static_cast<std::uint8_t>(value);
        next_read_is_msb_ = true;
        sequence_complete = false;
      } else {
        result = static_cast<std::uint8_t>(value >> 8u);
        next_read_is_msb_ = false;
      }
      break;
    case pit_access::none:
      return 0;
  }

  if (sequence_complete) {
    latched_ = false;
  }
  return result;
}

void pit_channel::write_data(std::uint8_t byte) noexcept {
  if (mode_ == pit_mode::none || access_ == pit_access::none) {
    // No control word has ever selected this channel. A real 8253 has
    // no meaningful reaction to this either — nothing is programmed yet
    // for the byte to mean anything against.
    return;
  }

  std::uint16_t assembled = 0;
  switch (access_) {
    case pit_access::lsb:
      assembled = byte;
      break;
    case pit_access::msb:
      assembled = static_cast<std::uint16_t>(byte) << 8u;
      break;
    case pit_access::both:
      if (!awaiting_msb_) {
        partial_write_ = byte;
        awaiting_msb_ = true;
        return;
      }
      assembled = static_cast<std::uint16_t>(
          partial_write_ | (static_cast<std::uint16_t>(byte) << 8u));
      awaiting_msb_ = false;
      break;
    case pit_access::none:
      return;
  }

  const std::uint32_t divisor = divisor_of(assembled);
  const ticks at = now();

  if (mode_ == pit_mode::mode0) {
    // A new count always restarts immediately, whatever was in flight.
    start_cycle(at, divisor);
    return;
  }

  // Modes 2 and 3: the first load after a control word starts right
  // away; a rewrite of a channel already counting is deferred to that
  // cycle's own boundary (this file's top comment).
  catch_up(at);
  if (active_divisor_ == 0) {
    start_cycle(at, divisor);
  } else {
    pending_divisor_ = divisor;
    has_pending_ = true;
  }
}

void pit_channel::set_gate(bool level) noexcept {
  if (level == gate_) {
    return;
  }

  const ticks at = now();

  if (!level) {
    // Falling edge: catch up first so the frozen snapshot `live_count()`
    // answers from now on is correct, then stop the deadline — nothing
    // will happen while nothing is counting.
    catch_up(at);
    gate_ = false;
    gate_paused_at_ = at;
    box_->deadlines().disarm(*this);
    return;
  }

  gate_ = true;

  if (mode_ == pit_mode::mode0) {
    // Resume, not reload: shift the reference tick forward by exactly
    // how long the pause lasted, so `now - active_since_` recovers the
    // value it held the moment the gate went low.
    active_since_ += (at - gate_paused_at_);
    if (active_divisor_ != 0) {
      box_->deadlines().arm(*this, active_since_ + active_divisor_);
    }
    return;
  }

  // Modes 2 and 3: the rising edge is itself a reload, exactly like a
  // natural terminal count (Intel 8253 datasheet).
  if (active_divisor_ != 0) {
    start_cycle(at, has_pending_ ? pending_divisor_ : active_divisor_);
  }
}

void pit_channel::on_deadline(ticks due) {
  // `due` is exactly `active_since_ + active_divisor_` at the moment
  // this was armed (scheduler.h: a handler is called with the tick it
  // asked for), so there is nothing to reconcile against "now" here.
  if (irq_ != nullptr) {
    irq_->raise_irq0();
  }

  if (mode_ == pit_mode::mode0) {
    // One-shot: the edge fired once, and nothing rearms it. The count
    // itself keeps running past zero (`live_count()` models the 16-bit
    // wrap); only the output edge was ever a single event.
    return;
  }

  start_cycle(due, has_pending_ ? pending_divisor_ : active_divisor_);
}

ticks pit_channel::now() const noexcept { return box_->time(); }

ticks pit_channel::reference_tick(ticks at) const noexcept {
  return gate_ ? at : gate_paused_at_;
}

void pit_channel::catch_up(ticks at) noexcept {
  if (mode_ != pit_mode::mode2 && mode_ != pit_mode::mode3) {
    return;
  }
  if (!has_pending_ || active_divisor_ == 0) {
    return;
  }

  const ticks ref = reference_tick(at);
  const ticks boundary = active_since_ + active_divisor_;
  if (ref < boundary) {
    return;
  }

  start_cycle(boundary, pending_divisor_);
}

void pit_channel::start_cycle(ticks at, std::uint32_t divisor) noexcept {
  active_since_ = at;
  active_divisor_ = divisor;
  has_pending_ = false;

  if (gate_) {
    box_->deadlines().arm(*this, at + divisor);
  } else {
    box_->deadlines().disarm(*this);
  }
}

std::uint32_t pit_channel::live_count(ticks at) const noexcept {
  if (active_divisor_ == 0) {
    return 0;
  }

  const ticks elapsed = reference_tick(at) - active_since_;

  if (mode_ != pit_mode::mode0) {
    // Periodic: `catch_up()` has already folded in the one reload that
    // could be pending, so this is a plain wrap against the divisor
    // currently in force.
    const auto position = static_cast<std::uint32_t>(elapsed % active_divisor_);
    return active_divisor_ - position;
  }

  // Mode 0 does not reload itself: it counts down to zero once, then
  // keeps counting through the 16-bit wrap forever, until rewritten.
  if (elapsed <= active_divisor_) {
    return active_divisor_ - static_cast<std::uint32_t>(elapsed);
  }
  const std::uint64_t past = elapsed - active_divisor_;
  return (0x10000u - static_cast<std::uint32_t>(past % 0x10000u)) % 0x10000u;
}

// --- pit -------------------------------------------------------------------

pit::pit(machine& box, pic::controller& irq0) noexcept
    : channel0_(box, &irq0), channel1_(box, nullptr), channel2_(box, nullptr) {}

void pit::reset() {
  channel0_.reset();
  channel1_.reset();
  channel2_.reset();
}

std::uint8_t pit::read_port(std::uint16_t port) {
  switch (port) {
    case pit_channel0_port:
      return channel0_.read_data();
    case pit_channel1_port:
      return channel1_.read_data();
    case pit_channel2_port:
      return channel2_.read_data();
    default:
      // 43h, the control word register: write-only on real hardware —
      // nothing drives the bus on a read of it, so this floats exactly
      // as an unclaimed port would (device.h).
      return open_bus_value;
  }
}

void pit::write_port(std::uint16_t port, std::uint8_t value) {
  switch (port) {
    case pit_channel0_port:
      channel0_.write_data(value);
      return;
    case pit_channel1_port:
      channel1_.write_data(value);
      return;
    case pit_channel2_port:
      channel2_.write_data(value);
      return;
    default:
      write_control(value);
      return;
  }
}

void pit::write_control(std::uint8_t value) {
  const auto select = static_cast<std::uint8_t>(
      (value >> control_select_shift) & control_field_mask);
  const auto access = static_cast<std::uint8_t>(
      (value >> control_access_shift) & control_field_mask);
  const auto mode_bits = static_cast<std::uint8_t>(
      (value >> control_mode_shift) & control_mode_mask);
  const bool bcd = (value & control_bcd_bit) != 0;

  if (select == select_read_back) {
    report_fault(pit_control_port, value);
    return;
  }

  pit_channel* channel = &channel2_;
  switch (select) {
    case 0:
      channel = &channel0_;
      break;
    case 1:
      channel = &channel1_;
      break;
    default:
      break;
  }

  if (access == 0) {
    // RW=00: the count-latch command. Mode and BCD are not meaningful
    // here and are not even validated — a latch does not touch either.
    channel->latch();
    return;
  }

  if (bcd) {
    report_fault(pit_control_port, value);
    return;
  }

  pit_mode mode;
  switch (mode_bits) {
    case 0:
      mode = pit_mode::mode0;
      break;
    case 2:
    case 6:  // M2 duplicates mode 2 (Intel 8253 datasheet: the mode
             // field's high bit is a don't-care for modes 2 and 3).
      mode = pit_mode::mode2;
      break;
    case 3:
    case 7:
      mode = pit_mode::mode3;
      break;
    default:
      report_fault(pit_control_port, value);
      return;
  }

  channel->write_control(static_cast<pit_access>(access), mode);
}

}  // namespace amberfolio::machine
