// SPDX-License-Identifier: AGPL-3.0-only
//
// The character generator: a picture of every character this machine's
// BIOS can draw, and the argument for why one exists here at all.
//
// M4 (#121). M3 wrote down the opposite position — "this machine has no
// video ROM ... and a font is picture data, which is not something this
// project ships" — and refused every request for one. That was the right
// answer while nothing had asked. Then the game got into character
// creation and asked twice in the same breath: `INT 10h AH=09h` to draw
// a character in a graphics mode, having installed no font of its own,
// and before that `AH=11h AL=30h` to find out where the generator is,
// which it took away as a pointer to one of this machine's IRET stubs.
// Both of those are a program using a *hardware* facility the adapter
// has always had, and a machine without one is missing a piece of an
// EGA rather than declining a piece of a game.
//
//
// Why this is not the thing the clean-content rule forbids
// --------------------------------------------------------
//
// CONTRIBUTING.md's rule is about material from the original games: no
// game code, no game data, no original byte sequences. A character
// generator is none of those — it was never in the game, it was in the
// adapter — and the bytes here are not transcribed from any ROM either.
// Every glyph in font.cpp was drawn for this project: five pixels wide
// on a seven-row body for the letters and digits, with the block, shade
// and line-drawing codes computed from their own geometry, because a
// full block genuinely is eight rows of eight lit pixels and there is no
// other thing it could be.
//
// So the text a program draws through this BIOS looks like *this*
// machine's font, not like IBM's. That is a real and visible difference
// and it is the honest one: the alternative was either shipping
// somebody else's bitmaps or refusing to draw at all.
//
//
// How a glyph is laid out
// -----------------------
//
// Eight bytes, one per scan line, top row first. Within a byte the
// leftmost pixel is bit 7 — the same order the EGA's own bit mask uses
// (ega.h), which is why int10.cpp can hand a glyph row straight to the
// Bit Mask register with no shuffling in between.
//
// The table is flat and indexed by code, so glyph *c* starts at
// `c * glyph_height`. `high_half_first` is where the top half begins,
// which is the half INT 1Fh has always addressed.
//
//
// Where it lives, and who points at it
// -------------------------------------
//
// In the emulated machine's own memory, in the BIOS region, put there by
// `service_floor::reset()` at `service::stub_segment:service::font_offset`
// along with the vector table and the stubs — because that is where a
// real adapter's font is, and because a program is entitled to read it,
// index it and hand its address to something else. Power-on then points
// **INT 43h** at the whole table and **INT 1Fh** at its top half, which
// is what a real BIOS leaves in those vectors and what makes
// `AH=11h AL=30h` able to answer truthfully.
//
// A program that installs a font of its own overwrites those vectors and
// this machine reads *its* table instead (int10.h's "Where the glyphs
// come from"). Unlike a real EGA BIOS, a later mode set does not point
// INT 43h back here: there is one font here and one mode that draws, so
// re-pointing could only ever throw away a program's own table for no
// gain.

#pragma once

#include <cstdint>
#include <span>

namespace amberfolio::machine::font {

/// Scan lines in a character cell, and so bytes in a glyph.
inline constexpr std::uint16_t glyph_height = 8;

/// Glyphs in the table: the whole code page, because a program may index
/// it with any byte and a gap would be a glyph-shaped lie.
inline constexpr unsigned glyph_count = 256;

inline constexpr std::uint16_t table_bytes =
    static_cast<std::uint16_t>(glyph_height * glyph_count);

/// The first code of the top half — the half INT 1Fh points at, and the
/// half no PC ROM ever carried (int10.h).
inline constexpr std::uint8_t high_half_first = 0x80;

/// The two vectors a BIOS leaves pointing at a character generator.
/// Neither is an entry point: INT 1Fh holds the address of the
/// graphics-mode glyphs for the top half of the code page, and INT 43h
/// the address of the current generator. A program stores a far pointer
/// there and the BIOS reads it back; nothing ever executes an INT to
/// either.
inline constexpr std::uint8_t high_half_vector = 0x1F;
inline constexpr std::uint8_t generator_vector = 0x43;

/// The glyphs, in code order. A view of static storage: the same bytes
/// every reset copies into the machine's BIOS region.
[[nodiscard]] std::span<const std::uint8_t> glyphs() noexcept;

}  // namespace amberfolio::machine::font
