// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal extractor. journal_extract.h has the reasoning.

#include "amberfolio/host/journal_extract.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

#include "libdeflate.h"

namespace amberfolio::host {
namespace {

/// How many bytes one row of samples occupies, before any predictor byte.
[[nodiscard]] std::uint64_t row_bytes(const journal_image& image) noexcept {
  const std::uint64_t bits =
      std::uint64_t{image.width} * image.components * image.bits_per_component;
  return (bits + 7U) / 8U;
}

/// How many bytes one whole sample occupies, rounded up — the PNG
/// predictor's `bpp`, which is what a Sub or a Paeth filter steps back
/// by. One, for anything narrower than a byte, which is what the PNG
/// specification says too.
[[nodiscard]] std::uint32_t sample_bytes(const journal_image& image) noexcept {
  const std::uint32_t bits =
      std::uint32_t{image.components} * image.bits_per_component;
  return std::max<std::uint32_t>(1U, bits / 8U);
}

[[nodiscard]] bool uses_png_predictor(const journal_image& image) noexcept {
  return image.predictor >= 10U && image.predictor <= 15U;
}

/// Whether this build can expand samples of this shape into gray.
[[nodiscard]] bool image_supported(const journal_image& image) noexcept {
  if (image.width == 0U || image.height == 0U) {
    return false;
  }
  const bool depth =
      image.bits_per_component == 1U || image.bits_per_component == 8U;
  const bool components = image.components == 1U || image.components == 3U;
  // A bilevel image has one component by definition; three components at
  // one bit each is not a thing this refuses politely, it is a thing that
  // does not exist.
  const bool sane = image.bits_per_component == 8U || image.components == 1U;
  // Predictor 0 and 1 both mean "none"; 10 to 15 are PNG's. TIFF's
  // predictor 2 lands in neither and is refused rather than guessed at.
  const bool predictor = image.predictor <= 1U || uses_png_predictor(image);
  return depth && components && sane && predictor;
}

/// The PNG paeth predictor (RFC 2083 §6.6), on bytes.
[[nodiscard]] std::uint8_t paeth(std::uint8_t a, std::uint8_t b,
                                 std::uint8_t c) noexcept {
  const int p = int{a} + int{b} - int{c};
  const int pa = std::abs(p - int{a});
  const int pb = std::abs(p - int{b});
  const int pc = std::abs(p - int{c});
  if (pa <= pb && pa <= pc) {
    return a;
  }
  return pb <= pc ? b : c;
}

/// Undo the PNG row filters in `raw`, which is `height` rows of one
/// filter byte plus `stride` bytes, leaving `height * stride` bytes in
/// `out`.
///
/// The five filter types, and a sixth answer for anything else: a row
/// whose filter byte is not 0–4 is not a PNG-predicted row, and guessing
/// which of the five was meant would be the one place in this file where
/// a wrong answer looked like a right one.
[[nodiscard]] bool unpredict(std::span<const std::uint8_t> raw,
                             std::uint64_t stride, std::uint32_t height,
                             std::uint32_t bpp,
                             std::vector<std::uint8_t>& out) {
  const auto width = static_cast<std::size_t>(stride);
  out.assign(static_cast<std::size_t>(height) * width, 0U);
  for (std::uint32_t y = 0; y < height; ++y) {
    const std::size_t source = static_cast<std::size_t>(y) * (width + 1U);
    const std::uint8_t kind = raw[source];
    const std::uint8_t* in = raw.data() + source + 1U;
    std::uint8_t* row = out.data() + (static_cast<std::size_t>(y) * width);
    const std::uint8_t* above =
        y == 0 ? nullptr : row - static_cast<std::ptrdiff_t>(width);
    for (std::size_t x = 0; x < width; ++x) {
      const std::uint8_t left = x >= bpp ? row[x - bpp] : 0U;
      const std::uint8_t up = above != nullptr ? above[x] : 0U;
      const std::uint8_t up_left =
          (above != nullptr && x >= bpp) ? above[x - bpp] : 0U;
      switch (kind) {
        case 0:
          row[x] = in[x];
          break;
        case 1:
          row[x] = static_cast<std::uint8_t>(in[x] + left);
          break;
        case 2:
          row[x] = static_cast<std::uint8_t>(in[x] + up);
          break;
        case 3:
          row[x] = static_cast<std::uint8_t>(
              in[x] + ((unsigned{left} + unsigned{up}) / 2U));
          break;
        case 4:
          row[x] = static_cast<std::uint8_t>(in[x] + paeth(left, up, up_left));
          break;
        default:
          return false;
      }
    }
  }
  return true;
}

/// Samples to gray, one row at a time.
void expand_row(std::span<const std::uint8_t> row, const journal_image& image,
                std::uint8_t* out) noexcept {
  if (image.bits_per_component == 1U) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const std::uint8_t byte = row[x / 8U];
      const bool bit = ((byte >> (7U - (x % 8U))) & 1U) != 0U;
      // A set bit is white in a plain bilevel image and ink in an
      // inverted one; the flag is a fact about the document's `/Decode`
      // and applying it is not optional (journal_facts.h).
      const bool white = image.inverted ? !bit : bit;
      out[x] = white ? 0xFFU : 0x00U;
    }
    return;
  }
  if (image.components == 1U) {
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const std::uint8_t v = row[x];
      out[x] = image.inverted ? static_cast<std::uint8_t>(0xFFU - v) : v;
    }
    return;
  }
  for (std::uint32_t x = 0; x < image.width; ++x) {
    const std::size_t at = static_cast<std::size_t>(x) * 3U;
    // Rec. 601 luma, in integers: 0.299 / 0.587 / 0.114 scaled by 1000.
    const unsigned luma = ((299U * row[at]) + (587U * row[at + 1U]) +
                           (114U * row[at + 2U]) + 500U) /
                          1000U;
    const std::uint8_t v = static_cast<std::uint8_t>(std::min(luma, 255U));
    out[x] = image.inverted ? static_cast<std::uint8_t>(0xFFU - v) : v;
  }
}

