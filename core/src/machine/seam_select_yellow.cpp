// SPDX-License-Identifier: AGPL-3.0-only
//
// The select-yellow piece of `modern-controls` (seam_modern_controls.cpp):
// every selection the player can move is drawn as a yellow block with its
// letters cut out of it, and a command's key letter as a white one (#453,
// #483).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and colour numbers. Not one byte of the
// program is reproduced here.
//
// **A selection is drawn in whatever its caller's "bright" colour is, and
// that is white nearly everywhere.** Five routines draw one:
//
//   * **The menu-bar routine's highlighted group** (overlay 25, `0x03BD`).
//     A command bar is a string of words, a word is a group, and the
//     highlighted group is drawn by the routine's leaf at `0x01DA` one
//     character at a time through the program's glyph blitter, every
//     character in the colour its caller passed as `color_hi`. A character
//     outside the highlight is drawn by the same leaf in `color_hi` when
//     it is a **command letter** (one of `0-9` and `A-Z`, which is why
//     the bars are mixed case: a capital is the key, a lower-case letter
//     is the rest of the word) and in `color_lo` when it is not. Callers
//     pass `0x0F` nearly always, and the confirmation prompts of the camp
//     and the party screen pass `0x0E`, the prompts of the character
//     screen `0x0D`.
//   * **The pick-list's highlighted row** (overlay 25, the leaf at
//     `0x0969`, called from the list routine at `0x0D9A`): the selected
//     row's trimmed text, redrawn whole by the program's string routine in
//     the list's `color_hi`.
//   * **The party roster's selected member** (the resident drawer at
//     paragraph `0xBA`, offset `0x0767`): the selected member's name is
//     drawn in white and every other member's in a colour for the
//     member's state. The party-member picker and the party-order screen
//     show their selection through this drawer.
//   * **Modify's selected score** (overlay 19, `0x0901` for the six
//     abilities and the resident `0x0973` for hit points): a score is
//     drawn in green, and light magenta (`0x0D`) when the caller passes a
//     nonzero first argument. Modify is the only caller that does.
//
// Everything is drawn a glyph or a string at a time with the colour as one
// pushed word, or, for Modify, as one local the routine stores before it
// draws. **A byte argument is pushed as a whole register** (`mov al, x` and
// `push ax`), so its high half is whatever the register held, and the
// routine reads the low byte and nothing else. So does this seam: a point
// compares and rewrites the low byte of such a word and leaves the rest.
//
// **Every glyph goes through the blitter** (seam_font.cpp), and the string
// routine is a loop over it, in the blitter's own paragraph. On the EGA the
// blitter takes each of the glyph's eight rows into DL and stores it to each
// page, once for every plane the colour lights; a plane the colour does not
// light it stores as zero. **So a cell is opaque**: whatever was in it is
// gone, and a row stored inverted draws the paper in the colour and the
// letter in black. The blitter and the string routine keep BP frames, with
// the far return address above the saved BP and the arguments above that,
// so from a store instruction the frames lead back to the call that drew
// the glyph.
//
//
// What the seam does
// ------------------
//
// **Rewrites one colour, in one stack word or one local, before a draw.**
// Nothing else: no position, no text, no extra glyph. The program draws in
// its own font, so the faces (`font-sans`, `font-chisel`) letter everything
// here too.
//
//   * **The bar's highlighted group** (point 1, the glyph call in the
//     leaf's highlighted arm). A command letter keeps `color_hi`, and
//     every other character is yellow. Where `color_hi` is already
//     yellow, a command letter is drawn white, so the key still stands out.
//     A bar with a single command letter (a script's one-choice notice) has
//     nothing to select among and is left as the program draws it.
//   * **The pick-list's row** (point 2, the string call): yellow.
//   * **The roster's selected member** (point 3, the string call at the
//     drawer's selected-member arm, `inside_calls`: the automap and the
//     journal give the roster back through a batch): yellow, where the
//     program draws white. A number `hero-keys` draws in front of the name
//     is a hotkey and stays white.
//   * **Modify's selected score** (points 4 and 5, after the colour is
//     chosen and before it is used): yellow where the program chose light
//     magenta.
//
// A point **checks the frame it was told is there** and declines, touching
// nothing, when it is not: the glyph or string call's own other words, and
// the colour the caller's frame says it passed. A colour is never
// rewritten to be what it already is.
//
// **Inverts every glyph of a selection** (points 8 and 9, the blitter's two
// row stores, #483). A colour alone is not a selection: yellow is also a
// hurt character's hit points and the prompt colour of a few questions,
// and white is every key letter. So the store follows the frames back to
// the call that drew its glyph, and inverts DL when that call is one of the
// draws above with the colour this seam gave it: a bar character in the lit
// word, the pick-list's row, the roster's selected member, Modify's
// highlighted score. The colours stay what the points above made them, so
// the lit word is a yellow block and its key letter a white one. A face's
// row is in DL by then: the faces' points are the instructions after the
// fetches, which run before the stores.
//
// **Two other seams draw a selection of their own.** `menu-cursor` draws
// the main menu's cursor row, and the journal its listing's cursor row and
// the lit word of its bar, through the program's string routine in a batch
// (seam.h). Those calls return to the batch's own address, and each sets
// `selection_mark` in the high half of the colour word it pushes: the string
// routine reads the low byte, so the program never sees it, and the store
// inverts what a marked call draws (docs/seams.md §10).
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On, the seam is seen the first time a highlight is drawn: a command bar
// has one, and the party roster has a selected member, from the first
// screen that shows either. There is no "on and untriggered", so the pair
// is a `contrast` (`quiet-select-yellow` against `quiet`), the shape the
// faces' pair has. What the contrast may differ in is the display and the
// video memory, and the points' own stack words, which sit where the
// program leaves them. Off, the engine is not consulted (§7).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// The selection already on the screen at a switch keeps its colour until
// the program draws it again, as a face does. The icon editor's cell cursor
// (a box drawn on a grid of pixels, in a colour that has to show against
// the art) and the combat grid's aim cursor are movable and are not a
// selection among text, and are left as the program draws them.

