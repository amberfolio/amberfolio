// SPDX-License-Identifier: AGPL-3.0-only
//
// Where the player's directory appears (install.h, #397). The edition-row
// half needs a whole copy of an edition to say yes, which no test here has;
// what can be held down is the flag, and that anything short of a whole,
// recognised copy stays at the root.

#include "install.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {
namespace {

[[nodiscard]] machine::dos_path path_of(std::string_view text) {
  const machine::vfs_result<machine::dos_path> where =
      machine::canonicalize_host_path({text.data(), text.size()});
  EXPECT_TRUE(where.ok()) << text;
  return where.value;
}

TEST(SettleInstall, TheFlagDecidesWhenItIsGiven) {
  const auto files = std::make_unique<machine::memory_filesystem>();
  const install_choice given = settle_install("\\POOLRAD", "START.EXE", *files);
  EXPECT_TRUE(given.ok);
  EXPECT_EQ(given.directory, path_of("POOLRAD"));
  EXPECT_STREQ(given.from, "--install");

  const install_choice root = settle_install("/", "START.EXE", *files);
  EXPECT_TRUE(root.ok);
  EXPECT_TRUE(root.directory.is_root());
}

TEST(SettleInstall, AFlagThatIsNoDirectoryIsRefused) {
  const auto files = std::make_unique<machine::memory_filesystem>();
  EXPECT_FALSE(settle_install("A:\\GAMES", "START.EXE", *files).ok);
  EXPECT_FALSE(settle_install("\\NINELETTER", "START.EXE", *files).ok);
}

TEST(SettleInstall, AProgramNoEditionNamesStaysAtTheRoot) {
  const auto files = std::make_unique<machine::memory_filesystem>();
  const auto made = files->create(path_of("GAME.EXE"));
  ASSERT_TRUE(made.ok());
  const std::array<std::uint8_t, 2> bytes{'M', 'Z'};
  ASSERT_TRUE(files->write(made.value, bytes).ok());
  ASSERT_EQ(files->close(made.value), machine::vfs_error::none);

  const install_choice choice = settle_install("", "GAME.EXE", *files);
  EXPECT_TRUE(choice.ok);
  EXPECT_TRUE(choice.directory.is_root());
  EXPECT_STREQ(choice.from, "the root");

  // And one that is not there at all.
  EXPECT_TRUE(settle_install("", "NONE.EXE", *files).directory.is_root());
}

}  // namespace
}  // namespace amberfolio::sdl
