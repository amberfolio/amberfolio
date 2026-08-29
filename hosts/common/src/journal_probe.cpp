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

// --- what the three entries are ----------------------------------------

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

/// Entry three: the page this build does not decode (#212).
///
/// Whole 8x8 blocks in both directions, because the encoder below writes
/// one block per MCU and a partial block would mean padding rules that
/// buy this file nothing. The region is inset on all four sides, so a
/// consumer that ignored it and read the whole page would be visibly
/// answering a different question.
constexpr journal_image probe_image_three{
    .width = 64,
    .height = 32,
    .bits_per_component = 8,
    .components = 1,
    .predictor = 1,
    .filter = journal_filter::dct,
    .inverted = false,
};
constexpr journal_region probe_region_three{
    .left = 8, .top = 8, .width = 48, .height = 8};
/// ...and the second piece of it, below the first with a gap between, so
/// an engine that read one rectangle where two were asked for gives a
/// visibly different answer (M5-E3b, #214).
constexpr journal_region probe_region_three_second{
    .left = 8, .top = 20, .width = 48, .height = 8};

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

// --- the first two images, as the samples a PDF stream carries ---------

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

// --- a baseline JPEG, for the page this build does not decode ----------
//
// ITU-T T.81. Everything here is the smallest legal form of each piece,
// because the only thing the probe needs of this stream is that it *is*
// one: nothing in this build decodes it (#212), and the fixture engine
// compares it byte for byte against what this produced.

/// The one gray every pixel of the page is, and what the encoder has to
/// write to say so.
///
/// A flat quantization table of eight, and a level-shifted sample of 32,
/// make the quantized DC coefficient of every block exactly 32: the 2-D
/// DCT of a constant block puts eight times the level-shifted value in
/// the DC coefficient and nothing anywhere else, and eight divided by
/// eight is one. So the whole image is one DC value and a run of zero
/// differences, which is why there is no transform in this file.
constexpr std::uint8_t probe_jpeg_gray = 160;
constexpr std::uint8_t probe_jpeg_quant = 8;

/// ...which is that one number, derived rather than asserted: level-shift
/// the sample, take the eight the DCT of a constant block multiplies it
/// by, and divide by the quantizer.
constexpr int probe_jpeg_dc =
    ((int{probe_jpeg_gray} - 128) * 8) / int{probe_jpeg_quant};

/// The category it falls in, and how many bits of it are written after
/// the symbol: 32 to 63 is category 6 (T.81 Table F.1).
constexpr std::uint8_t probe_jpeg_dc_category = 6;
static_assert(probe_jpeg_dc >= 32 && probe_jpeg_dc <= 63,
              "the one Huffman symbol this encoder writes for a DC value"
              " has to be the category that value is actually in");

/// Bits out, most significant first, with T.81 §B.1.1.5's byte stuffing:
/// a 0xFF in entropy-coded data is followed by a zero, so that no marker
/// can be spelled by accident.
class jpeg_bits {
 public:
  void put(std::uint32_t value, unsigned width) {
    for (unsigned i = width; i > 0; --i) {
      const bool bit = ((value >> (i - 1U)) & 1U) != 0U;
      byte_ = static_cast<std::uint8_t>((byte_ << 1U) | (bit ? 1U : 0U));
      if (++filled_ == 8U) {
        flush_byte();
      }
    }
  }

  /// Pad the last byte with ones, which is what T.81 §F.1.2.3 says to do
  /// and is why a decoder cannot mistake the padding for another code.
  [[nodiscard]] std::vector<std::uint8_t> finish() {
    if (filled_ != 0U) {
      put(0xFFU, 8U - filled_);
    }
    return std::move(out_);
  }

 private:
  void flush_byte() {
    out_.push_back(byte_);
    if (byte_ == 0xFFU) {
      out_.push_back(0x00U);
    }
    byte_ = 0;
    filled_ = 0;
  }

  std::vector<std::uint8_t> out_;
  std::uint8_t byte_{0};
  unsigned filled_{0};
};

void jpeg_marker(std::vector<std::uint8_t>& out, std::uint8_t code) {
  out.push_back(0xFFU);
  out.push_back(code);
}

void jpeg_word(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8U));
  out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

