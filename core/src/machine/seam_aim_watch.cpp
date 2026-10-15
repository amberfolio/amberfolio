// SPDX-License-Identifier: AGPL-3.0-only
//
// The aim-watch piece of `modern-controls` (seam_modern_controls.cpp): it
// notes where the combat aim bar's routine keeps its frame while the bar
// waits for a key, so the scene can say where the cursor is, who is under
// it and how far away (#523, machine/scene.h).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses and offsets. Not one byte of the program is
// reproduced here.
//
// **The aim command is overlay 13** (the module below; the overlay
// manager keeps its load segment in the word at image `0x690`, the word
// `bar-keys` names the aim bar's caller by). It aims two ways, each a
// routine of its own that waits for a key through the program's menu-bar
// routine, each with a `push bp; mov bp,sp` frame:
//
//   * **From target to target**, the routine at `0x2AA0`. Its caller
//     hands it the target's record as a far pointer at `[BP+0x0C]`, the
//     acting figure's at `[BP+0x10]`, the weapon's reach at `[BP+0x0A]`
//     and whether this is an attack at `[BP+8]` (a spell's aim passes 0,
//     and then no range is printed). It measures the range into the
//     byte at `[BP-0x30]` and prints it, then builds the word Target into
//     its bar string at `[BP-0x2A]` (a length byte, then the letters)
//     only where the target may be aimed at. All of that is decided at
//     `0x2BB6`, where the branches meet; from there the bar string is
//     rebuilt as the whole bar, and the call into the menu-bar routine
//     returns at `0x2C31`.
//   * **Manual**, the routine at `0x2DFA`, a cursor the arrows move. Its
//     column is the byte at `[BP-0x33]` and its row the byte at
//     `[BP-0x34]`. Each pass runs a line from the acting figure to the
//     cursor and sets the byte at `[BP-0x39]` where the line arrives,
//     printing the range only then and only for an attack (`[BP+0x10]`).
//     At `0x2F84`, where both branches meet, the range word at `[BP-0x38]`
//     is halved; from there the record the panel shows, the figure on the
//     cursor's square or a body lying there, is put in the far pointer at
//     `[BP+0x14]`, zero for none. The line's byte is then cleared wherever
//     Target is not to be offered, and the branches meet again at `0x310A`,
//     where the bar is built from it; the call into the menu-bar routine
//     returns at `0x3178`.
//
// The two returns are the ones `bar-keys` names those two bars' calls by,
// found there through the menu-bar routine's frame, which is their second
// route. The offsets, by two routes: overlay 13's disassembly (the frame
// built at each entry; the target pointer handed to the sheet drawer and
// compared with the acting figure's; the range byte printed and compared
// with the reach; the cursor's two bytes clamped to the grid's 50 by 25
// and compared with a body's column and row) and the module's own bytes at
// each point.
//
//
// What the seam does
// ------------------
//
// **Five points, in overlay 13**: the two meeting points just before each
// wait, which note the mode, the stack segment and BP, and the decisions
// the frame will not hold by the time anyone reads it (the bar string has
// been rebuilt by then, and the manual line's byte cleared); the halving in
// manual, which notes whether the line arrived; and each call's return,
// which notes the wait over.
//
// **Why the frame and not a copy.** While the bar waits, the routine's
// frame is the program's own record of the cursor, the panel's record and
// the range, and it stays put until the call returns. A seam that copied
// them would hold a second set to go stale; the scene reads the one the
// program keeps. What the seam keeps is what the machine has stopped
// holding where anyone can find it: where that frame is (BP has moved on
// by the time a host asks), and the two decisions above.
//
// **Observation, like `text-entry`'s.** Its words are dropped by
// `enable()`, `reset()` and `clear()`, so a machine reset mid-aim, or the
// seam switched off and on again, starts from "not aiming" rather than
// from a frame nobody saw the end of. A replay reaches the same points
// and notes them again.
//
// **Only between the meeting point and the return.** The bar is up for
// longer than that, but between the keys the routine is drawing, and the
// frame is being rewritten under a reader. The span noted is the one in
// which the program is waiting for the player, which is the one in which
// a host asks.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// The handlers read the routine's frame and write nothing of the
// machine's: every recording with `modern-controls` on verifies as it did.
//
//
// What it is not yet
// ------------------
//
// Nothing outstanding.

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/scene.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// Overlay 13, the aim command, and where the program keeps its segment.
constexpr seam_module aim_module{
    .file = "GAME.OVR",
    .file_offset = 75771,
    .length = 15474,
    .digest =
        "19eb3541795a718bf02cc1db08ef2802c286b620c440269f36793e0829464578",
    .load_segment_at = 0x690};

