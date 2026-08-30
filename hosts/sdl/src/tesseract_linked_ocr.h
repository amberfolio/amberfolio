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

 private:
  /// One piece: its bytes into the engine, its rectangle applied, its
  /// text out. Empty and false when the engine read nothing.
  [[nodiscard]] bool read_part(const host::journal_part& part, bool encoded,
                               std::string& out);

  std::string tessdata_;
  std::string engine_;
  tesseract::TessBaseAPI* api_{nullptr};
};

}  // namespace amberfolio::sdl
