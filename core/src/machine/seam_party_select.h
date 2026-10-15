// SPDX-License-Identifier: AGPL-3.0-only
//
// Selecting a party member where the program will not do it for us: the
// party list, the cursor's rule, and the one step both `hero-keys` (the
// number row) and `list-arrows` (Up and Down) take at a bar that is **not
// raw** (#469).
//
// **The two ways to select.** At a raw-mode caller of the menu-bar routine
// the seams hand the caller a Home or an End, which the caller gives to the
// party cursor and then redraws the list (seam_hero_keys.cpp, seam_list_
// arrows.cpp). At a caller with raw mode **off** the routine throws Home,
// End and the arrows away itself, so there is no caller to hand them to.
// What is left is what the caller would have done: put the target in the
// selected-member pointer and ask the program's own roster drawer to draw
// the list again. This file is that second way, and the party list both
// seams read the target out of.
//
// **The step.** `select_and_redraw()` queues one call of the roster drawer
// (resident, paragraph `0xBA`, offset `0x0767`, one far pointer: the
// selected member, segment first) and writes DGROUP `0x5D92`/`0x5D94`. The
// pointer is a place the program would have put the selection itself; the
// drawing is the program's own, so `hero-keys`' numbers and the faces reach
// it as they reach every other redraw. **The key is answered first**, with
// the key the program throws away (`seam_key_read.h`): the engine offers
// the read point again when the batch ends, and a handler that left the
// digit at the head of the ring would select it a second time.
//
// All of it addresses, offsets and key codes. Not one byte of the program
// is reproduced here.

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/seam.h"
#include "seam_key_read.h"
#include "seam_menu_bar.h"

namespace amberfolio::machine::party_select {

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
/// How far into a record this reads: the next pointer's last byte.
constexpr std::uint16_t record_reach = 0x0108;

/// A party is six characters and two non-player ones.
constexpr unsigned max_party = 8;

// --- Reading the party -----------------------------------------------------

struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;

  [[nodiscard]] constexpr bool operator==(const far_pointer&) const = default;
};