#include <array>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_menu_bar.h"

namespace amberfolio::machine {
namespace {

// --- The program's colours --------------------------------------------------

constexpr std::uint8_t colour_yellow = 0x0E;
constexpr std::uint8_t colour_white = 0x0F;
constexpr std::uint8_t colour_magenta = 0x0D;
constexpr std::uint8_t colour_green = 0x0A;

// --- The modules ------------------------------------------------------------

/// Overlay 19, the character sheet's: file offset 135226 (`0x2103A`), 11026
/// bytes (`0x2B12`). The manager's record for it is at image `0x0854` and
/// the load-segment word sixteen bytes in, by the search `seam_cheats.cpp`
/// documents (one match; the same search returns the known words for the
/// other modules, docs/seams.md §10).
constexpr std::uint32_t sheet_load_segment_at = 0x0860;

constexpr seam_module sheet_module{
    .file = "GAME.OVR",
    .file_offset = 135226,
    .length = 11026,
    .digest =
        "3011cde3c5b83ac154a7c9f735c4c95d5449154cf250a655992c1fcbb04911e1",
    .load_segment_at = sheet_load_segment_at};

// --- The points -------------------------------------------------------------

/// Overlay 25: the glyph blitter's call in the bar leaf's highlighted arm,
/// and the string drawer's call in the pick-list's highlight-on leaf.
constexpr std::uint32_t bar_group_call = 0x0273;
constexpr std::uint32_t list_row_call = 0x09FD;
constexpr std::uint32_t bar_letter_call = 0x02CA;
constexpr std::uint32_t bar_rest_call = 0x0305;

/// The resident image: the string drawer's call at the roster drawer's
/// selected-member arm, and the instruction after the colour of a hit-point
/// value is chosen.
constexpr std::uint32_t roster_selected_call = 0x13AF;
constexpr std::uint32_t hit_points_chosen = 0x153F;

/// Overlay 19: the instruction after an ability score's colour is chosen.
constexpr std::uint32_t score_chosen = 0x0918;

/// The resident image: the blitter's two EGA row stores, one per page, each
/// the instruction that writes DL to the display (seam_font.cpp has the
/// fetches before them).
constexpr std::array<std::uint32_t, 2> row_store_offsets{0x74A2, 0x74C8};

// --- The calls a store follows back to --------------------------------------

/// Every call below is a direct far call, five bytes, so the return address
/// a callee's frame holds is the call's offset and five.
constexpr std::uint32_t far_call = 5;

/// The string routine's call into the blitter, as the image offset of the
/// instruction after it: the routine pushes CS and calls near, so the
/// blitter's frame holds a far return like any other.
constexpr std::uint32_t string_glyph_return = 0x7724;

/// The string calls of Modify's two score draws: overlay 19's, one for the
/// score and one for exceptional strength's percentage, and the resident hit
/// points'.
constexpr std::uint32_t score_string_call = 0x0976;
constexpr std::uint32_t percentage_string_call = 0x0A41;
constexpr std::uint32_t hit_points_string_call = 0x155E;

// --- The frames the points read ---------------------------------------------

/// The bar leaf's frame, above BP: the caller's BP, as the near argument
/// the leaf reaches the caller's locals through. In the caller's frame, at
/// BP + 0x0E, the colour it was handed for letters and the highlight.
constexpr std::uint16_t leaf_caller_bp = 6;
constexpr std::uint16_t caller_colour_hi = 0x0E;
constexpr std::uint16_t caller_colour_lo = 0x10;

/// The leaf's own locals and argument: the position of the character it is
/// drawing (one-based, a byte at BP - 1) and the group the highlight is on
/// (a byte at BP + 8). And the caller's: the bar, a Pascal string whose
/// length is a byte of its own and whose characters are numbered from one;
/// and the group table, a pair of positions to a group, the first at
/// `caller - 0x8F + 2g` and the last the byte after.
constexpr std::uint16_t leaf_index = 1;
constexpr std::uint16_t leaf_group = 8;
constexpr std::uint16_t caller_bar_length = 0x64;
constexpr std::uint16_t caller_bar = 0x53;
constexpr std::uint16_t caller_groups = 0x8F;

/// The glyph blitter's call, from the top of the stack: six words, the last
/// pushed first. Column and row, then colour, between a count and a
/// character and a fold flag.
constexpr std::uint16_t glyph_fold = 0;
constexpr std::uint16_t glyph_character = 2;
constexpr std::uint16_t glyph_count = 4;
constexpr std::uint16_t glyph_colour = 6;
constexpr std::uint16_t glyph_row = 8;
constexpr std::uint16_t bar_row = 0x18;

/// The string drawer's call, from the top of the stack: the string's offset
/// and segment, then colour, row and column.
constexpr std::uint16_t string_offset = 0;
constexpr std::uint16_t string_segment = 2;
constexpr std::uint16_t string_colour = 4;
constexpr std::uint16_t string_row = 6;
constexpr std::uint16_t string_column = 8;

/// The list leaf's frame: its context's near pointer at BP + 6, and the
/// colour it draws a highlighted row in at +0x20 of that context.
constexpr std::uint16_t list_context = 6;
constexpr std::uint16_t list_colour_hi = 0x20;

/// The roster drawer's frame, below BP: the column and row bytes of the
/// cell it is drawing at and the far pointer to the member (offset, then
/// segment).
constexpr std::uint16_t roster_column = 5;
constexpr std::uint16_t roster_row = 6;
constexpr std::uint16_t roster_member = 4;

/// Modify's two score draws: the first argument is above BP at +6 (a
/// nonzero byte means "highlighted"), and the colour is a local, below BP.
constexpr std::uint16_t score_highlighted = 6;
constexpr std::uint16_t sheet_local_colour = 0x2B;
constexpr std::uint16_t hit_points_local_colour = 1;

/// A callee's frame, above its BP: the caller's BP, the far return address
/// (offset, then segment) and the first of the arguments, the last pushed.
constexpr std::uint16_t frame_saved_bp = 0;
constexpr std::uint16_t frame_return_ip = 2;
constexpr std::uint16_t frame_return_cs = 4;
constexpr std::uint16_t frame_arguments = 6;

/// The string routine's colour, a word in its frame: the third argument
/// from the top, after the string's offset and segment.
constexpr std::uint16_t string_frame_colour = frame_arguments + string_colour;

// --- Reading the machine ----------------------------------------------------

struct stack {
  cpu::processor& cpu;
  std::uint16_t ss;
  std::uint16_t sp;
  std::uint16_t bp;

