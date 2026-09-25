// SPDX-License-Identifier: AGPL-3.0-only
//
// `--install`'s wrapper (#397): a filesystem seen from one directory
// down. Over the in-memory backend, because what is under test is the
// prefix and the answers outside it, not a disk.

#include "mounted_vfs.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {
namespace {

using machine::dos_path;
using machine::open_mode;
using machine::vfs_error;

[[nodiscard]] dos_path path_of(std::string_view text) {
  const machine::vfs_result<dos_path> where =
      machine::canonicalize_host_path({text.data(), text.size()});
  EXPECT_TRUE(where.ok()) << text;
  return where.value;
}

struct rig {
  rig()
      : inner(std::make_unique<machine::memory_filesystem>()),
        outer(*inner, path_of("POOLRAD")) {
    const auto made = inner->create(path_of("START.EXE"));
    EXPECT_TRUE(made.ok());
    const std::array<std::uint8_t, 3> bytes{1, 2, 3};
    EXPECT_TRUE(inner->write(made.value, bytes).ok());
    EXPECT_EQ(inner->close(made.value), vfs_error::none);
  }

  /// On the heap: the in-memory backend's arena is megabytes.
  std::unique_ptr<machine::memory_filesystem> inner;
  mounted_filesystem outer;
};

TEST(MountedFilesystem, TheDirectoryGivenIsAtTheMountPoint) {
  rig r;
  EXPECT_TRUE(r.outer.exists(path_of("POOLRAD\\START.EXE")));
  EXPECT_FALSE(r.outer.exists(path_of("START.EXE")));
  const auto seen = r.outer.stat(path_of("POOLRAD\\START.EXE"));
  ASSERT_TRUE(seen.ok());
  EXPECT_EQ(seen.value.size, 3U);

  const auto opened =
      r.outer.open(path_of("POOLRAD\\START.EXE"), open_mode::read_only);
  ASSERT_TRUE(opened.ok());
  std::array<std::uint8_t, 8> back{};
  const auto got = r.outer.read(opened.value, back);
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, 3U);
  EXPECT_EQ(r.outer.close(opened.value), vfs_error::none);
}

TEST(MountedFilesystem, TheRootHoldsTheMountPointAndNothingElse) {
  rig r;
  const auto root = r.outer.stat(dos_path{});
  ASSERT_TRUE(root.ok());
  EXPECT_TRUE(root.value.is_directory);
  const auto mount = r.outer.stat(path_of("POOLRAD"));
  ASSERT_TRUE(mount.ok());
  EXPECT_TRUE(mount.value.is_directory);

  const auto count = r.outer.entry_count(dos_path{});
  ASSERT_TRUE(count.ok());
  EXPECT_EQ(count.value, 1U);
  const auto first = r.outer.entry_at(dos_path{}, 0);
  ASSERT_TRUE(first.ok());
  EXPECT_TRUE(first.value.is_directory);
  EXPECT_EQ(first.value.name, path_of("POOLRAD").leaf());
  EXPECT_EQ(r.outer.entry_at(dos_path{}, 1).error, vfs_error::no_more_files);

  // And the whole tree is the inner one, one directory down.
  EXPECT_EQ(machine::tree_file_count(r.outer), 1U);
}

TEST(MountedFilesystem, WritesLandInsideTheDirectoryGiven) {
  rig r;
  ASSERT_EQ(r.outer.mkdir(path_of("POOLRAD\\SAVE")), vfs_error::none);
  const auto made = r.outer.create(path_of("POOLRAD\\SAVE\\SAVGAMA.DAT"));
  ASSERT_TRUE(made.ok());
  const std::array<std::uint8_t, 2> bytes{9, 9};
  ASSERT_TRUE(r.outer.write(made.value, bytes).ok());
  ASSERT_EQ(r.outer.close(made.value), vfs_error::none);
  EXPECT_TRUE(r.inner->exists(path_of("SAVE\\SAVGAMA.DAT")));

  EXPECT_EQ(r.outer.unlink(path_of("POOLRAD\\SAVE\\SAVGAMA.DAT")),
            vfs_error::none);
  EXPECT_FALSE(r.inner->exists(path_of("SAVE\\SAVGAMA.DAT")));
}

TEST(MountedFilesystem, OutsideTheMountIsNotThereAndCannotBeWritten) {
  rig r;
  EXPECT_EQ(r.outer.open(path_of("GAME.OVR"), open_mode::read_only).error,
            vfs_error::file_not_found);
  EXPECT_EQ(r.outer.open(path_of("ELSE\\GAME.OVR"), open_mode::read_only).error,
            vfs_error::path_not_found);
  EXPECT_EQ(r.outer.open(dos_path{}, open_mode::read_only).error,
            vfs_error::access_denied);
  EXPECT_EQ(r.outer.create(path_of("TOP.DAT")).error, vfs_error::access_denied);
  EXPECT_EQ(r.outer.mkdir(path_of("OTHER")), vfs_error::access_denied);
  EXPECT_EQ(r.outer.unlink(path_of("TOP.DAT")), vfs_error::file_not_found);
  EXPECT_EQ(r.outer.entry_count(path_of("ELSE")).error,
            vfs_error::path_not_found);
}

}  // namespace
}  // namespace amberfolio::sdl
