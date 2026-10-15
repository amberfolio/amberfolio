// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card's table (#427).
//
// What a test can hold a table of words to is its shape, and that is what
// is here: every row says something, every seam a row names exists, a
// context is not named twice, the words fit the grid the desktop host
// sets them in, and the JSON a page reads is the table and not a second
// copy that can drift. Whether a row is *true* is the other half, and no
// test in this repository runs the game: docs/hosts.md says which rows
// were driven and which rest on two readings of the program.

#include "amberfolio/host/key_card.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

[[nodiscard]] bool is_a_seam(std::string_view id) {
  const auto seams = machine::all_seams();
  return std::ranges::any_of(seams, [&](const machine::seam_definition& seam) {
    return seam.id == id;
  });
}

[[nodiscard]] std::size_t occurrences(std::string_view text,
                                      std::string_view word) {
  std::size_t count = 0;
  for (std::size_t at = text.find(word); at != std::string_view::npos;
       at = text.find(word, at + word.size())) {
    ++count;
  }
  return count;
}

[[nodiscard]] std::size_t row_count() {
  std::size_t rows = 0;
  for (const key_context& context : key_card()) {
    rows += context.rows.size();
  }
  return rows;
}

TEST(KeyCard, EveryContextHasRowsAndEveryRowSaysSomething) {
  ASSERT_FALSE(key_card().empty());
  for (const key_context& context : key_card()) {
    SCOPED_TRACE(std::string(context.id));
    EXPECT_FALSE(context.title.empty());
    EXPECT_FALSE(context.rows.empty());
    for (const key_row& row : context.rows) {
      SCOPED_TRACE(std::string(row.keys));
      EXPECT_FALSE(row.keys.empty());
      EXPECT_FALSE(row.does.empty());
    }
  }
  EXPECT_FALSE(key_card_legend().empty());
}

