// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop engine's region filter (tsv_words.h, M5-E3a #212).
//
// An entry whose stream this build does not decode reaches Tesseract as a
// whole page, and the entry is a rectangle of it — so what makes the
// answer the entry's rather than the page's is this function. It is the
// one part of `tesseract_ocr.cpp` that can be checked with no engine
// installed, which is why it is its own file and why this exists.
//
// Every `tsv` table below is written here, in the format Tesseract
// documents. Nothing in this file came out of an engine, and nothing in
// it is anybody's journal.

#include "tsv_words.h"

#include <string>

#include "amberfolio/host/journal_extract.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

/// The header Tesseract writes first, which this has to skip.
constexpr const char* kHeader =
    "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop"
    "\twidth\theight\tconf\ttext\n";

/// One word line: level 5, and the fields the filter reads.
///
/// `block` and `paragraph` are where the word sat in the engine's own
/// layout tree, which is what says where a paragraph ended (#331). Both
/// **restart inside their parent**, the way Tesseract numbers them: the
/// first paragraph of every block is `par_num` 1, and the first line of
/// every paragraph is `line_num` 1.
[[nodiscard]] std::string WordIn(int block, int paragraph, int line, int left,
                                 int top, int width, int height,
                                 const char* text) {
  return "5\t1\t" + std::to_string(block) + "\t" + std::to_string(paragraph) +
         "\t" + std::to_string(line) + "\t1\t" + std::to_string(left) + "\t" +
         std::to_string(top) + "\t" + std::to_string(width) + "\t" +
         std::to_string(height) + "\t96\t" + text + "\n";
}

/// The same, in the one block and the one paragraph most of these tables
/// have — the shape every check that is not about paragraphs wants.
[[nodiscard]] std::string Word(int line, int left, int top, int width,
                               int height, const char* text) {
  return WordIn(1, 1, line, left, top, width, height, text);
}

constexpr host::journal_region kBox{
    .left = 100, .top = 100, .width = 200, .height = 100};

TEST(TsvWords, KeepsWhatIsInsideAndDropsWhatIsNot) {
  const std::string table =
      std::string(kHeader) + Word(1, 120, 120, 40, 10, "inside") +
      Word(1, 10, 10, 40, 10, "above") + Word(1, 500, 120, 40, 10, "right") +
      Word(1, 120, 500, 40, 10, "below");
  EXPECT_EQ(tsv_words_within(table, kBox), "inside");
}

TEST(TsvWords, JoinsOneLineWithSpacesAndTwoWithANewline) {
  const std::string table =
      std::string(kHeader) + Word(1, 110, 110, 30, 10, "one") +
      Word(1, 150, 110, 30, 10, "two") + Word(2, 110, 130, 30, 10, "three");
  EXPECT_EQ(tsv_words_within(table, kBox), "one two\nthree");
}

TEST(TsvWords, AParagraphOpensOnAnIndentAfterALineThatEnded) {
  // The rule that puts the blank lines in (#345). The reader honours a
  // blank line and nothing else (#316), so where these fall is the whole
  // of how an entry reads.
  //
  // A heading at the margin, then a line indented past it: that is a
  // paragraph opening, and the heading is short enough to have ended.
  const std::string table =
      std::string(kHeader) + Word(1, 110, 110, 30, 10, "heading") +
      Word(2, 130, 130, 150, 10, "indented") +
      Word(3, 110, 150, 150, 10, "and") + Word(4, 110, 170, 150, 10, "on");
  EXPECT_EQ(tsv_words_within(table, kBox), "heading\n\nindented\nand\non");
}

TEST(TsvWords, AnIndentAfterAFullLineIsNotAParagraph) {
  // What the engine's own paragraphs got wrong on the real edition, and
  // the reason this file stopped trusting them (tsv_words.h). A word the
  // engine failed to read at the start of a line leaves the line looking
  // indented; the line before it ran the width of the column and ended
  // mid-sentence, so nothing opened.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 150, 10, "they must be") +
                            WordIn(1, 2, 1, 140, 130, 120, 10, "of monsters");
  EXPECT_EQ(tsv_words_within(table, kBox), "they must be\nof monsters");
}

