// SPDX-License-Identifier: AGPL-3.0-only
//
// The memory VFS backend: the full operation set (open/create/read/write/
// seek/close/unlink/mkdir/exists/stat), the errors each one produces, and
// the pinned enumeration order. This is the suite the issue's exit
// criterion names — pass this, and #51/#52 build on `filesystem` without
// the interface changing.

#include "amberfolio/machine/memory_vfs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] std::span<const char> raw(std::string_view text) {
  return {text.data(), text.size()};
}

[[nodiscard]] dos_path path(std::initializer_list<std::string_view> parts) {
  dos_path result;
  for (const auto part : parts) {
    const auto parsed = dos_name::parse(raw(part));
    EXPECT_TRUE(parsed.ok());
    EXPECT_TRUE(result.push(parsed.value));
  }
  return result;
}

[[nodiscard]] dos_name name(std::string_view text) {
  const auto parsed = dos_name::parse(raw(text));
  EXPECT_TRUE(parsed.ok());
  return parsed.value;
}

/// std::span has no operator==; compare contents instead of trying to
/// compare the views.
[[nodiscard]] bool bytes_equal(std::span<const std::uint8_t> a,
                               std::span<const std::uint8_t> b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
}

/// On the heap, always — memory_vfs.h says why: `memory_filesystem`
/// carries 8 MiB and a default thread stack does not.
[[nodiscard]] std::unique_ptr<memory_filesystem> make() {
  return std::make_unique<memory_filesystem>();
}

// --- create / open / read / write / close -------------------------------

TEST(memory_vfs_create, makes_a_new_file_open_for_read_and_write) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());

  const std::uint8_t written[] = {1, 2, 3, 4};
  const auto wrote = fs->write(handle.value, written);
  ASSERT_TRUE(wrote.ok());
  EXPECT_EQ(wrote.value, 4u);

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 0).ok());
  std::uint8_t read_back[4] = {};
  const auto got = fs->read(handle.value, read_back);
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, 4u);
  EXPECT_TRUE(bytes_equal(read_back, written));
}

TEST(memory_vfs_create, truncates_an_existing_file_rather_than_failing) {
  const auto fs = make();
  const auto first = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(first.ok());
  const std::uint8_t data[] = {9, 9, 9, 9, 9};
  ASSERT_TRUE(fs->write(first.value, data).ok());
  ASSERT_EQ(fs->close(first.value), vfs_error::none);

  const auto second = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(second.ok());
  const auto info = fs->stat(path({"GAME.DAT"}));
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value.size, 0u);
}

TEST(memory_vfs_create, refuses_the_root) {
  const auto fs = make();
  EXPECT_EQ(fs->create(dos_path{}).error, vfs_error::access_denied);
}

TEST(memory_vfs_create, refuses_a_name_that_is_already_a_directory) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_EQ(fs->create(path({"SAVE"})).error, vfs_error::access_denied);
}

TEST(memory_vfs_create, reports_path_not_found_when_the_parent_is_missing) {
  const auto fs = make();
  EXPECT_EQ(fs->create(path({"SAVE", "1.DAT"})).error,
            vfs_error::path_not_found);
}

TEST(memory_vfs_open, reports_file_not_found_for_a_missing_leaf) {
  const auto fs = make();
  EXPECT_EQ(fs->open(path({"GAME.DAT"}), open_mode::read_only).error,
            vfs_error::file_not_found);
}

TEST(memory_vfs_open, reports_path_not_found_when_an_ancestor_is_missing) {
  const auto fs = make();
  EXPECT_EQ(fs->open(path({"SAVE", "1.DAT"}), open_mode::read_only).error,
            vfs_error::path_not_found);
}

TEST(memory_vfs_open, refuses_a_directory) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_EQ(fs->open(path({"SAVE"}), open_mode::read_only).error,
            vfs_error::access_denied);
}

TEST(memory_vfs_open, refuses_the_root) {
  const auto fs = make();
  EXPECT_EQ(fs->open(dos_path{}, open_mode::read_only).error,
            vfs_error::access_denied);
}

