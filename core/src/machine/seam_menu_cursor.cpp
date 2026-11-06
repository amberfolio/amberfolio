// SPDX-License-Identifier: AGPL-3.0-only
//
// The menu-cursor piece of `modern-controls` (seam_modern_controls.cpp): a
// cursor on the main menu's commands, drawn from the moment the menu is. Up and
// Down move it, and Return takes the command it is on (#434, #453).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here.
//
// **The main menu is one loop, in overlay 16.** It is the party-setup
// screen at the start of the program, and it is the same loop again when a
// script re-enters it (a training hall), so there is one call site for
// both. The loop draws a frame, the party, and then the commands: eleven
// records in the data segment, from `0x0619`, forty-two bytes each, a
// Pascal string followed at `+0x29` by an **enable byte**. A record whose
// enable byte is not zero is drawn, packed from text row `0x0C` and one row
// to a command: the first letter in white (`0x0F`) at column 2, and the rest
// of the word in green (`0x0A`) from column 3. Which commands are enabled is
// decided by that same block, each time it runs. With no party member it
// writes Drop, Modify, Train, View, Remove, Save and Begin off and Load on;
// with one it writes Drop, Modify, View, Remove, Save and Begin on, Load off,
// and Train on where the place trains. Create, Add and Exit it never
// writes, and they are on from the start. So the menu shows four commands
// with no party (Create, Add, Load, Exit), nine with one, and ten at a
// training hall.
//
// It then asks the menu-bar routine (overlay 25, `0x03BD`) for a key, with
// the bar `C D M T V A R L S B E J`, both bar colours **zero** and the raw
// mode argument set. A bar with no colour is not drawn, so **the menu has
// no highlight at all**, and the routine takes Enter only from a bar that
// has a colour, so Enter is dropped. A command is taken only by its letter.
// An arrow is handed back with the out-parameter set; the loop sends the
// scan codes `G` and `O` (Home and End) to the party cursor, drops any
// other, and goes back to the call **without redrawing**. A letter it was
// given, whether or not its command is enabled, ends in a **full redraw**:
// the frame, the party and the commands are drawn again, and so, in
// particular, whatever was painted over them is gone.
//
//
// What the seam does
// ------------------
//
// **Two points.**
//
//   * **The loop's call into the menu-bar routine** (overlay 16, `0x02F8`,
//     the instruction the facts above call the call). Reached every time
//     the loop asks for a key, and so as soon as the menu is drawn: with no
//     cursor on the screen the handler lights the **first** command shown,
//     which is what makes the cursor visible from the start (#453).
//   * **The menu-bar routine's key-read call**, the one `bar-keys` reads
//     at (overlay 25, `0x0572`, reached only when a key is waiting). The
//     call is the main menu's when the routine's far return address is
//     overlay 16 (the manager's word at `0x0790`) at `0x02FD`. Every other
//     caller of the routine is left alone.
//
// What it does there:
//
//   * **Up and Down** (scan `0x48` and `0x50`, character zero) move the
//     cursor over the **enabled** commands, in the order the menu draws
//     them, wrapping at both ends. Disabled commands are not drawn and are
//     skipped. The cursor is drawn by the program's own string routine: the
//     first letter of the command it moved to as a white block and the
//     rest of the row as a block in the selection's colour (yellow here),
//     the look `select-yellow` gives every
//     selection: the colours are the call's, and `selection_mark` in each
//     colour word is what has `select-yellow` invert the glyphs (docs/seams.md
//     §10). The word it left is put back in the two colours
//     the menu draws it in. The key is then replaced by a character the
//     routine ignores, which keeps it waiting (the batch that draws offers
//     the point again, and the key must not be moved on twice).
//   * **Return**, with the cursor drawn, becomes the command's letter, in
//     the BIOS ring before the program reads it. The program takes the
//     command by the route a typed letter takes. With no cursor drawn
//     Return is left alone, and the program drops it as it always did.
//   * **Every other key is the program's**: a typed letter, Home, End.
//
// **Visible from the start.** The cursor is on the **first** command shown
// as soon as the menu is drawn, so the first press moves it: Down, Down
// takes the third command shown. A menu the loop draws again (a command
// was taken, a screen was left) wipes the cursor, and the loop's next
// call puts it back on the first.
//
// **A second press while the cursor is being drawn.** The program draws a
// glyph at a time, as it draws everything, and moving the cursor costs
// about a tenth of a second of the machine's time. A key pressed in that
// time waits behind the placeholder, and the program's read keeps the last
// key waiting and throws the rest away, so the placeholder is taken off
// first and the key behind it, if it is Up, Down or Return, is the one
// handled. Nothing else is ever taken off the ring.
//
// **Where the cursor lives.** In the program's own memory, in the one
// place its redraw also writes. The seam keeps no word of its own. The menu
// block rewrites the enable bytes of Drop and Load every time it runs, and
// exactly one of the two is on: Drop with a party member, Load without. The
// byte that is on holds `1`, and the seam holds **`2` plus the row of the
// command the cursor is on** in it instead while the cursor is drawn.
// Every reader of an enable byte tests it against zero (the menu's draw and
// dispatch; no other routine reads it), so the program cannot tell. And
// since the cursor is painted on the screen the menu's redraw wipes, the
// redraw that wipes it also puts the byte back: **the cursor lasts exactly
// as long as its pixels**, with nothing for the seam to remember and nothing
// to clear. A command taken, by its letter or by Return, ends in a redraw,
// so the next Up or Down begins again from the first command.
//
// **Why not the bar's highlight byte** (`0x6B2B`, the one every bar shares)
// as the cursor. It is the natural place, and the first design. It cannot
// say whether the cursor is drawn: a typed letter sets it, so a menu that
// had only been *used with the keyboard* would grow a cursor on its return
// from the command, and Return would take a command nobody could see. The
// program's own steps of it (`,` and `.`, which `bar-keys` gives to Left
// and Right) move it with nothing drawn. And it is not where the menu left
// it: the screen a command opens runs a bar of its own, which sets the same
// byte, so back at the menu it held that bar's group (measured: 2 after the
// add screen, 1 after the view screen), not the command's. The seam leaves
// it alone, and a cursor that has just come back from a command starts from
// the first.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On, the cursor is seen as soon as the main menu is drawn, so a run that
// reaches the menu is not the run with the seam off, whatever keys are
// pressed: the pair is a `contrast`, and so is the one for a cursor that
// moves (tests/sessions/README.md). It was an `identical` while the cursor
// was hidden until the first Up or Down (#434).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// **On Curse of the Azure Bonds** (#555) the loop is the same loop, in
// overlay 18: twelve records from DGROUP `0x0553` with Human Change Classes
// fifth, the same enable bytes at `+0x29` written the same way (Drop on and
// Load, now the ninth record, off with a party member, the reverse without;
// Create, Add and Exit never written), the same packed rows from `0x0C` in
// the same colours, and the same call into the menu-bar routine with no
// colour and raw mode set, at `0x038E` (return `0x0393`), with the bar at
// `0x052A` and the prompt's far pointer pushed before it. Its string drawer
// takes a paper colour, handed black. So the cursor lives where it lives on
// Pool of Radiance, in the enable byte of Drop or Load, and the script
// opcode that re-enters the menu (a training hall, and the end of the game)
// reaches the same call. The cursor's row is a white block, the colour of
// the program's own highlight (the profile's `selection_colour`).
//
// Nothing outstanding. The keypad's 8 and 2 are not taken (the routine
// turns them into the movement letters itself, and the party's cursor is
// the program's there), and nothing here is done for a screen other than
// the main menu: the pick-lists' Up and Down are `list-arrows`.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_key_read.h"

