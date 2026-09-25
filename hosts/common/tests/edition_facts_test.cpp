// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition requirement table, and the joints it has to keep.
//
// The table is data/editions.json and the C++ under test is compiled
// from it (hosts/common/CMakeLists.txt), so there is nothing here about
// whether the two agree — they are one thing. What is here is everything
// the JSON could still get wrong on its own: an edition core does not
// recognise, a document core does not know, a boot file that is not in
// its own artifact list, a repack file list that has drifted from the
// pristine disk `tests/sessions/party.session` pins, and two releases of
// one program that the match cannot tell apart.

#include "amberfolio/host/edition_facts.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <fstream>
#include <ios>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/save_layer.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;

/// The digest 64 hex characters spell. The table's own spelling is the
/// text one, and everything a host offers is bytes, so a test that
/// crosses the two needs this.
sha256_digest digest_of(std::string_view hex) {
  sha256_digest out;
  EXPECT_TRUE(machine::parse_digest(hex, out)) << hex;
  return out;
}

/// The row with this id. Every case below names the release it is
/// about, because two rows share one program image.
const edition_requirements& row(std::string_view id) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    if (edition.id == id) {
      return edition;
    }
  }
  ADD_FAILURE() << "no row " << id;
  return edition_requirements_table()[0];
}

/// The release sold on GOG and Steam: the baseline, and the first row.
const edition_requirements& store() { return row("por-store"); }
/// The third-party repack the session library was recorded on.
const edition_requirements& repack() { return row("por-archive"); }

/// What a player's POOL.CFG hashes to: whatever their launcher wrote,
/// which is nothing the table knows.
constexpr std::string_view players_config =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

/// A player's whole copy of `edition`, as a host that walked the
/// directory would offer it: every file, and a configuration file of the
/// player's own.
std::vector<offered_file> whole_copy(const edition_requirements& edition) {
  std::vector<offered_file> offered;
  for (const edition_artifact& artifact : edition.artifacts) {
    if (artifact.kind == artifact_kind::file) {
      offered.push_back(
          {.name = artifact.name, .digest = digest_of(artifact.fingerprint)});
    } else if (artifact.kind == artifact_kind::configuration) {
      offered.push_back(
          {.name = artifact.name, .digest = digest_of(players_config)});
    }
  }
  return offered;
}

/// Whether `match` says `name` is missing.
bool misses(const edition_match& match, std::string_view name) {
  for (const std::size_t a : match.missing) {
    if (match.edition->artifacts[a].name == name) {
      return true;
    }
  }
  return false;
}

TEST(EditionFacts, EveryEditionIsOneTheMachineRecognizes) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    sha256_digest digest;
    ASSERT_TRUE(machine::parse_digest(edition.fingerprint, digest))
        << edition.id;
    EXPECT_NE(machine::find_edition(digest), nullptr)
        << edition.id
        << ": the requirements name an edition machine::known_editions() "
           "does not, so a host could render a checklist for a program "
           "this build would then refuse every seam for";
    EXPECT_THAT(edition.name, Not(IsEmpty())) << edition.id;
    EXPECT_THAT(edition.about, Not(IsEmpty())) << edition.id;
  }
}

TEST(EditionFacts, TheStoreReleaseIsTheBaselineAndTheRepackKeepsItsId) {
  const std::span<const edition_requirements> table =
      edition_requirements_table();
  ASSERT_EQ(table.size(), 2U);
  EXPECT_EQ(&table[0], &store());
  // The site keys on ids and the session library pins this disk, so the
  // repack's id is the one it has always had.
  EXPECT_EQ(repack().id, "por-archive");
  // One program image, two releases.
  EXPECT_EQ(store().fingerprint, repack().fingerprint);
  EXPECT_EQ(machine::known_editions().size(), 1U);
}

TEST(EditionFacts, EachRowStatesWhereItIsInstalled) {
  // Where the store's own launcher mounts the copy and changes into it,
  // and the root for the repack, which is where every existing player's
  // copy already is.
  EXPECT_EQ(store().install, "\\POOLRAD");
  EXPECT_EQ(repack().install, "\\");
  for (const edition_requirements& edition : edition_requirements_table()) {
    ASSERT_THAT(edition.install, Not(IsEmpty())) << edition.id;
    EXPECT_EQ(edition.install.front(), '\\') << edition.id;
    EXPECT_EQ(edition.install.find('/'), std::string_view::npos) << edition.id;
  }
}

