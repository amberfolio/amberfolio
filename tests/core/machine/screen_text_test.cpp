// SPDX-License-Identifier: AGPL-3.0-only
//
// Screen text (screen_text.h): a frame read back as characters by
// matching it against the program's own glyphs.
//
// The glyphs here are this project's own (font.h), laid out the way the
// program lays its font out — sixty-four of them, indexed by the
// character upper-cased modulo sixty-four — because the program's are
// never in this tree. What is under test is the matcher and where it
// finds the font, not any particular bitmap.

#include "amberfolio/machine/screen_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/font.h"
#include "amberfolio/machine/log.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

using program_font = std::array<std::uint8_t, program_font_bytes>;

/// The program's index for a character: upper-cased, modulo sixty-four.
[[nodiscard]] std::size_t index_of(char ch) {
  auto code = static_cast<unsigned char>(ch);
  if (code >= 'a' && code <= 'z') {
    code = static_cast<unsigned char>(code - 0x20);
  }
  return code % program_font_glyphs;
}

/// A sixty-four glyph font in the program's layout, drawn from font.h:
/// index 0-31 is `@` to `_`, 32-63 is space to `?`.
[[nodiscard]] program_font make_font() {
  program_font out{};
  const std::span<const std::uint8_t> ours = font::glyphs();
  for (std::size_t index = 0; index < program_font_glyphs; ++index) {
    const std::size_t code = index < 32 ? index + 0x40 : index;
    for (std::size_t row = 0; row < text_cell_pixels; ++row) {
      out[(index * text_cell_pixels) + row] =
          ours[(code * font::glyph_height) + row];
    }
  }
  return out;
}

using frame = std::array<std::uint8_t, frame_pixels>;

/// `text` into `pixels` at a cell, in `font`'s glyphs, as the program
/// draws it: every pixel of every cell is the ink or the paper.
void draw(std::span<std::uint8_t> pixels, const program_font& font,
          unsigned column, unsigned row, std::string_view text,
          std::uint8_t ink, std::uint8_t paper) {
  for (const char ch : text) {
    const std::size_t glyph = index_of(ch) * text_cell_pixels;
    for (std::size_t y = 0; y < text_cell_pixels; ++y) {
      const std::uint8_t bits = font[glyph + y];
      for (std::size_t x = 0; x < text_cell_pixels; ++x) {
        const std::size_t at =
            ((std::size_t{row} * text_cell_pixels + y) * frame_width) +
            (std::size_t{column} * text_cell_pixels) + x;
        pixels[at] = ((bits >> (7 - x)) & 1U) != 0 ? ink : paper;
      }
    }
    ++column;
  }
}

[[nodiscard]] const text_cell& cell(const text_grid& grid, unsigned column,
                                    unsigned row) {
  return grid[(std::size_t{row} * text_columns) + column];
}

/// The characters of a row from `column`, `count` of them.
[[nodiscard]] std::string read(const text_grid& grid, unsigned column,
                               unsigned row, unsigned count) {
  std::string out;
  for (unsigned i = 0; i < count; ++i) {
    out += static_cast<char>(cell(grid, column + i, row).code);
  }
  return out;
}

TEST(ScreenText, TheGridIsTheFrameInEightPixelCells) {
  EXPECT_EQ(text_columns, 40U);
  EXPECT_EQ(text_rows, 25U);
}

TEST(ScreenText, ReadsTextWithItsInkAndPaper) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 3, 5, "STR 18(42)", 15, 1);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(read(grid, 3, 5, 10), "STR 18(42)");
  EXPECT_EQ(cell(grid, 3, 5).ink, 15);
  EXPECT_EQ(cell(grid, 3, 5).paper, 1);
  // The blank inside the run is a blank in the paper's colour.
  EXPECT_EQ(cell(grid, 6, 5).ink, 1);
  EXPECT_EQ(cell(grid, 6, 5).paper, 1);
}

TEST(ScreenText, ReadsLowerCaseAsTheUpperCaseTheProgramDraws) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 0, "exit", 10, 0);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(read(grid, 0, 0, 4), "EXIT");
}

TEST(ScreenText, TellsAShortcutLetterByItsInk) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 1, 12, "C", 15, 0);
  draw(*pixels, font, 2, 12, "REATE", 10, 0);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(read(grid, 1, 12, 6), "CREATE");
  EXPECT_EQ(cell(grid, 1, 12).ink, 15);
  EXPECT_EQ(cell(grid, 2, 12).ink, 10);
}

TEST(ScreenText, EitherColourMayBeTheInk) {
  // Dark letters on a light bar read the same as light on dark: which of
  // the two colours is the ink is the one that draws a glyph.
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 24, "YES", 0, 14);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(read(grid, 0, 24, 3), "YES");
  EXPECT_EQ(cell(grid, 0, 24).ink, 0);
  EXPECT_EQ(cell(grid, 0, 24).paper, 14);
}

TEST(ScreenText, ACellOfThreeColoursIsNotText) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 0, "A", 15, 0);
  // A third colour inside the cell, off its column 0 and row 7.
  (*pixels)[(3 * frame_width) + 3] = 4;

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 0, 0).code, cell_not_text);
}

TEST(ScreenText, ACellIsReadAgainWithoutTheEdgesASelectionsMarginTakes) {
  // A selection block draws its top in row 7 of the cell above and its
  // right in column 0 of the cell after (#483): a third colour there, or a
  // line on a blank cell, is read past.
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 0, "A", 10, 0);
  draw(*pixels, font, 1, 0, " ", 10, 0);
  for (std::size_t x = 0; x < text_cell_pixels; ++x) {
    (*pixels)[(7 * frame_width) + x] = 14;  // under `A`
  }
  for (std::size_t y = 0; y < text_cell_pixels; ++y) {
    (*pixels)[(y * frame_width) + text_cell_pixels] = 14;  // the blank's left
  }

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 0, 0).code, 'A');
  EXPECT_EQ(cell(grid, 0, 0).ink, 10);
  EXPECT_EQ(cell(grid, 0, 0).paper, 0);
  EXPECT_EQ(cell(grid, 1, 0).code, ' ');
  EXPECT_EQ(cell(grid, 1, 0).paper, 0);
}

