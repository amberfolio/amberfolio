// SPDX-License-Identifier: AGPL-3.0-only
//
// The on-screen keyboard's pixels (#377): a layout fitted to a window, a
// key's rectangle inside that fit, and which key a pointer landed on.
//
// The keyboard itself is core's and `screen_keyboard_test.cpp` pins it.
// What is checked here is the only part this host adds — the arithmetic
// between a quarter unit and a pixel — and the one claim that matters
// about it: a point inside a key's own rectangle finds that key, at every
// window size, in every layout. A hit test that is right for the window
// the author happened to have is the bug this exists to catch.

#include "screen_keyboard_view.h"

#include <array>
#include <cstddef>
#include <string>
#include <utility>

#include "amberfolio/machine/screen_keyboard.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

namespace osk = machine::screen_keyboard;

/// Window sizes worth trying: the host's own default (320x200 at scale
/// 3), a small window, a wide one and a tall one — the tall one because
/// the height share is what stops a one-row layout from filling it.
constexpr std::array<std::pair<int, int>, 4> windows{{
    {960, 600},
    {320, 200},
    {1920, 480},
    {600, 1200},
}};

TEST(ScreenKeyboardView, FitsInsideTheWindowItIsGiven) {
  for (const auto& [width, height] : windows) {
    for (const osk::layout& which : osk::layouts()) {
      SCOPED_TRACE(std::string(which.name) + " in " + std::to_string(width) +
                   "x" + std::to_string(height));
      const keyboard_box box = fit_keyboard(which, width, height);
      EXPECT_GT(box.quarter, 0.0F);
      EXPECT_GE(box.left, 0.0F);
      EXPECT_LE(box.left + box.width, static_cast<float>(width));
      EXPECT_LE(box.top + box.height, static_cast<float>(height));
      EXPECT_LE(box.height, static_cast<float>(height) / 2.0F);
    }
  }
}

TEST(ScreenKeyboardView, GivesEveryKeyARectangleInsideTheKeyboard) {
  const osk::layout& board = *osk::layout_named("full");
  const keyboard_box box = fit_keyboard(board, 960, 600);
  for (const osk::key& one : board.keys) {
    SCOPED_TRACE(one.label);
    const SDL_FRect rect = key_rect(box, one);
    EXPECT_GT(rect.w, 0.0F);
    EXPECT_GT(rect.h, 0.0F);
    EXPECT_GE(rect.x, box.left);
    EXPECT_GE(rect.y, box.top);
    EXPECT_LE(rect.x + rect.w, box.left + box.width);
    EXPECT_LE(rect.y + rect.h, box.top + box.height);
  }
}

/// The claim: the pointer path and the drawing path agree. A key drawn
/// somewhere a click on it does not find is the failure a player meets as
/// "that key does nothing".
TEST(ScreenKeyboardView, FindsEveryKeyUnderTheMiddleOfItsOwnRectangle) {
  for (const auto& [width, height] : windows) {
    for (const osk::layout& which : osk::layouts()) {
      const keyboard_box box = fit_keyboard(which, width, height);
      for (std::size_t i = 0; i < which.keys.size(); ++i) {
        SCOPED_TRACE(std::string(which.name) + " " +
                     std::string(which.keys[i].label) + " in " +
                     std::to_string(width) + "x" + std::to_string(height));
        const SDL_FRect rect = key_rect(box, which.keys[i]);
        EXPECT_EQ(key_under(box, which, rect.x + (rect.w / 2.0F),
                            rect.y + (rect.h / 2.0F)),
                  i);
      }
    }
  }
}

TEST(ScreenKeyboardView, FindsNothingOutsideTheKeyboard) {
  const osk::layout& board = *osk::layout_named("full");
  const keyboard_box box = fit_keyboard(board, 960, 600);
  EXPECT_EQ(key_under(box, board, box.left - 1.0F, box.top + 1.0F),
            osk::no_key);
  EXPECT_EQ(key_under(box, board, box.left + 1.0F, box.top - 1.0F),
            osk::no_key);
  EXPECT_EQ(key_under(box, board, box.left + box.width + 1.0F, box.top + 1.0F),
            osk::no_key);
  EXPECT_EQ(key_under(box, board, box.left + 1.0F, box.top + box.height + 1.0F),
            osk::no_key);
  // The function row is centred, so the keyboard's own top-left corner is
  // inside the keyboard and on no key at all.
  EXPECT_EQ(key_under(box, board, box.left + 1.0F, box.top + 1.0F),
            osk::no_key);
}

}  // namespace
}  // namespace amberfolio::sdl
