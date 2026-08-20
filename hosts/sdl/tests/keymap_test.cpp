// SPDX-License-Identifier: AGPL-3.0-only
//
// The SDL host's key mapping, checked against something written down
// independently of it.
//
// #80 filed the windowed paths as compiled-but-never-run, and the
// keyboard is the one where "never run" is worst. The other paths are
// wrong all at once or not at all: a texture upload that has the stride
// wrong makes every frame garbage the first time anybody looks. A
// mapping table is wrong in one row and right in the other eighty, and
// the way that surfaces is a player, in a game, pressing the one key
// nobody happened to try — three milestones from now, looking like a bug
// in whatever they were doing at the time.
//
// A test that restated the table would find nothing: it would be the same
// eighty literals typed a second time by the same person on the same
// afternoon, and it would agree with a transposition as happily as with
// the truth. So nothing here restates it. Every claim below is derived
// from `machine::xt_keyboard`, which is core's account of the same
// hardware — written for the BIOS's benefit rather than the host's,
// written first, and asserted by its own tests against the BDA
// behaviour programs actually observe.
//
// Three claims:
//
//   1. Coverage. Every make code an 83-key XT board can send, 01h-53h,
//      is reachable from some SDL key. This is the one that finds a
//      missing row: if two SDL keys both map to 24h, then 25h has no
//      producer and this says so.
//
//   2. Soundness. Every SDL key the host maps produces a code core's
//      table has an entry for. A host that posted a code the BIOS calls
//      `unmapped` would be a keystroke that vanishes.
//
//   3. Legends agree. SDL names a key by what is printed on the keycap,
//      and for the printable keys that name is one character — and core's
//      table records the character that key sends unshifted. On a US
//      layout those are the same character. That is the claim a
//      transposed row fails: swap the mappings for `,` and `.` and
//      coverage still passes, soundness still passes, and this does not.
//
// The keys whose legend is a word rather than a character — Escape, Tab,
// Return, Backspace, Space — are the handful claim 3 cannot reach, and
// they are named one by one at the end, against the control codes core
// says they send.

#include "keymap.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/keyboard.h"