TEST(memory_vfs_read, refuses_a_handle_opened_write_only) {
  const auto fs = make();
  const auto created = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(created.ok());
  ASSERT_EQ(fs->close(created.value), vfs_error::none);

  const auto handle = fs->open(path({"GAME.DAT"}), open_mode::write_only);
  ASSERT_TRUE(handle.ok());
  std::uint8_t buffer[1];
  EXPECT_EQ(fs->read(handle.value, buffer).error, vfs_error::access_denied);
}

TEST(memory_vfs_write, refuses_a_handle_opened_read_only) {
  const auto fs = make();
  const auto created = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(created.ok());
  ASSERT_EQ(fs->close(created.value), vfs_error::none);

  const auto handle = fs->open(path({"GAME.DAT"}), open_mode::read_only);
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[1] = {1};
  EXPECT_EQ(fs->write(handle.value, data).error, vfs_error::access_denied);
}

TEST(memory_vfs_read, past_end_of_file_returns_zero_and_is_not_an_error) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  std::uint8_t buffer[4] = {};
  const auto got = fs->read(handle.value, buffer);
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, 0u);
}

TEST(memory_vfs_read_write, use_independent_positions_and_operations) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[] = {1, 2, 3};
  ASSERT_TRUE(fs->write(handle.value, data).ok());
  // Position is now 3 (end of what was written); a read from here sees
  // end of file rather than the bytes just written.
  std::uint8_t buffer[3] = {};
  const auto got = fs->read(handle.value, buffer);
  ASSERT_TRUE(got.ok());
  EXPECT_EQ(got.value, 0u);
}

TEST(memory_vfs_write_past_eof, zero_fills_the_gap) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 4).ok());
  const std::uint8_t tail[] = {0xAA};
  ASSERT_TRUE(fs->write(handle.value, tail).ok());

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 0).ok());
  std::uint8_t all[5] = {};
  const auto got = fs->read(handle.value, all);
  ASSERT_TRUE(got.ok());
  ASSERT_EQ(got.value, 5u);
  const std::uint8_t expected[] = {0, 0, 0, 0, 0xAA};
  EXPECT_TRUE(bytes_equal(all, expected));
}

TEST(memory_vfs_write, a_short_write_at_capacity_is_not_an_error) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  ASSERT_TRUE(
      fs->seek(handle.value, seek_origin::begin,
               static_cast<std::int32_t>(memory_filesystem::max_file_size - 1))
          .ok());
  const std::uint8_t data[] = {1, 2, 3};
  const auto wrote = fs->write(handle.value, data);
  ASSERT_TRUE(wrote.ok());
  EXPECT_EQ(wrote.value, 1u);
}

// --- truncate --------------------------------------------------------------

TEST(memory_vfs_truncate, shrinks_to_the_current_position) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[] = {1, 2, 3, 4, 5};
  ASSERT_TRUE(fs->write(handle.value, data).ok());

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 2).ok());
  EXPECT_EQ(fs->truncate(handle.value), vfs_error::none);

  const auto info = fs->stat(path({"GAME.DAT"}));
  ASSERT_TRUE(info.ok());
  EXPECT_EQ(info.value.size, 2u);
}

TEST(memory_vfs_truncate, extends_with_zero_fill_past_the_old_end) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[] = {1, 2, 3};
  ASSERT_TRUE(fs->write(handle.value, data).ok());

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 5).ok());
  EXPECT_EQ(fs->truncate(handle.value), vfs_error::none);

  ASSERT_TRUE(fs->seek(handle.value, seek_origin::begin, 0).ok());
  std::uint8_t all[5] = {};
  const auto got = fs->read(handle.value, all);
  ASSERT_TRUE(got.ok());
  ASSERT_EQ(got.value, 5u);
  const std::uint8_t expected[] = {1, 2, 3, 0, 0};
  EXPECT_TRUE(bytes_equal(all, expected));
}

TEST(memory_vfs_truncate, refuses_a_handle_opened_read_only) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  ASSERT_EQ(fs->close(handle.value), vfs_error::none);

  const auto ro = fs->open(path({"GAME.DAT"}), open_mode::read_only);
  ASSERT_TRUE(ro.ok());
  EXPECT_EQ(fs->truncate(ro.value), vfs_error::access_denied);
}

TEST(memory_vfs_truncate, reports_invalid_handle_for_a_handle_never_opened) {
  const auto fs = make();
  EXPECT_EQ(fs->truncate(file_handle{.slot = 0}), vfs_error::invalid_handle);
}

