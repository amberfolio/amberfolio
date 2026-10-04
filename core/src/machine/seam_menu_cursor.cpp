// SPDX-License-Identifier: AGPL-3.0-only
//
// The menu-cursor seam: a cursor on the main menu's commands. Up and Down
// move it, and Return takes the command it is on (#434).
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
// **One point**, the same one `bar-keys` reads at, in the menu-bar routine
// at the call into the key-read routine (overlay 25, `0x0572`, reached only
// when a key is waiting). The call is the main menu's when the routine's
// far return address is overlay 16 (the manager's word at `0x0790`) at
// `0x02FD`. Every other caller of the routine is left alone.
//
//   * **Up and Down** (scan `0x48` and `0x50`, character zero) move a
//     cursor over the **enabled** commands, in the order the menu draws
//     them, wrapping at both ends. Disabled commands are not drawn and are
//     skipped. The cursor is drawn by the program's own string routine: the
//     whole word of the command it moved to, in white, and the word it left
//     in the two colours the menu draws it in. The key is then replaced by
//     a character the routine ignores, which keeps it waiting (the batch
//     that draws offers the point again, and the key must not be moved on
//     twice).
//   * **Return**, with the cursor drawn, becomes the command's letter, in
//     the BIOS ring before the program reads it. The program takes the
//     command by the route a typed letter takes. With no cursor drawn
//     Return is left alone, and the program drops it as it always did.
//   * **Every other key is the program's**: a typed letter, Home, End.
//
// **Hidden until used.** The seam draws nothing and writes nothing until
// the first Up or Down. The cursor then starts where the menu would put
// it, on the **first** command, and the press moves it: Down, Down takes
// the third command shown.
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
// On, and no Up or Down pressed at the main menu, the run is byte for byte
// the run with the seam off: the handler reads the ring's head word and
// writes nothing unless it is Up or Down at that caller, or Return at that
// caller **with the cursor drawn**. The pair is an `identical` and a
// `contrast` (tests/sessions/README.md).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
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
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_key_read.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> menu_cursor_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The module the menu-bar routine lives in ------------------------------

/// Overlay 25, the module `bar-keys` and `list-arrows` are in (docs/seams.md
/// §10 has the facts and the checks).
constexpr std::uint32_t menu_load_segment_at = 0x3C60;

constexpr seam_module menu_module{
    .file = "GAME.OVR",
    .file_offset = 182479,
    .length = 4682,
    .digest =
        "175454bc2f527dd6757c89eaa50a6cdd27a9cf5d3aaa197b33a140f5b09a3901",
    .load_segment_at = menu_load_segment_at};

/// In the menu-bar routine: the call into the key-read routine. The
/// handler runs before the call, so nothing has been read yet.
constexpr std::uint32_t key_read_call = 0x0572;

// --- The routine's frame ---------------------------------------------------

constexpr std::uint16_t frame_return_ip = 2;
constexpr std::uint16_t frame_return_cs = 4;

/// Below BP: the routine's copy of the bar, a Pascal string whose
/// characters are numbered from one, and its length byte's own bound.
constexpr std::uint16_t local_bar = 0x53;
constexpr std::uint8_t max_bar_length = 40;

// --- The main menu's call --------------------------------------------------

/// The word the program's overlay manager keeps overlay 16's load segment
/// in (the module the party-setup loop is in), and the offset of the
/// instruction after the loop's call into the menu-bar routine.
constexpr std::uint32_t loop_load_segment_at = 0x790;
constexpr std::uint16_t loop_return_offset = 0x02FD;

// --- The data segment ------------------------------------------------------

/// The command records: eleven, forty-two bytes each. A Pascal string
/// (length, then the characters) and, at `+0x29`, the enable byte.
constexpr std::uint16_t data_records = 0x0619;
constexpr std::uint16_t record_stride = 0x2A;
constexpr std::uint8_t record_count = 11;
constexpr std::uint16_t record_enable = 0x29;
constexpr std::uint8_t max_record_text = 0x28;

/// The records of the two commands whose enable byte the menu's redraw
/// rewrites on every run and of which exactly one is on: the second (Drop,
/// on with a party member) and the eighth (Load, on without).
constexpr std::uint8_t record_with_member = 1;
constexpr std::uint8_t record_without_member = 7;

