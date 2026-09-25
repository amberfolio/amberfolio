// SPDX-License-Identifier: AGPL-3.0-only
//
// The text probe. journal_text_probe.h has the reasoning; what is here is
// the generator, and the only facts its table carries are ones it
// measured while writing the file.

#include "amberfolio/host/journal_text_probe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

/// What the two items read as. Invented, like every word of this file,
/// and written the way `journal_text.h` says a reading is written: one
/// line to a paragraph, a blank line between paragraphs.
constexpr std::array<std::string_view, journal_text_probe_items> probe_texts{
    "Entry 1:\n\n"
    "The amber lamp burns low over the folio, and its keeper writes by "
    "night. A self-taught scribe, she keeps no calendar.\n\n"
    "\u201CTwo lamps,\u201D she says (softly).",
    "Tale 1: A folio of amber pages sings when opened.",
};

/// The font's map: an encoding of this file's own, in both of a CMap's
/// forms. Code 0x80 is a Cyrillic letter drawn as a Latin `T`, which the
/// route folds; 0x7F is mapped by nothing.
constexpr std::string_view probe_cmap =
    "/CIDInit /ProcSet findresource begin\n"
    "12 dict begin\n"
    "begincmap\n"
    "/CMapName /AmberFolio-Probe-UCS def\n"
    "/CMapType 2 def\n"
    "1 begincodespacerange\n<00> <FF>\nendcodespacerange\n"
    "10 beginbfchar\n"
    "<20> <0020>\n<28> <0028>\n<29> <0029>\n<2C> <002C>\n<2D> <002D>\n"
    "<2E> <002E>\n<3A> <003A>\n<80> <0422>\n<81> <201C>\n<82> <201D>\n"
    "endbfchar\n"
    "3 beginbfrange\n"
    "<30> <39> [<0030> <0031> <0032> <0033> <0034> <0035> <0036> <0037>"
    " <0038> <0039>]\n"
    "<41> <5A> <0041>\n"
    "<61> <7A> <0061>\n"
    "endbfrange\n"
    "endcmap\n"
    "CMapName currentdict /CMap defineresource pop\n"
    "end\nend\n";

/// Page one: the entry, over two columns, with everything around it that
/// must *not* be read into it. `journal_text_probe.h` lists what each
/// line is for.
constexpr std::string_view probe_page_one =
    "% the text probe, page one\n"
    "/Artifact <</Note (a string (with parens) inside)>> BDC\n"
    "BT /F2 9 Tf 1 0 0 1 20 285 Tm (Untabled font) Tj ET\n"
    "EMC\n"
    "BT /F1 9 Tf 1 0 0 1 20 5 Tm (\\177) Tj ET\n"
    "BT /F1 1 Tf 11 0 0 11 20 260 Tm (Entry 1:) Tj ET\n"
    "BT /F1 1 Tf 9 0 0 9 28.5 248 Tm"
    " [(The amber lamp) -20 ( burns low over the fo)] TJ ET\n"
    "BT /F1 1 Tf 9 0 0 9 180 248 Tm (-) Tj ET\n"
    "BT /F1 9 Tf 12 TL 1 0 0 1 20 238 Tm"
    " (lio, and its keeper writes by  night. ) Tj"
    " (A self-) ' (taught scribe,) ' ET\n"
    "q 16 0 0 8 20 120 cm BI /W 2 /H 1 /CS /G /BPC 8 ID )( EI Q\n"
    "q 32 0 0 16 20 150 cm /Im1 Do Q\n"
    "q 1 0 0 1 190 0 cm\n"
    "BT /F1 9 Tf 1 0 0 1 20 260 Tm (she keeps no calendar.) Tj"
    " 8.5 -12 Td (\\201\\200wo lamps,\\202 she\\\n says \\(softly\\).) Tj"
    " ET\n"
    "Q\n";

/// Page two: a tale in one piece, unfiltered, positioned by `TD` and `T*`
/// and opened with a hex string.
constexpr std::string_view probe_page_two =
    "BT /F1 9 Tf 1 0 0 1 20 260 Tm"
    " <54616C6520313A2041 20666F6C696F206F6620616D62657220> Tj"
    " 0 -10 TD (pages sings when ) Tj T* (opened.) Tj ET\n";

/// The picture on entry one: a small gray ramp, eight bits, no predictor.
constexpr std::uint32_t probe_art_width = 16;
constexpr std::uint32_t probe_art_height = 8;

