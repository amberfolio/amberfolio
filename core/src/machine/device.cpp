// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/device.h"

namespace amberfolio::machine {

// Out of line, and all four together: this is where the vtable is
// emitted, so every device in the tree shares one rather than each
// translation unit that sees the header emitting its own.

std::uint8_t device::read_memory(std::uint32_t /*address*/) {
  return open_bus_value;
}

void device::write_memory(std::uint32_t /*address*/, std::uint8_t /*value*/) {}

std::uint8_t device::read_port(std::uint16_t /*port*/) {
  return open_bus_value;
}

void device::write_port(std::uint16_t /*port*/, std::uint8_t /*value*/) {}

}  // namespace amberfolio::machine
