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

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"

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

 private:
  /// A directory of this host's own, made on first use and removed in the
  /// destructor.
  [[nodiscard]] bool scratch(std::string& out);

  /// The two shapes of a scan's piece (see above): a decoded bitmap as a
  /// PGM, read whole; a stream under its own name, read whole and
  /// filtered to that piece's rectangle afterwards. `recognize()` joins
  /// what they answer, in order.
  [[nodiscard]] bool recognize_bitmap(const host::journal_bitmap& page,
                                      std::string& out);
  [[nodiscard]] bool recognize_encoded(const host::journal_part& part,
                                       std::string& out);

  std::string program_;
  std::string engine_;
  std::string scratch_;
};

}  // namespace amberfolio::sdl