[[nodiscard]] std::vector<std::uint8_t> bytes_of(std::string_view text) {
  return {text.begin(), text.end()};
}

void append(std::vector<std::uint8_t>& out, std::string_view text) {
  out.insert(out.end(), text.begin(), text.end());
}

[[nodiscard]] std::string xref_offset(std::uint64_t value) {
  std::string text = std::to_string(value);
  return std::string(text.size() < 10U ? 10U - text.size() : 0U, '0') + text;
}

struct stream_fact {
  std::uint64_t offset{};
  std::uint32_t length{};
  std::uint32_t decoded{};
};

struct text_probe_document {
  std::vector<std::uint8_t> bytes;
  std::array<journal_text_font, 1> fonts{};
  std::array<journal_text_page, 2> pages{};
  std::array<journal_text_fragment, 3> fragments{};
  std::array<journal_fragment, 1> art{};
  std::array<std::string, journal_text_probe_items> digests{};
  std::array<journal_entry_fact, journal_text_probe_items> facts{};
  std::string fingerprint;
  journal_edition edition{};
};

/// One stream object, recording where its data landed.
[[nodiscard]] stream_fact write_stream(std::vector<std::uint8_t>& out,
                                       std::string_view dictionary,
                                       std::span<const std::uint8_t> data,
                                       std::uint32_t decoded) {
  append(out, dictionary);
  append(out, " /Length ");
  append(out, std::to_string(data.size()));
  append(out, " >>\nstream\n");
  const stream_fact fact{.offset = out.size(),
                         .length = static_cast<std::uint32_t>(data.size()),
                         .decoded = decoded};
  out.insert(out.end(), data.begin(), data.end());
  append(out, "\nendstream\nendobj\n");
  return fact;
}

[[nodiscard]] std::string hex_of(const sha256_digest& digest) {
  std::array<char, sha256_digest::text_length + 1> text{};
  const std::size_t written = format_hex(digest, text);
  return {text.data(), written};
}

