// SPDX-License-Identifier: AGPL-3.0-only
//
// The entries that are pictures. journal_picture.h has the reasoning and
// docs/journal.md §11 has the measurements every number here came from.

#include "amberfolio/host/journal_picture.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/machine/journal.h"

namespace amberfolio::host {
namespace {

using machine::journal_art_height;
using machine::journal_art_ink;
using machine::journal_art_levels;
using machine::journal_art_paper;
using machine::journal_art_pixels_per_byte;
using machine::journal_art_stride;
using machine::journal_art_width;

/// The reduction is integer arithmetic all the way down, and that is a
/// decision rather than an aesthetic. A store's `fingerprint()` is a
/// thing a maintainer reports (`docs/journal.md` §8), and a reduction
/// that drifted between two builds — or two optimizers — would make
/// that number mean nothing.
///
/// What it does **not** buy is the same fingerprint on two different
/// hosts, and that is measured rather than hoped for: the decode in
/// front of this is each image library's own, so one page turned into
/// grey by two of them is not the same samples, and the pictures that
/// come out differ in about a third of a percent of their pixels, by
/// one level. `docs/journal.md` §11.2 has the number and which two.
[[nodiscard]] constexpr std::uint32_t scaled(std::uint32_t index,
                                             std::uint32_t of,
                                             std::uint32_t to) noexcept {
  return static_cast<std::uint32_t>((static_cast<std::uint64_t>(index) * of) /
                                    to);
}

/// A page whose darkest and lightest are this close is a page with
/// nothing on it: reduced from a rectangle that was measured wrong, or
/// from a genuinely blank corner. It comes back as paper rather than as
/// a field of noise stretched to fill four levels.
constexpr std::uint32_t least_contrast = 16;

/// Where the two ends of the range are taken, as fractions of the
/// reduced page's own tones: the darkest half-percent and the lightest
/// tenth. Percentiles rather than the extremes, so one speck of scanner
/// dirt cannot set the black point for a whole page and one blown
/// highlight cannot set the white point.
///
/// **The lightest tenth rather than the paper's mode**, which is what
/// this took first and what a real scan makes look right: the paper *is*
/// the mode of a drawing, so the mode is a good white point for every
/// page this will ever see. It is a bad one for a page that is half ink,
/// and worse for a smooth gradient, where the mode is an arbitrary
/// member of a flat histogram — both of which come out as a page with no
/// range at all, drawn as blank. A percentile cannot do that, and on the
/// real pages the two agree.
constexpr std::size_t ink_in_parts = 200;
constexpr std::size_t paper_in_parts = 10;

/// A whole byte of paper — the paper level in each of its four pairs.
/// What a row's tail padding is filled with, and what a picture of a
/// blank page comes out as.
constexpr std::uint8_t paper_byte =
    static_cast<std::uint8_t>(journal_art_paper * 0x55U);
static_assert(journal_art_levels == 4,
              "the packing is two bits a pixel, and so is the fill above");

constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] int base64_value(char c) noexcept {
  const std::size_t at = base64_alphabet.find(c);
  return at == std::string_view::npos ? -1 : static_cast<int>(at);
}

}  // namespace

journal_picture_shape fit_picture(std::uint32_t width,
                                  std::uint32_t height) noexcept {
  if (width == 0 || height == 0) {
    return {};
  }
  // Widest first, and shrink to the box's depth if that overflows it.
  // The aspect is folded in here and nowhere else: a picture is `tall`
  // fifths as wide, in *pixels*, as its printed proportions ask for,
  // because a pixel of this mode is that much taller than it is wide.
  constexpr std::uint64_t tall = machine::journal_art_aspect_tall;
  constexpr std::uint64_t wide = machine::journal_art_aspect_wide;
  std::uint64_t out_width = journal_art_width;
  std::uint64_t out_height =
      (((static_cast<std::uint64_t>(height) * out_width * wide) /
        static_cast<std::uint64_t>(width)) +
       (tall / 2U)) /
      tall;
  if (out_height > journal_art_height) {
    out_height = journal_art_height;
    out_width = (((static_cast<std::uint64_t>(width) * out_height * tall) /
                  static_cast<std::uint64_t>(height)) +
                 (wide / 2U)) /
                wide;
    out_width = std::min<std::uint64_t>(out_width, journal_art_width);
  }
  return {
      .width =
          static_cast<std::uint16_t>(std::max<std::uint64_t>(1U, out_width)),
      .height =
          static_cast<std::uint16_t>(std::max<std::uint64_t>(1U, out_height))};
}

