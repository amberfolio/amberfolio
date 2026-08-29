// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal probe: a document this project made, so the ingestion can
// be driven end to end without one it did not (M5-E3, #174).
//
// `known_journals()` is empty and will stay empty until somebody sits
// down with a real edition, and no page, scan or word of a real journal
// may ever enter this tree (CONTRIBUTING.md). Both of those are settled,
// and together they would leave the entire pipeline — offsets, inflate,
// predictor, crop, engine, store — with no test that runs anywhere but on
// a maintainer's own desk.
//
// So this file builds a document. A real PDF, small, deterministic to the
// byte, with two image XObjects in it and a fact table that says where
// they are; the fingerprint is of the bytes this code produces, so it is
// a fact about a file that exists rather than an invention. It is the
// same arrangement `tests/programs` has with the game — a self-written
// artifact that *can* be committed, standing in for one that cannot —
// and the same arrangement the web host's probe seam has with a player's
// seam listing.
//
//
// Two entries, chosen for what they exercise
// -----------------------------------------
//
// Not for realism. Between them they cover every branch the extractor
// has that a real edition could take:
//
//   * entry 1 — eight bits of gray, no predictor. The plain path;
//   * entry 2 — one bit a pixel, inverted, PNG predictor, and a
//     different row filter on every row, so all five of them run.
//
// Both are FlateDecode, and the streams are **stored** deflate blocks:
// nothing in this tree compresses anything, so the probe writes the one
// form of a zlib stream that can be produced without a compressor. That
// leaves libdeflate's Huffman decoding untested *here*, which is correct
// — it is tested by libdeflate, against the world's compressors, which is
// the whole reason for using it (`cmake/AmberfolioLibdeflate.cmake`).
//
//
// The engine, and why it is not a stub
// -----------------------------------
//
// `journal_probe_ocr` answers text for exactly one image: the one the
// probe's own bitmap generator produces for that entry, compared pixel by
// pixel. Anything else, it refuses. So it is not a fake OCR that says yes
// to whatever it is handed — it is a fixture that can only be satisfied
// by the right offset, the right filter, the right predictor and the
// right crop, and a text store with its words in it is evidence that all
// four were right.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ocr.h"

namespace amberfolio::host {

/// How many entries the probe edition has.
inline constexpr std::size_t journal_probe_entries = 2;

/// The probe document's bytes — the same bytes on every target, every
/// time. Built once and cached.
[[nodiscard]] const std::vector<std::uint8_t>& journal_probe_pdf();

/// The probe edition: its fingerprint is of `journal_probe_pdf()`, and
/// its entries are the facts about the two images in it.
///
/// A one-element span, for handing to `journal_ingester`'s constructor.
/// It is deliberately *not* in `known_journals()`: a player's build has
/// no business knowing about a document this project made up.
[[nodiscard]] std::span<const journal_edition> journal_probe_table();

/// What entry `index` of the probe is supposed to look like once it has
/// been decoded and cropped — the answer the extractor has to produce.
///
/// Generated from the same description the document was generated from,
/// so a test compares two derivations of one intention rather than a
/// result against a copy of itself.
[[nodiscard]] journal_bitmap journal_probe_expected(std::size_t index);

/// The text `journal_probe_ocr` answers for entry `index`.
[[nodiscard]] std::string_view journal_probe_text(std::size_t index);

/// The fixture engine: the probe's words for the probe's pixels, and
/// nothing for anything else.
class journal_probe_ocr final : public journal_ocr {
 public:
  journal_probe_ocr();

  [[nodiscard]] bool recognize(const journal_bitmap& page,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override;

 private:
  std::vector<journal_bitmap> expected_;
};

}  // namespace amberfolio::host
