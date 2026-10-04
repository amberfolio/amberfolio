// SPDX-License-Identifier: AGPL-3.0-only
//
// The bar-keys seam: Left and Right step a command bar's highlight, Enter
// takes the highlighted command, and Esc answers No at a Yes/No question
// (#425, #432, #438).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here.
//
// **Every command bar in the game goes through one routine**, in overlay
// 25 (`0x03BD`). It is handed the bar as a string, draws it with one
// *group* highlighted, and loops: ask the program's key-pending routine
// whether a key is waiting, and if one is, call the program's key-read
// routine and decide what it was. The call into the key-read routine is at
// `0x0572`, and is reached only when a key is waiting.
//
// What it does with the key it reads:
//
//   * `,` and `.` step the highlight, back and forward, wrapping at both
//     ends, and redraw the bar. The highlight is a one-based group index,
//     one byte in the data segment (`0x6B2B`) that every bar shares.
//   * **A letter** is upper-cased and compared with every character of
//     the bar. A match sets the highlight to that character's group and
//     ends the loop. **The scan does not stop at its first match, so the
//     last one wins.** Only the characters `0-9` and `A-Z` are command
//     letters; a mixed-case bar has one capital to a group.
//   * **Enter** ends the loop with `0x0D` as the answer, but only when the
//     bar's colours are not both zero (a bar that is not drawn does not
//     take Enter). The routine does nothing else with it: the caller is
//     handed a `0x0D`.
//   * **An extended key** (an arrow) is read as a zero and then its scan
//     code. With the caller's **raw-mode** argument set, it ends the loop
//     and is handed back with the out-parameter set, and the caller
//     decides; this is how the party's arrows move it in 3D and in the
//     wilderness. **With raw mode clear, the routine reads it and throws it
//     away.**
//
// **What the callers do with an arrow** (#432). Most raw-mode callers do
// nothing with Left and Right: the camp bar, its Magic and Alter sub-bars
// and the member-order screen hand every raw key to one resident routine,
// the party cursor's, which steps the selected member on `G` and `O` and
// **on any other key moves the selection back to the head of the party**.
// A few callers use the arrows on purpose, to move the party, a fighter or
// an aiming cursor, or to change a value. And a raw caller hands back the
// scan codes `0x4B` and `0x4D`, which are also the letters `K` and `M`, so
// one that compares letters without testing the out-parameter acts on an
// arrow as if it were that letter (docs/seams.md §10 has each).
//
// The routine parses the bar into locals of its own frame before it starts
// asking for keys, and everything this seam reads about the bar it reads
// there, so it sees the bar the routine sees and not a second reading of
// it: a one-byte flag (Enter is allowed), the group count, a table of each
// group's first and last position, and the bar itself as a Pascal string.
// A seam that has spliced a command onto the bar (the Encamp Fix's `Fix`,
// the journal's `Notes`) has done so before the routine copied it, so the
// group of a spliced command is a group like any other here.
//
//
// What the seam does
// ------------------
//
// **One point, at the call into the key-read routine**, and so reached
// only when a key is waiting and the program is about to read it. The
// keystroke it is about to read is the head of the BIOS ring at 40:1Eh,
// and the handler rewrites that word *before the program reads it*:
//
//   * **Left or Right** (scan `0x4B` or `0x4D`, character zero) becomes
//     `,` or `.`, **at every caller except those in the exclusion table
//     below**, raw mode or not. A caller that uses the arrows on purpose is
//     in the table, and gets its arrow.
//   * **Enter** (scan `0x1C`, character `0x0D`) becomes the command letter
//     of the highlighted group, when the call's **caller** is one in the
//     table below and the letter's *last* match in the bar is that same
//     group. The program then takes the command by the route it would
//     have taken for a typed letter: it sets the highlight (to the group
//     it already holds), draws, and returns the letter.
//
// Nothing is drawn, no program routine is called, no key is posted. The
// bar, its highlight and its commands stay the program's own.
//
// **The caller is identified by the routine's far return address**,
// overlay-qualified: the frame holds the caller's offset and segment, and a
// caller is in the table when its segment is the one the program's overlay
// manager says that module is at now and its offset is the instruction
// after the call. A caller the Enter table does not name is never offered
// Enter, and a caller the exclusion table does not name is offered its
// arrows as `,` and `.`.
//
// **The script prompts** (#438). The event scripts ask their questions
// through one runner in overlay 7 (`0x1684`, reached through a stub), which
// parses the `~`-marked hotkeys out of the script's text and calls the
// menu-bar routine in raw mode until the key is a hotkey letter, or Enter
// when the runner's own **allow-Enter** argument is set. With it clear, the
// runner asks again on Enter. The argument is a word on the runner's frame,
// `BP+8`, and the menu-bar routine's saved BP *is* the runner's BP, so
// Enter at the runner's call (return offset `0x16EB`) is taken only when
// that byte is zero. With it set, Enter is the program's own, which is the
// first choice. The read is refused unless the byte is inside conventional
// RAM.
//
// **Why a table and not every bar.** The routine hands Enter back, and
// what a caller does with `0x0D` is the caller's own business. A caller
// is in the table when it **asks again** on a `0x0D` it was handed: its
// compares match nothing, or its loop's exit class does not hold it, and
// nothing happens. A caller that did something with it, a pick-list that
// confirms its row, a prompt that Enter dismisses, a loop that Enter ends,
// would be handed a letter it never asked for, so it is not here. Each
// caller in the table was read in the disassembly from its return offset
// on, and shown to ignore Enter by a second route (docs/seams.md §10 has
// both routes for each, and a verdict for every caller that is not here).
//
//   | caller | module | return offset | what it does with `0x0D` |
//   |---|---|---|---|
//   | the Yes/No prompt | overlay 25 | `0x111E` | loops until the answer is in {Y, N} |
//   | the adventuring bar, overhead view | overlay 14 | `0x09D5` | none of its compares match; asks again |
//   | the adventuring bar, 3D view | overlay 14 | `0x0C45` | the same |
//   | the camp bar | overlay 15 | `0x1F24` | none of its compares match; asks again |
//   | camp's Magic bar | overlay 15 | `0x1447` | none of its compares match; asks again |
//   | camp's Alter bar | overlay 15 | `0x1CA4` | none of its compares match; asks again |
//   | alter's Portraits and Monsters bar | overlay 15 | `0x1DDF` | compares M and P; loops until {NUL, E} |
//   | camp's game-speed bar | overlay 15 | `0x1B91` | compares F and S; loops until {NUL, E} |
//   | the portrait bar | overlay 16 | `0x3449` | compares H, B and K; loops until K |
//   | the shop's bar | overlay 6 | `0x061F` | compares nine letters; repaints and loops |
//   | the temple's bar | overlay 4 | `0x0DAA` | compares nine letters; repaints and loops |
//   | the post-combat treasure bar | overlay 5 | `0x1024` | compares V, T, P, S, D, E, G and O; loops |
//   | the post-combat Take bar | overlay 5 | `0x0D91` | compares M, I, E, G and O; loops |
//   | the combat command bar | overlay 8 | `0x0819` | not in the set of commands; asks again |
//   | the combat Done bar | overlay 8 | `0x0F70` | compares G, D, Q, B and S; loops until {NUL, E} |
//   | the combat game-speed bar | overlay 8 | `0x120C` | compares S and F; loops until {NUL, E} |
//   | the View bar | overlay 19 | `0x0C9C` | compares I, S, T and D; loops until {NUL, E} |
//   | the temple's appraise bar | overlay 21 | `0x1C47` | compares G, J and E; loops until E |
//   | the load-game slot bar | overlay 17 | `0x16E2` | loops until the answer is a slot letter |
//   | the rest-time menu | overlay 20 | `0x076E` | **takes it as `R`, Rest**; in the table by decision, so Enter follows the highlight |
//
// **Esc answers No** (#438), at two callers only: the Yes/No prompt (the
// loop in the table above, which ignores Esc and asks again) and the script
// runner, when the bar it handed over has exactly `Y` and `N` for its
// command letters (a script's `~Yes ~No`). Esc becomes the letter `N`,
// posted as Enter's letters are, and the program takes it by the route a
// typed `N` takes. At every other caller, Esc is the program's own.
//
// **The callers that keep their arrows** (#432). Every caller of the
// routine was read (docs/seams.md §10 lists each with its verdict); these
// use Left and Right on purpose:
//
//   | caller | module | return offset | what it does with an arrow |
//   |---|---|---|---|
//   | the adventuring bar, overhead view | overlay 14 | `0x09D5` | turns the party, or steps it |
//   | the adventuring bar, 3D view | overlay 14 | `0x0C45` | the same |
//   | the combat move loop | overlay 8 | `0x0AC8` | steps the fighter |
//   | the combat aim cursor | overlay 13 | `0x3178` | moves the cursor |
//   | the stat editor | overlay 16 | `0x216E` | lowers and raises a score |
//   | the treasure share's press-Enter prompt | overlay 5 | `0x0AF8` | any extended key ends it |
//   | the NPC share's press-Enter prompt | overlay 5 | `0x14C7` | the same |
//
// The last two are not uses of the arrow by purpose, but a prompt that
// told the player to press Enter and let any arrow end it as well; with
// `,` and `.` in its place a Left or a Right would do nothing at all.
//
// Two callers act on an arrow by letter coincidence and are **not** in the
// table, by decision: the post-combat Take bar (Right is its `M`, Money)
// and the temple's keep-or-sell prompt (Left is its `K`, Keep). With the
// seam on the arrows step the highlight there like everywhere else.
//
// The rest-time menu (overlay 20, return `0x076E`) is not in the exclusion
// table either, by decision: the program steps its days/hours/minutes field
// on Left and Right, and with the seam on they step the bar's highlight
// instead, as at any other bar. `Y`, `H` and `M` still pick the field. Its
// Enter is in the Enter table by the same decision: the program maps it to
// `R`, Rest, and with the seam on it takes the highlighted command, which
// is Rest until the highlight is moved.
//
// **Why the BIOS ring and not AL.** The program reads the key two
// routines deep, through INT 16h, so the first place any seam can see it
// is the ring, which is also where the automap's and the journal's claims
// take theirs (`seam_key_read.h`). Those claims are at the key-pending
// routine, which the loop calls *first*: a key they want is gone before
// this point is reached, and a key they do not want is the program's. This
// point is not a claim, since it takes nothing off the ring. One race is
// guarded: a key can land between the journal's claim and the poll's own
// look at the ring, so the handler does nothing at all while the reader is
// open.
//
// **The pushback slot.** The program keeps the second half of an extended
// key in a one-byte slot (`0x8501`) and answers "a key is waiting" from it.
// While it is armed the head of the ring is not the key about to be read,
// so the handler touches nothing.
//
// **The last-match guard.** The highlighted group's first character is the
// letter, and the routine's scan would give a letter that appears again
// later in the bar to the *later* group. Enter is rewritten only when the
// highlighted group's letter is the last one in the bar; otherwise the
// handler declines and the program drops the Enter as it always did.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On and no Left, Right, Enter or Esc pressed at a bar, the run is byte for
// byte the run with the seam off: the handler reads the ring's head word and
// writes nothing unless it is one of those four. The pair is an
// `identical` and a `contrast` (tests/sessions/README.md).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// Enter at the callers that are not in the table, each with its verdict in
// docs/seams.md §10: most of them do something with it already, and two
// (the save-game slot bar and the stat editor) drop it and are left out on
// purpose. Out of scope: Up and Down, which are `list-arrows`' (#423,
// #435) at the same point, and a held key repeating (#426).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_menu_bar.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> bar_keys_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// The module, the read point, the frame's return address and who called the
// routine are shared with `list-arrows`, which has a point at the same
// instruction (seam_menu_bar.h).

