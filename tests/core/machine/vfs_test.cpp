// SPDX-License-Identifier: AGPL-3.0-only
//
// The DOS name semantics on their own: `dos_name::parse`, `dos_path`, and
// `canonicalize()` — the part of vfs.h every backend relies on never
// having to redo. Operating a filesystem over these types is
// memory_vfs_test.cpp's job; this file is only about what a name and a
// path canonicalize to.

#include "amberfolio/machine/vfs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/memory_vfs.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// A raw path as `canonicalize()` wants it: a span over the characters,
/// nothing more. Test-side convenience only — core itself never sees a
/// `std::string_view`.
[[nodiscard]] std::span<const char> raw(std::string_view text) {
  return {text.data(), text.size()};
}

[[nodiscard]] dos_path canonical(const dos_path& cwd, std::string_view text) {
  const auto result = canonicalize(cwd, raw(text));
  EXPECT_TRUE(result.ok()) << "canonicalizing \"" << text << "\"";
  return result.value;
}

[[nodiscard]] dos_path path(std::initializer_list<std::string_view> parts) {
  dos_path result;
  for (const auto part : parts) {
    const auto parsed = dos_name::parse(raw(part));
    EXPECT_TRUE(parsed.ok()) << part;
    EXPECT_TRUE(result.push(parsed.value));
  }
  return result;
}

// --- dos_name::parse -----------------------------------------------------

TEST(dos_name_parse, upper_cases_a_lower_case_name) {
  const auto result = dos_name::parse(raw("readme.txt"));
  ASSERT_TRUE(result.ok());
  const auto text = result.value.text();
  EXPECT_EQ(std::string_view(text.data(), text.size()), "README.TXT");
}

TEST(dos_name_parse, accepts_a_name_with_no_extension) {
  const auto result = dos_name::parse(raw("readme"));
  ASSERT_TRUE(result.ok());
  const auto text = result.value.text();
  EXPECT_EQ(std::string_view(text.data(), text.size()), "README");
}

TEST(dos_name_parse, accepts_the_longest_legal_name) {
  const auto result = dos_name::parse(raw("ABCDEFGH.IJK"));
  ASSERT_TRUE(result.ok());
  const auto text = result.value.text();
  EXPECT_EQ(std::string_view(text.data(), text.size()), "ABCDEFGH.IJK");
}

TEST(dos_name_parse, accepts_the_documented_special_characters) {
  // One at a time: the legal set has more members than an 8-character
  // name has room for.
  for (const char c : std::string_view("!#$%&'-@^_`{}~")) {
    const char text[1] = {c};
    EXPECT_TRUE(dos_name::parse(raw(std::string_view(text, 1))).ok())
        << "character '" << c << "'";
  }
}

TEST(dos_name_parse, refuses_a_name_over_eight_characters) {
  EXPECT_EQ(dos_name::parse(raw("ABCDEFGHI")).error, vfs_error::path_not_found);
}

TEST(dos_name_parse, refuses_an_extension_over_three_characters) {
  EXPECT_EQ(dos_name::parse(raw("A.BCDE")).error, vfs_error::path_not_found);
}

TEST(dos_name_parse, refuses_an_empty_name) {
  EXPECT_EQ(dos_name::parse(raw(".TXT")).error, vfs_error::path_not_found);
  EXPECT_EQ(dos_name::parse(raw("")).error, vfs_error::path_not_found);
}

TEST(dos_name_parse, refuses_more_than_one_dot) {
  EXPECT_EQ(dos_name::parse(raw("A.B.C")).error, vfs_error::path_not_found);
}

TEST(dos_name_parse, refuses_a_space) {
  EXPECT_EQ(dos_name::parse(raw("A B")).error, vfs_error::path_not_found);
}

TEST(dos_name_parse, refuses_fat_reserved_punctuation) {
  for (const char c : std::string_view(R"(" * + , / : ; < = > ? \ [ ] |)")) {
    if (c == ' ') {
      continue;
    }
    const char text[2] = {c, '\0'};
    EXPECT_EQ(dos_name::parse(raw(std::string_view(text, 1))).error,
              vfs_error::path_not_found)
        << "character '" << c << "'";
  }
}

TEST(dos_name_equality, is_case_sensitive_over_the_already_canonical_form) {
  // Names compare byte for byte once canonical — the folding already
  // happened in parse(), so this is just confirming two independently
  // parsed equal names really do compare equal.
  const auto a = dos_name::parse(raw("Save.Dat"));
  const auto b = dos_name::parse(raw("SAVE.DAT"));
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(a.value, b.value);
}

