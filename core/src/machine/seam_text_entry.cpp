// SPDX-License-Identifier: AGPL-3.0-only
//
// The text-entry piece of `modern-controls` (seam_modern_controls.cpp): it
// notes when the program is inside its line editor, so a host can say so
// (#504, machine/text_entry.h).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses. Not one byte of the program is reproduced here.
//
// **The line editor** is one resident routine (`1709:09E3`, image
// `0x7A73`), and the program reads every line of text through it: a new
// character's name, a script's free-text answer, a script's number prompt
// and the code word at the copy-protection challenge (seam_edit_keys.cpp has
// the four callers and how they were found). It opens with `push bp`, loops
// on the program's key read until Return or Esc, then clears the message row,
// copies the line out and returns through **its one return**, a `retf 8` at
// `1709:0B63` (image `0x7BF3`). No other instruction leaves it: the loop's
// only exits are the two jumps to the clearing tail.
//
// The addresses by two routes: the resident disassembly (the prologue at
// `1709:09E3`, the single `retf` at `1709:0B63`, nothing between that leaves
// the routine) and the bytes of the unpacked image, `55 89 E5` at image
// `0x7A73` and `CA 08 00` at image `0x7BF3`.
//
//
// What the seam does
// ------------------
//
// **Two points, in the resident image**: the editor's first instruction and
// its return. The first sets one of the seam's own words, the second clears
// it. Neither reads or writes anything of the machine's, so with the seam on
// a run is the run it was, and the recordings made with it on before these
// points existed still verify.
//
// **Why a word of the seam's and not of the machine's.** It is observation,
// exactly as the overlay tracker is: a replay reaches the same two
// instructions and rebuilds it, and nothing that is serialized or hashed
// holds it. A seam's words are dropped by `enable()`, `reset()` and
// `clear()`, so a machine reset mid-line, or the seam switched off and on
// again outside the editor, starts from "not reading" rather than from a
// crossing nobody saw the other half of.
//
// **Why on `modern-controls`.** The site that asked (#504) turns the seam on
// for every player, and the edit keys already live inside this editor. With
// the seam off nothing watches, and `text_entry_now()` says `unknown`.
//
// **Esc as well as Return.** Both end the editor's loop through the same
// tail, so the line abandoned with Esc ends the reading as an accepted one
// does. What the caller then does (asks again, after a number that was not
// one) is a fresh entry, and the word goes up again.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// Nothing of the machine's is read or written: every recording with
// `modern-controls` on verifies as it did.
//
//
// What it is not yet
// ------------------
//
// Nothing outstanding.

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_entry.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// In the resident image, from the image segment: the line editor's first
/// instruction, and its one return.
constexpr std::uint32_t editor_entry = 0x7A73;
constexpr std::uint32_t editor_return = 0x7BF3;

void reading_begins(machine& /*box*/, seam_context& ctx) {
  ctx.set_scratch(text_entry_scratch, 1);
}

void reading_ends(machine& /*box*/, seam_context& ctx) {
  ctx.set_scratch(text_entry_scratch, 0);
}

constexpr std::array<seam_point, 2> text_entry_point_table{
    {{.module = resident_image, .offset = editor_entry, .run = &reading_begins},
     {.module = resident_image,
      .offset = editor_return,
      .run = &reading_ends}}};
static_assert(text_entry_point_table.size() == text_entry_point_count);

}  // namespace

std::span<const seam_point> text_entry_points() noexcept {
  return text_entry_point_table;
}

text_entry text_entry_now(const machine& box) noexcept {
  const seam_status status = box.seams().status(modern_controls_id);
  if (status.state != seam_state::on) {
    return text_entry::unknown;
  }
  return box.seams().scratch(modern_controls_id, text_entry_scratch) != 0
             ? text_entry::reading
             : text_entry::not_reading;
}

}  // namespace amberfolio::machine
