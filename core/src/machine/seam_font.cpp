// SPDX-License-Identifier: AGPL-3.0-only
//
// The font seams: the program's lettering in a face of the player's
// choosing (text_face.h), one seam a face, alternatives in one group.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses and offsets. Not one byte of the program, and not
// one of its glyphs, is reproduced here.
//
// **Every character the program puts on the screen goes through one
// routine**, its glyph blitter, in the resident image. It takes a
// character, a colour and a cell, folds the character to an index in its
// font, and copies the glyph's eight rows to the screen one row at a
// time. The font is a buffer the program allocates and fills from its own
// data files at boot, and finds through a far pointer at
// `data_font_pointer` in its data segment — the same pointer the
// automap's zone label and screen text follow (seams.md §10).
//
// The blitter has a path per adapter. On the EGA, which is the only one
// this machine has, it fetches each row of the glyph into DL with ES:DI
// on the byte — the far pointer's segment in ES, its offset plus eight
// times the index plus the row in DI — and stores it to the display, once
// per plane the colour lights, to both of the two pages it keeps. There
// are **two such fetches**, one per page, and `row_fetched_offsets` is
// the instruction after each: the point at which DL holds the row and
// nothing has been done with it yet.
//
// **Curse of the Azure Bonds draws from a copy** (#555). Its font is the
// same shape, but its blitter first copies the glyph's eight bytes into
// eight of its own in the data segment and then calls a path per
// adapter, which draws from the copy. The EGA path draws one page, and
// reads each row into AH with BX the row's number; the glyph's index is
// the path's first argument. Its one point is the instruction after
// that read (`row_copied_offset`), and its handler is its own.
//
//
// What the seam does
// ------------------
//
// **Swaps the row.** At either point, if ES:DI is inside the font and
// the glyph is one the face replaces (`text_face::replaces`), DL becomes
// the face's row; otherwise it is left alone. A picture glyph (index
// sixty-four and up, the runes and the rope) and the nine text glyphs
// that are not lettering stay the program's.
//
// **Writes nothing but DL** (AH on Curse). The program's font buffer is
// never touched, which is what lets the face come and go between one
// glyph and the next: switched off, the very next row fetched is the
// program's own, and the screen goes back as the program repaints it.
// Nothing about a face is machine state but the pixels it drew.
//
// **Runs inside other seams' calls** (`seam_point::inside_calls`). The
// journal draws its pages, and paints the screen back when it closes,
// through the program's own string and frame routines in a batch, and
// the engine offers no ordinary point while one runs. These points are
// the blitter's own instructions whoever called it, and the handler
// edits DL and nothing else, so they are offered there too; without it
// the journal and the screen it hands back were lettered in the
// program's face with a face on (#419).
//
// **Checks the fetch is the fetch.** The far pointer is read out of the
// data segment the facts name, and ES:DI has to fall inside the glyphs it
// points at. Anything else is not the instruction these facts describe,
// and the handler declines and touches nothing. On Curse, where the row
// comes out of a copy, DS has to be the data segment, BX a row, the
// index a glyph, and AH that glyph's row in the font.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On, a font seam is **seen from the first character drawn**: there is no
// "on and untriggered" for it, because the program draws text from its
// first screen on. Its pair is a `contrast` (`quiet-font-sans`,
// `quiet-font-chisel` against `quiet`), and what the contrast may differ
// in is the display, the video memory and DL. On Curse the pair is
// `curse-quiet-font-sans` and `curse-quiet-font-chisel` against
// `curse-quiet`, and the register is AH, which an interrupt taken in the
// middle of a glyph pushes: one stale word of the stack differs too.
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// **Text already on the screen keeps its old face** until the program
// draws it again. A switch mid-game shows in the next menu, the next step
// or the next message; repainting the screen at the switch is a separate
// piece of work.

#include <array>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_face.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// One program's facts: where its blitter has just fetched a glyph row,
/// and the buffer the row came out of.
struct font_facts {
  const program_profile& profile;
  /// Glyphs the program's font buffer holds: its text table and the
  /// pictures past it. A glyph beyond the last of them is not one.
  std::uint32_t buffer_glyphs;
  /// The instruction after each of the blitter's two EGA row fetches out
  /// of the font, as an offset from the image segment. DL holds the row;
  /// ES:DI is on it. Zero on a program whose blitter reads a copy.
  std::array<std::uint32_t, 2> row_fetched_offsets;
  /// The instruction after the EGA row read out of the glyph's copy, as
  /// an offset from the image segment: AH holds the row, BX is its
  /// number. Zero on a program whose blitter reads the font itself.
  std::uint32_t row_copied_offset;
  /// Where the glyph's index is at that point: a byte this far above BP,
  /// the argument the blitter's EGA path was called with.
  std::uint16_t index_above_bp;
};

/// Pool of Radiance, the baseline edition.
constexpr font_facts pool_of_radiance_font{
    .profile = pool_of_radiance,
    .buffer_glyphs = 177,
    .row_fetched_offsets = {0x749A, 0x74C0},
    .row_copied_offset = 0,
    .index_above_bp = 0};

/// Curse of the Azure Bonds (#555). The same font, laid out the same way:
/// 177 glyphs of eight bytes, the text table first, a character's index
/// its upper case modulo sixty-four, and the same nine text slots and the
/// space holding something other than lettering. The blitter differs.
/// It copies the glyph's eight bytes out of the font into eight bytes of
/// its own in the data segment, just past the font's far pointer, and
/// then calls its EGA path with the glyph's index as the first argument;
/// that path draws the cell on one page only, a row at a time out of the
/// copy.
constexpr font_facts curse_font{.profile = curse_of_the_azure_bonds,
                                .buffer_glyphs = 177,
                                .row_fetched_offsets = {},
                                .row_copied_offset = 0x5A91,
                                .index_above_bp = 4};

