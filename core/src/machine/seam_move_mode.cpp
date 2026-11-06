// SPDX-License-Identifier: AGPL-3.0-only
//
// The move-mode piece of `modern-controls` (seam_modern_controls.cpp): on the
// party's own bar, walking is a mode, so Left and Right can step the bar
// (#479).
//
// The later games in the series did this: their adventure bar starts with
// `Move`, the arrows step the bar until it is chosen, and the party walks
// until `Exit`. This program's bar has no such command, so its arrows walk
// and nothing steps the bar but `,` and `.`. With the piece on:
//
//   * **Menu mode.** The bar's first command is `Move`, in `Area`'s place in
//     the city and before `Cast` in the wilderness. Left and Right step the
//     highlight, Up and Down select the previous and next member, and Return
//     takes the lit command. Nothing walks.
//   * **Walking mode.** The bar is `Exit` alone, lit. The arrows, the keypad
//     and the number row walk and turn as the program's own bar has them do.
//     Return, Esc and `E` go back to menu mode, and every other letter does
//     nothing.
//   * **`Move` lit is where the party arrives**: after a load, a fight, camp,
//     a shop, a script's question, any screen with a bar of its own. Walking
//     lasts across steps and the events a step runs, and ends at the next
//     bar that is not the party's or a door's.
//
// `Area`, the overhead view, is gone with the piece on: the automap is the
// overhead view (by decision, #479).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here. Pool of Radiance's first; Curse of the Azure Bonds'
// differences after the seam's own section.
//
// **The adventuring input routine** is overlay 14 `0x0989`. It reads the
// view mode byte (`0x49FA`): mode 1 is the city, modes 2 to 4 the
// wilderness. Each calls the menu-bar routine (overlay 25 `0x03BD`, raw mode
// set) with a bar that is a Pascal `string[40]` in the data segment, `0x04B6`
// for the city and `0x04DF` for the wilderness, and loops on the answer:
//
//   * **Out-parameter clear** (`BP-0x04` zero): a letter off the bar. The
//     city compares it with `A` (toggle the overhead view byte `0x6AAC`, when
//     the area allows it), `C`, `V`, `E` (end the loop: the caller camps),
//     `S` and `L`; the wilderness with the same five without `A`. **Nothing
//     else matches, so `M` and any other letter go round the loop.**
//   * **Out-parameter set**: a raw key. `H` (Up), `K` (Left), `M` (Right) and
//     `P` (Down) walk and turn: relative in the city, where only `H` ends the
//     routine, and absolute facings in the wilderness, where all four end
//     it. The menu-bar routine has already turned the digits into the
//     letters the keypad's layout implies (`8` is `H`, `7` is `G` and so on).
//     Every other raw key, including a letter the bar does not hold, goes to
//     the party cursor, which steps the selected member on `G` and `O` and
//     on anything else goes to the head of the party.
//
// **The call's arguments**, at the call instruction: the out-parameter's far
// pointer on top (`SS:SP`), then five words (raw mode and the bar's
// colours), then **the bar's far pointer, offset at `SS:SP+14` and segment
// at `SS:SP+16`**, then the prompt's (`SS:SP+18`). The routine cleans them
// itself. The prompt is a string temporary in the loop's frame at
// `BP-0x33`, loaded with the empty string before each call, so only its
// length byte is read. The bytes after it, down to the out-parameter's
// neighbours (`BP-0x32` to `BP-0x05`), are used by the loop only for
// buffers it fills afresh after the call returns (the `Not Here` message,
// View's, the icon names before the loop).
//
// **The bar's highlight** is one byte, `0x6B2B`, a one-based group index
// that every bar in the program shares (#304). The routine sets it to the
// group of the command it matched, and `,` and `.` step it.
//
// **The overhead view** is the byte `0x6AAC`. The program clears it itself
// on a new game, where an area does not allow it, and when a script's
// message comes up; `A` is the only thing that sets it.
//
//
// What the seam does
// ------------------
//
// **Two points per bar, either side of the call**, the journal's pair
// (seam_journal.cpp); the journal's run first at both.
//
//   * **Before**: the bar the call is about to be handed, the program's
//     string with whatever the journal has appended, is copied into the
//     loop's free frame at `BP-0x32`, the copy is made the bar of this mode,
//     and the call's far pointer is aimed at the copy. Menu mode: the city's
//     first group, four letters, becomes `Move`; the wilderness gets `Move `
//     in front of its first group, which moves every group up one (27 + 5,
//     and the journal's six, is 38 of 40). Walking mode: the copy is `Exit`.
//   * **After**: the letter. `M` off the bar starts walking (the program
//     loops on it), and `E` off the walking bar stops it and is handed back
//     as `-`, which the program loops on, so the party does not camp.
//
// **The program's string is never written.** The copy is in a local nothing
// reads, and the pointer to it is an argument the routine pops. So a run
// the seam is switched off in the middle of, with the bar up, goes on with
// the program's own bar from the next call, and one switched on in the
// middle simply does not have the piece's after-point for that call. Whether
// a call already carries the copy (the point offered again after a batch)
// is read off the pointer, not remembered.
//
// **The highlight, so `Move` and `Exit` are lit when they should be.** Outside
// the call the byte holds the program's numbering; inside it the bar's. The
// before-point puts `Move` (group one) under it when the party has just
// arrived or `Move` was the last thing lit, and otherwise moves the
// wilderness bar's number up one for the inserted group; the after-point
// takes it back down, remembers `Move` being lit in a word of its own (there
// is no program group for it to be), and puts back the value it was entered
// with when the journal's `Notes` was chosen (#330's rule). The walking bar
// has one group, lit, and the byte is put back as it was found.
//
// **Keys: one point, at the call into the key-read routine** (overlay 25
// `0x0572`), the point every key piece is at, and the last of them, so the
// others have already passed on this bar. The ring's head is rewritten before
// the program reads it, at the two adventuring callers only:
//
//   * menu mode: Left and Right become `,` and `.`, Up and Down become Home
//     and End (the party cursor), the digits `4`, `6`, `8` and `2` become the
//     same four, and a letter that is not one of the bar's command letters is
//     answered with the key the program throws away (`seam_key_read.h`), so a
//     typed `H`, `K` or `P` does not walk;
//   * walking mode: Esc becomes `E`, and every letter but `E` is thrown away.
//     Return is `bar-keys`' (the lit command, which is `Exit`'s `E`).
//
// **Any other caller of the menu-bar routine that asks for a choice ends
// walking**, and lights `Move` for the party's next bar. That is the whole
// of "the party arrives in menu mode": a fight, camp, a shop, a script's
// question, View and the load screen all ask through the same routine. Two
// kinds of bar are part of the walk instead, and leave both words alone:
// the two door bars (overlay 14, returns `0x0EBF` and `0x0FFE`), which a
// walk runs into and goes on from, and **a notice**, a bar the routine has
// parsed into one group (its count at `BP-0x8E`), such as the prompt a
// script raises to wait for Return before it goes on. One command is no
// choice; the pick-list's bar is the exception (overlay 25 `0x0FE0`),
// because its rows are. The program's other waits for a key (a script's
// pause, `message-pause`'s) do not go through the routine at all.
//
// **A party in the overhead view** when the city bar comes up (the piece
// switched on while it was there) is put back in the 3D view: the byte is
// cleared and the program's own screen composer is called through the batch,
// as the journal gives its screen back, and the point is offered again.
//
// **State**: five words of the seam's own (seam.h "A seam's own few
// words"): walking, arrived, `Move` lit, which bar this call was handed and
// the highlight it was entered with. They are configuration: `enable()`
// starts them at zero, which is menu mode with `Move` to be lit.

