// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/memory_map.h"

namespace amberfolio::machine {

memory_map::memory_map(memory_layout map) noexcept : layout_(map) {}

bool memory_map::claim(memory_window window, device& dev) {
  if (window.last < window.first || claimed_ == max_windows) {
    return false;
  }
  for (std::size_t i = 0; i < claimed_; ++i) {
    if (windows_[i].overlaps(window)) {
      return false;
    }
  }

  windows_[claimed_] = window;
  owners_[claimed_] = &dev;
  ++claimed_;
  return true;
}

device* memory_map::owner(std::uint32_t address) const noexcept {
  for (std::size_t i = 0; i < claimed_; ++i) {
    if (windows_[i].contains(address)) {
      return owners_[i];
    }
  }
  return nullptr;
}

region memory_map::classify(std::uint32_t address) const noexcept {
  // Above the address space before anything else. The CPU cannot produce
  // such an address — it folds and wraps before it reaches the bus
  // (address.h) — so this is about a caller that got it wrong, and the
  // honest answer for an address the machine does not have is that
  // nothing answers for it. It also means every path below can index
  // `storage_` without a bounds check that would never fire.
  if (address >= cpu::address_space_size) {
    return region::open_bus;
  }

  if (owner(address) != nullptr) {
    return region::device;
  }

  switch (layout_) {
    case memory_layout::flat:
      return region::ram;
    case memory_layout::pc:
      break;
  }

  if (address < conventional_ram_size) {
    return region::ram;
  }
  if (bios_region.contains(address)) {
    return region::rom;
  }
  return region::open_bus;
}

}  // namespace amberfolio::machine
