// SPDX-License-Identifier: AGPL-3.0-only
//
// The OCR engine, as the ingester sees it (M5-E3, #174).
//
// One virtual call, because that is the whole of what an engine is asked:
// here are the pixels of one entry, give me the words. Everything about
// *which* engine, where it came from and how it is reached is on the far
// side of this interface, and the two hosts answer it differently for
// reasons that are about their platforms and not about journals.
//
//
// The two engines, and the decision about each
// -------------------------------------------
//
// Tesseract, both times (Apache-2.0, which CONTRIBUTING.md's inbound rule
// allows). `.tesseract-version` and `.tesseract-js-version` pin them the
// way `.emscripten-version` pins emsdk, and `docs/journal.md` §5 is the
// decision record. In short:
//
//   * **Desktop**: the player's own installed `tesseract`, run as a
//     program. Not linked, not vendored, not fetched by the build. That
//     keeps a large C++ dependency out of every contributor's configure
//     step for a feature used once during onboarding, and it keeps the
//     licence question trivial — nothing is combined with anything. The
//     version is asked of the engine at ingestion and written into the
//     store, so a store always says what read it.
//   * **Browser**: tesseract.js, served from the page's own origin,
//     fetched into the build tree by `scripts/fetch-ocr-engine.py` and
//     never committed. The deployed page does not reach a CDN at
//     runtime; if the engine is not beside the page, the page says so in
//     as many words rather than quietly recognizing nothing.
//
// Neither is required. A build with no engine ingests every entry's image
// and stores no text, and says both numbers — which is the honest state
// and is exactly what a player who has not installed anything should be
// told.
//
//
// Why the engine does not see the document
// ---------------------------------------
//
// It is handed one entry's scan and nothing else: not the file, not the
// path, not the edition. An engine that never sees the document cannot
// leak it, cannot cache it, and cannot be the reason a page uploads it
// somewhere. On the desktop that promise is one directory of temporary
// files this host writes and deletes; in the browser it is a typed array
// that never leaves the tab.
//
//
// Two shapes of scan, and who crops (#212)
// ----------------------------------------
//
// `journal_scan` (`journal_extract.h`) is either samples this build
// produced or a stream it could not decode, and it says which. The
// difference reaches an engine as one obligation:
//
//   * **`gray`** — the bitmap is already the entry. Read all of it.
//   * **`jpeg`** — the bytes are the whole *page*, and `scan.region` is
//     the part of it that is the entry. Read the page and **keep only
//     what falls inside that rectangle.**
//
// The second is not a burden invented here: both engines already report
// where on the page each word was — Tesseract through its `tsv` output,
// tesseract.js through `data.words[].bbox` — so filtering by rectangle is
// reading a number they were going to produce anyway. It is written into
// this interface rather than left to each host because two hosts that
// filtered differently would give a player two different transcriptions
// of one page, and neither could be said to be wrong.
//
// An engine that ignores the region is not *broken*, it is imprecise: it
// returns the whole page's text where the entry's was asked for. That is
// the failure mode to expect from a new engine, and it looks like a
// journal entry with its neighbours attached.

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::host {

/// An OCR engine.
///
/// Implemented by a host — `sdl::tesseract_ocr` runs the player's own
/// installed engine, the page's `journal.mjs` drives tesseract.js, and
/// `journal_probe.h`'s answers for exactly one known image so that CI can
/// drive the whole pipeline without either.
class journal_ocr {
 public:
  journal_ocr() = default;
  journal_ocr(const journal_ocr&) = delete;
  journal_ocr(journal_ocr&&) = delete;
  journal_ocr& operator=(const journal_ocr&) = delete;
  journal_ocr& operator=(journal_ocr&&) = delete;
  virtual ~journal_ocr() = default;

  /// Read `scan` and leave its text in `out`.
  ///
  /// For a `gray` scan that is the whole bitmap; for an encoded one it is
  /// the words inside `scan.region` and no others (see above).
  ///
  /// False for an engine that could not read this scan — which is a
  /// finding about one entry and not about the run: the ingester records
  /// it, counts it, and goes on to the next entry. An engine that is not
  /// there at all is a different thing and is `nullptr`, not a false.
  [[nodiscard]] virtual bool recognize(const journal_scan& scan,
                                       std::string& out) = 0;

  /// What this engine is, in one line — `tesseract 5.5.1`, or whatever
  /// the engine says of itself. It goes into the store's header, because
  /// "which engine read this" is the first question anybody asks of a
  /// transcription they think is wrong.
  [[nodiscard]] virtual std::string_view engine() const = 0;
};

}  // namespace amberfolio::host
