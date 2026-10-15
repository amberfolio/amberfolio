// SPDX-License-Identifier: AGPL-3.0-only
//
// What a launch settles on once the command line and the config file have
// both been read (launch_config.h, #382, #472). desktop_config_test.cpp holds
// the format and the `prefer()` rule on their own; this is the same rule
// through the host's own plumbing -- the file path, the replay exemption, the
// first run -- which is where a quiet overruling of a flag would live.
//
// Every case is headless, or has a config that already answers the sidecar
// question: this host asks that question on stdin, and a test must never be
// the thing it waits for.

#include "launch_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "desktop_config.h"
#include "options.h"

namespace amberfolio::sdl {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;

/// A scratch directory of this test's own, gone when it is.
class LaunchConfig : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ =
        std::filesystem::temp_directory_path() /
        ("amberfolio-launch-config-" +
         std::string(
             ::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }

  void TearDown() override {
    std::error_code why;
    std::filesystem::remove_all(dir_, why);
  }

  /// A config file with this text, and its path.
  [[nodiscard]] std::string config_with(std::string_view text) const {
    const std::filesystem::path path = dir_ / "config.txt";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return path.string();
  }

  [[nodiscard]] std::string path_of(std::string_view name) const {
    return (dir_ / name).string();
  }

 private:
  std::filesystem::path dir_;
};

TEST(VolumePercent, RoundTripsEveryPercentageTheFlagTakes) {
  for (unsigned percent = 0; percent <= 100; ++percent) {
    EXPECT_EQ(volume_percent(static_cast<float>(percent) / 100.0F), percent);
  }
  EXPECT_EQ(volume_percent(0.333F), 33U);
}

TEST(NobodyIsAtTheKeyboard, IsTheShapeOfTheRunAndNotATerminal) {
  options person;
  EXPECT_FALSE(nobody_is_at_the_keyboard(person));

  for (int shape = 0; shape < 6; ++shape) {
    options driven;
    switch (shape) {
      case 0:
        driven.headless = true;
        break;
      case 1:
        driven.presses.emplace_back();
        break;
      case 2:
        driven.pulls.emplace_back();
        break;
      case 3:
        driven.record_path = "a.rec";
        break;
      case 4:
        driven.dump_prefix = "out";
        break;
      default:
        driven.verify = true;
        break;
    }
    EXPECT_TRUE(nobody_is_at_the_keyboard(driven)) << shape;
  }
}

TEST(ConfigOf, WritesWhatTheRunSettledOn) {
  options opts;
  opts.root = "games/por";
  opts.program = "START.EXE";
  opts.seams = {"automap"};
  opts.volume = 0.5F;
  opts.muted = true;
  opts.speed = machine::speed_preset::turbo_xt;
  opts.scale = 2;
  opts.save_sidecars = true;

  const desktop_config config = config_of(opts);
  EXPECT_EQ(config.game_directory, "games/por");
  EXPECT_EQ(config.program, "START.EXE");
  EXPECT_THAT(config.seams.value_or(std::vector<std::string>{}),
              ElementsAre("automap"));
  EXPECT_EQ(config.volume_percent, 50U);
  EXPECT_EQ(config.muted, true);
  EXPECT_EQ(config.speed, "turbo");
  EXPECT_EQ(config.scale, 2U);
  EXPECT_EQ(config.save_sidecars, true);
  EXPECT_FALSE(config.journal_ocr.has_value())
      << "a discovered engine is never frozen into the file";
}

TEST(ConfigOf, LeavesOutWhatNobodyNamed) {
  const desktop_config config = config_of(options{});
  EXPECT_FALSE(config.game_directory.has_value());
  EXPECT_FALSE(config.program.has_value());
  EXPECT_FALSE(config.seams.has_value());
}

TEST_F(LaunchConfig, TheConfigFillsWhatTheCommandLineLeftOut) {
  options opts;
  opts.headless = true;
  opts.config_path = config_with(
      "amberfolio-config 1\n"
      "game-directory /games/por\n"
      "program START.EXE\n"
      "seam automap\n"
      "volume 40\n"
      "speed turbo\n"
      "scale 2\n"
      "save-sidecars off\n");

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              HasSubstr("amberfolio: config read "));

  EXPECT_EQ(opts.root.generic_string(), "/games/por");
  EXPECT_EQ(opts.program, "START.EXE");
  EXPECT_THAT(opts.seams, ElementsAre("automap"));
  EXPECT_TRUE(opts.seams_from_config);
  EXPECT_FLOAT_EQ(opts.volume, 0.4F);
  EXPECT_EQ(opts.speed, machine::speed_preset::turbo_xt);
  EXPECT_EQ(opts.scale, 2U);
  EXPECT_FALSE(opts.save_sidecars);
}

