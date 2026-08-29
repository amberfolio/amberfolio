// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal probe. journal_probe.h has the reasoning; what is here is
// the document generator, and it is written so that the *only* facts the
// table below carries are ones this file measured while writing the file.

#include "amberfolio/host/journal_probe.h"

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
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

// --- what the two entries are ------------------------------------------

constexpr journal_image probe_image_one{
    .width = 64,
    .height = 32,
    .bits_per_component = 8,
    .components = 1,
    .predictor = 1,
    .filter = journal_filter::flate,
    .inverted = false,
};
constexpr journal_region probe_region_one{
    .left = 8, .top = 4, .width = 32, .height = 16};

constexpr journal_image probe_image_two{
    .width = 48,
    .height = 24,
    .bits_per_component = 1,
    .components = 1,
    .predictor = 15,
    .filter = journal_filter::flate,
    .inverted = true,
};
constexpr journal_region probe_region_two{
    .left = 16, .top = 8, .width = 24, .height = 12};

/// The gray of a pixel of entry one's page, and whether a pixel of entry
/// two's is ink. Two functions, and the document *and* the expectation
/// are both generated from them — which is why an extraction that is
/// wrong anywhere cannot agree with the expectation by accident.
[[nodiscard]] std::uint8_t probe_gray(std::uint32_t x, std::uint32_t y) {
  return static_cast<std::uint8_t>(((x * 4U) + (y * 3U)) & 0xFFU);
}

[[nodiscard]] bool probe_ink(std::uint32_t x, std::uint32_t y) {
  return ((x + y) % 5U) == 0U;
}

// --- deflate, the one form of it this tree can write -------------------

/// Adler-32 (RFC 1950 §9), the zlib stream's trailer.
[[nodiscard]] std::uint32_t adler32(std::span<const std::uint8_t> data) {
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (const std::uint8_t byte : data) {
    a = (a + byte) % 65521U;
    b = (b + a) % 65521U;
  }
  return (b << 16U) | a;
}

