// SPDX-License-Identifier: AGPL-3.0-only
//
// The hero-keys seam: the number row's 1 to 8 select a party member, and
// the party list shows each member's number (#439).
//
//
// What the program does, stated as facts
// --------------------------------------
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here.
//
// **The party is a linked list of records, and one far pointer names the
// selected member.** The head is a far pointer in the data segment
// (`data_party_head`), each record holds the next member's far pointer at
// `record_next`, and the selected member is another far pointer
// (`data_current`). The program changes the selection through one resident
// routine, the party cursor: **on `G` it steps back** (from the head it
// wraps to the tail, from anyone else it goes to the member whose next is
// the current one), **on `O` it steps forward** (the tail stays where it
// is), and **on any other key it goes to the head**. The callers then
// redraw the party list from the selected member.
//
// **A caller reaches the cursor with a raw key.** The menu-bar routine
// (overlay 25, `0x03BD`) has a raw mode, and in it the extended keys come
// back as their scan codes, `0x47` (Home) as `G` and `0x4F` (End) as `O`,
// and the characters `1` to `9` come back translated through a table in
// the data segment into the movement letters the keypad's layout implies
// (`7` as `G`, `1` as `O`, `8` as `H`, and so on). The callers named
// below hand `G` and `O` to the cursor and redraw. The program cannot
// tell the number row from the keypad, because it reads the character;
// the BIOS ring carries the scan code as well, which is the whole of how
// this seam can: the number row is scan `0x02` to `0x09` for `1` to `8`,
// the keypad's digits with NumLock on are scan `0x47` to `0x51`.
//
// **The party list is drawn by one resident routine** (the roster
// drawer), whose frame is a fact the seam leans on: the column the names
// start at (a byte, one for the main menu and `0x11` beside the viewport),
// the row it is on (a byte, four for the first member and one more each),
// and the far pointer to the record being drawn. For each member it clears
// the row from that column to `0x26`, draws the name there, and then draws
// the armour class and the hit points in columns of their own (from `0x20`
// and `0x24`, right-aligned). A name is at most fifteen characters, so in
// the column beside the viewport it fills `0x11` to `0x1F`.
//
//
// What the seam does
// ------------------
//
// **Keys: one point, at the call into the key-read routine inside the
// menu-bar routine** (overlay 25 `0x0572`, the point `bar-keys` is at
// too; the two do not meet, since each looks at its own keys). A
// number-row digit `1` to `8` at the head of the BIOS ring, from a caller
// in the table below, selects that member by **driving the program's own
// cursor**: the handler puts the selection on the target's successor (or
// on the head, for the last member and for a party of one) and rewrites
// the keystroke to Home. The caller reads Home as `G`, hands it to the
// cursor, and the cursor steps back onto the target, and the caller
// redraws the party list. Nothing is drawn here, and the selection is
// written only to a place the program would have put it itself.
//
// A digit with no member behind it is rewritten to the key the program
// throws away (`seam_key_read.h`): the routine goes back to waiting where
// it was. `9` and `0`, the keypad's digits, and every caller not in the
// table are left alone. The caller is identified by the routine's far
// return address, overlay-qualified, as `bar-keys` identifies its own.
//
// **Display: two points in the roster drawer**, both `inside_calls`, since
// the automap and the journal give the roster back through a batch of the
// program's own calls. They share one state, the column byte of the
// drawer's frame, which the program reads for nothing between the two:
//
//   * after the row has been cleared and before the name is drawn, the
//     column goes up by two, and the member's number is drawn at the old
//     column in white by the program's own glyph routine, called by hand
//     with its far return aimed at this very point (the point is reached
//     again, finds the column already moved, and lets the program go on).
//     The name is then drawn two columns to the right by the program's
//     own routines, in the status colour it would have had;
//   * after the name and before the armour class, a name that now ends
//     past column `0x1F` (the last column the name had before) has its
//     tail cleared, columns `0x20` up to its end, by the program's own
//     clear routine, called the same way; then the column goes back.
//     **The column is left as the program had it** at every other arrival.
//
// **What a long name keeps.** Beside the viewport the name field is
// columns `0x13` to `0x1F` now, thirteen, so a name of fourteen or fifteen
// characters shows its first thirteen. On the main menu, where the names
// start at column one, there is room for every name in full.
//
// Everything drawn is drawn by the program's own routines, so the faces
// (`font-sans`, `font-chisel`) letter the numbers as they letter the names.
//
// **Checks the frames are the frames.** The key handler refuses a party
// list it cannot read (a pointer outside conventional memory, a ninth
// member, no data segment where the facts put it). The roster handlers
// refuse a column or row the facts do not allow. A handler that refuses
// declines and touches nothing.
//
//
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------
//
// On, the roster is changed from the first time it is drawn with a member
// in it: the seam is seen as soon as there is a party list to see, so its
// pair is a `contrast`, not an `identical`. With no digit pressed the
// machine differs from the seam-off run only by what the roster drawer
// does (the display, and the drawer's own frame while it runs).
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
// The party-order screen is not a caller: with a member picked up, Home
// and End move the member instead of the selection, and the seam cannot
// tell which state it is in. Left out, too, are the combat screens and the
// pick-lists, whose digits are real.

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"
#include "seam_key_read.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> hero_keys_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