TEST(ScreenText, ACellThatReadsTheFirstTimeIsNotReadAgain) {
  // A glyph with a pixel of its own in row 7 reads exactly, edges and all.
  program_font font = make_font();
  const std::size_t at = index_of('Q') * text_cell_pixels;
  font[at + 7] = 0x04;  // a tail
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 0, "Q", 15, 0);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 0, 0).code, 'Q');
}

TEST(ScreenText, TwoColoursThatDrawNoGlyphAreNotText) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();
  // A checkerboard: two colours, and no character's bitmap.
  for (std::size_t y = 0; y < text_cell_pixels; ++y) {
    for (std::size_t x = 0; x < text_cell_pixels; ++x) {
      (*pixels)[(y * frame_width) + x] = ((x + y) % 2 == 0) ? 7 : 8;
    }
  }

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 0, 0).code, cell_not_text);
}

TEST(ScreenText, AnEmptyScreenIsBlanksNotText) {
  const program_font font = make_font();
  auto pixels = std::make_unique<frame>();

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 20, 12).code, ' ');
}

TEST(ScreenText, TwoCharactersWithOneBitmapAreAmbiguousNotAGuess) {
  // Give `O` the bitmap of `0`: a cell with that bitmap is either, so it
  // is read as neither.
  program_font font = make_font();
  for (std::size_t row = 0; row < text_cell_pixels; ++row) {
    font[(index_of('O') * text_cell_pixels) + row] =
        font[(index_of('0') * text_cell_pixels) + row];
  }
  auto pixels = std::make_unique<frame>();
  draw(*pixels, font, 0, 0, "0K", 15, 0);

  text_grid grid{};
  read_text_cells(*pixels, font, grid);

  EXPECT_EQ(cell(grid, 0, 0).code, cell_ambiguous);
  EXPECT_EQ(cell(grid, 1, 0).code, 'K');
}

// --- On a machine ----------------------------------------------------------

/// Where the baseline keeps its font: the data segment as an offset in
/// the image, and the far pointer in it (seams.md §10, the zone label).
constexpr std::uint32_t dgroup_offset = 0xC7C0;
constexpr std::uint16_t font_pointer = 0x5E20;
constexpr std::uint16_t image_segment = 0x1000;
/// Anywhere in conventional memory the test's glyphs can sit.
constexpr std::uint16_t glyph_segment = 0x3000;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {}

  void load(const sha256_digest& digest) {
    box->seams().loaded(digest, image_segment);
  }

  void load_baseline() {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, digest));
    load(digest);
  }

  /// The glyphs into RAM and the program's far pointer at them.
  void install(const program_font& font) {
    const std::span<std::uint8_t> ram = box->memory().ram();
    const std::uint32_t glyphs = std::uint32_t{glyph_segment} * 16U;
    for (std::size_t i = 0; i < font.size(); ++i) {
      ram[glyphs + i] = font[i];
    }
    const std::uint32_t at =
        (std::uint32_t{image_segment} * 16U) + dgroup_offset + font_pointer;
    ram[at] = 0;
    ram[at + 1] = 0;
    ram[at + 2] = static_cast<std::uint8_t>(glyph_segment & 0xFFU);
    ram[at + 3] = static_cast<std::uint8_t>(glyph_segment >> 8U);
  }

  diagnostic_log log;
  std::unique_ptr<machine> box;
};

TEST(ScreenTextOnAMachine, NoProgramIsNoText) {
  rig r;
  text_grid grid{};
  EXPECT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::no_program);
  EXPECT_EQ(cell(grid, 0, 0).code, cell_not_text);
}

TEST(ScreenTextOnAMachine, AProgramWithNoFontLocationIsNoText) {
  rig r;
  sha256_digest other{};
  other.bytes[0] = 0x5A;
  r.load(other);
  r.install(make_font());

  text_grid grid{};
  EXPECT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::wrong_binary);
  EXPECT_STREQ(screen_text_trouble_name(screen_text_trouble::wrong_binary),
               "wrong_binary");
}

TEST(ScreenTextOnAMachine, AFontNotYetInstalledIsNoText) {
  rig r;
  r.load_baseline();

  text_grid grid{};
  EXPECT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::no_font);
  EXPECT_EQ(cell(grid, 0, 0).code, cell_not_text);
}

TEST(ScreenTextOnAMachine, ReadsTheScreenInTheFontTheProgramInstalled) {
  rig r;
  r.load_baseline();
  const program_font font = make_font();
  r.install(font);
  draw(r.box->display().writable_pixels(), font, 0, 24, "CHOOSE A FUNCTION", 13,
       0);

  text_grid grid{};
  ASSERT_EQ(read_screen_text(*r.box, grid), screen_text_trouble::none);
  EXPECT_EQ(read(grid, 0, 24, 17), "CHOOSE A FUNCTION");
  EXPECT_EQ(cell(grid, 0, 24).ink, 13);
}

TEST(ScreenTextOnAMachine, ReadingChangesNothing) {
  rig r;
  r.load_baseline();
  r.install(make_font());
  const state_hashes before = hash_state(*r.box);

  text_grid grid{};
  (void)read_screen_text(*r.box, grid);

  EXPECT_TRUE(hash_state(*r.box) == before);
}

}  // namespace
}  // namespace amberfolio::machine