// --- seek ----------------------------------------------------------------

TEST(memory_vfs_seek, clamps_below_zero_to_zero) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const auto result = fs->seek(handle.value, seek_origin::begin, -10);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value, 0u);
}

TEST(memory_vfs_seek, from_current_and_from_end) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[] = {1, 2, 3, 4, 5};
  ASSERT_TRUE(fs->write(handle.value, data).ok());

  auto at_end = fs->seek(handle.value, seek_origin::end, 0);
  ASSERT_TRUE(at_end.ok());
  EXPECT_EQ(at_end.value, 5u);

  auto back_two = fs->seek(handle.value, seek_origin::current, -2);
  ASSERT_TRUE(back_two.ok());
  EXPECT_EQ(back_two.value, 3u);
}

// --- close / invalid handles ---------------------------------------------

TEST(memory_vfs_close, invalidates_the_handle) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  ASSERT_EQ(fs->close(handle.value), vfs_error::none);
  EXPECT_EQ(fs->close(handle.value), vfs_error::invalid_handle);
}

TEST(memory_vfs_operations, report_invalid_handle_for_a_handle_never_opened) {
  const auto fs = make();
  EXPECT_EQ(fs->close(invalid_file_handle), vfs_error::invalid_handle);
  std::uint8_t buffer[1];
  EXPECT_EQ(fs->read(invalid_file_handle, buffer).error,
            vfs_error::invalid_handle);
  EXPECT_EQ(fs->write(invalid_file_handle, buffer).error,
            vfs_error::invalid_handle);
  EXPECT_EQ(fs->seek(invalid_file_handle, seek_origin::begin, 0).error,
            vfs_error::invalid_handle);
}

TEST(memory_vfs_open_handles, are_bounded_and_report_too_many_open_files) {
  const auto fs = make();
  // Left open on purpose: max_open_handles is a bound on concurrently
  // open files, not on how many exist, so nothing here is closed until
  // the object itself is torn down.
  for (std::size_t i = 0; i < memory_filesystem::max_open_handles; ++i) {
    const auto handle = fs->create(path({std::to_string(i) + ".DAT"}));
    ASSERT_TRUE(handle.ok()) << i;
  }

  const auto overflow = fs->create(path({"OVERFLOW.DAT"}));
  EXPECT_EQ(overflow.error, vfs_error::too_many_open_files);
}

// --- unlink ----------------------------------------------------------------

TEST(memory_vfs_unlink, removes_a_file) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  ASSERT_EQ(fs->close(handle.value), vfs_error::none);

  EXPECT_EQ(fs->unlink(path({"GAME.DAT"})), vfs_error::none);
  EXPECT_FALSE(fs->exists(path({"GAME.DAT"})));
}

TEST(memory_vfs_unlink, reports_file_not_found_for_a_missing_file) {
  const auto fs = make();
  EXPECT_EQ(fs->unlink(path({"GAME.DAT"})), vfs_error::file_not_found);
}

TEST(memory_vfs_unlink, refuses_a_directory) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_EQ(fs->unlink(path({"SAVE"})), vfs_error::access_denied);
}

TEST(memory_vfs_unlink, refuses_a_file_with_an_open_handle) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  EXPECT_EQ(fs->unlink(path({"GAME.DAT"})), vfs_error::access_denied);
}

TEST(memory_vfs_unlink, refuses_the_root) {
  const auto fs = make();
  EXPECT_EQ(fs->unlink(dos_path{}), vfs_error::access_denied);
}

// --- mkdir / exists / stat -------------------------------------------------

TEST(memory_vfs_mkdir, creates_a_directory_files_can_then_live_under) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  const auto handle = fs->create(path({"SAVE", "1.DAT"}));
  EXPECT_TRUE(handle.ok());
}

TEST(memory_vfs_mkdir, refuses_a_name_already_taken) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::access_denied);
}

TEST(memory_vfs_mkdir, reports_path_not_found_for_a_missing_parent) {
  const auto fs = make();
  EXPECT_EQ(fs->mkdir(path({"SAVE", "SLOT1"})), vfs_error::path_not_found);
}

