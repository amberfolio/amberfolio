// SPDX-License-Identifier: AGPL-3.0-only
//
// The linked OCR engine. tesseract_linked_ocr.h has the reasoning.

#include "tesseract_linked_ocr.h"

#include <leptonica/allheaders.h>
#include <tesseract/baseapi.h>

#include <cstddef>
#include <string>
#include <utility>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"

namespace amberfolio::sdl {
namespace {

/// Shorter than this and the engine has read nothing worth keeping, which
/// is what sends a piece round again under automatic page segmentation
/// (tesseract_linked_ocr.h). Sixteen characters is under a single short
/// line: every entry of the first edition that reads at all reads far more
/// than that, and every one that does not read at all returned three or
/// five.
constexpr std::size_t too_little = 16;

/// Trailing blank lines are the engine's page break and not the entry's
/// text; a store that kept them would diff badly against a corrected copy
/// of the same entry for no reason at all.
void trim_trailing(std::string& text) {
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r' ||
                           text.back() == ' ' || text.back() == '\f')) {
    text.pop_back();
  }
}

/// Whatever the engine answered, as a string this owns.
[[nodiscard]] std::string taken(char* text) {
  std::string out;
  if (text != nullptr) {
    out.assign(text);
    delete[] text;
  }
  trim_trailing(out);
  return out;
}

/// What the engine was sure of, off the per-word confidences it has
/// already computed (#315).
///
/// `AllWordConfidences()` answers a `-1`-terminated array the caller owns
/// — the same ownership `GetUTF8Text()` has, and freed the same way. It
/// is asked *after* the recognition whose numbers are wanted, because
/// like every other accessor on this API it reports the last page read.
[[nodiscard]] host::journal_reading_quality confidences(
    tesseract::TessBaseAPI& api) {
  host::journal_reading_quality out;
  int* every = api.AllWordConfidences();
  if (every == nullptr) {
    return out;
  }
  double total = 0.0;
  for (const int* at = every; *at >= 0; ++at) {
    ++out.words;
    total += static_cast<double>(*at);
    if (static_cast<double>(*at) < host::journal_doubtful_confidence) {
      ++out.doubtful;
    }
  }
  delete[] every;
  if (out.words != 0) {
    out.known = true;
    out.confidence = total / static_cast<double>(out.words);
  }
  return out;
}

}  // namespace

tesseract_linked_ocr::tesseract_linked_ocr(std::string tessdata)
    : tessdata_(std::move(tessdata)) {}

tesseract_linked_ocr::~tesseract_linked_ocr() {
  if (api_ != nullptr) {
    api_->End();
    delete api_;
  }
}

bool tesseract_linked_ocr::available() {
  // Leptonica complains about the image formats this build switched off
  // (`cmake/AmberfolioTesseract.cmake`) — Tesseract probes for a TIFF
  // debug font it will never draw. Those are messages about a decision
  // rather than about a fault, and a player watching an ingestion should
  // not read a wall of them.
  setMsgSeverity(L_SEVERITY_NONE);

  api_ = new tesseract::TessBaseAPI();
  if (api_->Init(tessdata_.c_str(), "eng") != 0) {
    delete api_;
    api_ = nullptr;
    return false;
  }
  engine_ = std::string("tesseract ") + tesseract::TessBaseAPI::Version() +
            " (linked)";
  return true;
}

bool tesseract_linked_ocr::read_part(const host::journal_part& part,
                                     bool encoded, std::string& out,
                                     host::journal_reading_quality& how) {
  out.clear();
  how = {};
  if (encoded) {
    // The stream, decoded by Leptonica and cropped by the engine. This
    // host has not looked inside it and does not need to (#212).
    Pix* page = pixReadMem(part.encoded.data(), part.encoded.size());
    if (page == nullptr) {
      return false;
    }
    api_->SetImage(page);
    api_->SetRectangle(static_cast<int>(part.region.left),
                       static_cast<int>(part.region.top),
                       static_cast<int>(part.region.width),
                       static_cast<int>(part.region.height));
    api_->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
    out = taken(api_->GetUTF8Text());
    if (out.size() < too_little) {
      // A picture with a caption on it rather than a column of prose.
      api_->SetPageSegMode(tesseract::PSM_AUTO);
      api_->SetRectangle(static_cast<int>(part.region.left),
                         static_cast<int>(part.region.top),
                         static_cast<int>(part.region.width),
                         static_cast<int>(part.region.height));
      out = taken(api_->GetUTF8Text());
    }
    // After the recognition whose numbers are wanted, and before the
    // image goes: the accessor reports the last page read.
    how = confidences(*api_);
    pixDestroy(&page);
    return !out.empty();
  }

  // Samples this build produced, already cropped, handed over as bytes —
  // no Leptonica and no image format in the way.
  if (part.gray.empty()) {
    return false;
  }
  api_->SetImage(part.gray.pixels.data(), static_cast<int>(part.gray.width),
                 static_cast<int>(part.gray.height), 1,
                 static_cast<int>(part.gray.width));
  api_->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
  out = taken(api_->GetUTF8Text());
  if (out.size() < too_little) {
    api_->SetPageSegMode(tesseract::PSM_AUTO);
    out = taken(api_->GetUTF8Text());
  }
  how = confidences(*api_);
  return !out.empty();
}

bool tesseract_linked_ocr::recognize(const host::journal_scan& scan,
                                     std::string& out) {
  out.clear();
  quality_ = {};
  if (api_ == nullptr || scan.empty()) {
    return false;
  }
  // Every piece, in the order the fact table put them in, joined the way a
  // reader would read them (#214). A piece the engine could not read fails
  // the entry rather than leaving a hole in the middle of it.
  const bool encoded = scan.encoding != host::journal_encoding::gray;
  std::string piece;
  host::journal_reading_quality how;
  double weighted = 0.0;
  for (const host::journal_part& part : scan.parts) {
    if (!read_part(part, encoded, piece, how)) {
      out.clear();
      quality_ = {};
      return false;
    }
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += piece;
    // Weighted by words, so an entry whose second piece is three words
    // does not pull the whole entry's confidence around (#315).
    if (how.known) {
      quality_.known = true;
      quality_.words += how.words;
      quality_.doubtful += how.doubtful;
      weighted += how.confidence * static_cast<double>(how.words);
    }
  }
  if (quality_.words != 0) {
    quality_.confidence = weighted / static_cast<double>(quality_.words);
  }
  return !out.empty();
}

}  // namespace amberfolio::sdl
