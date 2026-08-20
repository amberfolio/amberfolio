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
//   * **`max_entries` files and directories, combined, 192.** A Gold Box
//     installation is a flat directory of somewhere around a hundred and
//     twenty files — the program, an overlay file, and a long tail of
//     data files. A directory is an entry like any other (an empty one,
//     structurally), so files and subdirectories share the one table
//     rather than two separately-sized ones that would have to be
//     reasoned about together anyway. A hundred and ninety-two leaves
//     that distribution room for a save directory and several dozen save
//     slots on top; a filesystem that needed more would not be this
//     game's.
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
// takes a player's directory needs six times the entries, and six times
// the entries at a fixed 256 KiB apiece is 48 MiB of static storage to
// hold a megabyte and a half of files — most of it, in a browser,
// committed linear memory that never holds a byte.
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
// stack. Heap-allocate it (`std::make_unique<memory_filesystem>()`, as
// every test here does).
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
  static constexpr std::size_t max_entries = 192;
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
  void clear() noexcept;

  /// Bytes of the arena currently held by files. What a dev page shows a
  /// player who has just dropped a directory on it, and what a test
  /// asserts when the claim is about capacity rather than about content.
  [[nodiscard]] std::size_t bytes_used() const noexcept { return used_; }

  [[nodiscard]] vfs_result<file_handle> open(const dos_path& path,
                                             open_mode mode) override;
  [[nodiscard]] vfs_result<file_handle> create(const dos_path& path) override;
  [[nodiscard]] vfs_result<std::size_t> read(
      file_handle handle, std::span<std::uint8_t> out) override;
  [[nodiscard]] vfs_result<std::size_t> write(
      file_handle handle, std::span<const std::uint8_t> in) override;
  [[nodiscard]] vfs_result<std::uint32_t> seek(file_handle handle,
                                               seek_origin origin,
                                               std::int32_t offset) override;
  vfs_error truncate(file_handle handle) override;
  vfs_error close(file_handle handle) override;
  vfs_error unlink(const dos_path& path) override;
  vfs_error mkdir(const dos_path& path) override;
  [[nodiscard]] bool exists(const dos_path& path) const override;
  [[nodiscard]] vfs_result<file_stat> stat(const dos_path& path) const override;
  [[nodiscard]] vfs_result<std::size_t> entry_count(
      const dos_path& dir) const override;
  [[nodiscard]] vfs_result<directory_entry> entry_at(
      const dos_path& dir, std::size_t index) const override;

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
