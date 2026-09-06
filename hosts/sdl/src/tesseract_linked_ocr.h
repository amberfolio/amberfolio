// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's OCR engine, linked (M5-E3c, #216).
//
// `tesseract_ocr.h` is the other one, and the two are alternatives rather
// than a fallback pair: this file exists only when
// `AMBERFOLIO_LINK_TESSERACT` is on, and when it is, this is the engine
// the host uses. `journal_ocr.h`'s one virtual call is what makes that a
// build option rather than a change to anything above it.
//
//
// Why both exist
// --------------
//
// The run-it-as-a-program engine is what a contributor gets: no
// dependency to build, and a player who has Tesseract installed already
// gets a working reader. What it cannot do is work for a player who has
// installed nothing, and that is most players — so the shipped build
// wants the engine inside it. `cmake/AmberfolioTesseract.cmake` is the
// cost of that and the argument for paying it only when asked.
//
// Neither engine is *better*. They read the same pages with the same
// Tesseract; the difference is who has to have installed it.
//
//
// What the linked API buys, beyond not needing an install
// ------------------------------------------------------
//
// **It can crop.** `TessBaseAPI::SetRectangle` restricts recognition to a
// rectangle of the page, which is exactly what a journal entry's fragment
// is (`host/journal_extract.h`). The program-driven engine has no such
// flag — Tesseract's command line cannot crop — so it reads the whole page
// and filters the words out of a `tsv` afterwards (`tsv_words.h`). Both
// arrive at the same answer; this one asks the question directly, and does
// not re-encode a player's scan to do it.
//
// **And it needs no temporary files.** The other engine's promise is a
// directory of scratch files it writes and deletes, because a program has
// to be handed a path. This one is handed bytes in memory and the document
// never touches the disk at all, which is the same promise kept more
// simply.
//
//
// Two page-segmentation modes, and why the second one
// --------------------------------------------------
//
// A journal entry is one uniform block of text, so `PSM_SINGLE_BLOCK` is
// right for it and is what runs first. It is *wrong* for the entries that
// are maps — a caption over a picture — where it reads nothing at all:
// three of the first edition's fifty-eight are like that, and driven
// against them single-block returned three characters where automatic
// page segmentation returned the heading and the caption.
//
// So an almost-empty answer is retried with `PSM_AUTO`. It is a fallback
// and not the default because automatic segmentation on a plain column of
// prose is slower and no better, and because "it read nothing" is a
// cheaper test than guessing in advance which entries are pictures.
//
//
// Why #331 cost this engine nothing
// ---------------------------------
//
// The reader honours a blank line as a paragraph break and reads a single
// newline as a space (`host/journal_ocr.h`), and neither of the other two
// engines was emitting a blank line: both are built out of a per-word
// layout table, and both were joining every line flat. This one is not
// built out of anything — it returns `TessBaseAPI::GetUTF8Text()`, which
// is Tesseract's own text renderer, and that already ends each line with
// a separator and each paragraph with one more. Both separators default
// to `"\n"` (`LTRResultIterator`'s constructor), so a paragraph boundary
// is already a blank line and a line boundary already is not.
//
// **That is read off tesseract 5.5.1's source and not run.** What is
// claimed here is what the pinned version's code does — `GetUTF8Text`
// walking `RIL_PARA`, `AppendUTF8ParagraphText` appending
// `line_separator_` per line and `paragraph_separator_` at each paragraph
// end — and not what a page came out looking like. The other two engines'
// rules are the ones with tests, because they are the ones that can be
// checked with no engine installed.
//
// And what this engine shares with them is the part that is not about
// separators at all: **Tesseract has to find the paragraphs**. All three
// carry the engine's own paragraph decision and none of them invents
// one, so an edition whose paragraphs the detector runs together comes
// out as one block from every one of them. `docs/journal.md` §5 has the
// one reason to think this edition might be such a case, and it is
// unmeasured.
//
// The trailing blank line the renderer leaves is the page's and not the
// entry's, and `trim_trailing` above takes it off — which is also what
// keeps a piece from arriving at the fragment join with a break already
// on the end of it.
//
//
// And why #315's page-segmentation finding does not apply here
// -----------------------------------------------------------
//
// #315 measured what `--psm 6` costs the program-driven engine on an
// encoded scan — 12.1% and 21.1% character error against 2.9% and 4.0%
// under automatic segmentation — and `tesseract_ocr.h` carries the table.
// None of it is a finding about this engine, because the cause is not the
// mode: it is that the other engine hands Tesseract a **two-page spread**
// and calls it one block. `SetRectangle` above means this one hands it
// one column of one entry, and single-block is then exactly true of what
// is in the picture. The measured equivalent of what this engine does —
// the entry cropped, read as one block — was 2.9% and 2.3%.
//
// So this file's page-segmentation choice was already right and stays.
// What it does gain from #315 is the confidence it was already computing
// and throwing away, which is what tells a player which of their
// ninety-nine entries to look at.

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"

namespace tesseract {
class TessBaseAPI;
}

namespace amberfolio::sdl {

/// Tesseract, linked into this binary.
class tesseract_linked_ocr final : public host::journal_ocr {
 public:
  /// `tessdata` is the directory holding `eng.traineddata`.
  explicit tesseract_linked_ocr(std::string tessdata);
  ~tesseract_linked_ocr() override;

  /// Start the engine. False if the model is not where it was told to
  /// look, in which case nothing else here should be called and the host
  /// says so — the same shape `tesseract_ocr::available()` has, and for
  /// the same reason.
  [[nodiscard]] bool available();

  [[nodiscard]] bool recognize(const host::journal_scan& scan,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override { return engine_; }

  [[nodiscard]] host::journal_reading_quality quality() const override {
    return quality_;
  }

 private:
  /// One piece: its bytes into the engine, its rectangle applied, its
  /// text out. Empty and false when the engine read nothing.
  [[nodiscard]] bool read_part(const host::journal_part& part, bool encoded,
                               std::string& out,
                               host::journal_reading_quality& how);

  std::string tessdata_;
  std::string engine_;
  tesseract::TessBaseAPI* api_{nullptr};
  /// What the last `recognize()` was sure of, summed over its pieces
  /// (#315).
  host::journal_reading_quality quality_;
};

}  // namespace amberfolio::sdl
