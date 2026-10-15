// SPDX-License-Identifier: AGPL-3.0-only
//
// The selection block's margin bookkeeping (selection_margin.h, #483): which
// cells are blocks, what margin the cells around them want, and which margin
// pixels are this machine's to take back. No machine and no screen: the seam
// that paints is seam_select_yellow_test.cpp's.

#include "amberfolio/machine/selection_margin.h"

#include <cstdint>
#include <memory>
#include <utility>

#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::uint8_t yellow = 0x0E;
constexpr std::uint8_t white = 0x0F;
constexpr unsigned page = 0;

/// A block's word on row 5: a white key at column 10, yellow to 12.
void a_word(selection_margins& m) {
  m.drawn(page, 5, 10, white);
  m.drawn(page, 5, 11, yellow);
  m.drawn(page, 5, 12, yellow);
}

[[nodiscard]] std::unique_ptr<selection_margins> margins() {
  return std::make_unique<selection_margins>();
}

TEST(SelectionMargin, AWordWantsItsTopAboveEveryCellInThatCellsColour) {
  const auto m = margins();
  a_word(*m);
  const selection_margins::strip key = m->wanted(page, 4, 10);
  EXPECT_EQ(key.bits[7], 0xFF);
  EXPECT_EQ(key.colour[7], white);
  for (unsigned line = 0; line < 7; ++line) {
    EXPECT_EQ(key.bits[line], 0) << "only the bottom line";
  }
  EXPECT_EQ(m->wanted(page, 4, 11).colour[7], yellow);
  EXPECT_EQ(m->wanted(page, 4, 12).bits[7], 0xFF);
}

TEST(SelectionMargin, AWordWantsItsRightInColumnZeroOfTheCellAfter) {
  const auto m = margins();
  a_word(*m);
  const selection_margins::strip after = m->wanted(page, 5, 13);
  for (unsigned line = 0; line < 8; ++line) {
    EXPECT_EQ(after.bits[line], 0x80) << line;
    EXPECT_EQ(after.colour[line], yellow) << line;
  }
  const selection_margins::strip corner = m->wanted(page, 4, 13);
  EXPECT_EQ(corner.bits[7], 0x80) << "the corner above the right margin";
  EXPECT_EQ(corner.colour[7], yellow);
}

TEST(SelectionMargin, NothingOnTheLeftOrBelowAndNothingInABlock) {
  // Every face keeps column 0 and row 7 of a cell paper, so the block's left
  // and bottom margin are its own cells'.
  const auto m = margins();
  a_word(*m);
  for (const auto& [row, column] :
       {std::pair{5U, 9U}, std::pair{6U, 10U}, std::pair{6U, 13U},
        std::pair{5U, 10U}, std::pair{5U, 11U}}) {
    const selection_margins::strip s = m->wanted(page, row, column);
    for (unsigned line = 0; line < 8; ++line) {
      EXPECT_EQ(s.bits[line], 0) << row << "," << column << " line " << line;
    }
  }
}

TEST(SelectionMargin, WhatIsPaintedIsOwnedAndTakenBackWhenTheBlockGoes) {
  const auto m = margins();
  a_word(*m);
  selection_margins::change c = m->needed(page, 4, 11);
  EXPECT_EQ(c.paint.bits[7], 0xFF);
  EXPECT_EQ(c.erase.bits[7], 0);
  m->done(page, 4, 11, c.erase, c.paint);
  EXPECT_TRUE(m->needed(page, 4, 11).empty());

  for (unsigned column = 10; column <= 12; ++column) {
    m->drawn(page, 5, column, 0);  // the word is drawn plain
  }
  c = m->needed(page, 4, 11);
  EXPECT_EQ(c.erase.bits[7], 0xFF);
  EXPECT_EQ(c.erase.colour[7], yellow);
  EXPECT_EQ(c.paint.bits[7], 0);
}

TEST(SelectionMargin, OnlyWhatWasPaintedIsTakenBack) {
  // The seam paints only over black: a pixel the program had drawn there is
  // not this machine's, and is never put back to black.
  const auto m = margins();
  a_word(*m);
  selection_margins::change c = m->needed(page, 4, 11);
  selection_margins::strip painted = c.paint;
  painted.bits[7] = 0xF7;  // one pixel was a descender
  m->done(page, 4, 11, c.erase, painted);
  EXPECT_EQ(m->owned(page, 4, 11).bits[7], 0xF7);

  m->drawn(page, 5, 11, 0);
  EXPECT_EQ(m->needed(page, 4, 11).erase.bits[7], 0xF7);
}

TEST(SelectionMargin, ACellTheProgramDrawsIsAllItsOwnAgain) {
  const auto m = margins();
  a_word(*m);
  selection_margins::change c = m->needed(page, 4, 11);
  m->done(page, 4, 11, c.erase, c.paint);
  m->drawn(page, 4, 11, 0);  // the program redraws the cell above
  EXPECT_EQ(m->owned(page, 4, 11).bits[7], 0);
  EXPECT_EQ(m->needed(page, 4, 11).paint.bits[7], 0xFF)
      << "and the word below still wants its top there";
}

TEST(SelectionMargin, AMarginInTheWrongColourIsTakenBackAndPaintedAgain) {
  const auto m = margins();
  a_word(*m);
  selection_margins::change c = m->needed(page, 4, 11);
  m->done(page, 4, 11, c.erase, c.paint);
  m->drawn(page, 5, 11, white);
  c = m->needed(page, 4, 11);
  EXPECT_EQ(c.erase.bits[7], 0xFF);
  EXPECT_EQ(c.erase.colour[7], yellow);
  EXPECT_EQ(c.paint.bits[7], 0xFF);
  EXPECT_EQ(c.paint.colour[7], white);
}

TEST(SelectionMargin, AClearedRectangleHasNoBlocksAndOwnsNothing) {
  const auto m = margins();
  a_word(*m);
  EXPECT_TRUE(m->any(page));
  selection_margins::change c = m->needed(page, 5, 13);
  m->done(page, 5, 13, c.erase, c.paint);
  m->cleared(page, 5, 10, 5, 12);
  EXPECT_EQ(m->block(page, 5, 11), 0);
  c = m->needed(page, 5, 13);
  EXPECT_EQ(c.erase.bits[0], 0x80) << "the right margin outside it goes";
  m->done(page, 5, 13, c.erase, c.paint);
  EXPECT_FALSE(m->any(page));
}

TEST(SelectionMargin, TheEdgesOfTheScreenAreNotCells) {
  const auto m = margins();
  m->drawn(page, 0, 39, yellow);
  EXPECT_TRUE(m->needed(page, 0, 40).empty());
  EXPECT_TRUE(m->needed(page, 25, 0).empty());
  m->drawn(selection_margins::pages, 3, 3, yellow);
  EXPECT_FALSE(m->any(selection_margins::pages));
  EXPECT_EQ(m->wanted(page, 0, 0).bits[7], 0);
}

TEST(SelectionMargin, ThePagesAreApart) {
  const auto m = margins();
  a_word(*m);
  EXPECT_FALSE(m->any(1));
  EXPECT_EQ(m->wanted(1, 4, 10).bits[7], 0);
}

}  // namespace
}  // namespace amberfolio::machine
