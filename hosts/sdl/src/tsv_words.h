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

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::sdl {

/// The words of `table` whose **centre** falls inside `region`, joined
/// into lines by the engine's own line numbering.
///
/// Centre, and not any overlap: a box that straddles the boundary belongs
/// to whichever side most of it is on, which is the rule that gives the
/// same answer a crop of the image would have for every word a crop would
/// not have cut in half. Overlap would pull in a neighbouring column's
/// words wherever the scan is tight, and containment would drop a word
/// whose box the engine drew one pixel wide of the rectangle.
[[nodiscard]] std::string tsv_words_within(std::string_view table,
                                           const host::journal_region& region);

}  // namespace amberfolio::sdl
