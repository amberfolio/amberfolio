// SPDX-License-Identifier: AGPL-3.0-only
//
// The save-layer table (save_layer.h, #208): the layer is found by the
// program's fingerprint the way a seam's addresses are, every row is
// well formed, and a path is read against it — the slot letter and the
// member index out of the name, a game file answering no row at all.
//
// What no test here does is claim the table is *right*. That is a fact
// about a program nothing in this repository may run (CLAUDE.md), and it
// was gathered by watching a real copy write its files; `docs/hosts.md`
// §6 has those runs. What is testable here is that the table is
// self-consistent and that the matcher reads it the way the patterns are
// written.

#include "amberfolio/machine/save_layer.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// The baseline's digest, read out of the edition table rather than
/// pasted: what is under test is the lookup, not the value.
[[nodiscard]] sha256_digest baseline_digest() {
  const std::span<const edition> editions = known_editions();
  EXPECT_FALSE(editions.empty());
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(editions.front().fingerprint, digest));
  return digest;
}

[[nodiscard]] const save_layer& baseline() {
  const save_layer* layer = save_layer_for(baseline_digest());
  EXPECT_NE(layer, nullptr);
  return *layer;
}

/// A path as a host would hand one over, through the one canonicalizer
/// (#146) so a test cannot spell a name the machine could not.
[[nodiscard]] dos_path path_of(std::string_view text) {
  const vfs_result<dos_path> where =
      canonicalize_host_path({text.data(), text.size()});
  EXPECT_TRUE(where.ok()) << "not a path: " << text;
  return where.ok() ? where.value : dos_path{};
}

[[nodiscard]] save_layer_row match(std::string_view text) {
  return match_save_file(baseline(), path_of(text));
}

// --- Finding a layer -----------------------------------------------------

TEST(SaveLayer, IsKeyedOnTheProgramTheWayASeamIs) {
  const save_layer* layer = save_layer_for(baseline_digest());
  ASSERT_NE(layer, nullptr);
  EXPECT_EQ(layer->fingerprint, known_editions().front().fingerprint);
  EXPECT_FALSE(layer->files.empty());
  EXPECT_FALSE(layer->slots.empty());
  EXPECT_GT(layer->members, 0);
}

TEST(SaveLayer, AnswersNullForAProgramItHasNoTableFor) {
  sha256_digest digest = baseline_digest();
  digest.bytes[31] = static_cast<std::uint8_t>(digest.bytes[31] ^ 0x01U);

  EXPECT_EQ(save_layer_for(digest), nullptr);
  EXPECT_EQ(save_layer_for(sha256_digest{}), nullptr);
}

// --- The rows ------------------------------------------------------------

TEST(SaveLayer, EveryRowIsWellFormed) {
  for (const save_file& row : baseline().files) {
    EXPECT_FALSE(row.pattern.empty());
    EXPECT_FALSE(row.about.empty());
    EXPECT_NE(save_file_kind_name(row.kind), nullptr);
    // A row that is not part of a slot cannot be required by one.
    if (row.pattern.find("<S>") == std::string_view::npos) {
      EXPECT_FALSE(row.required) << row.pattern;
    }
  }
}

TEST(SaveLayer, EveryPatternIsAPathOnceItsPlaceholdersAreFilledIn) {
  const save_layer& layer = baseline();
  for (const save_file& row : layer.files) {
    std::string spelled(row.pattern);
    for (std::string::size_type at = spelled.find("<S>");
         at != std::string::npos; at = spelled.find("<S>")) {
      spelled.replace(at, 3, 1, layer.slots.front());
    }
    for (std::string::size_type at = spelled.find("<N>");
         at != std::string::npos; at = spelled.find("<N>")) {
      spelled.replace(at, 3, 1, '1');
    }
    for (std::string::size_type at = spelled.find("<NAME>");
         at != std::string::npos; at = spelled.find("<NAME>")) {
      spelled.replace(at, 6, "EVOKER");
    }
    const vfs_result<dos_path> where =
        canonicalize_host_path({spelled.data(), spelled.size()});
    EXPECT_TRUE(where.ok()) << row.pattern << " spells " << spelled;
  }
}

TEST(SaveLayer, KindsHaveTheirOwnSpellings) {
  EXPECT_STREQ(save_file_kind_name(save_file_kind::slot), "slot");
  EXPECT_STREQ(save_file_kind_name(save_file_kind::member), "member");
  EXPECT_STREQ(save_file_kind_name(save_file_kind::roster), "roster");
  EXPECT_STREQ(save_file_kind_name(save_file_kind::character), "character");
  EXPECT_STREQ(save_file_kind_name(save_file_kind::config), "config");
  EXPECT_STREQ(save_file_kind_name(save_file_kind::sidecar), "sidecar");
}

// --- Reading a path against it -------------------------------------------

TEST(SaveLayerMatch, ReadsTheSlotLetterOutOfASlotFile) {
  const save_layer_row found = match("SAVE\\SAVGAMA.DAT");
  ASSERT_NE(found.file, nullptr);
  EXPECT_EQ(found.file->kind, save_file_kind::slot);
  EXPECT_TRUE(found.file->required);
  EXPECT_EQ(found.slot, 'A');
  EXPECT_EQ(found.member, 0);
}