TEST(TsvWords, ALineThatEndedInAStopCanOpenTheNextParagraph) {
  // The other half of "the line before it ended": a full-width line that
  // finishes a sentence is a paragraph's last line however long it is.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 150, 10, "use to me.") +
                            WordIn(1, 2, 1, 140, 130, 120, 10, "Bring any");
  EXPECT_EQ(tsv_words_within(table, kBox), "use to me.\n\nBring any");
}

TEST(TsvWords, ANewBlockStartsAParagraphWithoutNeedingAnIndent) {
  // A block is a different region of the page - a heading over a column,
  // a column beside another - so it is the other way a line can *start* a
  // paragraph. The line before it still has to have ended, and this one
  // has, by being far short of the column. Both of these are `par_num` 1
  // and `line_num` 1 of their own block.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 30, 10, "heading") +
                            WordIn(2, 1, 1, 110, 140, 150, 10, "body");
  EXPECT_EQ(tsv_words_within(table, kBox), "heading\n\nbody");
}

TEST(TsvWords, ANewBlockAfterALineThatDidNotEndIsNotAParagraph) {
  // The half of the rule the engine's own blocks do not carry: on a real
  // scan it opens a block mid-sentence where it lost a word, and a break
  // there is a paragraph the printed page does not have (#345).
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 150, 10, "for the") +
                            WordIn(2, 1, 1, 110, 130, 150, 10, "band of");
  EXPECT_EQ(tsv_words_within(table, kBox), "for the\nband of");
}

TEST(TsvWords, TwoOneLineParagraphsDoNotRunTogether) {
  // Every count restarts inside its parent, so both of these lines are
  // `line_num` 1. The filter compared that number alone until #331 and
  // joined them with a space - two lines read as one.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 30, 10, "first") +
                            WordIn(1, 2, 1, 110, 130, 30, 10, "second");
  EXPECT_EQ(tsv_words_within(table, kBox), "first\nsecond");
}

TEST(TsvWords, ABreakFallsBetweenKeptWordsAndNeverAtTheCrop) {
  // The two ways to get this wrong, both of which produce a *wrong* break
  // rather than a missing one (#331).
  //
  // A rectangle clips a block: the words that fall outside are dropped,
  // and what is kept ends without a break, because a break is only ever
  // emitted between two lines that were both kept. So the fragment this
  // is half of joins to the next one with the single newline its caller
  // writes, and the paragraph is not cut in two.
  const std::string clipped = std::string(kHeader) +
                              WordIn(1, 1, 1, 110, 110, 30, 10, "kept.") +
                              WordIn(2, 1, 1, 110, 140, 30, 10, "opens") +
                              WordIn(2, 1, 2, 110, 500, 30, 10, "past") +
                              WordIn(3, 1, 1, 110, 520, 30, 10, "elsewhere");
  EXPECT_EQ(tsv_words_within(clipped, kBox), "kept.\n\nopens");

  // And a table whose first blocks kept nothing does not open on a break
  // either.
  const std::string late = std::string(kHeader) +
                           WordIn(1, 1, 1, 10, 10, 30, 10, "above") +
                           WordIn(2, 1, 1, 110, 110, 30, 10, "inside");
  EXPECT_EQ(tsv_words_within(late, kBox), "inside");
}

TEST(TsvWords, TheIndentIsMeasuredFromTheInkAndNotTheRectangle) {
  // The margin is the leftmost line that was *kept*, not the rectangle's
  // own left edge: a rectangle is measured to the column and the ink
  // inside it starts where it starts (journal_facts.h). Every line here
  // sits well inside the box, and the second is still not indented
  // relative to the first.
  const std::string table = std::string(kHeader) +
                            Word(1, 150, 110, 30, 10, "one.") +
                            Word(2, 150, 130, 30, 10, "two");
  EXPECT_EQ(tsv_words_within(table, kBox), "one.\ntwo");
}

