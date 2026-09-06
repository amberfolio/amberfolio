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
#include <utility>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_picture.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/platform.h"
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
  EXPECT_TRUE(store.serialize().starts_with("amberfolio-journal 4\n"));
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
  EXPECT_EQ(store.parse("amberfolio-journal 5\nedition a\nengine b\n"),
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

  // And it is written back as the current version, so a store is
  // upgraded by being opened rather than by anybody being told to do
  // anything.
  EXPECT_TRUE(store.serialize().starts_with("amberfolio-journal 4\n"));
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
// changed() — the flag a host persists on (M5-C1, #229)
// ---------------------------------------------------------------------------
//
// It was the *log's* flag until #229 and is the *store's* now, because
// the caller it exists for is a host deciding whether to write the store
// out. A correction that did not raise it is a correction that quietly
// never gets saved, and the site's alternative — serializing the whole
// store every frame and comparing it against what was last persisted —
// is a hash over everything to answer a boolean the store already knew.

TEST(JournalStoreChanged, ACorrectionRaisesItAndOnlyTheCallerLowersIt) {
  journal_store store = Filled();
  store.clear_changed();
  ASSERT_FALSE(store.changed());

  ASSERT_TRUE(store.correct(Entry(1), "a person fixed this"));
  EXPECT_TRUE(store.changed());

  // Reading it does not lower it: a flag that cleared itself on read
  // would lose a correction made between the read and the write, which is
  // exactly the window a host's persist step lives in.
  EXPECT_TRUE(store.changed());

  store.clear_changed();
  EXPECT_FALSE(store.changed());

  // And it stays down until the next write — reads of every kind leave
  // it alone.
  EXPECT_EQ(store.text(Entry(1)), "a person fixed this");
  EXPECT_NE(store.find(Entry(1)), nullptr);
  EXPECT_EQ(store.size(), 2u);
  EXPECT_EQ(store.recognized(), 2u);
  EXPECT_EQ(store.corrections(), 1u);
  EXPECT_FALSE(store.serialize().empty());
  static_cast<void>(store.fingerprint());
  EXPECT_FALSE(store.changed());

  ASSERT_TRUE(store.correct(Entry(2), "and this one too"));
  EXPECT_TRUE(store.changed());
}

TEST(JournalStoreChanged, EveryOtherWriteRaisesItToo) {
  journal_store store;

  store.set_edition(
      "2222222222222222222222222222222222222222222222222222222222222222");
  EXPECT_TRUE(store.changed());
  store.clear_changed();

  store.set_engine("test fixture 1.0");
  EXPECT_TRUE(store.changed());
  store.clear_changed();

  EXPECT_TRUE(store.record_scan(Entry(1), "as scanned"));
  EXPECT_TRUE(store.changed());
  store.clear_changed();

  const std::array<machine::journal_seen_row, 1> rows{{
      {.what = Entry(1),
       .month = 1,
       .day = 1,
       .hour = 0,
       .minute = 5,
       .read = false},
  }};
  store.set_seen(rows);
  EXPECT_TRUE(store.changed());
  store.clear_changed();

  store.clear();
  EXPECT_TRUE(store.changed());
}

TEST(JournalStoreChanged, AWriteThatWasRefusedRaisesNothing) {
  journal_store store = Filled();
  store.clear_changed();

  const std::string too_long(journal_max_entry_bytes + 1, 'x');
  EXPECT_FALSE(store.record_scan(Entry(3), too_long));
  EXPECT_FALSE(store.correct(Entry(3), too_long));
  EXPECT_FALSE(store.changed());
}

TEST(JournalStoreChanged, ReadingAStoreInIsTheOneWriteThatDoesNotRaiseIt) {
  // The bytes came *from* a host, which therefore already holds them.
  // Without this every host would write back, on startup, the file it had
  // just read — and the log's own restore has the same rule for the same
  // reason (`RestoringIsNotTheLogMoving`, above).
  const std::string text = Filled().serialize();

  journal_store store;
  ASSERT_EQ(store.parse(text), journal_trouble::none);
  EXPECT_FALSE(store.changed());

  // A refused parse leaves the store as it was, flag included.
  ASSERT_TRUE(store.correct(Entry(1), "a person fixed this"));
  ASSERT_TRUE(store.changed());
  EXPECT_NE(store.parse("not a store at all"), journal_trouble::none);
  EXPECT_TRUE(store.changed());
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

// ---------------------------------------------------------------------------
// The cheat that cites everything (#301)
// ---------------------------------------------------------------------------

/// A store the probe edition was ingested into: entries 1, 2 and 3 and
/// the tale numbered one, in the store's own order.
[[nodiscard]] journal_store probe_store() {
  journal_ingester ingester(journal_probe_table());
  EXPECT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  journal_probe_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  EXPECT_EQ(report.recognized, journal_probe_entries);
  store.clear_changed();
  return store;
}

/// One moment for the whole cite: a bulk cite is one evening.
constexpr machine::wall_time At{
    .year = 1990, .month = 3, .day = 14, .hour = 21, .minute = 5};

TEST(JournalCiteAll, TheProbeEditionIsCitedEntryOneFirstAndAllUnread) {
  journal_store store = probe_store();
  ASSERT_EQ(store.size(), journal_probe_entries);
  machine::journal_state into;

  EXPECT_EQ(cite_all_journal(into, store, At), journal_probe_entries);

  // The listing reads top down as the book does: the entries in order,
  // then the tale. `note_seen` puts each row on the front, so this is
  // the store walked backwards — the same one reversed loop
  // `restore_journal_log` has, for the same reason.
  const std::span<const machine::journal_seen_row> got = into.seen();
  ASSERT_EQ(got.size(), 4u);
  EXPECT_EQ(got[0].what, Entry(1));
  EXPECT_EQ(got[1].what, Entry(2));
  EXPECT_EQ(got[2].what, Entry(3));
  EXPECT_EQ(got[3].what, Tale(1));
  for (const machine::journal_seen_row& row : got) {
    EXPECT_FALSE(row.read) << "a cited row is a to-do, not a done";
    EXPECT_EQ(row.month, At.month);
    EXPECT_EQ(row.day, At.day);
    EXPECT_EQ(row.hour, At.hour);
    EXPECT_EQ(row.minute, At.minute);
  }
}

TEST(JournalCiteAll, TheStoreGetsTheSameLogThroughTheSameWrite) {
  // What makes it survive a reload: the rows go into the store's own log
  // through `set_seen`, which is the `journal_seen` service's write, and
  // that raises the store's flag so a host writes the file — while the
  // machine's own flag comes down, because the host now has it.
  journal_store store = probe_store();
  machine::journal_state into;
  static_cast<void>(cite_all_journal(into, store, At));

  EXPECT_TRUE(store.changed());
  EXPECT_FALSE(into.seen_changed());
  ASSERT_EQ(store.seen().size(), into.seen().size());
  for (std::size_t i = 0; i < store.seen().size(); ++i) {
    EXPECT_EQ(store.seen()[i].what, into.seen()[i].what) << "row " << i;
  }
  // And it round-trips as `seen` lines, Entry 1 first.
  journal_store back;
  ASSERT_EQ(back.parse(store.serialize()), journal_trouble::none);
  ASSERT_EQ(back.seen().size(), 4u);
  EXPECT_EQ(back.seen().front().what, Entry(1));
  EXPECT_EQ(back.seen().back().what, Tale(1));
}

TEST(JournalCiteAll, ASecondCallNeitherDoublesNorForgetsWhatWasRead) {
  journal_store store = probe_store();
  machine::journal_state into;
  static_cast<void>(cite_all_journal(into, store, At));
  ASSERT_TRUE(into.mark_seen_read(Entry(2)));

  const machine::wall_time later{
      .year = 1990, .month = 3, .day = 15, .hour = 9, .minute = 30};
  EXPECT_EQ(cite_all_journal(into, store, later), journal_probe_entries);

  const std::span<const machine::journal_seen_row> got = into.seen();
  ASSERT_EQ(got.size(), 4u) << "citing twice doubled the log";
  EXPECT_EQ(got[0].what, Entry(1));
  EXPECT_EQ(got[1].what, Entry(2));
  EXPECT_EQ(got[2].what, Entry(3));
  EXPECT_EQ(got[3].what, Tale(1));
  EXPECT_TRUE(got[1].read) << "a second cite unread what a person had read";
  EXPECT_FALSE(got[0].read);
  EXPECT_EQ(got[0].day, later.day) << "a re-cite re-dates, as the game's does";
}

TEST(JournalCiteAll, WhatTheGameCitedStaysUnderneath) {
  // Clears nothing: a row the game cited that is not in the store — a
  // number the engine read nothing for, say — is still on the log, below
  // the cited ones, with its own date and its own read flag.
  journal_store store = probe_store();
  machine::journal_state into;
  into.note_seen(Entry(7), 1, 1, 0, 5);
  ASSERT_TRUE(into.mark_seen_read(Entry(7)));

  EXPECT_EQ(cite_all_journal(into, store, At), journal_probe_entries);

  const std::span<const machine::journal_seen_row> got = into.seen();
  ASSERT_EQ(got.size(), 5u);
  EXPECT_EQ(got[0].what, Entry(1));
  EXPECT_EQ(got[4].what, Entry(7));
  EXPECT_TRUE(got[4].read);
  EXPECT_EQ(got[4].minute, 5);
}

TEST(JournalCiteAll, AnEmptyStoreCitesNothingAndTouchesNothing) {
  // The reader's own "you have not ingested a journal" is the answer for
  // a player with no store, not an empty log and not a file written.
  journal_store store;
  machine::journal_state into;
  into.note_seen(Entry(7), 1, 1, 0, 5);
  into.set_seen_changed(false);

  EXPECT_EQ(cite_all_journal(into, store, At), 0u);
  EXPECT_EQ(into.seen().size(), 1u);
  EXPECT_FALSE(into.seen_changed());
  EXPECT_FALSE(store.changed());
  EXPECT_TRUE(store.seen().empty());
}

TEST(JournalCiteAll, AWholeEditionFitsInTheLog) {
  // The cap was sixty-four and a real edition is ninety-nine sections
  // (#232: fifty-eight entries, twenty-three tales, eighteen
  // proclamations numbered 59-214 with gaps), so citing everything used
  // to drop the last thirty-five off the end — which defeats the purpose.
  // The store here has that edition's shape and none of its words.
  journal_store store;
  store.set_edition(
      "2222222222222222222222222222222222222222222222222222222222222222");
  for (std::uint16_t i = 1; i <= 58; ++i) {
    ASSERT_TRUE(store.record_scan(Entry(i), "an entry, as scanned"));
  }
  for (std::uint16_t i = 1; i <= 23; ++i) {
    ASSERT_TRUE(store.record_scan(Tale(i), "a tale, as scanned"));
  }
  constexpr std::array<std::uint16_t, 18> proclamations{
      59,  64,  71,  78,  84,  90,  97,  103, 109,
      116, 122, 128, 135, 141, 147, 154, 160, 214};
  for (const std::uint16_t number : proclamations) {
    ASSERT_TRUE(
        store.record_scan(Proclamation(number), "a proclamation, as scanned"));
  }
  ASSERT_EQ(store.size(), 99u);
  static_assert(machine::journal_log_rows >= 99,
                "the log has to hold a whole edition for the cheat to mean"
                " anything");

  machine::journal_state into;
  EXPECT_EQ(cite_all_journal(into, store, At), 99u);
  ASSERT_EQ(into.seen().size(), 99u) << "the oldest fell off the end";
  EXPECT_EQ(into.seen().front().what, Entry(1));
  EXPECT_EQ(into.seen()[57].what, Entry(58));
  EXPECT_EQ(into.seen()[58].what, Tale(1));
  EXPECT_EQ(into.seen()[81].what, Proclamation(59));
  EXPECT_EQ(into.seen().back().what, Proclamation(214));
  // And the store kept all of it, since its cap is the machine's.
  EXPECT_EQ(store.seen().size(), 99u);
}

// --- the pictures (#328) ---------------------------------------------------

/// A picture whose bytes are the shape it claims, built here rather than
/// reduced, so these tests are about the *store* and not about the
/// reducer.
journal_picture Drawing(machine::journal_citation what, std::uint8_t nth,
                        std::uint16_t width, std::uint16_t height,
                        std::uint8_t fill = 0x1B) {
  journal_picture one;
  one.what = what;
  one.nth = nth;
  one.width = width;
  one.height = height;
  one.levels.assign(machine::journal_art_stride(width) * height, fill);
  return one;
}

TEST(JournalStorePictures, ARoundTripKeepsEveryPixel) {
  journal_store store = Filled();
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 0, 40, 12)));
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 1, 7, 3, 0x2D)));
  ASSERT_TRUE(store.record_picture(Drawing(
      Tale(2), 0, machine::journal_art_width, machine::journal_art_height)));
  EXPECT_EQ(store.picture_count(), 3U);

  journal_store read;
  ASSERT_EQ(read.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(read.picture_count(), 3U);
  EXPECT_EQ(read.serialize(), store.serialize());
  EXPECT_EQ(read.fingerprint(), store.fingerprint());

  ASSERT_EQ(read.pictures(Entry(1)).size(), 2U);
  EXPECT_EQ(read.pictures(Entry(1))[1].width, 7);
  EXPECT_EQ(read.pictures(Entry(1))[1].levels,
            store.pictures(Entry(1))[1].levels);
  EXPECT_TRUE(read.pictures(Entry(2)).empty());
  ASSERT_NE(read.picture(Tale(2), 0), nullptr);
  EXPECT_EQ(read.picture(Tale(2), 0)->height, machine::journal_art_height);
  EXPECT_EQ(read.picture(Tale(2), 1), nullptr);
}