// --- The routine's frame ---------------------------------------------------

/// Below BP: the routine's parse of the bar. `enter_allowed` is a byte,
/// then the group count, then, from the same base, a pair of bytes per
/// group (its first position and its last), group one first. The bar is a
/// Pascal string whose characters are numbered from one.
constexpr std::uint16_t local_enter_allowed = 0x8F;
constexpr std::uint16_t local_group_count = 0x8E;
constexpr std::uint16_t local_bar = 0x53;

/// The table has room for twenty groups, and the bar copy is a
/// `string[40]`.
constexpr std::uint8_t max_groups = 20;
constexpr std::uint8_t max_bar_length = 40;

// --- The data segment ------------------------------------------------------

/// The one-based index of the group a bar highlights, shared by every bar
/// (docs/seams.md §10, the Encamp Fix, #304).
constexpr std::uint16_t data_bar_highlight = 0x6B2B;

// --- The callers that ignore Enter -----------------------------------------

using menu_bar::adventure_load_segment_at;
using menu_bar::aim_load_segment_at;
using menu_bar::caller;
using menu_bar::camp_load_segment_at;
using menu_bar::combat_load_segment_at;
using menu_bar::post_combat_load_segment_at;
using menu_bar::roster_load_segment_at;
using menu_bar::script_load_segment_at;