TEST(memory_vfs_mkdir, exhausts_into_directory_full) {
  const auto fs = make();
  for (std::size_t i = 0; i < memory_filesystem::max_entries; ++i) {
    ASSERT_EQ(fs->mkdir(path({std::to_string(i)})), vfs_error::none) << i;
  }
  EXPECT_EQ(fs->mkdir(path({"OVERFLOW"})), vfs_error::directory_full);
}

TEST(memory_vfs_exists, is_true_for_the_root_files_and_directories) {
  const auto fs = make();
  EXPECT_TRUE(fs->exists(dos_path{}));
  EXPECT_FALSE(fs->exists(path({"GAME.DAT"})));

  ASSERT_TRUE(fs->create(path({"GAME.DAT"})).ok());
  EXPECT_TRUE(fs->exists(path({"GAME.DAT"})));

  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_TRUE(fs->exists(path({"SAVE"})));
}

TEST(memory_vfs_stat, reports_size_and_kind) {
  const auto fs = make();
  const auto handle = fs->create(path({"GAME.DAT"}));
  ASSERT_TRUE(handle.ok());
  const std::uint8_t data[] = {1, 2, 3};
  ASSERT_TRUE(fs->write(handle.value, data).ok());

  const auto file_info = fs->stat(path({"GAME.DAT"}));
  ASSERT_TRUE(file_info.ok());
  EXPECT_EQ(file_info.value.size, 3u);
  EXPECT_FALSE(file_info.value.is_directory);

  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  const auto dir_info = fs->stat(path({"SAVE"}));
  ASSERT_TRUE(dir_info.ok());
  EXPECT_TRUE(dir_info.value.is_directory);

  const auto root_info = fs->stat(dos_path{});
  ASSERT_TRUE(root_info.ok());
  EXPECT_TRUE(root_info.value.is_directory);
}

TEST(memory_vfs_stat, reports_file_not_found_and_path_not_found) {
  const auto fs = make();
  EXPECT_EQ(fs->stat(path({"GAME.DAT"})).error, vfs_error::file_not_found);
  EXPECT_EQ(fs->stat(path({"SAVE", "1.DAT"})).error, vfs_error::path_not_found);
}

// --- entry_count / entry_at: the pinned enumeration order -----------------

TEST(memory_vfs_enumerate, lists_direct_children_in_name_order) {
  const auto fs = make();
  ASSERT_TRUE(fs->create(path({"CHARLIE.DAT"})).ok());
  ASSERT_TRUE(fs->create(path({"ALPHA.DAT"})).ok());
  ASSERT_TRUE(fs->create(path({"BRAVO.DAT"})).ok());

  const auto count = fs->entry_count(dos_path{});
  ASSERT_TRUE(count.ok());
  ASSERT_EQ(count.value, 3u);

  const auto first = fs->entry_at(dos_path{}, 0);
  const auto second = fs->entry_at(dos_path{}, 1);
  const auto third = fs->entry_at(dos_path{}, 2);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(third.ok());

  EXPECT_EQ(first.value.name, name("ALPHA.DAT"));
  EXPECT_EQ(second.value.name, name("BRAVO.DAT"));
  EXPECT_EQ(third.value.name, name("CHARLIE.DAT"));
}

TEST(memory_vfs_enumerate, does_not_cross_directory_boundaries) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  ASSERT_TRUE(fs->create(path({"SAVE", "1.DAT"})).ok());
  ASSERT_TRUE(fs->create(path({"ROOT.DAT"})).ok());

  const auto root_count = fs->entry_count(dos_path{});
  ASSERT_TRUE(root_count.ok());
  EXPECT_EQ(root_count.value, 2u);  // ROOT.DAT and the SAVE directory.

  const auto save_count = fs->entry_count(path({"SAVE"}));
  ASSERT_TRUE(save_count.ok());
  EXPECT_EQ(save_count.value, 1u);
  const auto entry = fs->entry_at(path({"SAVE"}), 0);
  ASSERT_TRUE(entry.ok());
  EXPECT_EQ(entry.value.name, name("1.DAT"));
}