// --- The module the menu-bar routine lives in ------------------------------

/// The same module, word and point `bar-keys` and `list-arrows` use; the
/// facts and their checks are documented there (docs/seams.md §10).
constexpr std::uint32_t menu_load_segment_at = 0x3C60;

constexpr seam_module menu_module{
    .file = "GAME.OVR",
    .file_offset = 182479,
    .length = 4682,
    .digest =
        "175454bc2f527dd6757c89eaa50a6cdd27a9cf5d3aaa197b33a140f5b09a3901",
    .load_segment_at = menu_load_segment_at};

/// In the menu-bar routine: the call into the key-read routine, reached
/// only when a key is waiting. Offset from the module's start.
constexpr std::uint32_t key_read_call = 0x0572;

/// Above BP in the routine's frame: the caller's return address.
constexpr std::uint16_t frame_return_ip = 2;
constexpr std::uint16_t frame_return_cs = 4;

// --- The data segment ------------------------------------------------------

/// The data segment is the image's paragraph `0xC7C` on: image offset
/// `0xC7C0`, which is DS `0x0CDC` where the image is at `0x60`.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;

/// The selected member: a far pointer, offset then segment.
constexpr std::uint16_t data_current = 0x5D92;
/// The party's head: a far pointer, offset then segment.
constexpr std::uint16_t data_party_head = 0x5D96;
/// A record's next-member far pointer, offset then segment.
constexpr std::uint16_t record_next = 0x0104;
/// How far into a record this seam reads: the next pointer's last byte.
constexpr std::uint16_t record_reach = 0x0108;

/// The program's one-byte pushback slot for an extended key's second half.
constexpr std::uint16_t data_key_pushback = 0x8501;

/// A party is six characters and two non-player ones.
constexpr unsigned max_party = 8;

// --- The callers that hand Home and End to the party cursor ----------------

/// A caller of the menu-bar routine, named by the word the program keeps its
/// module's load segment in and the offset of the instruction after its
/// call.
struct hero_caller {
  std::uint32_t load_segment_at;
  std::uint16_t return_offset;
};

/// The words the program's overlay manager keeps the modules' load
/// segments in: overlays 4, 5, 6, 7, 14, 15 and 16, found by searching the
/// resident image for the manager's record of each (its file offset and
/// length from the overlay table), one match each, and the word sixteen
/// bytes into it. The search returns the known words for overlays 5, 14,
/// 15 and 16 (`bar-keys`' table).
constexpr std::uint32_t temple_load_segment_at = 0x230;       // overlay 4
constexpr std::uint32_t post_combat_load_segment_at = 0x260;  // overlay 5
constexpr std::uint32_t shop_load_segment_at = 0x290;         // overlay 6
constexpr std::uint32_t script_load_segment_at = 0x2C0;       // overlay 7
constexpr std::uint32_t adventure_load_segment_at = 0x730;    // overlay 14
constexpr std::uint32_t camp_load_segment_at = 0x760;         // overlay 15
constexpr std::uint32_t main_menu_load_segment_at = 0x790;    // overlay 16

