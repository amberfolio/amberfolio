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

/// The baseline: the program image every address in this build's seam
/// tables is a fact about. It is one image in two releases — the one sold
/// on GOG and Steam, and a third-party repack of it that the facts were
/// first gathered on — and a machine edition is a program image, so it is
/// one row here and two in the host's requirement table
/// (`host/edition_facts.h`). Named for what a player would recognize,
/// with the file the fingerprint is of, so a listing says which of a
/// directory's files it was looking at. The title appears nominatively
/// (TRADEMARK.md) — it says which game the edition is an edition of, and
/// nothing more.
///
/// The second row is the program the Steam release of Curse of the Azure
/// Bonds boots, which identifies itself as v1.3 (PLAN.md §7, M9). Its
/// facts are a profile of its own (`program_profile.h`), and a seam is
/// available on it where the seam's fingerprints name that profile.
///
/// Each row's configuration file was read off the copies themselves:
/// `POOL.CFG` has five lines with the save directory on the fourth, and
/// `CURSE.CFG` four with the save directory on the third. Both say the
/// sound device on the second.
constexpr std::array<edition, 2> table{{
    {.fingerprint =
         "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",
     .name = "Pool of Radiance (START.EXE)",
     .configuration = {.file = "POOL.CFG", .sound_line = 2, .save_line = 4}},
    {.fingerprint =
         "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd",
     .name = "Curse of the Azure Bonds (START.EXE)",
     .configuration = {.file = "CURSE.CFG", .sound_line = 2, .save_line = 3}},
}};

}  // namespace

std::span<const edition> known_editions() { return table; }

const launcher_configuration* find_configuration(
    std::string_view leaf) noexcept {
  for (const edition& known : table) {
    if (known.configuration.file == leaf) {
      return &known.configuration;
    }
  }
  return nullptr;
}

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