/// The program's one-byte pushback slot for an extended key's second half.
constexpr std::uint16_t data_key_pushback = 0x8501;

/// What the cursor's byte holds with the cursor on row `r`: `cursor_base +
/// r`. The menu's redraw leaves it at one, which is below any row's.
constexpr std::uint8_t cursor_base = 2;

// --- The screen ------------------------------------------------------------

/// The first row a command is drawn on, and the two columns: the first
/// letter at the first, the rest of the word at the second.
constexpr std::uint16_t first_row = 0x0C;
constexpr std::uint16_t letter_column = 2;
constexpr std::uint16_t rest_column = 3;

/// The menu's colours: the first letter and the cursor (white), and the
/// rest of the word (green).
constexpr std::uint16_t colour_letter = 0x0F;
constexpr std::uint16_t colour_rest = 0x0A;

/// The program's string drawer, as an offset from the image base: draws a
/// Pascal string at a cell. `retf 0Ah`, arguments pushed column, row,
/// colour, then the string's segment and offset (docs/seams.md §3).
constexpr std::uint16_t image_draw_string = 0x076B6;

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
[[nodiscard]] bool called_from_the_main_menu(cpu::processor& cpu,
                                             const seam_context& ctx) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t ip =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_ip));
  const std::uint16_t cs =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_cs));
  if (ip != loop_return_offset) {
    return false;
  }
  const std::uint16_t segment = loaded_at(cpu, ctx, loop_load_segment_at);
  return segment != 0 && segment == cs;
}

// --- The menu, read --------------------------------------------------------

/// What the menu shows: the records whose enable byte is not zero, in the
/// order the menu draws them, and where the cursor is among them.
struct menu_reading {
  std::array<std::uint8_t, record_count> record{};
  std::uint8_t shown{0};

  /// The enable byte the cursor lives in, as an offset in the data segment,
  /// and what it holds.
  std::uint16_t cursor_byte{0};
  std::uint8_t cursor_value{0};
};

[[nodiscard]] constexpr std::uint16_t record_at(std::uint8_t record) noexcept {
  return static_cast<std::uint16_t>(data_records + (record * record_stride));
}

[[nodiscard]] constexpr std::uint16_t enable_at(std::uint8_t record) noexcept {
  return static_cast<std::uint16_t>(record_at(record) + record_enable);
}

/// Read the menu. False when it is not a menu these facts describe: neither
/// or both of the two enable bytes the cursor could live in are on.
[[nodiscard]] bool read_menu(cpu::processor& cpu, std::uint16_t ds,
                             menu_reading& out) {
  out = {};
  for (std::uint8_t r = 0; r < record_count; ++r) {
    if (cpu.read_byte(ds, enable_at(r)) != 0) {
      out.record[out.shown++] = r;
    }
  }
  const std::uint16_t with_member = enable_at(record_with_member);
  const std::uint16_t without_member = enable_at(record_without_member);
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
[[nodiscard]] std::uint8_t text_length(cpu::processor& cpu, std::uint16_t ds,
                                       std::uint8_t record) {
  const std::uint8_t length = cpu.read_byte(ds, record_at(record));
  return length > max_record_text ? max_record_text : length;
}

// --- Return ----------------------------------------------------------------

/// The keystroke Return should become: the letter of the command the
/// cursor is on, posted under Return's own scan code. Zero if there is
/// nothing to answer. Declines when the letter is not one the routine's bar
/// holds, which no menu these facts describe produces.
[[nodiscard]] std::uint16_t letter_for_return(cpu::processor& cpu,
                                              seam_context& ctx,
                                              const menu_reading& menu,
                                              std::uint8_t row) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ds = regs[cpu::sreg::ds];
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];

  const std::uint8_t record = menu.record[row];
  if (text_length(cpu, ds, record) == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  const std::uint8_t letter = upper(
      cpu.read_byte(ds, static_cast<std::uint16_t>(record_at(record) + 1U)));
  if (!is_command_letter(letter)) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }

  // The routine takes a letter that is in the bar it was handed, and reads
  // the bar from a copy in its own frame: look where it looks.
  const std::uint8_t length =
      cpu.read_byte(ss, static_cast<std::uint16_t>(bp - local_bar));
  if (length == 0 || length > max_bar_length) {
    ctx.decline(seam_reason::point_not_recognized);
    return 0;
  }
  for (std::uint8_t position = 1; position <= length; ++position) {
    const std::uint8_t c = cpu.read_byte(
        ss, static_cast<std::uint16_t>((bp - local_bar) + position));
    if (c == letter) {
      return static_cast<std::uint16_t>(scan_enter | letter);
    }
  }
  ctx.decline(seam_reason::point_not_recognized);
  return 0;
}

