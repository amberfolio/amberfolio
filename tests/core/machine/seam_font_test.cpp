// SPDX-License-Identifier: AGPL-3.0-only
//
// The font seams (seam_font.cpp) and the faces they draw (text_face.h),
// exercised through their mechanism and not through any program.
//
// The seam swaps the row the program's glyph blitter has just fetched, so
// the rig lays out what the blitter would be looking at: the program's
// font pointer in its data segment, a table of glyphs where it points,
// and the processor standing on one of the two points with ES:DI on a row
// and the row in DL. The program's glyphs are never in this tree; the
// table here is this project's own BIOS font (font.h), laid out the way
// the program lays its own out.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. The interception addresses
// *are* read from the definition, because those are the mechanism. Every
// byte here is this file's own (PLAN.md §6).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/font.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/log.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/screen_text.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_face.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

using text_face::face;

// --- The facts this test lays memory out by --------------------------------

constexpr std::uint32_t dgroup_offset = 0xC7C0;
constexpr std::uint16_t data_font_pointer = 0x5E20;

/// Where the test puts the program's glyphs: a segment of its own, and
/// an offset inside it that is not zero, so a handler that forgot to
/// subtract it gives a wrong glyph rather than a lucky one.
constexpr std::uint16_t glyph_segment = 0x3000;
constexpr std::uint16_t glyph_offset = 0x0010;

/// The buffer the program keeps: its sixty-four text glyphs and the
/// pictures past them.
constexpr unsigned buffer_glyphs = 177;

/// What DL holds before the handler runs: no glyph row in either table.
constexpr std::uint8_t sentinel_row = 0xA5;

/// The program's index for a character: upper-cased, modulo sixty-four.
[[nodiscard]] unsigned index_of(char ch) {
  auto code = static_cast<unsigned char>(ch);
  if (code >= 'a' && code <= 'z') {
    code = static_cast<unsigned char>(code - 0x20);
  }
  return code % text_face::glyph_count;
}

using program_table = std::array<std::uint8_t, text_face::table_bytes>;

/// A program font in the program's layout, out of font.h.
[[nodiscard]] program_table make_program_font() {
  program_table out{};
  const std::span<const std::uint8_t> ours = font::glyphs();
  for (std::size_t index = 0; index < text_face::glyph_count; ++index) {
    const std::size_t code = index < 32 ? index + 0x40 : index;
    for (std::size_t row = 0; row < text_face::glyph_bytes; ++row) {
      out[(index * text_face::glyph_bytes) + row] =
          ours[(code * font::glyph_height) + row];
    }
  }
  return out;
}

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    install(make_program_font());
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  [[nodiscard]] static std::uint16_t dgroup() noexcept {
    return static_cast<std::uint16_t>(image_load_segment +
                                      (dgroup_offset / 16));
  }

  void put_byte(std::uint32_t address, std::uint8_t value) const {
    box->memory().ram()[address] = value;
  }

  /// The program's glyphs, its far pointer at them, and a picture past
  /// them in every row of the buffer, so the whole buffer is non-zero.
  void install(const program_table& font) const {
    const std::uint32_t glyphs =
        cpu::physical_address(glyph_segment, 0) + glyph_offset;
    for (std::size_t i = 0; i < std::size_t{buffer_glyphs} * 8; ++i) {
      put_byte(static_cast<std::uint32_t>(glyphs + i),
               i < font.size() ? font[i] : 0x81);
    }
    const std::uint32_t at =
        cpu::physical_address(dgroup(), 0) + data_font_pointer;
    put_byte(at, static_cast<std::uint8_t>(glyph_offset & 0xFFU));
    put_byte(at + 1, static_cast<std::uint8_t>(glyph_offset >> 8U));
    put_byte(at + 2, static_cast<std::uint8_t>(glyph_segment & 0xFFU));
    put_byte(at + 3, static_cast<std::uint8_t>(glyph_segment >> 8U));
  }

  void enable(std::string_view id) const {
    ASSERT_EQ(box->seams().enable(id), seam_reason::none);
  }

  /// The physical address of one of `id`'s points, read from the
  /// definition.
  [[nodiscard]] std::uint32_t point(std::string_view id,
                                    std::size_t which) const {
    const seam_definition* found = box->seams().find(id);
    EXPECT_NE(found, nullptr);
    EXPECT_LT(which, found->points.size());
    return cpu::physical_address(image_load_segment, 0) +
           found->points[which].offset;
  }

  /// Stand on a point with ES:DI on row `row` of glyph `glyph`, the row's
  /// stand-in in DL, and step once. Answers DL afterwards.
  [[nodiscard]] std::uint8_t fetch(std::uint32_t address, unsigned glyph,
                                   unsigned row,
                                   std::uint16_t es = glyph_segment) const {
    box->memory().ram()[address] = 0xF4;  // HLT, for after the handler
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = static_cast<std::uint16_t>(
        address - cpu::physical_address(image_load_segment, 0));
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::sreg::es] = es;
    r[cpu::reg16::di] =
        static_cast<std::uint16_t>(glyph_offset + (glyph * 8) + row);
    r.set(cpu::reg8::dl, sentinel_row);
    box->step();
    return r.get(cpu::reg8::dl);
  }

  diagnostic_log log;
  std::unique_ptr<machine> box;
};

