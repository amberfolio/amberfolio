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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

/// A citation, spelled out. The store's key is a *pair* since #218 and
/// these tests say which section they mean rather than relying on a
/// default — which is the whole point of the change and would be a poor
/// thing for its own tests to lean on.
constexpr machine::journal_citation Entry(std::uint16_t number) {
  return {.kind = journal_kind::entry, .number = number};
}

constexpr machine::journal_citation Tale(std::uint16_t number) {
  return {.kind = journal_kind::tale, .number = number};
}

constexpr machine::journal_citation Proclamation(std::uint16_t number) {
  return {.kind = journal_kind::proclamation, .number = number};
}

journal_store Filled() {
  journal_store store;
  store.set_edition(
      "1111111111111111111111111111111111111111111111111111111111111111");
  store.set_engine("test fixture 1.0");
  EXPECT_TRUE(store.record_scan(Entry(2), "second entry, as scanned"));
  EXPECT_TRUE(store.record_scan(Entry(1), "first entry, as scanned"));
  return store;
}

TEST(JournalStore, ACorrectionIsWhatTheReaderShows) {
  journal_store store = Filled();
  EXPECT_EQ(store.text(Entry(1)), "first entry, as scanned");
  ASSERT_TRUE(store.correct(Entry(1), "first entry, as a person fixed it"));
  EXPECT_EQ(store.text(Entry(1)), "first entry, as a person fixed it");

  // And the scan is still there underneath, which is what makes a
  // re-ingestion able to improve it without destroying the fix.
  const journal_text* entry = store.find(Entry(1));
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->scanned, "first entry, as scanned");
  EXPECT_EQ(entry->corrected, "first entry, as a person fixed it");
}

TEST(JournalStore, IngestionReplacesTheScanAndNeverTheCorrection) {
  // #174's "a player can fix an OCR error and the fix survives
  // re-ingestion", at the one place it is actually decided.
  journal_store store = Filled();
  ASSERT_TRUE(store.correct(Entry(1), "what a person wrote"));
  ASSERT_TRUE(store.record_scan(Entry(1), "what a better engine read"));

  const journal_text* entry = store.find(Entry(1));
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->scanned, "what a better engine read");
  EXPECT_EQ(entry->corrected, "what a person wrote");
  EXPECT_EQ(store.text(Entry(1)), "what a person wrote");
}

TEST(JournalStore, AnEntryNobodyHasIsEmptyRatherThanAbsent) {
  const journal_store store = Filled();
  EXPECT_EQ(store.find(Entry(99)), nullptr);
  EXPECT_TRUE(store.text(Entry(99)).empty());
}

TEST(JournalStore, TheRoundTripIsExact) {
  journal_store store = Filled();
  ASSERT_TRUE(store.correct(Entry(2), "a correction\nwith a newline in it"));

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(read.edition(), store.edition());
  EXPECT_EQ(read.engine(), store.engine());
  ASSERT_EQ(read.size(), store.size());
  EXPECT_EQ(read.text(Entry(1)), store.text(Entry(1)));
  EXPECT_EQ(read.text(Entry(2)), store.text(Entry(2)));
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
  ASSERT_TRUE(store.record_scan(Entry(3),
                                "scanned entry 4 8\nfake\ncorrected entry 5 "
                                "1\nx\namberfolio-journal 2\n"));

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(read.size(), 1U);
  EXPECT_EQ(read.text(Entry(3)), store.text(Entry(3)));
}

TEST(JournalStore, EntriesComeOutSortedSoAFingerprintMeansSomething) {
  journal_store a;
  a.set_edition("e");
  a.set_engine("g");
  ASSERT_TRUE(a.record_scan(Entry(7), "seven"));
  ASSERT_TRUE(a.record_scan(Entry(3), "three"));

  journal_store b;
  b.set_edition("e");
  b.set_engine("g");
  ASSERT_TRUE(b.record_scan(Entry(3), "three"));
  ASSERT_TRUE(b.record_scan(Entry(7), "seven"));

  // Same content written in two orders is one file, which is what makes
  // `fingerprint()` worth reporting on an issue.
  EXPECT_EQ(a.serialize(), b.serialize());
  EXPECT_EQ(a.fingerprint(), b.fingerprint());
}