TEST(TsvWords, AWordBelongsToWhicheverSideItsCentreIsOn) {
  // The rule the header argues for: overlap would pull in a neighbouring
  // column wherever a scan is tight, and containment would drop a word
  // the engine boxed one pixel wide of the rectangle.
  const std::string mostly_in =
      std::string(kHeader) + Word(1, 90, 120, 40, 10, "kept");
  EXPECT_EQ(tsv_words_within(mostly_in, kBox), "kept");

  const std::string mostly_out =
      std::string(kHeader) + Word(1, 70, 120, 40, 10, "dropped");
  EXPECT_EQ(tsv_words_within(mostly_out, kBox), "");
}

TEST(TsvWords, TheEdgesAreHalfOpenTheWayEveryRectHereIs) {
  // Left and top are in, right and bottom are out — the same convention
  // the crop uses, so the two agree on a word sitting exactly on a corner.
  const std::string on_the_left =
      std::string(kHeader) + Word(1, 100, 120, 0, 0, "left");
  EXPECT_EQ(tsv_words_within(on_the_left, kBox), "left");

  const std::string on_the_right =
      std::string(kHeader) + Word(1, 300, 120, 0, 0, "right");
  EXPECT_EQ(tsv_words_within(on_the_right, kBox), "");
}

TEST(TsvWords, OnlyWordsCountAndNotTheLayoutTreeAroundThem) {
  // Levels 1 to 4 are the page, block, paragraph and line that contain a
  // word; their boxes are inside the region too, and counting them would
  // repeat the text four times over.
  std::string table = kHeader;
  for (int level = 1; level <= 4; ++level) {
    table +=
        std::to_string(level) + "\t1\t1\t1\t1\t1\t110\t110\t50\t20\t-1\t\n";
  }
  table += Word(1, 110, 110, 50, 20, "word");
  EXPECT_EQ(tsv_words_within(table, kBox), "word");
}

TEST(TsvWords, ALineItCannotReadIsSkippedRatherThanGuessedAt) {
  // A word it cannot place is not evidence that the word was inside the
  // rectangle (tsv_words.h).
  const std::string table = std::string(kHeader) + "5\t1\t1\t1\t1\t1\tx\ty" +
                            "\t10\t10\t96\tnonsense\n" + "not a tsv line\n" +
                            "5\t1\t1\n" + Word(1, 120, 120, 40, 10, "good");
  EXPECT_EQ(tsv_words_within(table, kBox), "good");
}

TEST(TsvWords, AnEmptyWordIsNotAWord) {
  // Tesseract emits blank text for boxes it found and read nothing in;
  // keeping them would put stray spaces through the middle of a line.
  const std::string table =
      std::string(kHeader) + Word(1, 110, 110, 30, 10, "one") +
      Word(1, 150, 110, 30, 10, "") + Word(1, 190, 110, 30, 10, "two");
  EXPECT_EQ(tsv_words_within(table, kBox), "one two");
}

TEST(TsvWords, CarriageReturnsAreNotPartOfAWord) {
  // The engine's output goes through a file, and on Windows that file
  // comes back with CRLF. A word with a stray return on it would reach
  // the store and diff against a corrected copy for no reason at all.
  const std::string table =
      "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop"
      "\twidth\theight\tconf\ttext\r\n5\t1\t1\t1\t1\t1\t120\t120\t40\t10"
      "\t96\tword\r\n";
  EXPECT_EQ(tsv_words_within(table, kBox), "word");
}

TEST(TsvWords, NothingInsideIsAnEmptyAnswerAndNotAWholePage) {
  const std::string table =
      std::string(kHeader) + Word(1, 10, 10, 40, 10, "elsewhere");
  EXPECT_EQ(tsv_words_within(table, kBox), "");
}

TEST(TsvWords, AnEmptyTableIsAnEmptyAnswer) {
  EXPECT_EQ(tsv_words_within("", kBox), "");
  EXPECT_EQ(tsv_words_within(kHeader, kBox), "");
}

}  // namespace
}  // namespace amberfolio::sdl
