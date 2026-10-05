// SPDX-License-Identifier: AGPL-3.0-only
//
// The listening level F11 and F12 move, and the capture buffer the audio
// thread appends to (audio_device.h, #148, #472). Neither is machine state;
// what is held down is that the keys do what docs/hosts.md says they do.

#include "audio_device.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>

namespace amberfolio::sdl {
namespace {

using ::testing::HasSubstr;

TEST(ListeningLevel, StartsAtUnityAndAudible) {
  const listening_level level;
  EXPECT_FLOAT_EQ(level.gain(), 1.0F);
  EXPECT_FALSE(level.muted);
}

TEST(ListeningLevel, MuteSilencesTheGainAndKeepsTheLevel) {
  listening_level level{.volume = 0.5F, .muted = false};
  level.toggle_mute();
  EXPECT_FLOAT_EQ(level.gain(), 0.0F);
  EXPECT_FLOAT_EQ(level.volume, 0.5F) << "lifting the latch gives it back";
  level.toggle_mute();
  EXPECT_FLOAT_EQ(level.gain(), 0.5F);
}

TEST(ListeningLevel, LouderStepsThroughTheRungsAndWrapsToTheBottom) {
  listening_level level{.volume = 0.25F, .muted = false};
  for (const float want : {0.50F, 0.75F, 1.00F, 0.25F, 0.50F}) {
    level.louder();
    EXPECT_FLOAT_EQ(level.volume, want);
  }
}

TEST(ListeningLevel, LouderFromBetweenRungsGoesToTheNextOneUp) {
  listening_level level{.volume = 0.30F, .muted = false};
  level.louder();
  EXPECT_FLOAT_EQ(level.volume, 0.50F);
}

TEST(ListeningLevel, LouderWhileMutedOnlyLiftsTheLatch) {
  listening_level level{.volume = 0.50F, .muted = true};
  level.louder();
  EXPECT_FALSE(level.muted);
  EXPECT_FLOAT_EQ(level.volume, 0.50F) << "one press, one audible change";
}

TEST(ListeningLevel, SaysWhichOfTheTwoItIs) {
  testing::internal::CaptureStderr();
  listening_level{.volume = 0.75F, .muted = false}.say();
  listening_level{.volume = 0.75F, .muted = true}.say();
  const std::string said = testing::internal::GetCapturedStderr();
  EXPECT_THAT(said, HasSubstr("amberfolio: audio volume 75%\n"));
  EXPECT_THAT(said, HasSubstr("amberfolio: audio muted\n"));
}

TEST(CaptureSamples, StopsWhenFullAndSaysSo) {
  audio_bridge bridge;
  bridge.capture.assign(4, 0.0F);
  const std::array<float, 3> three{0.1F, 0.2F, 0.3F};

  capture_samples(bridge, three);
  EXPECT_EQ(bridge.captured.load(), 3U);
  EXPECT_FALSE(bridge.truncated.load());

  capture_samples(bridge, three);
  EXPECT_EQ(bridge.captured.load(), 4U);
  EXPECT_TRUE(bridge.truncated.load());
  EXPECT_FLOAT_EQ(bridge.capture[3], 0.1F);
}

TEST(CaptureSamples, IsNothingWithoutABuffer) {
  audio_bridge bridge;
  const std::array<float, 2> two{0.1F, 0.2F};
  capture_samples(bridge, two);
  EXPECT_EQ(bridge.captured.load(), 0U);
  EXPECT_FALSE(bridge.truncated.load());
}

}  // namespace
}  // namespace amberfolio::sdl
