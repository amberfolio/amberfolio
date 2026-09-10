// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition requirement table, and the joints it has to keep.
//
// The table is data/editions.json and the C++ under test is compiled
// from it (hosts/common/CMakeLists.txt), so there is nothing here about
// whether the two agree — they are one thing. What is here is everything
// the JSON could still get wrong on its own: an edition core does not
// recognise, a document core does not know, a boot file that is not in
// its own artifact list, and a file list that has drifted from the
// pristine disk `tests/sessions/party.session` pins.

#include "amberfolio/host/edition_facts.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
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

/// The one edition this build states requirements for, and the one every
/// case below is about.
const edition_requirements& only_edition() {
  const std::span<const edition_requirements> table =
      edition_requirements_table();
  EXPECT_THAT(table, Not(IsEmpty()));
  return table[0];
}

/// A player's whole copy of `edition`, as a host that walked the
/// directory would offer it: every file, and nothing that is not one.
std::vector<offered_file> whole_copy(const edition_requirements& edition) {
  std::vector<offered_file> offered;
  for (const edition_artifact& artifact : edition.artifacts) {
    if (artifact.kind != artifact_kind::file) {
      continue;
    }
    offered.push_back(
        {.name = artifact.name, .digest = digest_of(artifact.fingerprint)});
  }
  return offered;
}

TEST(EditionFacts, EveryEditionIsOneTheMachineRecognizes) {
  for (const edition_requirements& edition : edition_requirements_table()) {
    sha256_digest digest;
    ASSERT_TRUE(machine::parse_digest(edition.fingerprint, digest))
        << edition.id;
    const machine::edition* known = machine::find_edition(digest);
    ASSERT_NE(known, nullptr)
        << edition.id
        << ": the requirements name an edition machine::known_editions() "
           "does not, so a host could render a checklist for a program "
           "this build would then refuse every seam for";
    EXPECT_EQ(known->name, edition.name);
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
  std::size_t documents = 0;
  for (const edition_requirements& edition : edition_requirements_table()) {
    for (const edition_artifact& artifact : edition.artifacts) {
      if (artifact.kind != artifact_kind::document) {
        continue;
      }
      ++documents;
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
  EXPECT_EQ(documents, machine::known_documents().size());
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

/// The one place in this repository that already pins a pristine copy of
/// this edition, file by file: the descriptor of the session recorded on
/// one (`tests/sessions/README.md`). If the two disagree, one of them is
/// wrong about the player's disk, and a checklist that is wrong about it
/// is worse than none.
TEST(EditionFacts, TheFileListIsThePristineDiskTheSessionPins) {
  const std::string path =
      std::string(AMBERFOLIO_SESSIONS_DIR) + "/party.session";
  std::ifstream descriptor(path);
  ASSERT_TRUE(descriptor.is_open()) << path;

  std::unordered_map<std::string, std::string> pinned;  // name -> "size sha"
  std::vector<std::string> pinned_dirs;
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
    } else if (keyword == "dir") {
      std::string name;
      ASSERT_TRUE(static_cast<bool>(descriptor >> name));
      pinned_dirs.push_back(name);
    } else {
      descriptor.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }
  ASSERT_THAT(pinned, Not(IsEmpty()));

  const edition_requirements& edition = only_edition();
  std::size_t files = 0;
  std::size_t directories = 0;
  for (const edition_artifact& artifact : edition.artifacts) {
    if (artifact.kind == artifact_kind::document) {
      continue;
    }
    const std::string name(artifact.name);
    if (artifact.kind == artifact_kind::directory) {
      ++directories;
      EXPECT_THAT(pinned_dirs, ::testing::Contains(name));
      continue;
    }
    ++files;
    const auto found = pinned.find(name);
    ASSERT_NE(found, pinned.end())
        << name << " is required by the table and is not on the disk "
        << "party.session pins";
    EXPECT_EQ(found->second, std::to_string(artifact.size) + " " +
                                 std::string(artifact.fingerprint));
  }
  EXPECT_EQ(files, pinned.size());
  EXPECT_EQ(directories, pinned_dirs.size());
}

TEST(EditionFacts, FindsRequirementsByTheBootFingerprint) {
  const edition_requirements& edition = only_edition();
  EXPECT_EQ(find_requirements(edition.fingerprint), &edition);
  EXPECT_EQ(find_requirements(""), nullptr);
  EXPECT_EQ(find_requirements(std::string(sha256_digest::text_length, 'a')),
            nullptr);
}

TEST(EditionMatch, AWholeCopyIsCompleteExceptForWhatIsNotAFile) {
  const edition_requirements& edition = only_edition();
  const std::vector<offered_file> offered = whole_copy(edition);
  const edition_match match = match_edition(offered);

  ASSERT_EQ(match.edition, &edition);
  EXPECT_THAT(match.unclaimed, IsEmpty());
  // The directory is the one required row a list of files cannot carry,
  // and it is deliberately still reported: a copy with no SAVE\ is a
  // copy that cannot save. The two documents are absent as well and say
  // nothing here, because neither is required.
  ASSERT_EQ(match.missing.size(), 1U);
  EXPECT_EQ(edition.artifacts[match.missing[0]].kind, artifact_kind::directory);
  EXPECT_FALSE(match.complete());
}

TEST(EditionMatch, SaysWhichRequiredFilesAreMissing) {
  const edition_requirements& edition = only_edition();
  std::vector<offered_file> offered = whole_copy(edition);
  ASSERT_GT(offered.size(), 2U);
  const std::string_view dropped = offered.back().name;
  offered.pop_back();

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  bool named = false;
  for (const std::size_t a : match.missing) {
    named = named || edition.artifacts[a].name == dropped;
  }
  EXPECT_TRUE(named) << dropped;
}

TEST(EditionMatch, MatchesOnBytesSoARenamedFileStillCounts) {
  const edition_requirements& edition = only_edition();
  std::vector<offered_file> offered = whole_copy(edition);
  offered.front().name = "SOMETHING.ELSE";

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  EXPECT_THAT(match.unclaimed, IsEmpty());
}

TEST(EditionMatch, TheRightNameWithTheWrongBytesIsMissingAndUnclaimed) {
  const edition_requirements& edition = only_edition();
  std::vector<offered_file> offered = whole_copy(edition);
  const std::string_view name = offered.front().name;
  offered.front().digest = digest_of(std::string(64, 'b'));

  const edition_match match = match_edition(offered);
  ASSERT_EQ(match.edition, &edition);
  // Both halves of the answer, which is the whole reason there are two
  // lists: the artifact is not here, and this file is not one of ours.
  bool missed = false;
  for (const std::size_t a : match.missing) {
    missed = missed || edition.artifacts[a].name == name;
  }
  EXPECT_TRUE(missed) << name;
  ASSERT_EQ(match.unclaimed.size(), 1U);
  EXPECT_EQ(offered[match.unclaimed[0]].name, name);
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

TEST(EditionMatch, OneFileIsEnoughToNameTheClosestEdition) {
  const edition_requirements& edition = only_edition();
  const std::vector<offered_file> offered = {
      {.name = edition.boot, .digest = digest_of(edition.fingerprint)}};
  const edition_match match = match_edition(offered);

  ASSERT_EQ(match.edition, &edition);
  EXPECT_EQ(match.matched.size(), 1U);
  EXPECT_THAT(match.unclaimed, IsEmpty());
  EXPECT_GT(match.missing.size(), 1U);
}

}  // namespace
}  // namespace amberfolio::host