TEST(EditionFacts, TheBootFileIsAnArtifactAndCarriesTheFingerprint) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    const edition_artifact* boot = nullptr;
    for (const edition_artifact& artifact : edition.artifacts) {
      if (artifact.kind == artifact_kind::file &&
          artifact.name == edition.boot) {
        boot = &artifact;
      }
    }
    ASSERT_NE(boot, nullptr) << edition.id << ": " << edition.boot;
    EXPECT_TRUE(boot->required);
    EXPECT_EQ(boot->fingerprint, edition.fingerprint);
    EXPECT_GT(boot->size, 0U);
  }
}

TEST(EditionFacts, EveryDocumentIsOneTheMachineKnowsAndNoneIsRequired) {
  std::set<std::string_view> documents;
  for (const edition_requirements& edition : edition_requirements_table()) {
    for (const edition_artifact& artifact : edition.artifacts) {
      if (artifact.kind != artifact_kind::document) {
        continue;
      }
      documents.insert(artifact.fingerprint);
      // PLAN.md §2: the binaries are the one artifact nothing runs
      // without. A document that came back required would be this build
      // refusing a copy over a PDF.
      EXPECT_FALSE(artifact.required);
      EXPECT_THAT(artifact.name, IsEmpty());
      EXPECT_NE(artifact.document, machine::document_kind::none);

      const machine::document_edition* known =
          machine::find_document(digest_of(artifact.fingerprint));
      ASSERT_NE(known, nullptr) << artifact.about;
      EXPECT_EQ(known->kind, artifact.document);
      EXPECT_EQ(known->name, artifact.about);
    }
  }
  // Both releases run one program, so both carry the same documents.
  EXPECT_EQ(documents.size(), machine::known_documents().size());
}

TEST(EditionFacts, EveryFileIsRequiredAndCarriesASizeAndADigest) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    for (const edition_artifact& artifact : edition.artifacts) {
      if (artifact.kind != artifact_kind::file) {
        continue;
      }
      EXPECT_TRUE(artifact.required) << artifact.name;
      EXPECT_GT(artifact.size, 0U) << artifact.name;
      EXPECT_EQ(artifact.fingerprint.size(), sha256_digest::text_length)
          << artifact.name;
      EXPECT_EQ(artifact.document, machine::document_kind::none);
    }
  }
}

TEST(EditionFacts, TheConfigurationFileIsRequiredByNameAlone) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    std::size_t configurations = 0;
    for (const edition_artifact& artifact : edition.artifacts) {
      if (artifact.kind != artifact_kind::configuration) {
        continue;
      }
      ++configurations;
      EXPECT_EQ(artifact.name, "POOL.CFG") << edition.id;
      EXPECT_TRUE(artifact.required) << edition.id;
      EXPECT_THAT(artifact.fingerprint, IsEmpty()) << edition.id;
      EXPECT_EQ(artifact.size, 0U) << edition.id;
    }
    EXPECT_EQ(configurations, 1U) << edition.id;
  }
}

TEST(EditionFacts, TheStoreRowListsOnlyWhatTheStoresShip) {
  for (const edition_artifact& artifact : store().artifacts) {
    EXPECT_NE(artifact.name, "CFG.C");
    EXPECT_NE(artifact.name, "CFG.EXE");
    EXPECT_NE(artifact.name, "POOL.BAT");
  }
}

