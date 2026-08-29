// SPDX-License-Identifier: AGPL-3.0-only
//
// The text store (journal_store.h, M5-E3 #174): the two texts per entry,
// the round trip, and the refusals.
//
// Every string below is this file's own invention. A store holds a
// player's own document read off a player's own copy, and it is the one
// thing in this project that *is* content — so nothing resembling a real
// transcription is in this tree, and the fixtures are deliberately
// obvious nonsense.

#include "amberfolio/host/journal_store.h"

#include <string>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

journal_store Filled() {
  journal_store store;
  store.set_edition(
      "1111111111111111111111111111111111111111111111111111111111111111");
  store.set_engine("test fixture 1.0");
  EXPECT_TRUE(store.record_scan(2, "second entry, as scanned"));
  EXPECT_TRUE(store.record_scan(1, "first entry, as scanned"));
  return store;
}

TEST(JournalStore, ACorrectionIsWhatTheReaderShows) {
  journal_store store = Filled();
  EXPECT_EQ(store.text(1), "first entry, as scanned");
  ASSERT_TRUE(store.correct(1, "first entry, as a person fixed it"));
  EXPECT_EQ(store.text(1), "first entry, as a person fixed it");

  // And the scan is still there underneath, which is what makes a
  // re-ingestion able to improve it without destroying the fix.
  const journal_text* entry = store.find(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->scanned, "first entry, as scanned");
  EXPECT_EQ(entry->corrected, "first entry, as a person fixed it");
}

TEST(JournalStore, IngestionReplacesTheScanAndNeverTheCorrection) {
  // #174's "a player can fix an OCR error and the fix survives
  // re-ingestion", at the one place it is actually decided.
  journal_store store = Filled();
  ASSERT_TRUE(store.correct(1, "what a person wrote"));
  ASSERT_TRUE(store.record_scan(1, "what a better engine read"));

  const journal_text* entry = store.find(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->scanned, "what a better engine read");
  EXPECT_EQ(entry->corrected, "what a person wrote");
  EXPECT_EQ(store.text(1), "what a person wrote");
}

TEST(JournalStore, AnEntryNobodyHasIsEmptyRatherThanAbsent) {
  const journal_store store = Filled();
  EXPECT_EQ(store.find(99), nullptr);
  EXPECT_TRUE(store.text(99).empty());
}

TEST(JournalStore, TheRoundTripIsExact) {
  journal_store store = Filled();
  ASSERT_TRUE(store.correct(2, "a correction\nwith a newline in it"));

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(read.edition(), store.edition());
  EXPECT_EQ(read.engine(), store.engine());
  ASSERT_EQ(read.size(), store.size());
  EXPECT_EQ(read.text(1), store.text(1));
  EXPECT_EQ(read.text(2), store.text(2));
  EXPECT_EQ(read.serialize(), store.serialize());
  EXPECT_EQ(read.fingerprint(), store.fingerprint());
}

TEST(JournalStore, TextThatLooksLikeAHeaderSurvivesTheRoundTrip) {
  // Why every record is length-prefixed: a transcription that happens to
  // contain a line starting with `scanned 3 4` must not be readable as
  // one, and the only way to be sure of that is to never look for a
  // keyword inside a body at all.
  journal_store store;
  store.set_edition("abc");
  store.set_engine("test");
  ASSERT_TRUE(store.record_scan(
      3, "scanned 4 8\nfake\ncorrected 5 1\nx\namberfolio-journal 1\n"));

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(read.size(), 1U);
  EXPECT_EQ(read.text(3), store.text(3));
}

TEST(JournalStore, EntriesComeOutSortedSoAFingerprintMeansSomething) {
  journal_store a;
  a.set_edition("e");
  a.set_engine("g");
  ASSERT_TRUE(a.record_scan(7, "seven"));
  ASSERT_TRUE(a.record_scan(3, "three"));

  journal_store b;
  b.set_edition("e");
  b.set_engine("g");
  ASSERT_TRUE(b.record_scan(3, "three"));
  ASSERT_TRUE(b.record_scan(7, "seven"));

  // Same content written in two orders is one file, which is what makes
  // `fingerprint()` worth reporting on an issue.
  EXPECT_EQ(a.serialize(), b.serialize());
  EXPECT_EQ(a.fingerprint(), b.fingerprint());
}