// --- Up and Down -----------------------------------------------------------

/// One call to the program's own string drawer.
[[nodiscard]] bool draw(seam_context& ctx, std::uint16_t image,
                        std::uint16_t row, std::uint16_t column,
                        std::uint16_t colour, std::uint16_t segment,
                        std::uint16_t offset) {
  const std::array<std::uint16_t, 5> where{column, row, colour, segment,
                                           offset};
  return ctx.call_program(image, image_draw_string, where);
}

/// Put the word `row` stands for back the way the menu draws it: the first
/// letter in the letter's colour, the rest in the other.
[[nodiscard]] bool draw_plain(seam_context& ctx, cpu::processor& cpu,
                              std::uint16_t image, std::uint16_t ds,
                              const menu_reading& menu, std::uint8_t row) {
  const std::uint8_t record = menu.record[row];
  const std::uint8_t length = text_length(cpu, ds, record);
  if (length == 0) {
    return true;
  }
  const auto screen_row = static_cast<std::uint16_t>(first_row + row);

  std::array<std::uint8_t, 2> head{
      1, cpu.read_byte(ds, static_cast<std::uint16_t>(record_at(record) + 1U))};
  std::array<std::uint8_t, max_record_text> tail{};
  tail[0] = static_cast<std::uint8_t>(length - 1U);
  for (std::uint8_t i = 1; i < length; ++i) {
    tail[i] = cpu.read_byte(
        ds, static_cast<std::uint16_t>(record_at(record) + 1U + i));
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
  if (!draw(ctx, image, screen_row, letter_column, colour_letter, head_segment,
            head_offset)) {
    return false;
  }
  return length == 1 || draw(ctx, image, screen_row, rest_column, colour_rest,
                             tail_segment, tail_offset);
}

/// Light the word `row` stands for: the record itself, whole, in white, drawn
/// where it stands in the program's memory.
[[nodiscard]] bool draw_lit(seam_context& ctx, std::uint16_t image,
                            std::uint16_t ds, const menu_reading& menu,
                            std::uint8_t row) {
  return draw(ctx, image, static_cast<std::uint16_t>(first_row + row),
              letter_column, colour_letter, ds, record_at(menu.record[row]));
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
  if (cpu.read_byte(ds, data_key_pushback) != 0) {
    // The head of the ring is not the key about to be read.
    return;
  }
  if (!called_from_the_main_menu(cpu, ctx)) {
    return;
  }
  if (skips_placeholder) {
    head = next_slot(head);
    cpu.write_word(bda::segment, bda::keyboard_buffer_head, head);
    key = cpu.read_word(bda::segment, head);
  }

  menu_reading menu;
  if (!read_menu(cpu, ds, menu) || menu.shown == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint8_t lit = cursor_row(menu);

  if (key == key_enter) {
    if (lit == menu.shown) {
      // No cursor is drawn: the program drops a Return here.
      return;
    }
    const std::uint16_t answer = letter_for_return(cpu, ctx, menu, lit);
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
  if (!draw_lit(ctx, image, ds, menu, to) ||
      (was_drawn && lit != to && !draw_plain(ctx, cpu, image, ds, menu, lit))) {
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

constexpr std::array<seam_point, 1> menu_cursor_points{
    {{.module = menu_module, .offset = key_read_call, .run = &at_key_read}}};

constexpr seam_definition menu_cursor_definition{
    .id = "menu-cursor",
    .about =
        "Up and Down move a cursor over the main menu's commands, and "
        "Return takes the one it is on",
    .fingerprints = menu_cursor_binaries,
    .points = menu_cursor_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& menu_cursor_seam() noexcept {
  return menu_cursor_definition;
}

}  // namespace amberfolio::machine
