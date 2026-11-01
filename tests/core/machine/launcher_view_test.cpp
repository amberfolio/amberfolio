// SPDX-License-Identifier: AGPL-3.0-only
//
// The launcher's view of a configuration file (launcher_view.h, #404): a read-only
// open of it answers the sound line as the Tandy chip, and
// nothing else a program or a host can do sees anything but the file.

#include "amberfolio/machine/launcher_view.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// The repack's launcher's file, which says the speaker.
constexpr std::string_view speaker_config =
    "E\r\nP\r\nC:\\\r\nC:\\SAVE\\\r\nF\r\n";

[[nodiscard]] dos_path path_of(std::string_view text) {
  const vfs_result<dos_path> where =
      canonicalize_host_path({text.data(), text.size()});
  EXPECT_TRUE(where.ok()) << "not a path: " << text;
  return where.ok() ? where.value : dos_path{};
}

void put(filesystem& fs, std::string_view path, std::string_view text) {
  const vfs_result<file_handle> made = fs.create(path_of(path));
  ASSERT_TRUE(made.ok());
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
  ASSERT_TRUE(fs.write(made.value, std::span(bytes, text.size())).ok());
  ASSERT_EQ(fs.close(made.value), vfs_error::none);
}

[[nodiscard]] std::string read_all(filesystem& fs, std::string_view path,
                                   open_mode mode = open_mode::read_only) {
  const vfs_result<file_handle> file = fs.open(path_of(path), mode);
  EXPECT_TRUE(file.ok());
  if (!file.ok()) {
    return {};
  }
  std::array<std::uint8_t, 256> bytes{};
  const vfs_result<std::size_t> got = fs.read(file.value, bytes);
  EXPECT_TRUE(got.ok());
  EXPECT_EQ(fs.close(file.value), vfs_error::none);
  return {reinterpret_cast<const char*>(bytes.data()), got.value};
}

[[nodiscard]] std::string with_sound(char letter) {
  std::string text(speaker_config);
  text[3] = letter;
  return text;
}

TEST(LauncherView, TheProgramReadsTheTandyChip) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", speaker_config);
  launcher_view view(disk);

  EXPECT_EQ(read_all(view, "POOL.CFG"), with_sound('T'));
  EXPECT_EQ(view.shown(), 1u);
}

TEST(LauncherView, TheFileItselfIsUntouched) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", speaker_config);
  launcher_view view(disk);

  static_cast<void>(read_all(view, "POOL.CFG"));
  EXPECT_EQ(read_all(disk, "POOL.CFG"), speaker_config);
  EXPECT_EQ(disk.generation(), 2u);  // the put's create and write only
}

TEST(LauncherView, EveryLetterIsReadAsTandy) {
  for (const char letter : {'P', 'S', 'T', 'X'}) {
    auto owned = std::make_unique<memory_filesystem>();
    memory_filesystem& disk = *owned;
    put(disk, "POOL.CFG", with_sound(letter));
    launcher_view view(disk);
    EXPECT_EQ(read_all(view, "POOL.CFG"), with_sound('T')) << letter;
  }
}

TEST(LauncherView, AFileThatAlreadySaysTandyIsHandedOverAsItIs) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", with_sound('T'));
  launcher_view view(disk);
  EXPECT_EQ(read_all(view, "POOL.CFG"), with_sound('T'));
  EXPECT_EQ(view.shown(), 0u);
}

TEST(LauncherView, ItReadsTheFileInAnyDirectory) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  ASSERT_EQ(disk.mkdir(path_of("POOLRAD")), vfs_error::none);
  put(disk, "POOLRAD\\POOL.CFG", speaker_config);
  launcher_view view(disk);
  EXPECT_EQ(read_all(view, "POOLRAD\\POOL.CFG"), with_sound('T'));
}

TEST(LauncherView, OtherFilesAndOtherModesAreTheDisks) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", speaker_config);
  put(disk, "OTHER.CFG", speaker_config);
  launcher_view view(disk);

  EXPECT_EQ(read_all(view, "OTHER.CFG"), speaker_config);
  EXPECT_EQ(read_all(view, "POOL.CFG", open_mode::read_write), speaker_config);
  EXPECT_EQ(view.shown(), 0u);
}

TEST(LauncherView, AFileWithNoSecondLineIsLeftAlone) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", "E\r\n");
  launcher_view view(disk);
  EXPECT_EQ(read_all(view, "POOL.CFG"), "E\r\n");
}