//
//
// On Curse of the Azure Bonds (#555)
// ----------------------------------
//
// The same mechanism with that program's facts, in a row of its own. Its
// adventuring loop (overlay 15 `0x08DF`) has **one** call into the menu-bar
// routine (`0x0927`, the instruction after it `0x092C`), handed the one bar
// at `0x03D8` in the data segment; the overhead view is toggled inside the
// same loop and keeps that bar. So the bar is the shape of Pool of
// Radiance's city bar: a first group of a capital `A` and three more, whose
// place `Move` takes. The loop's frame is the baseline's where this piece
// reads and writes it: the out-parameter at `BP-0x04`, the letter at
// `BP-0x03`, the empty prompt at `BP-0x33`, and below them nothing the call
// reads (`BP-0x2E` is View's buffer, which View clears before it reads).
// Off the bar the loop matches `A` (the overhead view's byte, `0x7228`),
// `C`, `V`, `E` (camp), `S` and `L`, and loops on anything else; raw, `H`
// walks, `K`, `M` and `P` turn, and any other key goes to the party cursor
// and the roster is drawn again. The menu-bar routine throws away a letter
// the bar does not hold, takes Return as the lit group's letter, and in raw
// mode turns the digits into the keypad's letters (`8` is `H`) as the
// baseline's does, and keeps its group count at `BP-0x8E` as the
// baseline's does. The two door bars, in the same module, return at
// `0x0CD7` and `0x0E16`; the pick-list's call, from inside the menu
// overlay, at `0x10F3`. A script's continue prompt is its menu command
// with one item, which the routine parses into one group.
//
// The overland is not this loop: it is a journey menu, a bar of the
// script's, so it is "another caller" here, and the party's next bar in a
// town or a dungeon is in menu mode with `Move` lit.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_key_read.h"
#include "seam_menu_bar.h"
#include "seam_party_select.h"

