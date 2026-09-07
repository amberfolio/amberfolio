// SPDX-License-Identifier: AGPL-3.0-only
//
// held_keys.h's small body (#313).

#include "amberfolio/host/held_keys.h"

namespace amberfolio::host {

void held_keys::note(std::uint8_t scancode,
                     machine::key_action action) noexcept {
  if (scancode == 0 || scancode >= code_count) {
    return;
  }
  held_.set(scancode, action == machine::key_action::down);
}

bool held_keys::held(std::uint8_t scancode) const noexcept {
  return scancode < code_count && held_.test(scancode);
}

std::vector<std::uint8_t> held_keys::release_all() {
  std::vector<std::uint8_t> codes;
  codes.reserve(held_.count());
  for (std::size_t code = 1; code < code_count; ++code) {
    if (held_.test(code)) {
      codes.push_back(static_cast<std::uint8_t>(code));
    }
  }
  held_.reset();
  return codes;
}

}  // namespace amberfolio::host