constexpr std::array<hero_caller, 11> hero_callers{{
    // The adventuring bar, overhead view and 3D view and wilderness.
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x09D5},
    {.load_segment_at = adventure_load_segment_at, .return_offset = 0x0C45},
    // The main menu, the title screen's and every other.
    {.load_segment_at = main_menu_load_segment_at, .return_offset = 0x02FD},
    // The camp bar, and its Magic and Alter bars.
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1F24},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1447},
    {.load_segment_at = camp_load_segment_at, .return_offset = 0x1CA4},
    // The script prompts' one menu routine.
    {.load_segment_at = script_load_segment_at, .return_offset = 0x16EB},
    // The post-combat loot bar and its Take bar.
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x1024},
    {.load_segment_at = post_combat_load_segment_at, .return_offset = 0x0D91},
    // The shop's bar and the temple's.
    {.load_segment_at = shop_load_segment_at, .return_offset = 0x061F},
    {.load_segment_at = temple_load_segment_at, .return_offset = 0x0DAA},
}};

// --- The keys, as the BIOS ring holds them: scan code high, character low --

/// Home: scan `0x47`, no character. The caller reads it as `G`.
constexpr std::uint16_t key_home = 0x4700;

/// The scan code of the number row's `1`; the digits count up from it.
constexpr std::uint8_t number_row_scan_of_one = 0x02;

/// The member the number-row key `key` names, one to eight, or zero when
/// it is not one: the character and the scan code both have to be the
/// number row's, so the keypad's digits with NumLock on (scan `0x47` to
/// `0x51`) are not.
[[nodiscard]] constexpr unsigned hero_of(std::uint16_t key) noexcept {
  const auto character = static_cast<std::uint8_t>(key & 0xFFU);
  const auto scan = static_cast<std::uint8_t>(key >> 8U);
  if (character < '1' || character > '1' + max_party - 1U) {
    return 0;
  }
  const unsigned digit = character - '1' + 1U;
  return scan == number_row_scan_of_one + digit - 1U ? digit : 0U;
}

// --- Reading the party -----------------------------------------------------

struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;
};

[[nodiscard]] bool is_record(far_pointer at) noexcept {
  if ((at.offset | at.segment) == 0) {
    return false;
  }
  // A far pointer that has not been set up yet points anywhere, and a read
  // above conventional memory is a read of the video window, where it
  // loads the adapter's latches (docs/seams.md §8.4).
  return at.offset <= 0x10000U - record_reach &&
         cpu::physical_address(at.segment, at.offset) + record_reach <=
             conventional_ram_size;
}

[[nodiscard]] far_pointer read_pointer(cpu::processor& cpu,
                                       std::uint16_t segment,
                                       std::uint16_t offset) {
  return {.offset = cpu.read_word(segment, offset),
          .segment =
              cpu.read_word(segment, static_cast<std::uint16_t>(offset + 2U))};
}

void write_pointer(cpu::processor& cpu, std::uint16_t segment,
                   std::uint16_t offset, far_pointer to) {
  cpu.write_word(segment, offset, to.offset);
  cpu.write_word(segment, static_cast<std::uint16_t>(offset + 2U), to.segment);
}

/// The party's members, in the order the program's list holds them.
struct party {
  std::array<far_pointer, max_party> member{};
  unsigned size{0};
  /// The list went on past eight members, or through a pointer that is not
  /// a record: not the structure these facts describe.
  bool readable{true};
};

