// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's page decoder, for the entries that are pictures
// (#328). Only in a build that has already linked one
// (`AMBERFOLIO_LINK_TESSERACT`, `cmake/AmberfolioTesseract.cmake`).
//
//
// Why there is one here at all, and only here
// -------------------------------------------
//
// A page of a journal reaches an OCR engine one of two ways
// (`docs/journal.md` §4a): as samples this build produced, or — for the
// one tabled edition, whose every page is `/DCTDecode` — as the stream's
// own bytes, with the engine doing the decoding. #212 settled that this
// project would not carry a JPEG decoder, and that stands.
//
// A **picture** has no engine to hand that work to. Somebody has to turn
// the page into samples, and `host::journal_page_decoder` is the door
// that says so out loud. The rule this file obeys is the same one that
// kept a decoder out in the first place: *don't write one*. Use the one
// that is already in the process for another reason.
//
// So this exists exactly where Leptonica and libjpeg-turbo are already
// linked, because Tesseract needs them — the `AMBERFOLIO_LINK_TESSERACT`
// build, which is also the build a player who has installed nothing
// gets. It is one call to `pixReadMem`, which `tesseract_linked_ocr.cpp`
// already makes on the same bytes, and one to `pixConvertTo8`.
//
// A default desktop build has no decoder within reach and therefore no
// pictures: the ingestion says `pictures 0 of 14` and names the filter,
// which is `docs/journal.md` §4's "log, don't fake" for a thing this
// build honestly cannot do. That is the same asymmetry §5a already
// records for the *quality* of a reading, and it closes the same way —
// by linking the engine, not by growing a decoder.

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_picture.h"

namespace amberfolio::sdl {

/// Leptonica, asked for eight bits of gray a sample.
///
/// It answers whatever the page is — the archive release's are
/// `/DeviceRGB` — so the conversion to gray is here rather than assumed,
/// and it is Leptonica's own `pixConvertTo8`, which uses the standard
/// luma weights `journal_extract.cpp` applies to an RGB page it decodes
/// itself. Two paths to one answer, on purpose: a picture out of a Flate
/// edition and a picture out of this one should not differ because of
/// who did the arithmetic.
class leptonica_page_decoder final : public host::journal_page_decoder {
 public:
  [[nodiscard]] bool decode(std::span<const std::uint8_t> stream,
                            host::journal_bitmap& out) override;

  [[nodiscard]] const char* name() const noexcept override;
};

}  // namespace amberfolio::sdl
