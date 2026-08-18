// SPDX-License-Identifier: AGPL-3.0-only
//
// The I/O port space, and which device answers for which part of it.
//
// The memory side has a layout to consult because most of the megabyte is
// RAM whether or not anything claimed it; the port side has no such
// thing. A PC's port space is empty until a card is in a slot, so this is
// nothing but the list of claims, and a port outside every one of them is
// open bus — FF, a dropped write, and a log line the first time
// (diagnostics.h).
//
// The list is scanned, not indexed. A table indexed by port number would
// be 64 Ki pointers to answer a question a handful of ranges can answer
// in a few compares, on a bus cycle that is already an interpreted
// instruction away from anything that matters.

#pragma once

#include <array>
#include <cstddef>

#include "amberfolio/machine/device.h"

namespace amberfolio::machine {

class port_map {
 public:
  /// How many port ranges the map has room for. M2 claims six or so — the
  /// PIT's four, the PIC's two, the speaker gate, and the EGA's register
  /// pairs — and sixteen leaves the whole milestone room without making
  /// the scan worth thinking about.
  static constexpr std::size_t max_ranges = 16;

  /// Route `range` to `dev`. False, and nothing registered, if it
  /// overlaps a range already claimed or there is no room left. Two
  /// devices answering for one port is a wiring mistake with no sensible
  /// resolution, so it is refused rather than arbitrated.
  bool claim(port_range range, device& dev);

  /// The device that claimed `port`, or null if none did.
  [[nodiscard]] device* owner(std::uint16_t port) const noexcept;

 private:
  std::array<port_range, max_ranges> ranges_{};
  std::array<device*, max_ranges> owners_{};
  std::size_t claimed_{};
};

}  // namespace amberfolio::machine