TEST(dos_name_less,
     orders_lexicographically_and_a_prefix_before_its_extension) {
  const auto a = dos_name::parse(raw("A")).value;
  const auto ab = dos_name::parse(raw("AB")).value;
  const auto b = dos_name::parse(raw("B")).value;

  EXPECT_TRUE(dos_name_less(a, ab));
  EXPECT_TRUE(dos_name_less(ab, b));
  EXPECT_FALSE(dos_name_less(a, a));
}

// --- dos_path --------------------------------------------------------

TEST(dos_path_default, is_the_root) {
  const dos_path root;
  EXPECT_TRUE(root.is_root());
  EXPECT_EQ(root.depth(), 0u);
}

TEST(dos_path_push, grows_the_path_and_pop_shrinks_it) {
  dos_path p;
  const auto save = dos_name::parse(raw("SAVE")).value;
  const auto dat = dos_name::parse(raw("1.DAT")).value;

  ASSERT_TRUE(p.push(save));
  ASSERT_TRUE(p.push(dat));
  EXPECT_EQ(p.depth(), 2u);
  EXPECT_EQ(p.leaf(), dat);
  EXPECT_EQ(p.component(0), save);

  EXPECT_TRUE(p.pop());
  EXPECT_EQ(p.depth(), 1u);
  EXPECT_EQ(p.leaf(), save);
}

TEST(dos_path_pop, at_the_root_answers_false_and_changes_nothing) {
  dos_path root;
  EXPECT_FALSE(root.pop());
  EXPECT_TRUE(root.is_root());
}

TEST(dos_path_push, refuses_past_max_depth) {
  dos_path p;
  const auto name = dos_name::parse(raw("A")).value;
  for (std::size_t i = 0; i < dos_path::max_depth; ++i) {
    ASSERT_TRUE(p.push(name));
  }
  EXPECT_FALSE(p.push(name));
  EXPECT_EQ(p.depth(), dos_path::max_depth);
}

TEST(dos_path_parent, of_the_root_is_the_root) {
  const dos_path root;
  EXPECT_EQ(root.parent(), root);
}

TEST(dos_path_equality, ignores_what_pop_leaves_behind) {
  // The whole reason operator== can be a plain default: two paths built
  // to the same depth by different histories of push()/pop() must
  // compare equal, which only holds if pop() clears the slot it
  // vacates rather than leaving a stale dos_name behind it.
  dos_path a = path({"SAVE", "1.DAT"});
  a.pop();
  a.push(dos_name::parse(raw("2.DAT")).value);

  const dos_path b = path({"SAVE", "2.DAT"});
  EXPECT_EQ(a, b);
}

// --- canonicalize() ----------------------------------------------------

TEST(canonicalize_drive, accepts_c_case_insensitively) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "C:\\GAME.EXE"), path({"GAME.EXE"}));
  EXPECT_EQ(canonical(root, "c:\\GAME.EXE"), path({"GAME.EXE"}));
}

TEST(canonicalize_drive, refuses_a_drive_other_than_c) {
  const dos_path root;
  EXPECT_EQ(canonicalize(root, raw("D:\\GAME.EXE")).error,
            vfs_error::invalid_drive);
}

TEST(canonicalize_absolute,
     starts_from_the_root_whatever_the_current_directory) {
  const dos_path cwd = path({"SAVE"});
  EXPECT_EQ(canonical(cwd, "\\GAME.EXE"), path({"GAME.EXE"}));
}

TEST(canonicalize_relative, resolves_against_the_current_directory) {
  const dos_path cwd = path({"SAVE"});
  EXPECT_EQ(canonical(cwd, "1.DAT"), path({"SAVE", "1.DAT"}));
}

TEST(canonicalize_empty, resolves_to_the_current_directory_unchanged) {
  const dos_path cwd = path({"SAVE"});
  EXPECT_EQ(canonical(cwd, ""), cwd);
}

TEST(canonicalize_case,
     folds_a_mixed_case_path_to_the_canonical_upper_case_form) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "Game\\Save1.Dat"), path({"GAME", "SAVE1.DAT"}));
}

TEST(canonicalize_separators, collapses_repeated_and_trailing_backslashes) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "\\GAME\\\\SAVE\\"), path({"GAME", "SAVE"}));
}

TEST(canonicalize_dot, is_a_no_op_component) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "\\GAME\\.\\SAVE"), path({"GAME", "SAVE"}));
}

TEST(canonicalize_dot_dot, removes_one_component) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "\\GAME\\SAVE\\..\\OTHER"),
            path({"GAME", "OTHER"}));
}

TEST(canonicalize_dot_dot, at_the_root_stays_at_the_root) {
  const dos_path root;
  EXPECT_EQ(canonical(root, "\\..\\..\\GAME"), path({"GAME"}));
}

