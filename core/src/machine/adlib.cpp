// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/adlib.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/opl2.h"
#include "amberfolio/machine/state.h"

namespace amberfolio::machine {
namespace {

/// The bits of register 01 and of register 08 that are modelled.
constexpr std::uint8_t test_bits_modelled = opl2_reg::waveform_enable;
constexpr std::uint8_t split_bits_modelled = opl2_reg::note_select;

/// Register 04's bits that mean something when bit 7 is clear.
constexpr std::uint8_t control_bits = adlib_start_timer_1 |
                                      adlib_start_timer_2 | adlib_mask_timer_1 |
                                      adlib_mask_timer_2;

}  // namespace

bool adlib_card::is_register(std::uint8_t address) noexcept {
  if (address == opl2_reg::test || address == opl2_reg::timer_1 ||
      address == opl2_reg::timer_2 || address == opl2_reg::timer_control ||
      address == opl2_reg::keyboard_split || address == opl2_reg::rhythm) {
    return true;
  }
  const std::uint8_t group = address & 0xF0U;
  if (group == opl2_reg::frequency || group == opl2_reg::key_block ||
      group == opl2_reg::feedback) {
    return (address & 0x0FU) < opl2_channels;
  }
  switch (address & 0xE0U) {
    case opl2_reg::flags:
    case opl2_reg::level:
    case opl2_reg::attack_decay:
    case opl2_reg::sustain_release:
    case opl2_reg::waveform:
      return opl2_operator_at(address & 0x1FU) < opl2_operators;
    default:
      return false;
  }
}

void adlib_card::reset() {
  address_ = 0;
  regs_.fill(0);
  flags_ = 0;
  for (timer& t : timers_) {
    t.running = false;
    t.expires = 0;
  }
}

void adlib_card::save_state(state_sink& out) const {
  out.u8(address_);
  out.bytes(regs_);
  out.u8(flags_);
  for (const timer& t : timers_) {
    out.flag(t.running);
    out.u64(t.running ? t.expires : 0);
  }
}

void adlib_card::settle(ticks now) noexcept {
  for (std::size_t i = 0; i < timers_.size(); ++i) {
    timer& t = timers_[i];
    if (!t.running || now < t.expires) {
      continue;
    }
    // It ran out at least once. A masked timer still counts and loads
    // again; it only keeps its flag down.
    if ((regs_[opl2_reg::timer_control] & t.mask_bit) == 0) {
      flags_ |= static_cast<std::uint8_t>(t.flag | adlib_status_any);
    }
    const std::uint8_t loaded =
        regs_[i == 0 ? opl2_reg::timer_1 : opl2_reg::timer_2];
    const ticks length = (256U - loaded) * t.period;
    t.expires += ((now - t.expires) / length + 1) * length;
  }
}

std::uint8_t adlib_card::read_port(std::uint16_t port) {
  if (port != adlib_address_port) {
    return open_bus_value;
  }
  settle(box_->time());
  return static_cast<std::uint8_t>(flags_ | adlib_status_mark);
}

bool adlib_card::write_register(std::uint8_t reg, std::uint8_t value) {
  if (reg == opl2_reg::test && (value & ~test_bits_modelled) != 0) {
    return false;
  }
  if (reg == opl2_reg::keyboard_split && (value & ~split_bits_modelled) != 0) {
    return false;
  }

  if (reg != opl2_reg::timer_control) {
    // The two timer values are read when a timer loads, so the flags are
    // settled under the old ones first.
    if (reg == opl2_reg::timer_1 || reg == opl2_reg::timer_2) {
      settle(box_->time());
    }
    regs_[reg] = value;
    return true;
  }

  const ticks now = box_->time();
  settle(now);
  if ((value & adlib_clear_flags) != 0) {
    // Clears the flags, and the rest of the byte is ignored.
    flags_ = 0;
    return true;
  }
  regs_[reg] = static_cast<std::uint8_t>(value & control_bits);
  for (std::size_t i = 0; i < timers_.size(); ++i) {
    timer& t = timers_[i];
    const bool start = (value & t.start_bit) != 0;
    if (start && !t.running) {
      const std::uint8_t loaded =
          regs_[i == 0 ? opl2_reg::timer_1 : opl2_reg::timer_2];
      t.expires = now + (256U - loaded) * t.period;
    }
    t.running = start;
  }
  return true;
}

void adlib_card::write_port(std::uint16_t port, std::uint8_t value) {
  if (port == adlib_address_port) {
    address_ = value;
    return;
  }
  if (!is_register(address_)) {
    return;
  }
  if (!write_register(address_, value)) {
    report_fault(port, value);
    return;
  }
  // The timers make no sound; everything else is the synthesizer's.
  if (address_ != opl2_reg::timer_1 && address_ != opl2_reg::timer_2 &&
      address_ != opl2_reg::timer_control) {
    box_->audio().publish_fm(box_->time(), address_, value);
  }
}

}  // namespace amberfolio::machine
