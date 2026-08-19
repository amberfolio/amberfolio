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
// handful of floppies holding well under a hundred files between the
// program, its overlays, its data files and whatever save slots and
// journal fixtures the enhancements (PLAN.md §5) add. Every bound below
// is picked against that target, stated here rather than left to be
// discovered by an exhausted table failing a test nobody expected to
// fail.
//
//   * **`max_entries` files and directories, combined, 32.** "A few
//     dozen files" is the issue's own phrase for the size class; a
//     directory is an entry like any other (an empty one, structurally),
//     so files and subdirectories share the one table rather than two
//     separately-sized ones that would have to be reasoned about
//     together anyway. Sixty-some real files plus a dozen save slots
//     plus a directory or two is comfortably inside it; a filesystem that
//     needed hundreds of entries would not be this game's.
//   * **`max_file_size`, 256 KiB per file.** `scripts/check-clean.sh`
//     already draws this exact line — a file over 256 KiB tracked in
//     this repository is flagged as plausibly a game asset
//     (CONTRIBUTING.md) — so a backend built for testing and for the wasm
//     dev page has no principled reason to hold anything bigger than the
//     largest thing this project already refuses to commit. Gold Box
//     era executables, overlays and data files are all comfortably under
//     it.
//   * **`max_open_handles`, 16.** Not DOS's own per-program handle table
//     — that is #52's, at the INT 21h layer, and it is DOS state this
//     interface deliberately does not model (vfs.h's top comment). This
//     is a different, lower-level bound: how many files *this backend*
//     can have mid-operation at once. Sixteen sits comfortably inside
//     the classic DOS default (`CONFIG.SYS FILES=`, commonly shipped at
//     20) without pretending to reproduce that table here.
//
// Each file's bytes are stored in a fixed `std::array<std::uint8_t,
// max_file_size>` inside its own entry — not a shared byte arena carved
// up between files — because the fixed-capacity-table house pattern
// (`memory_map::max_windows`, `port_map::max_ranges`,
// `machine::max_devices`) is exactly this: an array sized for the worst
// case, indexed directly, with no allocator standing between a slot and
// its owner. A shared arena would need its own allocation scheme
// (compaction when a file shrinks, a place to put the freed bytes) for a
// problem `max_entries * max_file_size` — a documented 8 MiB — already
// solves by being small enough not to need one.
//
// **This object is not a thing to put on a stack.** Same rule
// `memory_map.h` states for its own megabyte, for the same reason: 32
// entries of up to 256 KiB is 8 MiB of storage inside `memory_filesystem`,
// two orders of magnitude past a default thread stack. Heap-allocate it
// (`std::make_unique<memory_filesystem>()`, as every test here does) and
// every write to it goes to a specific `entry`'s specific field — never
// through an aggregate-initialized temporary of the whole `entry` type,
// which would put a 256 KiB copy on the stack to build the very thing
// this file exists to avoid keeping there.
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
  static constexpr std::size_t max_entries = 32;
  static constexpr std::size_t max_file_size = std::size_t{256} * 1024;
  static constexpr std::size_t max_open_handles = 16;

  memory_filesystem() noexcept = default;

  /// Back to the empty root directory: every entry and every open handle
  /// gone. Not part of `filesystem` — there is no RESET line for a
  /// filesystem, and PLAN.md §4 makes VFS contents part of a replay's
  /// *initial* conditions, set up once before a run starts rather than
  /// something the machine itself ever resets — but a test, or a dev
  /// page reloading a fresh game image, wants a clean slate without
  /// paying for a new 8 MiB object.
  void clear() noexcept;

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
    std::uint32_t length{};
    std::array<std::uint8_t, max_file_size> data{};
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

  std::array<entry, max_entries> entries_{};
  std::array<open_file, max_open_handles> handles_{};
};

}  // namespace amberfolio::machine
