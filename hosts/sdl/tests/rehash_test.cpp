// SPDX-License-Identifier: AGPL-3.0-only
//
// rehash.h: `--rehash`'s text (#404). A re-hashed recording differs from
// the one it was made from in the header's state version and the
// checkpoint lines, and nowhere else; and a run whose checkpoints do not
// pair off with the recording's writes nothing.

#include "rehash.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/state.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

constexpr std::string_view recording =
    "amberfolio-recording 3 state=0\n"
    "program A.EXE 00\n"
    "key 10 1e down\n"
    "checkpoint 10 2 aa\n"
    "checkpoint 20 5 bb\n"
    "end 20 5\n";

TEST(Rehash, OnlyTheHeaderAndTheCheckpointsChange) {
  std::string_view why;
  const std::optional<std::string> out = rehash_text(
      recording, {"checkpoint 10 2 cc\n", "checkpoint 20 5 dd\n"}, why);
  ASSERT_TRUE(out.has_value()) << why;
  EXPECT_EQ(*out, "amberfolio-recording 3 state=" +
                      std::to_string(machine::state_format_version) +
                      "\n"
                      "program A.EXE 00\n"
                      "key 10 1e down\n"
                      "checkpoint 10 2 cc\n"
                      "checkpoint 20 5 dd\n"
                      "end 20 5\n");
}

TEST(Rehash, CheckpointsThatDoNotPairOffAreRefused) {
  std::string_view why;
  EXPECT_FALSE(rehash_text(recording, {"checkpoint 10 2 cc\n"}, why));
  EXPECT_EQ(why, "more checkpoints than were taken");

  const std::vector<std::string> three(3, "checkpoint 1 1 ee\n");
  EXPECT_FALSE(rehash_text(recording, three, why));
  EXPECT_EQ(why, "fewer checkpoints than were taken");
}

TEST(Rehash, AFileWithNoHeaderIsRefused) {
  std::string_view why;
  EXPECT_FALSE(rehash_text("program A.EXE 00\n", {}, why));
  EXPECT_EQ(why, "no header to rewrite");
}

}  // namespace
}  // namespace amberfolio::sdl
