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

TEST(TsvWords, TwoParagraphsAreSeparatedByABlankLine) {
  // The reader honours a blank line and nothing else (#316), and until
  // #331 nothing upstream ever emitted one - so a real entry read as one
  // solid block of prose from the first row to the last.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 30, 10, "one") +
                            WordIn(1, 1, 2, 110, 120, 30, 10, "two") +
                            WordIn(1, 2, 1, 110, 140, 30, 10, "three");
  EXPECT_EQ(tsv_words_within(table, kBox), "one\ntwo\n\nthree");
}

TEST(TsvWords, TwoOneLineParagraphsDoNotRunTogether) {
  // Every count restarts inside its parent, so both of these lines are
  // `line_num` 1. The filter compared that number alone until #331 and
  // joined them with a space - two paragraphs read as one line.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 30, 10, "first") +
                            WordIn(1, 2, 1, 110, 130, 30, 10, "second");
  EXPECT_EQ(tsv_words_within(table, kBox), "first\n\nsecond");
}

TEST(TsvWords, ANewBlockIsANewParagraphToo) {
  // Broader than "a paragraph break inside a block", on purpose: it is
  // what Tesseract's own text output does, so this host's three engines
  // break in the same places (tsv_words.h). Both of these are `par_num`
  // 1 and `line_num` 1 of their own block.
  const std::string table = std::string(kHeader) +
                            WordIn(1, 1, 1, 110, 110, 30, 10, "heading") +
                            WordIn(2, 1, 1, 110, 140, 30, 10, "body");
  EXPECT_EQ(tsv_words_within(table, kBox), "heading\n\nbody");
}

TEST(TsvWords, ABreakFallsBetweenKeptWordsAndNeverAtTheCrop) {
  // The two ways to get this wrong, both of which produce a *wrong* break
  // rather than a missing one (#331).
  //
  // A rectangle clips a paragraph: the words of paragraph 2 that fall
  // outside are dropped, and what is kept ends without a break, because a
  // break is only ever emitted between two words that were both kept. So
  // the fragment this is half of joins to the next one with the single
  // newline its caller writes, and the paragraph is not cut in two.
  const std::string clipped = std::string(kHeader) +
                              WordIn(1, 1, 1, 110, 110, 30, 10, "kept") +
                              WordIn(1, 2, 1, 110, 140, 30, 10, "opens") +
                              WordIn(1, 2, 2, 110, 500, 30, 10, "past") +
                              WordIn(1, 3, 1, 110, 520, 30, 10, "elsewhere");
  EXPECT_EQ(tsv_words_within(clipped, kBox), "kept\n\nopens");

  // And a table whose first paragraphs kept nothing does not open on a
  // break either.
  const std::string late = std::string(kHeader) +
                           WordIn(1, 1, 1, 10, 10, 30, 10, "above") +
                           WordIn(1, 2, 1, 110, 110, 30, 10, "inside");
  EXPECT_EQ(tsv_words_within(late, kBox), "inside");
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
