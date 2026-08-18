// SPDX-License-Identifier: AGPL-3.0-only

#include "programs/assembler.h"

#include <stdexcept>
#include <string>

namespace amberfolio::programs {

void assembler::db(std::initializer_list<std::uint8_t> bytes) {
  code_.insert(code_.end(), bytes);
}

void assembler::dw(std::uint16_t value) {
  code_.push_back(static_cast<std::uint8_t>(value));
  code_.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void assembler::label(std::string_view name) {
  const auto [where, fresh] = labels_.emplace(name, code_.size());
  if (!fresh) {
    throw std::logic_error("duplicate label: " + std::string(name));
  }
}

void assembler::jump(std::uint8_t opcode, std::string_view target) {
  code_.push_back(opcode);
  fixups_.push_back({.at = code_.size(), .target = std::string(target)});
  code_.push_back(0);
}

std::vector<std::uint8_t> assembler::assemble() const {
  std::vector<std::uint8_t> patched = code_;
  for (const fixup& f : fixups_) {
    const auto found = labels_.find(f.target);
    if (found == labels_.end()) {
      throw std::logic_error("no such label: " + f.target);
    }
    // From the byte after the displacement, which is where IP stands once
    // the processor has fetched it.
    const auto from = static_cast<std::ptrdiff_t>(f.at) + 1;
    const std::ptrdiff_t delta =
        static_cast<std::ptrdiff_t>(found->second) - from;
    if (delta < -128 || delta > 127) {
      throw std::logic_error(f.target + " is out of a rel8's reach (" +
                             std::to_string(delta) + " bytes)");
    }
    patched[f.at] = static_cast<std::uint8_t>(delta);
  }
  return patched;
}

}  // namespace amberfolio::programs
