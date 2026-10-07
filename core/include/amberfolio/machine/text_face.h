// SPDX-License-Identifier: AGPL-3.0-only
//
// Text faces: lettering a player may choose over the program's own, and
// the one question everything that reads or rasterizes the program's text
// has to ask — which face is it being drawn in right now.
//
//
// What a face is
// --------------
//
// Sixty-four glyphs in the program's own layout (screen_text.h): eight
// bytes a glyph, one byte a scanline, bit 0x80 the leftmost pixel,
// indexed by the character upper-cased modulo sixty-four. Every glyph
// here was drawn for this project, on an eight-by-eight grid, from
// nothing; none is transcribed from the program's font or from any other.
//
// **A letter sits in columns 1 to 7 and rows 0 to 6**, as the program's
// own do: the column on its left and the row under it are paper, the gap
// between letters and between lines, save for the punctuation that hangs
// below the line. A selection drawn inverted (seam_select_yellow.cpp)
// keeps that column and row as its margin inside the cell (#483).
//
// **A face replaces the letters, the digits and the punctuation, and
// nothing else.** Nine of the program's sixty-four text glyphs are not
// lettering at all — a frame corner where `@` would be, frame pieces and
// solid blocks at `[ \ ] ^ _ $ %` — and the program reaches them through
// the same table as its text. Those, and the space, stay the program's:
// `replaces()` is false for them, and a seam that consults a face draws
// the program's own glyph there. Everything past glyph sixty-three is
// pictures (the runes, the rope border) and no face goes near it.
//
//
// Who draws in one
// ----------------
//
// The `font-*` seams (seam_font.cpp), one per face, which swap each glyph
// row the program fetches as it fetches it. The program's font buffer is
// never written, so a face switched off is gone at the next glyph drawn.
// The seams are alternatives (`seam_definition::group`): at most one is
// on.
//
// Two other readers follow the face rather than the buffer, and both
// through `drawing()`: screen text (screen_text.h), which would otherwise
// read every replaced cell as not text, and the automap's zone label,
// which rasterizes the program's font itself and would otherwise letter
// one band of the screen differently from the rest.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace amberfolio::machine {

class seam_engine;

namespace text_face {

/// The lettering on the screen.
enum class face : std::uint8_t {
  /// The program's own, from its own buffer. No seam is drawing.
  program,
  /// A plain bold sans, two-pixel stems, slashed zero.
  sans,
  /// The sans cut as if by a broad pen held at forty-five degrees.
  chisel,
};

/// Glyphs in a face, and bytes in a glyph: the program's text table.
inline constexpr unsigned glyph_count = 64;
inline constexpr unsigned glyph_bytes = 8;
inline constexpr std::size_t table_bytes =
    std::size_t{glyph_count} * glyph_bytes;

/// Whether a face draws program glyph `index`, or leaves it the program's.
/// False past the text table.
[[nodiscard]] bool replaces(unsigned index) noexcept;

/// Row `row` of glyph `index` in `which`. Only meaningful where
/// `replaces(index)` and `which` is not `face::program`; zero elsewhere.
[[nodiscard]] std::uint8_t row(face which, unsigned index,
                               unsigned row) noexcept;

/// The seam that draws `which`, or empty for `face::program`.
[[nodiscard]] std::string_view seam_id(face which) noexcept;

/// The face the program's text is being drawn in on this machine now:
/// the face of the first `font-*` seam that is on and armed, or the
/// program's own.
[[nodiscard]] face drawing(const seam_engine& seams) noexcept;

/// `program`, a copy of the program's text table (`table_bytes` long), as
/// `which` draws it: every glyph the face replaces written over. Nothing
/// for `face::program` or a table of another length.
void apply(face which, std::span<std::uint8_t> program) noexcept;

}  // namespace text_face
}  // namespace amberfolio::machine
