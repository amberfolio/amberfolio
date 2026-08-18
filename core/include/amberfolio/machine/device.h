// SPDX-License-Identifier: AGPL-3.0-only
//
// What a device is to the machine, and it is deliberately almost nothing:
// something that claims a window of physical memory and/or a set of I/O
// ports, answers bus cycles inside what it claimed, and goes back to
// power-on state when the RESET line is pulled.
//
// Time is not here. A device that has to do something at a *moment*
// rather than in answer to a bus cycle posts a deadline to the scheduler,
// and the scheduler is M2-F2 (#43): it arrives as a second thing a device
// may take part in, not as another virtual on this one. Designing that
// interface here would mean guessing at it before the PIT and the
// renderer have said what they need, so the seam is left open instead.
//
// This header is also the machine's bus vocabulary — the range types and
// the value an unanswered cycle reads back as — because both maps and
// every device need them.

#pragma once

#include <cstdint>
#include <span>

namespace amberfolio::machine {

/// What a bus cycle nothing answers for reads back as.
///
/// Not a guess and not a fake: an ISA bus with nothing driving it floats
/// high, and FF is what the processor latches. PLAN.md §3's rule is that
/// writes to absent hardware are "ignored (logged, not faked)" — the
/// logging is the machine's job (diagnostics.h), and this is the honest
/// half of the answer.
inline constexpr std::uint8_t open_bus_value = 0xFF;

/// An inclusive range of I/O ports.
///
/// Inclusive rather than half-open because that is how the hardware is
/// documented — the PIT is "40h-43h", not "40h through 44h" — and
/// because a half-open range ending at the top of the port space could
/// not be written down in sixteen bits.
struct port_range {
  std::uint16_t first{};
  std::uint16_t last{};

  [[nodiscard]] constexpr bool contains(std::uint16_t port) const noexcept {
    return port >= first && port <= last;
  }

  [[nodiscard]] constexpr bool overlaps(port_range other) const noexcept {
    return first <= other.last && other.first <= last;
  }

  friend constexpr bool operator==(const port_range&,
                                   const port_range&) = default;
};

/// An inclusive range of physical memory addresses.
struct memory_window {
  std::uint32_t first{};
  std::uint32_t last{};

  [[nodiscard]] constexpr bool contains(std::uint32_t address) const noexcept {
    return address >= first && address <= last;
  }

  [[nodiscard]] constexpr bool overlaps(memory_window other) const noexcept {
    return first <= other.last && other.first <= last;
  }

  friend constexpr bool operator==(const memory_window&,
                                   const memory_window&) = default;
};

/// Everything a device asks the machine to route to it, in one place.
///
/// Spans rather than one window and one range, because a device may want
/// neither, either, or several of each: the EGA claims A0000-AFFFF and
/// two register pairs, the PIT claims four ports and no memory. An empty
/// span is a device that wants none of that kind, and it is the default.
///
/// The spans are read once, when the device is attached, and must be
/// valid until then — a static member of the device, in practice.
struct claims {
  std::span<const memory_window> memory{};
  std::span<const port_range> ports{};
};

class device {
 public:
  device() = default;
  device(const device&) = delete;
  device(device&&) = delete;
  device& operator=(const device&) = delete;
  device& operator=(device&&) = delete;

  /// What this device answers for. Called once, by machine::attach().
  [[nodiscard]] virtual claims claimed() const noexcept = 0;

  /// The RESET line: back to power-on state. What that means for memory
  /// the device owns — an EGA's planes, say — is the device's business,
  /// exactly as the machine's RAM is the machine's (see machine::reset()).
  virtual void reset() = 0;

  /// A bus cycle inside one of this device's memory windows. `address` is
  /// physical, not an offset into the device: a device with two windows
  /// would otherwise have no way to tell which cycle this is.
  ///
  /// Defaulted to open bus rather than pure, so that a device with no
  /// memory window — most of them — does not carry two dead overrides.
  /// The machine never calls these outside what the device claimed, so
  /// reaching the default means a device claimed a window and did not
  /// implement it.
  [[nodiscard]] virtual std::uint8_t read_memory(std::uint32_t address);
  virtual void write_memory(std::uint32_t address, std::uint8_t value);

  /// A cycle on one of this device's ports. Defaulted for the same
  /// reason, and it is the common case: only the EGA has memory.
  [[nodiscard]] virtual std::uint8_t read_port(std::uint16_t port);
  virtual void write_port(std::uint16_t port, std::uint8_t value);

 protected:
  // See cpu/bus.h: a device is held by reference by the machine and never
  // owned or deleted through this type, so it pays for no vtable slot it
  // does not need.
  ~device() = default;
};

}  // namespace amberfolio::machine