std::uint8_t journal_picture::level_at(std::uint32_t x,
                                       std::uint32_t y) const noexcept {
  if (x >= width || y >= height) {
    return journal_art_paper;
  }
  const std::size_t at = (static_cast<std::size_t>(y) * stride()) +
                         (x / journal_art_pixels_per_byte);
  if (at >= levels.size()) {
    return journal_art_paper;
  }
  const unsigned shift = 6U - (2U * (x % journal_art_pixels_per_byte));
  return static_cast<std::uint8_t>((levels[at] >> shift) & 0x3U);
}

bool reduce_picture(const journal_bitmap& page, journal_picture& out) {
  out.width = 0;
  out.height = 0;
  out.levels.clear();
  if (page.width == 0 || page.height == 0 ||
      page.pixels.size() < static_cast<std::size_t>(page.width) * page.height) {
    return false;
  }

  const journal_picture_shape shape = fit_picture(page.width, page.height);
  if (shape.width == 0 || shape.height == 0) {
    return false;
  }

  // 1. The box filter. Every source sample is counted exactly once,
  //    which is what keeps a hairline drawn at scan resolution from
  //    falling between two sample points of a nearest reduction.
  std::vector<std::uint8_t> tones(static_cast<std::size_t>(shape.width) *
                                  shape.height);
  for (std::uint32_t ty = 0; ty < shape.height; ++ty) {
    const std::uint32_t y0 = scaled(ty, page.height, shape.height);
    const std::uint32_t y1 =
        std::max(y0 + 1U, scaled(ty + 1U, page.height, shape.height));
    for (std::uint32_t tx = 0; tx < shape.width; ++tx) {
      const std::uint32_t x0 = scaled(tx, page.width, shape.width);
      const std::uint32_t x1 =
          std::max(x0 + 1U, scaled(tx + 1U, page.width, shape.width));
      std::uint32_t sum = 0;
      std::uint32_t count = 0;
      for (std::uint32_t y = y0; y < y1 && y < page.height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * page.width;
        for (std::uint32_t x = x0; x < x1 && x < page.width; ++x) {
          sum += page.pixels[row + x];
          ++count;
        }
      }
      tones[(static_cast<std::size_t>(ty) * shape.width) + tx] =
          static_cast<std::uint8_t>(count == 0 ? 255U : sum / count);
    }
  }

  // 2. The two ends of the range, off the reduced page's own tones. The
  //    scan's paper is a cream and its ink never reaches black, so a
  //    fixed threshold either loses the lightest lines or fills the
  //    page. Both ends are percentiles rather than extremes, and why
  //    the light one is not the paper's mode is above.
  std::array<std::uint32_t, 256> histogram{};
  for (const std::uint8_t tone : tones) {
    ++histogram[tone];
  }
  const std::size_t want =
      std::max<std::size_t>(1U, tones.size() / ink_in_parts);
  std::uint32_t ink = 0;
  std::size_t seen = 0;
  for (std::size_t value = 0; value < histogram.size(); ++value) {
    seen += histogram[value];
    if (seen >= want) {
      ink = static_cast<std::uint32_t>(value);
      break;
    }
  }
  const std::size_t light =
      tones.size() - std::min(tones.size(), tones.size() / paper_in_parts);
  std::uint32_t paper = 255;
  seen = 0;
  for (std::size_t value = 0; value < histogram.size(); ++value) {
    seen += histogram[value];
    if (seen >= light) {
      paper = static_cast<std::uint32_t>(value);
      break;
    }
  }

  // 3. Nearest, no dither. Measured: every dithered candidate turned the
  //    paper into a mesh at this size (`docs/journal.md` §11).
  out.width = shape.width;
  out.height = shape.height;
  const std::size_t stride = journal_art_stride(shape.width);
  // **Filled with paper first**, which is what makes a row's tail
  // padding paper rather than ink. A width that is not a multiple of
  // four leaves up to three pixels over at the end of every row, and
  // zero is the *ink* level — so a reader walking the packed bytes
  // rather than asking `level_at` would draw a bright edge down the
  // right of nine of the fourteen pictures in the one tabled edition.
  out.levels.assign(stride * shape.height, paper_byte);
  const bool blank = paper <= ink || (paper - ink) < least_contrast;
  constexpr std::uint32_t steps = journal_art_levels - 1U;
  for (std::uint32_t y = 0; y < shape.height; ++y) {
    for (std::uint32_t x = 0; x < shape.width; ++x) {
      std::uint8_t level = journal_art_paper;
      if (!blank) {
        const std::uint32_t tone =
            tones[(static_cast<std::size_t>(y) * shape.width) + x];
        const std::uint32_t span = paper - ink;
        const std::uint32_t above =
            tone <= ink ? 0U : std::min(tone - ink, span);
        level = static_cast<std::uint8_t>(((above * steps * 2U) + span) /
                                          (span * 2U));
      }
      const std::size_t at = (static_cast<std::size_t>(y) * stride) +
                             (x / journal_art_pixels_per_byte);
      const unsigned shift = 6U - (2U * (x % journal_art_pixels_per_byte));
      out.levels[at] = static_cast<std::uint8_t>(
          (out.levels[at] & ~static_cast<std::uint8_t>(0x3U << shift)) |
          static_cast<std::uint8_t>(level << shift));
    }
  }
  static_assert(journal_art_ink == 0,
                "the darkest tone is the first level, which is what the "
                "arithmetic above produces");
  return true;
}