TEST(canonicalize_dot_dot, resolved_relative_to_the_current_directory_too) {
  const dos_path cwd = path({"GAME", "SAVE"});
  EXPECT_EQ(canonical(cwd, "..\\OTHER"), path({"GAME", "OTHER"}));
}

TEST(canonicalize_invalid_component, reports_path_not_found) {
  const dos_path root;
  EXPECT_EQ(canonicalize(root, raw("\\GAME\\TOOLONGNAME.TXT")).error,
            vfs_error::path_not_found);
  EXPECT_EQ(canonicalize(root, raw("\\BAD*NAME.TXT")).error,
            vfs_error::path_not_found);
}

TEST(canonicalize_depth, refuses_a_path_deeper_than_max_depth) {
  const dos_path root;
  std::string deep;
  for (std::size_t i = 0; i <= dos_path::max_depth; ++i) {
    deep += "\\A";
  }
  EXPECT_EQ(canonicalize(root, raw(deep)).error, vfs_error::path_not_found);
}

// --- vfs_error / vfs_result ---------------------------------------------

TEST(dos_error_code, matches_the_documented_dos_extended_error_codes) {
  EXPECT_EQ(dos_error_code(vfs_error::file_not_found), 0x02);
  EXPECT_EQ(dos_error_code(vfs_error::path_not_found), 0x03);
  EXPECT_EQ(dos_error_code(vfs_error::too_many_open_files), 0x04);
  EXPECT_EQ(dos_error_code(vfs_error::access_denied), 0x05);
  EXPECT_EQ(dos_error_code(vfs_error::invalid_handle), 0x06);
  EXPECT_EQ(dos_error_code(vfs_error::invalid_drive), 0x0F);
  EXPECT_EQ(dos_error_code(vfs_error::no_more_files), 0x12);
  EXPECT_EQ(dos_error_code(vfs_error::directory_full), 0x52);
}

TEST(dos_error_code, answers_zero_for_no_error) {
  EXPECT_EQ(dos_error_code(vfs_error::none), 0u);
}

TEST(vfs_result_ok, is_true_only_for_none) {
  const vfs_result<int> success{.value = 5, .error = vfs_error::none};
  const vfs_result<int> failure{.value = 0, .error = vfs_error::file_not_found};

  EXPECT_TRUE(success.ok());
  EXPECT_FALSE(failure.ok());
}

// --- The whole tree, and one file whole (M5-D2, #170) -------------------
//
// Two free functions over `filesystem`, written once above every backend
// for the reason `canonicalize()` is written once above every backend.
// Driven here against the in-memory one; the SDL host runs the identical
// code over a real directory.

/// `\` for the root, `\A\B.DAT` for anything else — what
/// `format_dos_path` gives, spelled here so this file does not have to
/// pull in report.h to say what a path looks like.
[[nodiscard]] std::string spell(const dos_path& path) {
  if (path.is_root()) {
    return "\\";
  }
  std::string out;
  for (std::size_t i = 0; i < path.depth(); ++i) {
    out += '\\';
    const std::span<const char> text = path.component(i).text();
    out.append(text.data(), text.size());
  }
  return out;
}

[[nodiscard]] std::vector<std::string> walk(const filesystem& fs) {
  std::vector<tree_file> found(tree_file_count(fs));
  const std::size_t count = tree_files(fs, found);
  EXPECT_EQ(count, found.size());
  std::vector<std::string> out;
  out.reserve(found.size());
  for (const tree_file& entry : found) {
    out.push_back(spell(entry.path) + ":" + std::to_string(entry.size));
  }
  return out;
}

/// Make `path`, parents and all, holding `bytes`.
void stage(filesystem& fs, std::string_view path,
           std::span<const std::uint8_t> bytes) {
  const dos_path where = canonicalize(dos_path{}, raw(path)).value;
  dos_path so_far;
  for (std::size_t i = 0; i + 1 < where.depth(); ++i) {
    ASSERT_TRUE(so_far.push(where.component(i)));
    if (!fs.exists(so_far)) {
      ASSERT_EQ(fs.mkdir(so_far), vfs_error::none) << path;
    }
  }
  const vfs_result<file_handle> made = fs.create(where);
  ASSERT_TRUE(made.ok()) << path;
  if (!bytes.empty()) {
    ASSERT_TRUE(fs.write(made.value, bytes).ok()) << path;
  }
  ASSERT_EQ(fs.close(made.value), vfs_error::none) << path;
}

