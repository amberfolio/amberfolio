// SPDX-License-Identifier: AGPL-3.0-only
//
// The DOS name semantics on their own: `dos_name::parse`, `dos_path`, and
// `canonicalize()` — the part of vfs.h every backend relies on never
// having to redo. Operating a filesystem over these types is
// memory_vfs_test.cpp's job; this file is only about what a name and a
// path canonicalize to.

#include "amberfolio/machine/vfs.h"

#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

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

}  // namespace
}  // namespace amberfolio::machine