journal_trouble reduce_entry_pictures(std::span<const std::uint8_t> document,
                                      const journal_entry_fact& fact,
                                      journal_page_decoder* decoder,
                                      std::vector<journal_picture>& out) {
  out.clear();
  journal_trouble first = journal_trouble::none;
  std::uint8_t nth = 0;
  journal_bitmap page;
  journal_bitmap piece;
  for (const journal_fragment& art : fact.art) {
    journal_trouble why = journal_trouble::none;
    if (journal_filter_decoded(art.image.filter)) {
      why = extract_fragment(document, art, piece);
    } else if (!journal_filter_supported(art.image.filter) ||
               decoder == nullptr) {
      // One answer for two states, because they are the same state as
      // far as a picture is concerned: a filter nobody has written code
      // for, and one the pipeline can carry to an OCR engine but cannot
      // turn into samples on its own (#212). A picture has no engine to
      // hand that work to, so without a decoder there is nothing to
      // reduce — said plainly rather than answered with a blank page.
      why = journal_trouble::filter_unsupported;
    } else if (art.offset + art.length > document.size()) {
      why = journal_trouble::stream_out_of_bounds;
    } else if (!decoder->decode(
                   document.subspan(static_cast<std::size_t>(art.offset),
                                    art.length),
                   page)) {
      why = journal_trouble::stream_corrupt;
    } else if (page.width != art.image.width ||
               page.height != art.image.height) {
      // The decoder read *something* and it is not the page the table
      // describes. The same check the decoded path gets for free from
      // its own size arithmetic, and the same failure a table row that
      // is off by a page produces.
      why = journal_trouble::stream_size_wrong;
    } else {
      why = crop(page, art.region, piece);
    }
    if (why == journal_trouble::none) {
      journal_picture made;
      made.what = {.kind = fact.kind, .number = fact.number};
      made.nth = nth;
      if (reduce_picture(piece, made)) {
        out.push_back(std::move(made));
      } else {
        why = journal_trouble::region_outside;
      }
    }
    if (why != journal_trouble::none && first == journal_trouble::none) {
      first = why;
    }
    ++nth;
  }
  return first;
}

