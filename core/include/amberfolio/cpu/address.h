// SPDX-License-Identifier: AGPL-3.0-only
//
// Real-mode address formation. It is the CPU's arithmetic, not the bus's
// (bus.h says why), and it is small enough and used from enough places —
// instruction fetch, effective addresses, stack, string operations — to
// have its own header rather than a home inside one of them.

#pragma once

#include <cstdint>

namespace amberfolio::cpu {

/// The address space: 1 MiB, and the 8086 has no twenty-first address pin.
/// A segment:offset pair that arithmetically exceeds it wraps to the
/// bottom — FFFF:0010 is physical 00000, which is real, documented 8086
/// behaviour and the reason the A20 gate later had to exist.
inline constexpr std::uint32_t address_space_size = 0x100000;
inline constexpr std::uint32_t address_mask = address_space_size - 1;

/// Physical address of `segment:offset`.
[[nodiscard]] constexpr std::uint32_t physical_address(
    std::uint16_t segment, std::uint16_t offset) noexcept {
  return ((static_cast<std::uint32_t>(segment) << 4u) + offset) & address_mask;
}

}  // namespace amberfolio::cpu
