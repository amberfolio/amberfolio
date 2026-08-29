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
// Tesseract is Apache-2.0, so linking it would be allowed
// (CONTRIBUTING.md's inbound rule). It is not linked anyway, for reasons
// that are about this project rather than about licences:
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
// in as many words rather than quietly recognizing nothing. Packaging —
// whether a desktop build should ship an engine of its own — is M6's
// question, and `journal_ocr.h`'s interface is what makes it answerable
// without touching anything above it.
//
//
// What crosses to the engine, and what does not
// ---------------------------------------------
//
// One entry's cropped bitmap, as a PGM in a directory of this host's own
// making, deleted as soon as the engine has answered. Never the
// document, never its path, never anything else about it. That is
// `journal_ocr.h`'s promise and this is the one implementation where it
// costs something to keep — and the cost is worth it, because a
// player's document is a player's document.
//
// PGM (P5) rather than PNG because it is eight lines to write and
// Leptonica reads it natively: an image encoder would be code with a
// bug budget, standing between the pixels this project checked and the
// engine that reads them.

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

  [[nodiscard]] bool recognize(const host::journal_bitmap& page,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override { return engine_; }

 private:
  /// A directory of this host's own, made on first use and removed in the
  /// destructor.
  [[nodiscard]] bool scratch(std::string& out);

  std::string program_;
  std::string engine_;
  std::string scratch_;
};

}  // namespace amberfolio::sdl
