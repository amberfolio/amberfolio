// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card's lines on this host (#427).
//
// The table is `hosts/common`'s and is held to its own shape there; what
// is checked here is what this host does with it: that every line is the
// panel's width, so the two views are one rectangle; that every page is
// the same height, so turning one does not resize it; that a page number
// past the end is brought back; that a seam's rows are in the lines
// only when the card was built with the seam on; and that the card is
// the one for the program the engine is running.

#include "key_card_view.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/key_card.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

[[nodiscard]] std::vector<host::key_page> everything() {
  return host::key_card_for(host::card_shell::desktop, {},
                            [](std::string_view) { return true; });
}

/// Whether any page of `pages` is the context `id`.
[[nodiscard]] bool has_page(const std::vector<host::key_page>& pages,
                            std::string_view id) {
  for (const host::key_page& page : pages) {
    if (page.context->id == id) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool any_line_has(const std::vector<std::string>& lines,
                                std::string_view word) {
  for (const std::string& line : lines) {
    if (line.find(word) != std::string::npos) {
      return true;
    }
  }
  return false;
}

TEST(KeyCardView, EveryLineIsThePanelsWidth) {
  const std::vector<host::key_page> pages = everything();
  for (std::size_t page = 0; page < pages.size(); ++page) {
    std::size_t at = page;
    for (const std::string& line : card_lines(pages, at)) {
      EXPECT_EQ(line.size(), card_columns) << line;
    }
  }
}

TEST(KeyCardView, EveryPageIsTheSameHeight) {
  const std::vector<host::key_page> pages = everything();
  ASSERT_GT(pages.size(), 1U);
  std::size_t first = 0;
  const std::size_t height = card_lines(pages, first).size();
  for (std::size_t page = 1; page < pages.size(); ++page) {
    std::size_t at = page;
    EXPECT_EQ(card_lines(pages, at).size(), height) << page;
  }
}

TEST(KeyCardView, APagePastTheEndIsBroughtBack) {
  const std::vector<host::key_page> pages = everything();
  std::size_t page = 1000;
  const std::vector<std::string> lines = card_lines(pages, page);
  EXPECT_EQ(page, pages.size() - 1);
  EXPECT_TRUE(any_line_has(lines, pages.back().context->title));
}

TEST(KeyCardView, TheKeysAndWhatTheyDoSitInTwoColumns) {
  const std::vector<host::key_page> pages = everything();
  std::size_t page = 0;
  const std::vector<std::string> lines = card_lines(pages, page);
  // Two heading lines, then one line per row, in the table's order.
  const host::key_page& shown = pages.front();
  for (std::size_t i = 0; i < shown.rows.size(); ++i) {
    const std::string& line = lines.at(2 + i);
    EXPECT_EQ(line.substr(0, shown.rows[i]->keys.size()), shown.rows[i]->keys);
    EXPECT_EQ(line.substr(card_does_column, shown.rows[i]->does.size()),
              shown.rows[i]->does);
  }
}

TEST(KeyCardView, ASeamsRowsAreInTheLinesOnlyWhileItIsOn) {
  // A fresh engine has every seam off, and no program is loaded, so
  // none could be turned on: the card is the program as it is.
  machine::seam_engine engine;
  const std::vector<host::key_page> off = card_pages(engine);
  std::size_t at = 0;
  for (std::size_t page = 0; page < off.size(); ++page) {
    at = page;
    EXPECT_FALSE(any_line_has(card_lines(off, at), "Notes log"));
  }

  const std::vector<host::key_page> on = everything();
  bool found = false;
  for (std::size_t page = 0; page < on.size(); ++page) {
    at = page;
    found = found || any_line_has(card_lines(on, at), "Notes log");
  }
  EXPECT_TRUE(found);
}

TEST(KeyCardView, TheCardIsTheRunningProgramsOwn) {
  const auto programs = machine::known_editions();
  ASSERT_EQ(programs.size(), 2U);

  // No program, and a program this build does not recognize: the
  // baseline's card.
  machine::seam_engine engine;
  EXPECT_TRUE(has_page(card_pages(engine), "wilderness"));
  EXPECT_FALSE(has_page(card_pages(engine), "journey"));
  engine.loaded(sha256_digest{}, machine::image_load_segment);
  ASSERT_EQ(engine.known_edition(), nullptr);
  EXPECT_TRUE(has_page(card_pages(engine), "wilderness"));

  sha256_digest digest{};
  ASSERT_TRUE(machine::parse_digest(programs[0].fingerprint, digest));
  engine.loaded(digest, machine::image_load_segment);
  EXPECT_TRUE(has_page(card_pages(engine), "wilderness"));
  EXPECT_FALSE(has_page(card_pages(engine), "journey"));

  // Curse of the Azure Bonds has no wilderness to walk, and a journey.
  ASSERT_TRUE(machine::parse_digest(programs[1].fingerprint, digest));
  engine.loaded(digest, machine::image_load_segment);
  const std::vector<host::key_page> curse = card_pages(engine);
  EXPECT_FALSE(has_page(curse, "wilderness"));
  EXPECT_TRUE(has_page(curse, "journey"));
  std::size_t at = 0;
  for (std::size_t page = 0; page < curse.size(); ++page) {
    at = page;
    EXPECT_FALSE(any_line_has(card_lines(curse, at), "wilderness"));
  }
}

TEST(KeyCardView, AnEmptyCardSaysSo) {
  const std::vector<host::key_page> none;
  std::size_t page = 3;
  const std::vector<std::string> lines = card_lines(none, page);
  EXPECT_TRUE(any_line_has(lines, "no keys"));
}

}  // namespace
}  // namespace amberfolio::sdl
