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
                                     bool encoded, std::string& out) {
  out.clear();
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
  return !out.empty();
}

bool tesseract_linked_ocr::recognize(const host::journal_scan& scan,
                                     std::string& out) {
  out.clear();
  if (api_ == nullptr || scan.empty()) {
    return false;
  }
  // Every piece, in the order the fact table put them in, joined the way a
  // reader would read them (#214). A piece the engine could not read fails
  // the entry rather than leaving a hole in the middle of it.
  const bool encoded = scan.encoding != host::journal_encoding::gray;
  std::string piece;
  for (const host::journal_part& part : scan.parts) {
    if (!read_part(part, encoded, piece)) {
      out.clear();
      return false;
    }
    if (!out.empty()) {
      out.push_back('\n');
    }
    out += piece;
  }
  return !out.empty();
}

}  // namespace amberfolio::sdl