/// In the module: target to target's meeting point and its wait's return;
/// manual's halving, meeting point and its wait's return.
constexpr std::uint32_t target_decided = 0x2BB6;
constexpr std::uint32_t target_returned = 0x2C31;
constexpr std::uint32_t manual_line_ends = 0x2F84;
constexpr std::uint32_t manual_decided = 0x310A;
constexpr std::uint32_t manual_returned = 0x3178;

/// The frames, from BP: target to target's attack flag and bar string;
/// manual's attack flag and line byte.
constexpr int target_attack = 0x08;
constexpr int target_bar = -0x2A;
constexpr int manual_attack = 0x10;
constexpr int manual_line = -0x39;

struct frame {
  std::uint16_t segment;
  std::uint16_t bp;
};

[[nodiscard]] frame frame_now(machine& box) noexcept {
  const cpu::registers& r = box.processor().regs();
  return {.segment = r[cpu::sreg::ss], .bp = r[cpu::reg16::bp]};
}

[[nodiscard]] std::uint8_t byte_at(machine& box, frame at, int offset) {
  return box.processor().read_byte(at.segment,
                                   static_cast<std::uint16_t>(at.bp + offset));
}

void note(seam_context& ctx, scene_aim_mode mode, frame at,
          std::uint16_t flags) {
  ctx.set_scratch(aim_segment_scratch, at.segment);
  ctx.set_scratch(aim_frame_scratch, at.bp);
  ctx.set_scratch(aim_flags_scratch, flags);
  ctx.set_scratch(aim_mode_scratch, static_cast<std::uint16_t>(mode));
}

void target_bar_up(machine& box, seam_context& ctx) {
  const frame at = frame_now(box);
  std::uint16_t flags = 0;
  if (byte_at(box, at, target_bar) != 0) {
    flags |= aim_offered;
  }
  if (byte_at(box, at, target_attack) != 0) {
    flags |= aim_range_shown;
  }
  note(ctx, scene_aim_mode::target, at, flags);
}

void manual_line_noted(machine& box, seam_context& ctx) {
  const frame at = frame_now(box);
  const bool shown = byte_at(box, at, manual_line) != 0 &&
                     byte_at(box, at, manual_attack) != 0;
  ctx.set_scratch(aim_flags_scratch, shown ? aim_range_shown : 0);
}

void manual_bar_up(machine& box, seam_context& ctx) {
  const frame at = frame_now(box);
  auto flags = static_cast<std::uint16_t>(ctx.scratch(aim_flags_scratch) &
                                          aim_range_shown);
  if (byte_at(box, at, manual_line) != 0) {
    flags |= aim_offered;
  }
  note(ctx, scene_aim_mode::manual, at, flags);
}

void bar_down(machine& /*box*/, seam_context& ctx) {
  ctx.set_scratch(aim_mode_scratch,
                  static_cast<std::uint16_t>(scene_aim_mode::none));
}

constexpr std::array<seam_point, 5> aim_watch_point_table{{
    {.module = aim_module, .offset = target_decided, .run = &target_bar_up},
    {.module = aim_module, .offset = target_returned, .run = &bar_down},
    {.module = aim_module,
     .offset = manual_line_ends,
     .run = &manual_line_noted},
    {.module = aim_module, .offset = manual_decided, .run = &manual_bar_up},
    {.module = aim_module, .offset = manual_returned, .run = &bar_down},
}};
static_assert(aim_watch_point_table.size() == aim_watch_point_count);

}  // namespace

std::span<const seam_point> aim_watch_points() noexcept {
  return aim_watch_point_table;
}

bool aim_watch_now(const machine& box, scene_aim_frame& out) noexcept {
  if (!modern_controls_on(box)) {
    return false;
  }
  const seam_engine& seams = box.seams();
  const std::uint16_t mode =
      seams.scratch(modern_controls_id, aim_mode_scratch);
  const std::uint16_t flags =
      seams.scratch(modern_controls_id, aim_flags_scratch);
  out = {};
  if (mode == static_cast<std::uint16_t>(scene_aim_mode::target) ||
      mode == static_cast<std::uint16_t>(scene_aim_mode::manual)) {
    out.mode = static_cast<scene_aim_mode>(mode);
    out.stack_segment = seams.scratch(modern_controls_id, aim_segment_scratch);
    out.frame = seams.scratch(modern_controls_id, aim_frame_scratch);
    out.offered = (flags & aim_offered) != 0;
    out.range_shown = (flags & aim_range_shown) != 0;
  }
  return true;
}

}  // namespace amberfolio::machine
