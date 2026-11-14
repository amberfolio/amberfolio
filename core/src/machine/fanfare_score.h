// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam's scores (seam_fanfare.cpp, #538, #598), one piece a
// game, as the seam plays them: a list of writes to the Tandy sound
// chip's port, each at a tick of the piece's own time. fanfare_score.cpp
// and fanfare_score_curse.cpp have the lists and where they come from.

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

/// Pool of Radiance's piece, the whole of it, in order.
[[nodiscard]] std::span<const chip_cue> fanfare_score() noexcept;

/// Curse of the Azure Bonds' piece, the whole of it, in order
/// (fanfare_score_curse.cpp, #598).
[[nodiscard]] std::span<const chip_cue> curse_fanfare_score() noexcept;

}  // namespace amberfolio::machine