/// The one place in this repository that already pins a pristine copy of
/// the repack, file by file: the descriptor of the session recorded on it
/// (`tests/sessions/README.md`). If the two disagree, one of them is
/// wrong about the player's disk, and a checklist that is wrong about it
/// is worse than none.
TEST(EditionFacts, TheRepackFileListIsThePristineDiskTheSessionPins) {
  const std::string path =
      std::string(AMBERFOLIO_SESSIONS_DIR) + "/party.session";
  std::ifstream descriptor(path);
  ASSERT_TRUE(descriptor.is_open()) << path;

  std::unordered_map<std::string, std::string> pinned;  // name -> "size sha"
  std::string keyword;
  while (descriptor >> keyword) {
    if (keyword == "file") {
      std::string name;
      std::string size;
      std::string sha;
      ASSERT_TRUE(static_cast<bool>(descriptor >> name >> size >> sha));
      std::string pin(size);
      pin.append(" ").append(sha);
      pinned.emplace(name, pin);
    } else {
      // A `dir` line included: the disk pins its SAVE directory, which
      // the program makes itself and no row requires.
      descriptor.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }
  ASSERT_THAT(pinned, Not(IsEmpty()));

  std::size_t named = 0;
  for (const edition_artifact& artifact : repack().artifacts) {
    if (artifact.kind == artifact_kind::document) {
      continue;
    }
    ++named;
    const auto found = pinned.find(std::string(artifact.name));
    ASSERT_NE(found, pinned.end())
        << artifact.name << " is required by the table and is not on the "
        << "disk party.session pins";
    if (artifact.kind == artifact_kind::file) {
      EXPECT_EQ(found->second, std::to_string(artifact.size) + " " +
                                   std::string(artifact.fingerprint));
    }
  }
  EXPECT_EQ(named, pinned.size());
}

TEST(EditionFacts, TheFingerprintFindsTheBaseline) {
  EXPECT_EQ(find_requirements(store().fingerprint), &store());
  EXPECT_EQ(find_requirements(repack().fingerprint), &store());
  EXPECT_EQ(find_requirements(""), nullptr);
  EXPECT_EQ(find_requirements(std::string(sha256_digest::text_length, 'a')),
            nullptr);
}

TEST(EditionMatch, AWholeCopyOfEachReleaseMatchesItsOwnRowComplete) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    const std::vector<offered_file> offered = whole_copy(edition);
    const edition_match match = match_edition(offered);

    ASSERT_EQ(match.edition, &edition) << edition.id;
    EXPECT_THAT(match.unclaimed, IsEmpty()) << edition.id;
    // The two documents are absent and say nothing, because neither is
    // required; the player's own POOL.CFG answers for its row.
    EXPECT_THAT(match.missing, IsEmpty()) << edition.id;
    EXPECT_TRUE(match.complete()) << edition.id;
  }
}

TEST(EditionMatch, TheConfigurationFileIsMatchedOnItsNameInAnyCaseOrPath) {
  std::vector<offered_file> offered = whole_copy(store());
  for (offered_file& file : offered) {
    if (file.name == "POOL.CFG") {
      file.name = "POOLRAD/pool.cfg";
    }
  }
  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &store());
  EXPECT_TRUE(match.complete());
  EXPECT_THAT(match.unclaimed, IsEmpty());
}

TEST(EditionMatch, ACopyWithNoConfigurationIsMissingIt) {
  std::vector<offered_file> offered = whole_copy(store());
  std::erase_if(offered, [](const offered_file& file) {
    return file.name == "POOL.CFG";
  });
  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &store());
  EXPECT_TRUE(misses(match, "POOL.CFG"));
  EXPECT_EQ(match.missing.size(), 1U);
}

TEST(EditionMatch, AConfigurationFileAloneNamesNothing) {
  const std::vector<offered_file> offered = {
      {.name = "POOL.CFG", .digest = digest_of(players_config)}};
  const edition_match match = match_edition(offered);
  EXPECT_EQ(match.edition, nullptr);
}

TEST(EditionMatch, SaysWhichRequiredFilesAreMissing) {
  const edition_requirements& edition = store();
  std::vector<offered_file> offered = whole_copy(edition);
  ASSERT_GT(offered.size(), 2U);
  const std::string_view dropped = offered.front().name;
  offered.erase(offered.begin());

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  EXPECT_TRUE(misses(match, dropped)) << dropped;
}

TEST(EditionMatch, MatchesOnBytesSoARenamedFileStillCounts) {
  const edition_requirements& edition = store();
  std::vector<offered_file> offered = whole_copy(edition);
  offered.front().name = "SOMETHING.ELSE";

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  EXPECT_THAT(match.unclaimed, IsEmpty());
}

TEST(EditionMatch, TheRightNameWithTheWrongBytesIsMissingAndUnclaimed) {
  const edition_requirements& edition = store();
  std::vector<offered_file> offered = whole_copy(edition);
  const std::string_view name = offered.front().name;
  offered.front().digest = digest_of(std::string(64, 'b'));

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  // Both halves of the answer, which is the whole reason there are two
  // lists: the artifact is not here, and this file is not one of ours.
  EXPECT_TRUE(misses(match, name)) << name;
  ASSERT_EQ(match.unclaimed.size(), 1U);
  EXPECT_EQ(offered[match.unclaimed[0]].name, name);
}