TEST(memory_vfs_enumerate, mixes_files_and_directories_by_name) {
  const auto fs = make();
  ASSERT_TRUE(fs->create(path({"B.DAT"})).ok());
  ASSERT_EQ(fs->mkdir(path({"A"})), vfs_error::none);

  const auto first = fs->entry_at(dos_path{}, 0);
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(first.value.name, name("A"));
  EXPECT_TRUE(first.value.is_directory);

  const auto second = fs->entry_at(dos_path{}, 1);
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(second.value.name, name("B.DAT"));
  EXPECT_FALSE(second.value.is_directory);
}

TEST(memory_vfs_enumerate, order_is_unaffected_by_creation_order) {
  const auto fs = make();
  const char* names[] = {"ZEBRA.DAT", "MANGO.DAT", "APPLE.DAT", "KIWI.DAT"};
  for (const char* n : names) {
    ASSERT_TRUE(fs->create(path({n})).ok());
  }

  const char* expected[] = {"APPLE.DAT", "KIWI.DAT", "MANGO.DAT", "ZEBRA.DAT"};
  for (std::size_t i = 0; i < 4; ++i) {
    const auto entry = fs->entry_at(dos_path{}, i);
    ASSERT_TRUE(entry.ok()) << i;
    EXPECT_EQ(entry.value.name, name(expected[i])) << i;
  }
}

TEST(memory_vfs_entry_at, reports_no_more_files_past_the_end) {
  const auto fs = make();
  ASSERT_TRUE(fs->create(path({"A.DAT"})).ok());
  EXPECT_EQ(fs->entry_at(dos_path{}, 1).error, vfs_error::no_more_files);
}

TEST(memory_vfs_entry_count, reports_path_not_found_for_a_missing_directory) {
  const auto fs = make();
  EXPECT_EQ(fs->entry_count(path({"SAVE"})).error, vfs_error::path_not_found);
}

TEST(memory_vfs_entry_count, refuses_a_file) {
  const auto fs = make();
  ASSERT_TRUE(fs->create(path({"GAME.DAT"})).ok());
  EXPECT_EQ(fs->entry_count(path({"GAME.DAT"})).error,
            vfs_error::access_denied);
}

// --- The arena (#84) --------------------------------------------------------
//
// One pool of bytes shared between files, kept packed, with no holes
// (memory_vfs.h). Everything above this line is about the interface and
// would read the same whatever the storage was; these cases are about the
// storage, and every one of them is a way the packing could be wrong
// while every test above stayed green.

/// Fill `name` with `count` bytes derived from `seed`, through the
/// ordinary create/write/close path.
void put(memory_filesystem& fs, std::string_view leaf, std::size_t count,
         std::uint8_t seed) {
  const auto made = fs.create(path({leaf}));
  ASSERT_TRUE(made.ok());
  std::vector<std::uint8_t> bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::uint8_t>(seed + i);
  }
  const auto wrote = fs.write(made.value, bytes);
  ASSERT_TRUE(wrote.ok());
  ASSERT_EQ(wrote.value, count);
  ASSERT_EQ(fs.close(made.value), vfs_error::none);
}

/// Every byte of `leaf`, read back through the ordinary path.
[[nodiscard]] std::vector<std::uint8_t> get(memory_filesystem& fs,
                                            std::string_view leaf) {
  const auto opened = fs.open(path({leaf}), open_mode::read_only);
  EXPECT_TRUE(opened.ok());
  std::vector<std::uint8_t> bytes(memory_filesystem::max_file_size);
  const auto read = fs.read(opened.value, bytes);
  EXPECT_TRUE(read.ok());
  bytes.resize(read.value);
  EXPECT_EQ(fs.close(opened.value), vfs_error::none);
  return bytes;
}

/// What `put()` would have written, for comparison.
[[nodiscard]] std::vector<std::uint8_t> expected(std::size_t count,
                                                 std::uint8_t seed) {
  std::vector<std::uint8_t> bytes(count);
  for (std::size_t i = 0; i < count; ++i) {
    bytes[i] = static_cast<std::uint8_t>(seed + i);
  }
  return bytes;
}

TEST(memory_vfs_arena, accounts_for_exactly_what_the_files_hold) {
  const auto fs = make();
  EXPECT_EQ(fs->bytes_used(), 0u);

  put(*fs, "A.DAT", 100, 1);
  EXPECT_EQ(fs->bytes_used(), 100u);
  put(*fs, "B.DAT", 250, 2);
  EXPECT_EQ(fs->bytes_used(), 350u);

  ASSERT_EQ(fs->unlink(path({"A.DAT"})), vfs_error::none);
  EXPECT_EQ(fs->bytes_used(), 250u);
}

