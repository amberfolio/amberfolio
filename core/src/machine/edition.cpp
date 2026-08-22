// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition table, and the two hex helpers every fact table compares
// through. edition.h has the reasoning.

#include "amberfolio/machine/edition.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

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

/// The baseline: the currently sold archive release's program image, the
/// copy every address in this build's seam tables is a fact about. Named
/// for what a player would recognize, with the file the fingerprint is
/// of, so a listing says which of a directory's files it was looking at.
/// The title appears nominatively (TRADEMARK.md) — it says which game the
/// edition is an edition of, and nothing more.
constexpr std::array<edition, 1> table{{
    {.fingerprint =
         "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",
     .name = "Pool of Radiance, archive release (START.EXE)"},
}};

}  // namespace

std::span<const edition> known_editions() { return table; }

const edition* find_edition(const sha256_digest& digest) noexcept {
  for (const edition& known : table) {
    if (digest_is(digest, known.fingerprint)) {
      return &known;
    }
  }
  return nullptr;
}

bool digest_is(const sha256_digest& digest, std::string_view text) noexcept {
  sha256_digest parsed;
  return parse_digest(text, parsed) && parsed == digest;
}

bool parse_digest(std::string_view text, sha256_digest& out) noexcept {
  if (text.size() != sha256_digest::text_length) {
    return false;
  }
  sha256_digest parsed;
  for (std::size_t i = 0; i < sha256_digest::byte_length; ++i) {
    const std::uint8_t high = nibble(text[i * 2]);
    const std::uint8_t low = nibble(text[i * 2 + 1]);
    if (high == 0xFF || low == 0xFF) {
      return false;
    }
    parsed.bytes[i] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  out = parsed;
  return true;
}

}  // namespace amberfolio::machine