TEST(SaveLayerMatch, ReadsTheSlotAndTheMemberOutOfARecord) {
  const save_layer_row record = match("SAVE\\CHRDATD6.SAV");
  ASSERT_NE(record.file, nullptr);
  EXPECT_EQ(record.file->kind, save_file_kind::member);
  EXPECT_TRUE(record.file->required);
  EXPECT_EQ(record.slot, 'D');
  EXPECT_EQ(record.member, 6);

  // What that member carries and what they have memorized are the same
  // slot and the same member, and neither is required: a member with
  // nothing to write has no such file (save_layer.h).
  for (const std::string_view also :
       {"SAVE\\CHRDATD6.ITM", "SAVE\\CHRDATD6.SPC"}) {
    const save_layer_row row = match(also);
    ASSERT_NE(row.file, nullptr) << also;
    EXPECT_EQ(row.file->kind, save_file_kind::member) << also;
    EXPECT_FALSE(row.file->required) << also;
    EXPECT_EQ(row.slot, 'D') << also;
    EXPECT_EQ(row.member, 6) << also;
  }
}

TEST(SaveLayerMatch, TakesAHostSpellingThroughTheOneCanonicalizer) {
  const save_layer_row slashes = match("save/savgama.dat");
  ASSERT_NE(slashes.file, nullptr);
  EXPECT_EQ(slashes.slot, 'A');
  EXPECT_EQ(slashes.index, match("SAVE\\SAVGAMA.DAT").index);
}

TEST(SaveLayerMatch, RefusesALetterTheProgramNeverAsksAbout) {
  // The slot letters stop where the program's own probe stops. A file
  // one letter past the end is not a slot, and claiming it for one would
  // be the heuristic this table exists to replace.
  ASSERT_EQ(baseline().slots, "ABCDEFGHIJ");
  const save_layer_row past = match("SAVE\\SAVGAMK.DAT");
  EXPECT_EQ(past.file, nullptr);
  EXPECT_EQ(past.slot, 0);
}

TEST(SaveLayerMatch, RefusesAMemberPastTheLargestASlotNames) {
  ASSERT_EQ(baseline().members, 8);
  const save_layer_row eighth = match("SAVE\\CHRDATE8.SAV");
  ASSERT_NE(eighth.file, nullptr);
  EXPECT_EQ(eighth.member, 8);

  // A ninth record, left by nothing this program writes, is not part of
  // the slot — the file may be on the disk, and the program never reads
  // it.
  const save_layer_row ninth = match("SAVE\\CHRDATE9.SAV");
  EXPECT_EQ(ninth.file, nullptr);
  EXPECT_EQ(ninth.member, 0);
}

TEST(SaveLayerMatch, PutsTheRosterAndTheCharactersInNoSlot) {
  const save_layer_row roster = match("SAVE\\CHARLIST.TXT");
  ASSERT_NE(roster.file, nullptr);
  EXPECT_EQ(roster.file->kind, save_file_kind::roster);
  EXPECT_EQ(roster.slot, 0);

  const save_layer_row named = match("SAVE\\EVOKER.CHA");
  ASSERT_NE(named.file, nullptr);
  EXPECT_EQ(named.file->kind, save_file_kind::character);
  EXPECT_EQ(named.slot, 0);
  EXPECT_EQ(named.member, 0);
}

TEST(SaveLayerMatch, PrefersTheMemberRowOverTheCharacterOne) {
  // `<NAME>.ITM` would match a member's own items file. The table runs
  // most specific first and the first match wins, which is the whole
  // reason the three `<NAME>` rows are last (save_layer.h).
  const save_layer_row record = match("SAVE\\CHRDATA1.ITM");
  ASSERT_NE(record.file, nullptr);
  EXPECT_EQ(record.file->kind, save_file_kind::member);
  EXPECT_EQ(record.slot, 'A');
}

TEST(SaveLayerMatch, KnowsThisBuildsOwnSidecars) {
  const save_layer_row working = match(save_layer_automap_working);
  ASSERT_NE(working.file, nullptr);
  EXPECT_EQ(working.file->kind, save_file_kind::sidecar);
  EXPECT_EQ(working.slot, 0);

  const save_layer_row log = match(save_layer_journal_working);
  ASSERT_NE(log.file, nullptr);
  EXPECT_EQ(log.file->kind, save_file_kind::sidecar);
  EXPECT_EQ(log.slot, 0);

  const save_layer_row snapshot = match("SAVE\\AFMAPB.DAT");
  ASSERT_NE(snapshot.file, nullptr);
  EXPECT_EQ(snapshot.file->kind, save_file_kind::sidecar);
  EXPECT_EQ(snapshot.slot, 'B');
}

TEST(SaveLayerMatch, SaysTheConfigurationFileIsTheProgramsAndNotAPlayers) {
  const save_layer_row config = match("POOL.CFG");
  ASSERT_NE(config.file, nullptr);
  EXPECT_EQ(config.file->kind, save_file_kind::config);
  EXPECT_EQ(config.slot, 0);
}

TEST(SaveLayerMatch, AnswersNoRowForAGameFile) {
  for (const std::string_view outside :
       {"START.EXE", "GAME.OVR", "8X8D1.DAX", "SAVE\\NOTOURS.XYZ"}) {
    const save_layer_row row = match(outside);
    EXPECT_EQ(row.file, nullptr) << outside;
    EXPECT_EQ(row.slot, 0) << outside;
    EXPECT_EQ(row.member, 0) << outside;
  }
}

TEST(SaveLayerMatch, AnswersNoRowForTheRoot) {
  EXPECT_EQ(match_save_file(baseline(), dos_path{}).file, nullptr);
}

}  // namespace
}  // namespace amberfolio::machine
