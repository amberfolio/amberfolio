// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal extractor: an entry's scan, out of the player's own PDF and
// into eight bits a pixel (M5-E3, #174).
//
// It is handed the whole document and one row of the fact table, and it
// does four things in order — follow the offset, decode the stream,
// undo the predictor, crop the region — checking at each step that what
// it found is the shape the table said it would be. Every one of those
// checks is what makes this safe to point at a file this project did not
// make: a table row that is wrong, or a document that is not the one the
// fingerprint said, fails at the first size that disagrees rather than
// producing a picture of nothing.
//
//
// Why the whole document at once
// ------------------------------
//
// The SDL host streams a document through the hasher a buffer at a time
// (`--document`), because hashing needs no more than that. Extraction
// needs random access, and the honest choices were a seek-and-read
// interface or a span over the whole file. The span wins on the shape of
// the actual problem: in a browser the document arrives as a `File` the
// page has already read into memory and there is nothing to seek, and on
// the desktop a document is tens of megabytes read once during onboarding
// and dropped. An interface that existed only to be implemented twice,
// one of them trivially, is an interface with no reason to exist.
//
//
// What comes out
// --------------
//
// Eight bits of gray per pixel, tightly packed, top row first — the one
// shape every OCR engine takes and the one this project can compare
// exactly in a test. Colour is thrown away here rather than in an
// engine: what is being read is ink on paper, the conversion is the
// standard luma one, and doing it in one place means the desktop and the
// browser hand their engines identical bytes. `journal_ocr.h` is what
// receives it.
//
//
// ...unless this build cannot decode it, and then the bytes go through
// -------------------------------------------------------------------
//
// M5-E3a (#212). The first real edition anybody put in front of this is a
// PDF whose every page scan is `/DCTDecode` — JPEG — and §4 of
// `docs/journal.md` had already decided what to do about that day before
// it came:
//
//   > What a `/DCTDecode` edition would want is **not a decoder**: it is
//   > passing the stream's own bytes through to the engine, which both
//   > Tesseract and tesseract.js read directly, with the region becoming
//   > the engine's business rather than the extractor's.
//
// So `extract_scan()` answers a `journal_scan`, which is one of two
// things and says which: samples this build produced, already cropped, or
// **the stream exactly as the document holds it**, with the entry's
// rectangle beside it. Nothing in this repository learns what a JPEG is.
//
// The cost is real and is worth naming: the crop moves. This build can
// crop what it decoded and cannot crop what it did not, so for an encoded
// scan the engine reads the whole page and its *output* is filtered to
// the rectangle — which both engines can do, because both report where on
// the page each word was. `journal_ocr.h` is where that contract lives.
//
// The gain is that the alternative was a JPEG decoder in a project whose
// whole argument for owning an inflate library was that a decoder tested
// only against its own encoder is untested (`cmake/AmberfolioLibdeflate`).
// A decoder for a format this project has no way to generate would have
// been worse than that, not better.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "amberfolio/host/journal_facts.h"

namespace amberfolio::host {

/// What went wrong, if anything did.
///
/// Its own reasons, spelled as findings about a *document* rather than as
/// error numbers, for the reason `automap_trouble` has its own: a host
/// that printed `invalid argument` for "this edition uses a filter this
/// build cannot decode" would be saying something untrue, and the person
/// reading it is trying to work out what their PDF needs.
enum class journal_trouble : std::uint8_t {
  /// Nothing has gone wrong.
  none,
  /// The document is not one this build knows the insides of. Not a
  /// failure — the machine saying "I do not know this file", the same
  /// answer `find_document()` gives one level out, and the one PLAN.md §9
  /// asks for.
  unrecognized_edition,
  /// There is no entry with that index or that number.
  no_such_entry,
  /// The table says the stream is at an offset, or has a length, that
  /// runs past the end of this file.
  stream_out_of_bounds,
  /// The stream's filter is one this build does not decode. The name is
  /// in the message; `journal_filter_supported()` is the same question
  /// asked in advance.
  filter_unsupported,
  /// The predictor, the bit depth or the component count is one this
  /// build does not expand.
  image_unsupported,
  /// The bytes at the offset are not a stream of that filter at all.
  stream_corrupt,
  /// They decoded, and to a different number of bytes than an image of
  /// that width, height and depth is made of. The single most useful
  /// failure there is: it is what a table row that is off by a page says.
  stream_size_wrong,
  /// The region is not inside the image.
  region_outside,
  /// There was no OCR engine to recognize with (`journal_ingest.h`).
  no_engine,
  /// The engine was there and did not recognize this image.
  engine_failed,
  /// A text store that is not one, or is one from a version this build
  /// does not read (`journal_store.h`).
  not_a_store,
  /// A store, or a document, larger than this build will read.
  too_large,
};

/// The printable name of one — a short phrase a host puts in a sentence.
/// Never null.
[[nodiscard]] const char* journal_trouble_name(journal_trouble what) noexcept;

/// One entry's scan, decoded: eight bits of gray a pixel, `width` of them
/// a row, `height` rows, top row first.
struct journal_bitmap {
  std::vector<std::uint8_t> pixels;
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] bool empty() const noexcept { return pixels.empty(); }
};

