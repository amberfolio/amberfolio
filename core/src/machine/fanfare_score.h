// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam's score (seam_fanfare.cpp, #538), as the seam plays
// it: a list of writes to the Tandy sound chip's port, each at a tick of
// the piece's own time. fanfare_score.cpp has the list and where it
// comes from.

#pragma once

#include <cstdint>
#include <span>

namespace amberfolio::machine {

/// One write: the byte, and the PIT tick from the piece's start it goes
/// out at. Ticks never go backwards along the score; writes on the same
/// tick go out in order. Constructed rather than aggregate, so the table
/// can say each cue as the pair it is.
struct chip_cue {
  constexpr chip_cue(std::uint32_t tick, std::uint8_t byte) noexcept
      : at(tick), value(byte) {}

  std::uint32_t at;
  std::uint8_t value;
};

/// The whole piece, in order.
[[nodiscard]] std::span<const chip_cue> fanfare_score() noexcept;

}  // namespace amberfolio::machine
