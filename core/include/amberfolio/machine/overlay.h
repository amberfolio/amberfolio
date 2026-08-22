// SPDX-License-Identifier: AGPL-3.0-only
//
// The overlay residency tracker: what the program has loaded where, as
// observed through the DOS file layer (M4-F3, #97).
//
// PLAN.md §5: "the original program swaps overlaid code through shared
// memory, so a raw address does not identify code; interception points
// are qualified by which module/overlay is currently resident (the
// machine tracks this as the program loads them)." This is the tracking.
//
//
// Observation, not matching
// -------------------------
//
// The tracker never looks at the bytes it records. It watches the one
// path an overlay can arrive by — INT 21h AH=3Fh (dos.cpp) — and notes,
// for each read: which file, from which offset in it, into which
// segment:offset, how many bytes, and the SHA-256 of what was read. All
// of those are *facts about the read* (CONTRIBUTING.md): the name is the
// program's own, the offset and length are numbers, and a digest names
// bytes without carrying them. Nothing in this file, and nothing in any
// seam that qualifies itself through it, holds a byte sequence to match
// against — PLAN.md §5's "nothing original embedded" rule, kept by
// construction rather than by review.
//
// A seam names the module it wants by the same facts (`seam_module`,
// seam.h): the file, the offset in it, the length, and optionally the
// digest. "Resident" means: the most recent read matching those facts
// landed in memory that no later read has overwritten.
//
//
// Below the fidelity boundary
// ---------------------------
//
// This is bookkeeping about what the machine did, never a hand on it. It
// alters no read, writes no memory, and a machine with every seam off
// behaves identically whether or not the table is kept — which is why
// it is not part of the machine-state serialization (#100): it is a
// record of machine events, not machine state, and a replay reconstructs
// it by replaying the events. `machine` tells the seam engine when the
// table changes, and the engine re-evaluates its points then (seam.h) —
// that, and not this file, is where a seam's arming decision is made.
//
//
// The table, and what a full one does
// -----------------------------------
//
// A fixed table of `max_modules` entries, each one memory range. A new
// read replaces every entry whose range it overlaps — the memory now
// holds something else, which is the one fact the table exists to keep
// true — and when the table is full the oldest entry goes. A seam whose
// module has been evicted sees it as not resident and stays inert with
// that reason (seam.h), which is the fail-closed direction: the worst
// case of a small table is a seam that declines, never one that fires on
// the wrong code.
//
// Every read is recorded, data reads included. A program reading records
// into a buffer creates an entry for that buffer and replaces it on the
// next read into the same place, so the table is bounded by the number
// of distinct destinations a program is reading into at once — a handful
// for the programs this machine is for — rather than by how much it
// reads. Recording only "large" reads would have been guessing what an
// overlay looks like, and the tracker does not get to guess.
//
//
// Versioned
// ---------
//
// `overlay_schema_version` names how a module is identified. A seam
// definition carries the version it was written against (seam.h), and
// the engine refuses one written against another — so a future change
// to how residency is recognised cannot silently re-target a seam that
// was written for the old rule.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

/// How a module is identified: by the facts of the read that loaded it.
/// Bump when the meaning of `overlay_load` or `seam_module` changes.
inline constexpr std::uint16_t overlay_schema_version = 1;

/// One read the tracker saw, and where it landed.
struct overlay_load {
  /// The file it came from, canonical.
  dos_path file;
  /// Where in the file the read began, and how many bytes it took.
  std::uint32_t file_offset{};
  std::uint32_t length{};
  /// Where the bytes landed, as the program addressed them.
  std::uint16_t segment{};
  std::uint16_t offset{};
  /// The SHA-256 of the bytes read — the identity of what is resident,
  /// without a copy of it.
  sha256_digest digest{};
  /// A counter that only goes up: which read this was. Age, for eviction,
  /// and the thing a caller compares to know whether anything changed.
  std::uint64_t generation{};

  /// The first and last physical address the read covered.
  [[nodiscard]] std::uint32_t first() const noexcept;
  [[nodiscard]] std::uint32_t last() const noexcept;
};

/// What a seam's point says about which code it lives in (seam.h). Here
/// rather than there because this is the file that decides what the
/// fields mean.
///
/// An empty `file` is the resident image — the program the loader
/// placed, which is never overlaid and needs no tracking. Anything else
/// names a module by the facts of the read that loads it.
struct seam_module {
  /// The leaf name of the file the module is read from — `OVL.BIN`, the
  /// name as DOS spells it. Empty for the resident image.
  std::string_view file{};
  /// The offset in that file the read begins at, and its length in bytes.
  std::uint32_t file_offset{};
  std::uint32_t length{};
  /// Optionally, the SHA-256 of the bytes the read delivers, as 64 hex
  /// characters; empty means the file, offset and length are the whole
  /// qualifier.
  std::string_view digest{};

  [[nodiscard]] constexpr bool is_resident_image() const noexcept {
    return file.empty();
  }
};

/// The resident image, for a point that does not live in an overlay.
inline constexpr seam_module resident_image{};

class overlay_tracker {
 public:
  /// Entries kept. Thirty-two distinct destinations is several times what
  /// an overlay manager and a handful of data buffers need at once, and
  /// small enough that `resident()` is a scan nobody has to think about.
  static constexpr std::size_t max_modules = 32;

  /// A read happened: `length` bytes of `file` from `file_offset`, landing
  /// at `segment:offset`, hashing to `digest`. Zero-length reads are not
  /// loads and are ignored. Called by the DOS read handler (dos.cpp).
  void note_read(const dos_path& file, std::uint32_t file_offset,
                 std::uint16_t segment, std::uint16_t offset,
                 std::uint32_t length, const sha256_digest& digest) noexcept;

  /// The load that put `module` in memory, or null if no read matching
  /// its facts is resident. The resident image is never in the table and
  /// answers null here; callers test `is_resident_image()` first.
  [[nodiscard]] const overlay_load* resident(
      const seam_module& module) const noexcept;

  /// The table, for a host or a test that wants to see it: `count()`
  /// entries, newest last.
  [[nodiscard]] std::size_t count() const noexcept { return count_; }
  [[nodiscard]] const overlay_load& at(std::size_t index) const noexcept {
    return loads_[index];
  }

  /// The generation of the most recent read — what a caller keeps to ask
  /// "has anything changed since I looked".
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }

  /// Forget everything. `machine::reset()` calls this: a reset machine
  /// has loaded nothing.
  void clear() noexcept;

 private:
  std::array<overlay_load, max_modules> loads_{};
  std::size_t count_{};
  std::uint64_t generation_{};
};

}  // namespace amberfolio::machine