[[nodiscard]] party read_party(cpu::processor& cpu, std::uint16_t ds) {
  party out;
  far_pointer at = read_pointer(cpu, ds, data_party_head);
  while ((at.offset | at.segment) != 0) {
    if (out.size == max_party || !is_record(at)) {
      out.readable = false;
      return out;
    }
    out.member[out.size++] = at;
    at = read_pointer(cpu, at.segment,
                      static_cast<std::uint16_t>(at.offset + record_next));
  }
  return out;
}

// --- The callers -----------------------------------------------------------

/// The segment the program says `at` is loaded at now; zero while it is
/// not loaded.
[[nodiscard]] std::uint16_t loaded_at(cpu::processor& cpu,
                                      const seam_context& ctx,
                                      std::uint32_t at) {
  return cpu.read_word(static_cast<std::uint16_t>(ctx.image_base() >> 4U),
                       static_cast<std::uint16_t>(at));
}

/// Whether this call of the menu-bar routine came from a caller in the
/// table.
[[nodiscard]] bool called_from_a_hero_caller(cpu::processor& cpu,
                                             const seam_context& ctx) {
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const std::uint16_t ip =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_ip));
  const std::uint16_t cs =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp + frame_return_cs));

  return std::ranges::any_of(hero_callers, [&](const hero_caller& caller) {
    if (ip != caller.return_offset) {
      return false;
    }
    const std::uint16_t segment = loaded_at(cpu, ctx, caller.load_segment_at);
    return segment != 0 && segment == cs;
  });
}

// --- Keys ------------------------------------------------------------------

/// The program is about to read the key at the head of the ring.
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();

  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  if (head == cpu.read_word(bda::segment, bda::keyboard_buffer_tail)) {
    // Empty: the poll was answered from the pushback slot.
    return;
  }
  const std::uint16_t key = cpu.read_word(bda::segment, head);
  const unsigned hero = hero_of(key);
  if (hero == 0) {
    return;
  }
  if (box.journal().reader_open()) {
    // The reader takes every key at the poll; one that got past it is the
    // reader's, not the bar's.
    return;
  }
  const automap_state& map = box.automap();
  if (map.at_command_bar() && map.panel_open() && !map.panel_covered()) {
    // The party's own bar is up with the map on the roster's cells, and
    // the map takes the keys that step the party cursor, at the poll and,
    // for one that got past it, at the read this very key is about to
    // reach: a Home from here would be taken there, with the selection
    // already half moved. A step whose whole visible effect is behind the
    // map is the map's to decline.
    return;
  }
  const std::uint16_t ds = regs[cpu::sreg::ds];
  if (cpu.read_byte(ds, data_key_pushback) != 0) {
    // The head of the ring is not the key about to be read.
    return;
  }
  if (!called_from_a_hero_caller(cpu, ctx)) {
    return;
  }
  if (ds != static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                       dgroup_paragraphs)) {
    // Not the data segment the facts put the party in.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const party members = read_party(cpu, ds);
  if (!members.readable) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const std::uint16_t ring_slot = head;
  if (hero > members.size) {
    // Nobody is there: the routine goes back to waiting.
    cpu.write_word(bda::segment, ring_slot,
                   static_cast<std::uint16_t>((key_ignored_scan << 8U) |
                                              key_ignored_ascii));
    return;
  }

  // The cursor's `G` goes to the member whose next is the current one, and
  // from the head it wraps to the tail. So: the target's successor, or the
  // head when the target is the last (or the only) member. The cursor
  // then lands on the target by its own rule and the caller redraws.
  const far_pointer from =
      hero == members.size ? members.member[0] : members.member[hero];
  write_pointer(cpu, ds, data_current, from);
  cpu.write_word(bda::segment, ring_slot, key_home);
}

// --- The roster drawer ------------------------------------------------------

/// The roster drawer's frame: below BP, the column its names start at (a
/// byte), the row it is on (a byte) and the far pointer to the record being
/// drawn (offset then segment).
constexpr std::uint16_t local_column = 5;
constexpr std::uint16_t local_row = 6;
constexpr std::uint16_t local_member = 4;