std::string encode_base64(std::span<const std::uint8_t> bytes) {
  std::string out;
  out.reserve(((bytes.size() + 2U) / 3U) * 4U);
  std::size_t at = 0;
  while (at + 3U <= bytes.size()) {
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(bytes[at]) << 16U) |
        (static_cast<std::uint32_t>(bytes[at + 1]) << 8U) |
        static_cast<std::uint32_t>(bytes[at + 2]);
    out.push_back(base64_alphabet[(triple >> 18U) & 0x3FU]);
    out.push_back(base64_alphabet[(triple >> 12U) & 0x3FU]);
    out.push_back(base64_alphabet[(triple >> 6U) & 0x3FU]);
    out.push_back(base64_alphabet[triple & 0x3FU]);
    at += 3U;
  }
  const std::size_t left = bytes.size() - at;
  if (left == 1U) {
    const std::uint32_t triple = static_cast<std::uint32_t>(bytes[at]) << 16U;
    out.push_back(base64_alphabet[(triple >> 18U) & 0x3FU]);
    out.push_back(base64_alphabet[(triple >> 12U) & 0x3FU]);
    out.push_back('=');
    out.push_back('=');
  } else if (left == 2U) {
    const std::uint32_t triple =
        (static_cast<std::uint32_t>(bytes[at]) << 16U) |
        (static_cast<std::uint32_t>(bytes[at + 1]) << 8U);
    out.push_back(base64_alphabet[(triple >> 18U) & 0x3FU]);
    out.push_back(base64_alphabet[(triple >> 12U) & 0x3FU]);
    out.push_back(base64_alphabet[(triple >> 6U) & 0x3FU]);
    out.push_back('=');
  }
  return out;
}

bool decode_base64(std::string_view text, std::vector<std::uint8_t>& out) {
  out.clear();
  if ((text.size() % 4U) != 0U) {
    return false;
  }
  out.reserve((text.size() / 4U) * 3U);
  for (std::size_t at = 0; at < text.size(); at += 4U) {
    std::array<int, 4> quad{};
    std::size_t taken = 4;
    for (std::size_t i = 0; i < 4U; ++i) {
      const char c = text[at + i];
      if (c == '=') {
        // Padding, and only in the last group and only at its end —
        // anything else is a store somebody has edited into something
        // this cannot read, which is `not_a_store` and not a guess.
        if (at + 4U != text.size() || i < 2U) {
          return false;
        }
        taken = std::min(taken, i);
        quad[i] = 0;
        continue;
      }
      if (taken != 4U) {
        return false;
      }
      const int value = base64_value(c);
      if (value < 0) {
        return false;
      }
      quad[i] = value;
    }
    const std::uint32_t triple = (static_cast<std::uint32_t>(quad[0]) << 18U) |
                                 (static_cast<std::uint32_t>(quad[1]) << 12U) |
                                 (static_cast<std::uint32_t>(quad[2]) << 6U) |
                                 static_cast<std::uint32_t>(quad[3]);
    out.push_back(static_cast<std::uint8_t>((triple >> 16U) & 0xFFU));
    if (taken > 2U) {
      out.push_back(static_cast<std::uint8_t>((triple >> 8U) & 0xFFU));
    }
    if (taken > 3U) {
      out.push_back(static_cast<std::uint8_t>(triple & 0xFFU));
    }
  }
  return true;
}

}  // namespace amberfolio::host