constexpr auto font_binaries =
    fingerprints_of(std::array{&pool_of_radiance, &curse_of_the_azure_bonds});

/// Swap the row in DL for `Which`'s, if the glyph is one it replaces.
template <const font_facts& F, text_face::face Which>
void swap_row(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  const auto dgroup = static_cast<std::uint16_t>((ctx.image_base() / 16U) +
                                                 F.profile.dgroup_paragraphs);
  const std::uint16_t font_offset =
      cpu.read_word(dgroup, F.profile.data_font_pointer);
  const std::uint16_t font_segment = cpu.read_word(
      dgroup, static_cast<std::uint16_t>(F.profile.data_font_pointer + 2));
  const std::uint16_t es = regs[cpu::sreg::es];
  const std::uint16_t di = regs[cpu::reg16::di];
  if (font_segment == 0 || es != font_segment || di < font_offset ||
      std::uint32_t{di} - font_offset >=
          F.buffer_glyphs * text_face::glyph_bytes) {
    // Not the fetch these facts describe. Touch nothing.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint32_t byte = std::uint32_t{di} - font_offset;
  const auto index = static_cast<unsigned>(byte / text_face::glyph_bytes);
  if (!text_face::replaces(index)) {
    // A picture, or a text glyph that is not lettering: the program's.
    return;
  }
  regs.set(
      cpu::reg8::dl,
      text_face::row(Which, index,
                     static_cast<unsigned>(byte % text_face::glyph_bytes)));
}

/// Swap the row in AH, read out of the glyph's copy, for `Which`'s, if
/// the glyph is one it replaces (Curse's blitter).
template <const font_facts& F, text_face::face Which>
void swap_copied_row(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  const auto dgroup = static_cast<std::uint16_t>((ctx.image_base() / 16U) +
                                                 F.profile.dgroup_paragraphs);
  const std::uint16_t font_offset =
      cpu.read_word(dgroup, F.profile.data_font_pointer);
  const std::uint16_t font_segment = cpu.read_word(
      dgroup, static_cast<std::uint16_t>(F.profile.data_font_pointer + 2));
  const std::uint16_t row = regs[cpu::reg16::bx];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  if (font_segment == 0 || regs[cpu::sreg::ds] != dgroup ||
      row >= text_face::glyph_bytes || bp > 0xFFFFU - F.index_above_bp) {
    // Not the read these facts describe. Touch nothing.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const unsigned index = cpu.read_byte(
      regs[cpu::sreg::ss], static_cast<std::uint16_t>(bp + F.index_above_bp));
  const std::uint32_t byte = (index * text_face::glyph_bytes) + row;
  if (index >= F.buffer_glyphs || font_offset + byte > 0xFFFFU ||
      cpu.read_byte(font_segment,
                    static_cast<std::uint16_t>(font_offset + byte)) !=
          regs.get(cpu::reg8::ah)) {
    // The index is not a glyph, or the row in AH is not that glyph's:
    // either way not the copy these facts describe.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!text_face::replaces(index)) {
    // A picture, or a text glyph that is not lettering: the program's.
    return;
  }
  regs.set(cpu::reg8::ah, text_face::row(Which, index, row));
}

/// Both fetches, swapped by `Run`.
template <const font_facts& F, seam_handler Run>
constexpr std::array<seam_point, 2> fetched_row_points{
    {{.module = resident_image,
      .offset = F.row_fetched_offsets[0],
      .run = Run,
      .inside_calls = true},
     {.module = resident_image,
      .offset = F.row_fetched_offsets[1],
      .run = Run,
      .inside_calls = true}}};

constexpr const auto& sans_points =
    fetched_row_points<pool_of_radiance_font,
                       &swap_row<pool_of_radiance_font, text_face::face::sans>>;
constexpr const auto& chisel_points = fetched_row_points<
    pool_of_radiance_font,
    &swap_row<pool_of_radiance_font, text_face::face::chisel>>;

/// The one read out of the copy, swapped by `Run`.
template <const font_facts& F, seam_handler Run>
constexpr std::array<seam_point, 1> copied_row_points{
    {{.module = resident_image,
      .offset = F.row_copied_offset,
      .run = Run,
      .inside_calls = true}}};

/// The points on every program but the baseline, whose points are the
/// definition's own (seam.h, schema 9).
constexpr std::array<seam_program_points, 1> sans_programs{
    {{.fingerprint = curse_of_the_azure_bonds.fingerprint,
      .points = copied_row_points<
          curse_font, &swap_copied_row<curse_font, text_face::face::sans>>}}};
constexpr std::array<seam_program_points, 1> chisel_programs{
    {{.fingerprint = curse_of_the_azure_bonds.fingerprint,
      .points = copied_row_points<
          curse_font, &swap_copied_row<curse_font, text_face::face::chisel>>}}};

/// The group the faces share: at most one is on.
constexpr std::string_view font_group = "font";

constexpr seam_definition sans_definition{
    .id = "font-sans",
    .about = "the game's lettering in a plain bold sans",
    .fingerprints = font_binaries,
    .points = sans_points,
    .programs = sans_programs,
    .group = font_group,
    .schema = seam_schema_version};

constexpr seam_definition chisel_definition{
    .id = "font-chisel",
    .about = "the game's lettering in a bold face cut as if by a broad pen",
    .fingerprints = font_binaries,
    .points = chisel_points,
    .programs = chisel_programs,
    .group = font_group,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& font_sans_seam() noexcept { return sans_definition; }

const seam_definition& font_chisel_seam() noexcept { return chisel_definition; }

}  // namespace amberfolio::machine
