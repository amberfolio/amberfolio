// SPDX-License-Identifier: AGPL-3.0-only
//
// Tesseract's `tsv` output, filtered to a rectangle (M5-E3a, #212).
//
// The desktop host's half of one rule written once in
// `host/journal_ocr.h`: an entry whose stream this build does not decode
// reaches the engine as a **whole page** plus the rectangle that is the
// entry, and what gets filtered is the engine's output rather than the
// image. Tesseract's CLI has no crop flag and this host is not about to
// grow an image library to make one — but it has `tsv`, which is one line
// per word with where the word was, and that is the same answer.
//
// Its own file rather than a lambda inside `tesseract_ocr.cpp` for one
// reason: it is the only part of this host's engine that can be checked
// without Tesseract installed, so it is the part that gets a test
// (`tests/tsv_words_test.cpp`). The same split `audio_gain` and `keymap`
// have, for the same reason.
//
//
// The format, as far as this reads it
// ----------------------------------
//
// Documented and fixed: a header line, then one line per item with
//
//     level page block par line word left top width height conf text
//
// twelve tab-separated fields, the text last and possibly empty. Level 5
// is a word; everything above it is the layout tree that contains one, so
// this keeps level 5 and ignores the rest. Anything it cannot parse it
// skips, because a line this does not understand is a line about a word
// it cannot place, and a word it cannot place is not evidence that the
// word was inside the rectangle.
//
// The eleventh field, `conf`, is the engine's confidence in that word on
// a 0-100 scale — a decimal, and `-1` on the layout lines this skips
// anyway. It was being parsed past and thrown away until #315; it is now
// summarized into a `journal_reading_quality`, which is what lets a host
// tell a player which of their entries to look at.
//
//
// Where the line breaks come from, and where the blank lines do (#331, #345)
// --------------------------------------------------------------------
//
// The reader's reflow honours exactly one break: a **blank line**, which
// it draws as a paragraph (`core/src/machine/seam_journal.cpp`, #316). A
// single newline it reads as a space, because an OCR engine emits one per
// *printed* line and the journal is set in a sixty-character column that
// has nothing to do with the twenty-two or thirty-eight the reader draws.
//
// So this file emits
//
//   * a space, between two words of one line;
//   * one newline, between two printed lines;
//   * a blank line, where a paragraph opened.
//
// Every one of the four counts **restarts inside its parent**, so what
// identifies a printed line is the whole tuple and not `line_num` alone.
// Comparing `line_num` by itself - which is what this did until #331 -
// ran two one-line paragraphs together, since both are `line_num` 1.
//
//
// Where a paragraph opens, which is not where the engine says (#345)
// -------------------------------------------------------------------
//
// This used to take `par_num` at face value: a change of paragraph in the
// engine's tree was a blank line in the reading. On the shipped edition
// that is wrong about a third of the time, and wrong in a way a player
// sees immediately - a sentence cut in half with a blank line in the
// middle of it.
//
// The cause is worth knowing, because it is not so much a bad heuristic
// as a compounding one. Tesseract opens a paragraph at a line it reads as
// indented. On a 1328x1003 scan of a two-page spread the engine
// occasionally fails on the *first word of a line* - and that line then
// begins where its second word does, which looks exactly like an indent.
// So the reading loses a word and gains a paragraph break at the same
// place, and the break is the half a player notices.
//
// The rule here asks for two things instead, and neither alone is enough:
//
//   * the line **before** it has to have ended - with a stop, or well
//     short of the column;
//   * the line itself has to **start** something - a new block, which is
//     a different region of the page, or an indent past the column's own
//     margin.
//
// A line the engine mangled fails the first test, because the line above
// it is a full line of prose that ends mid-sentence. A real paragraph
// passes both. Measured over the whole shipped edition - ninety-nine
// items, read in a browser - against the engine's own paragraphs: breaks
// that follow a finished sentence went from 157 to 183, and breaks that
// fall mid-sentence, which are nearly all wrong, from 110 to 66.
//
// The margin and the far edge are measured off the lines that were
// **kept**, not off the rectangle: a rectangle is measured to the column
// (`host/journal_facts.h`) and the ink inside it starts where it starts.
// The indent threshold is a fraction of the type's own height rather than
// a number of pixels, because the same edition read at one and at two
// arrives here with every measurement doubled.
//
// The break is emitted **between** two lines that were kept and never
// before the first or after the last, which is what keeps two hazards
// out. A rectangle that clips a block in half gains no break at the crop,
// because nothing was kept after it to break against; and the join
// between two *fragments* of one entry stays the single newline its
// callers write, because it happens outside this function altogether. An
// entry is a list of rectangles precisely because entries flow out of a
// column onto the facing page (`host/journal_facts.h`), and a break there
// would be a paragraph the printed page does not have.
//
// `hosts/web/page/journal.mjs` carries the identical rule, because a
// browser's engine answers the same boxes and a player should not get a
// different transcription for choosing a different host.
//
//
// Two callers, one of which has no rectangle (#315)
// ------------------------------------------------
//
// This started as the filter for a `/DCTDecode` page, which is the only
// shape that *needs* one. Since #315 the decoded path asks Tesseract for
// `tsv` as well — not because it needs filtering, it is already cropped,
// but because plain text carries no confidences and the whole of what
// this file does beyond the rectangle is worth having on both paths. So
// `tsv_read` takes a region or a null, and a null keeps every word.

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"

namespace amberfolio::sdl {

/// What one `tsv` table said: the words, and what the engine thought of
/// them.
struct tsv_reading {
  std::string text;
  host::journal_reading_quality quality;
};

/// Read `table`, keeping the words whose centre falls inside `region` —
/// or every word, when `region` is null.
///
/// The quality is over the words that were **kept**, never over the whole
/// page: on an encoded scan most of what the engine read is a different
/// entry, and a confidence averaged over those would be a number about
/// somebody else's page.
[[nodiscard]] tsv_reading tsv_read(std::string_view table,
                                   const host::journal_region* region);

/// The words of `table` whose **centre** falls inside `region`, joined
/// into lines and paragraphs by the engine's own layout numbering (see
/// above for which separator each boundary gets).
///
/// Centre, and not any overlap: a box that straddles the boundary belongs
/// to whichever side most of it is on, which is the rule that gives the
/// same answer a crop of the image would have for every word a crop would
/// not have cut in half. Overlap would pull in a neighbouring column's
/// words wherever the scan is tight, and containment would drop a word
/// whose box the engine drew one pixel wide of the rectangle.
///
/// `tsv_read`'s text, for a caller that wants only that — which is every
/// test of the rule above.
[[nodiscard]] std::string tsv_words_within(std::string_view table,
                                           const host::journal_region& region);

}  // namespace amberfolio::sdl
