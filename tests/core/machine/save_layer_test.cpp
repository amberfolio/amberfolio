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

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/memory_vfs.h"
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

/// The archive release's layout: started at the root, saving in `\SAVE`.
[[nodiscard]] save_layer_places at_root() {
  return {.save_directory = path_of("SAVE"), .current_directory = {}};
}

/// A copy started in `\POOLRAD` that saves beside its game files.
[[nodiscard]] save_layer_places beside() {
  return {.save_directory = path_of("POOLRAD"),
          .current_directory = path_of("POOLRAD")};
}

/// A copy started in `\POOLRAD` that saves one directory further down.
[[nodiscard]] save_layer_places below() {
  return {.save_directory = path_of("POOLRAD\\SAVE"),
          .current_directory = path_of("POOLRAD")};
}

[[nodiscard]] save_layer_row match(
    std::string_view text, const save_layer_places& places = at_root()) {
  return match_save_file(baseline(), places, path_of(text));
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
  const save_layer_row working =
      match("SAVE\\" + std::string(save_layer_automap_working));
  ASSERT_NE(working.file, nullptr);
  EXPECT_EQ(working.file->kind, save_file_kind::sidecar);
  EXPECT_EQ(working.slot, 0);

  const save_layer_row log =
      match("SAVE\\" + std::string(save_layer_journal_working));
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
  EXPECT_EQ(match_save_file(baseline(), at_root(), dos_path{}).file, nullptr);
}

// --- Where the rows are (#397) -------------------------------------------
//
// One table, three layouts: the archive release at the root saving in
// `\SAVE`, and two copies started in `\POOLRAD` — one saving beside its
// game files, one in `\POOLRAD\SAVE`.

TEST(SaveLayerPlaces, ClaimsASlotWhereverTheCopySavesAndNowhereElse) {
  const save_layer_row gog = match("POOLRAD\\SAVGAMA.DAT", beside());
  ASSERT_NE(gog.file, nullptr);
  EXPECT_EQ(gog.file->kind, save_file_kind::slot);
  EXPECT_EQ(gog.slot, 'A');

  const save_layer_row steam = match("POOLRAD\\SAVE\\CHRDATJ6.SPC", below());
  ASSERT_NE(steam.file, nullptr);
  EXPECT_EQ(steam.file->kind, save_file_kind::member);
  EXPECT_EQ(steam.slot, 'J');
  EXPECT_EQ(steam.member, 6);

  // The same name one directory off is not the copy's save.
  EXPECT_EQ(match("SAVE\\SAVGAMA.DAT", beside()).file, nullptr);
  EXPECT_EQ(match("POOLRAD\\SAVGAMA.DAT", below()).file, nullptr);
  EXPECT_EQ(match("POOLRAD\\SAVGAMA.DAT", at_root()).file, nullptr);
  EXPECT_EQ(match("SAVGAMA.DAT", at_root()).file, nullptr);
}

TEST(SaveLayerPlaces, PutsTheSidecarsBesideTheSaves) {
  for (const save_layer_places& places : {at_root(), beside(), below()}) {
    std::array<char, save_pattern_capacity> directory{};
    const save_file sidecar{.pattern = "AFMAPB.DAT", .about = "a snapshot"};
    ASSERT_GT(spell_save_pattern(sidecar, places, directory), 0U);
    const save_layer_row row = match(directory.data(), places);
    ASSERT_NE(row.file, nullptr) << directory.data();
    EXPECT_EQ(row.file->kind, save_file_kind::sidecar) << directory.data();
    EXPECT_EQ(row.slot, 'B') << directory.data();
  }
}

TEST(SaveLayerPlaces, TheConfigurationFileIsInTheStartingDirectory) {
  EXPECT_EQ(match("POOL.CFG", at_root()).file->kind, save_file_kind::config);
  EXPECT_EQ(match("POOLRAD\\POOL.CFG", below()).file->kind,
            save_file_kind::config);
  EXPECT_EQ(match("POOL.CFG", below()).file, nullptr);
  EXPECT_EQ(match("POOLRAD\\SAVE\\POOL.CFG", below()).file, nullptr);
}

