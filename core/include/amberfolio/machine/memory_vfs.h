// SPDX-License-Identifier: AGPL-3.0-only
//
// The in-memory `filesystem` backend (vfs.h): the test backend, the
// M2 dev page's backend until #55 lands IndexedDB, and the reference for
// what any other backend must do — everything in here is answerable
// purely from `vfs.h`'s contract, with no host access of any kind, which
// is exactly the property a directory-backed (#54) or IndexedDB-backed
// (#55) implementation has to reproduce.
//
//
// The capacities, and the reasoning behind each number
// -----------------------------------------------------
//
// This is not a general-purpose filesystem and is not trying to become
// one — see PLAN.md §3's target: one game, whose entire distribution is a
// handful of floppies holding a hundred-odd files between the program,
// its overlays, its data files and whatever save slots and journal
// fixtures the enhancements (PLAN.md §5) add. Every bound below is picked
// against that target, stated here rather than left to be discovered by
// an exhausted table failing a test nobody expected to fail.
//
//   * **`max_entries` files and directories, combined, 512.** A
//     directory is an entry like any other (an empty one, structurally),
//     so files and subdirectories share the one table rather than two
//     separately-sized ones that would have to be reasoned about
//     together anyway — and that sharing is the whole of why this number
//     has to be counted rather than eyeballed.
//
//     Count it. A Gold Box installation is a flat directory of about a
//     hundred and twenty files — the program, an overlay file, and a
//     long tail of data files. On top of that an archive release ships
//     `SAVE\` already populated: one directory and *seventy-odd* slot
//     files, before a player has saved anything. That is 195 entries on
//     a disk nobody has played yet, and then a party of six writes a
//     character file each into `SAVE\` per game saved.
//
//     This was 192 until #158, argued from the hundred and twenty alone
//     with "several dozen save slots" of headroom that the shipped slots
//     had already spent. The failure it produced is the one worth
//     naming, because it is not the one the number suggests: the table
//     filled *mid-installation*, seven data files were refused after it,
//     and the game booted, ran and fought without them — a browser was
//     running an installation with holes in it, invisible until
//     something asked for a wall definition. The save that could never
//     be written was merely the symptom loud enough to notice.
//
//     Five hundred and twelve is set clear of the real high-water mark
//     rather than a step past it — two and a half times a shipped
//     installation, so the room left over is for what a *player*
//     accumulates and not for the disk they arrived with. A bound set
//     *at* the count somebody measured is a bound that refuses the next
//     disk, which is exactly what 192 was. It is also `replay.h`'s
//     `replay_max_manifest_entries` exactly, and that is not a
//     coincidence to be tidied away: a recording that can name 512
//     entries and a backend that can hold fewer is a disk describable on
//     one host and unloadable on the other.
//
//     It costs what a table of fixed-size entries costs and nothing
//     else. An `entry` is a `dos_path` and two offsets — 116 bytes on a
//     64-bit target — so 320 more of them is 37,120 bytes, and
//     `sizeof(memory_filesystem)` goes from 8,411,280 to 8,448,400: four
//     tenths of one percent, all of it beside an arena that was already
//     8 MiB. Measured rather than estimated, because #155 found a real
//     wasm stack overflow from growth of exactly this kind — but nothing
//     here is ever on a stack (see below), so this is static or heap
//     bytes and not stack ones. The arena is untouched: a whole
//     installation weighs about 1.6 MB of the 8 MiB it already has.
//   * **`max_file_size`, 256 KiB per file.** `scripts/check-clean.sh`
//     already draws this exact line — a file over 256 KiB tracked in
//     this repository is flagged as plausibly a game asset
//     (CONTRIBUTING.md) — so a backend built for testing and for the wasm
//     dev page has no principled reason to hold anything bigger than the
//     largest thing this project already refuses to commit. Gold Box
//     era executables, overlays and data files are all comfortably under
//     it.
//   * **`arena_bytes`, 8 MiB across all files.** Five or six times what
//     a whole Gold Box installation weighs, so a dev page can take a
//     player's directory and still have room for everything the game
//     writes back.
//   * **`max_open_handles`, 16.** Not DOS's own per-program handle table
//     — that is #52's, at the INT 21h layer, and it is DOS state this
//     interface deliberately does not model (vfs.h's top comment). This
//     is a different, lower-level bound: how many files *this backend*
//     can have mid-operation at once. Sixteen sits comfortably inside
//     the classic DOS default (`CONFIG.SYS FILES=`, commonly shipped at
//     20) without pretending to reproduce that table here.
//
//
// One arena, kept packed
// -----------------------
//
// File bytes live in a single `arena_bytes` array, each file holding an
// offset and a length into it; the used region is always a packed prefix
// with no holes, and a file that changes size moves whatever follows it.
//
// This was a fixed `std::array<std::uint8_t, max_file_size>` per entry
// until M3-F2 (#84), and the argument for that was the
// fixed-capacity-table house pattern (`memory_map::max_windows`,
// `port_map::max_ranges`, `machine::max_devices`): an array sized for the
// worst case, indexed directly, with no allocator between a slot and its
// owner. What changed is the requirement, not the taste. A dev page that
// takes a player's directory needs many times the entries, and
// `max_entries` of them at a fixed 256 KiB apiece is 128 MiB of static
// storage to hold a megabyte and a half of files — most of it, in a
// browser, committed linear memory that never holds a byte. That the
// bound could then be raised in #158 for the cost of a table row apiece,
// rather than of a quarter-megabyte apiece, is this scheme paying for
// itself a second time.
//
// The scheme is deliberately not an allocator. There is no free list, no
// fragmentation and nothing to tune: growing a file shifts the tail right
// and shrinking it shifts the tail left, both by one `move_bytes()` plus
// a pass over `max_entries` offsets. That is memory traffic proportional
// to what happens to sit after the file, which sounds alarming and is
// not: a newly created file is placed at the end, where the tail is
// empty, so the case a program actually exercises — writing a save file
// it just created — moves nothing at all.
//
// The one behaviour this introduces is that bytes are now a capacity a
// caller can exhaust: a `write()` that would run past `arena_bytes`
// writes what fits and answers a short count, which is exactly what
// `write()` already did at `max_file_size` and exactly what DOS's own
// AH=40h does on a full disk.
//
// **This object is not a thing to put on a stack.** Same rule
// `memory_map.h` states for its own megabyte, for the same reason: the
// arena alone is 8 MiB, two orders of magnitude past a default thread
// stack and further still past wasm's. Heap-allocate it
// (`std::make_unique<memory_filesystem>()`, as every test here does), or
// give it static storage (`abi.cpp`'s `reference_devices` holds one by
// value in a namespace-scope buffer, which is where the wasm module's
// filesystem lives). Those two are the only places one has ever been,
// which is why `max_entries` can be sized against a real disk rather
// than against a stack frame.
//
//
// What determines enumeration order
// -----------------------------------
//
// `entry_at()` does not keep a sorted index; at `max_entries`'s size, a
// fresh selection scan per call — find the smallest name strictly after
// the last one chosen, `index + 1` times — costs nothing worth avoiding
// and needs no bookkeeping kept in step with `create()`/`mkdir()`/
// `unlink()`. The order it produces is `dos_name_less` (vfs.h), which is
// what "pinned" means throughout this file: a pure function of what
// currently exists, never of insertion order, never of where a freed
// slot happened to be reused.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

