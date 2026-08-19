// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/speaker.h"

#include <cstdint>

#include "amberfolio/machine/machine.h"

namespace amberfolio::machine {

speaker::speaker(machine& box, pit& timer) noexcept
    : box_(&box), timer_(&timer) {
  timer_->set_channel2_observer(*this);
}

void speaker::reset() {
  control_ = 0;
  // Channel 2's gate is wired to this device's bit 0, not tied high the
  // way `pit_channel::reset()` otherwise leaves every channel (this
  // file's top comment on `reset()`).
  timer_->set_gate2(false);
  last_level_ = false;
  // No `resync()` needed: with both bits back at their power-on 0,
  // `speaker_level()` is already the `false` `last_level_` starts at, and
  // nothing is armed on the scheduler across a reset for this device to
  // disarm (`machine::reset()` calls `deadlines().disarm_all()` before
  // any device's own `reset()` runs).
}

std::uint8_t speaker::read_port(std::uint16_t) {
  // Bits 0-1: whatever was last written. Every other bit — including 4
  // and 7, documented as static — reads 0: this device never stores them
  // (this file's top comment).
  return control_;
}

void speaker::write_port(std::uint16_t, std::uint8_t value) {
  if ((value & undocumented_bits) != 0) {
    report_fault(speaker_port, value);
    return;
  }

  control_ = static_cast<std::uint8_t>(value & (gate_bit | data_enable_bit));
  timer_->set_gate2((value & gate_bit) != 0);
  resync(now());
}

void speaker::on_deadline(ticks due) { resync(due); }

void speaker::on_channel2_changed(ticks at) noexcept { resync(at); }

ticks speaker::now() const noexcept { return box_->time(); }

bool speaker::speaker_level(ticks at) const noexcept {
  return (control_ & data_enable_bit) != 0 && timer_->channel2_output(at);
}

void speaker::resync(ticks at) {
  const bool level = speaker_level(at);
  if (level != last_level_) {
    box_->audio().publish(at, level);
    last_level_ = level;
  }

  // Only channel 2's own arithmetic can move the answer on its own; if
  // the data enable is off the AND is pinned false regardless of what
  // channel 2 does, so there is nothing worth waking up for.
  const ticks next = (control_ & data_enable_bit) != 0
                         ? timer_->channel2_next_output_change(at)
                         : never;
  if (next == never) {
    box_->deadlines().disarm(*this);
  } else {
    box_->deadlines().arm(*this, next);
  }
}

}  // namespace amberfolio::machine
