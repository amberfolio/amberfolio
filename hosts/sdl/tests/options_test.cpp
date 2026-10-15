// SPDX-License-Identifier: AGPL-3.0-only
//
// The command line (options.h, #472): the small grammars it is built from,
// and the combinations it refuses. A refusal is a sentence on stderr and an
// `options` that is not `valid`; what is held down here is that each pair of
// flags that cannot mean anything together says so, and that a line which
// is fine is not refused.

#include "options.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "amberfolio/machine/clock.h"

namespace amberfolio::sdl {
namespace {

using ::testing::HasSubstr;
using ::testing::IsEmpty;

/// `amberfolio ARGS...`, parsed. The strings outlive the call, which is
/// what `argv` is.
[[nodiscard]] options parse_line(std::vector<std::string> args) {
  args.insert(args.begin(), "amberfolio");
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (std::string& one : args) {
    argv.push_back(one.data());
  }
  return parse(static_cast<int>(argv.size()), argv.data());
}

/// What a refused line said, and that it was refused.
[[nodiscard]] std::string refused(std::vector<std::string> args) {
  testing::internal::CaptureStderr();
  const options opts = parse_line(std::move(args));
  const std::string said = testing::internal::GetCapturedStderr();
  EXPECT_FALSE(opts.valid) << said;
  return said;
}

TEST(ParseCount, ReadsADecimalAndNothingElse) {
  std::uint64_t out = 7;
  EXPECT_TRUE(parse_count("0", out));
  EXPECT_EQ(out, 0U);
  EXPECT_TRUE(parse_count("12345678901", out));
  EXPECT_EQ(out, 12345678901ULL);

  out = 7;
  EXPECT_FALSE(parse_count(nullptr, out));
  EXPECT_FALSE(parse_count("", out));
  EXPECT_FALSE(parse_count("-1", out));
  EXPECT_FALSE(parse_count("12x", out));
  EXPECT_FALSE(parse_count("one", out));
  EXPECT_EQ(out, 7U) << "a refusal leaves the answer alone";
}

TEST(ParsePull, IsASeamIdAtAFrame) {
  scripted_pull pull;
  ASSERT_TRUE(parse_pull("cheat-kill-all@600", pull));
  EXPECT_EQ(pull.id, "cheat-kill-all");
  EXPECT_EQ(pull.frame, 600U);
  EXPECT_FALSE(pull.done);

  EXPECT_FALSE(parse_pull("cheat-kill-all", pull));
  EXPECT_FALSE(parse_pull("@600", pull));
  EXPECT_FALSE(parse_pull("cheat-kill-all@", pull));
  EXPECT_FALSE(parse_pull("cheat-kill-all@soon", pull));
}

TEST(ParseWatch, IsAHexOffsetAndAByteOrWordWidth) {
  watch_point point;
  ASSERT_TRUE(parse_watch("6AAD", point));
  EXPECT_EQ(point.offset, 0x6AAD);
  EXPECT_EQ(point.width, 1U);

  ASSERT_TRUE(parse_watch("6aad:2", point));
  EXPECT_EQ(point.offset, 0x6AAD);
  EXPECT_EQ(point.width, 2U);

  EXPECT_FALSE(parse_watch("6AAD:3", point)) << "there is no third width";
  EXPECT_FALSE(parse_watch("", point));
  EXPECT_FALSE(parse_watch("12345", point)) << "an offset is sixteen bits";
  EXPECT_FALSE(parse_watch("0x6AAD", point)) << "hex without a prefix";
  EXPECT_FALSE(parse_watch("sixes", point));
}

TEST(SpeedWords, OneTableServesTheFlagAndTheConfigFile) {
  for (const machine::speed_preset preset :
       {machine::speed_preset::pc_xt, machine::speed_preset::turbo_xt,
        machine::speed_preset::at, machine::speed_preset::pc_386}) {
    machine::speed_preset back = machine::speed_preset::pc_xt;
    ASSERT_TRUE(speed_named(speed_word(preset), back)) << speed_word(preset);
    EXPECT_EQ(back, preset);
    EXPECT_NE(std::string(speed_name(preset)), "unknown");
  }
  machine::speed_preset out = machine::speed_preset::at;
  EXPECT_FALSE(speed_named("pdp11", out));
  EXPECT_EQ(out, machine::speed_preset::at);
}

TEST(Parse, TwoPositionalsAreTheDirectoryAndTheProgram) {
  const options opts = parse_line({"games/por", "START.EXE"});
  ASSERT_TRUE(opts.valid);
  EXPECT_EQ(opts.root.string(), "games/por");
  EXPECT_EQ(opts.program, "START.EXE");
  EXPECT_TRUE(opts.given.root);
  EXPECT_TRUE(opts.given.program);
}

TEST(Parse, NoArgumentsIsValidAndSaysWhatWasNotNamed) {
  const options opts = parse_line({});
  ASSERT_TRUE(opts.valid);
  EXPECT_TRUE(opts.root.empty());
  EXPECT_FALSE(opts.given.root);
  EXPECT_FALSE(opts.given.program);
}

TEST(Parse, ARepeatableFlagCollects) {
  const options opts =
      parse_line({"d", "p", "--seam", "automap", "--seam", "journal", "--press",
                  "A@60", "--pull", "x@5", "--watch", "6AAD:2"});
  ASSERT_TRUE(opts.valid);
  EXPECT_THAT(opts.seams, ::testing::ElementsAre("automap", "journal"));
  EXPECT_TRUE(opts.given.seams);
  EXPECT_EQ(opts.presses.size(), 1U);
  EXPECT_EQ(opts.pulls.size(), 1U);
  EXPECT_EQ(opts.watches.size(), 1U);
}

TEST(Parse, EverythingAfterTheSeparatorIsTheProgramsTail) {
  const options opts = parse_line({"d", "p", "--", "/A", "--seam"});
  ASSERT_TRUE(opts.valid);
  EXPECT_EQ(opts.command_tail, " /A --seam");
  EXPECT_THAT(opts.seams, IsEmpty());
}

TEST(Parse, AFlagIsRememberedAsGivenEvenWhenItNamesTheDefault) {
  const options opts = parse_line({"d", "p", "--speed", "xt", "--scale", "3",
                                   "--volume", "100", "--no-save-sidecars"});
  ASSERT_TRUE(opts.valid);
  EXPECT_TRUE(opts.given.speed);
  EXPECT_TRUE(opts.given.scale);
  EXPECT_TRUE(opts.given.volume);
  EXPECT_TRUE(opts.given.save_sidecars);
  EXPECT_FALSE(opts.save_sidecars);
}

TEST(Parse, AVolumeIsAPercentageAndNeverAmplifies) {
  const options half = parse_line({"d", "p", "--volume", "50"});
  ASSERT_TRUE(half.valid);
  EXPECT_FLOAT_EQ(half.volume, 0.5F);

  EXPECT_THAT(refused({"d", "p", "--volume", "150"}), HasSubstr("--volume"));
  EXPECT_THAT(refused({"d", "p", "--volume", "loud"}), HasSubstr("--volume"));
}

TEST(Parse, ABadScaleFallsBackToTheDefault) {
  const options opts = parse_line({"d", "p", "--scale", "0"});
  ASSERT_TRUE(opts.valid);
  EXPECT_EQ(opts.scale, default_scale);
}

TEST(Parse, WallIsNowNoneOrARealDate) {
  EXPECT_EQ(parse_line({"d", "p", "--wall", "now"}).wall,
            wall_source::host_clock);
  EXPECT_EQ(parse_line({"d", "p", "--wall", "none"}).wall,
            wall_source::unseeded);
  const options stated = parse_line({"d", "p", "--wall", "2026-09-06T08:07"});
  ASSERT_TRUE(stated.valid);
  EXPECT_EQ(stated.wall, wall_source::stated);
  EXPECT_EQ(stated.wall_stated.year, 2026);
  EXPECT_THAT(refused({"d", "p", "--wall", "2026-04-31"}), HasSubstr("--wall"));
}

TEST(Parse, AnUnknownOptionIsNamed) {
  EXPECT_THAT(refused({"d", "p", "--bogus"}),
              HasSubstr("unknown option --bogus"));
}

TEST(Parse, ThreePositionalsAreAMisInvocationAndGetTheUsage) {
  EXPECT_THAT(refused({"a", "b", "c"}), HasSubstr("usage: amberfolio"));
}

TEST(Parse, AHeadlessRunHasNoWindowAndNoSpeakerToTellWhatToDo) {
  EXPECT_THAT(refused({"d", "p", "--headless", "--fast", "2"}),
              HasSubstr("--fast needs a window"));
  EXPECT_THAT(refused({"d", "p", "--headless", "--mute"}),
              HasSubstr("need an audio device"));
  EXPECT_THAT(refused({"d", "p", "--headless", "--verify"}),
              HasSubstr("need a window"));
  EXPECT_THAT(refused({"d", "p", "--headless", "--keyboard", "full"}),
              HasSubstr("--keyboard needs a window"));
  EXPECT_THAT(refused({"d", "p", "--headless", "--seam-panel"}),
              HasSubstr("--seam-panel needs a window"));
}

TEST(Parse, ACompanionNeedsWhatItIsAboutTo) {
  EXPECT_THAT(refused({"d", "p", "--dump-every", "5"}),
              HasSubstr("--dump-every needs --dump"));
  EXPECT_THAT(refused({"d", "p", "--record-every", "5"}),
              HasSubstr("--record-every needs --record"));
  EXPECT_THAT(refused({"d", "p", "--rehash", "x.rec"}),
              HasSubstr("--rehash rewrites a recording"));
  EXPECT_THAT(refused({"d", "p", "--journal-probe"}),
              HasSubstr("need --journal"));
}

TEST(Parse, ARunIsRecordedOrReplayedNeverBoth) {
  EXPECT_THAT(refused({"d", "p", "--record", "a.rec", "--replay", "b.rec"}),
              HasSubstr("two halves of one thing"));
}

TEST(Parse, AReplayTakesItsSettingsFromTheRecording) {
  for (const char* also :
       {"--seam", "--speed", "--press", "--pull", "--wall"}) {
    std::vector<std::string> line = {"d", "p", "--replay", "a.rec", also};
    const std::string value = std::string(also) == "--seam"    ? "automap"
                              : std::string(also) == "--speed" ? "at"
                              : std::string(also) == "--press" ? "A@1"
                              : std::string(also) == "--pull"  ? "x@1"
                                                               : "none";
    line.push_back(value);
    EXPECT_THAT(refused(line), HasSubstr(also));
  }
  EXPECT_THAT(refused({"d", "p", "--replay", "a.rec", "--remember"}),
              HasSubstr("--remember cannot be given with --replay"));
  EXPECT_THAT(refused({"d", "p", "--replay", "a.rec", "--keyboard", "full"}),
              HasSubstr("--keyboard cannot be combined with --replay"));
}

TEST(Parse, TheConfigsOpposedPairsAreRefusedNotResolved) {
  EXPECT_THAT(refused({"d", "p", "--remember", "--no-config"}),
              HasSubstr("--remember writes the config"));
  EXPECT_THAT(refused({"d", "p", "--remember", "--forget-config"}),
              HasSubstr("--forget-config empties"));
  EXPECT_THAT(refused({"d", "p", "--no-config", "--config", "x.txt"}),
              HasSubstr("--config names a file"));
}

}  // namespace
}  // namespace amberfolio::sdl