namespace amberfolio::machine {
namespace {

// --- One program's facts ---------------------------------------------------

/// What this piece needs to know about one program, apart from its profile
/// (the menu overlay's read call, the roster overlay, the bar's local copy,
/// the pushback slot, the string drawer, the selection's colour): the loop's
/// call, the command records and the bar the loop passes.
struct menu_cursor_facts {
  const program_profile& profile;

  /// In the profile's `roster_overlay`: the loop's far call into the
  /// menu-bar routine, after the menu is drawn and the arguments are pushed,
  /// and the instruction after it, the return address the routine's frame
  /// holds.
  std::uint32_t loop_call;
  std::uint16_t loop_return;

  /// The command records in the data segment: where the first is, and how
  /// many. Each is forty-two bytes, a Pascal string and, at `+0x29`, the
  /// enable byte.
  std::uint16_t data_records;
  std::uint8_t record_count;

  /// The two records whose enable byte the menu's redraw rewrites on every
  /// run and of which exactly one is on: Drop, on with a party member, and
  /// Load, on without.
  std::uint8_t record_with_member;
  std::uint8_t record_without_member;

  /// The bar the loop hands the routine, in the data segment.
  std::uint16_t data_bar;

  /// Whether the string drawer takes the paper's colour, a word pushed
  /// between the colour and the string (black here).
  bool string_takes_paper;
};

/// Pool of Radiance: the baseline edition (program_profile.h). The loop is
/// overlay 16's, eleven records from DGROUP `0x0619`, the bar at `0x05F0`.
constexpr menu_cursor_facts pool_of_radiance_menu_cursor{
    .profile = pool_of_radiance,
    .loop_call = 0x02F8,
    .loop_return = 0x02FD,
    .data_records = 0x0619,
    .record_count = 11,
    .record_with_member = 1,
    .record_without_member = 7,
    .data_bar = 0x05F0,
    .string_takes_paper = false};

/// Curse of the Azure Bonds (#555). The loop is overlay 18's (the profile's
/// `roster_overlay`), twelve records from DGROUP `0x0553` (Human Change
/// Classes is the fifth), the bar at `0x052A`; Load is the ninth record.
/// The string drawer takes the paper.
constexpr menu_cursor_facts curse_menu_cursor{
    .profile = curse_of_the_azure_bonds,
    .loop_call = 0x038E,
    .loop_return = 0x0393,
    .data_records = 0x0553,
    .record_count = 12,
    .record_with_member = 1,
    .record_without_member = 8,
    .data_bar = 0x052A,
    .string_takes_paper = true};

/// The most records a program here has.
constexpr std::uint8_t max_records = 12;
static_assert(pool_of_radiance_menu_cursor.record_count <= max_records &&
              curse_menu_cursor.record_count <= max_records);

// --- The routine's frame ---------------------------------------------------

constexpr std::uint16_t frame_return_ip = 2;
constexpr std::uint16_t frame_return_cs = 4;

// --- The data segment ------------------------------------------------------

/// A command record: forty-two bytes, a Pascal string (length, then the
/// characters) and, at `+0x29`, the enable byte.
constexpr std::uint16_t record_stride = 0x2A;
constexpr std::uint16_t record_enable = 0x29;
constexpr std::uint8_t max_record_text = 0x28;

/// The loop's call into the menu-bar routine, from the top of the stack:
/// the offset and the segment of the out-parameter, the animation flag,
/// the raw-mode flag, both bar colours, the prompt colour, and the bar's
/// far pointer (offset, then segment). The same on both programs; Curse's
/// routine takes the prompt's far pointer below them.
constexpr std::uint16_t call_out_segment = 2;
constexpr std::uint16_t call_anim = 4;
constexpr std::uint16_t call_raw = 6;
constexpr std::uint16_t call_colour_hi = 8;
constexpr std::uint16_t call_colour_lo = 10;
constexpr std::uint16_t call_prompt_colour = 12;
constexpr std::uint16_t call_bar_offset = 14;
constexpr std::uint16_t call_bar_segment = 16;
constexpr std::uint16_t prompt_colour = 0x0D;

/// What the cursor's byte holds with the cursor on row `r`: `cursor_base +
/// r`. The menu's redraw leaves it at one, which is below any row's.
constexpr std::uint8_t cursor_base = 2;

// --- The screen ------------------------------------------------------------

/// The first row a command is drawn on, and the two columns: the first
/// letter at the first, the rest of the word at the second.
constexpr std::uint16_t first_row = 0x0C;
constexpr std::uint16_t letter_column = 2;
constexpr std::uint16_t rest_column = 3;

/// The menu's colours: the first letter (white) and the rest of the word
/// (green), on black. The cursor's word is drawn in the profile's selection
/// colour.
constexpr std::uint16_t colour_letter = 0x0F;
constexpr std::uint16_t colour_rest = 0x0A;
constexpr std::uint16_t colour_paper = 0x00;

// --- The keys, as the BIOS ring holds them: scan code high, character low --

constexpr std::uint16_t key_up = 0x4800;
constexpr std::uint16_t key_down = 0x5000;
constexpr std::uint16_t key_enter = 0x1C0D;

/// The scan code a command letter is posted under: Enter's own, because the
/// program reads the character and nothing else.
constexpr std::uint16_t scan_enter = 0x1C00;

/// Whether `c` is a character the routine takes as a command letter.
[[nodiscard]] constexpr bool is_command_letter(std::uint8_t c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr std::uint8_t upper(std::uint8_t c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<std::uint8_t>(c - 'a' + 'A') : c;
}

/// The segment the program says `at` is loaded at now; zero while it is
/// not loaded.
[[nodiscard]] std::uint16_t loaded_at(cpu::processor& cpu,
                                      const seam_context& ctx,
                                      std::uint32_t at) {
  return cpu.read_word(static_cast<std::uint16_t>(ctx.image_base() >> 4U),
                       static_cast<std::uint16_t>(at));
}

/// Whether this call of the menu-bar routine is the party-setup loop's.
[[nodiscard]] bool called_from_the_main_menu(const menu_cursor_facts& f,
                                             cpu::processor& cpu,
                                             const seam_context& ctx) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t ip =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_ip));
  const std::uint16_t cs =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_cs));
  if (ip != f.loop_return) {
    return false;
  }
  const std::uint16_t segment =
      loaded_at(cpu, ctx, f.profile.roster_overlay.load_segment_at);
  return segment != 0 && segment == cs;
}

