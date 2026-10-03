// SPDX-License-Identifier: AGPL-3.0-only
//
// The list-arrows seam: the up and down arrows step the highlight in the
// program's pick-lists, as Home and End already do (#423).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and scan codes. Not one byte of the
// program is reproduced here.
//
// **There is one vertical pick-list routine**, in overlay 25, and every
// list in the game goes through it: race, gender, class and alignment at
// character creation, the shops, training, coin selection, adding to the
// party, the encounter lists, the camp's Display. It draws the rows,
// keeps a highlight, and reads its keys through the program's menu-bar
// routine, in the same overlay, **in raw mode**.
//
// In raw mode the menu-bar routine hands a key it has no command for
// back as itself, and says so through an out-parameter, a byte in the
// caller's frame whose address the caller passed: one when the key is
// raw, zero when it is a bar command or a confirmation. An extended key
// (an arrow, Home, End, a page key) comes back as its scan code with the
// byte set. The list acts on four of them and ignores the rest:
//
//   * `0x47` (Home) steps up one row, `0x4F` (End) steps down one;
//   * `0x49` (PgUp) and `0x51` (PgDn) page, when a page is there.
//
// Up (`0x48`) and Down (`0x50`) come back the same way and are dropped.
// With the byte *clear*, `0x50` is the bar's own `P`, the Prev command,
// which is why the byte is read before anything is rewritten.
//
// The same routine translates the keypad: with NumLock on, 8 and 2 come
// back as `0x48` and `0x50` with the byte set, and 7 and 1 as `0x47` and
// `0x4F`. The program cannot tell the number row from the keypad (both
// deliver the same character), so the digits 7 and 1 already step a list
// and, with this seam, 8 and 2 do too.
//
// **The party-member picker** (Trade's receiver, "Cast Spell on whom",
// a script's party pick) lives in the resident image. It reads through
// the same menu-bar routine, in raw mode, and steps on `0x4F` to the
// next member and `0x47` to the previous one, dropping the arrows in the
// same way. Its out-parameter is a byte in its own frame.
//
//
// What the seam does
// ------------------
//
// **Two points, each where the menu-bar routine returns to its caller**,
// with AL the key. The handler runs before the instruction that stores
// AL, so a rewrite is what the caller sees:
//
//   * the byte is set and AL is `0x48`: AL becomes `0x47`;
//   * the byte is set and AL is `0x50`: AL becomes `0x4F`;
//   * anything else: nothing is touched.
//
// The program's own stepper does the rest, so the arrows wrap, skip
// titles and scroll exactly as Home and End do.
//
// **The scope is positive rather than inferred.** The arrows move the
// party in 3D, in the wilderness and in combat, and a host cannot tell
// from outside when one is free. These two points are reached only from
// inside a pick-list and a picker, so no other screen's arrow is ever
// offered to the handler. A key is read only when AL is an arrow, so a
// list driven with Home and End alone costs the seam not one byte read.
//
// **Checks the byte is a byte the routine writes.** The menu-bar routine
// sets it to zero or one and nothing else. Any other value is not the
// frame these facts describe, and the handler declines and touches
// nothing.
//
// **Qualified as overlay 25.** The list's point is in the overlay and is
// resolved through the program's own word for where it is (seam.h,
// `seam_module::load_segment_at`). The module is identified by its bytes
// as read. The store release's GAME.OVR differs from the repack's only
// inside overlay 2 (docs/seams.md §5), so this module's digest is the
// same on both.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On and no arrow pressed at a list, the run is byte for byte the run
// with the seam off: the handler reads nothing and writes nothing unless
// AL is an arrow. The pair is an `identical` and a `contrast`
// (tests/sessions/README.md).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// Nothing outstanding for the pick-lists. Out of scope, filed apart
// (#423): Enter on the Yes/No prompt, arrows and Enter on the horizontal
// bars, and a held key scrolling (the hosts drop OS key repeats).

#include <array>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> list_arrows_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The module the pick-list lives in -------------------------------------

/// Where the program keeps this module's load segment: the offset, in the
/// resident image, of one word (overlay.h, `seam_module::load_segment_at`).
/// Found the way `seam_cheats.cpp` documents: search the resident image
/// for the manager's record with the module's file offset and length, one
/// match, and take the word sixteen bytes into the record. The method
/// returns the known words for overlays 8 (`0x360`) and 15 (`0x760`).
constexpr std::uint32_t overlay_load_segment_at = 0x3C60;

/// Overlay 25: the menu-bar routine, the vertical pick-list and the Yes/No
/// prompt. The facts are the module's row in the overlay file's own table
/// and the manager's record of the same two numbers in the resident
/// image; the digest is of the bytes as read.
constexpr seam_module list_module{
    .file = "GAME.OVR",
    .file_offset = 182479,
    .length = 4682,
    .digest =
        "175454bc2f527dd6757c89eaa50a6cdd27a9cf5d3aaa197b33a140f5b09a3901",
    .load_segment_at = overlay_load_segment_at};

/// In the pick-list routine: the instruction after its call into the
/// menu-bar routine, which stores the key. Offset from the module's
/// start.
constexpr std::uint32_t list_after_input = 0x0FE0;

/// The out-parameter's address in that routine's frame, below BP.
constexpr std::uint16_t list_flag_below_bp = 0x57;

// --- The party-member picker -----------------------------------------------

/// In the resident image: the instruction after the picker's call into
/// the menu-bar routine, which stores the key. Offset from the image
/// segment.
constexpr std::uint32_t picker_after_input = 0x38AA;

/// The out-parameter's address in the picker's frame, below BP.
constexpr std::uint16_t picker_flag_below_bp = 0x2B;

// --- The keys --------------------------------------------------------------

constexpr std::uint8_t scan_home = 0x47;
constexpr std::uint8_t scan_up = 0x48;
constexpr std::uint8_t scan_end = 0x4F;
constexpr std::uint8_t scan_down = 0x50;

/// The menu-bar routine has just returned AL, with its out-parameter at
/// SS:BP-`flag_below_bp`. If the byte says the key is a raw one and the
/// key is an arrow, hand back Home or End instead.
void step_like_home_and_end(machine& box, seam_context& ctx,
                            std::uint16_t flag_below_bp) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  const std::uint8_t key = regs.get(cpu::reg8::al);
  if (key != scan_up && key != scan_down) {
    return;
  }

  const std::uint8_t raw = cpu.read_byte(
      regs[cpu::sreg::ss],
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - flag_below_bp));
  if (raw == 0) {
    // A bar command, not a raw key: 0x50 here is the bar's own Prev.
    return;
  }
  if (raw != 1) {
    // The routine writes zero or one. Not the frame these facts describe.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  regs.set(cpu::reg8::al, key == scan_up ? scan_home : scan_end);
}

void step_the_list(machine& box, seam_context& ctx) {
  step_like_home_and_end(box, ctx, list_flag_below_bp);
}

void step_the_picker(machine& box, seam_context& ctx) {
  step_like_home_and_end(box, ctx, picker_flag_below_bp);
}

constexpr std::array<seam_point, 2> list_arrows_points{
    {{.module = list_module, .offset = list_after_input, .run = &step_the_list},
     {.module = resident_image,
      .offset = picker_after_input,
      .run = &step_the_picker}}};

constexpr seam_definition list_arrows_definition{
    .id = "list-arrows",
    .about =
        "the up and down arrows step the game's pick-lists, as Home and "
        "End do",
    .fingerprints = list_arrows_binaries,
    .points = list_arrows_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& list_arrows_seam() noexcept {
  return list_arrows_definition;
}

}  // namespace amberfolio::machine