/// The columns the names start at: beside the viewport, and on the main
/// menu.
constexpr std::uint8_t column_beside_viewport = 0x11;
constexpr std::uint8_t column_main_menu = 0x01;

/// The first member's row, and how many rows the drawer can be on.
constexpr std::uint8_t first_row = 4;

/// How far the name moves right: the number and one blank column.
constexpr std::uint8_t name_shift = 2;

/// The last column the name had room in before the number came.
constexpr unsigned last_name_column = 0x1F;
/// The leftmost column of the armour class, and the rightmost of the row.
constexpr std::uint8_t first_cut_column = 0x20;
constexpr std::uint8_t last_row_column = 0x26;

/// The routines the seam asks the program to run, as the paragraph they
/// were linked at (a routine reaches its own literals through CS, so it
/// works only there: docs/seams.md §8.4) and the offset in it. Both are
/// far, and clean their own arguments.
///
/// The glyph blitter takes six words, pushed first to last: column, row,
/// colour, a count of one, the character, and a one that folds the
/// character to upper case. The clear takes four, left, top, right and
/// bottom, the order the roster drawer pushes them in.
constexpr std::uint16_t glyph_paragraph = 0x709;
constexpr std::uint16_t glyph_offset = 0x01DF;
constexpr std::uint16_t clear_paragraph = 0x3F1;
constexpr std::uint16_t clear_offset = 0x0137;

/// White, in the program's palette: the colour the program draws the
/// selected member's name in.
constexpr std::uint16_t colour_white = 0x000F;

/// In the roster drawer, the instruction after the call that clears a
/// member's row: the name is drawn next, down one of two paths.
constexpr std::uint32_t row_cleared = 0x138F;
/// In the roster drawer, where the two paths meet, before the armour class
/// is drawn.
constexpr std::uint32_t name_drawn = 0x13CB;

/// What the drawer's frame says at one of the two points.
struct roster_frame {
  std::uint16_t ss{};
  std::uint16_t bp{};
  std::uint8_t column{};
  std::uint8_t row{};
  far_pointer member{};
};

[[nodiscard]] std::uint16_t below(const roster_frame& frame,
                                  std::uint16_t distance) noexcept {
  return static_cast<std::uint16_t>(frame.bp - distance);
}

/// The drawer's frame, or false if it is not one these facts describe.
[[nodiscard]] bool read_roster_frame(cpu::processor& cpu, roster_frame& frame) {
  cpu::registers& regs = cpu.regs();
  frame.ss = regs[cpu::sreg::ss];
  frame.bp = regs[cpu::reg16::bp];
  frame.column = cpu.read_byte(frame.ss, below(frame, local_column));
  frame.row = cpu.read_byte(frame.ss, below(frame, local_row));
  frame.member = read_pointer(cpu, frame.ss, below(frame, local_member));
  return frame.row >= first_row && frame.row < first_row + max_party &&
         is_record(frame.member);
}

/// Whether `column` is one the drawer starts its names at.
[[nodiscard]] constexpr bool is_original(std::uint8_t column) noexcept {
  return column == column_beside_viewport || column == column_main_menu;
}

void push_word(cpu::processor& cpu, std::uint16_t value) {
  cpu::registers& regs = cpu.regs();
  regs[cpu::reg16::sp] = static_cast<std::uint16_t>(regs[cpu::reg16::sp] - 2U);
  cpu.write_word(regs[cpu::sreg::ss], regs[cpu::reg16::sp], value);
}

