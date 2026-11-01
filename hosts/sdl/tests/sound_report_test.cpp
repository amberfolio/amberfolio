// SPDX-License-Identifier: AGPL-3.0-only
//
// sound_report.h: what the desktop says about the Tandy chip (#404). A
// copy whose file names another device says so before the load; one that
// already says Tandy, or says nothing, is quiet; and a run that never
// touched the chip adds no line to its end.

#include "sound_report.h"

#include <cstdint>
#include <optional>

#include "amberfolio/machine/launcher_view.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

/// What each program's configuration file says, as the reader answers it.
machine::configured_sound pool(char letter) {
  return {.file = "POOL.CFG", .letter = static_cast<std::uint8_t>(letter)};
}
machine::configured_sound curse(char letter) {
  return {.file = "CURSE.CFG", .letter = static_cast<std::uint8_t>(letter)};
}

TEST(SoundReport, AFileThatNamesTheSpeakerIsSaid) {
  EXPECT_EQ(started_sound_line(pool('P')),
            "amberfolio: POOL.CFG sound P, started as T (Tandy sound)\n");
  EXPECT_EQ(started_sound_line(pool('S')),
            "amberfolio: POOL.CFG sound S, started as T (Tandy sound)\n");
  EXPECT_EQ(started_sound_line(curse('P')),
            "amberfolio: CURSE.CFG sound P, started as T (Tandy sound)\n");
}

TEST(SoundReport, AFileThatAlreadySaysTandyOrNothingIsQuiet) {
  EXPECT_EQ(started_sound_line(pool('T')), "");
  EXPECT_EQ(started_sound_line(curse('T')), "");
  EXPECT_EQ(started_sound_line(std::nullopt), "");
}

TEST(SoundReport, TheChipsTrafficOnlyWhenThereWasSome) {
  EXPECT_EQ(chip_traffic_line(0, 0), "");
  EXPECT_EQ(chip_traffic_line(1907, 0),
            "amberfolio: tandy writes=1907 dropped=0\n");
  EXPECT_EQ(chip_traffic_line(0, 3), "amberfolio: tandy writes=0 dropped=3\n");
}

}  // namespace
}  // namespace amberfolio::sdl
