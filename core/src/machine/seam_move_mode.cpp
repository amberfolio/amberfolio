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
// (seam_journal.cpp), and the journal's points run first at both:
//
//   * **Before**, menu mode: `Move` goes in. The city's first group is four
//     letters and is overwritten with `Move`, its four bytes kept to be put
//     back; the wilderness gets `Move ` in front of its first group, which
//     moves every group up one (27 + 5, and the journal's six, is 38 of 40).
//     Walking mode: the bar becomes `Exit`, its length byte and first four
//     kept, and the journal does not splice `Notes` onto it
//     (`modern_controls_walking()`).
//   * **After**, the bar is put back exactly as it was: the program's string
//     is the program's outside the one call that drew it. Then the letter:
//     `M` off the bar starts walking (the program loops on it), and `E` off
//     the walking bar stops it and is handed back as `-`, which the program
//     loops on, so the party does not camp.
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
// words"): walking, arrived, `Move` lit, which rewrite the bar is carrying,
// and the highlight it was entered with, plus the bytes a rewrite keeps.
// They are configuration: `enable()` starts them at zero, which is menu
// mode with `Move` to be lit.

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
constexpr unsigned scratch_rewrite = 3;
constexpr unsigned scratch_highlight = 4;
constexpr unsigned scratch_kept_first = 5;   // two bytes: length, char 1
constexpr unsigned scratch_kept_second = 6;  // chars 2 and 3
constexpr unsigned scratch_kept_third = 7;   // char 4

/// Which rewrite a bar is carrying between its two points.
enum class rewrite : std::uint8_t { none, city_move, wild_move, walking };

[[nodiscard]] std::uint16_t as_word(rewrite r) noexcept {
  return static_cast<std::uint16_t>(r);
}

[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  return party_select::is_the_data_segment(ctx, ds) ? ds : 0;
}

