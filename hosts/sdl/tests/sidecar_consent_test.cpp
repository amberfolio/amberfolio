// SPDX-License-Identifier: AGPL-3.0-only
//
// The question this host asks before it writes into somebody's game
// directory (#385).
//
// Two claims, and the second is the one with teeth. That a launch with a
// person in it asks is the feature; that a **driven** launch never does
// is the thing the session library depends on — a prompt in a sweep run
// hangs it, and a sidecar written by a verification run makes the disk
// every recorded session pins a different disk, which `scripts/sweep.py`
// answers by skipping rather than failing.

#include "sidecar_consent.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string_view>

namespace amberfolio::sdl {
namespace {

/// A launch with a person in it and nothing remembered: the one shape
/// that asks. Each case below turns exactly one thing off it.
[[nodiscard]] sidecar_run a_person_at_the_keyboard() {
  return sidecar_run{.named_on_the_command_line = false,
                     .config_answered = false,
                     .can_remember = true,
                     .driven = false,
                     .have_game = true};
}

TEST(SidecarConsent, APersonWithNothingRememberedIsAsked) {
  EXPECT_EQ(should_ask_about_sidecars(a_person_at_the_keyboard()),
            sidecar_ask::ask);
}

TEST(SidecarConsent, AFlagIsAnAnswerAndIsNotAsked) {
  sidecar_run run = a_person_at_the_keyboard();
  run.named_on_the_command_line = true;
  EXPECT_EQ(should_ask_about_sidecars(run),
            sidecar_ask::said_on_the_command_line);
}

TEST(SidecarConsent, AnAnswerAlreadyInTheConfigIsTheOnceInAskedOnce) {
  sidecar_run run = a_person_at_the_keyboard();
  run.config_answered = true;
  EXPECT_EQ(should_ask_about_sidecars(run), sidecar_ask::already_answered);
}

TEST(SidecarConsent, ADrivenRunIsNeverAskedAnything) {
  // The one that matters. A headless run, a `--press` script, a
  // `--record`, a `--dump` and a `--verify` all arrive here as this
  // single bool, and every one of them has to come back the same way:
  // nobody is asked, so the sidecars stay off and no disk changes under
  // a recording.
  sidecar_run run = a_person_at_the_keyboard();
  run.driven = true;
  EXPECT_EQ(should_ask_about_sidecars(run), sidecar_ask::nobody_is_asked);

  // And still not asked when there is nowhere to remember an answer and
  // no game either: a driven run is left alone whatever else is true of
  // it, which is what makes the guard one condition to reason about
  // rather than three.
  run.can_remember = false;
  run.have_game = false;
  EXPECT_EQ(should_ask_about_sidecars(run), sidecar_ask::nobody_is_asked);
}

TEST(SidecarConsent, NowhereToRememberIsNotAsked) {
  // A question whose answer cannot be written down is a question asked
  // again every launch, which is not asking once. `--no-config`, a
  // platform with no per-user directory, and a config file this build
  // refused to read all arrive here.
  sidecar_run run = a_person_at_the_keyboard();
  run.can_remember = false;
  EXPECT_EQ(should_ask_about_sidecars(run), sidecar_ask::nowhere_to_remember);
}

TEST(SidecarConsent, AFirstRunIsAskedWhatItNeedsInsteadOfThis) {
  sidecar_run run = a_person_at_the_keyboard();
  run.have_game = false;
  EXPECT_EQ(should_ask_about_sidecars(run),
            sidecar_ask::nothing_to_write_beside);
}

TEST(SidecarConsent, YesAndNoInAnyCaseAndWithAnyWhitespace) {
  EXPECT_EQ(read_sidecar_answer("y"), std::optional<bool>{true});
  EXPECT_EQ(read_sidecar_answer("Y\r\n"), std::optional<bool>{true});
  EXPECT_EQ(read_sidecar_answer("  yes  "), std::optional<bool>{true});
  EXPECT_EQ(read_sidecar_answer("YES"), std::optional<bool>{true});
  EXPECT_EQ(read_sidecar_answer("n"), std::optional<bool>{false});
  EXPECT_EQ(read_sidecar_answer("No\r"), std::optional<bool>{false});
}

TEST(SidecarConsent, SilenceIsNeitherAYesNorANo) {
  // The whole of "log, don't fake" applied to a prompt. An empty line is
  // what a closed stdin gives, and reading it as consent would put a
  // file in somebody's directory on the strength of a keypress that
  // never happened — while reading it as a refusal would write down an
  // answer nobody gave and stop the question ever being asked again.
  EXPECT_EQ(read_sidecar_answer(""), std::nullopt);
  EXPECT_EQ(read_sidecar_answer("   "), std::nullopt);
  EXPECT_EQ(read_sidecar_answer("maybe"), std::nullopt);
  EXPECT_EQ(read_sidecar_answer("yep"), std::nullopt);
  EXPECT_EQ(read_sidecar_answer("y please"), std::nullopt);
}

TEST(SidecarConsent, TheQuestionNamesTheFilesAndWhereTheyLand) {
  // A player can only delete a file they have been told the name of, and
  // "beside your saves" is not a name. The last line is the other half
  // of #385's second trap: with the store on and both seams off, nothing
  // appears at all — which is true because `slot_store` writes no empty
  // sidecar into a directory that has none, and this is the sentence
  // that has to stay in step with it.
  std::string whole;
  for (const std::string_view line : sidecar_question()) {
    whole += line;
    whole += '\n';
  }
  EXPECT_THAT(whole, ::testing::HasSubstr("\\SAVE\\AFMAP.DAT"));
  EXPECT_THAT(whole, ::testing::HasSubstr("\\SAVE\\AFSEEN.DAT"));
  EXPECT_THAT(whole, ::testing::HasSubstr("your game directory"));
  EXPECT_THAT(whole, ::testing::HasSubstr("until there is something to put"));
  EXPECT_THAT(std::string(sidecar_prompt()), ::testing::HasSubstr("[y/n]"));
}

}  // namespace
}  // namespace amberfolio::sdl
