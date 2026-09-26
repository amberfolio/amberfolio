// SPDX-License-Identifier: AGPL-3.0-only
//
// The Tandy 1000's sound chip at port C0h (#404).
//
// psg.h is the chip; this is the device a program writes to. Its whole
// job is the one the speaker has (speaker.h): keep what the chip was told
// as architectural state, and put every write onto `machine::audio()` at
// the tick it happened, where the consumer turns writes into samples.
//
// The chip is write-only. A read of C0h answers open bus, which is what
// the data lines float to on a part with no read path, and which the
// device base class already answers. Only C0h is claimed: the Tandy 1000
// decodes more of that block than the chip uses, and a program that
// touches the rest is told so by the unclaimed-port notice rather than
// answered by a guess.

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/machine/device.h"
#include "amberfolio/machine/psg.h"

namespace amberfolio::machine {

class machine;

/// The chip's port on a Tandy 1000.
inline constexpr std::uint16_t tandy_sound_port = 0xC0;

class tandy_sound final : public device {
 public:
  /// `box` must outlive this; writes are published on its audio timeline.
  explicit tandy_sound(machine& box) noexcept : box_(&box) {}

  static constexpr port_range port_window{.first = tandy_sound_port,
                                          .last = tandy_sound_port};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&port_window, 1)};
  }

  void reset() override;
  void save_state(state_sink& out) const override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  [[nodiscard]] const psg_registers& registers() const noexcept {
    return regs_;
  }

 private:
  machine* box_;
  psg_registers regs_{psg_registers::powered_on()};
};

}  // namespace amberfolio::machine
