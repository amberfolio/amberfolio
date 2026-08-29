// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal edition fact table (journal_facts.h, M5-E3 #174): what it
// knows, what it says about a document it does not, and the shape every
// row has to have.
//
// `tests/core/machine/document_test.cpp` is this suite one level out, and
// the two are checked alike on purpose — a fingerprint has to mean the
// same thing in all three of this project's tables.
//
// **No journal is here.** Every fingerprint below is this file's own
// invention or the probe's, which is a fact about a document this project
// generates. Nothing from a real edition is in this tree
// (CONTRIBUTING.md), and a test that needed one would be a test nobody
// without the document could run.

#include "amberfolio/host/journal_facts.h"

#include <cstdint>
#include <set>
#include <string_view>

#include "amberfolio/host/journal_probe.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

TEST(JournalTable, IsEmptyAndSaysSoRatherThanGuessing) {
  // The state journal_facts.h describes at length: these are facts about
  // a document somebody has to sit down with, and nobody has. The
  // consequence is the fail-closed one — every real journal is an
  // unrecognized edition until this table gains a row.
  //
  // If that ever changes, this is the line that changes with it, and the
  // rest of this file is what the new row has to satisfy.
  EXPECT_TRUE(known_journals().empty())
      << "an edition is here now; check it against the rules below and"
         " update #174's expectations with it";
}

/// Every rule a row has to satisfy, applied to whatever table is handed
/// in — the shipped one (empty, today) and the probe's (not).
void CheckRows(std::span<const journal_edition> table) {
  for (const journal_edition& edition : table) {
    sha256_digest parsed;
    EXPECT_TRUE(machine::parse_digest(edition.fingerprint, parsed))
        << edition.name << " has a fingerprint that is not 64 hex characters";
    EXPECT_FALSE(edition.name.empty());
    EXPECT_EQ(find_journal(table, parsed), &edition);

    for (const char c : edition.fingerprint) {
      const bool digit = c >= '0' && c <= '9';
      const bool lower = c >= 'a' && c <= 'f';
      EXPECT_TRUE(digit || lower) << edition.name << " has '" << c << "' in it";
    }

    EXPECT_FALSE(edition.entries.empty())
        << edition.name << " knows the insides of nothing";
    EXPECT_LE(edition.entries.size(), journal_max_entries);

    std::set<std::uint16_t> numbers;
    for (const journal_entry_fact& fact : edition.entries) {
      EXPECT_TRUE(numbers.insert(fact.number).second)
          << edition.name << " has two rows for entry " << fact.number;
      EXPECT_NE(fact.length, 0U) << "entry " << fact.number << " is no bytes";
      EXPECT_NE(fact.image.width, 0U);
      EXPECT_NE(fact.image.height, 0U);
      EXPECT_NE(fact.region.width, 0U);
      EXPECT_NE(fact.region.height, 0U);
      // A region has to be inside its image, and this is the one rule a
      // hand-edited row gets wrong quietly: the extractor would refuse
      // it, but it would refuse it on a player's machine rather than
      // here.
      EXPECT_LE(fact.region.left + fact.region.width, fact.image.width)
          << "entry " << fact.number << "'s region is off the right of it";
      EXPECT_LE(fact.region.top + fact.region.height, fact.image.height)
          << "entry " << fact.number << "'s region is off the bottom of it";
      EXPECT_TRUE(journal_filter_supported(fact.image.filter))
          << "entry " << fact.number << " is under a filter this build"
          << " cannot decode (" << journal_filter_name(fact.image.filter)
          << "), so shipping the row would ship a promise it cannot keep";
    }
  }
}

TEST(JournalTable, EveryShippedRowIsAWellFormedFact) {
  CheckRows(known_journals());
}

TEST(JournalTable, TheProbeSatisfiesTheSameRules) {
  // The probe is the only edition this build has, so it is the only thing
  // that can demonstrate the rules are checkable at all rather than
  // vacuously true over an empty table.
  ASSERT_EQ(journal_probe_table().size(), 1U);
  CheckRows(journal_probe_table());
}

