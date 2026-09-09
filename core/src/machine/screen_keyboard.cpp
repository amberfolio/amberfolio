// SPDX-License-Identifier: AGPL-3.0-only
//
// The three layouts, and the four rules a host drives them by.
// screen_keyboard.h has the reasoning; `docs/hosts.md` §7 has the format
// as a host sees it across the ABI.

#include "amberfolio/machine/screen_keyboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/keyboard.h"

namespace amberfolio::machine::screen_keyboard {
namespace {

/// A row of ordinary keys, laid left to right from `column`, so that a
/// row reads as the legends on it rather than as a column arithmetic
/// nobody can check by eye. Only the widths that are not one unit are
/// spelled out, below, key by key.
constexpr key ordinary(std::string_view label, std::uint8_t scancode,
                       std::uint8_t row, std::uint8_t column) {
  return {.label = label,
          .scancode = scancode,
          .row = row,
          .column = column,
          .width = unit};
}

// --- prompt ------------------------------------------------------------
//
// The yes/no question and the page of text waiting to be dismissed —
// `docs/playable.md`'s legs answer `Y`, leave by `Escape`, and turn a
// page with `Return`, and those four keys are the whole of it.

constexpr std::array<key, 4> prompt_keys{{
    {.label = "Y", .scancode = 0x15, .row = 0, .column = 0, .width = 8},
    {.label = "N", .scancode = 0x31, .row = 0, .column = 8, .width = 8},
    {.label = "Enter", .scancode = 0x1C, .row = 0, .column = 16, .width = 12},
    {.label = "Esc", .scancode = 0x01, .row = 0, .column = 28, .width = 12},
}};

// --- name --------------------------------------------------------------
//
// Free text. The digit row and the three letter rows in the order they
// are on the board, then a shift, a backspace, a space and a return.
// Nothing else: a name screen is the one place a stray command key is
// worst, because the program is putting whatever arrives into the name.

constexpr std::array<key, 40> name_keys{{
    ordinary("1", 0x02, 0, 0),
    ordinary("2", 0x03, 0, 4),
    ordinary("3", 0x04, 0, 8),
    ordinary("4", 0x05, 0, 12),
    ordinary("5", 0x06, 0, 16),
    ordinary("6", 0x07, 0, 20),
    ordinary("7", 0x08, 0, 24),
    ordinary("8", 0x09, 0, 28),
    ordinary("9", 0x0A, 0, 32),
    ordinary("0", 0x0B, 0, 36),

    ordinary("Q", 0x10, 1, 0),
    ordinary("W", 0x11, 1, 4),
    ordinary("E", 0x12, 1, 8),
    ordinary("R", 0x13, 1, 12),
    ordinary("T", 0x14, 1, 16),
    ordinary("Y", 0x15, 1, 20),
    ordinary("U", 0x16, 1, 24),
    ordinary("I", 0x17, 1, 28),
    ordinary("O", 0x18, 1, 32),
    ordinary("P", 0x19, 1, 36),

    ordinary("A", 0x1E, 2, 2),
    ordinary("S", 0x1F, 2, 6),
    ordinary("D", 0x20, 2, 10),
    ordinary("F", 0x21, 2, 14),
    ordinary("G", 0x22, 2, 18),
    ordinary("H", 0x23, 2, 22),
    ordinary("J", 0x24, 2, 26),
    ordinary("K", 0x25, 2, 30),
    ordinary("L", 0x26, 2, 34),

    {.label = "Shift", .scancode = 0x2A, .row = 3, .column = 0, .width = 6},
    ordinary("Z", 0x2C, 3, 6),
    ordinary("X", 0x2D, 3, 10),
    ordinary("C", 0x2E, 3, 14),
    ordinary("V", 0x2F, 3, 18),
    ordinary("B", 0x30, 3, 22),
    ordinary("N", 0x31, 3, 26),
    ordinary("M", 0x32, 3, 30),
    {.label = "Bksp", .scancode = 0x0E, .row = 3, .column = 34, .width = 6},

    {.label = "Space", .scancode = 0x39, .row = 4, .column = 0, .width = 24},
    {.label = "Enter", .scancode = 0x1C, .row = 4, .column = 24, .width = 16},
}};

// --- full --------------------------------------------------------------
//
// Every key the machine has: scan codes 0x01 to 0x53, each exactly once
// (screen_keyboard_test.cpp derives that against `xt_keyboard::xt_table`
// rather than trusting this list). Laid out as a grid a finger can hit
// and not as a picture of the board — the function keys are a row here,
// where an XT has them in a two-by-five block, and the keypad is two
// rows of seven carrying both of each key's legends, because Num Lock is
// what decides between them and it decides inside the BIOS.

constexpr std::array<key, 83> full_keys{{
    // The function row, centred over the 60 quarter units the widest
    // rows below are.
    ordinary("F1", 0x3B, 0, 10),
    ordinary("F2", 0x3C, 0, 14),
    ordinary("F3", 0x3D, 0, 18),
    ordinary("F4", 0x3E, 0, 22),
    ordinary("F5", 0x3F, 0, 26),
    ordinary("F6", 0x40, 0, 30),
    ordinary("F7", 0x41, 0, 34),
    ordinary("F8", 0x42, 0, 38),
    ordinary("F9", 0x43, 0, 42),
    ordinary("F10", 0x44, 0, 46),

    ordinary("Esc", 0x01, 1, 0),
    ordinary("1", 0x02, 1, 4),
    ordinary("2", 0x03, 1, 8),
    ordinary("3", 0x04, 1, 12),
    ordinary("4", 0x05, 1, 16),
    ordinary("5", 0x06, 1, 20),
    ordinary("6", 0x07, 1, 24),
    ordinary("7", 0x08, 1, 28),
    ordinary("8", 0x09, 1, 32),
    ordinary("9", 0x0A, 1, 36),
    ordinary("0", 0x0B, 1, 40),
    ordinary("-", 0x0C, 1, 44),
    ordinary("=", 0x0D, 1, 48),
    {.label = "Bksp", .scancode = 0x0E, .row = 1, .column = 52, .width = 8},

    {.label = "Tab", .scancode = 0x0F, .row = 2, .column = 0, .width = 6},
    ordinary("Q", 0x10, 2, 6),
    ordinary("W", 0x11, 2, 10),
    ordinary("E", 0x12, 2, 14),
    ordinary("R", 0x13, 2, 18),
    ordinary("T", 0x14, 2, 22),
    ordinary("Y", 0x15, 2, 26),
    ordinary("U", 0x16, 2, 30),
    ordinary("I", 0x17, 2, 34),
    ordinary("O", 0x18, 2, 38),
    ordinary("P", 0x19, 2, 42),
    ordinary("[", 0x1A, 2, 46),
    ordinary("]", 0x1B, 2, 50),
    {.label = "\\", .scancode = 0x2B, .row = 2, .column = 54, .width = 6},

    {.label = "Ctrl", .scancode = 0x1D, .row = 3, .column = 0, .width = 7},
    ordinary("A", 0x1E, 3, 7),
    ordinary("S", 0x1F, 3, 11),
    ordinary("D", 0x20, 3, 15),
    ordinary("F", 0x21, 3, 19),
    ordinary("G", 0x22, 3, 23),
    ordinary("H", 0x23, 3, 27),
    ordinary("J", 0x24, 3, 31),
    ordinary("K", 0x25, 3, 35),
    ordinary("L", 0x26, 3, 39),
    ordinary(";", 0x27, 3, 43),
    ordinary("'", 0x28, 3, 47),
    {.label = "Enter", .scancode = 0x1C, .row = 3, .column = 51, .width = 9},

    {.label = "Shift", .scancode = 0x2A, .row = 4, .column = 0, .width = 8},
    ordinary("`", 0x29, 4, 8),
    ordinary("Z", 0x2C, 4, 12),
    ordinary("X", 0x2D, 4, 16),
    ordinary("C", 0x2E, 4, 20),
    ordinary("V", 0x2F, 4, 24),
    ordinary("B", 0x30, 4, 28),
    ordinary("N", 0x31, 4, 32),
    ordinary("M", 0x32, 4, 36),
    ordinary(",", 0x33, 4, 40),
    ordinary(".", 0x34, 4, 44),
    ordinary("/", 0x35, 4, 48),
    {.label = "Shift", .scancode = 0x36, .row = 4, .column = 52, .width = 8},

    {.label = "Alt", .scancode = 0x38, .row = 5, .column = 0, .width = 8},
    {.label = "Space", .scancode = 0x39, .row = 5, .column = 8, .width = 28},
    {.label = "Caps", .scancode = 0x3A, .row = 5, .column = 36, .width = 8},
    {.label = "Num", .scancode = 0x45, .row = 5, .column = 44, .width = 8},
    {.label = "Scrl", .scancode = 0x46, .row = 5, .column = 52, .width = 8},

    {.label = "7 Home", .scancode = 0x47, .row = 6, .column = 2, .width = 8},
    {.label = "8 Up", .scancode = 0x48, .row = 6, .column = 10, .width = 8},
    {.label = "9 PgUp", .scancode = 0x49, .row = 6, .column = 18, .width = 8},
    {.label = "4 Left", .scancode = 0x4B, .row = 6, .column = 26, .width = 8},
    {.label = "5", .scancode = 0x4C, .row = 6, .column = 34, .width = 8},
    {.label = "6 Rght", .scancode = 0x4D, .row = 6, .column = 42, .width = 8},
    {.label = "*", .scancode = 0x37, .row = 6, .column = 50, .width = 8},

    {.label = "1 End", .scancode = 0x4F, .row = 7, .column = 2, .width = 8},
    {.label = "2 Down", .scancode = 0x50, .row = 7, .column = 10, .width = 8},
    {.label = "3 PgDn", .scancode = 0x51, .row = 7, .column = 18, .width = 8},
    {.label = "0 Ins", .scancode = 0x52, .row = 7, .column = 26, .width = 8},
    {.label = ". Del", .scancode = 0x53, .row = 7, .column = 34, .width = 8},
    {.label = "-", .scancode = 0x4A, .row = 7, .column = 42, .width = 8},
    {.label = "+", .scancode = 0x4E, .row = 7, .column = 50, .width = 8},
}};

constexpr std::array<layout, 3> all_layouts{{
    {.name = "prompt",
     .about = "the answers a yes/no question takes, and the key that turns "
              "a page of text",
     .rows = 1,
     .width = 40,
     .focus_scancode = 0x15,
     .keys = prompt_keys},
    {.name = "name",
     .about = "free text: the letters, the digits, a shift, a space, a "
              "backspace and a return",
     .rows = 5,
     .width = 40,
     .focus_scancode = 0x1E,
     .keys = name_keys},
    {.name = "full",
     .about = "every key this machine's keyboard has, all eighty-three of "
              "them",
     .rows = 8,
     .width = 60,
     .focus_scancode = 0x1E,
     .keys = full_keys},
}};

/// The centre of a key, in half-quarter units, so that an odd width does
/// not round away and two keys of the same span compare equal. Every
/// column comparison `move()` makes is in these.
[[nodiscard]] constexpr int centre_of(const key& which) noexcept {
  return (2 * static_cast<int>(which.column)) + static_cast<int>(which.width);
}

/// The half-open index range of `row` inside `which.keys`. The keys are
/// row-major, so this is one scan and not a search.
struct row_span {
  std::size_t first{0};
  std::size_t last{0};  ///< One past the end.
};

[[nodiscard]] row_span keys_on_row(const layout& which,
                                   std::uint8_t row) noexcept {
  row_span found{};
  bool seen = false;
  for (std::size_t i = 0; i < which.keys.size(); ++i) {
    if (which.keys[i].row != row) {
      if (seen) {
        break;
      }
      continue;
    }
    if (!seen) {
      found.first = i;
      seen = true;
    }
    found.last = i + 1;
  }
  return found;
}

}  // namespace

std::span<const layout> layouts() noexcept { return all_layouts; }

const layout* layout_named(std::string_view name) noexcept {
  for (const layout& which : all_layouts) {
    if (which.name == name) {
      return &which;
    }
  }
  return nullptr;
}

latch latch_of(std::uint8_t scancode) noexcept {
  if (scancode >= xt_keyboard::table_size) {
    return latch::none;
  }
  switch (xt_keyboard::xt_table[scancode].kind) {
    case xt_keyboard::key_kind::left_shift:
      return latch::left_shift;
    case xt_keyboard::key_kind::right_shift:
      return latch::right_shift;
    case xt_keyboard::key_kind::ctrl:
      return latch::ctrl;
    case xt_keyboard::key_kind::alt:
      return latch::alt;
    default:
      return latch::none;
  }
}

std::size_t default_focus(const layout& which) noexcept {
  for (std::size_t i = 0; i < which.keys.size(); ++i) {
    if (which.keys[i].scancode == which.focus_scancode) {
      return i;
    }
  }
  return no_key;
}

std::size_t key_at(const layout& which, std::uint8_t row,
                   std::uint16_t column) noexcept {
  for (std::size_t i = 0; i < which.keys.size(); ++i) {
    const key& candidate = which.keys[i];
    if (candidate.row != row) {
      continue;
    }
    const std::uint16_t from = candidate.column;
    const auto to = static_cast<std::uint16_t>(from + candidate.width);
    if (column >= from && column < to) {
      return i;
    }
  }
  return no_key;
}

std::size_t move(const layout& which, std::size_t focus, nav where) noexcept {
  if (which.keys.empty() || which.rows == 0) {
    return no_key;
  }
  if (focus >= which.keys.size()) {
    return default_focus(which);
  }

  const key& from = which.keys[focus];
  const row_span here = keys_on_row(which, from.row);

  if (where == nav::left) {
    return focus == here.first ? here.last - 1 : focus - 1;
  }
  if (where == nav::right) {
    return focus + 1 == here.last ? here.first : focus + 1;
  }

  // Up and down: the next row round, and on it the key whose span covers
  // the centre of the one being left — or, where that column is a gap,
  // the key whose own centre is nearest it, ties going left.
  const int step = where == nav::up ? -1 : 1;
  const auto rows = static_cast<int>(which.rows);
  const int target = ((static_cast<int>(from.row) + step) % rows + rows) % rows;
  const row_span there = keys_on_row(which, static_cast<std::uint8_t>(target));
  if (there.first == there.last) {
    return focus;
  }

  const int want = centre_of(from);
  std::size_t best = there.first;
  int best_distance = -1;
  for (std::size_t i = there.first; i < there.last; ++i) {
    const key& candidate = which.keys[i];
    if (want >= 2 * static_cast<int>(candidate.column) &&
        want < 2 * (static_cast<int>(candidate.column) +
                    static_cast<int>(candidate.width))) {
      return i;
    }
    const int distance = centre_of(candidate) > want
                             ? centre_of(candidate) - want
                             : want - centre_of(candidate);
    if (best_distance < 0 || distance < best_distance) {
      best = i;
      best_distance = distance;
    }
  }
  return best;
}

commit commit_key(const layout& which, std::size_t index,
                  std::uint8_t latched) noexcept {
  commit out{};
  out.latched = latched;
  if (index >= which.keys.size()) {
    return out;
  }

  const std::uint8_t scancode = which.keys[index].scancode;
  const auto bit = static_cast<std::uint8_t>(latch_of(scancode));
  if (bit != 0) {
    // A modifier: down if it is up, up if it is down, and nothing else
    // moves. The mask is what says which.
    const bool down = (latched & bit) == 0;
    out.events[0] = {.scancode = scancode, .down = down};
    out.count = 1;
    out.latched =
        static_cast<std::uint8_t>(down ? latched | bit : latched & ~bit);
    return out;
  }

  out.events[0] = {.scancode = scancode, .down = true};
  out.events[1] = {.scancode = scancode, .down = false};
  out.count = 2;

  // Then let go of whatever was latched under it, behind the key it was
  // latched for. `release_latched()` is the same walk, and the order is
  // the ascending one `host::held_keys::release_all()` uses.
  const commit release = release_latched(latched);
  for (std::size_t i = 0; i < release.count; ++i) {
    out.events[out.count] = release.events[i];
    ++out.count;
  }
  out.latched = release.latched;
  return out;
}

commit release_latched(std::uint8_t latched) noexcept {
  // In ascending scan-code order — ctrl 0x1D, left shift 0x2A, right
  // shift 0x36, alt 0x38 — which is `host::held_keys::release_all()`'s
  // order and not the order of the bits, because the bits are not in it.
  struct modifier {
    std::uint8_t scancode;
    latch bit;
  };
  constexpr std::array<modifier, 4> modifiers{{
      {.scancode = 0x1D, .bit = latch::ctrl},
      {.scancode = 0x2A, .bit = latch::left_shift},
      {.scancode = 0x36, .bit = latch::right_shift},
      {.scancode = 0x38, .bit = latch::alt},
  }};

  commit out{};
  for (const modifier& which : modifiers) {
    if ((latched & static_cast<std::uint8_t>(which.bit)) != 0) {
      out.events[out.count] = {.scancode = which.scancode, .down = false};
      ++out.count;
    }
  }
  out.latched = 0;
  return out;
}

}  // namespace amberfolio::machine::screen_keyboard