void build(text_probe_document& doc) {
  std::vector<std::uint8_t>& out = doc.bytes;
  std::array<std::uint64_t, 11> object_at{};
  append(out, "%PDF-1.4\n");
  out.insert(out.end(), {'%', 0xE2, 0xE3, 0xCF, 0xD3, '\n'});

  object_at[1] = out.size();
  append(out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  object_at[2] = out.size();
  append(out,
         "2 0 obj\n<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>\nendobj\n");
  object_at[3] = out.size();
  append(out,
         "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300]"
         " /Resources << /Font << /F1 5 0 R /F2 6 0 R >>"
         " /XObject << /Im1 9 0 R >> >> /Contents 7 0 R >>\nendobj\n");
  object_at[4] = out.size();
  append(out,
         "4 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 300]"
         " /Resources << /Font << /F1 5 0 R >> >> /Contents 8 0 R >>\n"
         "endobj\n");
  object_at[5] = out.size();
  append(out,
         "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica"
         " /ToUnicode 10 0 R >>\nendobj\n");
  object_at[6] = out.size();
  append(out,
         "6 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>\n"
         "endobj\n");

  const std::vector<std::uint8_t> one = bytes_of(probe_page_one);
  object_at[7] = out.size();
  const stream_fact page_one = write_stream(
      out, "7 0 obj\n<< /Filter /FlateDecode", journal_probe_zlib_stored(one),
      static_cast<std::uint32_t>(one.size()));

  const std::vector<std::uint8_t> two = bytes_of(probe_page_two);
  object_at[8] = out.size();
  const stream_fact page_two = write_stream(
      out, "8 0 obj\n<<", two, static_cast<std::uint32_t>(two.size()));

  std::vector<std::uint8_t> samples;
  for (std::uint32_t y = 0; y < probe_art_height; ++y) {
    for (std::uint32_t x = 0; x < probe_art_width; ++x) {
      samples.push_back(
          static_cast<std::uint8_t>(((x * 16U) + (y * 8U)) & 0xFFU));
    }
  }
  object_at[9] = out.size();
  const stream_fact picture = write_stream(
      out,
      "9 0 obj\n<< /Type /XObject /Subtype /Image /Width 16 /Height 8"
      " /ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode",
      journal_probe_zlib_stored(samples),
      static_cast<std::uint32_t>(samples.size()));

  const std::vector<std::uint8_t> cmap = bytes_of(probe_cmap);
  object_at[10] = out.size();
  const stream_fact map = write_stream(out, "10 0 obj\n<< /Filter /FlateDecode",
                                       journal_probe_zlib_stored(cmap),
                                       static_cast<std::uint32_t>(cmap.size()));

  const std::uint64_t xref_at = out.size();
  append(out, "xref\n0 11\n0000000000 65535 f \n");
  for (std::size_t object = 1; object < object_at.size(); ++object) {
    append(out, xref_offset(object_at[object]));
    append(out, " 00000 n \n");
  }
  append(out, "trailer\n<< /Size 11 /Root 1 0 R >>\nstartxref\n");
  append(out, std::to_string(xref_at));
  append(out, "\n%%EOF\n");

  doc.fonts[0] = journal_text_font{.resource = "F1",
                                   .offset = map.offset,
                                   .length = map.length,
                                   .decoded = map.decoded,
                                   .filter = journal_filter::flate};
  doc.pages[0] = journal_text_page{.page = 1,
                                   .offset = page_one.offset,
                                   .length = page_one.length,
                                   .decoded = page_one.decoded,
                                   .filter = journal_filter::flate,
                                   .fonts = doc.fonts};
  doc.pages[1] = journal_text_page{.page = 2,
                                   .offset = page_two.offset,
                                   .length = page_two.length,
                                   .decoded = page_two.decoded,
                                   .filter = journal_filter::none,
                                   .fonts = doc.fonts};
  // Entry one's two columns, and tale one's single piece. The second
  // column's box starts at its own margin, so its first line continues
  // the paragraph and its second, eight and a half points in, opens one.
  doc.fragments[0] = journal_text_fragment{
      .page = 1, .box = {.left = 20, .bottom = 200, .right = 200, .top = 275}};
  doc.fragments[1] = journal_text_fragment{
      .page = 1, .box = {.left = 210, .bottom = 200, .right = 400, .top = 275}};
  doc.fragments[2] = journal_text_fragment{
      .page = 2, .box = {.left = 20, .bottom = 240, .right = 400, .top = 275}};
  doc.art[0] = journal_fragment{.page = 1,
                                .offset = picture.offset,
                                .length = picture.length,
                                .image = {.width = probe_art_width,
                                          .height = probe_art_height,
                                          .bits_per_component = 8,
                                          .components = 1,
                                          .filter = journal_filter::flate},
                                .region = {.left = 0,
                                           .top = 0,
                                           .width = probe_art_width,
                                           .height = probe_art_height}};

  for (std::size_t i = 0; i < journal_text_probe_items; ++i) {
    const std::string_view text = probe_texts.at(i);
    doc.digests.at(i) = hex_of(sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size())));
  }
  doc.facts[0] = journal_entry_fact{
      .kind = journal_kind::entry,
      .number = 1,
      .art = doc.art,
      .text = std::span<const journal_text_fragment>(doc.fragments).first(2),
      .text_sha256 = doc.digests[0]};
  doc.facts[1] = journal_entry_fact{
      .kind = journal_kind::tale,
      .number = 1,
      .text = std::span<const journal_text_fragment>(doc.fragments).subspan(2),
      .text_sha256 = doc.digests[1]};

  doc.fingerprint = hex_of(sha256(doc.bytes));
  doc.edition =
      journal_edition{.fingerprint = doc.fingerprint,
                      .name = "Amber Folio journal text probe (synthetic)",
                      .entries = doc.facts,
                      .pages = doc.pages};
}

/// Built once, on first use, and never freed, as the scanned probe is —
/// and built **in place**, because its spans and its fingerprint's view
/// point into itself, and a copy would point into the one it was made
/// from.
const text_probe_document& probe() {
  static text_probe_document one;
  static const bool built = [] {
    build(one);
    return true;
  }();
  static_cast<void>(built);
  return one;
}

}  // namespace

const std::vector<std::uint8_t>& journal_text_probe_pdf() {
  return probe().bytes;
}

const journal_edition& journal_text_probe_edition() { return probe().edition; }

std::string_view journal_text_probe_text(std::size_t index) {
  return index < probe_texts.size() ? probe_texts.at(index)
                                    : std::string_view{};
}

}  // namespace amberfolio::host
