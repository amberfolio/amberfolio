// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's OCR engine: the player's own Tesseract, run as a
// program (M5-E3, #174).
//
// `journal_ocr.h` is the interface and says what the two hosts do with
// it; this is the desktop's answer, and the decision behind it is worth
// stating plainly because it is a decision and not an accident.
//
//
// Run, not linked
// ---------------
//
// This is the engine a **default** build uses. Since M5-E3c (#216) there
// is another, `tesseract_linked_ocr.h`, behind a CMake option that is off
// unless somebody asks — so what follows is the argument for this one
// being the default rather than an argument that linking is wrong.
//
// Tesseract is Apache-2.0, so linking it is allowed (CONTRIBUTING.md's
// inbound rule). It is not linked *by default*, for reasons that are
// about this project rather than about licences:
//
//   * it is a large C++ dependency with a large C++ dependency of its
//     own (Leptonica), and every contributor would pay for it at every
//     configure — for a feature that runs **once**, during onboarding,
//     and never again while a game is being played;
//   * `.tesseract-version` pins what this host expects, but a player's
//     distribution ships what it ships, and a build that linked one
//     version would be a build that could not use the one already
//     installed;
//   * nothing is combined with anything, so there is no licence question
//     to have an opinion about at all.
//
// What that costs is honest and is stated to the player: an engine that
// is not installed is an engine that is not there, and this host says so
// in as many words rather than quietly recognizing nothing. That cost is
// what #216 was filed about, and the answer was the build option rather
// than a change here — `journal_ocr.h`'s one virtual call is what made it
// answerable without touching anything above it, which is exactly what
// this paragraph used to predict.
//
//
// What crosses to the engine, and what does not
// ---------------------------------------------
//
// One entry's scan, in a directory of this host's own making, deleted as
// soon as the engine has answered. Never the document, never its path,
// never anything else about it. That is `journal_ocr.h`'s promise and
// this is the one implementation where it costs something to keep — and
// the cost is worth it, because a player's document is a player's
// document.
//
// A decoded scan goes over as PGM (P5) rather than PNG because it is
// eight lines to write and Leptonica reads it natively: an image encoder
// would be code with a bug budget, standing between the pixels this
// project checked and the engine that reads them.
//
//
// An encoded scan, and where the crop went (M5-E3a, #212)
// ------------------------------------------------------
//
// A `/DCTDecode` edition's stream is not decoded here, so there is
// nothing to crop and nothing to re-encode: the bytes are written out
// under their own name and Tesseract opens them itself. What that costs
// is that Tesseract reads the **whole page**, and the entry is a
// rectangle of it.
//
// The CLI has no crop flag, and this host is not about to grow an image
// library to make one. What it has instead is `tsv` output: one line per
// word with its `left top width height`, which is a documented Tesseract
// output format and needs no parser worth the name — it is tab-separated
// and the columns are fixed. So the page is read once and its *output* is
// filtered to the region, which is `journal_ocr.h`'s contract and gives
// the same answer a crop would have, without this host ever holding a
// pixel of the page.
//
// It is also the better arrangement of the two: cropping would have meant
// decoding and re-encoding a scan, and every re-encode is a chance to
// hand the engine something slightly worse than what the player has.
//
//
// ...and what reading a whole page costs, now that it is measured (#315)
// ---------------------------------------------------------------------
//
// A lot, and it was almost all one word on the command line.
//
// This host asked for `--psm 6` on both paths — "one uniform block of
// text", which a journal *entry* is. It is emphatically not what a
// **two-page spread** is, and that is what the encoded path hands over.
// Told the page is one block, Tesseract does not look for the four
// columns on it; it reads straight across them, so lines from the
// facing page interleave with the entry's, and the region filter below
// then keeps a plausible-looking wreck.
//
// Measured against a hand-typed truth for two real entries of the one
// edition in the table, on the pinned tesseract.js (the desktop's
// installed engine is still not on any machine this was measured on —
// `docs/hosts.md` §3), the character error rate was:
//
//     whole page, --psm 6      12.1%   21.1%
//     whole page, --psm 3       2.9%    4.0%
//
// So the encoded path asks for `--psm 3` — automatic page segmentation,
// which is Tesseract's own default and what this host was overriding —
// and the decoded path keeps `--psm 6`, because there the image really is
// one block. The rule is not "3 is better": it is that the mode has to
// match what is in the picture, and only one of these two paths hands
// over an entry.
//
// What did **not** help, measured on the same two entries and therefore
// not here: `-c user_defined_dpi=300` (not one character changed), `-c
// preserve_interword_spaces=1` (not one character), and reading the
// region as grey rather than colour (not one character). A `--user-words`
// list of the setting's proper nouns was measured too and is not here
// either — two of the 341 words were proper-noun misreadings, so a list
// that fixed both would move the rate by six parts in a thousand.
//
// **Upscaling is the one lever this engine cannot pull.** Two or three
// times the pixels is worth another halving of the rate, and reaching it
// needs the page decoded — which this host does not do and #212 refused
// on purpose. The decoded path *could* upscale the PGM it writes, and
// does not, because the only edition in the table is `/DCTDecode`: it
// would be code no shipped edition executes. `journal.mjs` does it,
// because a browser has already decoded the page to put it on a canvas
// and the platform's decoder is not this project's.

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"
#include "tsv_words.h"

namespace amberfolio::sdl {

/// The player's installed Tesseract, driven as a program.
class tesseract_ocr final : public host::journal_ocr {
 public:
  /// `program` is what to run — `tesseract` off the path by default, or
  /// whatever `--journal-ocr` was given.
  explicit tesseract_ocr(std::string program);

  ~tesseract_ocr() override;

  /// Ask the engine what it is. False if it is not there, in which case
  /// nothing else here should be called and the host says so.
  ///
  /// Separate from the constructor because "is there an engine" is a
  /// question a host asks once and reports, and a constructor that
  /// answered it by throwing or by leaving an object half-built would be
  /// two ways of saying the same thing badly.
  [[nodiscard]] bool available();

  [[nodiscard]] bool recognize(const host::journal_scan& scan,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override { return engine_; }

  [[nodiscard]] host::journal_reading_quality quality() const override {
    return quality_;
  }

 private:
  /// A directory of this host's own, made on first use and removed in the
  /// destructor.
  [[nodiscard]] bool scratch(std::string& out);

  /// The two shapes of a scan's piece (see above): a decoded bitmap as a
  /// PGM, read whole and as one block; a stream under its own name, read
  /// as a page and filtered to that piece's rectangle afterwards.
  /// `recognize()` joins what they answer, in order.
  ///
  /// Both answer a `tsv_reading` — the words and what the engine thought
  /// of them (#315). The decoded path does not need the rectangle and
  /// asks for `tsv` anyway, because plain text carries no confidences and
  /// the confidences are the only thing a host can tell a player about an
  /// entry nobody has corrected yet.
  [[nodiscard]] bool recognize_bitmap(const host::journal_bitmap& page,
                                      tsv_reading& out);
  [[nodiscard]] bool recognize_encoded(const host::journal_part& part,
                                       tsv_reading& out);

  std::string program_;
  std::string engine_;
  std::string scratch_;
  /// What the last `recognize()` was sure of, summed over its pieces.
  host::journal_reading_quality quality_;
};

}  // namespace amberfolio::sdl
