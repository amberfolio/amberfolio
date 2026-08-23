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
// That is a claim about a **read**, and it is the weaker of the two
// answers this file can give. Where the program keeps its own note of
// where a module is — `seam_module::load_segment_at`, which most
// overlay managers of this era do — that note is authoritative and this
// table is not consulted for the address at all. The reason is #131:
// a manager may move a resident module inside its arena without reading
// it again, and a table built out of reads cannot follow. Both readings
// are here because a program that keeps no such note still gets the
// weaker one, which is better than nothing and fails closed.
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

/// How a module is identified: by the facts of the read that loaded it,
/// and — since version 2 — optionally by the program's own record of
/// where it currently is (`seam_module::load_segment_at`). Bump when the
/// meaning of `overlay_load` or `seam_module` changes.
inline constexpr std::uint16_t overlay_schema_version = 2;

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

/// The value `seam_module::load_segment_at` carries when the program
/// keeps no reachable record of where a module is — in which case the
/// tracker's reads are the whole of what is known about it.
inline constexpr std::uint32_t no_load_segment = 0xFFFFFFFFU;

/// What a seam's point says about which code it lives in (seam.h). Here
/// rather than there because this is the file that decides what the
/// fields mean.
///
/// An empty `file` is the resident image — the program the loader
/// placed, which is never overlaid and needs no tracking. Anything else
/// names a module by the facts of the read that loads it, and — where
/// the program keeps one — by the program's own note of where that
/// module is right now.
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

  /// Where the *program* records this module's current load segment: an
  /// offset in the resident image of one word, holding a segment while
  /// the module is loaded and zero while it is not. `no_load_segment`
  /// when the program keeps no such word, or when nobody has found it.
  ///
  /// This is the field that makes an overlaid point survive an overlay
  /// manager that *moves* things (#131). The three facts above describe
  /// a **read**, and a read tells you where a module landed once. It
  /// does not tell you where the module is: a manager of this era owns
  /// an arena, and it may shuffle a module inside that arena, or satisfy
  /// a call from a copy it already holds, without going near DOS. Both
  /// were observed on the program this tree is for — a module that
  /// landed at one segment and ran, one frame later, from the next one
  /// up, and the same module 0x73 paragraphs away from its landing after
  /// a screen's worth of loading — and a tracker whose only input is
  /// `note_read()` cannot see either happen.
  ///
  /// A manager that moves modules has to write down where it put them,
  /// or it could not call into them itself. That note is in the resident
  /// image, which does not move, and it is maintained by the very code
  /// that does the moving — so it is a better answer than ours to both
  /// questions a seam asks: *is this module in memory*, and *where*. The
  /// engine reads it at the step boundary rather than at arming, so a
  /// point qualified this way cannot go stale between reads (seam.h).
  ///
  /// It is a fact about a binary like every other one in a seam's table
  /// — an offset, and what the word there means — and it is qualified by
  /// the fingerprint like every other one.
  std::uint32_t load_segment_at{no_load_segment};

  [[nodiscard]] constexpr bool is_resident_image() const noexcept {
    return file.empty();
  }

  /// Whether the program's own record of this module's whereabouts is
  /// known, and so whether the engine resolves the point through it.
  [[nodiscard]] constexpr bool has_load_segment() const noexcept {
    return load_segment_at != no_load_segment;
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
