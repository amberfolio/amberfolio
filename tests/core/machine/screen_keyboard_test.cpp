// SPDX-License-Identifier: AGPL-3.0-only
//
// The on-screen keyboard's model (screen_keyboard.h, #377): the layouts
// are well formed, the `full` one is derived against `xt_keyboard`'s own
// table rather than trusted, and the four rules a host drives them by —
// the hit test, the focus moves, the commit and the release — answer
// what the header says they answer.
//
// The one thing no test here can claim is that these are the *right*
// three layouts. That is a judgement about screens, taken from the ones
// `docs/playable.md` documents; what is testable is that nothing on the
// machine's keyboard is unreachable, that no key overlaps another, and
// that a latched Shift is let go of again.

#include "amberfolio/machine/screen_keyboard.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/keyboard.h"
#include "gtest/gtest.h"

namespace amberfolio::machine::screen_keyboard {
namespace {

[[nodiscard]] const layout& named(std::string_view name) {
  const layout* found = layout_named(name);
  EXPECT_NE(found, nullptr) << name;
  return *found;
}

/// The index of the key with make code `scancode`, which every test
/// below names a key by: an index literal would be a test that breaks
/// when a key is inserted above it rather than when the rule changes.
[[nodiscard]] std::size_t index_of(const layout& which, std::uint8_t scancode) {
  for (std::size_t i = 0; i < which.keys.size(); ++i) {
    if (which.keys[i].scancode == scancode) {
      return i;
    }
  }
  ADD_FAILURE() << which.name << " has no key " << static_cast<int>(scancode);
  return no_key;
}

// --- The tables --------------------------------------------------------

TEST(ScreenKeyboardLayouts, AreThreeAndAreNamed) {
  const std::span<const layout> all = layouts();
  ASSERT_EQ(all.size(), 3U);
  EXPECT_EQ(all[0].name, "prompt");
  EXPECT_EQ(all[1].name, "name");
  EXPECT_EQ(all[2].name, "full");
  for (const layout& which : all) {
    EXPECT_FALSE(which.about.empty()) << which.name;
    EXPECT_FALSE(which.keys.empty()) << which.name;
  }
}

TEST(ScreenKeyboardLayouts, AreFoundByNameAndOnlyByOneThisBuildHas) {
  EXPECT_EQ(layout_named("full"), &layouts()[2]);
  EXPECT_EQ(layout_named("gamepad"), nullptr);
  EXPECT_EQ(layout_named(""), nullptr);
}

TEST(ScreenKeyboardLayouts, AreRowMajorAndLeftToRightWithNoKeyOverlapping) {
  for (const layout& which : layouts()) {
    std::uint8_t row = 0;
    std::uint16_t reach = 0;  ///< Where the previous key on this row ended.
    for (const key& one : which.keys) {
      SCOPED_TRACE(std::string(which.name) + " " + std::string(one.label));
      EXPECT_GE(one.row, row) << "keys are ordered row by row";
      if (one.row != row) {
        row = one.row;
        reach = 0;
      }
      EXPECT_GE(one.column, reach) << "keys on a row are ordered left to "
                                      "right and may not overlap";
      EXPECT_GT(one.width, 0U);
      reach = static_cast<std::uint16_t>(one.column + one.width);
      EXPECT_LE(reach, which.width) << "a key may not stand outside the "
                                       "width a host sizes from";
    }
    EXPECT_EQ(row + 1, which.rows) << which.name << " counts its own rows";
  }
}

TEST(ScreenKeyboardLayouts, CarryOnlyKeysThisMachineHas) {
  for (const layout& which : layouts()) {
    for (const key& one : which.keys) {
      SCOPED_TRACE(std::string(which.name) + " " + std::string(one.label));
      ASSERT_LT(one.scancode, xt_keyboard::table_size);
      EXPECT_NE(one.scancode, 0U);
      EXPECT_NE(xt_keyboard::xt_table[one.scancode].kind,
                xt_keyboard::key_kind::unmapped);
      EXPECT_FALSE(one.label.empty());
    }
  }
}

/// The claim the `full` layout exists to make. Derived against
/// `xt_keyboard::xt_table` and not against a second list of eighty-three
/// numbers, for the reason `keymap_test.cpp` derives the SDL table:
/// a list like that is wrong in one row and right in every other.
TEST(ScreenKeyboardFull, CarriesEveryKeyOfTheMachineExactlyOnce) {
  std::array<int, xt_keyboard::table_size> seen{};
  for (const key& one : named("full").keys) {
    ASSERT_LT(one.scancode, xt_keyboard::table_size);
    ++seen[one.scancode];
  }
  for (std::size_t code = 1; code < xt_keyboard::table_size; ++code) {
    EXPECT_EQ(seen[code], 1)
        << "make code " << code << " is on the full keyboard once";
  }
  EXPECT_EQ(seen[0], 0) << "0 is not a make code";
}

TEST(ScreenKeyboardLayouts, StartTheFocusOnAKeyTheyCarry) {
  for (const layout& which : layouts()) {
    const std::size_t focus = default_focus(which);
    ASSERT_NE(focus, no_key) << which.name;
    EXPECT_EQ(which.keys[focus].scancode, which.focus_scancode);
  }
  EXPECT_EQ(named("prompt").keys[default_focus(named("prompt"))].label, "Y");
  EXPECT_EQ(named("name").keys[default_focus(named("name"))].label, "A");
}

TEST(ScreenKeyboardName, CarriesNothingThatIsNotTextOrAnEdit) {
  for (const key& one : named("name").keys) {
    SCOPED_TRACE(one.label);
    const xt_keyboard::key_kind kind = xt_keyboard::xt_table[one.scancode].kind;
    const bool text = kind == xt_keyboard::key_kind::letter ||
                      one.scancode == 0x39 ||  // space
                      (one.scancode >= 0x02 && one.scancode <= 0x0B);
    const bool edit = one.scancode == 0x0E ||  // backspace
                      one.scancode == 0x1C ||  // return
                      kind == xt_keyboard::key_kind::left_shift;
    EXPECT_TRUE(text || edit);
  }
}

// --- The hit test ------------------------------------------------------

TEST(ScreenKeyboardHitTest, FindsTheKeyUnderAColumn) {
  const layout& prompt = named("prompt");
  EXPECT_EQ(key_at(prompt, 0, 0), index_of(prompt, 0x15));  // Y, 0-8
  EXPECT_EQ(key_at(prompt, 0, 7), index_of(prompt, 0x15));
  EXPECT_EQ(key_at(prompt, 0, 8), index_of(prompt, 0x31));   // N, 8-16
  EXPECT_EQ(key_at(prompt, 0, 39), index_of(prompt, 0x01));  // Esc, 28-40
}

TEST(ScreenKeyboardHitTest, AnswersNoKeyOffTheEndAndInAGap) {
  const layout& prompt = named("prompt");
  EXPECT_EQ(key_at(prompt, 0, 40), no_key);
  EXPECT_EQ(key_at(prompt, 1, 0), no_key);
  // The function row is centred, so the layout's own left edge is a gap
  // on that row and a finger landing there presses nothing.
  EXPECT_EQ(key_at(named("full"), 0, 0), no_key);
  EXPECT_EQ(key_at(named("full"), 0, 10), index_of(named("full"), 0x3B));
}

// --- Moving the focus --------------------------------------------------

TEST(ScreenKeyboardMove, StepsAlongARowAndWrapsAtItsEnds) {
  const layout& prompt = named("prompt");
  const std::size_t y = index_of(prompt, 0x15);
  const std::size_t esc = index_of(prompt, 0x01);
  EXPECT_EQ(move(prompt, y, nav::right), index_of(prompt, 0x31));
  EXPECT_EQ(move(prompt, y, nav::left), esc) << "wraps to the end";
  EXPECT_EQ(move(prompt, esc, nav::right), y) << "wraps to the start";
}

TEST(ScreenKeyboardMove, ChangesRowAndWrapsTopToBottom) {
  const layout& text = named("name");
  const std::size_t q = index_of(text, 0x10);
  EXPECT_EQ(text.keys[move(text, q, nav::up)].label, "1");
  EXPECT_EQ(text.keys[move(text, index_of(text, 0x02), nav::up)].label, "Space")
      << "the top row's up is the bottom row";
  EXPECT_EQ(text.keys[move(text, index_of(text, 0x1C), nav::down)].label, "9")
      << "the bottom row's down is the top row, under the column it left";
}

TEST(ScreenKeyboardMove, LandsUnderTheColumnItLeftAndNotUnderTheIndex) {
  const layout& board = named("full");
  // G is the sixth letter of its row but sits over V, not over the sixth
  // key of the row below — the row below starts with a shift two keys
  // wide, so index arithmetic would land somewhere else entirely.
  EXPECT_EQ(board.keys[move(board, index_of(board, 0x22), nav::down)].label,
            "V");
  // Everything the space bar's span covers comes down onto it.
  constexpr std::array<std::uint8_t, 3> over_the_bar{{0x2C, 0x2F, 0x30}};
  for (const std::uint8_t code : over_the_bar) {  // Z, V, B
    EXPECT_EQ(board.keys[move(board, index_of(board, code), nav::down)].label,
              "Space")
        << "make code " << static_cast<int>(code);
  }
  // And leaving the bar again arrives under where it was left, not back
  // at the start of the row.
  EXPECT_EQ(board.keys[move(board, index_of(board, 0x39), nav::up)].label, "C");
}

TEST(ScreenKeyboardMove, TakesTheNearestKeyWhereTheColumnIsAGap) {
  const layout& board = named("full");
  // Esc is at the left edge of its row; the function row above it starts
  // ten quarter units in, so there is nothing over Esc at all.
  EXPECT_EQ(board.keys[move(board, index_of(board, 0x01), nav::up)].label,
            "F1");
}

TEST(ScreenKeyboardMove, StartsSomewhereSensibleFromNoFocusAtAll) {
  const layout& board = named("full");
  EXPECT_EQ(move(board, no_key, nav::right), default_focus(board));
  EXPECT_EQ(move(board, board.keys.size(), nav::up), default_focus(board));
}

// --- Committing --------------------------------------------------------

TEST(ScreenKeyboardCommit, TapsAnOrdinaryKey) {
  const layout& prompt = named("prompt");
  const commit out = commit_key(prompt, index_of(prompt, 0x15), 0);
  ASSERT_EQ(out.count, 2U);
  EXPECT_EQ(out.events[0].scancode, 0x15);
  EXPECT_TRUE(out.events[0].down);
  EXPECT_EQ(out.events[1].scancode, 0x15);
  EXPECT_FALSE(out.events[1].down);
  EXPECT_EQ(out.latched, 0U);
}

TEST(ScreenKeyboardCommit, LatchesAModifierAndLetsItGoOnASecondCommit) {
  const layout& board = named("full");
  const std::size_t shift = index_of(board, 0x2A);

  const commit down = commit_key(board, shift, 0);
  ASSERT_EQ(down.count, 1U);
  EXPECT_EQ(down.events[0].scancode, 0x2A);
  EXPECT_TRUE(down.events[0].down);
  EXPECT_EQ(down.latched, static_cast<std::uint8_t>(latch::left_shift));

  const commit up = commit_key(board, shift, down.latched);
  ASSERT_EQ(up.count, 1U);
  EXPECT_EQ(up.events[0].scancode, 0x2A);
  EXPECT_FALSE(up.events[0].down);
  EXPECT_EQ(up.latched, 0U);
}

TEST(ScreenKeyboardCommit, SendsTheKeyAndThenLetsTheLatchGoBehindIt) {
  const layout& board = named("full");
  const commit out = commit_key(board, index_of(board, 0x1E),
                                static_cast<std::uint8_t>(latch::left_shift));
  ASSERT_EQ(out.count, 3U);
  EXPECT_EQ(out.events[0].scancode, 0x1E);
  EXPECT_TRUE(out.events[0].down);
  EXPECT_EQ(out.events[1].scancode, 0x1E);
  EXPECT_FALSE(out.events[1].down);
  EXPECT_EQ(out.events[2].scancode, 0x2A) << "the shift comes up behind it";
  EXPECT_FALSE(out.events[2].down);
  EXPECT_EQ(out.latched, 0U);
}

/// The reason the two shifts have a bit each: letting go of the one that
/// was never pressed would leave the one that was down for the rest of
/// the run, and post a break for a key that never made.
TEST(ScreenKeyboardCommit, LetsGoOfTheShiftThatWasActuallyLatched) {
  const layout& board = named("full");
  const commit down = commit_key(board, index_of(board, 0x36), 0);
  EXPECT_EQ(down.latched, static_cast<std::uint8_t>(latch::right_shift));

  const commit out = commit_key(board, index_of(board, 0x1E), down.latched);
  ASSERT_EQ(out.count, 3U);
  EXPECT_EQ(out.events[2].scancode, 0x36);
  EXPECT_FALSE(out.events[2].down);
}

TEST(ScreenKeyboardCommit, LetsEveryLatchGoInAscendingScanCodeOrder) {
  const layout& board = named("full");
  const auto all =
      static_cast<std::uint8_t>(static_cast<std::uint8_t>(latch::ctrl) |
                                static_cast<std::uint8_t>(latch::left_shift) |
                                static_cast<std::uint8_t>(latch::alt));
  const commit out = commit_key(board, index_of(board, 0x1E), all);
  ASSERT_EQ(out.count, 5U);
  EXPECT_EQ(out.events[2].scancode, 0x1D);
  EXPECT_EQ(out.events[3].scancode, 0x2A);
  EXPECT_EQ(out.events[4].scancode, 0x38);
  EXPECT_EQ(out.latched, 0U);
}

/// The lock keys toggle inside the BIOS on the make code, so a tap is
/// what they want; latching one would leave it down and toggle it again
/// when it came up.
TEST(ScreenKeyboardCommit, TapsTheLockKeys) {
  const layout& board = named("full");
  constexpr std::array<std::uint8_t, 3> locks{{0x3A, 0x45, 0x46}};
  for (const std::uint8_t code : locks) {
    EXPECT_EQ(latch_of(code), latch::none) << static_cast<int>(code);
    const commit out = commit_key(board, index_of(board, code), 0);
    EXPECT_EQ(out.count, 2U) << static_cast<int>(code);
    EXPECT_EQ(out.latched, 0U);
  }
}

TEST(ScreenKeyboardCommit, AnswersNothingForAnIndexThatIsNotAKey) {
  const layout& board = named("full");
  const commit out = commit_key(board, no_key, 3);
  EXPECT_EQ(out.count, 0U);
  EXPECT_EQ(out.latched, 3U) << "and hands the mask back untouched";
}

TEST(ScreenKeyboardRelease, LetsGoOfWhatIsDownAndNothingElse) {
  const commit none = release_latched(0);
  EXPECT_EQ(none.count, 0U);

  const commit two = release_latched(
      static_cast<std::uint8_t>(static_cast<std::uint8_t>(latch::right_shift) |
                                static_cast<std::uint8_t>(latch::alt)));
  ASSERT_EQ(two.count, 2U);
  EXPECT_EQ(two.events[0].scancode, 0x36);
  EXPECT_FALSE(two.events[0].down);
  EXPECT_EQ(two.events[1].scancode, 0x38);
  EXPECT_FALSE(two.events[1].down);
  EXPECT_EQ(two.latched, 0U);
}

TEST(ScreenKeyboardLatch, IsReadOutOfTheMachinesOwnKeyTable) {
  EXPECT_EQ(latch_of(0x2A), latch::left_shift);
  EXPECT_EQ(latch_of(0x36), latch::right_shift);
  EXPECT_EQ(latch_of(0x1D), latch::ctrl);
  EXPECT_EQ(latch_of(0x38), latch::alt);
  EXPECT_EQ(latch_of(0x1E), latch::none);
  EXPECT_EQ(latch_of(0x00), latch::none);
  EXPECT_EQ(latch_of(0xFF), latch::none) << "and refuses to index past it";
}

}  // namespace
}  // namespace amberfolio::machine::screen_keyboard