TEST(JournalStore, AnEmptyStoreIsStillAStore) {
  journal_store store;
  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_TRUE(read.empty());
  EXPECT_TRUE(read.edition().empty());
}

TEST(JournalStore, SomethingThatIsNotAStoreIsRefusedWhole) {
  journal_store store = Filled();
  const std::string before = store.serialize();

  for (const std::string& bad :
       {std::string("hello\n"), std::string(""),
        std::string("amberfolio-journal 1\n"),
        std::string("amberfolio-journal 1\nedition a\n"),
        // A record whose body is shorter than its length says, which is
        // what a truncated write leaves behind.
        std::string("amberfolio-journal 1\nedition a\nengine b\n"
                    "scanned 1 40\nshort\n"),
        // A keyword that is not one of the two.
        std::string("amberfolio-journal 1\nedition a\nengine b\n"
                    "guessed 1 2\nhi\n")}) {
    EXPECT_EQ(store.parse(bad), journal_trouble::not_a_store) << bad;
    // Refused *whole*: a partly-read store is a player's transcription
    // with a hole in it, and nothing downstream could tell.
    EXPECT_EQ(store.serialize(), before);
  }
}

TEST(JournalStore, AStoreAnEditorSavedWithCrlfStillReads) {
  // A store is meant to be editable by hand, and an editor on Windows
  // writes CRLF. Every length in the format counts bytes, so without
  // normalizing the file would disagree with its own counts on every
  // record — a correct refusal, and a useless one.
  const journal_store store = Filled();
  std::string windows;
  for (const char c : store.serialize()) {
    if (c == '\n') {
      windows.push_back('\r');
    }
    windows.push_back(c);
  }

  journal_store read;
  ASSERT_EQ(read.parse(windows), journal_trouble::none);
  EXPECT_EQ(read.serialize(), store.serialize());
  EXPECT_EQ(read.fingerprint(), store.fingerprint());
}

TEST(JournalStore, AStoreFromALaterFormatIsRefusedRatherThanMisread) {
  journal_store store;
  EXPECT_EQ(store.parse("amberfolio-journal 2\nedition a\nengine b\n"),
            journal_trouble::not_a_store);
}

TEST(JournalStore, TextLongerThanTheLimitIsRefused) {
  journal_store store;
  const std::string huge(journal_max_entry_bytes + 1U, 'x');
  EXPECT_FALSE(store.record_scan(1, huge));
  EXPECT_FALSE(store.correct(1, huge));
  EXPECT_TRUE(store.empty());

  const std::string just_fits(journal_max_entry_bytes, 'x');
  EXPECT_TRUE(store.record_scan(1, just_fits));
}

TEST(JournalStore, AStoreFullOfEntriesTakesNoMore) {
  journal_store store;
  for (std::size_t i = 0; i < journal_max_entries; ++i) {
    ASSERT_TRUE(store.record_scan(static_cast<std::uint16_t>(i), "x")) << i;
  }
  EXPECT_FALSE(store.record_scan(
      static_cast<std::uint16_t>(journal_max_entries + 1U), "x"));
  // But an entry it already has is still writable, which is what a
  // re-ingestion of a full edition does on every single entry.
  EXPECT_TRUE(store.record_scan(0, "y"));
  EXPECT_EQ(store.size(), journal_max_entries);
}

TEST(JournalStore, TheTwoCountsAreWhatAHostReports) {
  journal_store store = Filled();
  EXPECT_EQ(store.recognized(), 2U);
  EXPECT_EQ(store.corrections(), 0U);

  ASSERT_TRUE(store.correct(2, "fixed"));
  EXPECT_EQ(store.recognized(), 2U);
  EXPECT_EQ(store.corrections(), 1U);

  // An entry the engine could not read is present and empty, and does not
  // count as recognized — which is the number that tells a missing engine
  // from a hard scan.
  ASSERT_TRUE(store.record_scan(5, ""));
  EXPECT_EQ(store.size(), 3U);
  EXPECT_EQ(store.recognized(), 2U);
}

TEST(JournalStore, ClearingLeavesNoHeaderBehind) {
  journal_store store = Filled();
  store.clear();
  EXPECT_TRUE(store.empty());
  EXPECT_TRUE(store.edition().empty());
  EXPECT_TRUE(store.engine().empty());
}

}  // namespace
}  // namespace amberfolio::host
