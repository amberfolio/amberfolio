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

TEST(ProgramProfile, SecretsIsThePartItsSeamsAndItsReadsUse) {
  const program_profile& secret = secret_of_the_silver_blades;
  EXPECT_NE(secret.dgroup_paragraphs, 0U);
  EXPECT_NE(secret.data_game_mode, 0U);
  EXPECT_NE(secret.data_roster_head, 0U);
  EXPECT_TRUE(secret.combat_overlay.has_load_segment());
  EXPECT_LT(secret.record_next, secret.record_status);
  EXPECT_LT(secret.record_side, secret.record_hit_points);
  // The screen text's (screen_text.h): the far pointer to the font.
  EXPECT_NE(secret.data_font_pointer, 0U);
  // The message pause's (seam_message_pause.cpp, #609): the runtime's key
  // read, the interpreter's module and the death routine's word.
  EXPECT_NE(secret.key_read_entry, 0U);
  EXPECT_TRUE(secret.interpreter_overlay.has_load_segment());
  EXPECT_NE(secret.death_load_segment_at, 0U);
  // The scene's (scene.h, #609): the current member, the area record and
  // its geometry block, the disk and area numbers, the party's square and
  // facing, the loaded map, the wall sets and the transition flag; and the
  // aim overlay its aim is watched in.
  EXPECT_NE(secret.data_current_member, 0U);
  EXPECT_NE(secret.data_area_record, 0U);
  EXPECT_NE(secret.area_geometry_block, 0U);
  EXPECT_NE(secret.data_disk_number, 0U);
  EXPECT_NE(secret.data_area_id, 0U);
  EXPECT_EQ(secret.data_party_y, secret.data_party_x + 1U);
  EXPECT_EQ(secret.data_party_facing, secret.data_party_x + 2U);
  EXPECT_NE(secret.data_map_pointer, 0U);
  EXPECT_NE(secret.data_wall_set, 0U);
  EXPECT_NE(secret.data_in_transition, 0U);
  EXPECT_TRUE(secret.aim_overlay.has_load_segment());
  // The scene's and the automap's both (#609): the square, the row and
  // the turns an area's script gives the status line, three words of the
  // area record in a row. The first two programs' lines print the stored
  // square, and they have none.
  EXPECT_NE(secret.area_shown_column, 0U);
  EXPECT_EQ(secret.area_shown_row, secret.area_shown_column + 2U);
  EXPECT_EQ(secret.area_shown_turn, secret.area_shown_column + 4U);
  EXPECT_EQ(pool_of_radiance.area_shown_column, 0U);
  EXPECT_EQ(curse_of_the_azure_bonds.area_shown_column, 0U);
  // The hero keys' (seam_hero_keys.cpp, #609): the menu-bar routine's
  // module and its read call, the pushback slot, the roster drawer's
  // module and the party menu's picker's.
  EXPECT_TRUE(secret.menu_overlay.has_load_segment());
  EXPECT_NE(secret.menu_key_read_call, 0U);
  EXPECT_NE(secret.data_key_pushback, 0U);
  EXPECT_TRUE(secret.summary_overlay.has_load_segment());
  EXPECT_TRUE(secret.view_overlay.has_load_segment());
  // No view kind, as on Curse: the adventuring mode has one view.
  EXPECT_EQ(secret.data_view_kind, 0U);
  // The automap's (seam_automap.cpp, #609): the key poll and its pushback
  // slot, the region and screen clears, the menu-bar thunk, the adventure
  // loop's module and its call into the bar, and the roster drawer's
  // overlay and the stub entry it is called through. No bar in the data
  // segment: the loop's bar is a copy on the stack, as everybody's is.
  EXPECT_NE(secret.key_pending_entry, 0U);
  EXPECT_NE(secret.data_key_pushback, 0U);
  EXPECT_NE(secret.clear_region_entry, 0U);
  EXPECT_NE(secret.clear_screen_entry, 0U);
  EXPECT_NE(secret.command_bar_entry, 0U);
  EXPECT_TRUE(secret.adventure_overlay.has_load_segment());
  EXPECT_EQ(secret.view_bar_return, secret.view_bar_call + 5U);
  EXPECT_TRUE(secret.summary_overlay.has_load_segment());
  EXPECT_NE(secret.screen_paragraph, 0U);
  EXPECT_NE(secret.roster_draw_offset, 0U);
  EXPECT_EQ(secret.data_view_bar, 0U);
  // The journal reader's (seam_journal.cpp, #609): the move mode's bar call,
  // the menu-bar routine's module and its own copy of the bar, the bar's
  // highlight, the string drawer, the composer's stub entry and the colour
  // a selection is drawn in.
  EXPECT_NE(secret.move_bar_return, 0U);
  EXPECT_NE(secret.move_bar_return, secret.view_bar_return);
  EXPECT_TRUE(secret.menu_overlay.has_load_segment());
  EXPECT_NE(secret.menu_local_bar, 0U);
  EXPECT_EQ(secret.menu_bar_capacity, 40U);
  EXPECT_NE(secret.data_bar_highlight, 0U);
  EXPECT_NE(secret.draw_string_entry, 0U);
  EXPECT_NE(secret.screen_composer_offset, 0U);
  EXPECT_NE(secret.selection_colour, 0U);
  // And nothing pretending to be a fact: what no seam or read available
  // on this program uses is zero until somebody measures it.
  EXPECT_EQ(secret.draw_frame_entry, 0U);
  EXPECT_FALSE(secret.camp_overlay.has_load_segment());
  EXPECT_FALSE(secret.roster_overlay.has_load_segment());
  // The move mode's bar is this program's alone.
  EXPECT_EQ(pool_of_radiance.move_bar_return, 0U);
  EXPECT_EQ(curse_of_the_azure_bonds.move_bar_return, 0U);
}

TEST(ProgramProfile, TheBaselineAndCurseHaveOne) {
  ASSERT_EQ(program_profiles().size(), 3U);
  EXPECT_EQ(program_profiles().front(), &pool_of_radiance);
  EXPECT_EQ(program_profiles()[1], &curse_of_the_azure_bonds);
  EXPECT_EQ(program_profiles().back(), &secret_of_the_silver_blades);
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
  // And the roster drawer's (#555): overlaid, called at its stub entry,
  // with the character sheet and the save slots named as modules.
  EXPECT_TRUE(curse_of_the_azure_bonds.summary_overlay.has_load_segment());
  EXPECT_TRUE(pool_of_radiance.summary_overlay.is_resident_image());
  EXPECT_NE(curse_of_the_azure_bonds.screen_paragraph, 0U);
  EXPECT_NE(curse_of_the_azure_bonds.roster_draw_offset, 0U);
  EXPECT_TRUE(curse_of_the_azure_bonds.view_overlay.has_load_segment());
  EXPECT_TRUE(curse_of_the_azure_bonds.slots_overlay.has_load_segment());
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
