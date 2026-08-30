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
#include <map>
#include <set>
#include <string_view>
#include <utility>

#include "amberfolio/host/journal_probe.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

TEST(JournalTable, HasTheOneEditionSomebodySatDownWith) {
  // It was empty until M5-E3b (#214), and the header said why at length:
  // these are facts about a document somebody has to sit down with. One
  // person now has, so the fail-closed state has a row in it — and every
  // rule below is what that row had to satisfy.
  ASSERT_EQ(known_journals().size(), 1U)
      << "an edition came or went; the rules below are what a row has to"
         " satisfy, and #214 is where this one came from";
  EXPECT_EQ(known_journals().front().entries.size(), 99U)
      << "the archive release's journal has fifty-eight entries,"
         " twenty-three tales and eighteen proclamations";

  std::map<journal_kind, std::size_t> counted;
  for (const journal_entry_fact& fact : known_journals().front().entries) {
    ++counted[fact.kind];
  }
  EXPECT_EQ(counted[journal_kind::entry], 58U);
  EXPECT_EQ(counted[journal_kind::tale], 23U);
  EXPECT_EQ(counted[journal_kind::proclamation], 18U);
}

TEST(JournalTable, TheEntriesAndTheTalesAreNumberedWithoutAGap) {
  // Sequential and complete: the numbering of these two sections is a
  // chain that was read off the printed headings, and a gap or a repeat
  // in it is the one way that reading could have gone wrong quietly.
  //
  // The proclamations are **not** here, and that is the point of keeping
  // them apart: they are printed in Roman numerals, they start at 59 and
  // they skip. A run of them proves nothing, so they get the check they
  // can pass instead — the one below.
  for (const auto& [kind, last] : {std::pair{journal_kind::entry, 58U},
                                   std::pair{journal_kind::tale, 23U}}) {
    std::uint16_t expected = 1;
    for (const journal_entry_fact& fact : known_journals().front().entries) {
      if (fact.kind != kind) {
        continue;
      }
      EXPECT_EQ(fact.number, expected)
          << journal_kind_name(kind) << " rows are not in order";
      ++expected;
    }
    EXPECT_EQ(expected, last + 1U)
        << journal_kind_name(kind) << " does not run to the end";
  }
}

TEST(JournalTable, TheProclamationsAscendWithoutRepeating) {
  // What can be checked about a section whose numbering has gaps in it,
  // and it is not nothing: the order was what caught a misread numeral
  // while the table was being gathered (`journal_facts.cpp`).
  std::uint16_t previous = 0;
  std::size_t seen = 0;
  for (const journal_entry_fact& fact : known_journals().front().entries) {
    if (fact.kind != journal_kind::proclamation) {
      continue;
    }
    EXPECT_GT(fact.number, previous) << "the proclamations are out of order";
    previous = fact.number;
    ++seen;
  }
  EXPECT_EQ(seen, 18U);
  EXPECT_EQ(previous, 214U) << "the last proclamation is CCXIV";
}

TEST(JournalTable, TheArchiveEditionsPagesAreAllCarriedNotDecoded) {
  // Every page of it is `/DCTDecode` (#212), which is the whole reason
  // that passthrough exists. A row that said otherwise would be a row
  // whose stream this build would try to inflate.
  for (const journal_entry_fact& fact : known_journals().front().entries) {
    for (const journal_fragment& piece : fact.fragments) {
      EXPECT_EQ(piece.image.filter, journal_filter::dct);
      EXPECT_FALSE(journal_filter_decoded(piece.image.filter));
      EXPECT_EQ(piece.image.components, 3U) << "the scans are RGB";
    }
  }
}

TEST(JournalTable, TheArchiveEditionsPiecesAreInReadingOrder) {
  // What an engine reads out of the pieces is joined in table order
  // (`journal_facts.h`), so the pieces of one entry have to be in the
  // order a person reads them: down a column, then the next column, then
  // the next scan. Anything else is an entry whose sentences are shuffled.
  for (const journal_entry_fact& fact : known_journals().front().entries) {
    for (std::size_t i = 1; i < fact.fragments.size(); ++i) {
      const journal_fragment& before = fact.fragments[i - 1];
      const journal_fragment& after = fact.fragments[i];
      const bool later_scan = after.page > before.page;
      const bool later_column =
          after.page == before.page && after.region.left > before.region.left;
      EXPECT_TRUE(later_scan || later_column)
          << "entry " << fact.number << " has a piece that does not follow"
          << " the one before it";
    }
  }
}