TEST(KeyCard, EveryContextIsNamedOnceInLowerCase) {
  std::set<std::string_view> seen;
  for (const key_context& context : key_card()) {
    EXPECT_TRUE(seen.insert(context.id).second) << context.id;
    EXPECT_FALSE(context.id.empty());
    for (const char c : context.id) {
      EXPECT_TRUE((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
          << context.id;
    }
  }
}

TEST(KeyCard, NoKeyIsListedTwiceInOneContext) {
  for (const key_context& context : key_card()) {
    std::set<std::string_view> seen;
    for (const key_row& row : context.rows) {
      EXPECT_TRUE(seen.insert(row.keys).second)
          << context.id << ": " << row.keys;
    }
  }
}

TEST(KeyCard, EverySeamARowNamesExists) {
  // The reason the table is held to `all_seams()` and not to a list of
  // its own: a row for a seam that was renamed, or never merged, would
  // be a row that never shows, and nothing else would say so.
  for (const key_context& context : key_card()) {
    for (const key_row& row : context.rows) {
      if (row.seam.empty()) {
        continue;
      }
      EXPECT_TRUE(is_a_seam(row.seam))
          << context.id << ": " << row.keys << " names " << row.seam;
    }
  }
}

TEST(KeyCard, TheSeamsThatTakeAKeyHaveRows) {
  // The four seams whose keys a player has to be told. A seam that is
  // added later with a key adds its rows; this is the floor, so that
  // deleting one of these is a red test and not a silent gap.
  std::set<std::string_view> named;
  for (const key_context& context : key_card()) {
    for (const key_row& row : context.rows) {
      if (!row.seam.empty()) {
        named.insert(row.seam);
      }
    }
  }
  EXPECT_TRUE(named.contains("automap"));
  EXPECT_TRUE(named.contains("journal"));
  EXPECT_TRUE(named.contains("encamp-fix"));
  EXPECT_TRUE(named.contains("modern-controls"));
}

TEST(KeyCard, TheWordsFitTheGridTheDesktopSetsThemIn) {
  for (const key_context& context : key_card()) {
    for (const key_row& row : context.rows) {
      EXPECT_LE(row.keys.size(), key_card_keys_width) << row.keys;
      EXPECT_LE(row.does.size(), key_card_does_width) << row.does;
    }
    EXPECT_LE(context.title.size(), std::size_t{60}) << context.title;
  }
}

TEST(KeyCard, EveryWordIsPrintableAsciiWithNothingToEscape) {
  const auto plain = [](std::string_view text) {
    return std::ranges::all_of(text, [](char c) {
      return c >= 0x20 && c <= 0x7E && c != '"' && c != '\\';
    });
  };
  EXPECT_TRUE(plain(key_card_legend()));
  for (const key_context& context : key_card()) {
    EXPECT_TRUE(plain(context.id)) << context.id;
    EXPECT_TRUE(plain(context.title)) << context.title;
    for (const key_row& row : context.rows) {
      EXPECT_TRUE(plain(row.keys)) << row.keys;
      EXPECT_TRUE(plain(row.does)) << row.does;
      EXPECT_TRUE(plain(row.seam)) << row.seam;
    }
  }
}

TEST(KeyCard, WithNothingOnTheCardIsTheProgramAsItIs) {
  const auto none = [](std::string_view) { return false; };
  const std::vector<key_page> pages = key_card_for(card_shell::desktop, none);
  ASSERT_FALSE(pages.empty());
  for (const key_page& page : pages) {
    EXPECT_FALSE(page.rows.empty());
    for (const key_row* row : page.rows) {
      EXPECT_TRUE(row->seam.empty()) << row->keys;
    }
  }
  // A context that is nothing but a seam's keys is not shown at all.
  for (const key_page& page : pages) {
    EXPECT_NE(page.context->id, "journal");
  }
}

TEST(KeyCard, ASeamsRowsAppearWhenItIsOnAndOnlyThen) {
  const auto journal = [](std::string_view id) { return id == "journal"; };
  const std::vector<key_page> pages =
      key_card_for(card_shell::desktop, journal);
  const auto found = std::ranges::find_if(pages, [](const key_page& page) {
    return page.context->id == "journal";
  });
  ASSERT_NE(found, pages.end());
  EXPECT_EQ(found->rows.size(), found->context->rows.size());

  // Another seam's row in a shared context stays hidden.
  for (const key_page& page : pages) {
    for (const key_row* row : page.rows) {
      EXPECT_TRUE(row->seam.empty() || row->seam == "journal") << row->keys;
    }
  }
}

TEST(KeyCard, AShellSeesItsOwnContextsAndTheGamesButNotTheOthers) {
  const auto all = [](std::string_view) { return true; };
  const auto has = [](const std::vector<key_page>& pages, std::string_view id) {
    return std::ranges::any_of(
        pages, [&](const key_page& page) { return page.context->id == id; });
  };
  const std::vector<key_page> desktop = key_card_for(card_shell::desktop, all);
  const std::vector<key_page> page = key_card_for(card_shell::page, all);
  EXPECT_TRUE(has(desktop, "desktop"));
  EXPECT_FALSE(has(page, "desktop"));
  EXPECT_TRUE(has(desktop, "explore"));
  EXPECT_TRUE(has(page, "explore"));
}

TEST(KeyCard, TheJsonIsTheTableAndNotACopyOfIt) {
  const std::string_view json = key_card_json();
  ASSERT_TRUE(json.starts_with("{\"legend\":"));
  ASSERT_TRUE(json.ends_with("]}"));
  EXPECT_EQ(occurrences(json, "\"id\":"), key_card().size());
  EXPECT_EQ(occurrences(json, "\"keys\":"), row_count());
  EXPECT_EQ(occurrences(json, "\"does\":"), row_count());
  EXPECT_EQ(occurrences(json, "\"seam\":"), row_count());
  EXPECT_EQ(occurrences(json, "{"), 1 + key_card().size() + row_count());
  EXPECT_EQ(occurrences(json, "}"), occurrences(json, "{"));
  EXPECT_EQ(occurrences(json, "["), 1 + key_card().size());
  EXPECT_EQ(occurrences(json, "]"), occurrences(json, "["));
  // Every word of the table is in it, verbatim.
  for (const key_context& context : key_card()) {
    EXPECT_NE(json.find(context.title), std::string_view::npos);
    for (const key_row& row : context.rows) {
      EXPECT_NE(json.find(row.keys), std::string_view::npos);
      EXPECT_NE(json.find(row.does), std::string_view::npos);
    }
  }
}

}  // namespace
}  // namespace amberfolio::host
