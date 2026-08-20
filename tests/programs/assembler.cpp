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
  fixups_.push_back({.at = code_.size(),
                     .kind = fixup_kind::relative8,
                     .target = std::string(target)});
  code_.push_back(0);
}

void assembler::near_jump(std::uint8_t opcode, std::string_view target) {
  code_.push_back(opcode);
  fixups_.push_back({.at = code_.size(),
                     .kind = fixup_kind::relative16,
                     .target = std::string(target)});
  code_.push_back(0);
  code_.push_back(0);
}

void assembler::dw_label(std::string_view target) {
  fixups_.push_back({.at = code_.size(),
                     .kind = fixup_kind::absolute16,
                     .target = std::string(target)});
  code_.push_back(0);
  code_.push_back(0);
}

std::size_t assembler::offset_of(std::string_view target) const {
  const auto found = labels_.find(target);
  if (found == labels_.end()) {
    throw std::logic_error("no such label: " + std::string(target));
  }
  return found->second;
}

void assembler::pad_to(std::size_t offset) {
  if (code_.size() > offset) {
    throw std::logic_error("already past offset " + std::to_string(offset) +
                           " (" + std::to_string(code_.size()) + " bytes)");
  }
  code_.resize(offset, 0);
}

std::vector<std::uint8_t> assembler::assemble() const {
  std::vector<std::uint8_t> patched = code_;
  for (const fixup& f : fixups_) {
    const auto found = labels_.find(f.target);
    if (found == labels_.end()) {
      throw std::logic_error("no such label: " + f.target);
    }
    const auto where = static_cast<std::ptrdiff_t>(found->second);

    if (f.kind == fixup_kind::absolute16) {
      if (where > 0xFFFF) {
        throw std::logic_error(f.target + " is past the end of a segment (" +
                               std::to_string(where) + ")");
      }
      patched[f.at] = static_cast<std::uint8_t>(where);
      patched[f.at + 1] = static_cast<std::uint8_t>(where >> 8U);
      continue;
    }

    // From the byte after the displacement, which is where IP stands once
    // the processor has fetched it.
    const std::ptrdiff_t width = f.kind == fixup_kind::relative8 ? 1 : 2;
    const std::ptrdiff_t from = static_cast<std::ptrdiff_t>(f.at) + width;
    const std::ptrdiff_t delta = where - from;

    if (f.kind == fixup_kind::relative8) {
      if (delta < -128 || delta > 127) {
        throw std::logic_error(f.target + " is out of a rel8's reach (" +
                               std::to_string(delta) + " bytes)");
      }
      patched[f.at] = static_cast<std::uint8_t>(delta);
      continue;
    }

    if (delta < -32768 || delta > 32767) {
      throw std::logic_error(f.target + " is out of a rel16's reach (" +
                             std::to_string(delta) + " bytes)");
    }
    patched[f.at] = static_cast<std::uint8_t>(delta);
    patched[f.at + 1] = static_cast<std::uint8_t>(delta >> 8U);
  }
  return patched;
}

}  // namespace amberfolio::programs