TEST(SaveLayerPlaces, SavingBesideTheGameClaimsNoGameFile) {
  // The ordering trap: with the save directory and the starting directory
  // one and the same, the configuration file is in both, and it is the
  // program's — never a playthrough's `<NAME>` row. The rest of the
  // publisher's files are checked against every edition's file list in
  // `hosts/common/tests/edition_facts_test.cpp`; these are the names a
  // `<NAME>` row would be likeliest to swallow.
  const save_layer_row config = match("POOLRAD\\POOL.CFG", beside());
  ASSERT_NE(config.file, nullptr);
  EXPECT_EQ(config.file->kind, save_file_kind::config);

  for (const std::string_view game :
       {"POOLRAD\\START.EXE", "POOLRAD\\GAME.OVR", "POOLRAD\\ITEMS",
        "POOLRAD\\CFG.EXE", "POOLRAD\\ITEM1.DAX", "POOLRAD\\MON1ITM.DAX",
        "POOLRAD\\MON2SPC.DAX", "POOLRAD\\MON1CHA.DAX"}) {
    EXPECT_EQ(match(game, beside()).file, nullptr) << game;
  }
}

TEST(SaveLayerPlaces, SpellsEachPatternWithItsDirectory) {
  const save_layer& layer = baseline();
  std::array<char, save_pattern_capacity> out{};
  // The archive release spells every row the way the table always did.
  ASSERT_GT(spell_save_pattern(layer.files.front(), at_root(), out), 0U);
  EXPECT_STREQ(out.data(), "SAVE\\SAVGAM<S>.DAT");
  ASSERT_GT(spell_save_pattern(layer.files.front(), below(), out), 0U);
  EXPECT_STREQ(out.data(), "POOLRAD\\SAVE\\SAVGAM<S>.DAT");

  for (const save_file& row : layer.files) {
    if (row.kind != save_file_kind::config) {
      continue;
    }
    ASSERT_GT(spell_save_pattern(row, at_root(), out), 0U);
    EXPECT_STREQ(out.data(), "POOL.CFG");
    ASSERT_GT(spell_save_pattern(row, beside(), out), 0U);
    EXPECT_STREQ(out.data(), "POOLRAD\\POOL.CFG");
  }

  // Too small is nothing, and says so.
  std::array<char, 4> tiny{};
  EXPECT_EQ(spell_save_pattern(layer.files.front(), at_root(), tiny), 0U);
}

// --- Where the program saves: its own configuration file (#397) -----------
//
// Written here, four lines of layout, because the file the program reads
// is not ours to copy: these say where to save and nothing else.