  /// A word pushed as a word: an offset, a segment, a column.
  [[nodiscard]] std::uint16_t top(std::uint16_t distance) const {
    return cpu.read_word(ss, static_cast<std::uint16_t>(sp + distance));
  }
  /// A byte pushed as a register: the low half of the word.
  [[nodiscard]] std::uint8_t top_byte(std::uint16_t distance) const {
    return cpu.read_byte(ss, static_cast<std::uint16_t>(sp + distance));
  }
  void set_top_byte(std::uint16_t distance, std::uint8_t value) const {
    cpu.write_byte(ss, static_cast<std::uint16_t>(sp + distance), value);
  }
  [[nodiscard]] std::uint16_t above_bp(std::uint16_t distance) const {
    return cpu.read_word(ss, static_cast<std::uint16_t>(bp + distance));
  }
  [[nodiscard]] std::uint16_t word_below_bp(std::uint16_t distance) const {
    return cpu.read_word(ss, static_cast<std::uint16_t>(bp - distance));
  }
  [[nodiscard]] std::uint8_t byte_below_bp(std::uint16_t distance) const {
    return cpu.read_byte(ss, static_cast<std::uint16_t>(bp - distance));
  }
  [[nodiscard]] std::uint8_t byte_above_bp(std::uint16_t distance) const {
    return cpu.read_byte(ss, static_cast<std::uint16_t>(bp + distance));
  }
};

[[nodiscard]] stack stack_of(machine& box) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  return {.cpu = cpu,
          .ss = regs[cpu::sreg::ss],
          .sp = regs[cpu::reg16::sp],
          .bp = regs[cpu::reg16::bp]};
}

/// Whether `c` is a character the menu-bar routine takes as a command
/// letter: the class `0-9` and `A-Z`.
[[nodiscard]] constexpr bool is_command_letter(std::uint8_t c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

// --- Points 1, 6 and 7: the bar ---------------------------------------------
//
// The leaf draws one character at a time through one of three calls: the
// highlighted group's (point 1), a command letter outside it (point 6) and
// any other character outside it (point 7). Which a character goes through
// is the program's group table, and a **group is not a word**: it runs from
// a command letter to two characters before the next, so on a bar whose
// command letters are not the first letters of its words (a digit at the end
// of one) the program's highlight is a word and a half. The seam draws the
// **word**: the run of characters between spaces that holds the group's
// command letter. It is yellow, with each command letter in it in the bar's
// bright; every character outside it is drawn as the bar draws the
// unselected, a command letter in the bright and the rest in the dim. The
// three points make the same decision for their own character, so the
// program's table, its highlight byte and its key matching are not touched
// and a character of the word the program left outside the group is lit by
// the point it goes through.
//
// **A bar with one command letter is not a selection.** It has one group, so
// there is nothing for the highlight to move to, and the script runner hands
// every one-choice prompt (`Press <enter>/<return> to continue`, stored as
// written here: one capital, the rest lower case, though the face draws
// lower case as capitals) to the routine as exactly that. Such a bar is left
// as the program draws it, which is one colour end to end where its bright
// and dim are the same; only the swapped pair below is put right. The rule is
// the number of command letters and not the case: the save and load slot bars
// (`A B C E J`) are all capitals and are real choices, one letter to a group.

/// The pair a few callers hand the bar the wrong way round: white for
/// `color_lo` and green for `color_hi`. Left alone the bar draws its
/// non-letter characters white, its command letters green and its
/// highlighted group green, which is the look of every other bar turned
/// inside out. With the seam on such a bar is drawn the way the others are:
/// the rest of each word green, each command letter white (docs/seams.md
/// §10 names the callers, and why the pair is recognised at the drawer and
/// not by caller).
[[nodiscard]] constexpr bool is_swapped(std::uint8_t color_lo,
                                        std::uint8_t color_hi) noexcept {
  return color_lo == colour_white && color_hi == colour_green;
}

/// What a character is drawn in. `in_word` is whether it is in the
/// highlighted word; `swapped` whether the bar is one of the swapped pairs.
[[nodiscard]] constexpr std::uint8_t bar_colour(
    std::uint8_t character, bool in_word, std::uint8_t color_lo,
    std::uint8_t color_hi) noexcept {
  const bool swapped = is_swapped(color_lo, color_hi);
  const std::uint8_t bright = swapped ? colour_white : color_hi;
  const std::uint8_t dim = swapped ? colour_green : color_lo;
  if (!is_command_letter(character)) {
    return in_word ? colour_yellow : dim;
  }
  // The key stays the bar's bright, unless that is yellow already and the
  // rest of the word is about to be, which would lose it.
  return in_word && bright == colour_yellow ? colour_white : bright;
}

/// What a character of a bar with one command letter is drawn in: what the
/// program chose, except that a swapped pair is put the right way round (its
/// bright green becomes white and its dim white green, which is what
/// `bar_colour` does for the characters of a bar that has a selection).
[[nodiscard]] constexpr std::uint8_t unselected_bar_colour(
    std::uint8_t drawn, std::uint8_t color_lo, std::uint8_t color_hi) noexcept {
  if (!is_swapped(color_lo, color_hi)) {
    return drawn;
  }
  return drawn == color_hi ? colour_white : colour_green;
}

/// What a point reads of the bar: the call's own words and the frames of the
/// leaf and of the routine that drew it.
struct bar_call {
  std::uint8_t color_lo{};
  std::uint8_t color_hi{};
  std::uint8_t character{};
  std::uint8_t colour{};
  bool in_word{false};
  bool one_command{false};
};

/// The widest bar the routine copies (a Pascal string of at most forty).
constexpr std::uint8_t max_bar = 0x28;

/// Read the call and work out whether its character is in the word the
/// highlight is on. False if it is not the call these facts describe.
[[nodiscard]] bool read_bar_call(const stack& s, bar_call& out) {
  out.colour = s.top_byte(glyph_colour);
  out.character = s.top_byte(glyph_character);
  if (s.top_byte(glyph_fold) != 1 || s.top_byte(glyph_count) != 1 ||
      s.top_byte(glyph_row) != bar_row) {
    return false;
  }
  const std::uint16_t caller = s.above_bp(leaf_caller_bp);
  const auto at = [&](std::uint16_t offset) {
    return s.cpu.read_byte(s.ss, static_cast<std::uint16_t>(offset));
  };
  out.color_hi = at(static_cast<std::uint16_t>(caller + caller_colour_hi));
  out.color_lo = at(static_cast<std::uint16_t>(caller + caller_colour_lo));

  // The bar as the routine copied it, the character this call is drawing,
  // and the group the highlight is on.
  const std::uint8_t length =
      at(static_cast<std::uint16_t>(caller - caller_bar_length));
  const std::uint8_t index = s.byte_below_bp(leaf_index);
  const std::uint8_t group = s.byte_above_bp(leaf_group);
  if (length == 0 || length > max_bar || index < 1 || index > length ||
      group < 1 || group > max_bar / 2) {
    return false;
  }
  const auto bar = [&](std::uint8_t position) {
    return at(static_cast<std::uint16_t>(caller - caller_bar + position));
  };
  if (bar(index) != out.character) {
    return false;
  }
  const std::uint8_t letter =
      at(static_cast<std::uint16_t>(caller - caller_groups + (2U * group)));
  if (letter < 1 || letter > length || bar(letter) == ' ') {
    return false;
  }
  std::uint8_t first = letter;
  while (first > 1 && bar(static_cast<std::uint8_t>(first - 1)) != ' ') {
    --first;
  }
  std::uint8_t last = letter;
  while (last < length && bar(static_cast<std::uint8_t>(last + 1)) != ' ') {
    ++last;
  }
  out.in_word = index >= first && index <= last;
  unsigned commands = 0;
  for (std::uint8_t position = 1; position <= length; ++position) {
    if (is_command_letter(bar(position))) {
      ++commands;
    }
  }
  out.one_command = commands == 1;
  return true;
}

/// One of the three bar points. `which` says which colour the program was
/// about to draw in: the bright for the highlight's arm and the letters', the
/// dim for the rest.
void at_bar(machine& box, seam_context& ctx, bool expect_bright,
            bool needs_colour) {
  const stack s = stack_of(box);
  bar_call call;
  if (!read_bar_call(s, call) ||
      call.colour != (expect_bright ? call.color_hi : call.color_lo) ||
      (needs_colour && call.color_hi == 0)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (call.color_hi == 0) {
    // A bar with no colour is not drawn: the main menu's.
    return;
  }
  const std::uint8_t colour =
      call.one_command
          ? unselected_bar_colour(call.colour, call.color_lo, call.color_hi)
          : bar_colour(call.character, call.in_word, call.color_lo,
                       call.color_hi);
  if (colour != call.colour) {
    s.set_top_byte(glyph_colour, colour);
  }
}

void at_bar_group(machine& box, seam_context& ctx) {
  at_bar(box, ctx, true, true);
}
void at_bar_letter(machine& box, seam_context& ctx) {
  at_bar(box, ctx, true, false);
}
void at_bar_rest(machine& box, seam_context& ctx) {
  at_bar(box, ctx, false, false);
}

// --- Point 2: the pick-list's row -------------------------------------------

void at_list_row(machine& box, seam_context& ctx) {
  const stack s = stack_of(box);
  const std::uint8_t colour = s.top_byte(string_colour);
  if (s.top(string_segment) != s.ss) {
    // The row is drawn from a buffer on the leaf's own stack.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint16_t context = s.above_bp(list_context);
  if (s.cpu.read_byte(s.ss, static_cast<std::uint16_t>(
                                context + list_colour_hi)) != colour) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (colour != 0 && colour != colour_yellow) {
    s.set_top_byte(string_colour, colour_yellow);
  }
}

// --- Point 3: the roster's selected member ----------------------------------

void at_selected_member(machine& box, seam_context& ctx) {
  const stack s = stack_of(box);
  // The call's own words against the drawer's frame: the cell and the
  // member it is drawing.
  const bool is_the_call =
      s.top(string_offset) == s.word_below_bp(roster_member) &&
      s.top(string_segment) == s.word_below_bp(roster_member - 2U) &&
      s.top_byte(string_row) == s.byte_below_bp(roster_row) &&
      s.top_byte(string_column) == s.byte_below_bp(roster_column);
  if (!is_the_call) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (s.top_byte(string_colour) == colour_white) {
    s.set_top_byte(string_colour, colour_yellow);
  }
}

// --- Points 4 and 5: Modify's selected score --------------------------------

/// A score draw's colour local: light magenta when the caller said
/// "highlighted", green when it did not. Anything else is not the routine
/// these facts describe.
void choose_score_colour(machine& box, seam_context& ctx, std::uint16_t local,
                         bool may_be_yellow) {
  const stack s = stack_of(box);
  const bool highlighted = s.byte_above_bp(score_highlighted) != 0;
  const std::uint8_t colour = s.byte_below_bp(local);
  if (highlighted) {
    if (colour != colour_magenta) {
      ctx.decline(seam_reason::point_not_recognized);
      return;
    }
    s.cpu.write_byte(s.ss, static_cast<std::uint16_t>(s.bp - local),
                     colour_yellow);
    return;
  }
  if (colour != colour_green && !(may_be_yellow && colour == colour_yellow)) {
    ctx.decline(seam_reason::point_not_recognized);
  }
}

void at_ability_score(machine& box, seam_context& ctx) {
  choose_score_colour(box, ctx, sheet_local_colour, false);
}

void at_hit_points(machine& box, seam_context& ctx) {
  // A hit-point value is yellow when the character is hurt.
  choose_score_colour(box, ctx, hit_points_local_colour, true);
}

// --- Points 8 and 9: the blitter's stores ------------------------------------
//
// A store has nothing to decline: every glyph the program draws passes
// through it, and one that is not a selection is the ordinary case. It looks
// at frames and writes DL, and nothing else.

/// The far return address in the frame at SS:`frame`, as an address.
[[nodiscard]] std::uint32_t return_of(const stack& s, std::uint16_t frame) {
  const std::uint16_t ip = s.cpu.read_word(
      s.ss, static_cast<std::uint16_t>(frame + frame_return_ip));
  const std::uint16_t cs = s.cpu.read_word(
      s.ss, static_cast<std::uint16_t>(frame + frame_return_cs));
  return cpu::physical_address(cs, ip);
}

/// Where the manager says the module whose load segment is kept at `word`
/// starts now, or zero while it is not loaded. Zero matches no return: no
/// caller's code is at the bottom of memory.
[[nodiscard]] std::uint32_t module_start(cpu::processor& cpu,
                                         const seam_context& ctx,
                                         std::uint32_t word) {
  const std::uint16_t segment = menu_bar::loaded_at(cpu, ctx, word);
  return segment == 0 ? 0 : cpu::physical_address(segment, 0);
}

/// Whether the call returning to `from` is one in `module` at `offset`.
[[nodiscard]] constexpr bool returns_to(std::uint32_t from,
                                        std::uint32_t module,
                                        std::uint32_t offset) noexcept {
  return module != 0 && from == module + offset + far_call;
}

/// Whether the glyph the blitter is storing, whose frame is at SS:BP, is a
/// character of a selection.
[[nodiscard]] bool draws_a_selection(const stack& s, const seam_context& ctx) {
  const std::uint32_t from = return_of(s, s.bp);
  const std::uint32_t bar = module_start(s.cpu, ctx, menu_bar::load_segment_at);

  // A bar's character, drawn by the bar leaf a glyph at a time. The call's
  // words are the blitter's arguments and the leaf's frame is the one the
  // blitter saved, so the point's own reading of the bar answers.
  if (returns_to(from, bar, bar_group_call) ||
      returns_to(from, bar, bar_letter_call) ||
      returns_to(from, bar, bar_rest_call)) {
    const stack call{.cpu = s.cpu,
                     .ss = s.ss,
                     .sp = static_cast<std::uint16_t>(s.bp + frame_arguments),
                     .bp = s.cpu.read_word(s.ss, static_cast<std::uint16_t>(
                                                     s.bp + frame_saved_bp))};
    bar_call read;
    return read_bar_call(call, read) && read.in_word && !read.one_command &&
           read.color_hi != 0;
  }

  // Anything else is a string, drawn by the string routine.
  if (from != ctx.image_base() + string_glyph_return) {
    return false;
  }
  const std::uint16_t drawer =
      s.cpu.read_word(s.ss, static_cast<std::uint16_t>(s.bp + frame_saved_bp));
  const std::uint16_t colour = s.cpu.read_word(
      s.ss, static_cast<std::uint16_t>(drawer + string_frame_colour));
  const std::uint32_t caller = return_of(s, drawer);

  // A seam's own selection, drawn in a batch and marked as one.
  if (caller == cpu::physical_address(service::stub_segment,
                                      service::call_return_offset)) {
    return (colour & selection_mark) == selection_mark;
  }
  // The program's, in the yellow the points above gave it.
  if ((colour & 0xFFU) != colour_yellow) {
    return false;
  }
  if (returns_to(caller, bar, list_row_call) ||
      returns_to(caller, ctx.image_base(), roster_selected_call)) {
    return true;
  }
  const std::uint32_t sheet = module_start(s.cpu, ctx, sheet_load_segment_at);
  if (returns_to(caller, sheet, score_string_call) ||
      returns_to(caller, sheet, percentage_string_call) ||
      returns_to(caller, ctx.image_base(), hit_points_string_call)) {
    // Yellow is also a hurt character's hit points: the draw says whether
    // it is the highlighted one.
    const std::uint16_t score = s.cpu.read_word(
        s.ss, static_cast<std::uint16_t>(drawer + frame_saved_bp));
    return s.cpu.read_byte(s.ss, static_cast<std::uint16_t>(
                                     score + score_highlighted)) != 0;
  }
  return false;
}

void at_row_store(machine& box, seam_context& ctx) {
  const stack s = stack_of(box);
  if (!draws_a_selection(s, ctx)) {
    return;
  }
  cpu::registers& regs = box.processor().regs();
  regs.set(cpu::reg8::dl, static_cast<std::uint8_t>(~regs.get(cpu::reg8::dl)));
}

// --- The definition ---------------------------------------------------------

constexpr std::array<seam_point, 9> select_yellow_point_table{
    {{.module = menu_bar::module,
      .offset = bar_group_call,
      .run = &at_bar_group},
     {.module = menu_bar::module, .offset = list_row_call, .run = &at_list_row},
     {.module = resident_image,
      .offset = roster_selected_call,
      .run = &at_selected_member,
      .inside_calls = true},
     {.module = sheet_module, .offset = score_chosen, .run = &at_ability_score},
     {.module = resident_image,
      .offset = hit_points_chosen,
      .run = &at_hit_points},
     {.module = menu_bar::module,
      .offset = bar_letter_call,
      .run = &at_bar_letter},
     {.module = menu_bar::module, .offset = bar_rest_call, .run = &at_bar_rest},
     // The stores run inside batches too: the roster is given back through
     // one, and the seams' own selections are drawn in one.
     {.module = resident_image,
      .offset = row_store_offsets[0],
      .run = &at_row_store,
      .inside_calls = true},
     {.module = resident_image,
      .offset = row_store_offsets[1],
      .run = &at_row_store,
      .inside_calls = true}}};
static_assert(select_yellow_point_table.size() == select_yellow_point_count);

}  // namespace

std::span<const seam_point> select_yellow_points() noexcept {
  return select_yellow_point_table;
}

}  // namespace amberfolio::machine