[[nodiscard]] std::uint16_t at(std::uint16_t base, unsigned by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

[[nodiscard]] bool holds(cpu::processor& cpu, std::uint16_t ds,
                         std::uint16_t bar, std::span<const std::uint8_t> word,
                         unsigned from) {
  for (unsigned i = 0; i < word.size(); ++i) {
    if (cpu.read_byte(ds, at(bar, from + i)) != word[i]) {
      return false;
    }
  }
  return true;
}

void put(cpu::processor& cpu, std::uint16_t ds, std::uint16_t bar,
         std::span<const std::uint8_t> word, unsigned from) {
  for (unsigned i = 0; i < word.size(); ++i) {
    cpu.write_byte(ds, at(bar, from + i), word[i]);
  }
}

[[nodiscard]] std::uint16_t pair(std::uint8_t low, std::uint8_t high) noexcept {
  return static_cast<std::uint16_t>(low | (high << 8U));
}

[[nodiscard]] std::uint8_t low_of(std::uint16_t w) noexcept {
  return static_cast<std::uint8_t>(w & 0xFFU);
}

[[nodiscard]] std::uint8_t high_of(std::uint16_t w) noexcept {
  return static_cast<std::uint8_t>(w >> 8U);
}

/// Keep the length byte and the first four characters of `bar`.
void keep_head(seam_context& ctx, cpu::processor& cpu, std::uint16_t ds,
               std::uint16_t bar) {
  ctx.set_scratch(scratch_kept_first,
                  pair(cpu.read_byte(ds, bar), cpu.read_byte(ds, at(bar, 1))));
  ctx.set_scratch(scratch_kept_second, pair(cpu.read_byte(ds, at(bar, 2)),
                                            cpu.read_byte(ds, at(bar, 3))));
  ctx.set_scratch(scratch_kept_third, cpu.read_byte(ds, at(bar, 4)));
}

/// Put back what `keep_head()` kept; `with_length` false leaves the length.
void put_head_back(seam_context& ctx, cpu::processor& cpu, std::uint16_t ds,
                   std::uint16_t bar, bool with_length) {
  const std::uint16_t first = ctx.scratch(scratch_kept_first);
  const std::uint16_t second = ctx.scratch(scratch_kept_second);
  if (with_length) {
    cpu.write_byte(ds, bar, low_of(first));
  }
  cpu.write_byte(ds, at(bar, 1), high_of(first));
  cpu.write_byte(ds, at(bar, 2), low_of(second));
  cpu.write_byte(ds, at(bar, 3), high_of(second));
  cpu.write_byte(ds, at(bar, 4), low_of(ctx.scratch(scratch_kept_third)));
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

/// The walking bar: `Exit`, lit.
void walking_bar(seam_context& ctx, cpu::processor& cpu, std::uint16_t ds,
                 std::uint16_t bar) {
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (length < word_length || length > bar_capacity) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  keep_head(ctx, cpu, ds, bar);
  cpu.write_byte(ds, bar, static_cast<std::uint8_t>(word_length));
  put(cpu, ds, bar, exit_word, 1);
  ctx.set_scratch(scratch_rewrite, as_word(rewrite::walking));
  ctx.set_scratch(scratch_highlight, cpu.read_byte(ds, data_bar_highlight));
  cpu.write_byte(ds, data_bar_highlight, 1);
}

/// The menu bar: `Move` first. False, and the bar untouched, if the string
/// is not the shape the facts say.
[[nodiscard]] bool menu_bar(seam_context& ctx, cpu::processor& cpu,
                            std::uint16_t ds, std::uint16_t bar, bool city) {
  const std::uint8_t length = cpu.read_byte(ds, bar);
  if (city) {
    // The first group is the overhead view's: a capital and three more,
    // then a separator.
    if (length <= word_length || length > bar_capacity ||
        cpu.read_byte(ds, at(bar, 1)) != 'A' ||
        cpu.read_byte(ds, at(bar, word_length + 1U)) != ' ') {
      return false;
    }
    keep_head(ctx, cpu, ds, bar);
    put(cpu, ds, bar, move_word, 1);
    ctx.set_scratch(scratch_rewrite, as_word(rewrite::city_move));
    return true;
  }
  if (length == 0 || length + inserted_length > bar_capacity) {
    return false;
  }
  for (unsigned nth = length; nth >= 1; --nth) {
    cpu.write_byte(ds, at(bar, nth + inserted_length),
                   cpu.read_byte(ds, at(bar, nth)));
  }
  put(cpu, ds, bar, move_word, 1);
  cpu.write_byte(ds, at(bar, inserted_length), ' ');
  cpu.write_byte(ds, bar, static_cast<std::uint8_t>(length + inserted_length));
  ctx.set_scratch(scratch_rewrite, as_word(rewrite::wild_move));
  return true;
}

void bar_before(machine& box, seam_context& ctx, std::uint16_t bar, bool city) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (ctx.scratch(scratch_rewrite) != as_word(rewrite::none)) {
    return;  // offered again after a batch: the bar is already this piece's.
  }
  if (city && leave_the_overhead_view(box, ctx, ds)) {
    return;
  }
  if (ctx.scratch(scratch_walking) != 0) {
    walking_bar(ctx, cpu, ds, bar);
    return;
  }
  if (ctx.scratch(scratch_arrived) == 0) {
    ctx.set_scratch(scratch_arrived, 1);
    ctx.set_scratch(scratch_move_lit, 1);
  }
  const std::uint8_t entered = cpu.read_byte(ds, data_bar_highlight);
  if (!menu_bar(ctx, cpu, ds, bar, city)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  ctx.set_scratch(scratch_highlight, entered);
  std::uint8_t lit = entered;
  if (ctx.scratch(scratch_move_lit) != 0) {
    lit = 1;
  } else if (!city) {
    lit = static_cast<std::uint8_t>(entered + 1U);
  }
  cpu.write_byte(ds, data_bar_highlight, lit);
}

// --- After it comes back ---------------------------------------------------

/// Put the bar back as it was; false if it was carrying nothing of this
/// piece's.
[[nodiscard]] rewrite put_the_bar_back(seam_context& ctx, cpu::processor& cpu,
                                       std::uint16_t ds, std::uint16_t bar) {
  const auto carried =
      static_cast<rewrite>(low_of(ctx.scratch(scratch_rewrite)));
  ctx.set_scratch(scratch_rewrite, as_word(rewrite::none));
  switch (carried) {
    case rewrite::city_move:
      if (holds(cpu, ds, bar, move_word, 1)) {
        put_head_back(ctx, cpu, ds, bar, false);
      }
      break;
    case rewrite::wild_move: {
      const std::uint8_t length = cpu.read_byte(ds, bar);
      if (length <= inserted_length || length > bar_capacity ||
          !holds(cpu, ds, bar, move_word, 1)) {
        break;
      }
      for (unsigned nth = inserted_length + 1U; nth <= length; ++nth) {
        cpu.write_byte(ds, at(bar, nth - inserted_length),
                       cpu.read_byte(ds, at(bar, nth)));
      }
      cpu.write_byte(ds, bar,
                     static_cast<std::uint8_t>(length - inserted_length));
      break;
    }
    case rewrite::walking:
      if (cpu.read_byte(ds, bar) == word_length &&
          holds(cpu, ds, bar, exit_word, 1)) {
        put_head_back(ctx, cpu, ds, bar, true);
      }
      break;
    case rewrite::none:
      break;
  }
  return carried;
}

void bar_after(machine& box, seam_context& ctx, std::uint16_t bar, bool city) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    return;
  }
  const rewrite carried = put_the_bar_back(ctx, cpu, ds, bar);
  if (carried == rewrite::none) {
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

  if (carried == rewrite::walking) {
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
  bar_after(box, ctx, data_city_bar, true);
}
void at_wild_before(machine& box, seam_context& ctx) {
  bar_before(box, ctx, data_wild_bar, false);
}
void at_wild_after(machine& box, seam_context& ctx) {
  bar_after(box, ctx, data_wild_bar, false);
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

bool move_mode_walking(const machine& box) noexcept {
  return box.seams().scratch(modern_controls_id, scratch_walking) != 0;
}

}  // namespace amberfolio::machine
