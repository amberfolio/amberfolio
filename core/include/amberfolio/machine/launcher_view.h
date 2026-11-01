// SPDX-License-Identifier: AGPL-3.0-only
//
// The configuration a launcher writes for this machine (#404).
//
// The program reads its settings out of its configuration file, and every
// seller's launcher writes that file for the machine it is about to
// start. Which file and which line says what is a fact about the program
// (`launcher_configuration`, edition.h): `POOL.CFG` and `CURSE.CFG` both
// say the graphics adapter on line 1 and **the sound device on line 2**
// — `P` the PC speaker, `T` a Tandy sound chip, `S` silent — and then
// the directories, the save directory on line 4 of `POOL.CFG` and line 3
// of `CURSE.CFG` (`save_layer.h` reads it). One storefront's launcher
// starts a DOSBox with a Tandy chip and says `T`; the other and the
// repack say `P`.
//
// This machine has the Tandy chip (tandy_sound.h), and the program only
// plays its music through it: told `P` it has the speaker's beeps and
// nothing more. So the host, which is the launcher here (`docs/hosts.md`
// §2c), starts every copy the way the Tandy launcher does: a view over a
// filesystem that answers a **read-only open of a configuration file**
// any known edition reads with the file's own bytes and its sound line's
// letter read as `T`. Everything else — every other file, every other
// line, the file opened for writing, its size and its directory entry —
// is the inner filesystem's, unchanged. So:
//
//   * the file a host keeps (the page writes the configuration file back into the
//     copy's store, the desktop reads the player's own folder) is the
//     one the player has, and a copy still matches its edition row and
//     still says what it said when they take it elsewhere;
//   * nothing under this is game code, a seam or machine state. It is
//     what a launcher chose before the program started, the same kind of
//     fact as which directory is current, and every host and every
//     replay makes the same choice, so a recording needs no line for it.
//
// A file with no sound line, or one too big to be a launcher's, is
// answered unchanged: there is no letter to read differently.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

/// The sound line's letters, as the configuration file spells them.
inline constexpr std::uint8_t configured_sound_speaker = 'P';
inline constexpr std::uint8_t configured_sound_tandy = 'T';
inline constexpr std::uint8_t configured_sound_silent = 'S';

/// What every copy is started with on this machine.
inline constexpr std::uint8_t sound_for_this_machine = configured_sound_tandy;

/// Where line `line` (one-based) begins in `bytes`, when it has a first
/// character: the byte after the `line - 1`th LF, if there is one before
/// the end and it is not itself the end of the line. Line 1 begins at the
/// first byte.
[[nodiscard]] std::optional<std::size_t> configured_line_offset(
    std::span<const std::uint8_t> bytes, std::size_t line) noexcept;

/// The configuration file a program started in `directory` reads: the
/// first known edition's (edition.h) whose file is there, and its path.
/// What a host reads before any program is loaded, when which of them
/// will run is not yet known; no two known programs read the same name,
/// and a copy holds the one its own program reads.
struct found_configuration {
  /// Null when no known edition's configuration file is in `directory`.
  const launcher_configuration* layout{nullptr};
  dos_path path{};

  [[nodiscard]] bool found() const noexcept { return layout != nullptr; }
};
[[nodiscard]] found_configuration find_configuration_in(
    const filesystem& fs, const dos_path& directory);

/// What a copy's configuration file says about sound.
struct configured_sound {
  /// The file's name (`POOL.CFG`, `CURSE.CFG`).
  std::string_view file;
  /// The sound line's first character, as the file says it.
  std::uint8_t letter{0};
};

/// The sound line of the configuration file in `current_directory`, as
/// the file says it: `std::nullopt` for a file that is absent,
/// unreadable, or has no sound line. What a host reports before a load;
/// it reads the inner filesystem and changes nothing.
[[nodiscard]] std::optional<configured_sound> read_configured_sound(
    filesystem& fs, const dos_path& current_directory);

/// The view described above. `inner` must outlive it.
class launcher_view final : public filesystem {
 public:
  explicit launcher_view(filesystem& inner) noexcept : inner_(&inner) {}

  /// How many configuration-file opens may be answered from a changed copy at
  /// once. The program opens it once, reads it and closes it; a fifth
  /// open while four are held is refused as `too_many_open_files`, the
  /// capacity answer every backend gives, rather than quietly handed the
  /// file unchanged.
  static constexpr std::size_t max_shown_files = 4;

  /// The largest configuration file this reads whole. The launchers write 24 to
  /// 40 bytes; a bigger file is answered unchanged.
  static constexpr std::size_t max_shown_size = 512;

  /// How many opens have been answered with the sound line changed. A host says
  /// so once; nothing depends on the count.
  [[nodiscard]] std::uint32_t shown() const noexcept { return shown_count_; }

  vfs_result<file_handle> open(const dos_path& path, open_mode mode) override;
  vfs_result<std::size_t> read(file_handle handle,
                               std::span<std::uint8_t> out) override;
  vfs_result<std::uint32_t> seek(file_handle handle, seek_origin origin,
                                 std::int32_t offset) override;
  vfs_error close(file_handle handle) override;

  [[nodiscard]] bool exists(const dos_path& path) const override;
  [[nodiscard]] vfs_result<file_stat> stat(const dos_path& path) const override;
  [[nodiscard]] vfs_result<std::size_t> entry_count(
      const dos_path& dir) const override;
  [[nodiscard]] vfs_result<directory_entry> entry_at(
      const dos_path& dir, std::size_t index) const override;

 protected:
  vfs_result<file_handle> create_file(const dos_path& path) override;
  vfs_result<std::size_t> write_file(file_handle handle,
                                     std::span<const std::uint8_t> in) override;
  vfs_error truncate_file(file_handle handle) override;
  vfs_error unlink_file(const dos_path& path) override;
  vfs_error make_directory(const dos_path& path) override;

 private:
  /// One open answered from a changed copy.
  struct shown_file {
    bool open{};
    std::size_t size{};
    std::uint32_t position{};
    std::array<std::uint8_t, max_shown_size> bytes{};
  };

  /// Handles this view hands out itself, far above any slot a backend
  /// here numbers from zero.
  static constexpr std::uint32_t first_shown_slot = 0xFFFFFF00U;

  [[nodiscard]] shown_file* shown_of(file_handle handle) noexcept;

  filesystem* inner_;
  std::array<shown_file, max_shown_files> shown_files_{};
  std::uint32_t shown_count_{};
};

}  // namespace amberfolio::machine