/// Run the program's own routine at `paragraph:offset` with `words` as its
/// arguments, first the deepest, **and come back to the instruction the
/// machine is on**: the far return pushed is this point itself, which is
/// reached again when the routine has gone, with its arguments cleaned up
/// by the routine and everything it clobbered dead (both points are at a
/// call's arguments, and the program loads what it needs afterwards).
///
/// This is what the engine's own batch does for a handler that is not
/// inside another batch, and it is done by hand here because the roster is
/// drawn inside the automap's and the journal's batches, which a handler
/// may not start one in (seam.h, `seam_point::inside_calls`).
void call_and_return_here(machine& box, seam_context& ctx,
                          std::span<const std::uint16_t> words,
                          std::uint16_t paragraph, std::uint16_t offset) {
  cpu::processor& cpu = box.processor();
  cpu::registers& regs = cpu.regs();
  const std::uint16_t return_cs = regs[cpu::sreg::cs];
  const std::uint16_t return_ip = regs.ip;
  for (const std::uint16_t word : words) {
    push_word(cpu, word);
  }
  push_word(cpu, return_cs);
  push_word(cpu, return_ip);
  ctx.redirect(static_cast<std::uint16_t>((ctx.image_base() >> 4U) + paragraph),
               offset);
}

/// The roster drawer has cleared a member's row, and the name is next.
void at_row_cleared(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  roster_frame frame;
  if (!read_roster_frame(cpu, frame)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!is_original(frame.column)) {
    if (is_original(static_cast<std::uint8_t>(frame.column - name_shift))) {
      // Reached again: the number has been drawn, and the program goes on
      // with the name two columns over.
      return;
    }
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // The name moves right, which the program does for both of its paths
  // because both read this byte; it is put back at the next point.
  cpu.write_byte(frame.ss, below(frame, local_column),
                 static_cast<std::uint8_t>(frame.column + name_shift));

  const unsigned hero = frame.row - first_row + 1U;
  const std::array<std::uint16_t, 6> glyph{
      frame.column,
      frame.row,
      colour_white,
      1,
      static_cast<std::uint16_t>('0' + hero),
      1};
  call_and_return_here(box, ctx, glyph, glyph_paragraph, glyph_offset);
}

/// The name is drawn, and the armour class is next.
void at_name_drawn(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  roster_frame frame;
  if (!read_roster_frame(cpu, frame)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const auto put_column = [&](unsigned column) {
    cpu.write_byte(frame.ss, below(frame, local_column),
                   static_cast<std::uint8_t>(column));
  };

  if (is_original(frame.column)) {
    // Nothing moved the name: nothing to put back.
    return;
  }
  if (is_original(static_cast<std::uint8_t>(frame.column + 1U - name_shift))) {
    // Reached again after the cut: put the column back as it was.
    put_column(frame.column - 1U);
    return;
  }
  if (!is_original(static_cast<std::uint8_t>(frame.column - name_shift))) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // The name has been drawn from `frame.column`. If it ends past the column
  // it ended at before, its tail is cleared; the cut is the program's own
  // clear, and this point is reached again after it.
  const unsigned length =
      cpu.read_byte(frame.member.segment, frame.member.offset);
  const unsigned end = frame.column + length - 1U;
  if (length != 0 && end > last_name_column) {
    put_column(frame.column - 1U);
    const std::array<std::uint16_t, 4> clear{
        first_cut_column, frame.row,
        static_cast<std::uint16_t>(std::min<unsigned>(end, last_row_column)),
        frame.row};
    call_and_return_here(box, ctx, clear, clear_paragraph, clear_offset);
    return;
  }
  put_column(frame.column - name_shift);
}

// --- The definition --------------------------------------------------------

constexpr std::array<seam_point, 3> hero_keys_points{
    {{.module = menu_module, .offset = key_read_call, .run = &at_key_read},
     {.module = resident_image,
      .offset = row_cleared,
      .run = &at_row_cleared,
      .inside_calls = true},
     {.module = resident_image,
      .offset = name_drawn,
      .run = &at_name_drawn,
      .inside_calls = true}}};

constexpr seam_definition hero_keys_definition{
    .id = "hero-keys",
    .about =
        "the number row's 1 to 8 select a party member, and the party list "
        "shows each member's number",
    .fingerprints = hero_keys_binaries,
    .points = hero_keys_points,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& hero_keys_seam() noexcept {
  return hero_keys_definition;
}

}  // namespace amberfolio::machine
