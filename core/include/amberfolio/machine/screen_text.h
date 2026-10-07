// SPDX-License-Identifier: AGPL-3.0-only
//
// Screen text: what the program has on the screen right now, read back as
// characters by matching the frame against the glyphs it was drawn with.
//
// A host that cannot look at a picture — a screen reader, an assistant
// driving the game for a player, a test that wants to say what a screen
// *says* rather than what it hashes to — wants the same answer, and none
// of them should rasterize a font to get it. So the core answers it once,
// on demand, between runs: nothing here runs per frame, nothing is kept,
// and nothing about the machine changes. It is a read, like the
// framebuffer is.
//
//
// Where the glyphs come from
// --------------------------
//
// From the machine, at call time, and from nowhere else. The program's
// own 8x8 font is read out of its memory through the data-segment far
// pointer the automap's zone label already follows (seams.md §10,
// M5-E2b): sixty-four glyphs indexed by the character upper-cased modulo
// sixty-four, so every character read back is upper case. Nothing about
// the font is stored, committed or shipped.
//
// **Or the face a font seam is drawing it in** (text_face.h): with one
// on, a cell is read against the program's table with the face's glyphs
// written over it first, and against the program's own after that, for
// text drawn before the switch. Either way every character read back is
// the program's character, not the face's.
//
// Only that font. Every screen from the credits through character
// creation was measured with this machine's own BIOS font (font.h) as a
// second table, and not one cell matched it that the program's font had
// not; a second table only adds glyphs for artwork to be mistaken for.
//
// Where the font pointer is is a fact about a binary, like a seam's
// addresses: a program this build has no location for gets no text and a
// reason, never a guess.
//
//
// What a cell is
// --------------
//
// The frame is cut into the 8x8 grid the program's text lies on, forty
// columns by twenty-five rows. A cell whose pixels are exactly two
// colours is text when one of those colours, taken as the ink, draws a
// glyph exactly; the other is the paper. A cell of one colour is a blank
// in that colour. Anything else — three colours or more, or two that draw
// no glyph — is not text, and says so.
//
// Two glyphs can have the same bitmap. When a cell matches more than one
// character the answer is `cell_ambiguous`, not the first of them: an
// `O` read as a `0` is exactly the misreading this exists to rule out.
//
// **A cell that does not read is read again without its column 0 and its
// row 7**, blanked out of the cell and of every glyph. A selection block
// draws its margin there in the cells above it and after it
// (selection_margin.h, #483), and those pixels are paper in every text glyph
// but a descender or two. The second reading counts only when it is one
// character or a blank; a cell that reads the first time reads exactly as
// before.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/machine/platform.h"

namespace amberfolio::machine {

class machine;

/// The text grid: the frame in 8x8 cells.
inline constexpr unsigned text_cell_pixels = 8;
inline constexpr unsigned text_columns = frame_width / text_cell_pixels;
inline constexpr unsigned text_rows = frame_height / text_cell_pixels;
inline constexpr std::size_t text_cells = std::size_t{text_columns} * text_rows;

/// The program's font: sixty-four glyphs of eight bytes, one byte to a
/// scanline, bit 0x80 the leftmost pixel.
inline constexpr unsigned program_font_glyphs = 64;
inline constexpr std::size_t program_font_bytes =
    std::size_t{program_font_glyphs} * text_cell_pixels;

/// A cell's `code` when it is not text.
inline constexpr std::uint8_t cell_not_text = 0x00;
/// A cell's `code` when its bitmap is more than one character's.
inline constexpr std::uint8_t cell_ambiguous = 0x01;

/// One cell. `code` is the character as ASCII (0x20-0x5F), or one of the
/// two markers above. `ink` and `paper` are palette indices 0-15; a blank
/// cell has them equal, and a cell that is not text has them both zero.
struct text_cell {
  std::uint8_t code{cell_not_text};
  std::uint8_t ink{0};
  std::uint8_t paper{0};
};

using text_grid = std::array<text_cell, text_cells>;

/// Why there is no text. The first two are the words the seams use for
/// the same two situations (`seam_reason_name()`).
enum class screen_text_trouble : std::uint8_t {
  none,
  /// No program is known to the machine.
  no_program,
  /// A program this build has no font location for.
  wrong_binary,
  /// The program is known, and has not installed its font yet — its own
  /// text primitive draws nothing in that state either.
  no_font,
};

[[nodiscard]] const char* screen_text_trouble_name(
    screen_text_trouble trouble) noexcept;

/// Read every cell of `pixels` (a `frame_width * frame_height` frame of
/// palette indices) against `program_font`, `program_font_bytes` of
/// glyphs. The matcher alone: no machine, no program facts.
///
/// `drawn_font`, when it is not empty, is the table the program's text
/// is being drawn in now (a face, text_face.h), tried first; a cell it
/// finds nothing in is tried against `program_font`, which is what text
/// drawn before the face changed is still in.
void read_text_cells(std::span<const std::uint8_t> pixels,
                     std::span<const std::uint8_t> program_font, text_grid& out,
                     std::span<const std::uint8_t> drawn_font = {}) noexcept;

/// The text on `box`'s screen now. Every cell is `cell_not_text` unless
/// the answer is `none`.
[[nodiscard]] screen_text_trouble read_screen_text(const machine& box,
                                                   text_grid& out) noexcept;

}  // namespace amberfolio::machine