namespace amberfolio::sdl {
namespace {

using machine::xt_keyboard::key_kind;
using machine::xt_keyboard::table_size;
using machine::xt_keyboard::xt_table;

/// The make codes an 83-key board can send: 01h through 53h. Slot 0 is
/// never a make code (keyboard.h).
constexpr std::uint8_t first_make_code = 0x01;
constexpr std::uint8_t last_make_code = table_size - 1;

/// Every SDL scancode there is. `SDL_SCANCODE_COUNT` is SDL's own count,
/// so this stays right when SDL grows one.
[[nodiscard]] std::vector<SDL_Scancode> all_scancodes() {
  std::vector<SDL_Scancode> codes;
  codes.reserve(static_cast<std::size_t>(SDL_SCANCODE_COUNT));
  for (int i = 0; i < SDL_SCANCODE_COUNT; ++i) {
    codes.push_back(static_cast<SDL_Scancode>(i));
  }
  return codes;
}

/// What SDL calls a key. A table lookup inside SDL, so it needs no
/// SDL_Init and this test opens no window and no device.
[[nodiscard]] std::string_view legend(SDL_Scancode code) {
  const char* name = SDL_GetScancodeName(code);
  return name != nullptr ? std::string_view(name) : std::string_view();
}

/// The character core says a key sends with nothing held, or 0 for a key
/// that does not send one.
[[nodiscard]] char unshifted_char(std::uint8_t make_code) {
  const machine::xt_keyboard::key_entry& entry = xt_table[make_code];
  switch (entry.kind) {
    case key_kind::ascii:
    case key_kind::letter:
      return entry.unshifted;
    case key_kind::numpad_digit:
      return entry.numeric;
    default:
      return '\0';
  }
}

// --- 1. Coverage --------------------------------------------------------

TEST(SdlKeymap, ReachesEveryMakeCodeTheBoardHas) {
  std::array<bool, table_size> reachable{};
  for (const SDL_Scancode code : all_scancodes()) {
    const std::uint8_t make = xt_scancode(code);
    if (make != 0) {
      reachable[make] = true;
    }
  }

  std::vector<std::string> missing;
  for (std::uint8_t make = first_make_code; make <= last_make_code; ++make) {
    if (!reachable[make]) {
      char said[64] = {};
      std::snprintf(said, sizeof said, "%02Xh (%s)", make,
                    unshifted_char(make) != '\0'
                        ? std::string(1, unshifted_char(make)).c_str()
                        : "no character");
      missing.emplace_back(said);
    }
  }
  EXPECT_THAT(missing, testing::IsEmpty())
      << "these XT make codes cannot be produced from any SDL key, so the "
         "key that sends them on a real board does nothing here";
}

// --- 2. Soundness -------------------------------------------------------

TEST(SdlKeymap, PostsNothingTheBiosHasNoEntryFor) {
  for (const SDL_Scancode code : all_scancodes()) {
    const std::uint8_t make = xt_scancode(code);
    if (make == 0) {
      continue;
    }
    EXPECT_GE(make, first_make_code) << "for SDL key '" << legend(code) << "'";
    EXPECT_LE(make, last_make_code) << "for SDL key '" << legend(code) << "'";
    if (make >= first_make_code && make <= last_make_code) {
      EXPECT_NE(xt_table[make].kind, key_kind::unmapped)
          << "SDL key '" << legend(code) << "' posts " << std::hex
          << static_cast<unsigned>(make) << "h, which the BIOS ignores";
    }
  }
}

// --- 3. The legends agree ----------------------------------------------

TEST(SdlKeymap, EveryPrintableKeySendsTheCharacterOnItsKeycap) {
  unsigned checked = 0;
  for (const SDL_Scancode code : all_scancodes()) {
    const std::uint8_t make = xt_scancode(code);
    if (make == 0) {
      continue;
    }
    // Only the keys SDL names with a single character: those are the
    // printable ones, and a keycap legend is the character the key
    // sends. Everything else — "Escape", "Left", "Keypad 7" — is a word
    // and is either a control key or the numeric keypad, both of which
    // are covered above and below.
    const std::string_view name = legend(code);
    if (name.size() != 1) {
      continue;
    }

    const auto lowered = static_cast<char>(
        std::tolower(static_cast<unsigned char>(name.front())));
    EXPECT_EQ(unshifted_char(make), lowered)
        << "SDL key '" << name << "' posts " << std::hex
        << static_cast<unsigned>(make) << "h, which the BIOS says is '"
        << unshifted_char(make) << "'";
    ++checked;
  }

  // A guard on the loop itself: if SDL ever stopped naming keys by their
  // legend, every iteration above would `continue` and this test would
  // pass by checking nothing. 47 is the printable keys of a US board —
  // 26 letters, 10 digits and 11 punctuation keys — and the number is
  // here to be recomputed, not trusted.
  EXPECT_EQ(checked, 26U + 10U + 11U);
}

TEST(SdlKeymap, TheKeysWhoseLegendIsAWord) {
  // The control characters, named individually because claim 3 cannot
  // reach them — each against what core says the key sends.
  EXPECT_EQ(unshifted_char(xt_scancode(SDL_SCANCODE_ESCAPE)), '\x1B');
  EXPECT_EQ(unshifted_char(xt_scancode(SDL_SCANCODE_TAB)), '\t');
  EXPECT_EQ(unshifted_char(xt_scancode(SDL_SCANCODE_RETURN)), '\r');
  EXPECT_EQ(unshifted_char(xt_scancode(SDL_SCANCODE_BACKSPACE)), '\b');
  EXPECT_EQ(unshifted_char(xt_scancode(SDL_SCANCODE_SPACE)), ' ');

  // The modifiers, against the kind core files them under rather than a
  // character they do not have.
  EXPECT_EQ(xt_table[xt_scancode(SDL_SCANCODE_LSHIFT)].kind,
            key_kind::left_shift);
  EXPECT_EQ(xt_table[xt_scancode(SDL_SCANCODE_RSHIFT)].kind,
            key_kind::right_shift);
  EXPECT_EQ(xt_table[xt_scancode(SDL_SCANCODE_LCTRL)].kind, key_kind::ctrl);
  EXPECT_EQ(xt_table[xt_scancode(SDL_SCANCODE_LALT)].kind, key_kind::alt);
  EXPECT_EQ(xt_table[xt_scancode(SDL_SCANCODE_CAPSLOCK)].kind,
            key_kind::caps_lock);

  // F1-F10, in order and contiguous, which is what the board sends.
  constexpr std::array<SDL_Scancode, 10> function_keys = {
      SDL_SCANCODE_F1, SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
      SDL_SCANCODE_F5, SDL_SCANCODE_F6, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
      SDL_SCANCODE_F9, SDL_SCANCODE_F10};
  for (std::size_t i = 0; i < function_keys.size(); ++i) {
    const std::uint8_t make = xt_scancode(function_keys[i]);
    EXPECT_EQ(xt_table[make].kind, key_kind::function);
    EXPECT_EQ(make, 0x3B + i);
  }

  // The arrow cluster and the keypad are the same keys on this board:
  // an 83-key XT has no dedicated arrows, so a modern keyboard's arrow
  // and its keypad twin are two ways to press one key and must post the
  // same code. That they *are* the same code is what stops claim 1 from
  // demanding twice the coverage the board has.
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_UP), xt_scancode(SDL_SCANCODE_KP_8));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_DOWN), xt_scancode(SDL_SCANCODE_KP_2));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_LEFT), xt_scancode(SDL_SCANCODE_KP_4));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_RIGHT), xt_scancode(SDL_SCANCODE_KP_6));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_HOME), xt_scancode(SDL_SCANCODE_KP_7));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_END), xt_scancode(SDL_SCANCODE_KP_1));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_PAGEUP), xt_scancode(SDL_SCANCODE_KP_9));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_PAGEDOWN), xt_scancode(SDL_SCANCODE_KP_3));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_INSERT), xt_scancode(SDL_SCANCODE_KP_0));
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_DELETE),
            xt_scancode(SDL_SCANCODE_KP_PERIOD));
}

TEST(SdlKeymap, SaysNothingForKeysTheBoardDoesNotHave) {
  // The 83-key board has no F11, no F12, no separate keypad Enter and no
  // keypad divide — those arrived with later keyboards. A host that
  // invented codes for them would be faking a machine, which is the one
  // thing this project does not do (PLAN.md §3, "log, don't fake").
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_F11), 0);
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_F12), 0);
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_KP_ENTER), 0);
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_KP_DIVIDE), 0);
  EXPECT_EQ(xt_scancode(SDL_SCANCODE_UNKNOWN), 0);
}

}  // namespace
}  // namespace amberfolio::sdl