/// The stream's bytes, decoded to exactly `expected` bytes, or a reason.
[[nodiscard]] journal_trouble decode_stream(
    std::span<const std::uint8_t> encoded, journal_filter filter,
    std::size_t expected, std::vector<std::uint8_t>& out) {
  if (filter == journal_filter::none) {
    if (encoded.size() != expected) {
      return journal_trouble::stream_size_wrong;
    }
    out.assign(encoded.begin(), encoded.end());
    return journal_trouble::none;
  }
  if (filter != journal_filter::flate) {
    return journal_trouble::filter_unsupported;
  }

  out.assign(expected, 0U);
  libdeflate_decompressor* zlib = libdeflate_alloc_decompressor();
  if (zlib == nullptr) {
    return journal_trouble::too_large;
  }
  std::size_t produced = 0;
  const libdeflate_result result = libdeflate_zlib_decompress(
      zlib, encoded.data(), encoded.size(), out.data(), out.size(), &produced);
  libdeflate_free_decompressor(zlib);

  switch (result) {
    case LIBDEFLATE_SUCCESS:
      // A stream that decoded to fewer bytes than an image of this shape
      // is made of is a table row that disagrees with the document, which
      // is worth its own answer.
      return produced == expected ? journal_trouble::none
                                  : journal_trouble::stream_size_wrong;
    case LIBDEFLATE_INSUFFICIENT_SPACE:
      return journal_trouble::stream_size_wrong;
    default:
      return journal_trouble::stream_corrupt;
  }
}

}  // namespace

const char* journal_trouble_name(journal_trouble what) noexcept {
  switch (what) {
    case journal_trouble::none:
      return "nothing went wrong";
    case journal_trouble::unrecognized_edition:
      return "this is not a journal edition this build knows";
    case journal_trouble::no_such_entry:
      return "there is no such entry in this edition";
    case journal_trouble::stream_out_of_bounds:
      return "the entry's stream runs past the end of the document";
    case journal_trouble::filter_unsupported:
      return "the stream's filter is one this build does not decode";
    case journal_trouble::image_unsupported:
      return "the image's depth, components or predictor is one this build"
             " does not expand";
    case journal_trouble::stream_corrupt:
      return "the stream did not decode";
    case journal_trouble::stream_size_wrong:
      return "the stream decoded to the wrong number of bytes for an image"
             " of that shape";
    case journal_trouble::region_outside:
      return "the entry's region is not inside its image";
    case journal_trouble::no_engine:
      return "there is no OCR engine to read it with";
    case journal_trouble::engine_failed:
      return "the OCR engine did not read it";
    case journal_trouble::not_a_store:
      return "that is not a text store this build reads";
    case journal_trouble::too_large:
      return "that is larger than this build will read";
  }
  return "something unnamed went wrong";
}

