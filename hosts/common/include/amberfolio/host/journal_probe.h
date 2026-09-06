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
// byte, with three image XObjects in it and a fact table that says where
// they are; the fingerprint is of the bytes this code produces, so it is
// a fact about a file that exists rather than an invention. It is the
// same arrangement `tests/programs` has with the game — a self-written
// artifact that *can* be committed, standing in for one that cannot —
// and the same arrangement the web host's probe seam has with a player's
// seam listing.
//
//
// Three entries, chosen for what they exercise
// -------------------------------------------
//
// Not for realism. Between them they cover every branch the extractor
// has that a real edition could take:
//
//   * entry 1 — eight bits of gray, no predictor. The plain path;
//   * entry 2 — one bit a pixel, inverted, PNG predictor, and a
//     different row filter on every row, so all five of them run. It is
//     also the one whose text carries a **paragraph break** (#331), which
//     is the one piece of shape a reading has: what it proves is that a
//     blank line survives the ingester, the store, the ABI and the
//     reader on all four targets, not that an engine found one — the
//     fixture below is not an engine, and finding one is checked where
//     the layout walks are (`sdl/src/tsv_words.h`, `web/page/journal.mjs`);
//   * entry 3 — `/DCTDecode`, which this build does **not** decode: its
//     stream goes to the engine as its own bytes with the entry's
//     rectangle beside it (M5-E3a, #212).
//
// The first two are FlateDecode, and their streams are **stored** deflate
// blocks: nothing in this tree compresses anything, so the probe writes
// the one form of a zlib stream that can be produced without a
// compressor. That leaves libdeflate's Huffman decoding untested *here*,
// which is correct — it is tested by libdeflate, against the world's
// compressors, which is the whole reason for using it
// (`cmake/AmberfolioLibdeflate.cmake`).
//
//
// The third one is a real JPEG, and a boring one on purpose
// --------------------------------------------------------
//
// It is a well-formed baseline JPEG this file encodes: a flat field of
// one gray, at a size that is whole 8x8 blocks, with a flat quantization
// table and two Huffman tables of two and one code. Every block after the
// first is a zero DC difference and an end-of-block, which is why the
// whole encoder is short enough to read.
//
// **Flat, and not a picture of words**, and that is the honest shape for
// what it has to prove. Nothing in this build decodes it — that is the
// entire point of entry 3 — so what the probe needs is a stream that
// *is* a JPEG of the right shape, arriving at the engine byte for byte.
// Whether a real engine reads real words off a real scan is the boundary
// `docs/journal.md` §7 already names as untested, and a probe that
// encoded text into a JPEG would not move it: the fixture below would
// still be answering by fiat, and no real engine runs in CI.
//
// What it does prove, and could not before: that a `/DCTDecode` row is
// followed to the right offset, bounds-checked, refused when its region
// is off the page, and handed over **unaltered**.
//
//
// The engine, and why it is not a stub
// -----------------------------------
//
// `journal_probe_ocr` answers text for exactly one scan per entry: for a
// decoded one, the bitmap the probe's own generator produces, compared
// pixel by pixel; for entry 3, the exact bytes the probe's own encoder
// produced, compared byte for byte and with the region checked too.
// Anything else, it refuses. So it is not a fake OCR that says yes to
// whatever it is handed — it is a fixture that can only be satisfied by
// the right offset, the right filter, the right predictor and the right
// crop, and a text store with its words in it is evidence that all four
// were right.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_picture.h"