TEST(JournalStorePictures, TheRecordSaysWhatItIs) {
  // A person opens this file, so the record has to be legible even
  // though its body is not: the section, the number, which picture and
  // its shape are all words and decimals.
  journal_store store = Filled();
  ASSERT_TRUE(store.record_picture(Drawing(Tale(3), 2, 8, 4)));
  EXPECT_NE(store.serialize().find("picture tale 3 2 8 4 "), std::string::npos);
}

TEST(JournalStorePictures, ASecondWriteReplacesRatherThanDoubles) {
  journal_store store = Filled();
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 0, 40, 12, 0x00)));
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 0, 20, 6, 0xFF)));
  ASSERT_EQ(store.picture_count(), 1U);
  EXPECT_EQ(store.picture(Entry(1), 0)->width, 20);
}

TEST(JournalStorePictures, APictureThatDoesNotAddUpIsRefused) {
  journal_store store;
  // A shape the reader has no room for.
  EXPECT_FALSE(store.record_picture(
      Drawing(Entry(1), 0, machine::journal_art_width + 1, 4)));
  EXPECT_FALSE(store.record_picture(
      Drawing(Entry(1), 0, 4, machine::journal_art_height + 1)));
  // Nothing at all.
  EXPECT_FALSE(store.record_picture(Drawing(Entry(1), 0, 0, 4)));
  // A citation that names nothing.
  EXPECT_FALSE(store.record_picture(Drawing(Entry(0), 0, 4, 4)));
  // More pictures than one entry may have.
  EXPECT_FALSE(store.record_picture(
      Drawing(Entry(1), machine::journal_art_per_entry, 4, 4)));
  // Bytes that are not the count the shape implies.
  journal_picture lying = Drawing(Entry(1), 0, 40, 12);
  lying.levels.pop_back();
  EXPECT_FALSE(store.record_picture(std::move(lying)));
  EXPECT_EQ(store.picture_count(), 0U);
}

