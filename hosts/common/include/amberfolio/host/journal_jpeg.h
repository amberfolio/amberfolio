// SPDX-License-Identifier: AGPL-3.0-only
//
// The page decoder every build has (#345).
//
//
// What it is for, and why it stopped being optional
// ------------------------------------------------
//
// A page of a journal reaches an OCR engine one of two ways
// (`docs/journal.md` §4a): as samples this build produced, or — for the
// one tabled edition, whose every page is `/DCTDecode` — as the stream's
// own bytes, with the engine doing the decoding. That is what let #212
// keep a JPEG decoder out of this project, and for *text* it still holds:
// nothing below is on the path of an entry that is words.
//
// A **picture** has no engine to hand the work to. #328 built
// `journal_page_decoder` as a door for that, to be filled by a host out
// of something it already linked — and the result was a drawing that only
// one build of one host could show. A default desktop build reported
// `pictures=0/14` and named the filter; the browser had no picture path
// at all; and a player who had done nothing wrong saw an entry's caption
// with the map missing under it (#345). An enhancement that works in one
// configuration is not an enhancement, so the decoder is here, in the
// library both hosts link, and every build has it.
//
// `cmake/AmberfolioStbImage.cmake` is why it is stb_image and not the
// libjpeg-turbo the OCR chain already builds.
//
//
// One decoder, so one answer
// --------------------------
//
// While Leptonica did this on one build and nothing did it on the others,
// two ingestions of one document could not be expected to agree: the
// decode in front of the reducer was each library's own, and
// `docs/journal.md` §11.2 recorded that two of them differ on a third of
// a percent of the pixels by one level. With a single decoder compiled
// into `amberfolio::host_services` that difference is gone, and a store's
// `fingerprint()` is a fact about the document and the reduction rather
// than about which host made it.
//
// The door itself stays. `journal_probe.h`'s fixture decoder is what
// tests the caller's half — a decoder that answers the wrong page, or
// none — and an edition whose pages are a filter stb does not read is
// still a host that says so and shows the words.

#pragma once

#include <cstdint>
#include <span>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_picture.h"

namespace amberfolio::host {

/// stb_image, asked for the page's own components and reduced to gray
/// here.
///
/// **Not `desired_channels = 1`**, which would have been shorter. stb's
/// own gray is `(77r + 150g + 29b) >> 8`; this project's is Rec. 601 to
/// three decimals (`journal_gray()`), and it is what
/// `journal_extract.cpp` applies to every page it decodes itself. A
/// picture out of a Flate edition and a picture out of a `/DCTDecode` one
/// must not differ because of who did the arithmetic, so the arithmetic
/// is stated once and both paths call it.
class jpeg_page_decoder final : public journal_page_decoder {
 public:
  [[nodiscard]] bool decode(std::span<const std::uint8_t> stream,
                            journal_bitmap& out) override;

  [[nodiscard]] const char* name() const noexcept override;
};

/// The one every `journal_ingester` starts with.
///
/// A single shared object because it holds nothing: a decoder is a
/// function with a vtable in front of it, and a host that wants a
/// different one still passes it to `set_page_decoder()`. Passing null
/// there is how a caller asks for *no* decoder, which is the state the
/// probe's refusal cases are about and no shipped build is in.
[[nodiscard]] journal_page_decoder& default_page_decoder() noexcept;

}  // namespace amberfolio::host