namespace amberfolio::machine {
namespace {

using menu_bar::caller;

// --- One program's facts ---------------------------------------------------

/// One of the party's own bars: the adventuring loop's call into the
/// menu-bar routine, the instruction after it, and the program's string the
/// call is handed.
struct party_bar {
  std::uint16_t call;
  std::uint16_t return_offset;
  std::uint16_t data_bar;
  /// Whether `Move` goes in front of the bar's first group (Pool of
  /// Radiance's wilderness) rather than in its place, which is the overhead
  /// view's `Area`.
  bool inserts;
};

/// What this piece needs to know about one program. The modules, the read
/// point, the routine's copy of the bar, the highlight and the pushback are
/// the profile's.
struct move_mode_facts {
  const program_profile& profile;
  /// The party's bars, each a pair of points in the adventure overlay.
  std::span<const party_bar> bars;
  /// The same calls as callers of the routine, by their returns.
  std::span<const caller> party_callers;
  /// The door bars, locked and stuck, in the same module: a walk meets
  /// them, and answering one does not end it.
  std::span<const caller> door_callers;
  /// The pick-list's call into the routine: a list is a choice whatever
  /// its bar holds.
  std::span<const caller> list_callers;
  /// Below the routine's BP: the number of groups it parsed the bar into.
  /// A bar of one group is a notice, not a choice (`is_a_notice()`).
  std::uint16_t local_group_count;
  /// Below the loop's BP: its out-parameter (zero is a letter off the bar),
  /// and where the copy is made, right after the empty prompt's length
  /// byte, with room for a whole bar before the loop's own bytes.
  std::uint16_t frame_out_flag;
  std::uint16_t frame_copy;
  /// The overhead view's byte, in the data segment.
  std::uint16_t data_overhead_view;
};

// --- Pool of Radiance ------------------------------------------------------

constexpr const program_profile& por = pool_of_radiance;

/// The city's bar and the wilderness's, both in overlay 14.
constexpr std::array<party_bar, 2> pool_of_radiance_bars{{
    {.call = por.area_bar_call,
     .return_offset = por.area_bar_return,
     .data_bar = por.data_area_bar,
     .inserts = false},
    {.call = por.view_bar_call,
     .return_offset = por.view_bar_return,
     .data_bar = por.data_view_bar,
     .inserts = true},
}};
constexpr std::array<caller, 2> pool_of_radiance_party_callers{{
    {.load_segment_at = por.adventure_overlay.load_segment_at,
     .return_offset = por.area_bar_return},
    {.load_segment_at = por.adventure_overlay.load_segment_at,
     .return_offset = por.view_bar_return},
}};
constexpr std::array<caller, 2> pool_of_radiance_door_callers{{
    {.load_segment_at = por.adventure_overlay.load_segment_at,
     .return_offset = 0x0EBF},
    {.load_segment_at = por.adventure_overlay.load_segment_at,
     .return_offset = 0x0FFE},
}};

constexpr std::array<caller, 1> pool_of_radiance_list_callers{
    {{.load_segment_at = por.menu_overlay.load_segment_at,
      .return_offset = por.pick_list_bar_return}}};

constexpr move_mode_facts pool_of_radiance_move_mode{
    .profile = pool_of_radiance,
    .bars = pool_of_radiance_bars,
    .party_callers = pool_of_radiance_party_callers,
    .door_callers = pool_of_radiance_door_callers,
    .list_callers = pool_of_radiance_list_callers,
    .local_group_count = 0x8E,
    .frame_out_flag = 0x04,
    .frame_copy = 0x32,
    .data_overhead_view = 0x6AAC};

// --- Curse of the Azure Bonds ---------------------------------------------

constexpr const program_profile& curse = curse_of_the_azure_bonds;

/// The one bar, in overlay 15: `Area` first, as Pool of Radiance's city.
constexpr std::array<party_bar, 1> curse_bars{{
    {.call = curse.view_bar_call,
     .return_offset = curse.view_bar_return,
     .data_bar = curse.data_view_bar,
     .inserts = false},
}};
constexpr std::array<caller, 1> curse_party_callers{{
    {.load_segment_at = curse.adventure_overlay.load_segment_at,
     .return_offset = curse.view_bar_return},
}};
constexpr std::array<caller, 2> curse_door_callers{{
    {.load_segment_at = curse.adventure_overlay.load_segment_at,
     .return_offset = 0x0CD7},
    {.load_segment_at = curse.adventure_overlay.load_segment_at,
     .return_offset = 0x0E16},
}};

/// Curse of the Azure Bonds (#555). The loop's frame is the baseline's
/// where this piece reads and writes it; the overhead view's byte is the
/// one `A` toggles.
/// The pick-list's call is a near call from inside the menu overlay.
constexpr std::array<caller, 1> curse_list_callers{
    {{.load_segment_at = curse.menu_overlay.load_segment_at,
      .return_offset = curse.pick_list_bar_return}}};

constexpr move_mode_facts curse_move_mode{.profile = curse_of_the_azure_bonds,
                                          .bars = curse_bars,
                                          .party_callers = curse_party_callers,
                                          .door_callers = curse_door_callers,
                                          .list_callers = curse_list_callers,
                                          .local_group_count = 0x8E,
                                          .frame_out_flag = 0x04,
                                          .frame_copy = 0x32,
                                          .data_overhead_view = 0x7228};

// --- The mechanism's constants ---------------------------------------------

/// At the call: where the bar's far pointer is, above SP. The same on both
/// programs.
constexpr std::uint16_t stack_bar_offset = 14;
constexpr std::uint16_t stack_bar_segment = 16;

/// A bar's slot: a length byte and forty characters.
constexpr std::uint8_t bar_capacity = 40;

// --- The words this piece adds ---------------------------------------------

/// One capital and a lower-case tail, as every spliced command is: the
/// tail's letters are not command letters, so they steal nothing.
constexpr std::array<std::uint8_t, 4> move_word{'M', 'o', 'v', 'e'};
constexpr std::array<std::uint8_t, 4> exit_word{'E', 'x', 'i', 't'};
/// Both words' length: four, which is also `Area`'s.
constexpr unsigned word_length = 4;
constexpr std::uint8_t move_letter = 'M';
constexpr std::uint8_t exit_letter = 'E';
constexpr std::uint8_t notes_letter = 'N';
/// `Move ` in front of the wilderness bar's first group.
constexpr std::uint8_t inserted_length = 5;

// --- Keys, as the ring holds them ------------------------------------------

constexpr std::uint16_t key_left = 0x4B00;
constexpr std::uint16_t key_right = 0x4D00;
constexpr std::uint16_t key_up = 0x4800;
constexpr std::uint16_t key_down = 0x5000;
constexpr std::uint16_t key_home = 0x4700;
constexpr std::uint16_t key_end = 0x4F00;
constexpr std::uint16_t key_escape = 0x011B;
constexpr std::uint16_t key_comma = 0x332C;
constexpr std::uint16_t key_period = 0x342E;
constexpr std::uint16_t key_e = 0x1245;

// --- The seam's own words --------------------------------------------------

constexpr unsigned scratch_walking = 0;
/// Zero until the party's bar has been up since the last other bar: zero
/// lights `Move`.
constexpr unsigned scratch_arrived = 1;
constexpr unsigned scratch_move_lit = 2;
constexpr unsigned scratch_handed = 3;
constexpr unsigned scratch_highlight = 4;

/// Which bar this call was handed, between its two points.
enum class handed : std::uint8_t { program, menu, walking };

[[nodiscard]] std::uint16_t as_word(handed h) noexcept {
  return static_cast<std::uint16_t>(h);
}

[[nodiscard]] std::uint16_t data_segment(const move_mode_facts& f,
                                         cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  return party_select::is_the_data_segment(f.profile, ctx, ds) ? ds : 0;
}

[[nodiscard]] std::uint16_t at(std::uint16_t base, unsigned by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

[[nodiscard]] std::uint8_t low_of(std::uint16_t w) noexcept {
  return static_cast<std::uint8_t>(w & 0xFFU);
}

[[nodiscard]] std::uint16_t pair(std::uint8_t low, std::uint8_t high) noexcept {
  return static_cast<std::uint16_t>(low | (high << 8U));
}

void put(cpu::processor& cpu, std::uint16_t segment, std::uint16_t bar,
         std::span<const std::uint8_t> word, unsigned from) {
  for (unsigned i = 0; i < word.size(); ++i) {
    cpu.write_byte(segment, at(bar, from + i), word[i]);
  }
}

// --- Before the bar goes out -----------------------------------------------

/// Whether the overhead view was on and the program has been asked to put
/// the 3D view back. The point is offered again when the batch is done.
[[nodiscard]] bool leave_the_overhead_view(const move_mode_facts& f,
                                           machine& box, seam_context& ctx,
                                           std::uint16_t ds) {
  cpu::processor& cpu = box.processor();
  if (cpu.read_byte(ds, f.data_overhead_view) == 0) {
    return false;
  }
  cpu.write_byte(ds, f.data_overhead_view, 0);
  // The composer repaints the roster's cells, where the map's panel is.
  box.automap().note_panel_painted_over();
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::array<std::uint16_t, 0> nothing{};
  return ctx.call_program(
      static_cast<std::uint16_t>(image + f.profile.screen_paragraph),
      f.profile.screen_composer_offset, nothing);
}

/// The bar of this mode, made in the copy at `SS:copy` from the program's
/// bar `b` in the data segment. False, and the copy not to be used, if the
/// program's string is not the shape the facts say.
[[nodiscard]] bool make_the_bar(cpu::processor& cpu, std::uint16_t ds,
                                const party_bar& b, std::uint16_t ss,
                                std::uint16_t copy, bool walking) {
  if (walking) {
    cpu.write_byte(ss, copy, static_cast<std::uint8_t>(word_length));
    put(cpu, ss, copy, exit_word, 1);
    return true;
  }
  const std::uint16_t bar = b.data_bar;
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (!b.inserts) {
    // The first group is the overhead view's: a capital and three more,
    // then a separator.
    if (length <= word_length || length > bar_capacity ||
        cpu.read_byte(ds, at(bar, 1)) != 'A' ||
        cpu.read_byte(ds, at(bar, word_length + 1U)) != ' ') {
      return false;
    }
    for (unsigned nth = 0; nth <= length; ++nth) {
      cpu.write_byte(ss, at(copy, nth), cpu.read_byte(ds, at(bar, nth)));
    }
    put(cpu, ss, copy, move_word, 1);
    return true;
  }
  if (length == 0 || length + inserted_length > bar_capacity) {
    return false;
  }
  cpu.write_byte(ss, copy, static_cast<std::uint8_t>(length + inserted_length));
  put(cpu, ss, copy, move_word, 1);
  cpu.write_byte(ss, at(copy, inserted_length), ' ');
  for (unsigned nth = 1; nth <= length; ++nth) {
    cpu.write_byte(ss, at(copy, nth + inserted_length),
                   cpu.read_byte(ds, at(bar, nth)));
  }
  return true;
}

void bar_before(const move_mode_facts& f, const party_bar& b, machine& box,
                seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(f, cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const auto copy =
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - f.frame_copy);
  const std::uint16_t handed_offset =
      cpu.read_word(ss, at(sp, stack_bar_offset));
  const std::uint16_t handed_segment =
      cpu.read_word(ss, at(sp, stack_bar_segment));
  if (handed_offset == copy && handed_segment == ss) {
    return;  // offered again after a batch: the call carries the copy already.
  }
  ctx.set_scratch(scratch_handed, as_word(handed::program));
  if (handed_offset != b.data_bar || handed_segment != ds) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!b.inserts && leave_the_overhead_view(f, box, ctx, ds)) {
    return;
  }
  const bool walking = ctx.scratch(scratch_walking) != 0;
  if (!make_the_bar(cpu, ds, b, ss, copy, walking)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  cpu.write_word(ss, at(sp, stack_bar_offset), copy);
  cpu.write_word(ss, at(sp, stack_bar_segment), ss);

  const std::uint16_t highlight = f.profile.data_bar_highlight;
  const std::uint8_t entered = cpu.read_byte(ds, highlight);
  ctx.set_scratch(scratch_highlight, entered);
  if (walking) {
    ctx.set_scratch(scratch_handed, as_word(handed::walking));
    cpu.write_byte(ds, highlight, 1);
    return;
  }
  ctx.set_scratch(scratch_handed, as_word(handed::menu));
  if (ctx.scratch(scratch_arrived) == 0) {
    ctx.set_scratch(scratch_arrived, 1);
    ctx.set_scratch(scratch_move_lit, 1);
  }
  std::uint8_t lit = entered;
  if (ctx.scratch(scratch_move_lit) != 0) {
    lit = 1;
  } else if (b.inserts) {
    lit = static_cast<std::uint8_t>(entered + 1U);
  }
  cpu.write_byte(ds, highlight, lit);
}

// --- After it comes back ---------------------------------------------------

void bar_after(const move_mode_facts& f, const party_bar& b, machine& box,
               seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const auto carried = static_cast<handed>(low_of(ctx.scratch(scratch_handed)));
  ctx.set_scratch(scratch_handed, as_word(handed::program));
  const std::uint16_t ds = data_segment(f, cpu, ctx);
  if (ds == 0 || carried == handed::program) {
    return;
  }

  cpu::registers& regs = cpu.regs();
  const bool off_the_bar =
      cpu.read_byte(regs[cpu::sreg::ss],
                    static_cast<std::uint16_t>(regs[cpu::reg16::bp] -
                                               f.frame_out_flag)) == 0;
  const std::uint8_t letter = regs.get(cpu::reg8::al);
  const auto entered =
      static_cast<std::uint8_t>(ctx.scratch(scratch_highlight));
  const std::uint16_t highlight = f.profile.data_bar_highlight;

  if (carried == handed::walking) {
    cpu.write_byte(ds, highlight, entered);
    if (off_the_bar && letter == exit_letter) {
      ctx.set_scratch(scratch_walking, 0);
      ctx.set_scratch(scratch_move_lit, 1);
      // Not the program's `E`: it would camp. It loops on this one.
      regs.set(cpu::reg8::al, key_ignored_ascii);
    }
    return;
  }

  if (off_the_bar && letter == notes_letter) {
    // The journal's: the highlight is what it was before (#330).
    cpu.write_byte(ds, highlight, entered);
    return;
  }
  const std::uint8_t lit = cpu.read_byte(ds, highlight);
  if (lit == 1) {
    ctx.set_scratch(scratch_move_lit, 1);
    cpu.write_byte(ds, highlight, entered);
  } else {
    ctx.set_scratch(scratch_move_lit, 0);
    if (b.inserts) {
      cpu.write_byte(ds, highlight, static_cast<std::uint8_t>(lit - 1U));
    }
  }
  if (off_the_bar && letter == move_letter) {
    ctx.set_scratch(scratch_walking, 1);
  }
}

template <const move_mode_facts& F, std::size_t N>
void at_bar_before(machine& box, seam_context& ctx) {
  bar_before(F, F.bars[N], box, ctx);
}

template <const move_mode_facts& F, std::size_t N>
void at_bar_after(machine& box, seam_context& ctx) {
  bar_after(F, F.bars[N], box, ctx);
}

// --- The keys --------------------------------------------------------------

[[nodiscard]] std::uint8_t upper(std::uint8_t c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<std::uint8_t>(c - 'a' + 'A') : c;
}

[[nodiscard]] bool is_letter(std::uint8_t c) noexcept {
  const std::uint8_t u = upper(c);
  return u >= 'A' && u <= 'Z';
}

/// Whether `letter` is one of the command letters of the bar the routine is
/// running.
[[nodiscard]] bool on_the_bar(const move_mode_facts& f, cpu::processor& cpu,
                              std::uint8_t letter) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const auto bar = static_cast<std::uint16_t>(regs[cpu::reg16::bp] -
                                              f.profile.menu_local_bar);
  const std::uint8_t length = cpu.read_byte(ss, bar);
  if (length > bar_capacity) {
    return false;
  }
  for (unsigned i = 1; i <= length; ++i) {
    if (cpu.read_byte(ss, at(bar, i)) == letter) {
      return true;
    }
  }
  return false;
}

/// What a key becomes in menu mode: the arrows and their digits step the
/// bar and the party, and a letter the bar does not hold is thrown away.
[[nodiscard]] std::optional<std::uint16_t> in_menu_mode(
    const move_mode_facts& f, cpu::processor& cpu, std::uint16_t key) {
  switch (key) {
    case key_left:
      return key_comma;
    case key_right:
      return key_period;
    case key_up:
      return key_home;
    case key_down:
      return key_end;
    default:
      break;
  }
  switch (low_of(key)) {
    case '4':
      return key_comma;
    case '6':
      return key_period;
    case '8':
      return key_home;
    case '2':
      return key_end;
    default:
      break;
  }
  const std::uint8_t c = low_of(key);
  if (is_letter(c) && !on_the_bar(f, cpu, upper(c))) {
    return pair(key_ignored_ascii, key_ignored_scan);
  }
  return std::nullopt;
}

/// What a key becomes in walking mode: Esc is `Exit`, and no letter but its
/// `E` does anything.
[[nodiscard]] std::optional<std::uint16_t> in_walking_mode(std::uint16_t key) {
  if (key == key_escape) {
    return key_e;
  }
  const std::uint8_t c = low_of(key);
  if (is_letter(c) && upper(c) != exit_letter) {
    return pair(key_ignored_ascii, key_ignored_scan);
  }
  return std::nullopt;
}

/// Whether the bar the routine is running is a notice: one command, such
/// as the prompt a script raises to wait for Return before it goes on,
/// which offers nothing to choose. A pick-list's bar is not one, however few
/// commands it has: the rows are the choice.
template <const move_mode_facts& F>
[[nodiscard]] bool is_a_notice(cpu::processor& cpu, const seam_context& ctx) {
  const cpu::registers& regs = cpu.regs();
  const std::uint8_t groups = cpu.read_byte(
      regs[cpu::sreg::ss],
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - F.local_group_count));
  return groups == 1 && !menu_bar::called_from(cpu, ctx, F.list_callers);
}

template <const move_mode_facts& F>
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  if (!menu_bar::called_from(cpu, ctx, F.party_callers)) {
    // Another screen's bar: a choice ends the walk, and the party's next
    // bar lights `Move`. A door's bar is part of the walk, and so is a
    // notice that only waits for a key.
    if (!menu_bar::called_from(cpu, ctx, F.door_callers) &&
        !is_a_notice<F>(cpu, ctx)) {
      ctx.set_scratch(scratch_walking, 0);
      ctx.set_scratch(scratch_arrived, 0);
    }
    return;
  }
  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  if (head == cpu.read_word(bda::segment, bda::keyboard_buffer_tail) ||
      box.journal().reader_open() ||
      cpu.read_byte(cpu.regs()[cpu::sreg::ds], F.profile.data_key_pushback) !=
          0) {
    return;
  }
  const std::uint16_t key = cpu.read_word(bda::segment, head);
  const std::optional<std::uint16_t> answer = ctx.scratch(scratch_walking) != 0
                                                  ? in_walking_mode(key)
                                                  : in_menu_mode(F, cpu, key);
  if (answer) {
    cpu.write_word(bda::segment, head, *answer);
  }
}