using menu_bar::appraise_load_segment_at;
using menu_bar::shop_load_segment_at;
using menu_bar::slots_load_segment_at;
using menu_bar::temple_load_segment_at;
using menu_bar::view_load_segment_at;

constexpr std::array<caller, 20> enter_callers{{
    {.load_segment_at = menu_bar::load_segment_at, .return_offset = 0x111E},
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x09D5},
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x0C45},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1F24},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1447},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1CA4},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1DDF},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1B91},
    {.load_segment_at = roster_load_segment_at, .return_offset = 0x3449},
    {.load_segment_at = shop_load_segment_at, .return_offset = 0x061F},
    {.load_segment_at = temple_load_segment_at, .return_offset = 0x0DAA},
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x1024},
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x0D91},
    {.load_segment_at = combat_load_segment_at, .return_offset = 0x0819},
    {.load_segment_at = combat_load_segment_at, .return_offset = 0x0F70},
    {.load_segment_at = combat_load_segment_at, .return_offset = 0x120C},
    {.load_segment_at = view_load_segment_at, .return_offset = 0x0C9C},
    {.load_segment_at = appraise_load_segment_at, .return_offset = 0x1C47},
    {.load_segment_at = slots_load_segment_at, .return_offset = 0x16E2},
    {.load_segment_at = menu_bar::rest_load_segment_at,
     .return_offset = 0x076E},
}};