/// `data` as a zlib stream of stored (uncompressed) deflate blocks.
///
/// A compressor is the one thing this project has no reason to own, and
/// stored blocks are a legal deflate stream every inflater reads — RFC
/// 1951 §3.2.4. The header is 0x78 0x01: window 32 KiB, no dictionary,
/// and a check value that makes the two bytes a multiple of 31.
[[nodiscard]] std::vector<std::uint8_t> zlib_stored(
    std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out;
  out.push_back(0x78);
  out.push_back(0x01);
  std::size_t at = 0;
  do {
    const std::size_t take = std::min<std::size_t>(data.size() - at, 0xFFFFU);
    const bool last = (at + take) >= data.size();
    out.push_back(last ? 0x01U : 0x00U);
    out.push_back(static_cast<std::uint8_t>(take & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((take >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>(~take & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((~take >> 8U) & 0xFFU));
    out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at),
               data.begin() + static_cast<std::ptrdiff_t>(at + take));
    at += take;
  } while (at < data.size());

  const std::uint32_t sum = adler32(data);
  out.push_back(static_cast<std::uint8_t>((sum >> 24U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((sum >> 16U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>((sum >> 8U) & 0xFFU));
  out.push_back(static_cast<std::uint8_t>(sum & 0xFFU));
  return out;
}

// --- the two images, as the samples a PDF stream carries ---------------

/// Entry one: one byte a pixel, no predictor.
[[nodiscard]] std::vector<std::uint8_t> probe_samples_one() {
  std::vector<std::uint8_t> out;
  out.reserve(static_cast<std::size_t>(probe_image_one.width) *
              probe_image_one.height);
  for (std::uint32_t y = 0; y < probe_image_one.height; ++y) {
    for (std::uint32_t x = 0; x < probe_image_one.width; ++x) {
      out.push_back(probe_gray(x, y));
    }
  }
  return out;
}

/// The PNG paeth predictor (RFC 2083 §6.6), the encoding side.
///
/// A second implementation of `journal_extract.cpp`'s, deliberately: a
/// generator that shared the decoder's arithmetic could not disagree with
/// it, and disagreeing with it is the whole of what the check is for.
[[nodiscard]] unsigned paeth_of(unsigned left, unsigned up, unsigned up_left) {
  const int a = static_cast<int>(left);
  const int b = static_cast<int>(up);
  const int c = static_cast<int>(up_left);
  const int p = a + b - c;
  const int pa = p > a ? p - a : a - p;
  const int pb = p > b ? p - b : b - p;
  const int pc = p > c ? p - c : c - p;
  if (pa <= pb && pa <= pc) {
    return left;
  }
  return pb <= pc ? up : up_left;
}

/// Entry two: one bit a pixel, PNG-predicted, a different row filter
/// every row so that all five of them are exercised.
///
/// A set bit is paper and a clear bit is ink here, because the image is
/// `/Decode [1 0]` — inverted — and the whole point of that flag is that
/// the extractor has to put it back the other way.
[[nodiscard]] std::vector<std::uint8_t> probe_samples_two() {
  const std::size_t stride = (probe_image_two.width + 7U) / 8U;
  std::vector<std::uint8_t> raw(stride * probe_image_two.height, 0U);
  for (std::uint32_t y = 0; y < probe_image_two.height; ++y) {
    for (std::uint32_t x = 0; x < probe_image_two.width; ++x) {
      if (!probe_ink(x, y)) {
        continue;
      }
      // Ink, and the image is inverted, so ink is a *set* bit.
      raw[(static_cast<std::size_t>(y) * stride) + (x / 8U)] |=
          static_cast<std::uint8_t>(1U << (7U - (x % 8U)));
    }
  }

  std::vector<std::uint8_t> out;
  out.reserve((stride + 1U) * probe_image_two.height);
  for (std::uint32_t y = 0; y < probe_image_two.height; ++y) {
    const auto kind = static_cast<std::uint8_t>(y % 5U);
    out.push_back(kind);
    for (std::size_t x = 0; x < stride; ++x) {
      const std::size_t at = (static_cast<std::size_t>(y) * stride) + x;
      const unsigned here = raw[at];
      const unsigned left = x >= 1U ? raw[at - 1U] : 0U;
      const unsigned up = y >= 1U ? raw[at - stride] : 0U;
      const unsigned up_left =
          (y >= 1U && x >= 1U) ? raw[at - stride - 1U] : 0U;
      unsigned encoded = here;
      switch (kind) {
        case 1:
          encoded = here - left;
          break;
        case 2:
          encoded = here - up;
          break;
        case 3:
          encoded = here - ((left + up) / 2U);
          break;
        case 4:
          encoded = here - paeth_of(left, up, up_left);
          break;
        default:
          break;
      }
      out.push_back(static_cast<std::uint8_t>(encoded & 0xFFU));
    }
  }
  return out;
}

// --- the document ------------------------------------------------------

void append(std::vector<std::uint8_t>& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}

void append_number(std::vector<std::uint8_t>& out, std::uint64_t value) {
  append(out, std::to_string(value));
}

/// Ten digits, zero-padded, as a cross-reference table entry wants.
[[nodiscard]] std::string xref_offset(std::uint64_t value) {
  const std::string text = std::to_string(value);
  const auto pad =
      static_cast<std::size_t>(10U - std::min<std::size_t>(10U, text.size()));
  return std::string(pad, '0') + text;
}

struct probe_document {
  std::vector<std::uint8_t> bytes;
  std::array<journal_entry_fact, journal_probe_entries> facts{};
};

/// Build the document, recording each stream's offset as it is written.
///
/// The recording is the whole method: the fact table is not written by
/// hand against a file somebody hopes is still the same, it is what the
/// generator measured while generating. That is exactly what gathering a
/// *real* edition's facts looks like too, minus the generator.
[[nodiscard]] probe_document build_probe() {
  probe_document doc;
  std::vector<std::uint8_t>& out = doc.bytes;
  std::array<std::uint64_t, 6> object_at{};

  append(out, "%PDF-1.4\n");
  // A comment line of high bytes, which every PDF writer emits to mark
  // the file as binary. It also means this document is not mistakable for
  // text by anything that sniffs.
  out.insert(out.end(), {'%', 0xE2, 0xE3, 0xCF, 0xD3, '\n'});

  object_at[1] = out.size();
  append(out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

  object_at[2] = out.size();
  append(out, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

  object_at[3] = out.size();
  append(out,
         "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200]"
         " /Resources << /XObject << /Im1 4 0 R /Im2 5 0 R >> >> >>\n"
         "endobj\n");

  const std::vector<std::uint8_t> stream_one = zlib_stored(probe_samples_one());
  object_at[4] = out.size();
  append(out,
         "4 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 32"
         " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode"
         " /Length ");
  append_number(out, stream_one.size());
  append(out, " >>\nstream\n");
  const std::uint64_t offset_one = out.size();
  out.insert(out.end(), stream_one.begin(), stream_one.end());
  append(out, "\nendstream\nendobj\n");

  const std::vector<std::uint8_t> stream_two = zlib_stored(probe_samples_two());
  object_at[5] = out.size();
  append(out,
         "5 0 obj\n<< /Type /XObject /Subtype /Image /Width 48 /Height 24"
         " /ColorSpace /DeviceGray /BitsPerComponent 1 /Decode [1 0]"
         " /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 1"
         " /BitsPerComponent 1 /Columns 48 >> /Length ");
  append_number(out, stream_two.size());
  append(out, " >>\nstream\n");
  const std::uint64_t offset_two = out.size();
  out.insert(out.end(), stream_two.begin(), stream_two.end());
  append(out, "\nendstream\nendobj\n");

  const std::uint64_t xref_at = out.size();
  append(out, "xref\n0 6\n0000000000 65535 f \n");
  for (std::size_t object = 1; object < object_at.size(); ++object) {
    append(out, xref_offset(object_at[object]));
    append(out, " 00000 n \n");
  }
  append(out, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n");
  append_number(out, xref_at);
  append(out, "\n%%EOF\n");

  doc.facts[0] = journal_entry_fact{
      .number = 1,
      .page = 1,
      .offset = offset_one,
      .length = static_cast<std::uint32_t>(stream_one.size()),
      .image = probe_image_one,
      .region = probe_region_one};
  doc.facts[1] = journal_entry_fact{
      .number = 2,
      .page = 1,
      .offset = offset_two,
      .length = static_cast<std::uint32_t>(stream_two.size()),
      .image = probe_image_two,
      .region = probe_region_two};
  return doc;
}

/// Built once, on first use, and never freed — the same shape the web
/// host's demo program has, and for the same reason.
const probe_document& probe() {
  static const probe_document one = build_probe();
  return one;
}

const std::string& probe_fingerprint() {
  static const std::string hex = [] {
    std::array<char, sha256_digest::text_length + 1> text{};
    const std::size_t written = format_hex(sha256(probe().bytes), text);
    return std::string(text.data(), written);
  }();
  return hex;
}

const std::array<journal_edition, 1>& probe_editions() {
  static const std::array<journal_edition, 1> table{{
      {.fingerprint = probe_fingerprint(),
       .name = "Amber Folio journal probe (synthetic)",
       .entries = probe().facts},
  }};
  return table;
}

constexpr std::array<std::string_view, journal_probe_entries> probe_words{
    "AMBER FOLIO PROBE ENTRY 1",
    "AMBER FOLIO PROBE ENTRY 2",
};

}  // namespace

const std::vector<std::uint8_t>& journal_probe_pdf() { return probe().bytes; }

std::span<const journal_edition> journal_probe_table() {
  return probe_editions();
}

journal_bitmap journal_probe_expected(std::size_t index) {
  journal_bitmap out;
  if (index >= journal_probe_entries) {
    return out;
  }
  const journal_region& region = probe().facts[index].region;
  out.width = region.width;
  out.height = region.height;
  out.pixels.reserve(static_cast<std::size_t>(region.width) * region.height);
  for (std::uint32_t y = 0; y < region.height; ++y) {
    for (std::uint32_t x = 0; x < region.width; ++x) {
      const std::uint32_t px = region.left + x;
      const std::uint32_t py = region.top + y;
      // Entry two's page is inverted, so what the extractor must produce
      // is ink black and paper white — the same way round as entry one's,
      // which is the whole reason the flag exists.
      out.pixels.push_back(index == 0 ? probe_gray(px, py)
                                      : (probe_ink(px, py) ? 0x00U : 0xFFU));
    }
  }
  return out;
}

std::string_view journal_probe_text(std::size_t index) {
  return index < probe_words.size() ? probe_words[index] : std::string_view{};
}

journal_probe_ocr::journal_probe_ocr() {
  expected_.reserve(journal_probe_entries);
  for (std::size_t index = 0; index < journal_probe_entries; ++index) {
    expected_.push_back(journal_probe_expected(index));
  }
}

bool journal_probe_ocr::recognize(const journal_bitmap& page,
                                  std::string& out) {
  for (std::size_t index = 0; index < expected_.size(); ++index) {
    const journal_bitmap& want = expected_[index];
    if (page.width == want.width && page.height == want.height &&
        page.pixels == want.pixels) {
      out.assign(journal_probe_text(index));
      return true;
    }
  }
  // Not a stub that says yes to anything: an image that is not one of the
  // two this fixture knows is an image the pipeline got wrong somewhere,
  // and the honest answer is that nothing was read (journal_probe.h).
  out.clear();
  return false;
}

std::string_view journal_probe_ocr::engine() const {
  return "amberfolio journal probe fixture";
}

}  // namespace amberfolio::host