class memory_filesystem final : public filesystem {
 public:
  static constexpr std::size_t max_entries = 512;
  static constexpr std::size_t max_file_size = std::size_t{256} * 1024;
  static constexpr std::size_t arena_bytes = std::size_t{8} * 1024 * 1024;
  static constexpr std::size_t max_open_handles = 16;

  memory_filesystem() noexcept = default;

  /// Back to the empty root directory: every entry, every open handle and
  /// every byte of the arena gone. Not part of `filesystem` — there is no
  /// RESET line for a filesystem, and PLAN.md §4 makes VFS contents part
  /// of a replay's *initial* conditions, set up once before a run starts
  /// rather than something the machine itself ever resets — but a test,
  /// or a dev page taking a second directory from the player, wants a
  /// clean slate without paying for a new 8 MiB object.
  ///
  /// Moves `generation()` (vfs.h): an emptied filesystem is a changed
  /// one, and it moves *forward* like every other change — a host that
  /// saw the counter go back to zero would read a wiped disk as one
  /// nothing had happened to.
  void clear() noexcept;

  /// Bytes of the arena currently held by files. What a dev page shows a
  /// player who has just dropped a directory on it, and what a test
  /// asserts when the claim is about capacity rather than about content.
  [[nodiscard]] std::size_t bytes_used() const noexcept { return used_; }

