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
//     bar that is not the party's.
//
// `Area`, the overhead view, is gone with the piece on: the automap is the
// overhead view (by decision, #479).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here.
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
// **Any other caller of the menu-bar routine ends walking**, and lights
// `Move` for the party's next bar. That is the whole of "the party arrives in
// menu mode": a fight, camp, a shop, a script's question, View and the load
// screen all ask through the same routine.
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

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_key_read.h"
#include "seam_menu_bar.h"
#include "seam_party_select.h"

namespace amberfolio::machine {
namespace {

// --- The adventuring loop's module (overlay 14) ----------------------------

/// The journal's module, restated: each seam carries its own fact table.
constexpr seam_module adventure_module{
    .file = "GAME.OVR",
    .file_offset = 91851,
    .length = 4268,
    .digest =
        "ce2018e8e9d51d422d12e2a8e60837322af70639bcbca2b9fe34ccfd333e2d3a",
    .load_segment_at = menu_bar::adventure_load_segment_at};

/// The call into the menu-bar routine and the instruction after it, per bar.
constexpr std::uint32_t city_before = 0x09D0;
constexpr std::uint32_t city_after = 0x09D5;
constexpr std::uint32_t wild_before = 0x0C40;
constexpr std::uint32_t wild_after = 0x0C45;

/// The loop's out-parameter, below its BP: zero is a letter off the bar.
constexpr std::uint16_t frame_out_flag = 0x04;
/// Where the copy is made, below the loop's BP: right after the empty
/// prompt's length byte (`BP-0x33`), with room for a whole bar before the
/// loop's own bytes at `BP-0x04`.
constexpr std::uint16_t frame_copy = 0x32;
/// At the call: where the bar's far pointer is, above SP.
constexpr std::uint16_t stack_bar_offset = 14;
constexpr std::uint16_t stack_bar_segment = 16;

/// The two callers, by the instruction after their call.
constexpr std::array<menu_bar::caller, 2> adventure_callers{{
    {.load_segment_at = menu_bar::adventure_load_segment_at,
     .return_offset = city_after},
    {.load_segment_at = menu_bar::adventure_load_segment_at,
     .return_offset = wild_after},
}};

// --- The data segment ------------------------------------------------------

constexpr std::uint16_t data_city_bar = 0x04B6;
constexpr std::uint16_t data_wild_bar = 0x04DF;
constexpr std::uint16_t data_bar_highlight = 0x6B2B;
constexpr std::uint16_t data_overhead_view = 0x6AAC;

/// A bar's slot: a length byte and forty characters.
constexpr std::uint8_t bar_capacity = 40;

/// The program's screen composer, reached as paragraph plus offset (the
/// journal's give-back, seam_journal.cpp, has why).
constexpr std::uint16_t composer_paragraph = 0x0BA;
constexpr std::uint16_t composer_offset = 0x27D9;

// --- The menu-bar routine's frame (overlay 25), at its key read -----------

/// The bar the routine parsed, a Pascal string below its BP (`bar-keys`).
constexpr std::uint16_t local_bar = 0x53;

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

[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  return party_select::is_the_data_segment(ctx, ds) ? ds : 0;
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
[[nodiscard]] bool leave_the_overhead_view(machine& box, seam_context& ctx,
                                           std::uint16_t ds) {
  cpu::processor& cpu = box.processor();
  if (cpu.read_byte(ds, data_overhead_view) == 0) {
    return false;
  }
  cpu.write_byte(ds, data_overhead_view, 0);
  // The composer repaints the roster's cells, where the map's panel is.
  box.automap().note_panel_painted_over();
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::array<std::uint16_t, 0> nothing{};
  return ctx.call_program(
      static_cast<std::uint16_t>(image + composer_paragraph), composer_offset,
      nothing);
}

/// The bar of this mode, made in the copy at `SS:copy` from the program's
/// bar at `DS:bar`. False, and the copy not to be used, if the program's
/// string is not the shape the facts say.
[[nodiscard]] bool make_the_bar(cpu::processor& cpu, std::uint16_t ds,
                                std::uint16_t bar, std::uint16_t ss,
                                std::uint16_t copy, bool city, bool walking) {
  if (walking) {
    cpu.write_byte(ss, copy, static_cast<std::uint8_t>(word_length));
    put(cpu, ss, copy, exit_word, 1);
    return true;
  }
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (city) {
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

void bar_before(machine& box, seam_context& ctx, std::uint16_t bar, bool city) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const auto copy =
      static_cast<std::uint16_t>(regs[cpu::reg16::bp] - frame_copy);
  const std::uint16_t handed_offset =
      cpu.read_word(ss, at(sp, stack_bar_offset));
  const std::uint16_t handed_segment =
      cpu.read_word(ss, at(sp, stack_bar_segment));
  if (handed_offset == copy && handed_segment == ss) {
    return;  // offered again after a batch: the call carries the copy already.
  }
  ctx.set_scratch(scratch_handed, as_word(handed::program));
  if (handed_offset != bar || handed_segment != ds) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (city && leave_the_overhead_view(box, ctx, ds)) {
    return;
  }
  const bool walking = ctx.scratch(scratch_walking) != 0;
  if (!make_the_bar(cpu, ds, bar, ss, copy, city, walking)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  cpu.write_word(ss, at(sp, stack_bar_offset), copy);
  cpu.write_word(ss, at(sp, stack_bar_segment), ss);

  const std::uint8_t entered = cpu.read_byte(ds, data_bar_highlight);
  ctx.set_scratch(scratch_highlight, entered);
  if (walking) {
    ctx.set_scratch(scratch_handed, as_word(handed::walking));
    cpu.write_byte(ds, data_bar_highlight, 1);
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
  } else if (!city) {
    lit = static_cast<std::uint8_t>(entered + 1U);
  }
  cpu.write_byte(ds, data_bar_highlight, lit);
}

// --- After it comes back ---------------------------------------------------

void bar_after(machine& box, seam_context& ctx, bool city) {
  cpu::processor& cpu = box.processor();
  const auto carried = static_cast<handed>(low_of(ctx.scratch(scratch_handed)));
  ctx.set_scratch(scratch_handed, as_word(handed::program));
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0 || carried == handed::program) {
    return;
  }

  cpu::registers& regs = cpu.regs();
  const bool off_the_bar =
      cpu.read_byte(regs[cpu::sreg::ss],
                    static_cast<std::uint16_t>(regs[cpu::reg16::bp] -
                                               frame_out_flag)) == 0;
  const std::uint8_t letter = regs.get(cpu::reg8::al);
  const auto entered =
      static_cast<std::uint8_t>(ctx.scratch(scratch_highlight));

  if (carried == handed::walking) {
    cpu.write_byte(ds, data_bar_highlight, entered);
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
    cpu.write_byte(ds, data_bar_highlight, entered);
    return;
  }
  const std::uint8_t lit = cpu.read_byte(ds, data_bar_highlight);
  if (lit == 1) {
    ctx.set_scratch(scratch_move_lit, 1);
    cpu.write_byte(ds, data_bar_highlight, entered);
  } else {
    ctx.set_scratch(scratch_move_lit, 0);
    if (!city) {
      cpu.write_byte(ds, data_bar_highlight,
                     static_cast<std::uint8_t>(lit - 1U));
    }
  }
  if (off_the_bar && letter == move_letter) {
    ctx.set_scratch(scratch_walking, 1);
  }
}

void at_city_before(machine& box, seam_context& ctx) {
  bar_before(box, ctx, data_city_bar, true);
}
void at_city_after(machine& box, seam_context& ctx) {
  bar_after(box, ctx, true);
}
void at_wild_before(machine& box, seam_context& ctx) {
  bar_before(box, ctx, data_wild_bar, false);
}
void at_wild_after(machine& box, seam_context& ctx) {
  bar_after(box, ctx, false);
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
[[nodiscard]] bool on_the_bar(cpu::processor& cpu, std::uint8_t letter) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const auto bar = static_cast<std::uint16_t>(regs[cpu::reg16::bp] - local_bar);
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
[[nodiscard]] std::optional<std::uint16_t> in_menu_mode(cpu::processor& cpu,
                                                        std::uint16_t key) {
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
  if (is_letter(c) && !on_the_bar(cpu, upper(c))) {
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

void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  if (!menu_bar::called_from(cpu, ctx, adventure_callers)) {
    // Another screen's bar: the walk is over, and the party's next bar
    // lights `Move`.
    ctx.set_scratch(scratch_walking, 0);
    ctx.set_scratch(scratch_arrived, 0);
    return;
  }
  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  if (head == cpu.read_word(bda::segment, bda::keyboard_buffer_tail) ||
      box.journal().reader_open() ||
      cpu.read_byte(cpu.regs()[cpu::sreg::ds], menu_bar::data_key_pushback) !=
          0) {
    return;
  }
  const std::uint16_t key = cpu.read_word(bda::segment, head);
  const std::optional<std::uint16_t> answer = ctx.scratch(scratch_walking) != 0
                                                  ? in_walking_mode(key)
                                                  : in_menu_mode(cpu, key);
  if (answer) {
    cpu.write_word(bda::segment, head, *answer);
  }
}

// --- The definition --------------------------------------------------------

constexpr std::array<seam_point, 5> move_mode_point_table{
    {{.module = adventure_module,
      .offset = city_before,
      .run = &at_city_before},
     {.module = adventure_module, .offset = city_after, .run = &at_city_after},
     {.module = adventure_module,
      .offset = wild_before,
      .run = &at_wild_before},
     {.module = adventure_module, .offset = wild_after, .run = &at_wild_after},
     {.module = menu_bar::module,
      .offset = menu_bar::key_read_call,
      .run = &at_key_read}}};
static_assert(move_mode_point_table.size() == move_mode_point_count);

}  // namespace

std::span<const seam_point> move_mode_points() noexcept {
  return move_mode_point_table;
}

}  // namespace amberfolio::machine
