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

/// Decode the stream `fact` names inside `document`, crop it to the
/// entry's region, and leave the result in `out`.
///
/// `out` is cleared first, whether this succeeds or not: a bitmap left
/// over from the previous entry is the one thing that could make a
/// failure look like a success one call later.
[[nodiscard]] journal_trouble extract_entry(
    std::span<const std::uint8_t> document, const journal_entry_fact& fact,
    journal_bitmap& out);

/// The decoded, uncropped image — the step before the crop, exposed
/// because it is the one a person checking a new edition's region wants
/// to look at, and because the crop is then a pure function of it that a
/// test can check on its own.
[[nodiscard]] journal_trouble decode_image(
    std::span<const std::uint8_t> document, const journal_entry_fact& fact,
    journal_bitmap& out);

/// `region` of `image`, into `out`. `region_outside` if it is not.
[[nodiscard]] journal_trouble crop(const journal_bitmap& image,
                                   const journal_region& region,
                                   journal_bitmap& out);

}  // namespace amberfolio::host