/// The Yes/No prompt, and the script runner's call into the routine (the
/// instruction after it, in overlay 7).
constexpr std::array<caller, 1> yes_no_caller{
    {{.load_segment_at = menu_bar::load_segment_at, .return_offset = 0x111E}}};
constexpr std::array<caller, 1> script_caller{
    {{.load_segment_at = script_load_segment_at, .return_offset = 0x16EB}}};

/// Above the script runner's BP: its allow-Enter argument, a word of which
/// the runner reads the low byte.
constexpr std::uint16_t runner_allow_enter = 8;

// --- The callers that keep Left and Right ---------------------------------

constexpr std::array<caller, 7> arrow_callers{{
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x09D5},
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x0C45},
    {.load_segment_at = combat_load_segment_at, .return_offset = 0x0AC8},
    {.load_segment_at = aim_load_segment_at, .return_offset = 0x3178},
    {.load_segment_at = roster_load_segment_at, .return_offset = 0x216E},
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x0AF8},
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x14C7},
}};

// --- The keys, as the BIOS ring holds them: scan code high, character low --

constexpr std::uint16_t key_left = 0x4B00;
constexpr std::uint16_t key_right = 0x4D00;
constexpr std::uint16_t key_enter = 0x1C0D;
constexpr std::uint16_t key_escape = 0x011B;
constexpr std::uint16_t key_comma = 0x332C;
constexpr std::uint16_t key_period = 0x342E;

/// The scan code a command letter is posted under: Enter's own, because the
/// program reads the character and nothing else. Esc's answer is posted the
/// same way.
constexpr std::uint16_t scan_enter = 0x1C00;

/// Whether `c` is a character the routine takes as a command letter.
[[nodiscard]] constexpr bool is_command_letter(std::uint8_t c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

/// Whether the script runner that called the routine takes Enter itself: its
/// allow-Enter argument, read through the routine's saved BP. True as well
/// when the frame cannot be the runner's, or the byte would not be in
/// conventional RAM, and nothing is read in either case: the program's own
/// Enter is the answer to a frame this seam cannot vouch for.
[[nodiscard]] bool runner_takes_enter_itself(cpu::processor& cpu) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t runner_bp = cpu.read_word(ss, bp);
  // The runner's frame is above this routine's, and its argument is the
  // runner's own: refuse a BP that is not above ours, or an address that
  // would reach past the RAM the program owns.
  const std::uint32_t argument_at =
      static_cast<std::uint32_t>(runner_bp) + runner_allow_enter;
  if (runner_bp <= bp || argument_at > 0xFFFFU ||
      cpu::physical_address(ss, static_cast<std::uint16_t>(argument_at)) >=
          conventional_ram_size) {
    return true;
  }
  return cpu.read_byte(ss, static_cast<std::uint16_t>(argument_at)) != 0;
}

/// Whether the bar the routine was handed has exactly `Y` and `N` for its
/// command letters: the two answers of a Yes/No question.
[[nodiscard]] bool bar_is_yes_no(cpu::processor& cpu) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const auto bar = static_cast<std::uint16_t>(bp - local_bar);

  const std::uint8_t length = cpu.read_byte(ss, bar);
  if (length == 0 || length > max_bar_length) {
    return false;
  }
  bool yes = false;
  bool no = false;
  for (std::uint8_t position = 1; position <= length; ++position) {
    const std::uint8_t c =
        cpu.read_byte(ss, static_cast<std::uint16_t>(bar + position));
    if (!is_command_letter(c)) {
      continue;
    }
    if (c == 'Y') {
      yes = true;
    } else if (c == 'N') {
      no = true;
    } else {
      return false;
    }
  }
  return yes && no;
}