journal_trouble decode_image(std::span<const std::uint8_t> document,
                             const journal_entry_fact& fact,
                             journal_bitmap& out) {
  out.pixels.clear();
  out.width = 0;
  out.height = 0;

  const journal_image& image = fact.image;
  // Decoded, not merely carried: this call answers samples, and a filter
  // that goes through untouched has none to answer with (#212).
  if (!journal_filter_decoded(image.filter)) {
    return journal_trouble::filter_unsupported;
  }
  if (!image_supported(image)) {
    return journal_trouble::image_unsupported;
  }
  if (fact.offset > document.size() ||
      fact.length > document.size() - fact.offset) {
    return journal_trouble::stream_out_of_bounds;
  }

  const std::uint64_t stride = row_bytes(image);
  const bool predicted = uses_png_predictor(image);
  const std::uint64_t decoded_bytes =
      std::uint64_t{image.height} * (stride + (predicted ? 1U : 0U));
  // An image the fact table describes as bigger than this build will hold
  // is refused before a byte is allocated for it, rather than after.
  if (decoded_bytes > std::uint64_t{256U} * 1024U * 1024U) {
    return journal_trouble::too_large;
  }

  std::vector<std::uint8_t> decoded;
  if (const journal_trouble why = decode_stream(
          document.subspan(static_cast<std::size_t>(fact.offset),
                           static_cast<std::size_t>(fact.length)),
          image.filter, static_cast<std::size_t>(decoded_bytes), decoded);
      why != journal_trouble::none) {
    return why;
  }

  std::vector<std::uint8_t> samples;
  if (predicted) {
    if (!unpredict(decoded, stride, image.height, sample_bytes(image),
                   samples)) {
      return journal_trouble::stream_corrupt;
    }
  } else {
    samples = std::move(decoded);
  }

  out.pixels.assign(static_cast<std::size_t>(image.width) * image.height, 0U);
  out.width = image.width;
  out.height = image.height;
  for (std::uint32_t y = 0; y < image.height; ++y) {
    const std::size_t at =
        static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    expand_row(std::span<const std::uint8_t>(samples).subspan(
                   at, static_cast<std::size_t>(stride)),
               image,
               out.pixels.data() + (static_cast<std::size_t>(y) * image.width));
  }
  return journal_trouble::none;
}

journal_trouble crop(const journal_bitmap& image, const journal_region& region,
                     journal_bitmap& out) {
  out.pixels.clear();
  out.width = 0;
  out.height = 0;
  if (region.width == 0U || region.height == 0U) {
    return journal_trouble::region_outside;
  }
  if (region.left > image.width || region.top > image.height ||
      region.width > image.width - region.left ||
      region.height > image.height - region.top) {
    return journal_trouble::region_outside;
  }

  out.pixels.assign(static_cast<std::size_t>(region.width) * region.height, 0U);
  out.width = region.width;
  out.height = region.height;
  for (std::uint32_t y = 0; y < region.height; ++y) {
    const std::size_t from =
        ((static_cast<std::size_t>(region.top) + y) * image.width) +
        region.left;
    const std::size_t to = static_cast<std::size_t>(y) * region.width;
    std::copy_n(image.pixels.begin() + static_cast<std::ptrdiff_t>(from),
                region.width,
                out.pixels.begin() + static_cast<std::ptrdiff_t>(to));
  }
  return journal_trouble::none;
}

journal_trouble extract_entry(std::span<const std::uint8_t> document,
                              const journal_entry_fact& fact,
                              journal_bitmap& out) {
  journal_bitmap page;
  if (const journal_trouble why = decode_image(document, fact, page);
      why != journal_trouble::none) {
    out.pixels.clear();
    out.width = 0;
    out.height = 0;
    return why;
  }
  return crop(page, fact.region, out);
}

journal_trouble extract_scan(std::span<const std::uint8_t> document,
                             const journal_entry_fact& fact,
                             journal_scan& out) {
  out = journal_scan{};

  if (journal_filter_decoded(fact.image.filter)) {
    out.encoding = journal_encoding::gray;
    return extract_entry(document, fact, out.gray);
  }
  if (!journal_filter_supported(fact.image.filter)) {
    return journal_trouble::filter_unsupported;
  }

  // The passthrough (#212). Everything this does is a bounds check and a
  // copy: the stream is not decoded, not inspected, and not believed —
  // an engine that is handed something that is not a picture says so,
  // which is the same answer it gives for a picture it cannot read.
  //
  // What *is* checked is what this build can check without looking
  // inside: that the offset and the length name bytes of this document,
  // and that the region the table gives is inside the shape the table
  // gives. The second is the check the crop used to make for free, and
  // losing it silently would be the one real cost of not decoding — a
  // rectangle off the edge of the page would reach the engine as a
  // filter that quietly matched no words, which reads exactly like an
  // engine that could not read the page.
  if (fact.offset > document.size() ||
      fact.length > document.size() - fact.offset) {
    return journal_trouble::stream_out_of_bounds;
  }
  if (fact.length == 0U) {
    return journal_trouble::stream_size_wrong;
  }
  const journal_image& image = fact.image;
  const journal_region& region = fact.region;
  if (image.width == 0U || image.height == 0U || region.width == 0U ||
      region.height == 0U || region.left > image.width ||
      region.top > image.height || region.width > image.width - region.left ||
      region.height > image.height - region.top) {
    return journal_trouble::region_outside;
  }

  const auto at = static_cast<std::size_t>(fact.offset);
  const auto length = static_cast<std::size_t>(fact.length);
  out.encoding = journal_encoding::jpeg;
  out.encoded.assign(
      document.begin() + static_cast<std::ptrdiff_t>(at),
      document.begin() + static_cast<std::ptrdiff_t>(at + length));
  out.region = region;
  return journal_trouble::none;
}

}  // namespace amberfolio::host