// --- The menu, read --------------------------------------------------------

/// What the menu shows: the records whose enable byte is not zero, in the
/// order the menu draws them, and where the cursor is among them.
struct menu_reading {
  std::array<std::uint8_t, max_records> record{};
  std::uint8_t shown{0};

  /// The enable byte the cursor lives in, as an offset in the data segment,
  /// and what it holds.
  std::uint16_t cursor_byte{0};
  std::uint8_t cursor_value{0};
};

[[nodiscard]] constexpr std::uint16_t record_at(const menu_cursor_facts& f,
                                                std::uint8_t record) noexcept {
  return static_cast<std::uint16_t>(f.data_records + (record * record_stride));
}

[[nodiscard]] constexpr std::uint16_t enable_at(const menu_cursor_facts& f,
                                                std::uint8_t record) noexcept {
  return static_cast<std::uint16_t>(record_at(f, record) + record_enable);
}

/// Read the menu. False when it is not a menu these facts describe: neither
/// or both of the two enable bytes the cursor could live in are on.
[[nodiscard]] bool read_menu(const menu_cursor_facts& f, cpu::processor& cpu,
                             std::uint16_t ds, menu_reading& out) {
  out = {};
  for (std::uint8_t r = 0; r < f.record_count; ++r) {
    if (cpu.read_byte(ds, enable_at(f, r)) != 0) {
      out.record[out.shown++] = r;
    }
  }
  const std::uint16_t with_member = enable_at(f, f.record_with_member);
  const std::uint16_t without_member = enable_at(f, f.record_without_member);
  const std::uint8_t drop = cpu.read_byte(ds, with_member);
  const std::uint8_t load = cpu.read_byte(ds, without_member);
  if ((drop != 0) == (load != 0)) {
    return false;
  }
  out.cursor_byte = drop != 0 ? with_member : without_member;
  out.cursor_value = drop != 0 ? drop : load;
  return true;
}

