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
// **Writes nothing but DL.** The program's font buffer is never touched,
// which is what lets the face come and go between one glyph and the next:
// switched off, the very next row fetched is the program's own, and the
// screen goes back as the program repaints it. Nothing about a face is
// machine state but the pixels it drew.
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
// and the handler declines and touches nothing.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On, a font seam is **seen from the first character drawn**: there is no
// "on and untriggered" for it, because the program draws text from its
// first screen on. Its pair is a `contrast` (`quiet-font-sans`,
// `quiet-font-chisel` against `quiet`), and what the contrast may differ
// in is the display, the video memory and DL.
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
  /// pictures past it. ES:DI beyond the last of them is not a glyph
  /// fetch.
  std::uint32_t buffer_glyphs;
  /// The instruction after each of the blitter's two EGA row fetches, as
  /// an offset from the image segment. DL holds the row; ES:DI is on it.
  std::array<std::uint32_t, 2> row_fetched_offsets;
};

/// Pool of Radiance, the baseline edition.
constexpr font_facts pool_of_radiance_font{
    .profile = pool_of_radiance,
    .buffer_glyphs = 177,
    .row_fetched_offsets = {0x749A, 0x74C0}};

constexpr auto font_binaries = fingerprints_of(std::array{&pool_of_radiance});

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

/// Both fetches, swapped into `Which`.
template <const font_facts& F, text_face::face Which>
constexpr std::array<seam_point, 2> fetched_row_points{
    {{.module = resident_image,
      .offset = F.row_fetched_offsets[0],
      .run = &swap_row<F, Which>,
      .inside_calls = true},
     {.module = resident_image,
      .offset = F.row_fetched_offsets[1],
      .run = &swap_row<F, Which>,
      .inside_calls = true}}};

constexpr const auto& sans_points =
    fetched_row_points<pool_of_radiance_font, text_face::face::sans>;
constexpr const auto& chisel_points =
    fetched_row_points<pool_of_radiance_font, text_face::face::chisel>;

/// The group the faces share: at most one is on.
constexpr std::string_view font_group = "font";

constexpr seam_definition sans_definition{
    .id = "font-sans",
    .about = "the game's lettering in a plain bold sans",
    .fingerprints = font_binaries,
    .points = sans_points,
    .group = font_group,
    .schema = seam_schema_version};

constexpr seam_definition chisel_definition{
    .id = "font-chisel",
    .about = "the game's lettering in a bold face cut as if by a broad pen",
    .fingerprints = font_binaries,
    .points = chisel_points,
    .group = font_group,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& font_sans_seam() noexcept { return sans_definition; }

const seam_definition& font_chisel_seam() noexcept { return chisel_definition; }

}  // namespace amberfolio::machine