[[nodiscard]] inline bool is_record(far_pointer at) noexcept {
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

[[nodiscard]] inline far_pointer read_pointer(cpu::processor& cpu,
                                              std::uint16_t segment,
                                              std::uint16_t offset) {
  return {.offset = cpu.read_word(segment, offset),
          .segment =
              cpu.read_word(segment, static_cast<std::uint16_t>(offset + 2U))};
}

inline void write_pointer(cpu::processor& cpu, std::uint16_t segment,
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

[[nodiscard]] inline party read_party(cpu::processor& cpu, std::uint16_t ds) {
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

/// Whether DS is the data segment the facts put the party in.
[[nodiscard]] inline bool is_the_data_segment(const seam_context& ctx,
                                              std::uint16_t ds) noexcept {
  return ds == static_cast<std::uint16_t>((ctx.image_base() >> 4U) +
                                          dgroup_paragraphs);
}

// --- The cursor's rule -----------------------------------------------------

/// Which way the cursor steps: back is `G` (Home), forward is `O` (End).
enum class step : std::uint8_t { back, forward };

[[nodiscard]] inline unsigned member_index(const party& members,
                                           far_pointer member) noexcept {
  for (unsigned i = 0; i < members.size; ++i) {
    if (members.member[i] == member) {
      return i;
    }
  }
  return members.size;
}

/// The index the program's party cursor lands on for a step from `current`:
/// back goes to the member whose next is the current one and, from the
/// head, wraps to the tail; forward goes to the next member and stays on
/// the tail. (Any other key goes to the head and is not a step.)
/// `members.size` when `current` is not in the list.
[[nodiscard]] constexpr unsigned stepped_index(const party& members,
                                               unsigned current,
                                               step which) noexcept {
  if (current >= members.size) {
    return members.size;
  }
  if (which == step::back) {
    return current == 0 ? members.size - 1U : current - 1U;
  }
  return current + 1U == members.size ? current : current + 1U;
}

// --- The callers that are not raw ------------------------------------------

/// The callers of the menu-bar routine with **raw mode off** whose screen
/// shows the party list while the bar is up (docs/seams.md §10 has each by
/// two routes). The routine throws Home, End and the arrows away for them,
/// and none of their bars has a digit for a command letter.
constexpr std::array<menu_bar::caller, 4> callers{{
    {.load_segment_at = menu_bar::adventure_load_segment_at,
     .return_offset = 0x0EBF},  // a locked door's bar
    {.load_segment_at = menu_bar::adventure_load_segment_at,
     .return_offset = 0x0FFE},  // a stuck door's bar
    {.load_segment_at = menu_bar::camp_load_segment_at,
     .return_offset = 0x1DDF},  // camp's Portraits and Monsters bar
    {.load_segment_at = menu_bar::slots_load_segment_at,
     .return_offset = 0x1DA1},  // the save slot bar, from camp
}};

/// Above BP in the menu-bar routine's frame: the raw-mode argument, pushed
/// as a whole word, tested as a byte.
constexpr std::uint16_t frame_raw_mode = 0x0C;

/// Below BP in the same frame: the bar as a Pascal string, its length at
/// `BP-0x53` and its characters numbered from one.
constexpr std::uint16_t frame_bar_below_bp = 0x53;
/// The most a bar is (the routine copies at most `0x28` characters).
constexpr std::uint8_t bar_longest = 0x28;

/// Whether the menu-bar routine, reading a key now, was called from a
/// caller in the table above with raw mode off, and the bar being asked
/// has no command letter that is `digit` (a character, `'1'` to `'8'`;
/// zero to skip that check). A caller in the table with raw mode on is not
/// the frame these facts describe: false, and the caller says so.
enum class where : std::uint8_t { not_here, here, frame_unknown };

[[nodiscard]] inline where at_a_party_bar(cpu::processor& cpu,
                                          const seam_context& ctx,
                                          std::uint8_t digit) {
  if (!menu_bar::called_from(cpu, ctx, callers)) {
    return where::not_here;
  }
  cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  if (cpu.read_byte(ss, static_cast<std::uint16_t>(bp + frame_raw_mode)) != 0) {
    return where::frame_unknown;
  }
  if (digit != 0) {
    const auto bar = static_cast<std::uint16_t>(bp - frame_bar_below_bp);
    const std::uint8_t length = cpu.read_byte(ss, bar);
    for (std::uint8_t i = 1; i <= length && i <= bar_longest; ++i) {
      if (cpu.read_byte(ss, static_cast<std::uint16_t>(bar + i)) == digit) {
        // The bar's own command: the digit stays its.
        return where::not_here;
      }
    }
  }
  return where::here;
}

// --- The step ---------------------------------------------------------------

/// The roster drawer: resident, reached as the paragraph it was linked at
/// (it reads its literals through CS, docs/seams.md §8.4) and the offset in
/// it. Far, one argument (the selected member's far pointer, segment first)
/// and it cleans its own.
constexpr std::uint16_t roster_draw_paragraph = 0x0BA;
constexpr std::uint16_t roster_draw_offset = 0x0767;

/// Whether the map's panel is on the roster's cells: a redraw of the list
/// would paint over it, and the panel's give-back is the map's to make.
[[nodiscard]] inline bool map_over_the_roster(machine& box) {
  const automap_state& map = box.automap();
  return map.panel_open() && map.panel_on_screen();
}

/// Answer the key at the head of the ring (`at`) with the key the program
/// throws away: the routine goes back to waiting where it was.
inline void answer_with_the_ignored_key(cpu::processor& cpu, std::uint16_t at) {
  cpu.write_word(
      bda::segment, at,
      static_cast<std::uint16_t>((key_ignored_scan << 8U) | key_ignored_ascii));
}

/// Make `target` the selected member and have the program draw the list
/// again, and answer the key at the head of the ring (`key_at`). The
/// answer comes first and whatever happens to the queue, so the key is
/// never left for the program to take as its own; false, and the
/// selection untouched, if the batch could not be queued.
inline bool select_and_redraw(machine& box, seam_context& ctx, std::uint16_t ds,
                              far_pointer target, std::uint16_t key_at) {
  cpu::processor& cpu = box.processor();
  answer_with_the_ignored_key(cpu, key_at);
  const std::array<std::uint16_t, 2> current{target.segment, target.offset};
  const auto image = static_cast<std::uint16_t>(ctx.image_base() >> 4U);
  if (!ctx.call_program(
          static_cast<std::uint16_t>(image + roster_draw_paragraph),
          roster_draw_offset, current)) {
    return false;
  }
  write_pointer(cpu, ds, data_current, target);
  return true;
}

}  // namespace amberfolio::machine::party_select