// --- The faces -------------------------------------------------------------

/// The nine text glyphs that are not lettering, and the space.
constexpr std::array<unsigned, 9> kept_indices{0,  27, 28, 29, 30,
                                               31, 32, 36, 37};

[[nodiscard]] bool kept(unsigned index) {
  return std::ranges::find(kept_indices, index) != kept_indices.end();
}

TEST(TextFace, ReplacesTheLettersDigitsAndPunctuationAndNothingElse) {
  for (unsigned index = 0; index < text_face::glyph_count; ++index) {
    EXPECT_EQ(text_face::replaces(index), !kept(index)) << index;
  }
  EXPECT_FALSE(text_face::replaces(text_face::glyph_count));
  for (const char ch : std::string_view("ABCXYZ0189.,:;!?-+()/'\"&*<=>#")) {
    EXPECT_TRUE(text_face::replaces(index_of(ch))) << ch;
  }
}

TEST(TextFace, EveryReplacedGlyphIsDrawnAndNoTwoAreAlike) {
  for (const face which : {face::sans, face::chisel}) {
    std::set<std::string> seen;
    for (unsigned index = 0; index < text_face::glyph_count; ++index) {
      if (!text_face::replaces(index)) {
        continue;
      }
      std::string bits;
      bool lit = false;
      for (unsigned row = 0; row < text_face::glyph_bytes; ++row) {
        const std::uint8_t byte = text_face::row(which, index, row);
        lit = lit || byte != 0;
        bits.push_back(static_cast<char>(byte));
      }
      EXPECT_TRUE(lit) << text_face::seam_id(which) << " glyph " << index;
      // Two characters with one bitmap would be a misreading screen text
      // reports as ambiguous, and a player cannot tell apart at all.
      EXPECT_TRUE(seen.insert(bits).second)
          << text_face::seam_id(which) << " glyph " << index;
    }
  }
}

TEST(TextFace, AGlyphIsLeftAColumnAndARowToSpaceIt) {
  // The column right of every letter and the row under it are paper, so
  // text set solid still has a gap between letters and between lines —
  // except the punctuation that hangs below the line by design.
  const std::set<char> descenders{',', ';', '_'};
  for (const face which : {face::sans, face::chisel}) {
    for (unsigned index = 0; index < text_face::glyph_count; ++index) {
      if (!text_face::replaces(index)) {
        continue;
      }
      const char ch = static_cast<char>(index < 32 ? index + 0x40 : index);
      for (unsigned row = 0; row < text_face::glyph_bytes; ++row) {
        EXPECT_EQ(text_face::row(which, index, row) & 0x01U, 0U)
            << text_face::seam_id(which) << " '" << ch << "'";
      }
      if (!descenders.contains(ch)) {
        EXPECT_EQ(text_face::row(which, index, 7), 0U)
            << text_face::seam_id(which) << " '" << ch << "'";
      }
    }
  }
}