/// The keystroke Enter should become: the letter of the highlighted group,
/// posted under Enter's own scan code. Zero if there is nothing to answer,
/// and declines when the frame is not the one these facts describe or the
/// letter would not be the group's.
[[nodiscard]] std::uint16_t letter_for_enter(cpu::processor& cpu,
                                             seam_context& ctx) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const auto below = [&](std::uint16_t distance) {
    return static_cast<std::uint16_t>(bp - distance);
  };

  if (cpu.read_byte(ss, below(local_enter_allowed)) == 0) {
    // The routine does not take Enter from a bar it does not draw.
    return 0;
  }

  const std::uint8_t group =
      cpu.read_byte(regs[cpu::sreg::ds], data_bar_highlight);
  if (group == 0) {
    // The index is one-based: no bar has been stepped or chosen from yet,
    // so no group is the highlighted one and Enter has no command to take.
    return 0;
  }

  const std::uint8_t groups = cpu.read_byte(ss, below(local_group_count));
  const std::uint8_t length = cpu.read_byte(ss, below(local_bar));
  if (groups == 0 || groups > max_groups || length == 0 ||
      length > max_bar_length || group > groups) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }

  const std::uint8_t first = cpu.read_byte(
      ss, static_cast<std::uint16_t>(below(local_enter_allowed) + 2U * group));
  if (first == 0 || first > length) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  const auto bar_at = [&](std::uint8_t position) {
    return cpu.read_byte(
        ss, static_cast<std::uint16_t>(below(local_bar) + position));
  };
  const std::uint8_t letter = bar_at(first);
  if (!is_command_letter(letter)) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }

  // The routine's scan does not stop at its first match: the last position
  // that holds the letter is the group it would set the highlight to.
  std::uint8_t last = first;
  for (std::uint8_t position = 1; position <= length; ++position) {
    if (bar_at(position) == letter) {
      last = position;
    }
  }
  if (last != first) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  return static_cast<std::uint16_t>(scan_enter | letter);
}

/// The program is about to read the key at the head of the ring.
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();

  const std::array<std::uint16_t, 4> wanted{key_left, key_right, key_enter,
                                            key_escape};
  const std::optional<menu_bar::pending_key> pending =
      menu_bar::key_about_to_be_read(box, wanted);
  if (!pending) {
    return;
  }
  const std::uint16_t head = pending->at;
  const std::uint16_t key = pending->key;

  if (key == key_escape) {
    // No at a Yes/No question, and nowhere else.
    if (menu_bar::called_from(cpu, ctx, yes_no_caller) ||
        (menu_bar::called_from(cpu, ctx, script_caller) &&
         bar_is_yes_no(cpu))) {
      cpu.write_word(bda::segment, head,
                     static_cast<std::uint16_t>(scan_enter | 'N'));
    }
    return;
  }

  if (key == key_enter) {
    const bool tabled = menu_bar::called_from(cpu, ctx, enter_callers);
    // A script prompt that does not take Enter itself is handed it as its
    // highlighted answer.
    if (!tabled && !(menu_bar::called_from(cpu, ctx, script_caller) &&
                     !runner_takes_enter_itself(cpu))) {
      return;
    }
    const std::uint16_t answer = letter_for_enter(cpu, ctx);
    if (answer != 0) {
      cpu.write_word(bda::segment, head, answer);
    }
    return;
  }

  if (menu_bar::called_from(cpu, ctx, arrow_callers)) {
    // A caller that uses the arrows keeps them.
    return;
  }
  cpu.write_word(bda::segment, head, key == key_left ? key_comma : key_period);
}

constexpr std::array<seam_point, 1> bar_keys_points{
    {{.module = menu_bar::module,
      .offset = menu_bar::key_read_call,
      .run = &at_key_read}}};

constexpr seam_definition bar_keys_definition{
    .id = "bar-keys",
    .about =
        "Left and Right step a command bar's highlight, Enter takes the "
        "highlighted command, and Esc answers No at a Yes/No question",
    .fingerprints = bar_keys_binaries,
    .points = bar_keys_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& bar_keys_seam() noexcept { return bar_keys_definition; }

}  // namespace amberfolio::machine
