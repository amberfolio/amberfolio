// SPDX-License-Identifier: AGPL-3.0-only
//
// The timer fix: the sound driver never hooks the timer interrupt over its
// own hook.
//
// **A built-in fix, not a listed seam** (`built_in_fixes()`, seam.h): on
// whenever the program is loaded, and in no panel, config file, flag or
// recording. A driver that has lost the BIOS's timer handler plays every
// sound thirteen times slower for the rest of the run and stops the DOS
// clock; no player wants that.
//
// **The plain program never gets there.** Quick steps does
// (seam_quick_steps.cpp): it lets a click play on with the driver's timer
// hooked, and the next sound hooks it again. The fix is not a piece of
// quick steps because it has to outlast it: a player who turns quick steps
// off between two clicks leaves the hook in place for the next sound to
// find, and the fix is what is there when it does.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of this is addresses and offsets (CONTRIBUTING.md). Nothing of the
// program is reproduced here.
//
// **The driver hooks the timer for each sound in its one-shot mode.** Its
// install routine, at image offset `0x9E5C` (`push ds`, reached by a far
// call), asks DOS for the timer interrupt's vector (INT 08h), keeps it in
// two words of its own code segment, installs its own handler there (at
// the offset the `mov dx` at `+0x1C` loads, `0x13EA`, in the driver's
// segment), speeds the timer thirteen-fold and returns with the `retf` at
// `+0x34`. Its uninstall routine puts the kept vector back and the timer to
// its BIOS rate. The handler chains to the kept vector on every thirteenth
// tick, which is how the DOS clock keeps time while the driver runs.
//
// **One-shot mode is the driver's busy word, DGROUP `0x5374`, set.** Then
// the dispatch entry (seam_quick_steps.cpp has its facts) hooks the timer
// before each request and unhooks it after its wait, so between two sounds
// the vector is always the BIOS's. With the word clear, the driver stays
// hooked, and its own start-and-stop gate does the hooking.
//
// **Hooked twice, the kept vector is the driver's own handler.** Its
// uninstall then puts its own handler back and slows the timer to the BIOS
// rate: the handler runs on, thirteen times too slowly, every sound after
// is thirteen times longer (a monster's death was heard to run five or six
// seconds), and the chain on every thirteenth tick jumps to the handler itself,
// so the BIOS's never runs again.
//
//
// What the fix does
// -----------------
//
// At the install routine's entry, when the busy word is set and the vector
// is already the driver's own handler, it moves IP to the routine's `retf`:
// the vector the driver kept the first time stays the BIOS's, and the timer
// is already at the driver's rate, which is all the install would have
// done. It checks that the instruction at `+0x1C` loads the handler's
// offset and that `+0x34` is a `retf`, and declines otherwise. With the
// busy word clear it does nothing: the program's own gate decides then,
// and the fix does not second-guess it.
//
// **Fidelity**: in a run with no seam on, the vector is the BIOS's at every
// install, so the fix finds nothing to fix, and every session verifies with
// it on.

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The SHA-256 of the program image these offsets are facts about: the
/// baseline edition (edition.h), the one quick steps runs on.
constexpr std::array<std::string_view, 1> timer_fix_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// The install routine's entry, from the image segment, and from there the
/// `mov dx, imm16` that loads its handler's offset and its `retf`.
constexpr std::uint32_t install_offset = 0x9E5C;
constexpr std::uint16_t handler_load = 0x1C;
constexpr std::uint16_t install_return = 0x34;

constexpr std::uint8_t mov_dx_imm16 = 0xBA;
constexpr std::uint8_t retf = 0xCB;

/// The data segment is the image's paragraph `0xC7C` on; the busy word.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t data_busy = 0x5374;

/// The timer interrupt's vector, in the vector table at segment 0.
constexpr std::uint16_t timer_vector = 0x08 * 4;

void hook_once(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t cs = regs[cpu::sreg::cs];
  const std::uint16_t ip = regs.ip;

  const auto at = [ip](std::uint16_t delta) {
    return static_cast<std::uint16_t>(ip + delta);
  };
  if (cpu.read_byte(cs, at(handler_load)) != mov_dx_imm16 ||
      cpu.read_byte(cs, at(install_return)) != retf) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const auto dgroup =
      static_cast<std::uint16_t>((ctx.image_base() >> 4U) + dgroup_paragraphs);
  if (cpu.read_word(dgroup, data_busy) == 0) {
    // The driver's resident mode: its own gate hooks, and is left to.
    return;
  }
  const std::uint16_t handler =
      cpu.read_word(cs, at(static_cast<std::uint16_t>(handler_load + 1)));
  if (cpu.read_word(0, timer_vector) != handler ||
      cpu.read_word(0, static_cast<std::uint16_t>(timer_vector + 2)) != cs) {
    // The BIOS's, as it always is in the plain program: nothing to fix.
    return;
  }

  ctx.redirect(cs, at(install_return));
}

constexpr std::array<seam_point, 1> timer_fix_points{
    {{.module = resident_image, .offset = install_offset, .run = &hook_once}}};

constexpr seam_definition timer_fix_definition{
    .id = "timer-fix",
    .about =
        "never let the sound driver hook the timer over its own hook, "
        "which plays every sound after it thirteen times slower",
    .fingerprints = timer_fix_binaries,
    .points = timer_fix_points,
    .trigger = false,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& timer_fix_seam() noexcept {
  return timer_fix_definition;
}

}  // namespace amberfolio::machine