TEST(JournalStorePictures, ARecordThatIsNotOneRefusesTheWholeStore) {
  journal_store store = Filled();
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 0, 8, 4)));
  const std::string before = store.serialize();
  for (const std::string& bad :
       {// a body that is not base64 at all
        std::string("amberfolio-journal 4\nedition a\nengine b\n"
                    "picture entry 1 0 8 4 4\n????\n"),
        // base64 that decodes to the wrong number of bytes for the shape
        std::string("amberfolio-journal 4\nedition a\nengine b\n"
                    "picture entry 1 0 8 4 4\nAAAA\n"),
        // a field short
        std::string("amberfolio-journal 4\nedition a\nengine b\n"
                    "picture entry 1 0 8 4\nAAAA\n"),
        // a section no build has ever written
        std::string("amberfolio-journal 4\nedition a\nengine b\n"
                    "picture rumour 1 0 8 4 4\nAAAA\n")}) {
    journal_store read = store;
    EXPECT_EQ(read.parse(bad), journal_trouble::not_a_store) << bad;
    EXPECT_EQ(read.serialize(), before) << "left as it was";
  }
}

TEST(JournalStorePictures, AVersionThreeStoreIsAPlayerWithNoDrawingsYet) {
  // Not an error: a store written before this build could make one. The
  // fix is a re-ingestion, which is what re-ingesting is for.
  journal_store store;
  ASSERT_EQ(store.parse("amberfolio-journal 3\nedition a\nengine b\n"
                        "scanned entry 4 6\nfourth\n"),
            journal_trouble::none);
  EXPECT_EQ(store.picture_count(), 0U);
  // And a `picture` record in a store that claims to be version 3 is a
  // file somebody edited into something this cannot read.
  EXPECT_EQ(store.parse("amberfolio-journal 3\nedition a\nengine b\n"
                        "picture entry 1 0 8 4 4\nAAAA\n"),
            journal_trouble::not_a_store);
}

TEST(JournalStorePictures, ClearingAndAChangedEditionTakeThemToo) {
  journal_store store = Filled();
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 0, 8, 4)));
  store.clear_changed();
  ASSERT_TRUE(store.record_picture(Drawing(Entry(1), 1, 8, 4)));
  EXPECT_TRUE(store.changed()) << "a picture is a write like any other";
  store.clear();
  EXPECT_EQ(store.picture_count(), 0U);
}

}  // namespace
}  // namespace amberfolio::host