TEST_F(LaunchConfig, AFlagIsNeverOverruledEvenOneThatNamesTheDefault) {
  options opts;
  opts.headless = true;
  opts.config_path = config_with(
      "amberfolio-config 1\n"
      "game-directory /games/por\n"
      "program START.EXE\n"
      "seam automap\n"
      "speed turbo\n"
      "save-sidecars off\n");
  opts.speed = machine::speed_preset::pc_xt;
  opts.given.speed = true;
  opts.seams = {"journal"};
  opts.given.seams = true;

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  static_cast<void>(testing::internal::GetCapturedStderr());

  EXPECT_EQ(opts.speed, machine::speed_preset::pc_xt);
  EXPECT_THAT(opts.seams, ElementsAre("journal"));
  EXPECT_FALSE(opts.seams_from_config);
}

TEST_F(LaunchConfig, ANoConfigRunReadsNothing) {
  options opts;
  opts.headless = true;
  opts.no_config = true;
  opts.root = "games/por";
  opts.program = "START.EXE";

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              ::testing::Not(HasSubstr("config read")));
}

TEST_F(LaunchConfig, AReplayReadsNoConfigAtAll) {
  options opts;
  opts.replay_path = "a.rec";
  opts.root = "games/por";
  opts.program = "START.EXE";
  opts.config_path = config_with(
      "amberfolio-config 1\n"
      "speed 386\n"
      "seam automap\n");

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  const std::string said = testing::internal::GetCapturedStderr();

  EXPECT_THAT(said, HasSubstr("config not read for --replay"));
  EXPECT_EQ(opts.speed, machine::default_speed);
  EXPECT_TRUE(opts.seams.empty());
}

TEST_F(LaunchConfig, AReplayWithoutItsDiskIsACommandLineToFix) {
  options opts;
  opts.replay_path = "a.rec";
  opts.config_path = config_with(
      "amberfolio-config 1\n"
      "game-directory /games/por\n"
      "program START.EXE\n");

  testing::internal::CaptureStderr();
  EXPECT_FALSE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              HasSubstr("a replay needs the directory and the program"));
  EXPECT_FALSE(opts.first_run) << "an error, not an invitation";
}

TEST_F(LaunchConfig, NothingToRunAndNoConfigIsAFirstRun) {
  options opts;
  opts.headless = true;
  opts.config_path = path_of("not-there.txt");

  testing::internal::CaptureStderr();
  EXPECT_FALSE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              HasSubstr("no game directory yet, which is not a problem"));
  EXPECT_TRUE(opts.first_run);
}

TEST_F(LaunchConfig, AConfigThisBuildCannotReadIsLeftAndTheDefaultsStand) {
  options opts;
  opts.headless = true;
  opts.root = "games/por";
  opts.program = "START.EXE";
  const std::string text =
      "amberfolio-config 1\n"
      "volume loud\n";
  opts.config_path = config_with(text);

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              HasSubstr("config starting on the defaults"));
  EXPECT_FLOAT_EQ(opts.volume, 1.0F);

  std::ifstream still(opts.config_path, std::ios::binary);
  const std::string kept((std::istreambuf_iterator<char>(still)),
                         std::istreambuf_iterator<char>());
  EXPECT_EQ(kept, text) << "never repaired";
}

TEST_F(LaunchConfig, RememberWritesTheSettledRunAndForgetEmptiesIt) {
  options opts;
  opts.headless = true;
  opts.root = "games/por";
  opts.program = "START.EXE";
  opts.speed = machine::speed_preset::at;
  opts.given.speed = true;
  opts.save_sidecars = false;
  opts.given.save_sidecars = true;
  opts.remember = true;
  opts.config_path = path_of("remembered.txt");

  testing::internal::CaptureStderr();
  ASSERT_TRUE(settle_config(opts));
  EXPECT_THAT(testing::internal::GetCapturedStderr(),
              HasSubstr("config remembered "));

  desktop_config back;
  std::ifstream file(opts.config_path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  ASSERT_TRUE(back.parse(text).ok());
  EXPECT_EQ(back.program, "START.EXE");
  EXPECT_EQ(back.speed, "at");

  options forget;
  forget.headless = true;
  forget.forget_config = true;
  forget.config_path = opts.config_path;
  testing::internal::CaptureStderr();
  EXPECT_FALSE(settle_config(forget));
  static_cast<void>(testing::internal::GetCapturedStderr());

  std::ifstream emptied(opts.config_path, std::ios::binary);
  const std::string after((std::istreambuf_iterator<char>(emptied)),
                          std::istreambuf_iterator<char>());
  desktop_config nothing;
  ASSERT_TRUE(nothing.parse(after).ok());
  EXPECT_TRUE(nothing.empty());
}

}  // namespace
}  // namespace amberfolio::sdl