TEST(EditionMatch, TheOverlayTellsTheTwoReleasesApart) {
  // A store copy with the repack's GAME.OVR in it: the repack's row is
  // the closer one, and what it is missing is the three files only the
  // repack ships.
  std::vector<offered_file> offered = whole_copy(store());
  for (offered_file& file : offered) {
    if (file.name == "GAME.OVR") {
      for (const edition_artifact& artifact : repack().artifacts) {
        if (artifact.name == "GAME.OVR") {
          file.digest = digest_of(artifact.fingerprint);
        }
      }
    }
  }
  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &repack());
  EXPECT_THAT(match.unclaimed, IsEmpty());
  EXPECT_EQ(match.missing.size(), 3U);
  EXPECT_TRUE(misses(match, "CFG.C"));
  EXPECT_TRUE(misses(match, "CFG.EXE"));
  EXPECT_TRUE(misses(match, "POOL.BAT"));
}

TEST(EditionMatch, KnowsNothingRatherThanGuessingWhenNothingBelongs) {
  const std::vector<offered_file> offered = {
      {.name = "README.TXT", .digest = digest_of(std::string(64, 'c'))},
      {.name = "SETUP.EXE", .digest = digest_of(std::string(64, 'd'))},
  };
  const edition_match match = match_edition(offered);
  EXPECT_EQ(match.edition, nullptr);
  EXPECT_FALSE(match.complete());
  EXPECT_THAT(match.matched, IsEmpty());
  EXPECT_THAT(match.missing, IsEmpty());
}

TEST(EditionMatch, OneSharedFileNamesTheBaseline) {
  // START.EXE belongs to both rows equally; a tie keeps the earlier row,
  // which is the release sold today.
  const edition_requirements& edition = store();
  const std::vector<offered_file> offered = {
      {.name = edition.boot, .digest = digest_of(edition.fingerprint)}};
  const edition_match match = match_edition(offered);

  ASSERT_EQ(match.edition, &edition);
  EXPECT_EQ(match.matched.size(), 1U);
  EXPECT_THAT(match.unclaimed, IsEmpty());
  EXPECT_GT(match.missing.size(), 1U);
}

// --- Where an edition sits, and what of it is the player's (#397) --------

TEST(EditionFacts, EveryInstallDirectoryIsADirectoryTheMachineCanName) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    const machine::vfs_result<machine::dos_path> install =
        machine::canonicalize_host_path(
            {edition.install.data(), edition.install.size()});
    EXPECT_TRUE(install.ok()) << edition.id << " says " << edition.install;
  }
}

/// `name` inside `directory`, as the machine names it.
machine::dos_path inside(const machine::dos_path& directory,
                         std::string_view name) {
  const machine::vfs_result<machine::dos_path> where =
      machine::canonicalize(directory, {name.data(), name.size()});
  EXPECT_TRUE(where.ok()) << name;
  return where.value;
}

TEST(EditionFacts, NoFileAnEditionShipsIsEverAPlaythroughs) {
  // The ordering trap the save layer's first-match rule has to survive: a
  // copy that saves beside its own game files puts its save directory and
  // its starting directory in one place, and then every file the
  // publisher ships is a candidate for a row. Only the configuration file
  // is named, and as the program's. Checked for every edition, laid out
  // where it installs, against the three save directories a copy can
  // name: its own folder, a folder below it, and the root's `\SAVE`.
  for (const edition_requirements& edition : edition_requirements_table()) {
    const machine::save_layer* layer =
        machine::save_layer_for(digest_of(edition.fingerprint));
    ASSERT_NE(layer, nullptr) << edition.id;
    const machine::vfs_result<machine::dos_path> install =
        machine::canonicalize_host_path(
            {edition.install.data(), edition.install.size()});
    ASSERT_TRUE(install.ok()) << edition.id;

    for (const machine::dos_path& saves :
         {install.value, inside(install.value, "SAVE"),
          inside(machine::dos_path{}, "SAVE")}) {
      const machine::save_layer_places places{
          .save_directory = saves, .current_directory = install.value};
      for (const edition_artifact& artifact : edition.artifacts) {
        if (artifact.kind == artifact_kind::document) {
          continue;
        }
        const machine::save_layer_row row = machine::match_save_file(
            *layer, places, inside(install.value, artifact.name));
        if (artifact.name == machine::save_layer_config_file) {
          ASSERT_NE(row.file, nullptr) << edition.id;
          EXPECT_EQ(row.file->kind, machine::save_file_kind::config)
              << edition.id;
        } else {
          EXPECT_EQ(row.file, nullptr)
              << edition.id << ": " << artifact.name << " was claimed as "
              << (row.file != nullptr
                      ? machine::save_file_kind_name(row.file->kind)
                      : "");
        }
      }
    }
  }
}

}  // namespace
}  // namespace amberfolio::host
