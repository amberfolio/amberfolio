// SPDX-License-Identifier: AGPL-3.0-only
//
// press_spec.h: the `--press` grammar (#313).
//
// Three claims. A bare `KEY@FRAME` is a tap, as every driven leg in
// docs/playable.md relies on. `:down` and `:up` after the frame pick one
// edge, and nothing else after the frame is accepted. And the split is on
// the last `@` with the suffix looked for only after it, so a key whose
// legend is punctuation (`Keypad :`, `@` on a board that has one) parses
// as the key it is.

#include "press_spec.h"

#include <string>

#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

TEST(PressSpec, ABareSpecIsATap) {
  scripted_press press;
  ASSERT_TRUE(parse_press("A@60", press));
  EXPECT_EQ(press.key, "A");
  EXPECT_EQ(press.frame, 60U);
  EXPECT_EQ(press.edges, press_edges::both);
  EXPECT_FALSE(press.done);
}

TEST(PressSpec, DownAndUpPickOneEdge) {
  scripted_press down;
  ASSERT_TRUE(parse_press("Left Alt@100:down", down));
  EXPECT_EQ(down.key, "Left Alt");
  EXPECT_EQ(down.frame, 100U);
  EXPECT_EQ(down.edges, press_edges::down);

  scripted_press up;
  ASSERT_TRUE(parse_press("Left Alt@160:up", up));
  EXPECT_EQ(up.key, "Left Alt");
  EXPECT_EQ(up.frame, 160U);
  EXPECT_EQ(up.edges, press_edges::up);
}

TEST(PressSpec, TheSuffixIsLookedForAfterTheFrameOnly) {
  scripted_press press;
  ASSERT_TRUE(parse_press("Keypad :@7", press));
  EXPECT_EQ(press.key, "Keypad :");
  EXPECT_EQ(press.frame, 7U);
  EXPECT_EQ(press.edges, press_edges::both);

  ASSERT_TRUE(parse_press("Keypad :@7:up", press));
  EXPECT_EQ(press.key, "Keypad :");
  EXPECT_EQ(press.edges, press_edges::up);

  // The split is on the last `@`: a key legend that is itself `@`.
  ASSERT_TRUE(parse_press("@@3:down", press));
  EXPECT_EQ(press.key, "@");
  EXPECT_EQ(press.frame, 3U);
  EXPECT_EQ(press.edges, press_edges::down);
}

TEST(PressSpec, AnythingElseIsRefusedAndLeavesTheOutputAlone) {
  scripted_press press;
  press.key = "untouched";
  press.frame = 9;
  press.edges = press_edges::up;
  for (const char* spec :
       {"", "A", "@60", "A@", "A@x", "A@60x", "A@60:", "A@60:held", "A@60:Down",
        "A@60:down:up", "A@-1", "A@60 ", "A@:down"}) {
    EXPECT_FALSE(parse_press(spec, press)) << spec;
    EXPECT_EQ(press.key, "untouched") << spec;
    EXPECT_EQ(press.frame, 9U) << spec;
    EXPECT_EQ(press.edges, press_edges::up) << spec;
  }
}

}  // namespace
}  // namespace amberfolio::sdl