/// The probe's JPEG page: a flat field of `probe_jpeg_gray`.
///
/// Two Huffman tables, of two codes and one. The DC table spells category
/// 0 as `0` and category 6 as `10`; the AC table spells end-of-block as
/// `0`. Both are canonical for their `BITS` counts, neither uses the
/// all-ones code of its longest length, and between them they say
/// everything a constant image needs said.
[[nodiscard]] std::vector<std::uint8_t> probe_jpeg() {
  std::vector<std::uint8_t> out;
  jpeg_marker(out, 0xD8);  // SOI

  jpeg_marker(out, 0xDB);  // DQT
  jpeg_word(out, 2U + 1U + 64U);
  out.push_back(0x00U);  // 8-bit precision, table 0
  for (int i = 0; i < 64; ++i) {
    out.push_back(probe_jpeg_quant);
  }

  jpeg_marker(out, 0xC0);  // SOF0, baseline sequential
  jpeg_word(out, 2U + 1U + 2U + 2U + 1U + 3U);
  out.push_back(0x08U);  // sample precision
  jpeg_word(out, static_cast<std::uint16_t>(probe_image_three.height));
  jpeg_word(out, static_cast<std::uint16_t>(probe_image_three.width));
  out.push_back(0x01U);  // one component
  out.push_back(0x01U);  // its id
  out.push_back(0x11U);  // sampling 1x1
  out.push_back(0x00U);  // quantization table 0

  jpeg_marker(out, 0xC4);  // DHT, DC table 0
  jpeg_word(out, 2U + 1U + 16U + 2U);
  out.push_back(0x00U);
  out.push_back(1U);  // one code of length 1
  out.push_back(1U);  // one code of length 2
  for (int i = 2; i < 16; ++i) {
    out.push_back(0U);
  }
  out.push_back(0x00U);                   // ...spells category 0
  out.push_back(probe_jpeg_dc_category);  // ...spells category 6

  jpeg_marker(out, 0xC4);  // DHT, AC table 0
  jpeg_word(out, 2U + 1U + 16U + 1U);
  out.push_back(0x10U);
  out.push_back(1U);  // one code of length 1
  for (int i = 1; i < 16; ++i) {
    out.push_back(0U);
  }
  out.push_back(0x00U);  // ...spells end-of-block

  jpeg_marker(out, 0xDA);  // SOS
  jpeg_word(out, 2U + 1U + 2U + 3U);
  out.push_back(0x01U);  // one component in this scan
  out.push_back(0x01U);  // its id
  out.push_back(0x00U);  // DC table 0, AC table 0
  out.push_back(0x00U);  // Ss
  out.push_back(0x3FU);  // Se
  out.push_back(0x00U);  // Ah / Al

  const unsigned blocks = ((probe_image_three.width + 7U) / 8U) *
                          ((probe_image_three.height + 7U) / 8U);
  jpeg_bits bits;
  for (unsigned block = 0; block < blocks; ++block) {
    if (block == 0U) {
      // The first block's difference is the value itself: category, then
      // that many bits of it.
      bits.put(0b10U, 2U);
      bits.put(static_cast<std::uint32_t>(probe_jpeg_dc),
               probe_jpeg_dc_category);
    } else {
      bits.put(0b0U, 1U);  // category 0: no difference, no bits after it
    }
    bits.put(0b0U, 1U);  // end-of-block: every AC coefficient is zero
  }
  const std::vector<std::uint8_t> scan = bits.finish();
  out.insert(out.end(), scan.begin(), scan.end());

  jpeg_marker(out, 0xD9);  // EOI
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
  /// The pieces, and the entries that point into them. Held together so
  /// the spans in `facts` name storage with the document's own life.
  std::array<journal_fragment, journal_probe_fragments> fragments{};
  std::array<journal_entry_fact, journal_probe_entries> facts{};
  /// Entry three's stream, kept so the fixture can compare what reached
  /// an engine against what was written (#212).
  std::vector<std::uint8_t> encoded;
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
  std::array<std::uint64_t, 7> object_at{};

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
         " /Resources << /XObject << /Im1 4 0 R /Im2 5 0 R /Im3 6 0 R >>"
         " >> >>\nendobj\n");

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

  doc.encoded = probe_jpeg();
  object_at[6] = out.size();
  append(out,
         "6 0 obj\n<< /Type /XObject /Subtype /Image /Width 64 /Height 32"
         " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /DCTDecode"
         " /Length ");
  append_number(out, doc.encoded.size());
  append(out, " >>\nstream\n");
  const std::uint64_t offset_three = out.size();
  out.insert(out.end(), doc.encoded.begin(), doc.encoded.end());
  append(out, "\nendstream\nendobj\n");

  const std::uint64_t xref_at = out.size();
  append(out, "xref\n0 7\n0000000000 65535 f \n");
  for (std::size_t object = 1; object < object_at.size(); ++object) {
    append(out, xref_offset(object_at[object]));
    append(out, " 00000 n \n");
  }
  append(out, "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n");
  append_number(out, xref_at);
  append(out, "\n%%EOF\n");

  // Entries one and two are one fragment each; entry three is **two**
  // (M5-E3b, #214), so the probe exercises an entry that flows the way a
  // real edition's do — two rectangles of one page, joined in order.
  doc.fragments[0] =
      journal_fragment{.page = 1,
                       .offset = offset_one,
                       .length = static_cast<std::uint32_t>(stream_one.size()),
                       .image = probe_image_one,
                       .region = probe_region_one};
  doc.fragments[1] =
      journal_fragment{.page = 1,
                       .offset = offset_two,
                       .length = static_cast<std::uint32_t>(stream_two.size()),
                       .image = probe_image_two,
                       .region = probe_region_two};
  doc.fragments[2] =
      journal_fragment{.page = 1,
                       .offset = offset_three,
                       .length = static_cast<std::uint32_t>(doc.encoded.size()),
                       .image = probe_image_three,
                       .region = probe_region_three};
  doc.fragments[3] =
      journal_fragment{.page = 1,
                       .offset = offset_three,
                       .length = static_cast<std::uint32_t>(doc.encoded.size()),
                       .image = probe_image_three,
                       .region = probe_region_three_second};
  doc.facts[0] = journal_entry_fact{
      .number = 1, .fragments = std::span(doc.fragments).subspan(0, 1)};
  doc.facts[1] = journal_entry_fact{
      .number = 2, .fragments = std::span(doc.fragments).subspan(1, 1)};
  doc.facts[2] = journal_entry_fact{
      .number = 3, .fragments = std::span(doc.fragments).subspan(2, 2)};
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
    "AMBER FOLIO PROBE ENTRY 3",
};

}  // namespace

