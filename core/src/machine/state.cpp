// SPDX-License-Identifier: AGPL-3.0-only
//
// state.h has the design; the layout itself is `machine::save_state`
// (machine.cpp) and each device's `save_state`. This file is the sink
// helpers, the hasher, and the names.

#include "amberfolio/machine/state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/machine.h"

namespace amberfolio::machine {

const char* state_section_name(state_section which) noexcept {
  switch (which) {
    case state_section::clock:
      return "clock";
    case state_section::cpu:
      return "cpu";
    case state_section::ram:
      return "ram";
    case state_section::devices:
      return "devices";
    case state_section::scheduler:
      return "scheduler";
    case state_section::keyboard:
      return "keyboard";
    case state_section::dos:
      return "dos";
    case state_section::wall:
      return "wall";
    case state_section::input:
      return "input";
    case state_section::console:
      return "console";
    case state_section::audio:
      return "audio";
    case state_section::display:
      return "display";
    case state_section::stop:
      return "stop";
  }
  return "unknown";
}

// --- state_sink ---------------------------------------------------------

void state_sink::u8(std::uint8_t value) {
  bytes(std::span<const std::uint8_t>(&value, 1));
}

void state_sink::u16(std::uint16_t value) {
  const std::array<std::uint8_t, 2> little{
      static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U)};
  bytes(little);
}

void state_sink::u32(std::uint32_t value) {
  const std::array<std::uint8_t, 4> little{
      static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(value >> 8U),
      static_cast<std::uint8_t>(value >> 16U),
      static_cast<std::uint8_t>(value >> 24U)};
  bytes(little);
}

void state_sink::u64(std::uint64_t value) {
  std::array<std::uint8_t, 8> little{};
  for (std::size_t i = 0; i < little.size(); ++i) {
    little[i] = static_cast<std::uint8_t>(value >> (8U * i));
  }
  bytes(little);
}

// --- state_hasher -------------------------------------------------------

void state_hasher::begin(state_section which) {
  close_section();
  current_ = which;
  in_section_ = true;
  section_ = sha256_hasher{};
  // The section's tag goes into the whole-stream hash, so two streams
  // that differ only in where a section boundary falls hash differently
  // — the layout is part of what is being pinned.
  const std::uint8_t tag = static_cast<std::uint8_t>(which);
  whole_.update(std::span<const std::uint8_t>(&tag, 1));
}

void state_hasher::bytes(std::span<const std::uint8_t> data) {
  whole_.update(data);
  if (in_section_) {
    section_.update(data);
  }
}

void state_hasher::close_section() {
  if (!in_section_) {
    return;
  }
  out_.sections[static_cast<std::size_t>(current_)] = section_.finish();
  in_section_ = false;
}

state_hashes state_hasher::finish() {
  close_section();
  out_.whole = whole_.finish();
  return out_;
}

// --- The entry points ----------------------------------------------------

void serialize_state(const machine& box, state_sink& out) {
  box.save_state(out);
}

state_hashes hash_state(const machine& box) {
  state_hasher hasher;
  serialize_state(box, hasher);
  return hasher.finish();
}

}  // namespace amberfolio::machine