/// The row the cursor is on, or `shown` when it is not drawn.
[[nodiscard]] std::uint8_t cursor_row(const menu_reading& menu) noexcept {
  if (menu.cursor_value < cursor_base) {
    return menu.shown;
  }
  const auto row = static_cast<std::uint8_t>(menu.cursor_value - cursor_base);
  return row < menu.shown ? row : menu.shown;
}

/// The record's Pascal string: its length, clamped to what a record holds.
[[nodiscard]] std::uint8_t text_length(const menu_cursor_facts& f,
                                       cpu::processor& cpu, std::uint16_t ds,
                                       std::uint8_t record) {
  const std::uint8_t length = cpu.read_byte(ds, record_at(f, record));
  return length > max_record_text ? max_record_text : length;
}

// --- Return ----------------------------------------------------------------

/// The keystroke Return should become: the letter of the command the
/// cursor is on, posted under Return's own scan code. Zero if there is
/// nothing to answer. Declines when the letter is not one the routine's bar
/// holds, which no menu these facts describe produces.
[[nodiscard]] std::uint16_t letter_for_return(const menu_cursor_facts& f,
                                              cpu::processor& cpu,
                                              seam_context& ctx,
                                              const menu_reading& menu,
                                              std::uint8_t row) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];

  const std::uint8_t record = menu.record[row];
  if (text_length(f, cpu, ds, record) == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  const std::uint8_t letter = upper(
      cpu.read_byte(ds, static_cast<std::uint16_t>(record_at(f, record) + 1U)));
  if (!is_command_letter(letter)) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }

  // The routine takes a letter that is in the bar it was handed, and reads
  // the bar from a copy in its own frame: look where it looks.
  const std::uint8_t length = cpu.read_byte(
      ss, static_cast<std::uint16_t>(bp - f.profile.menu_local_bar));
  if (length == 0 || length > f.profile.menu_bar_capacity) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  for (std::uint8_t position = 1; position <= length; ++position) {
    const std::uint8_t c = cpu.read_byte(
        ss,
        static_cast<std::uint16_t>((bp - f.profile.menu_local_bar) + position));
    if (c == letter) {
      return static_cast<std::uint16_t>(scan_enter | letter);
    }
  }
  ctx.decline(seam_reason::point_not_recognized);
  return 0;
}