TEST(tree_walk, is_every_file_at_its_own_path_and_no_directories) {
  // On the heap, every time: a `memory_filesystem` carries its own
  // multi-megabyte arena and is not a thing to put on a stack
  // (memory_vfs.h says so).
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  const std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
  stage(fs, "START.EXE", abc);
  stage(fs, R"(SAVE\SAVE1.DAT)", abc);
  stage(fs, R"(SAVE\SAVE2.DAT)", std::span<const std::uint8_t>{});
  stage(fs, R"(A\B\C\D.DAT)", abc);

  // Depth-first, each level in the pinned name order `entry_at()`
  // promises — so the same walk on every host and in every run
  // (PLAN.md §4). `A` before `SAVE` before `START.EXE`, and the walk
  // descends before it moves on.
  EXPECT_EQ(walk(fs), (std::vector<std::string>{
                          "\\A\\B\\C\\D.DAT:3",
                          "\\SAVE\\SAVE1.DAT:3",
                          "\\SAVE\\SAVE2.DAT:0",
                          "\\START.EXE:3",
                      }));
}

TEST(tree_walk, does_not_see_a_directory_with_nothing_in_it) {
  // The honest consequence of the set being the useful one (vfs.h): what
  // the walk lists is exactly what can be read and unlinked, and an
  // empty directory is neither.
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  ASSERT_EQ(fs.mkdir(canonicalize(dos_path{}, raw("EMPTY")).value),
            vfs_error::none);
  EXPECT_EQ(tree_file_count(fs), 0u);
  EXPECT_TRUE(walk(fs).empty());
}

TEST(tree_walk, is_empty_for_an_empty_filesystem) {
  const auto held = std::make_unique<memory_filesystem>();
  const memory_filesystem& fs = *held;
  EXPECT_EQ(tree_file_count(fs), 0u);
  EXPECT_TRUE(walk(fs).empty());
}

TEST(tree_walk, counts_what_a_short_buffer_could_not_hold) {
  // The total, not the number written: a caller handed a listing that
  // did not fit has to be able to tell that it did not, and a count that
  // stopped at the buffer would be a truncated listing presented as a
  // complete one — the reading #158 spent a milestone not having.
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  const std::array<std::uint8_t, 1> one{7};
  stage(fs, "A.DAT", one);
  stage(fs, "B.DAT", one);
  stage(fs, "C.DAT", one);

  std::array<tree_file, 2> room{};
  EXPECT_EQ(tree_files(fs, room), 3u);
  EXPECT_EQ(spell(room[0].path), "\\A.DAT");
  EXPECT_EQ(spell(room[1].path), "\\B.DAT");
}

TEST(read_file, brings_back_every_byte_and_closes_the_handle) {
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  const std::array<std::uint8_t, 5> data{1, 2, 3, 4, 5};
  stage(fs, "DATA.BIN", data);

  std::array<std::uint8_t, 5> out{};
  const dos_path where = canonicalize(dos_path{}, raw("DATA.BIN")).value;
  const vfs_result<std::uint32_t> read = read_file(fs, where, out);
  ASSERT_TRUE(read.ok());
  EXPECT_EQ(read.value, 5u);
  EXPECT_EQ(out, data);

  // The handle is closed on every path out, so the file can be read
  // again — and, since `unlink()` refuses a file with a handle open on
  // it, removed.
  EXPECT_TRUE(read_file(fs, where, out).ok());
  EXPECT_EQ(fs.unlink(where), vfs_error::none);
}

TEST(read_file, answers_a_short_count_for_a_buffer_that_will_not_hold_it) {
  // The rule this layer owns is how to read a file whole; how big a
  // buffer the caller was given is the caller's (vfs.h). It knows the
  // size — `stat()` answers it — so a short count is information rather
  // than a surprise.
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  const std::array<std::uint8_t, 5> data{1, 2, 3, 4, 5};
  stage(fs, "DATA.BIN", data);

  std::array<std::uint8_t, 2> out{};
  const dos_path where = canonicalize(dos_path{}, raw("DATA.BIN")).value;
  const vfs_result<std::uint32_t> read = read_file(fs, where, out);
  ASSERT_TRUE(read.ok());
  EXPECT_EQ(read.value, 2u);
  EXPECT_EQ(out, (std::array<std::uint8_t, 2>{1, 2}));
  EXPECT_EQ(fs.stat(where).value.size, 5u) << "and the caller could have known";
}

TEST(read_file, passes_the_filesystem_s_own_refusal_through) {
  const auto held = std::make_unique<memory_filesystem>();
  memory_filesystem& fs = *held;
  std::array<std::uint8_t, 4> out{};
  EXPECT_EQ(
      read_file(fs, canonicalize(dos_path{}, raw("GONE.DAT")).value, out).error,
      vfs_error::file_not_found);

  ASSERT_EQ(fs.mkdir(canonicalize(dos_path{}, raw("SAVE")).value),
            vfs_error::none);
  EXPECT_EQ(
      read_file(fs, canonicalize(dos_path{}, raw("SAVE")).value, out).error,
      vfs_error::access_denied);
}

}  // namespace
}  // namespace amberfolio::machine
