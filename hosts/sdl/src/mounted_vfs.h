// SPDX-License-Identifier: AGPL-3.0-only
//
// A filesystem seen from one directory down: `--install` (#397).
//
// The copies on sale are laid out for a launcher that mounts their
// folder so that it sits at `C:\POOLRAD` and changes into it. A player
// points this host at that folder — the one with `START.EXE` in it — and
// the program has to find itself at `\POOLRAD\`, because its
// configuration file says so and it saves where that file says. This is
// the wrapper that puts it there: every path at or below the mount point
// is the inner filesystem's, with the mount point taken off the front;
// the directories above it exist and hold nothing but the one on the way
// down; everything else is not there.
//
// Nothing here decides what a path means. It is handed canonical
// `dos_path` values, strips a prefix and hands canonical values on, which
// is the one thing a backend may do with a name (machine/vfs.h). The DOS
// answers for what is outside the mount are the ones a real disk with
// nothing else on it would give: a directory for an ancestor, "not
// found" below one, and a refusal to write anything beside the mount —
// there is no host directory behind that to write into, and a program
// that tried would have been told the same by a write-protected disk.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {

class mounted_filesystem final : public machine::filesystem {
 public:
  /// `inner`'s root appears at `at`. `inner` must outlive this.
  mounted_filesystem(machine::filesystem& inner, const machine::dos_path& at);

  [[nodiscard]] const machine::dos_path& mount_point() const noexcept {
    return at_;
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
  machine::vfs_result<machine::file_handle> create_file(
      const machine::dos_path& path) override;
  machine::vfs_result<std::size_t> write_file(
      machine::file_handle handle, std::span<const std::uint8_t> in) override;
  machine::vfs_error truncate_file(machine::file_handle handle) override;
  machine::vfs_error unlink_file(const machine::dos_path& path) override;
  machine::vfs_error make_directory(const machine::dos_path& path) override;

 private:
  /// `path` as the inner filesystem names it, when it is at or below the
  /// mount point.
  [[nodiscard]] std::optional<machine::dos_path> inside(
      const machine::dos_path& path) const noexcept;

  /// Whether `path` is strictly above the mount point: the root, or a
  /// directory on the way down to it.
  [[nodiscard]] bool above(const machine::dos_path& path) const noexcept;

  /// What a name outside the mount that is not above it answers: not
  /// found in a directory that exists, or no such directory.
  [[nodiscard]] machine::vfs_error missing(
      const machine::dos_path& path) const noexcept;

  machine::filesystem* inner_;
  machine::dos_path at_;
};

}  // namespace amberfolio::sdl