// --- Up and Down -----------------------------------------------------------

/// One call to the program's own string drawer.
[[nodiscard]] bool draw(const menu_cursor_facts& f, seam_context& ctx,
                        std::uint16_t image, std::uint16_t row,
                        std::uint16_t column, std::uint16_t colour,
                        std::uint16_t segment, std::uint16_t offset) {
  if (f.string_takes_paper) {
    const std::array<std::uint16_t, 6> where{column,       row,     colour,
                                             colour_paper, segment, offset};
    return ctx.call_program(image, f.profile.draw_string_entry, where);
  }
  const std::array<std::uint16_t, 5> where{column, row, colour, segment,
                                           offset};
  return ctx.call_program(image, f.profile.draw_string_entry, where);
}

/// Draw the word `row` stands for the way the menu draws it: the first
/// letter white and the rest green; or, for the cursor's row, as a
/// selection (`selection_mark`, #483): the letter a white block and the
/// rest a block in the selection's colour, yellow on the baseline and
/// white on Curse.
[[nodiscard]] bool draw_word(const menu_cursor_facts& f, seam_context& ctx,
                             cpu::processor& cpu, std::uint16_t image,
                             std::uint16_t ds, const menu_reading& menu,
                             std::uint8_t row, bool selected) {
  const std::uint16_t mark = selected ? selection_mark : 0;
  const auto letter = static_cast<std::uint16_t>(colour_letter | mark);
  const auto rest = static_cast<std::uint16_t>(
      (selected ? f.profile.selection_colour : colour_rest) | mark);
  const std::uint8_t record = menu.record[row];
  const std::uint8_t length = text_length(f, cpu, ds, record);
  if (length == 0) {
    return true;
  }
  const auto screen_row = static_cast<std::uint16_t>(first_row + row);

  std::array<std::uint8_t, 2> head{
      1,
      cpu.read_byte(ds, static_cast<std::uint16_t>(record_at(f, record) + 1U))};
  std::array<std::uint8_t, max_record_text> tail{};
  tail[0] = static_cast<std::uint8_t>(length - 1U);
  for (std::uint8_t i = 1; i < length; ++i) {
    tail[i] = cpu.read_byte(
        ds, static_cast<std::uint16_t>(record_at(f, record) + 1U + i));
  }

  std::uint16_t head_segment = 0;
  std::uint16_t head_offset = 0;
  std::uint16_t tail_segment = 0;
  std::uint16_t tail_offset = 0;
  if (!ctx.place_bytes(head, head_segment, head_offset)) {
    return false;
  }
  if (length > 1 &&
      !ctx.place_bytes(std::span<const std::uint8_t>(tail.data(), length),
                       tail_segment, tail_offset)) {
    return false;
  }
  if (!draw(f, ctx, image, screen_row, letter_column, letter, head_segment,
            head_offset)) {
    return false;
  }
  return length == 1 || draw(f, ctx, image, screen_row, rest_column, rest,
                             tail_segment, tail_offset);
}