TEST(JournalStore, TheThreeSectionsAreKeptApartAndComeOutInBlocks) {
  // #218's whole reason, and the order it settles on. The same number in
  // three sections is three rows; the store sorts by section and then by
  // number, so a serialized store reads as three blocks rather than as
  // an interleaving nobody asked for.
  journal_store store;
  store.set_edition("e");
  store.set_engine("g");
  ASSERT_TRUE(store.record_scan(Proclamation(4), "a proclamation"));
  ASSERT_TRUE(store.record_scan(Tale(4), "a tale"));
  ASSERT_TRUE(store.record_scan(Entry(4), "an entry"));
  EXPECT_EQ(store.size(), 3U);

  EXPECT_EQ(store.text(Entry(4)), "an entry");
  EXPECT_EQ(store.text(Tale(4)), "a tale");
  EXPECT_EQ(store.text(Proclamation(4)), "a proclamation");

  const std::string text = store.serialize();
  const std::size_t entry = text.find("scanned entry 4 ");
  const std::size_t tale = text.find("scanned tale 4 ");
  const std::size_t proclamation = text.find("scanned proclamation 4 ");
  ASSERT_NE(proclamation, std::string::npos);
  EXPECT_LT(entry, tale);
  EXPECT_LT(tale, proclamation);

  journal_store read;
  ASSERT_EQ(read.parse(text), journal_trouble::none);
  EXPECT_EQ(read.text(Proclamation(4)), "a proclamation");
  EXPECT_EQ(read.fingerprint(), store.fingerprint());
}

TEST(JournalStore, TheLogSurvivesTheRoundTripInItsOwnOrder) {
  // The log is a log: its order is its content, not an artefact of what
  // was written first, so it comes back exactly as it went in.
  journal_store store;
  store.set_edition("e");
  store.set_engine("g");
  const std::array<machine::journal_seen_row, 3> rows{
      {{.what = Proclamation(109),
        .month = 8,
        .day = 29,
        .hour = 22,
        .minute = 19,
        .read = false},
       {.what = Tale(12),
        .month = 8,
        .day = 29,
        .hour = 21,
        .minute = 44,
        .read = true},
       {.what = Entry(3),
        .month = 8,
        .day = 29,
        .hour = 20,
        .minute = 15,
        .read = true}}};
  store.set_seen(rows);
  ASSERT_EQ(store.seen().size(), 3u);

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  ASSERT_EQ(read.seen().size(), 3u);
  for (std::size_t i = 0; i < rows.size(); ++i) {
    EXPECT_EQ(read.seen()[i].what, rows[i].what) << i;
    EXPECT_EQ(read.seen()[i].month, rows[i].month) << i;
    EXPECT_EQ(read.seen()[i].day, rows[i].day) << i;
    EXPECT_EQ(read.seen()[i].hour, rows[i].hour) << i;
    EXPECT_EQ(read.seen()[i].minute, rows[i].minute) << i;
    EXPECT_EQ(read.seen()[i].read, rows[i].read) << i;
  }
  EXPECT_EQ(read.fingerprint(), store.fingerprint());
}

TEST(JournalStore, AVersionTwoStoreIsAPlayerNothingHasCitedYet) {
  journal_store store;
  ASSERT_EQ(store.parse("amberfolio-journal 2\nedition a\nengine b\n"
                        "scanned entry 4 6\nfourth\n"),
            journal_trouble::none);
  EXPECT_EQ(store.size(), 1u);
  EXPECT_TRUE(store.seen().empty()) << "no log is not a broken store";
  EXPECT_TRUE(store.serialize().starts_with("amberfolio-journal 3\n"));
}

TEST(JournalStore, ALogLineThatIsNotOneIsRefusedWhole) {
  journal_store store = Filled();
  const std::string before = store.serialize();
  for (const std::string& bad :
       {// a kind no build has ever written
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "seen rumour 1 8 29 22 19 0\n"),
        // a field short
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "seen entry 1 8 29 22 0\n"),
        // number zero names nothing
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "seen entry 0 8 29 22 19 0\n")}) {
    EXPECT_EQ(store.parse(bad), journal_trouble::not_a_store) << bad;
    EXPECT_EQ(store.serialize(), before);
  }
}

