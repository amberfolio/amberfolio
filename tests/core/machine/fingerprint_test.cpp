// SPDX-License-Identifier: AGPL-3.0-only
//
// The fingerprint of a file on the machine's filesystem.
//
// Two claims, and both of them are about the boundary rather than about
// SHA-256 (which sha256_test.cpp settles against the standard's own
// vectors): that the digest of a file is the digest of its bytes,
// whatever the backend did with them, and that a file this layer could
// not read produces an error rather than the digest of nothing — which
// is the one wrong answer that would look exactly like a right one.

#include "amberfolio/machine/fingerprint.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] dos_path path_of(std::string_view text) {
  const vfs_result<dos_path> where =
      canonicalize(dos_path{}, std::span<const char>(text.data(), text.size()));
  EXPECT_TRUE(where.ok()) << text;
  return where.value;
}

void put(memory_filesystem& fs, std::string_view name,
         std::span<const std::uint8_t> bytes) {
  const vfs_result<file_handle> made = fs.create(path_of(name));
  ASSERT_TRUE(made.ok());
  const vfs_result<std::size_t> wrote = fs.write(made.value, bytes);
  ASSERT_TRUE(wrote.ok());
  ASSERT_EQ(wrote.value, bytes.size());
  ASSERT_EQ(fs.close(made.value), vfs_error::none);
}

TEST(FingerprintFile, IsTheDigestOfTheBytesTheFileHolds) {
  const auto fs = std::make_unique<memory_filesystem>();

  // Long enough to cross several of the read chunks and not a multiple of
  // any of them, so the loop's last partial read is exercised.
  std::vector<std::uint8_t> content(70000);
  for (std::size_t i = 0; i < content.size(); ++i) {
    content[i] = static_cast<std::uint8_t>(i * 7u);
  }
  put(*fs, "BIG.DAT", content);

  const vfs_result<sha256_digest> got =
      fingerprint_file(*fs, path_of("BIG.DAT"));
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, sha256(content));
}

TEST(FingerprintFile, HashesAnEmptyFileAsTheEmptyMessage) {
  const auto fs = std::make_unique<memory_filesystem>();
  put(*fs, "EMPTY.DAT", {});

  const vfs_result<sha256_digest> got =
      fingerprint_file(*fs, path_of("EMPTY.DAT"));
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, sha256({}));
}

TEST(FingerprintFile, RefusesAFileThatIsNotThere) {
  const auto fs = std::make_unique<memory_filesystem>();

  const vfs_result<sha256_digest> got =
      fingerprint_file(*fs, path_of("GONE.DAT"));
  EXPECT_FALSE(got.ok());
  EXPECT_EQ(got.error, vfs_error::file_not_found);
  // And answers no digest at all, rather than the digest of the empty
  // message — which a caller comparing against a table would otherwise
  // read as a real, and wrong, identity.
  EXPECT_NE(got.value, sha256({}));
}

TEST(FingerprintFile, LeavesNoHandleBehindWhenItFails) {
  const auto fs = std::make_unique<memory_filesystem>();

  // More failed fingerprints than the backend has handles. If any of them
  // leaked one, the last would fail with `too_many_open_files` instead of
  // with the error it is actually about.
  for (std::size_t i = 0; i <= memory_filesystem::max_open_handles; ++i) {
    const vfs_result<sha256_digest> got =
        fingerprint_file(*fs, path_of("GONE.DAT"));
    ASSERT_EQ(got.error, vfs_error::file_not_found) << "attempt " << i;
  }

  put(*fs, "HERE.DAT", {});
  EXPECT_TRUE(fingerprint_file(*fs, path_of("HERE.DAT")).ok());
}

}  // namespace
}  // namespace amberfolio::machine
