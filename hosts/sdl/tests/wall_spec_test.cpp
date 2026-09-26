// SPDX-License-Identifier: AGPL-3.0-only
//
// wall_spec.h: `--wall`'s date (#320). A bare date is its midnight, the
// time's parts come in order and are all optional after the minute, and
// a date the calendar does not have is refused by the machine's own rule.

#include "wall_spec.h"

#include "amberfolio/machine/platform.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

TEST(WallSpec, ABareDateIsItsMidnight) {
  machine::wall_time at{};
  ASSERT_TRUE(parse_wall("1990-06-15", at));
  EXPECT_EQ(at.year, 1990);
  EXPECT_EQ(at.month, 6);
  EXPECT_EQ(at.day, 15);
  EXPECT_EQ(at.hour, 0);
  EXPECT_EQ(at.minute, 0);
}

TEST(WallSpec, TheTimeComesDownToTheCentisecond) {
  machine::wall_time at{};
  ASSERT_TRUE(parse_wall("2026-09-06T08:07:10.25", at));
  EXPECT_EQ(at.hour, 8);
  EXPECT_EQ(at.minute, 7);
  EXPECT_EQ(at.second, 10);
  EXPECT_EQ(at.centisecond, 25);
}

TEST(WallSpec, AnythingElseIsRefused) {
  machine::wall_time at{};
  EXPECT_FALSE(parse_wall("2026-09-06 08:07", at));
  EXPECT_FALSE(parse_wall("2026-09-06T08", at));
  EXPECT_FALSE(parse_wall("2026-9-6", at));
  EXPECT_FALSE(parse_wall("2026-04-31", at));
}

}  // namespace
}  // namespace amberfolio::sdl
