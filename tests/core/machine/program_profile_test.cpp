// SPDX-License-Identifier: AGPL-3.0-only
//
// The program profiles (program_profile.h, #552): a profile is found by
// its program's digest and by nothing else, each is an edition this build
// knows, and every seam is available only on programs that have one.

#include "amberfolio/machine/program_profile.h"

#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] bool has_profile(std::string_view fingerprint) {
  for (const program_profile* known : program_profiles()) {
    if (known->fingerprint == fingerprint) {
      return true;
    }
  }
  return false;
}

TEST(ProgramProfile, IsFoundByItsProgramsDigest) {
  ASSERT_FALSE(program_profiles().empty());
  for (const program_profile* known : program_profiles()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(known->fingerprint, digest)) << known->name;
    EXPECT_EQ(find_program_profile(digest), known) << known->name;
  }
}

TEST(ProgramProfile, AnswersNullForAProgramWithoutOne) {
  EXPECT_EQ(find_program_profile(sha256_digest{}), nullptr);
}

TEST(ProgramProfile, IsAnEditionThisBuildKnows) {
  for (const program_profile* known : program_profiles()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(known->fingerprint, digest));
    EXPECT_NE(find_edition(digest), nullptr) << known->name;
  }
}

TEST(ProgramProfile, TheBaselineAndCurseHaveOne) {
  ASSERT_EQ(program_profiles().size(), 2U);
  EXPECT_EQ(program_profiles().front(), &pool_of_radiance);
  EXPECT_EQ(program_profiles().back(), &curse_of_the_azure_bonds);
  EXPECT_EQ(pool_of_radiance.dgroup_offset(),
            std::uint32_t{pool_of_radiance.dgroup_paragraphs} * 16U);
  // The facts the cheats and the host reads use on Curse (#554, #555), and
  // nothing pretending to be one: a member nobody has measured there is
  // zero, and the view kind is zero because the program has none.
  EXPECT_NE(curse_of_the_azure_bonds.dgroup_paragraphs, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.data_game_mode, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.data_roster_head, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.data_font_pointer, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.data_map_pointer, 0U);
  EXPECT_TRUE(curse_of_the_azure_bonds.combat_overlay.has_load_segment());
  EXPECT_EQ(curse_of_the_azure_bonds.data_view_kind, 0U);
  // The message pause's (#555): the runtime's key read, the interpreter
  // overlay and the death routine's load-segment word.
  EXPECT_NE(curse_of_the_azure_bonds.key_read_entry, 0U);
  EXPECT_TRUE(curse_of_the_azure_bonds.interpreter_overlay.has_load_segment());
  EXPECT_NE(curse_of_the_azure_bonds.death_load_segment_at, 0U);
  // modern-controls' (#555): the pushback slot its keys are written to.
  EXPECT_NE(curse_of_the_azure_bonds.data_key_pushback, 0U);
  EXPECT_EQ(curse_of_the_azure_bonds.roster_draw_offset, 0U);
  // The automap's (#555): the keyboard poll, the region and screen clears,
  // the menu-bar thunk, and the one adventuring bar and its call's return;
  // the overhead view's bar is the same one, so its members stay zero.
  EXPECT_NE(curse_of_the_azure_bonds.key_pending_entry, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.clear_region_entry, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.clear_screen_entry, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.command_bar_entry, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.data_view_bar, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.view_bar_return, 0U);
  EXPECT_EQ(curse_of_the_azure_bonds.data_area_bar, 0U);
  EXPECT_EQ(curse_of_the_azure_bonds.area_bar_return, 0U);
}

TEST(ProgramProfile, EverySeamIsAvailableOnlyOnProgramsWithAProfile) {
  for (const std::span<const seam_definition> seams :
       {all_seams(), built_in_fixes()}) {
    for (const seam_definition& seam : seams) {
      EXPECT_FALSE(seam.fingerprints.empty()) << seam.id;
      for (const std::string_view fingerprint : seam.fingerprints) {
        EXPECT_TRUE(has_profile(fingerprint)) << seam.id;
      }
    }
  }
}

}  // namespace
}  // namespace amberfolio::machine