TEST(TextFace, ApplyWritesOnlyTheGlyphsAFaceReplaces) {
  const program_table program = make_program_font();
  program_table drawn = program;
  text_face::apply(face::sans, drawn);
  for (unsigned index = 0; index < text_face::glyph_count; ++index) {
    for (unsigned row = 0; row < text_face::glyph_bytes; ++row) {
      const std::size_t at = (std::size_t{index} * 8) + row;
      EXPECT_EQ(drawn[at], text_face::replaces(index)
                               ? text_face::row(face::sans, index, row)
                               : program[at]);
    }
  }
  program_table untouched = program;
  text_face::apply(face::program, untouched);
  EXPECT_EQ(untouched, program);
}

TEST(TextFace, NoSeamOnIsTheProgramsOwn) {
  const rig r;
  EXPECT_EQ(text_face::drawing(r.box->seams()), face::program);
  r.enable("font-chisel");
  EXPECT_EQ(text_face::drawing(r.box->seams()), face::chisel);
  r.enable("font-sans");
  EXPECT_EQ(text_face::drawing(r.box->seams()), face::sans);
  EXPECT_EQ(r.box->seams().status("font-chisel").state, seam_state::off);
}

// --- The seam --------------------------------------------------------------

TEST(SeamFont, SwapsALetterRowAtBothFetches) {
  const rig r;
  r.enable("font-sans");
  // The sans `A`'s apex, restated: three pixels from column two.
  EXPECT_EQ(r.fetch(r.point("font-sans", 0), index_of('A'), 0), 0x38);
  EXPECT_EQ(r.fetch(r.point("font-sans", 1), index_of('A'), 0), 0x38);
  for (unsigned row = 0; row < 8; ++row) {
    EXPECT_EQ(r.fetch(r.point("font-sans", 0), index_of('7'), row),
              text_face::row(face::sans, index_of('7'), row));
  }
}

TEST(SeamFont, EachFaceDrawsItsOwn) {
  const rig r;
  r.enable("font-chisel");
  const unsigned h = index_of('H');
  // The chisel shaves the stroke's top-left corner off; the sans does not.
  EXPECT_NE(text_face::row(face::chisel, h, 0),
            text_face::row(face::sans, h, 0));
  EXPECT_EQ(r.fetch(r.point("font-chisel", 0), h, 0),
            text_face::row(face::chisel, h, 0));
}

TEST(SeamFont, LeavesTheGlyphsThatAreNotLetteringAlone) {
  const rig r;
  r.enable("font-sans");
  for (const unsigned index : kept_indices) {
    EXPECT_EQ(r.fetch(r.point("font-sans", 0), index, 3), sentinel_row)
        << index;
  }
  // A picture past the text table.
  EXPECT_EQ(r.fetch(r.point("font-sans", 1), 100, 3), sentinel_row);
  // Left alone is not declined: every one of those arrivals counts.
  EXPECT_EQ(r.box->seams().status("font-sans").fired, kept_indices.size() + 1);
}

TEST(SeamFont, DeclinesAFetchThatIsNotFromTheFont) {
  const rig r;
  r.enable("font-sans");
  EXPECT_EQ(r.fetch(r.point("font-sans", 0), index_of('A'), 0, 0x4000),
            sentinel_row);
  // Past the end of the buffer is not a glyph either.
  EXPECT_EQ(r.fetch(r.point("font-sans", 0), buffer_glyphs, 0), sentinel_row);
  // A decline is not an act.
  EXPECT_EQ(r.box->seams().status("font-sans").fired, 0U);
}