  [[nodiscard]] vfs_result<file_handle> open(const dos_path& path,
                                             open_mode mode) override;
  [[nodiscard]] vfs_result<std::size_t> read(
      file_handle handle, std::span<std::uint8_t> out) override;
  [[nodiscard]] vfs_result<std::uint32_t> seek(file_handle handle,
                                               seek_origin origin,
                                               std::int32_t offset) override;
  vfs_error close(file_handle handle) override;
  [[nodiscard]] bool exists(const dos_path& path) const override;
  [[nodiscard]] vfs_result<file_stat> stat(const dos_path& path) const override;
  [[nodiscard]] vfs_result<std::size_t> entry_count(
      const dos_path& dir) const override;
  [[nodiscard]] vfs_result<directory_entry> entry_at(
      const dos_path& dir, std::size_t index) const override;

 protected:
  // The five vfs.h wraps so that `generation()` cannot be forgotten.
  vfs_result<file_handle> create_file(const dos_path& path) override;
  vfs_result<std::size_t> write_file(file_handle handle,
                                     std::span<const std::uint8_t> in) override;
  vfs_error truncate_file(file_handle handle) override;
  vfs_error unlink_file(const dos_path& path) override;
  vfs_error make_directory(const dos_path& path) override;

 private:
  struct entry {
    bool in_use{};
    bool is_directory{};
    dos_path path;

    /// Where this file's bytes start in the arena, and how many there
    /// are. Meaningless for a directory, which owns no bytes and is
    /// never passed to `resize()`.
    std::uint32_t offset{};
    std::uint32_t length{};
  };

  struct open_file {
    bool in_use{};
    std::size_t entry_index{};
    std::uint32_t position{};
    open_mode mode{};
  };

  [[nodiscard]] entry* find(const dos_path& path) noexcept;
  [[nodiscard]] const entry* find(const dos_path& path) const noexcept;

  /// Whether `path`'s parent resolves to an existing directory (the root
  /// counts). What distinguishes `file_not_found` from `path_not_found`
  /// everywhere below.
  [[nodiscard]] bool parent_exists(const dos_path& path) const noexcept;

  [[nodiscard]] vfs_error missing_path_error(
      const dos_path& path) const noexcept;

  /// `max_entries` / `max_open_handles` when nothing is free — the
  /// exhaustion answer, never a silent overwrite of something in use.
  [[nodiscard]] std::size_t free_entry_slot() const noexcept;
  [[nodiscard]] std::size_t free_handle_slot() const noexcept;

  /// Give `e` exactly `new_length` bytes, moving whatever follows it in
  /// the arena and fixing up every offset that moved. False, and nothing
  /// changed, if growing it would run past `arena_bytes`.
  ///
  /// The bytes an extension exposes are **not** zeroed here: `write()`
  /// and `truncate()` each know which part of the extension they are
  /// about to overwrite and which part is a gap that has to read as zero,
  /// and blanking the whole range first would zero bytes that are about
  /// to be written over anyway.
  [[nodiscard]] bool resize(entry& e, std::uint32_t new_length) noexcept;

  /// The largest `e` could be made, given `max_file_size` and what is
  /// left of the arena.
  [[nodiscard]] std::uint32_t growth_ceiling(const entry& e) const noexcept;

  std::array<entry, max_entries> entries_{};
  std::array<open_file, max_open_handles> handles_{};

  /// Every file's bytes, packed into `[0, used_)` in no particular order
  /// — see "One arena, kept packed" above.
  std::array<std::uint8_t, arena_bytes> arena_{};
  std::size_t used_{};
};

}  // namespace amberfolio::machine