/// The slot after `slot` in the BIOS ring.
[[nodiscard]] constexpr std::uint16_t next_slot(std::uint16_t slot) noexcept {
  const auto next = static_cast<std::uint16_t>(slot + 2U);
  return next >= bda::keyboard_buffer_end ? bda::keyboard_buffer : next;
}

/// The keystroke the seam leaves where an Up or a Down was: one the routine
/// reads and throws away.
constexpr auto key_placeholder =
    static_cast<std::uint16_t>((key_ignored_scan << 8U) | key_ignored_ascii);

[[nodiscard]] constexpr bool is_ours(std::uint16_t key) noexcept {
  return key == key_up || key == key_down || key == key_enter;
}

template <const menu_cursor_facts& F>
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  std::uint16_t head = cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  const std::uint16_t tail =
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
  if (head == tail) {
    // Empty: the poll was answered from the pushback slot.
    return;
  }
  std::uint16_t key = cpu.read_word(bda::segment, head);
  // The seam's own placeholder at the head with a key of the seam's behind
  // it: the player pressed again while the cursor was being drawn. The
  // program's read keeps the last key waiting and drops the others, so the
  // placeholder is taken off and the key behind it is the one handled.
  const bool skips_placeholder =
      key == key_placeholder && next_slot(head) != tail &&
      is_ours(cpu.read_word(bda::segment, next_slot(head)));
  if (!is_ours(key) && !skips_placeholder) {
    return;
  }
  if (box.journal().reader_open()) {
    // The reader takes every key at the poll; one that got past it is the
    // reader's, not the menu's.
    return;
  }
  const std::uint16_t ds = regs[cpu::sreg::ds];
  if (cpu.read_byte(ds, F.profile.data_key_pushback) != 0) {
    // The head of the ring is not the key about to be read.
    return;
  }
  if (!called_from_the_main_menu(F, cpu, ctx)) {
    return;
  }
  if (skips_placeholder) {
    head = next_slot(head);
    cpu.write_word(bda::segment, bda::keyboard_buffer_head, head);
    key = cpu.read_word(bda::segment, head);
  }

  menu_reading menu;
  if (!read_menu(F, cpu, ds, menu) || menu.shown == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint8_t lit = cursor_row(menu);

  if (key == key_enter) {
    if (lit == menu.shown) {
      // No cursor is drawn: the program drops a Return here.
      return;
    }
    const std::uint16_t answer = letter_for_return(F, cpu, ctx, menu, lit);
    if (answer != 0) {
      cpu.write_word(bda::segment, head, answer);
    }
    return;
  }

  // The cursor starts on the first command; Up and Down move it from there.
  const std::uint8_t from = lit == menu.shown ? 0 : lit;
  const std::uint8_t to =
      key == key_down
          ? static_cast<std::uint8_t>((from + 1U) % menu.shown)
          : static_cast<std::uint8_t>((from + menu.shown - 1U) % menu.shown);

  // The word it moves to is lit first, so that the cursor is on the screen
  // before the one it leaves is put back.
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  const bool was_drawn = lit != menu.shown;
  if (!draw_word(F, ctx, cpu, image, ds, menu, to, true) ||
      (was_drawn && lit != to &&
       !draw_word(F, ctx, cpu, image, ds, menu, lit, false))) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  cpu.write_byte(ds, menu.cursor_byte,
                 static_cast<std::uint8_t>(cursor_base + to));
  // Answered: the key becomes one the routine throws away, and the routine
  // goes back to waiting. The batch offers this point again when it is done,
  // and must find nothing to move.
  cpu.write_word(bda::segment, head, key_placeholder);
}

