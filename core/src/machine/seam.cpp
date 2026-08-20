// SPDX-License-Identifier: AGPL-3.0-only
//
// seam.h has the design; this is the arming and the dispatch it promises.

#include "amberfolio/machine/seam.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/machine/machine.h"

namespace amberfolio::machine {
namespace {

/// One hex character as a nibble, or 0xFF for anything that is not one.
[[nodiscard]] std::uint8_t nibble(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  return 0xFF;
}

/// Whether `digest` is what `text` spells. False for text of the wrong
/// length or with a character that is not hex — a seam whose fingerprint
/// is mistyped applies to nothing, which is the failure this project
/// wants over one that applies to everything.
[[nodiscard]] bool digest_is(const sha256_digest& digest,
                             std::string_view text) noexcept {
  if (text.size() != sha256_digest::text_length) {
    return false;
  }
  for (std::size_t i = 0; i < sha256_digest::byte_length; ++i) {
    const std::uint8_t high = nibble(text[i * 2]);
    const std::uint8_t low = nibble(text[i * 2 + 1]);
    if (high == 0xFF || low == 0xFF) {
      return false;
    }
    if (digest.bytes[i] != static_cast<std::uint8_t>((high << 4U) | low)) {
      return false;
    }
  }
  return true;
}

}  // namespace

void seam_engine::loaded(const sha256_digest& digest,
                         std::uint16_t image_segment) {
  clear();
  digest_ = digest;
  image_segment_ = image_segment;
  have_program_ = true;
}

void seam_engine::clear() noexcept {
  points_ = {};
  armed_ = 0;
  enabled_ = {};
  enabled_count_ = 0;
  have_program_ = false;
}

seam_error seam_engine::enable(std::string_view id) {
  if (!have_program_) {
    return seam_error::no_program;
  }

  const seam_definition* found = nullptr;
  for (const seam_definition& seam : all_seams()) {
    if (seam.id == id) {
      found = &seam;
      break;
    }
  }
  if (found == nullptr) {
    return seam_error::unknown_seam;
  }
  if (!digest_is(digest_, found->fingerprint)) {
    return seam_error::wrong_binary;
  }
  if (armed_ + found->points.size() > max_points ||
      enabled_count_ == max_enabled) {
    return seam_error::too_many_points;
  }

  for (const seam_point& point : found->points) {
    points_[armed_] = {
        .at = cpu::physical_address(image_segment_, 0) + point.image_offset,
        .run = point.run};
    ++armed_;
  }
  enabled_[enabled_count_] = found->id;
  ++enabled_count_;
  return seam_error::none;
}

void seam_engine::dispatch(machine& box, std::uint32_t at) {
  for (std::size_t i = 0; i < armed_; ++i) {
    if (points_[i].at == at && points_[i].run != nullptr) {
      points_[i].run(box);
    }
  }
}

}  // namespace amberfolio::machine