namespace amberfolio::host {

/// How many entries the probe edition has.
inline constexpr std::size_t journal_probe_entries = 4;

/// Which row of the probe is the one that collides (M5-E3d, #218): a
/// **tale** numbered one, over the same rectangle as entry one.
///
/// It is what proves the kind end to end on all four targets — extraction
/// through the engine into the store and back out — and it costs the
/// document nothing, because it points at a fragment that is already
/// there. A build that keyed anything on the number alone loses one of
/// the two rows here, and does it silently.
inline constexpr std::size_t journal_probe_colliding_entry = 3;

/// Which of them is the `/DCTDecode` one (#212). Named rather than
/// spelled `2` at each of its several readers, because "the entry that
/// goes through undecoded" is the fact and its index is an accident.
///
/// It is also the one with **two** fragments (M5-E3b, #214), so the probe
/// exercises an entry that flows the way a real edition's do.
inline constexpr std::size_t journal_probe_encoded_entry = 2;

/// How many pieces the three entries have between them: one, one, two.
inline constexpr std::size_t journal_probe_fragments = 4;

/// How many **pictures** the probe has, and which entries they are on
/// (#328).
///
/// Two, and they are on the two entries that reach the extractor by
/// different routes on purpose: entry one's page this build decodes
/// itself, so its picture is made in CI on every target with nothing
/// installed; entry three's is `/DCTDecode`, so its picture is what
/// proves the other half — refused with a sentence when there is no
/// decoder, and produced when a host supplies one.
///
/// Their rectangles are **not** the entries' text rectangles, which is
/// the shape a real edition turned out to have: a picture is measured to
/// its ink and a text fragment to its column, and an extraction that
/// confused the two would produce a visibly different bitmap.
inline constexpr std::size_t journal_probe_art = 2;
inline constexpr std::size_t journal_probe_art_decoded_entry = 0;
inline constexpr std::size_t journal_probe_art_encoded_entry =
    journal_probe_encoded_entry;

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
///
/// **Empty for `journal_probe_encoded_entry`**, which has no decoded
/// answer at all: what the extractor must produce for it is bytes, and
/// `journal_probe_encoded()` is that.
[[nodiscard]] journal_bitmap journal_probe_expected(std::size_t index);

/// What entry `index`'s stream is, byte for byte — the answer the
/// extractor has to hand an engine for an entry this build does not
/// decode (#212). Empty for every other entry.
[[nodiscard]] std::span<const std::uint8_t> journal_probe_encoded(
    std::size_t index);

/// The samples picture `index` of the probe is supposed to be reduced
/// from — the cropped gray, before `reduce_picture()` touches it.
///
/// Generated from the same description the document was generated from,
/// on `journal_probe_expected()`'s own argument: a test that compares
/// the ingestion's picture with `reduce_picture()` of this is comparing
/// two derivations of one intention, and one that compared it against a
/// stored bitmap would be comparing a result with a copy of itself.
[[nodiscard]] journal_bitmap journal_probe_art_expected(std::size_t index);

/// The text `journal_probe_ocr` answers for entry `index`.
[[nodiscard]] std::string_view journal_probe_text(std::size_t index);

/// What `journal_probe_noisy_ocr` puts on the end of every reading, and
/// what that costs in edits (#315).
///
/// An **append**, and not a substitution or a dropped character, and the
/// reason is the whole point of the fixture: a string and the same string
/// with three characters on the end are exactly three character edits and
/// exactly one word edit apart — no less, because a Levenshtein distance
/// is at least the difference in length, and no more, because the trivial
/// alignment achieves it. So the error rate a test expects is arithmetic
/// off `journal_probe_text()`'s own lengths rather than a number somebody
/// ran the harness once to find out. A substitution has no such
/// guarantee: the cheapest alignment of two strings that differ in one
/// place is usually one edit and is not always.
inline constexpr std::string_view journal_probe_noise = " zz";
inline constexpr std::size_t journal_probe_noise_edits = 3;
inline constexpr std::size_t journal_probe_noise_word_edits = 1;

/// The fixture engine: the probe's words for the probe's pixels, and
/// nothing for anything else.
class journal_probe_ocr final : public journal_ocr {
 public:
  journal_probe_ocr();

  [[nodiscard]] bool recognize(const journal_scan& scan,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override;

 private:
  std::vector<journal_bitmap> expected_;
};

/// The fixture decoder: the probe's own samples for the probe's own
/// encoded page, and nothing for anything else (#328).
///
/// A picture, unlike a page of text, has no OCR engine to hand a
/// `/DCTDecode` stream to, so a host that wants pictures out of such an
/// edition has to supply something that decodes one
/// (`journal_picture.h`). Nothing in this tree does — #212's refusal
/// stands — so what CI can prove about that path is the plumbing around
/// it, and this is what proves it.
///
/// It is not a stub that answers anything: it compares the bytes it is
/// handed against the exact stream the probe's own encoder produced, so
/// a wrong offset, a wrong length or a stream reached by the text route
/// gets nothing.
class journal_probe_decoder final : public journal_page_decoder {
 public:
  [[nodiscard]] bool decode(std::span<const std::uint8_t> stream,
                            journal_bitmap& out) override;

  [[nodiscard]] const char* name() const noexcept override;

  /// How many times it has been asked, so a test can tell "no picture
  /// because nothing asked" from "no picture because the answer was
  /// refused".
  [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

 private:
  std::size_t calls_{0};
};

/// The same fixture, reading badly on purpose (#315).
///
/// `journal_score.h` measures how well an ingestion went, and a harness
/// that has only ever been shown a perfect reading is a harness nobody
/// has checked. This is the other half of the `journal_probe.h`
/// arrangement applied to that: a synthetic document, and an engine that
/// gets it wrong by an amount this project chose, so the error rate the
/// harness should report is *arithmetic* — nothing here is a number
/// somebody observed and then asserted.
///
/// It is still not a stub. Everything `journal_probe_ocr` refuses, this
/// refuses, because it delegates: the right offset, the right filter, the
/// right predictor and the right crop all still have to be right before
/// there is anything to spoil.
///
/// It also reports a **confidence**, which nothing else in this tree can
/// do without an engine installed. The numbers rise with each reading of
/// a run — 50, 60, 70, ... — which is a fixture's arbitrary choice made
/// for one reason: an aggregate that averaged them unweighted and one
/// that weighted them by words give different answers, and a fixture that
/// answered the same number every time could not tell the two apart.
class journal_probe_noisy_ocr final : public journal_ocr {
 public:
  [[nodiscard]] bool recognize(const journal_scan& scan,
                               std::string& out) override;

  [[nodiscard]] std::string_view engine() const override;

  [[nodiscard]] journal_reading_quality quality() const override {
    return quality_;
  }

 private:
  journal_probe_ocr honest_;
  journal_reading_quality quality_;
  /// How many readings this engine has answered, which is what makes the
  /// confidences differ. Reset by nothing: one of these drives one
  /// ingestion, the way one `journal_probe_ocr` does.
  std::size_t readings_{0};
};

}  // namespace amberfolio::host
