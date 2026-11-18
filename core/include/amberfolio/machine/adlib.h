// SPDX-License-Identifier: AGPL-3.0-only
//
// The AdLib card at ports 388h and 389h (#616).
//
// opl2.h is the chip; this is the card a program writes to. Its job is
// the Tandy chip's (tandy_sound.h): keep what the chip was told as
// architectural state, and put every register write onto
// `machine::audio()` at the tick it happened, where the consumer turns
// writes into samples.
//
//
// The two ports
// -------------
//
//   388h  write: the register address.  read: the status.
//   389h  write: the register's data.   read: nothing drives the bus.
//
// The chip latches the address and takes the data when it comes; a
// program is meant to wait a few microseconds between the two and after
// the second, and nothing here needs it to. A read of 389h is open bus:
// the chip has no register it will read back.
//
//
// The status, and the two timers
// ------------------------------
//
// The status byte is the only thing the chip says, and it is how a
// program finds the card: bit 7 says a timer has run out, bit 6 that
// timer 1 has and bit 5 that timer 2 has, and **bits 2 and 1 read as
// ones**, which is the YM3812's own mark (its successor reads zeros
// there).
//
// Timer 1 counts up from the value in register 02 every 80 microseconds
// and timer 2 from register 03 every 320, each setting its flag and
// loading its value again when it passes 255. Those periods are four
// and sixteen of the chip's samples, so 96 and 384 ticks exactly.
// Register 04 controls both: bit 0 starts timer 1 and bit 1 timer 2, bit
// 6 masks timer 1's flag and bit 5 timer 2's, and **a write with bit 7
// set clears the flags and does nothing else**. The detection every
// program makes is: mask and clear, read the status (nothing set), load
// FFh into timer 1 and start it, wait, read the status (bits 7 and 6
// set).
//
// **The timers are computed, never ticked** (`docs/machine.md` §4): a
// running timer is the tick it next runs out at, and a read of the
// status or a write to the timer registers settles both against
// `machine::time()` first. The card's interrupt line is not connected on
// an AdLib, so nothing here raises one.
//
//
// What is refused
// ---------------
//
// Register 01's test bits and register 08's composite sine mode are not
// modelled (opl2.h), and a write that sets one is `report_fault()`: a
// loud log line and a clean stop, not a guess. A write to an address
// that is not a register is dropped, as the chip drops it.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/opl2.h"

namespace amberfolio::machine {

class machine;

/// The card's two ports.
inline constexpr std::uint16_t adlib_address_port = 0x388;
inline constexpr std::uint16_t adlib_data_port = 0x389;

/// The status byte's bits, and the two that always read as ones.
inline constexpr std::uint8_t adlib_status_any = 0x80;
inline constexpr std::uint8_t adlib_status_timer_1 = 0x40;
inline constexpr std::uint8_t adlib_status_timer_2 = 0x20;
inline constexpr std::uint8_t adlib_status_mark = 0x06;

/// Register 04's bits.
inline constexpr std::uint8_t adlib_start_timer_1 = 0x01;
inline constexpr std::uint8_t adlib_start_timer_2 = 0x02;
inline constexpr std::uint8_t adlib_mask_timer_2 = 0x20;
inline constexpr std::uint8_t adlib_mask_timer_1 = 0x40;
inline constexpr std::uint8_t adlib_clear_flags = 0x80;

/// One count of each timer, in ticks: four and sixteen of the chip's
/// samples.
inline constexpr ticks adlib_timer_1_period = ticks{4} * opl2_ticks_per_sample;
inline constexpr ticks adlib_timer_2_period = ticks{16} * opl2_ticks_per_sample;

class adlib_card final : public device {
 public:
  /// `box` must outlive this; writes are published on its audio timeline
  /// and the timers read its clock.
  explicit adlib_card(machine& box) noexcept : box_(&box) {}

  static constexpr port_range port_window{.first = adlib_address_port,
                                          .last = adlib_data_port};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&port_window, 1)};
  }

  void reset() override;
  void save_state(state_sink& out) const override;
  [[nodiscard]] std::uint8_t read_port(std::uint16_t port) override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  /// The register file as the program has written it. An address that is
  /// not a register reads back as zero here: it was never stored.
  [[nodiscard]] std::uint8_t reg(std::uint8_t address) const noexcept {
    return regs_[address];
  }

  /// The address the next data write goes to.
  [[nodiscard]] std::uint8_t address() const noexcept { return address_; }

 private:
  struct timer {
    /// What a count costs, and the control bits that start and mask it.
    ticks period{};
    std::uint8_t start_bit{};
    std::uint8_t mask_bit{};
    std::uint8_t flag{};

    bool running{};
    /// The tick the count next passes 255. Meaningful while running.
    ticks expires{};
  };

  /// Bring both timers' flags up to `now`.
  void settle(ticks now) noexcept;

  /// A data write to a register that exists. False for one that is
  /// refused.
  [[nodiscard]] bool write_register(std::uint8_t reg, std::uint8_t value);

  /// Whether `address` is a register of the chip.
  [[nodiscard]] static bool is_register(std::uint8_t address) noexcept;

  machine* box_;
  std::uint8_t address_{};
  std::array<std::uint8_t, 256> regs_{};
  std::uint8_t flags_{};
  std::array<timer, 2> timers_{{
      {.period = adlib_timer_1_period,
       .start_bit = adlib_start_timer_1,
       .mask_bit = adlib_mask_timer_1,
       .flag = adlib_status_timer_1},
      {.period = adlib_timer_2_period,
       .start_bit = adlib_start_timer_2,
       .mask_bit = adlib_mask_timer_2,
       .flag = adlib_status_timer_2},
  }};
};

}  // namespace amberfolio::machine