// --- The menu is drawn -------------------------------------------------------

/// The loop is about to ask the menu-bar routine for a key. The menu has
/// just been drawn, or has been all along; the cursor is on the screen
/// when the enable byte it lives in says so, and when it is not the first
/// command is lit.
template <const menu_cursor_facts& F>
void at_menu_drawn(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const auto top = [&](std::uint16_t distance) {
    return cpu.read_word(ss, static_cast<std::uint16_t>(sp + distance));
  };
  // A byte argument is pushed as a whole register, so its high half is
  // whatever the register held: the routine reads the low byte only.
  const auto top_byte = [&](std::uint16_t distance) {
    return cpu.read_byte(ss, static_cast<std::uint16_t>(sp + distance));
  };
  // The call's own words: the loop's bar, with both colours zero and raw
  // mode set, in the data segment the facts name.
  const bool is_the_call =
      ds == static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                       F.profile.dgroup_paragraphs) &&
      top(call_out_segment) == ss && top_byte(call_anim) == 0 &&
      top_byte(call_raw) == 1 && top_byte(call_colour_hi) == 0 &&
      top_byte(call_colour_lo) == 0 &&
      top_byte(call_prompt_colour) == prompt_colour &&
      top(call_bar_offset) == F.data_bar && top(call_bar_segment) == ds;
  if (!is_the_call) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  menu_reading menu;
  if (!read_menu(F, cpu, ds, menu) || menu.shown == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (cursor_row(menu) != menu.shown) {
    // The cursor is on the screen: this is the loop coming back, or this
    // handler's own batch having finished.
    return;
  }
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  if (!draw_word(F, ctx, cpu, image, ds, menu, 0, true)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  cpu.write_byte(ds, menu.cursor_byte, cursor_base);
}

/// A program's two points: the key read inside the menu-bar routine, and
/// the loop's call into it.
template <const menu_cursor_facts& F>
constexpr std::array<seam_point, 2> menu_cursor_points_of() {
  return {{{.module = F.profile.menu_overlay,
            .offset = F.profile.menu_key_read_call,
            .run = &at_key_read<F>},
           {.module = F.profile.roster_overlay,
            .offset = F.loop_call,
            .run = &at_menu_drawn<F>}}};
}

constexpr auto menu_cursor_point_table =
    menu_cursor_points_of<pool_of_radiance_menu_cursor>();
static_assert(menu_cursor_point_table.size() == menu_cursor_point_count);

constexpr auto curse_menu_cursor_point_table =
    menu_cursor_points_of<curse_menu_cursor>();
static_assert(curse_menu_cursor_point_table.size() == menu_cursor_point_count);

}  // namespace

std::span<const seam_point> menu_cursor_points() noexcept {
  return menu_cursor_point_table;
}

std::span<const seam_point> curse_menu_cursor_points() noexcept {
  return curse_menu_cursor_point_table;
}

}  // namespace amberfolio::machine