TEST(JournalTable, EveryEditionIsAJournalInTheDocumentTable) {
  // The two tables are one artifact seen twice: the document table gates
  // a seam on a file, this one says what is inside that same file. An
  // edition here that the gate does not know would be a journal that
  // could be ingested and never read.
  for (const journal_edition& edition : known_journals()) {
    sha256_digest digest;
    ASSERT_TRUE(machine::parse_digest(edition.fingerprint, digest));
    const machine::document_edition* document = machine::find_document(digest);
    ASSERT_NE(document, nullptr)
        << edition.name << " is not in the document table";
    EXPECT_EQ(document->kind, machine::document_kind::journal)
        << edition.name << " is in the document table as something else";
  }
}

TEST(JournalTable, TheProbeIsNotAThingAPlayersBuildKnows) {
  // A document this project made up has no business in a player's
  // listing, in the gate, or in the shipped table — the same rule the web
  // host's probe seam is held to.
  sha256_digest digest;
  ASSERT_TRUE(
      machine::parse_digest(journal_probe_table().front().fingerprint, digest));
  EXPECT_EQ(find_journal(digest), nullptr);
  EXPECT_EQ(machine::find_document(digest), nullptr);
  EXPECT_EQ(machine::find_edition(digest), nullptr);
}

TEST(JournalTable, AnUnknownDocumentIsNull) {
  sha256_digest stranger;
  ASSERT_TRUE(machine::parse_digest(
      "0000000000000000000000000000000000000000000000000000000000000000",
      stranger));
  EXPECT_EQ(find_journal(stranger), nullptr);
  EXPECT_EQ(find_journal(journal_probe_table(), stranger), nullptr);
}

TEST(JournalFilter, IsNamedAsThePdfSpellsItAndSaysWhatItCanDecode) {
  EXPECT_STREQ(journal_filter_name(journal_filter::none), "none");
  EXPECT_STREQ(journal_filter_name(journal_filter::flate), "FlateDecode");
  EXPECT_STREQ(journal_filter_name(journal_filter::dct), "DCTDecode");
  EXPECT_STREQ(journal_filter_name(journal_filter::ccitt), "CCITTFaxDecode");
  EXPECT_STREQ(journal_filter_name(journal_filter::jbig2), "JBIG2Decode");

  // Two decoded, three named and refused. "Log, don't fake", one level up
  // from a service: an edition that needs one of the other three learns
  // which one rather than getting noise.
  EXPECT_TRUE(journal_filter_supported(journal_filter::none));
  EXPECT_TRUE(journal_filter_supported(journal_filter::flate));
  EXPECT_FALSE(journal_filter_supported(journal_filter::dct));
  EXPECT_FALSE(journal_filter_supported(journal_filter::ccitt));
  EXPECT_FALSE(journal_filter_supported(journal_filter::jbig2));
}

TEST(JournalTrouble, EveryReasonHasWordsAPersonReads) {
  for (const journal_trouble what :
       {journal_trouble::none, journal_trouble::unrecognized_edition,
        journal_trouble::no_such_entry, journal_trouble::stream_out_of_bounds,
        journal_trouble::filter_unsupported, journal_trouble::image_unsupported,
        journal_trouble::stream_corrupt, journal_trouble::stream_size_wrong,
        journal_trouble::region_outside, journal_trouble::no_engine,
        journal_trouble::engine_failed, journal_trouble::not_a_store,
        journal_trouble::too_large}) {
    const char* name = journal_trouble_name(what);
    ASSERT_NE(name, nullptr);
    EXPECT_NE(std::string_view(name), "something unnamed went wrong")
        << "a reason reached the fallback, which means one was added"
           " without its words";
  }
}

}  // namespace
}  // namespace amberfolio::host