TEST(LauncherView, AShownCopySeeksAndCannotBeWritten) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", speaker_config);
  launcher_view view(disk);

  const vfs_result<file_handle> file =
      view.open(path_of("POOL.CFG"), open_mode::read_only);
  ASSERT_TRUE(file.ok());
  const vfs_result<std::uint32_t> at =
      view.seek(file.value, seek_origin::begin, 3);
  ASSERT_TRUE(at.ok());
  std::array<std::uint8_t, 1> one{};
  ASSERT_TRUE(view.read(file.value, one).ok());
  EXPECT_EQ(one[0], 'T');

  const std::array<std::uint8_t, 1> byte{'P'};
  EXPECT_EQ(view.write(file.value, byte).error, vfs_error::access_denied);
  EXPECT_EQ(view.seek(file.value, seek_origin::end, 10).value,
            speaker_config.size());
  EXPECT_EQ(view.close(file.value), vfs_error::none);
  EXPECT_EQ(view.close(file.value), vfs_error::invalid_handle);
}

TEST(LauncherView, AHostReadsWhatTheFileSays) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  put(disk, "POOL.CFG", speaker_config);
  const std::optional<configured_sound> said =
      read_configured_sound(disk, dos_path{});
  ASSERT_TRUE(said.has_value());
  EXPECT_EQ(said.value_or(configured_sound{}).file, "POOL.CFG");
  EXPECT_EQ(said.value_or(configured_sound{}).letter, 'P');

  auto none = std::make_unique<memory_filesystem>();
  memory_filesystem& empty = *none;
  EXPECT_FALSE(read_configured_sound(empty, dos_path{}).has_value());
}

// --- Curse of the Azure Bonds' file (#551) ---------------------------------

/// What a Curse launcher that says the speaker would write: four lines,
/// the save directory on the third.
constexpr std::string_view curse_speaker_config =
    "E\r\nP\r\nC:\\CURSE\\SAVE\\\r\nF\r\n";

TEST(LauncherView, CursesFileIsReadAsTandyToo) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  ASSERT_EQ(disk.mkdir(path_of("CURSE")), vfs_error::none);
  put(disk, "CURSE\\CURSE.CFG", curse_speaker_config);
  launcher_view view(disk);

  std::string tandy(curse_speaker_config);
  tandy[3] = 'T';
  EXPECT_EQ(read_all(view, "CURSE\\CURSE.CFG"), tandy);
  EXPECT_EQ(view.shown(), 1u);
  EXPECT_EQ(read_all(disk, "CURSE\\CURSE.CFG"), curse_speaker_config);

  const std::optional<configured_sound> said =
      read_configured_sound(disk, path_of("CURSE"));
  ASSERT_TRUE(said.has_value());
  EXPECT_EQ(said.value_or(configured_sound{}).file, "CURSE.CFG");
  EXPECT_EQ(said.value_or(configured_sound{}).letter, 'P');
}

TEST(LauncherView, EachProgramsFileIsFoundWhereItIsStarted) {
  auto owned = std::make_unique<memory_filesystem>();
  memory_filesystem& disk = *owned;
  ASSERT_EQ(disk.mkdir(path_of("CURSE")), vfs_error::none);
  put(disk, "POOL.CFG", speaker_config);
  put(disk, "CURSE\\CURSE.CFG", curse_speaker_config);

  const found_configuration pool = find_configuration_in(disk, dos_path{});
  ASSERT_TRUE(pool.found());
  EXPECT_EQ(pool.layout->file, "POOL.CFG");
  EXPECT_EQ(pool.layout->save_line, 4u);

  const found_configuration curse =
      find_configuration_in(disk, path_of("CURSE"));
  ASSERT_TRUE(curse.found());
  EXPECT_EQ(curse.layout->file, "CURSE.CFG");
  EXPECT_EQ(curse.layout->save_line, 3u);
  EXPECT_EQ(curse.path, path_of("CURSE\\CURSE.CFG"));

  auto none = std::make_unique<memory_filesystem>();
  EXPECT_FALSE(find_configuration_in(*none, dos_path{}).found());
}

TEST(LauncherView, ALineIsFoundByItsNumber) {
  const std::string_view text = "E\r\nP\r\n\r\nF";
  const std::span<const std::uint8_t> bytes(
      reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
  EXPECT_EQ(configured_line_offset(bytes, 1), std::optional<std::size_t>{0});
  EXPECT_EQ(configured_line_offset(bytes, 2), std::optional<std::size_t>{3});
  EXPECT_EQ(configured_line_offset(bytes, 3), std::nullopt) << "empty";
  EXPECT_EQ(configured_line_offset(bytes, 4), std::optional<std::size_t>{8});
  EXPECT_EQ(configured_line_offset(bytes, 5), std::nullopt) << "past the end";
  EXPECT_EQ(configured_line_offset(bytes, 0), std::nullopt);
}

}  // namespace
}  // namespace amberfolio::machine
