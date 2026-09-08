// SPDX-License-Identifier: AGPL-3.0-only
//
// The whitespace an engine's reading answers, as a test (journal_ocr.h).
//
// The interface half of an OCR engine that nobody can check by running an
// engine: no real one runs in CI, and both hosts' own engines need a
// program or a library that is not there. What *is* checkable is the join
// — the one piece of an entry's shape that neither engine decides — and
// this is it, because the rule reads as an arbitrary count of newlines
// until you know what the reader does with each of them.
//
// One newline is a space (#316), so it is what two pieces of one flowing
// entry get; a blank line is a paragraph, so it is what a boundary the
// fact table measured as a break gets (#361). Getting the second wrong
// runs the sentence before the break into the sentence after it, on one
// line, which is what #361 was.

#include "amberfolio/host/journal_ocr.h"

#include <string>

#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

TEST(JournalJoin, TheFirstPieceIsNotJoinedToAnything) {
  std::string out;
  journal_join_piece(out, "one", false);
  EXPECT_EQ(out, "one");
}

TEST(JournalJoin, AFirstPieceThatOpensAParagraphStillOpensNothing) {
  // The fact table forbids it (`journal_facts_test.cpp`), but a caller
  // that got it wrong should not be able to open a reading with a blank
  // line: there is nothing before it to break from.
  std::string out;
  journal_join_piece(out, "one", true);
  EXPECT_EQ(out, "one");
}

TEST(JournalJoin, TwoPiecesOfOneFlowingEntryAreOneNewlineApart) {
  // A boundary is a continuation, so the reader reads the join as a
  // space: an entry is in pieces from running out of column, not from
  // the writer stopping (#331).
  std::string out;
  journal_join_piece(out, "the sentence runs", false);
  journal_join_piece(out, "on to here", false);
  EXPECT_EQ(out, "the sentence runs\non to here");
}

TEST(JournalJoin, APieceTheTableCallsAParagraphGetsABlankLine) {
  std::string out;
  journal_join_piece(out, "one thought ends.", false);
  journal_join_piece(out, "Another begins.", true);
  EXPECT_EQ(out, "one thought ends.\n\nAnother begins.");
}

TEST(JournalJoin, AThreePieceEntryCanBeBothAtOnce) {
  // Which is exactly the one entry that made this necessary: its prose
  // resumes under its own drawing as a new paragraph, and then flows
  // into the next column mid-sentence (#357, #361).
  std::string out;
  journal_join_piece(out, "over the drawing.", false);
  journal_join_piece(out, "Under the drawing, and", true);
  journal_join_piece(out, "on into the next column.", false);
  EXPECT_EQ(out,
            "over the drawing.\n\nUnder the drawing, and\n"
            "on into the next column.");
}

}  // namespace
}  // namespace amberfolio::host
