// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/port_map.h"

namespace amberfolio::machine {

bool port_map::claim(port_range range, device& dev) {
  if (range.last < range.first || claimed_ == max_ranges) {
    return false;
  }
  for (std::size_t i = 0; i < claimed_; ++i) {
    if (ranges_[i].overlaps(range)) {
      return false;
    }
  }

  ranges_[claimed_] = range;
  owners_[claimed_] = &dev;
  ++claimed_;
  return true;
}

device* port_map::owner(std::uint16_t port) const noexcept {
  for (std::size_t i = 0; i < claimed_; ++i) {
    if (ranges_[i].contains(port)) {
      return owners_[i];
    }
  }
  return nullptr;
}

}  // namespace amberfolio::machine
