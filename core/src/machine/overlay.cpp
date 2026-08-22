// SPDX-License-Identifier: AGPL-3.0-only
//
// overlay.h has the design; this is the table and the two rules it
// promises — a new read replaces whatever it overlaps, and a full table
// drops its oldest entry.

#include "amberfolio/machine/overlay.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/machine/edition.h"

namespace amberfolio::machine {
namespace {

/// Whether `name`, a leaf as DOS spells it, is the last component of
/// `path`. Case-exact, because both sides are already canonical: a seam
/// writes its module's name upper-cased the way the VFS would answer it.
[[nodiscard]] bool leaf_is(const dos_path& path,
                           std::string_view name) noexcept {
  if (path.is_root()) {
    return false;
  }
  const std::span<const char> text = path.leaf().text();
  if (text.size() != name.size()) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != name[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::uint32_t overlay_load::first() const noexcept {
  return cpu::physical_address(segment, offset);
}

std::uint32_t overlay_load::last() const noexcept {
  // The read wrapped within the segment the way every DOS buffer does
  // (dos.cpp writes it byte by byte through `write_byte`), so its last
  // byte is at `offset + length - 1` in the same segment. A range that
  // crosses the segment's end is treated as running to the end of the
  // megabyte here: rare, never an overlay, and the honest answer for "which
  // addresses did this touch" without modelling the wrap twice.
  const std::uint32_t span = length == 0 ? 0 : length - 1;
  const std::uint32_t end = first() + span;
  return end < first() ? cpu::address_space_size - 1 : end;
}

void overlay_tracker::note_read(const dos_path& file, std::uint32_t file_offset,
                                std::uint16_t segment, std::uint16_t offset,
                                std::uint32_t length,
                                const sha256_digest& digest) noexcept {
  if (length == 0) {
    return;
  }

  overlay_load arrived{.file = file,
                       .file_offset = file_offset,
                       .length = length,
                       .segment = segment,
                       .offset = offset,
                       .digest = digest,
                       .generation = ++generation_};

  // Everything this read overwrote is no longer resident. Compacted in
  // place, so the table stays a packed prefix and `at()` is an index.
  std::size_t kept = 0;
  for (std::size_t i = 0; i < count_; ++i) {
    const overlay_load& old = loads_[i];
    const bool overlaps =
        old.first() <= arrived.last() && arrived.first() <= old.last();
    if (!overlaps) {
      loads_[kept++] = old;
    }
  }
  count_ = kept;

  if (count_ == max_modules) {
    // The oldest is at index 0: entries are appended in generation order
    // and compaction preserves it.
    for (std::size_t i = 1; i < count_; ++i) {
      loads_[i - 1] = loads_[i];
    }
    --count_;
  }

  loads_[count_++] = arrived;
}

const overlay_load* overlay_tracker::resident(
    const seam_module& module) const noexcept {
  if (module.is_resident_image()) {
    return nullptr;
  }
  // Newest first, so that if two resident reads somehow match the same
  // facts the one the program made last is the one answered.
  for (std::size_t i = count_; i > 0; --i) {
    const overlay_load& load = loads_[i - 1];
    if (load.file_offset != module.file_offset ||
        load.length != module.length || !leaf_is(load.file, module.file)) {
      continue;
    }
    if (!module.digest.empty() && !digest_is(load.digest, module.digest)) {
      continue;
    }
    return &load;
  }
  return nullptr;
}

void overlay_tracker::clear() noexcept {
  count_ = 0;
  generation_ = 0;
}

}  // namespace amberfolio::machine