TEST(memory_vfs_arena, a_directory_costs_no_bytes) {
  const auto fs = make();
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);
  EXPECT_EQ(fs->bytes_used(), 0u);
}

TEST(memory_vfs_arena, growing_a_file_does_not_disturb_the_ones_after_it) {
  const auto fs = make();
  put(*fs, "FIRST.DAT", 64, 1);
  put(*fs, "MID.DAT", 64, 2);
  put(*fs, "LAST.DAT", 64, 3);

  // Rewrite the first file, longer. Everything after it has to move, and
  // this is the case that says whether the offsets moved with it.
  put(*fs, "FIRST.DAT", 4096, 4);

  EXPECT_EQ(get(*fs, "FIRST.DAT"), expected(4096, 4));
  EXPECT_EQ(get(*fs, "MID.DAT"), expected(64, 2));
  EXPECT_EQ(get(*fs, "LAST.DAT"), expected(64, 3));
  EXPECT_EQ(fs->bytes_used(), 4096u + 64u + 64u);
}

TEST(memory_vfs_arena, shrinking_a_file_does_not_disturb_the_ones_after_it) {
  const auto fs = make();
  put(*fs, "FIRST.DAT", 4096, 1);
  put(*fs, "MID.DAT", 64, 2);
  put(*fs, "LAST.DAT", 64, 3);

  put(*fs, "FIRST.DAT", 16, 4);

  EXPECT_EQ(get(*fs, "FIRST.DAT"), expected(16, 4));
  EXPECT_EQ(get(*fs, "MID.DAT"), expected(64, 2));
  EXPECT_EQ(get(*fs, "LAST.DAT"), expected(64, 3));
  EXPECT_EQ(fs->bytes_used(), 16u + 64u + 64u);
}

TEST(memory_vfs_arena, unlinking_from_the_middle_keeps_the_rest_intact) {
  const auto fs = make();
  put(*fs, "A.DAT", 300, 1);
  put(*fs, "B.DAT", 300, 2);
  put(*fs, "C.DAT", 300, 3);

  ASSERT_EQ(fs->unlink(path({"B.DAT"})), vfs_error::none);

  EXPECT_EQ(get(*fs, "A.DAT"), expected(300, 1));
  EXPECT_EQ(get(*fs, "C.DAT"), expected(300, 3));
  EXPECT_EQ(fs->bytes_used(), 600u);
}

TEST(memory_vfs_arena, growing_a_file_from_empty_moves_only_the_others) {
  // The case the `&other != &e` guard in `resize()` exists for: a
  // zero-length file's range starts exactly where the tail does, so a
  // careless fix-up pass would shift the file being grown along with
  // everything after it.
  const auto fs = make();
  put(*fs, "A.DAT", 128, 1);
  ASSERT_TRUE(fs->create(path({"EMPTY.DAT"})).ok());
  put(*fs, "B.DAT", 128, 2);

  put(*fs, "EMPTY.DAT", 512, 3);

  EXPECT_EQ(get(*fs, "A.DAT"), expected(128, 1));
  EXPECT_EQ(get(*fs, "EMPTY.DAT"), expected(512, 3));
  EXPECT_EQ(get(*fs, "B.DAT"), expected(128, 2));
}

TEST(memory_vfs_arena, a_gap_left_by_a_seek_reads_back_as_zero) {
  const auto fs = make();
  put(*fs, "OTHER.DAT", 64, 9);

  const auto made = fs->create(path({"SPARSE.DAT"}));
  ASSERT_TRUE(made.ok());
  ASSERT_TRUE(fs->seek(made.value, seek_origin::begin, 1000).ok());
  const std::array<std::uint8_t, 1> tail = {0xAB};
  ASSERT_TRUE(fs->write(made.value, tail).ok());
  ASSERT_EQ(fs->close(made.value), vfs_error::none);

  const std::vector<std::uint8_t> back = get(*fs, "SPARSE.DAT");
  ASSERT_EQ(back.size(), 1001u);
  EXPECT_TRUE(std::all_of(back.begin(), back.begin() + 1000,
                          [](std::uint8_t b) { return b == 0; }));
  EXPECT_EQ(back[1000], 0xAB);
  EXPECT_EQ(get(*fs, "OTHER.DAT"), expected(64, 9));
}