// --- The definition --------------------------------------------------------

constexpr std::array<seam_point, 5> move_mode_point_table{
    {{.module = por.adventure_overlay,
      .offset = pool_of_radiance_bars[0].call,
      .run = &at_bar_before<pool_of_radiance_move_mode, 0>},
     {.module = por.adventure_overlay,
      .offset = pool_of_radiance_bars[0].return_offset,
      .run = &at_bar_after<pool_of_radiance_move_mode, 0>},
     {.module = por.adventure_overlay,
      .offset = pool_of_radiance_bars[1].call,
      .run = &at_bar_before<pool_of_radiance_move_mode, 1>},
     {.module = por.adventure_overlay,
      .offset = pool_of_radiance_bars[1].return_offset,
      .run = &at_bar_after<pool_of_radiance_move_mode, 1>},
     {.module = por.menu_overlay,
      .offset = por.menu_key_read_call,
      .run = &at_key_read<pool_of_radiance_move_mode>}}};
static_assert(move_mode_point_table.size() == move_mode_point_count);

/// Curse's three: either side of the one bar's call, and the key read.
constexpr std::array<seam_point, 3> curse_move_mode_point_table{
    {{.module = curse.adventure_overlay,
      .offset = curse_bars[0].call,
      .run = &at_bar_before<curse_move_mode, 0>},
     {.module = curse.adventure_overlay,
      .offset = curse_bars[0].return_offset,
      .run = &at_bar_after<curse_move_mode, 0>},
     {.module = curse.menu_overlay,
      .offset = curse.menu_key_read_call,
      .run = &at_key_read<curse_move_mode>}}};
static_assert(curse_move_mode_point_table.size() ==
              curse_move_mode_point_count);

}  // namespace

std::span<const seam_point> move_mode_points() noexcept {
  return move_mode_point_table;
}

std::span<const seam_point> curse_move_mode_points() noexcept {
  return curse_move_mode_point_table;
}

}  // namespace amberfolio::machine