TEST(JournalTable, TheArchiveEditionIsMostlyButNotAlwaysOnePiece) {
  // The finding that shaped the schema, as a number: an entry is usually
  // one rectangle and often is not, so a table of one region per entry
  // could not have described this document (#214).
  std::size_t one = 0;
  std::size_t many = 0;
  for (const journal_entry_fact& fact : known_journals().front().entries) {
    (fact.fragments.size() == 1U ? one : many) += 1U;
  }
  EXPECT_GT(one, 0U);
  EXPECT_GT(many, 0U) << "if every entry fits one rectangle, the fragment"
                         " list has stopped earning its keep";
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

    // The *pair*, since #218: two rows may share a number as long as they
    // are in different sections, and two rows that share both are the
    // ambiguity the kind was added to remove.
    std::set<std::pair<journal_kind, std::uint16_t>> numbers;
    for (const journal_entry_fact& fact : edition.entries) {
      EXPECT_TRUE(numbers.insert({fact.kind, fact.number}).second)
          << edition.name << " has two rows for "
          << journal_kind_name(fact.kind) << ' ' << fact.number;
      EXPECT_FALSE(fact.fragments.empty())
          << "entry " << fact.number << " is nowhere";
      for (const journal_fragment& piece : fact.fragments) {
        EXPECT_NE(piece.length, 0U)
            << "entry " << fact.number << " has a piece of no bytes";
        EXPECT_NE(piece.image.width, 0U);
        EXPECT_NE(piece.image.height, 0U);
        EXPECT_NE(piece.region.width, 0U);
        EXPECT_NE(piece.region.height, 0U);
        // A region has to be inside its image, and this is the one rule a
        // hand-edited row gets wrong quietly: the extractor would refuse
        // it, but it would refuse it on a player's machine rather than
        // here.
        EXPECT_LE(piece.region.left + piece.region.width, piece.image.width)
            << "entry " << fact.number << " has a piece off the right of it";
        EXPECT_LE(piece.region.top + piece.region.height, piece.image.height)
            << "entry " << fact.number << " has a piece off the bottom of it";
        EXPECT_TRUE(journal_filter_supported(piece.image.filter))
            << "entry " << fact.number << " is under a filter this build"
            << " cannot carry (" << journal_filter_name(piece.image.filter)
            << "), so shipping the row would ship a promise it cannot keep";
        // Every piece of one entry is read by one engine call, so an
        // entry whose pieces disagreed about how they are encoded is one
        // no engine could be handed (`journal_extract.h`).
        EXPECT_EQ(journal_filter_decoded(piece.image.filter),
                  journal_filter_decoded(fact.fragments.front().image.filter))
            << "entry " << fact.number << " mixes decoded and carried pieces";
      }
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

  // Two decoded here, one carried to the engine undecoded (#212), two
  // named and refused. "Log, don't fake", one level up from a service: an
  // edition that needs one of the last two learns which one rather than
  // getting noise.
  EXPECT_TRUE(journal_filter_decoded(journal_filter::none));
  EXPECT_TRUE(journal_filter_decoded(journal_filter::flate));
  EXPECT_FALSE(journal_filter_decoded(journal_filter::dct));

  EXPECT_TRUE(journal_filter_supported(journal_filter::none));
  EXPECT_TRUE(journal_filter_supported(journal_filter::flate));
  EXPECT_TRUE(journal_filter_supported(journal_filter::dct));
  EXPECT_FALSE(journal_filter_supported(journal_filter::ccitt));
  EXPECT_FALSE(journal_filter_supported(journal_filter::jbig2));

  // And every filter this build carries is one an edition may name, which
  // is the invariant the two questions have to keep between them: a
  // supported filter is decoded here or handed over whole, never neither.
  for (const journal_filter filter :
       {journal_filter::none, journal_filter::flate, journal_filter::dct,
        journal_filter::ccitt, journal_filter::jbig2}) {
    if (journal_filter_decoded(filter)) {
      EXPECT_TRUE(journal_filter_supported(filter))
          << journal_filter_name(filter);
    }
  }
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