class SaveDirectory : public ::testing::Test {
 protected:
  void put(std::string_view path, std::string_view text) {
    const dos_path where = path_of(path);
    if (!where.parent().is_root() && !fs.exists(where.parent())) {
      ASSERT_EQ(fs.mkdir(where.parent()), vfs_error::none);
    }
    const vfs_result<file_handle> made = fs.create(where);
    ASSERT_TRUE(made.ok());
    const std::span<const std::uint8_t> bytes(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    ASSERT_TRUE(fs.write(made.value, bytes).ok());
    ASSERT_EQ(fs.close(made.value), vfs_error::none);
  }

  [[nodiscard]] save_directory_answer read(std::string_view from = "") {
    return read_save_directory(fs, from.empty() ? dos_path{} : path_of(from));
  }

  memory_filesystem fs;
};

TEST_F(SaveDirectory, ReadsTheFourthLineAgainstTheStartingDirectory) {
  put("POOL.CFG", "a\r\nb\r\nC:\\\r\nC:\\SAVE\\\r\ne\r\n");
  const save_directory_answer root = read();
  ASSERT_TRUE(root.ok());
  EXPECT_EQ(root.directory, path_of("SAVE"));

  put("POOLRAD\\POOL.CFG", "a\r\nb\r\nC:\\POOLRAD\\\r\nC:\\POOLRAD\\\r\ne\r\n");
  const save_directory_answer gog = read("POOLRAD");
  ASSERT_TRUE(gog.ok());
  EXPECT_EQ(gog.directory, path_of("POOLRAD"));
}

TEST_F(SaveDirectory, ARelativeLineIsRelativeToTheStartingDirectory) {
  // As the program's own open of it would be: no drive, no leading
  // separator, and the directory it started in is where it lands.
  put("POOLRAD\\POOL.CFG", "a\nb\nc\nSAVE\\\n");
  const save_directory_answer answer = read("POOLRAD");
  ASSERT_TRUE(answer.ok());
  EXPECT_EQ(answer.directory, path_of("POOLRAD\\SAVE"));
}

TEST_F(SaveDirectory, TakesALastLineWithNoEnding) {
  put("POOL.CFG", "a\r\nb\r\nc\r\nC:\\POOLRAD\\SAVE");
  const save_directory_answer answer = read();
  ASSERT_TRUE(answer.ok());
  EXPECT_EQ(answer.directory, path_of("POOLRAD\\SAVE"));
}

TEST_F(SaveDirectory, SaysWhenThereIsNoFile) {
  const save_directory_answer none = read();
  EXPECT_FALSE(none.ok());
  EXPECT_EQ(none.trouble, save_directory_trouble::no_config);

  // A configuration file somewhere else is not the one the program opens.
  put("POOLRAD\\POOL.CFG", "a\nb\nc\nC:\\SAVE\\\n");
  EXPECT_EQ(read().trouble, save_directory_trouble::no_config);
}

TEST_F(SaveDirectory, SaysWhenTheFileDoesNotSay) {
  put("POOL.CFG", "a\r\nb\r\nc\r\n");
  EXPECT_EQ(read().trouble, save_directory_trouble::too_short);

  put("POOL.CFG", "a\r\nb\r\nc\r\n\r\ne\r\n");
  EXPECT_EQ(read().trouble, save_directory_trouble::too_short);

  put("POOL.CFG", "");
  EXPECT_EQ(read().trouble, save_directory_trouble::too_short);
}

TEST_F(SaveDirectory, SaysWhenTheLineIsNotAPathThisMachineCanName) {
  put("POOL.CFG", "a\nb\nc\nA:\\SAVE\\\n");
  EXPECT_EQ(read().trouble, save_directory_trouble::not_a_path);

  put("POOL.CFG", "a\nb\nc\nC:\\NINELETTER\\\n");
  EXPECT_EQ(read().trouble, save_directory_trouble::not_a_path);
}

TEST_F(SaveDirectory, TheTroublesHaveTheirOwnSpellings) {
  EXPECT_STREQ(save_directory_trouble_name(save_directory_trouble::none),
               "none");
  EXPECT_STREQ(save_directory_trouble_name(save_directory_trouble::no_config),
               "no-config");
  EXPECT_STREQ(save_directory_trouble_name(save_directory_trouble::unreadable),
               "unreadable");
  EXPECT_STREQ(save_directory_trouble_name(save_directory_trouble::too_short),
               "too-short");
  EXPECT_STREQ(save_directory_trouble_name(save_directory_trouble::not_a_path),
               "not-a-path");
}

TEST_F(SaveDirectory, GivesTheMatcherItsPlaces) {
  put("POOLRAD\\POOL.CFG", "a\r\nb\r\nc\r\nC:\\POOLRAD\\SAVE\\\r\n");
  save_layer_places places;
  ASSERT_TRUE(save_layer_places_of(fs, path_of("POOLRAD"), places));
  EXPECT_EQ(places.save_directory, path_of("POOLRAD\\SAVE"));
  EXPECT_EQ(places.current_directory, path_of("POOLRAD"));

  save_layer_places untouched = at_root();
  EXPECT_FALSE(save_layer_places_of(fs, dos_path{}, untouched));
  EXPECT_EQ(untouched.save_directory, at_root().save_directory);
}

}  // namespace
}  // namespace amberfolio::machine
