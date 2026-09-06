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
// tesseract.js through a `bbox` on every word of its `blocks` (#306 is
// what reading the previous major's `data.words` cost) — so filtering by
// rectangle is reading a number they were going to produce anyway. It is
// written into this interface rather than left to each host because two
// hosts that filtered differently would give a player two different
// transcriptions of one page, and neither could be said to be wrong.
//
// An engine that ignores the region is not *broken*, it is imprecise: it
// returns the whole page's text where the entry's was asked for. That is
// the failure mode to expect from a new engine, and it looks like a
// journal entry with its neighbours attached.
//
//
// What a reading's line breaks mean (#331)
// ----------------------------------------
//
// The text an engine answers is read by one thing, and that thing honours
// exactly one break. `core/src/machine/seam_journal.cpp` reflows an entry
// into a page twenty-two or thirty-eight characters wide, and since #316
// it reads a **single newline as a space** — an engine emits one per
// *printed* line, and the journal is set in a column that has nothing to
// do with the reader's — and a **blank line as a paragraph break**, which
// gets one blank row however many blank lines there were.
//
// So the whitespace an engine answers is an interface and not a detail:
//
//   * a **space** between two words of one printed line;
//   * **one newline** between two lines of one paragraph;
//   * a **blank line** between two paragraphs;
//   * **one newline** between two fragments of one entry, which is a
//     continuation and not a break — an entry is a list of rectangles
//     because entries flow out of a column onto the facing page
//     (`journal_facts.h`), and eighteen of the first edition's fifty-eight
//     do. Both hosts join their pieces here, not in the engine.
//
// It is written down here for the reason the region rule above is: an
// engine that got this wrong would not look broken. Every paragraph break
// missing reads as one long block of prose — which is what #331 was, for
// the length of a release — and a break in the wrong place reads as a
// journal whose paragraphs are somewhere else.
//
// The three engines this project has all agree, and one of them for free:
// Tesseract's own plain text ends every line with `"\n"` and every
// paragraph with one more, so `tesseract_linked_ocr` needed no change.
// The other two are built out of the layout numbering both report per
// word — Tesseract's `tsv` columns (`sdl/src/tsv_words.h`) and
// tesseract.js's `blocks[].paragraphs[].lines[]` (`web/page/journal.mjs`).
//
// None of the three **invents** a break: each carries the engine's own
// paragraph decision, so an edition whose paragraphs Tesseract's detector
// runs together still comes out as one block. That is a question about a
// real document and a real engine and it is unmeasured;
// `docs/journal.md` §5 says why this edition is worth checking.
//
//
// What the engine knew about the reading it just did (#315)
// --------------------------------------------------------
//
// Both engines answer a confidence per word and both hosts were throwing
// it away — the desktop parses Tesseract's `tsv`, whose eleventh column is
// exactly that, and tesseract.js puts a `confidence` on every word beside
// the `bbox` the region filter already reads. That is a measurement this
// pipeline was producing and discarding.
//
// It is worth keeping for one reason and not for another, and #315
// measured which is which. Against a hand-typed truth for two real
// entries — 341 words, 26 of them wrong — flagging every word under
// sixty picked out 2.6% of the words and 78% of what it picked was
// genuinely wrong, but it caught only 27% of the errors. So:
//
//   * **Marking doubtful words in the text is not worth it.** Three
//     quarters of the mistakes are ones the engine is confident about —
//     an apostrophe read as a double quote, a lower-case `k` read as a
//     capital — and a mark that finds a quarter of them while putting
//     noise in front of a reader is a bad trade.
//   * **A per-entry score is.** Across a whole real edition the mean
//     confidence separated the readings that were fine from the two that
//     were not by a wide margin, and it moved with the character error
//     rate on every setting #315 tried (70.4 at the old setting, 86.1
//     with page segmentation fixed, 90.9 with the page upscaled). It is
//     what a host prints so a player knows which of their ninety-nine
//     entries to look at before they need them in play.
//
// So the confidence is carried up here as a summary of the *reading*, and
// the text is left alone. An engine that does not know is `known: false`
// rather than zero, because "the engine was not sure" and "this engine
// does not report confidence" are different facts and a zero would be
// read as the first.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::host {

/// Under this, a word is one the engine was not sure of.
///
/// Sixty, measured (see above) rather than picked: it is where the
/// precision of the flag was still around three quarters on the two
/// entries #315 scored by hand, and where the share of a whole real
/// edition's words that it flags falls from a quarter to a twentieth once
/// page segmentation is right. Tesseract's scale is 0 to 100 and both
/// engines report on it.
inline constexpr double journal_doubtful_confidence = 60.0;

/// What an engine knew about the reading it just answered.
///
/// A summary and not a per-word list, deliberately: the per-word numbers
/// are what an engine's own filter reads them for, and nothing above this
/// interface has been shown to have a use for them that is better than
/// the noise it would cost (see above).
struct journal_reading_quality {
  /// False for an engine that does not report confidences at all, in
  /// which case every other field here is meaningless. Not a zero
  /// confidence, which is a different statement.
  bool known{false};
  /// How many words the engine kept for this entry.
  std::size_t words{0};
  /// How many of them were under `journal_doubtful_confidence`.
  std::size_t doubtful{0};
  /// The mean confidence over those words, on the engine's own 0-100
  /// scale.
  double confidence{0.0};

  /// The share of the entry the engine was unsure of, 0 to 1.
  [[nodiscard]] double doubtful_share() const noexcept {
    return words == 0
               ? 0.0
               : static_cast<double>(doubtful) / static_cast<double>(words);
  }
};

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

  /// What the engine knew about the reading `recognize()` just did
  /// (#315), or an unknown quality for an engine that does not say.
  ///
  /// Valid only immediately after a `recognize()` that answered true, the
  /// way `errno` is valid only immediately after the call that set it.
  /// A virtual with a default rather than a pure one, because it is a
  /// thing an engine *may* report and every implementation that cannot
  /// should say so by saying nothing: making it pure would have every
  /// fixture in the tree grow a line that means "no".
  [[nodiscard]] virtual journal_reading_quality quality() const { return {}; }
};

}  // namespace amberfolio::host