/// How an entry's scan reaches the engine (#212).
enum class journal_encoding : std::uint8_t {
  /// Eight bits of gray a pixel, this build's own samples, **already
  /// cropped** to the entry. Every edition whose filter this build
  /// decodes.
  gray,
  /// The stream's own bytes, exactly as the document holds them, with the
  /// entry's rectangle beside them for the engine to apply to what it
  /// reads. A `/DCTDecode` edition.
  jpeg,
};

/// One piece of an entry's scan, as an engine receives it.
///
/// Two shapes and the scan's own field saying which — never one buffer
/// meaning two things by context, because the two are read by different
/// code and a consumer that guessed wrong would hand an engine a JPEG and
/// call it pixels.
struct journal_part {
  /// `gray` only: the samples, cropped to this piece of the entry.
  journal_bitmap gray;

  /// Anything but `gray`: the stream, whole and unaltered.
  std::vector<std::uint8_t> encoded;

  /// Anything but `gray`: which rectangle of that image this piece is.
  /// Meaningless for `gray`, where the crop has already happened and
  /// `gray.width`/`gray.height` say everything.
  journal_region region{};
};

/// One entry's scan: its pieces, in reading order.
///
/// Usually one. An edition whose entries flow between columns has more
/// (`journal_facts.h`'s `journal_fragment`), and what an engine reads out
/// of them is joined in this order.
struct journal_scan {
  journal_encoding encoding{journal_encoding::gray};
  std::vector<journal_part> parts;

  /// Whether there is anything here to read at all.
  [[nodiscard]] bool empty() const noexcept { return parts.empty(); }
};

/// Get entry `fact` out of `document` and into `out`: every fragment, in
/// order, by whichever of the two routes its filter takes (#212).
///
/// `out` is cleared first, whether this succeeds or not: a scan left over
/// from the previous entry is the one thing that could make a failure look
/// like a success one call later. A fragment that fails fails the whole
/// entry — half an entry read as though it were the whole one is the
/// outcome nothing downstream could detect.
[[nodiscard]] journal_trouble extract_scan(
    std::span<const std::uint8_t> document, const journal_entry_fact& fact,
    journal_scan& out);

/// Decode the stream `fragment` names inside `document` and crop it to
/// that fragment's region.
///
/// The decoding half of `extract_scan()`, one piece at a time, and
/// `filter_unsupported` for a filter this build does not decode — it
/// answers *samples*, and there are none for a stream that goes through
/// untouched. Kept as its own call because a test that wants the pixels
/// wants exactly this.
///
/// `out` is cleared first, on the same reasoning as above.
[[nodiscard]] journal_trouble extract_fragment(
    std::span<const std::uint8_t> document, const journal_fragment& fragment,
    journal_bitmap& out);

/// The decoded, uncropped image — the step before the crop, exposed
/// because it is the one a person checking a new edition's region wants
/// to look at, and because the crop is then a pure function of it that a
/// test can check on its own.
///
/// `filter_unsupported` for an edition this build does not decode, and
/// that is a genuine loss rather than a technicality: the person placing
/// a `/DCTDecode` edition's rectangles cannot preview a page through this
/// and has to open the document in something that reads JPEG. Said here
/// so they find out from the interface rather than from an empty bitmap.
[[nodiscard]] journal_trouble decode_image(
    std::span<const std::uint8_t> document, const journal_fragment& fragment,
    journal_bitmap& out);

/// `region` of `image`, into `out`. `region_outside` if it is not.
[[nodiscard]] journal_trouble crop(const journal_bitmap& image,
                                   const journal_region& region,
                                   journal_bitmap& out);

}  // namespace amberfolio::host