TEST(JournalStore, TheLogIsCappedAtWhatAReaderCouldShow) {
  journal_store store;
  std::vector<machine::journal_seen_row> many;
  for (std::uint16_t i = 1; i <= machine::journal_log_rows + 10; ++i) {
    many.push_back({.what = Entry(i), .month = 8, .day = 30, .hour = 10});
  }
  store.set_seen(many);
  EXPECT_EQ(store.seen().size(), machine::journal_log_rows);
  EXPECT_EQ(store.seen().front().what, Entry(1)) << "the front is kept";
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
        std::string("amberfolio-journal 3\n"),
        std::string("amberfolio-journal 3\nedition a\n"),
        // A record whose body is shorter than its length says, which is
        // what a truncated write leaves behind.
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "scanned entry 1 40\nshort\n"),
        // A kind no build has ever written.
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "scanned rumour 1 2\nhi\n"),
        // A version 2 record wearing version 1's shape.
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "scanned 1 2\nhi\n"),
        // A keyword that is not one of the two.
        std::string("amberfolio-journal 3\nedition a\nengine b\n"
                    "guessed entry 1 2\nhi\n")}) {
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
  EXPECT_EQ(store.parse("amberfolio-journal 4\nedition a\nengine b\n"),
            journal_trouble::not_a_store);
}

TEST(JournalStore, AStoreFromVersionOneIsReadRatherThanThrownAway) {
  // Version 1 had no kind on its records because there was one
  // section, so every record in one is a journal entry and reading it
  // as such loses nothing. Refusing it would have thrown a player
  // away their corrections to make a point.
  journal_store store;
  ASSERT_EQ(store.parse("amberfolio-journal 1\nedition a\nengine b\n"
                        "scanned 4 6\nfourth\n"
                        "corrected 4 5\nfixed\n"),
            journal_trouble::none);
  EXPECT_EQ(store.size(), 1U);
  EXPECT_EQ(store.text(Entry(4)), "fixed");
  EXPECT_TRUE(store.text(Tale(4)).empty());

  // And it is written back as version 2, so a store is upgraded by
  // being opened rather than by anybody being told to do anything.
  EXPECT_TRUE(store.serialize().starts_with("amberfolio-journal 3\n"));
  EXPECT_NE(store.serialize().find("scanned entry 4 6\n"), std::string::npos);
}

TEST(JournalStore, TextLongerThanTheLimitIsRefused) {
  journal_store store;
  const std::string huge(journal_max_entry_bytes + 1U, 'x');
  EXPECT_FALSE(store.record_scan(Entry(1), huge));
  EXPECT_FALSE(store.correct(Entry(1), huge));
  EXPECT_TRUE(store.empty());

  const std::string just_fits(journal_max_entry_bytes, 'x');
  EXPECT_TRUE(store.record_scan(Entry(1), just_fits));
}

TEST(JournalStore, AStoreFullOfEntriesTakesNoMore) {
  journal_store store;
  for (std::size_t i = 0; i < journal_max_entries; ++i) {
    ASSERT_TRUE(store.record_scan(Entry(static_cast<std::uint16_t>(i)), "x"))
        << i;
  }
  EXPECT_FALSE(store.record_scan(
      Entry(static_cast<std::uint16_t>(journal_max_entries + 1U)), "x"));
  // But an entry it already has is still writable, which is what a
  // re-ingestion of a full edition does on every single entry.
  EXPECT_TRUE(store.record_scan(Entry(0), "y"));
  EXPECT_EQ(store.size(), journal_max_entries);
}