const std::vector<std::uint8_t>& journal_probe_pdf() { return probe().bytes; }

std::span<const journal_edition> journal_probe_table() {
  return probe_editions();
}

journal_bitmap journal_probe_expected(std::size_t index) {
  journal_bitmap out;
  // Entry three has no decoded answer at all: what the extractor must
  // produce for it is bytes (journal_probe.h).
  if (index >= journal_probe_entries || index == journal_probe_encoded_entry) {
    return out;
  }
  // Its one fragment's rectangle: the entries with a decoded answer have
  // exactly one piece (journal_probe.h).
  const journal_region& region = probe().facts[index].fragments.front().region;
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

std::span<const std::uint8_t> journal_probe_encoded(std::size_t index) {
  if (index != journal_probe_encoded_entry) {
    return {};
  }
  return probe().encoded;
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

bool journal_probe_ocr::recognize(const journal_scan& scan, std::string& out) {
  out.clear();
  if (scan.parts.empty()) {
    return false;
  }

  if (scan.encoding == journal_encoding::gray) {
    if (scan.parts.size() != 1U) {
      return false;  // no decoded entry of this probe has two pieces
    }
    const journal_bitmap& got = scan.parts.front().gray;
    for (std::size_t index = 0; index < expected_.size(); ++index) {
      const journal_bitmap& want = expected_[index];
      if (want.empty()) {
        continue;  // the encoded entry has no bitmap to be
      }
      if (got.width == want.width && got.height == want.height &&
          got.pixels == want.pixels) {
        out.assign(journal_probe_text(index));
        return true;
      }
    }
    return false;
  }

  // The encoded entry (#212): the stream this fixture's own encoder wrote,
  // byte for byte, **and** the regions the fact table gave, in order. The
  // regions are checked here because they are the one thing an engine is
  // now responsible for applying, so a probe that ignored them would be a
  // probe that could not tell a passthrough which forgot to carry them —
  // or one that dropped a fragment (#214).
  const std::span<const std::uint8_t> want =
      journal_probe_encoded(journal_probe_encoded_entry);
  const std::span<const journal_fragment> fragments =
      probe().facts[journal_probe_encoded_entry].fragments;
  if (want.empty() || scan.parts.size() != fragments.size()) {
    return false;
  }
  for (std::size_t i = 0; i < scan.parts.size(); ++i) {
    const journal_part& part = scan.parts[i];
    const journal_region& where = fragments[i].region;
    if (part.encoded.size() != want.size() ||
        !std::equal(part.encoded.begin(), part.encoded.end(), want.begin()) ||
        part.region.left != where.left || part.region.top != where.top ||
        part.region.width != where.width ||
        part.region.height != where.height) {
      // Not a stub that says yes to anything: a scan that is not the one
      // this fixture knows is a scan the pipeline got wrong somewhere, and
      // the honest answer is that nothing was read (journal_probe.h).
      return false;
    }
  }
  out.assign(journal_probe_text(journal_probe_encoded_entry));
  return true;
}

std::string_view journal_probe_ocr::engine() const {
  return "amberfolio journal probe fixture";
}

}  // namespace amberfolio::host
