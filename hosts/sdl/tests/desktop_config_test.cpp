// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop config file (#382): its format, and the precedence rule
// that decides what a run is actually settling on.
//
// Two claims, and the second is the load-bearing one. A format that
// round-trips is worth pinning; a **flag > config > default** that is
// right two arms out of three is a host that quietly overrules a
// player's command line, which is the kind of bug a person reports as
// "it ignores what I type sometimes".

#include "desktop_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace amberfolio::sdl {
namespace {

using ::testing::ElementsAre;

TEST(DesktopConfig, ReadsEverySettingItCanHold) {
  desktop_config config;
  const config_reading read = config.parse(
      "amberfolio-config 1\n"
      "game-directory /home/somebody/games/a game\n"
      "program START.EXE\n"
      "seam automap\n"
      "seam journal\n"
      "journal-ocr /usr/bin/tesseract\n"
      "volume 75\n"
      "mute on\n"
      "speed at\n"
      "scale 3\n"
      "save-sidecars on\n");

  ASSERT_TRUE(read.ok()) << config_trouble_name(read.why);
  // The value is the whole rest of the line, so a path with a space in
  // it needs no quoting — which is most of the paths on two of the three
  // desktops.
  EXPECT_EQ(config.game_directory, "/home/somebody/games/a game");
  EXPECT_EQ(config.program, "START.EXE");
  // Compared as the optional it is, rather than dereferenced behind an
  // ASSERT: `bugprone-unchecked-optional-access` cannot see a gtest
  // macro's early return, and a `*` here fails the lint on a claim that
  // is already the stronger one — that the file said these two seams and
  // said them at all.
  EXPECT_EQ(config.seams,
            (std::optional<std::vector<std::string>>{{"automap", "journal"}}));
  EXPECT_EQ(config.journal_ocr, "/usr/bin/tesseract");
  EXPECT_EQ(config.volume_percent, 75U);
  EXPECT_EQ(config.muted, true);
  EXPECT_EQ(config.speed, "at");
  EXPECT_EQ(config.scale, 3U);
  EXPECT_EQ(config.save_sidecars, true);
}

TEST(DesktopConfig, RoundTrips) {
  desktop_config config;
  ASSERT_TRUE(config
                  .parse("amberfolio-config 1\n"
                         "game-directory D:\\games\\por\n"
                         "program START.EXE\n"
                         "seam explored\n"
                         "volume 0\n"
                         "mute off\n"
                         "speed 386\n"
                         "scale 1\n"
                         "save-sidecars off\n")
                  .ok());

  desktop_config again;
  ASSERT_TRUE(again.parse(config.serialize()).ok());
  EXPECT_EQ(again.game_directory, config.game_directory);
  EXPECT_EQ(again.program, config.program);
  EXPECT_EQ(again.seams, config.seams);
  EXPECT_EQ(again.volume_percent, config.volume_percent);
  EXPECT_EQ(again.muted, config.muted);
  EXPECT_EQ(again.speed, config.speed);
  EXPECT_EQ(again.scale, config.scale);
  EXPECT_EQ(again.save_sidecars, config.save_sidecars);
}

TEST(DesktopConfig, SaysNothingAboutWhatNobodyChose) {
  desktop_config config;
  ASSERT_TRUE(config
                  .parse("amberfolio-config 1\n"
                         "game-directory /games/por\n")
                  .ok());

  EXPECT_FALSE(config.seams.has_value());
  EXPECT_FALSE(config.journal_ocr.has_value());
  EXPECT_FALSE(config.volume_percent.has_value());
  // A config that named no engine is the whole of "go and look"
  // (`ocr_discovery.h`); one that named `none` is a player who said no.
  desktop_config said_none;
  ASSERT_TRUE(said_none
                  .parse("amberfolio-config 1\n"
                         "journal-ocr none\n")
                  .ok());
  EXPECT_EQ(said_none.journal_ocr, "none");
}

TEST(DesktopConfig, RefusesAFileThatIsNotOne) {
  desktop_config config;
  config.program = "KEPT.EXE";

  for (const std::string_view text : {
           std::string_view(""),
           std::string_view("something else entirely\n"),
           std::string_view("amberfolio-config\n"),
           std::string_view("amberfolio-config x\n"),
       }) {
    const config_reading read = config.parse(text);
    EXPECT_EQ(read.why, config_trouble::not_a_config) << text;
    EXPECT_EQ(read.line, 1U);
  }
  // Untouched, every time: half a config is worse than none.
  EXPECT_EQ(config.program, "KEPT.EXE");
}

TEST(DesktopConfig, RefusesAConfigFromALaterBuild) {
  desktop_config config;
  const config_reading read =
      config.parse("amberfolio-config 2\ngame-directory /x\n");
  EXPECT_EQ(read.why, config_trouble::later_version);
  EXPECT_FALSE(config.game_directory.has_value());
}

TEST(DesktopConfig, NamesTheLineItCouldNotRead) {
  // The whole of "every refusal keeps its reason": a host that says
  // "bad-value" and stops has told a player nothing they can act on.
  desktop_config config;
  const config_reading value = config.parse(
      "amberfolio-config 1\n"
      "game-directory /games/por\n"
      "volume 300\n");
  EXPECT_EQ(value.why, config_trouble::bad_value);
  EXPECT_EQ(value.line, 3U);
  EXPECT_EQ(value.text, "volume 300");

  const config_reading key = config.parse(
      "amberfolio-config 1\n"
      "colour-scheme amber\n");
  EXPECT_EQ(key.why, config_trouble::unknown_key);
  EXPECT_EQ(key.line, 2U);
  EXPECT_EQ(key.text, "colour-scheme amber");

  // And nothing survived either of them.
  EXPECT_TRUE(config.empty());
}

TEST(DesktopConfig, RefusesValuesThatAreNearlyRight) {
  for (const std::string_view line : {
           std::string_view("volume 100.0"),
           std::string_view("volume -1"),
           std::string_view("scale 0"),
           std::string_view("speed pdp11"),
           std::string_view("mute yes"),
           std::string_view("save-sidecars 1"),
           std::string_view("program"),
       }) {
    desktop_config config;
    const config_reading read = config.parse(
        std::string("amberfolio-config 1\n") + std::string(line) + "\n");
    EXPECT_FALSE(read.ok()) << line;
  }
}

TEST(DesktopConfig, ReadsAFileSomebodyEditedOnWindows) {
  // CRLF, because this is a file a player is invited to open. Without
  // the normalization `speed at\r` is a machine this build has never
  // heard of, and the whole file is refused over a line ending.
  desktop_config config;
  ASSERT_TRUE(config
                  .parse("amberfolio-config 1\r\n"
                         "speed at\r\n"
                         "scale 2\r\n")
                  .ok());
  EXPECT_EQ(config.speed, "at");
  EXPECT_EQ(config.scale, 2U);
}

TEST(DesktopConfig, ForgettingSerializesToAHeaderAlone) {
  const desktop_config nothing;
  EXPECT_TRUE(nothing.empty());
  EXPECT_EQ(nothing.serialize(), "amberfolio-config 1\n");

  desktop_config back;
  EXPECT_TRUE(back.parse(nothing.serialize()).ok());
  EXPECT_TRUE(back.empty());
}

// --- flag > config > default -------------------------------------------

TEST(Precedence, AFlagIsNeverOverruled) {
  unsigned scale = 4;
  prefer(true, std::optional<unsigned>(3), scale);
  EXPECT_EQ(scale, 4U);
}

TEST(Precedence, AConfigReplacesADefault) {
  unsigned scale = 2;
  prefer(false, std::optional<unsigned>(3), scale);
  EXPECT_EQ(scale, 3U);
}

TEST(Precedence, ADefaultSurvivesASilentConfig) {
  unsigned scale = 2;
  prefer(false, std::optional<unsigned>{}, scale);
  EXPECT_EQ(scale, 2U);
}

TEST(Precedence, AFlagBeatsAConfigEvenWhenItNamesTheDefault) {
  // The reason `given_on_the_command_line` exists at all: a player is
  // allowed to choose the default, and a value cannot say whether
  // somebody chose it. Without the flag this run would come out muted.
  bool muted = false;
  prefer(true, std::optional<bool>(true), muted);
  EXPECT_FALSE(muted);
}

TEST(Precedence, SeamsAreReplacedAndNeverMerged) {
  // Additive would be a command line that cannot turn a seam *off*
  // relative to the file, which is the one thing a player with a
  // remembered seam needs to be able to do without editing a file.
  std::vector<std::string> seams{"automap"};
  const std::optional<std::vector<std::string>> from_config{
      std::vector<std::string>{"journal", "explored"}};
  prefer(true, from_config, seams);
  EXPECT_THAT(seams, ElementsAre("automap"));

  std::vector<std::string> none;
  prefer(false, from_config, none);
  EXPECT_THAT(none, ElementsAre("journal", "explored"));
}

}  // namespace
}  // namespace amberfolio::sdl