TEST(JournalStore, TheTwoCountsAreWhatAHostReports) {
  journal_store store = Filled();
  EXPECT_EQ(store.recognized(), 2U);
  EXPECT_EQ(store.corrections(), 0U);

  ASSERT_TRUE(store.correct(Entry(2), "fixed"));
  EXPECT_EQ(store.recognized(), 2U);
  EXPECT_EQ(store.corrections(), 1U);

  // An entry the engine could not read is present and empty, and does not
  // count as recognized — which is the number that tells a missing engine
  // from a hard scan.
  ASSERT_TRUE(store.record_scan(Entry(5), ""));
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

// ---------------------------------------------------------------------------
// The read log, back into the machine (STO-4, #237)
// ---------------------------------------------------------------------------

/// A store holding three cited things, newest first, with the middle one
/// read — the shape a real store has after a session.
[[nodiscard]] journal_store three_seen() {
  journal_store store;
  // Newest first, which is the order a store holds and hands back, and
  // the order the machine's own log is in. The middle one has been read.
  const std::array<machine::journal_seen_row, 3> rows{{
      {.what = {.kind = machine::journal_kind::proclamation, .number = 64},
       .month = 1,
       .day = 1,
       .hour = 0,
       .minute = 7,
       .read = false},
      {.what = {.kind = machine::journal_kind::tale, .number = 3},
       .month = 1,
       .day = 1,
       .hour = 0,
       .minute = 6,
       .read = true},
      {.what = {.kind = machine::journal_kind::entry, .number = 7},
       .month = 1,
       .day = 1,
       .hour = 0,
       .minute = 5,
       .read = false},
  }};
  store.set_seen(rows);
  store.clear_changed();
  return store;
}

TEST(JournalLogRestore, TheMachineGetsTheStoresLogInTheStoresOrder) {
  // The bug this exists for is invisible to anything that only counts
  // rows: the store holds the log newest first and so does the machine,
  // and `note_seen` puts each row on the *front* — so feeding them in
  // stored order hands the reader its own list upside down. That is one
  // reversed loop in eight lines, and it was written twice.
  const journal_store store = three_seen();
  ASSERT_EQ(store.seen().size(), 3u);

  machine::journal_state into;
  restore_journal_log(into, store);

  const std::span<const machine::journal_seen_row> got = into.seen();
  ASSERT_EQ(got.size(), 3u);
  for (std::size_t i = 0; i < got.size(); ++i) {
    EXPECT_EQ(got[i].what, store.seen()[i].what) << "row " << i;
    EXPECT_EQ(got[i].month, store.seen()[i].month) << "row " << i;
    EXPECT_EQ(got[i].day, store.seen()[i].day) << "row " << i;
    EXPECT_EQ(got[i].hour, store.seen()[i].hour) << "row " << i;
    EXPECT_EQ(got[i].minute, store.seen()[i].minute) << "row " << i;
  }
}

TEST(JournalLogRestore, TheStarComesBackOffWhatWasAlreadyRead) {
  // The half a player sees. Without it every entry they had opened is
  // unread again on the next run, which is what a browser did until #237.
  const journal_store store = three_seen();
  machine::journal_state into;
  restore_journal_log(into, store);

  const std::span<const machine::journal_seen_row> got = into.seen();
  ASSERT_EQ(got.size(), 3u);
  for (const machine::journal_seen_row& row : got) {
    const bool wanted = row.what.kind == machine::journal_kind::tale;
    EXPECT_EQ(row.read, wanted) << "the tale was the one that had been opened";
  }
}

TEST(JournalLogRestore, RestoringIsNotTheLogMoving) {
  // A host writes its store back when the log changes. Restoring what the
  // store already holds must not look like a change, or every run would
  // rewrite the file it had just read.
  const journal_store store = three_seen();
  machine::journal_state into;
  restore_journal_log(into, store);
  EXPECT_FALSE(into.seen_changed());

  // And a real citation afterwards still does look like one.
  into.note_seen({.kind = machine::journal_kind::entry, .number = 9}, 1, 1, 0,
                 8);
  EXPECT_TRUE(into.seen_changed());
}

TEST(JournalLogRestore, AnEmptyStoreLeavesAnEmptyLog) {
  const journal_store store;
  machine::journal_state into;
  restore_journal_log(into, store);
  EXPECT_TRUE(into.seen().empty());
  EXPECT_FALSE(into.seen_changed());
}

}  // namespace
}  // namespace amberfolio::host