TEST(SeamFont, DeclinesWhenTheProgramHasNoFont) {
  rig r;
  const std::uint32_t at =
      cpu::physical_address(rig::dgroup(), 0) + data_font_pointer;
  for (std::uint32_t i = 0; i < 4; ++i) {
    r.put_byte(at + i, 0);
  }
  r.enable("font-sans");
  EXPECT_EQ(r.fetch(r.point("font-sans", 0), index_of('A'), 0), sentinel_row);
  EXPECT_EQ(r.box->seams().status("font-sans").fired, 0U);
}

TEST(SeamFont, OffIsTheProgramsRow) {
  const rig r;
  const seam_definition* sans = r.box->seams().find("font-sans");
  ASSERT_NE(sans, nullptr);
  const std::uint32_t at =
      cpu::physical_address(image_load_segment, 0) + sans->points[0].offset;
  EXPECT_EQ(r.fetch(at, index_of('A'), 0), sentinel_row);
}

TEST(SeamFont, NeverWritesTheProgramsFont) {
  const rig r;
  r.enable("font-sans");
  const std::uint32_t glyphs =
      cpu::physical_address(glyph_segment, 0) + glyph_offset;
  const program_table before = make_program_font();
  for (unsigned index = 0; index < text_face::glyph_count; ++index) {
    static_cast<void>(r.fetch(r.point("font-sans", 0), index, 2));
  }
  for (std::size_t i = 0; i < before.size(); ++i) {
    EXPECT_EQ(r.box->memory().ram()[glyphs + i], before[i]) << i;
  }
}

// --- Screen text, while a face is drawing ------------------------------------

/// `text` into the display at a cell in `table`'s glyphs.
void draw(machine& box, const program_table& table, unsigned column,
          unsigned row, std::string_view text, std::uint8_t ink) {
  std::span<std::uint8_t> pixels = box.display().writable_pixels();
  for (const char ch : text) {
    const std::size_t glyph = std::size_t{index_of(ch)} * 8;
    for (std::size_t y = 0; y < 8; ++y) {
      for (std::size_t x = 0; x < 8; ++x) {
        const std::size_t at = ((std::size_t{row} * 8 + y) * frame_width) +
                               (std::size_t{column} * 8) + x;
        pixels[at] = ((table[glyph + y] >> (7 - x)) & 1U) != 0 ? ink : 0;
      }
    }
    ++column;
  }
}

[[nodiscard]] std::string read_back(const text_grid& grid, unsigned column,
                                    unsigned row, std::size_t length) {
  std::string out;
  for (std::size_t i = 0; i < length; ++i) {
    out.push_back(static_cast<char>(
        grid[(std::size_t{row} * text_columns) + column + i].code));
  }
  return out;
}

TEST(SeamFontScreenText, ReadsTextDrawnInTheFace) {
  const rig r;
  r.enable("font-chisel");
  program_table drawn = make_program_font();
  text_face::apply(face::chisel, drawn);
  draw(*r.box, drawn, 0, 24, "AREA CAST VIEW 1,12", 15);

  text_grid grid{};
  ASSERT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::none);
  EXPECT_EQ(read_back(grid, 0, 24, 19), "AREA CAST VIEW 1,12");
}

TEST(SeamFontScreenText, StillReadsTextDrawnBeforeTheSwitch) {
  const rig r;
  draw(*r.box, make_program_font(), 0, 0, "LEVEL 4", 15);
  r.enable("font-sans");
  program_table drawn = make_program_font();
  text_face::apply(face::sans, drawn);
  draw(*r.box, drawn, 0, 1, "EXP 16814", 15);

  text_grid grid{};
  ASSERT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::none);
  EXPECT_EQ(read_back(grid, 0, 0, 7), "LEVEL 4");
  EXPECT_EQ(read_back(grid, 0, 1, 9), "EXP 16814");
}

TEST(SeamFontScreenText, AFaceThatIsOffIsNotRead) {
  const rig r;
  program_table drawn = make_program_font();
  text_face::apply(face::sans, drawn);
  draw(*r.box, drawn, 0, 0, "W", 15);

  text_grid grid{};
  ASSERT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::none);
  EXPECT_EQ(grid[0].code, cell_not_text);
}

}  // namespace
}  // namespace amberfolio::machine
