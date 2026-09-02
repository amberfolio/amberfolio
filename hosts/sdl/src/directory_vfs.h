// SPDX-License-Identifier: AGPL-3.0-only
//
// The `filesystem` interface (core/machine/vfs.h) over a directory on the
// host. This is the shape M3 points at the player's own game files, so it
// is worth being exact about what it does and does not have to defend
// against.
//
//
// Root escape is prevented by the type, not by string checking
// -----------------------------------------------------------
//
// The issue that specifies this host (#54) calls a contained root "the
// host's one security-shaped obligation", and the obvious reading is that
// this file should be canonicalising paths and rejecting `..`. It should
// not, because by the time a path reaches a backend that work has already
// happened and cannot be undone:
//
//   * A backend is only ever handed a `dos_path`, and the only way to
//     build one from text is `canonicalize()`, which lives in core.
//   * `canonicalize()` resolves `.` and `..` itself, component by
//     component, and `..` at the root leaves the path at the root —
//     there is nothing above `C:\` to ascend to, exactly as DOS has it.
//     No `..` survives into a `dos_path`.
//   * Every surviving component is a `dos_name`, which `dos_name::parse`
//     validated against the legal 8.3 character set. `.` and `..` are not
//     legal names, and neither is anything containing a separator.
//   * `dos_path` is depth-bounded, so a path cannot grow without limit.
//
// So the containment argument is: a `dos_path` is a sequence of validated
// names, and this file joins them to the root with the host's own
// separator. There is no input to this backend that can name anything
// outside the root, because there is no way to express one in the type.
//
// What this file therefore has to do is *not reintroduce* the problem:
// never build a host path from anything but a `dos_path`'s components,
// and never hand the host a component it did not get from one. There is a
// belt-and-braces check in `host_path()` anyway — it re-rejects a
// component containing a separator, a colon, or a leading dot — because
// the cost is a few comparisons on a path that is about to hit the disk,
// and because a future change to `dos_name`'s charset should fail here
// loudly rather than silently widen what a program can reach.
//
//
// What a backend is not allowed to decide
// ---------------------------------------
//
// Case. vfs.h is emphatic that DOS name semantics live in core and that a
// backend "never makes a case-sensitivity decision" — which matters most
// here, because this is the backend that will run on a case-sensitive
// Linux filesystem and a case-insensitive Windows one. Names arrive
// upper-cased and canonical; a directory entry read back off the disk is
// matched by parsing it into a `dos_name` and comparing that, so
// `Save1.Dat` on disk and `SAVE1.DAT` in a program are the same file on
// every platform.
//
// Enumeration order. `entry_at()` must answer in `dos_name_less` order,
// pinned, because PLAN.md §4 makes a directory listing part of a replay's
// initial conditions. `std::filesystem::directory_iterator` gives
// whatever order the host filesystem feels like, which is exactly what
// that rule exists to keep out of machine state, so this sorts.
//
//
// Deliberately simple
// -------------------
//
// Handles are a fixed table of open `std::fstream`s. There is no cache,
// no write-back buffering, and no attempt to hold directory listings
// between calls: M2 needs a host that is correct, and the volume of file
// I/O a Gold Box binary does is nowhere near enough for any of that to
// pay for itself. If M3's boot log says otherwise, this is a small file
// to revisit.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {

class directory_filesystem final : public machine::filesystem {
 public:
  /// How many files may be open at once. The same bound
  /// `memory_filesystem` uses, and for the same reason: it sits inside
  /// the classic DOS `FILES=` default without pretending to reproduce
  /// DOS's own per-program handle table, which is the DOS layer's
  /// (core/machine/dos.h).
  static constexpr std::size_t max_open_handles = 16;

  /// `root` is the directory the emulated C: drive is. It must already
  /// exist; `usable()` says whether it did.
  explicit directory_filesystem(std::filesystem::path root);

  /// False if the root is missing or is not a directory. Checked once,
  /// at construction, so every later operation can assume it.
  [[nodiscard]] bool usable() const noexcept { return usable_; }

  [[nodiscard]] const std::filesystem::path& root() const noexcept {
    return root_;
  }

  machine::vfs_result<machine::file_handle> open(
      const machine::dos_path& path, machine::open_mode mode) override;
  machine::vfs_result<std::size_t> read(machine::file_handle handle,
                                        std::span<std::uint8_t> out) override;
  machine::vfs_result<std::uint32_t> seek(machine::file_handle handle,
                                          machine::seek_origin origin,
                                          std::int32_t offset) override;
  machine::vfs_error close(machine::file_handle handle) override;

  [[nodiscard]] bool exists(const machine::dos_path& path) const override;
  [[nodiscard]] machine::vfs_result<machine::file_stat> stat(
      const machine::dos_path& path) const override;
  [[nodiscard]] machine::vfs_result<std::size_t> entry_count(
      const machine::dos_path& dir) const override;
  [[nodiscard]] machine::vfs_result<machine::directory_entry> entry_at(
      const machine::dos_path& dir, std::size_t index) const override;

 protected:
  // The five machine::filesystem wraps so that `generation()` cannot be
  // forgotten (machine/vfs.h). Nothing here buffers, so there is nothing
  // for a close to commit and no sixth.
  machine::vfs_result<machine::file_handle> create_file(
      const machine::dos_path& path) override;
  machine::vfs_result<std::size_t> write_file(
      machine::file_handle handle, std::span<const std::uint8_t> in) override;
  machine::vfs_error truncate_file(machine::file_handle handle) override;
  machine::vfs_error unlink_file(const machine::dos_path& path) override;
  machine::vfs_error make_directory(const machine::dos_path& path) override;

 private:
  struct open_file {
    std::fstream stream;
    bool in_use{false};
    bool writable{false};
    std::filesystem::path where;
  };

  /// The host path `path` names, or an empty path if any component fails
  /// the belt-and-braces check described at the top of this file.
  [[nodiscard]] std::filesystem::path host_path(
      const machine::dos_path& path) const;

  /// Every entry of `dir` that this backend can name, in `dos_name_less`
  /// order. An entry whose on-disk name is not a legal 8.3 name is
  /// skipped: a program cannot ask for it, so reporting it would only
  /// produce a listing with a hole in it.
  [[nodiscard]] machine::vfs_result<std::vector<machine::directory_entry>>
  listing(const machine::dos_path& dir) const;

  [[nodiscard]] open_file* find(machine::file_handle handle) noexcept;

  std::filesystem::path root_;
  bool usable_{false};
  std::vector<open_file> handles_{max_open_handles};
};

}  // namespace amberfolio::sdl