TEST(memory_vfs_arena, holds_a_whole_game_directorys_worth_of_files) {
  // The requirement #84 raised these bounds for: a Gold Box installation
  // is around a hundred and twenty files. Sixteen kilobytes apiece is
  // more than the average one weighs and the total is well inside the
  // arena, so what this proves is that the *entry table* is the size the
  // header claims and the packing holds up over a hundred and fifty
  // resizes.
  const auto fs = make();
  constexpr std::size_t files = 150;
  constexpr std::size_t each = 16 * 1024;

  for (std::size_t i = 0; i < files; ++i) {
    const std::string leaf = "F" + std::to_string(i) + ".DAT";
    put(*fs, leaf, each, static_cast<std::uint8_t>(i));
  }

  EXPECT_EQ(fs->bytes_used(), files * each);
  const auto count = fs->entry_count(dos_path{});
  ASSERT_TRUE(count.ok());
  EXPECT_EQ(count.value, files);

  // And every one of them still reads back what it was given, which is
  // the claim a hundred and fifty tail-moves could have broken anywhere.
  for (std::size_t i = 0; i < files; ++i) {
    const std::string leaf = "F" + std::to_string(i) + ".DAT";
    EXPECT_EQ(get(*fs, leaf), expected(each, static_cast<std::uint8_t>(i)))
        << leaf;
  }
}

TEST(memory_vfs_arena, a_write_past_the_end_of_the_arena_is_a_short_count) {
  const auto fs = make();

  // Fill it to within a hundred bytes, in max-sized files.
  constexpr std::size_t big = memory_filesystem::max_file_size;
  constexpr std::size_t whole = memory_filesystem::arena_bytes / big;
  for (std::size_t i = 0; i < whole; ++i) {
    put(*fs, "B" + std::to_string(i) + ".DAT", big, 0);
  }
  ASSERT_EQ(fs->bytes_used(), whole * big);

  const auto made = fs->create(path({"OVER.DAT"}));
  ASSERT_TRUE(made.ok());
  const std::vector<std::uint8_t> more(1024, 0x5A);
  const auto wrote = fs->write(made.value, more);

  // Not an error: DOS's own AH=40h answers a full disk with a short
  // count, and so does this (vfs.h's note on `directory_full`).
  ASSERT_TRUE(wrote.ok());
  EXPECT_EQ(wrote.value, memory_filesystem::arena_bytes - (whole * big));
  EXPECT_EQ(fs->bytes_used(), memory_filesystem::arena_bytes);
  ASSERT_EQ(fs->close(made.value), vfs_error::none);
}

TEST(memory_vfs_arena, a_write_capped_by_the_file_size_is_still_a_short_count) {
  // The bound that was there before the arena, unchanged by it.
  const auto fs = make();
  const auto made = fs->create(path({"HUGE.DAT"}));
  ASSERT_TRUE(made.ok());
  const std::vector<std::uint8_t> bytes(memory_filesystem::max_file_size + 512);
  const auto wrote = fs->write(made.value, bytes);
  ASSERT_TRUE(wrote.ok());
  EXPECT_EQ(wrote.value, memory_filesystem::max_file_size);
  ASSERT_EQ(fs->close(made.value), vfs_error::none);
}

// --- clear() ---------------------------------------------------------------

TEST(memory_vfs_clear, empties_every_entry_and_every_handle) {
  const auto fs = make();
  ASSERT_TRUE(fs->create(path({"GAME.DAT"})).ok());
  ASSERT_EQ(fs->mkdir(path({"SAVE"})), vfs_error::none);

  fs->clear();

  EXPECT_FALSE(fs->exists(path({"GAME.DAT"})));
  EXPECT_FALSE(fs->exists(path({"SAVE"})));
  const auto count = fs->entry_count(dos_path{});
  ASSERT_TRUE(count.ok());
  EXPECT_EQ(count.value, 0u);
  EXPECT_EQ(fs->bytes_used(), 0u);
}

}  // namespace
}  // namespace amberfolio::machine
