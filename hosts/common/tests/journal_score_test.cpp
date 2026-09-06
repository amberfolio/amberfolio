// SPDX-License-Identifier: AGPL-3.0-only
//
// How well an ingestion went, measured (journal_score.h, #315).
//
// Two halves, and they answer different questions.
//
// The first is the arithmetic: a handful of strings written here, with
// the edit distance between them worked out by hand. Nothing in it came
// out of an engine and nothing in it is anybody's journal — the strings
// are this file's own, chosen so that the answer is obvious enough to
// argue with.
//
// The second drives the whole ingestion with `journal_probe_noisy_ocr`,
// which reads the probe document correctly and then puts three known
// characters on the end of every answer. That makes the error rate the
// harness *must* report a fact about `journal_probe_text()`'s lengths
// rather than a number somebody observed once — which is the only kind of
// end-to-end assertion worth making about a measurement.

#include "amberfolio/host/journal_score.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

TEST(JournalNormalize, CollapsesEveryRunOfWhitespaceAndTrimsTheEnds) {
  // The one thing normalization does, and the reason it does it: the
  // scan's line breaks are the column's and not the entry's, so a player
  // who retyped an entry as one flowing paragraph must not score worse
  // than the engine did.
  EXPECT_EQ(journal_normalize("  one\n\ttwo \r\n three  \n"), "one two three");
  EXPECT_EQ(journal_normalize(""), "");
  EXPECT_EQ(journal_normalize("   \n\t  "), "");
  EXPECT_EQ(journal_normalize("alone"), "alone");
}

TEST(JournalNormalize, KeepsCaseAndPunctuation) {
  // Both measured before they were decided (journal_score.h): folding
  // case moved the rate by four parts in a thousand on real entries, and
  // an apostrophe read as a double quote is the commonest error left in a
  // good reading — a metric that could not see it would call an engine
  // that made them all perfect.
  EXPECT_EQ(journal_normalize("The Keep's wall."), "The Keep's wall.");
  EXPECT_NE(journal_character_score("kobolds", "Kobolds").distance, 0U);
  EXPECT_NE(journal_character_score("'The", "\"The").distance, 0U);
}

TEST(JournalEditDistance, IsTheOrdinaryOne) {
  EXPECT_EQ(journal_edit_distance("", ""), 0U);
  EXPECT_EQ(journal_edit_distance("abc", "abc"), 0U);
  EXPECT_EQ(journal_edit_distance("", "abc"), 3U);
  EXPECT_EQ(journal_edit_distance("abc", ""), 3U);
  // One substitution, one deletion, one insertion.
  EXPECT_EQ(journal_edit_distance("abc", "abd"), 1U);
  EXPECT_EQ(journal_edit_distance("abc", "ac"), 1U);
  EXPECT_EQ(journal_edit_distance("abc", "abxc"), 1U);
  // The textbook one.
  EXPECT_EQ(journal_edit_distance("kitten", "sitting"), 3U);
}

TEST(JournalScore, RatesAgainstTheTruthAndNotTheOtherWayRound) {
  // The denominator is the length of the *truth*, so an engine that read
  // nothing scores 1.0 and an engine that read twice as much scores over
  // it. Neither is clamped: reading the neighbouring column is the one
  // failure a whole-page reading has, and a metric that capped at 1 would
  // hide how far past the entry it went.
  const journal_score nothing = journal_character_score("abcd", "");
  EXPECT_TRUE(nothing.taken);
  EXPECT_EQ(nothing.reference, 4U);
  EXPECT_DOUBLE_EQ(nothing.rate(), 1.0);

  const journal_score too_much = journal_character_score("ab", "abcdef");
  EXPECT_DOUBLE_EQ(too_much.rate(), 2.0);
}

TEST(JournalScore, WithNoTruthNothingIsTaken) {
  // Not a zero rate, which would read as a perfect transcription.
  const journal_score none = journal_character_score("", "anything");
  EXPECT_FALSE(none.taken);
  EXPECT_DOUBLE_EQ(none.rate(), 0.0);
}

TEST(JournalScore, RefusesAPairItWouldTakeForeverOn) {
  // journal_score.h's one refusal: a store may legally hold a 64 KiB
  // entry and the comparison is quadratic, so beyond the limit the score
  // is not taken rather than approximated. A truncated comparison
  // reported as a rate is a number that looks like a measurement.
  const std::string huge(journal_score_limit + 1U, 'a');
  const journal_score refused = journal_character_score(huge, huge);
  EXPECT_FALSE(refused.taken);
  // And one character under it is fine.
  const std::string big(journal_score_limit, 'a');
  EXPECT_TRUE(journal_character_score(big, big).taken);
}

TEST(JournalScore, TheWordRateCountsWordsAndNotCharacters) {
  // Four words, one of them wrong: a quarter by word, and much less than
  // a quarter by character. The two say different things on purpose.
  const std::string_view truth = "the wall was erected";
  const std::string_view read = "the wall wns erected";
  const journal_score words = journal_word_score(truth, read);
  EXPECT_EQ(words.reference, 4U);
  EXPECT_EQ(words.distance, 1U);
  EXPECT_DOUBLE_EQ(words.rate(), 0.25);
  EXPECT_LT(journal_character_score(truth, read).rate(), words.rate());
}

TEST(JournalStoreScore, OnlyCorrectedItemsAreMeasuredAndScoredAgainstTheFix) {
  // The whole design in one case: the ground truth is the player's own
  // correction, so an item nobody has corrected is not in the result at
  // all. Counting it as a perfect zero would make the aggregate *improve*
  // every time a player found another mistake, which is the one way this
  // measurement could have been made actively misleading.
  journal_store store;
  constexpr machine::journal_citation one{.kind = journal_kind::entry,
                                          .number = 1};
  constexpr machine::journal_citation two{.kind = journal_kind::entry,
                                          .number = 2};
  ASSERT_TRUE(store.record_scan(one, "the wall wns erected"));
  ASSERT_TRUE(store.record_scan(two, "nobody has checked this one"));
  ASSERT_TRUE(store.correct(one, "the wall was erected"));

  const journal_store_report report = score_journal_store(store);
  ASSERT_EQ(report.items.size(), 1U);
  EXPECT_EQ(report.items.front().what, one);
  EXPECT_EQ(report.items.front().characters.distance, 1U);
  EXPECT_EQ(report.items.front().characters.reference, 20U);
  EXPECT_EQ(report.refused, 0U);
  // The aggregate is the one item, since it is the only one measurable.
  EXPECT_TRUE(report.characters.taken);
  EXPECT_EQ(report.characters.distance, 1U);
  EXPECT_EQ(report.characters.reference, 20U);
}

TEST(JournalStoreScore, TheAggregateIsARatioOfSumsAndNotAMeanOfRates) {
  // The two differ, and the difference is the point: a short item read
  // badly must not weigh as much as a long one read well. Here a
  // four-character item is entirely wrong and a forty-character item is
  // perfect. The mean of the rates is 0.5; the rate a reader meets is
  // 4/44.
  journal_store store;
  constexpr machine::journal_citation small{.kind = journal_kind::entry,
                                            .number = 1};
  constexpr machine::journal_citation large{.kind = journal_kind::entry,
                                            .number = 2};
  const std::string forty(40, 'a');
  ASSERT_TRUE(store.record_scan(small, "zzzz"));
  ASSERT_TRUE(store.correct(small, "abcd"));
  ASSERT_TRUE(store.record_scan(large, forty));
  ASSERT_TRUE(store.correct(large, forty));

  const journal_store_report report = score_journal_store(store);
  ASSERT_EQ(report.items.size(), 2U);
  EXPECT_EQ(report.characters.distance, 4U);
  EXPECT_EQ(report.characters.reference, 44U);
  EXPECT_DOUBLE_EQ(report.characters.rate(), 4.0 / 44.0);
  EXPECT_NE(report.characters.rate(), 0.5);
}

TEST(JournalStoreScore, AnItemTooLongToMeasureIsCountedRatherThanDropped) {
  journal_store store;
  constexpr machine::journal_citation huge{.kind = journal_kind::entry,
                                           .number = 1};
  const std::string enormous(journal_score_limit + 1U, 'a');
  ASSERT_TRUE(store.record_scan(huge, enormous));
  ASSERT_TRUE(store.correct(huge, enormous));

  const journal_store_report report = score_journal_store(store);
  EXPECT_TRUE(report.items.empty());
  EXPECT_EQ(report.refused, 1U);
  EXPECT_FALSE(report.characters.taken);
}

// --- End to end, over the probe --------------------------------------

/// What the noisy fixture must produce for entry `index`: the right
/// answer with the noise on the end.
[[nodiscard]] std::string noisy(std::size_t index) {
  return std::string(journal_probe_text(index)) +
         std::string(journal_probe_noise);
}

TEST(JournalScoreOverTheProbe, TheHarnessReportsTheErrorTheFixtureMade) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_probe_noisy_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  ASSERT_EQ(report.recognized, journal_probe_entries);

  // Every item corrected to what the fixture *should* have read, which is
  // exactly what a player does when they fix an entry — and is what makes
  // the store scoreable at all.
  std::size_t characters = 0;
  for (std::size_t index = 0; index < journal_probe_entries; ++index) {
    const journal_entry_fact* fact = ingester.entry_at(index);
    ASSERT_NE(fact, nullptr);
    const machine::journal_citation what{.kind = fact->kind,
                                         .number = fact->number};
    ASSERT_EQ(store.text(what), noisy(index));
    ASSERT_TRUE(store.correct(what, journal_probe_text(index)));
    characters += journal_probe_text(index).size();
  }

  const journal_store_report scored = score_journal_store(store);
  ASSERT_EQ(scored.items.size(), journal_probe_entries);
  for (const journal_item_score& item : scored.items) {
    // Three characters and one word, exactly, for the reason
    // `journal_probe_noise` is an append (journal_probe.h).
    EXPECT_EQ(item.characters.distance, journal_probe_noise_edits);
    EXPECT_EQ(item.words.distance, journal_probe_noise_word_edits);
  }
  EXPECT_EQ(scored.characters.distance,
            journal_probe_noise_edits * journal_probe_entries);
  EXPECT_EQ(scored.characters.reference, characters);
  EXPECT_EQ(scored.words.distance,
            journal_probe_noise_word_edits * journal_probe_entries);
}

TEST(JournalScoreOverTheProbe, AnIngestionCarriesWhatTheEngineWasSureOf) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_probe_noisy_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  ASSERT_EQ(report.quality.size(), journal_probe_entries);

  // The fixture's confidences rise with each reading, so this is also
  // where the aggregate's weighting is checked: every reading here has
  // the same number of words, so the weighted mean and the plain one
  // agree and the arithmetic is 50, 60, 70, 80.
  std::size_t words = 0;
  double weighted = 0.0;
  for (std::size_t i = 0; i < report.quality.size(); ++i) {
    const journal_reading_quality& how = report.quality[i].reading;
    EXPECT_TRUE(how.known);
    EXPECT_DOUBLE_EQ(how.confidence, 50.0 + (10.0 * static_cast<double>(i)));
    EXPECT_EQ(how.doubtful, 1U);
    EXPECT_GT(how.words, 1U);
    words += how.words;
    weighted += how.confidence * static_cast<double>(how.words);
  }
  const journal_reading_quality all = report.reading();
  EXPECT_TRUE(all.known);
  EXPECT_EQ(all.words, words);
  EXPECT_EQ(all.doubtful, journal_probe_entries);
  EXPECT_DOUBLE_EQ(all.confidence, weighted / static_cast<double>(words));

  // And the worst is first, which is the order a host prints: the
  // question after an ingestion is which entries to look at.
  const std::vector<journal_item_quality> worst = report.worst_first();
  ASSERT_EQ(worst.size(), journal_probe_entries);
  EXPECT_DOUBLE_EQ(worst.front().reading.confidence, 50.0);
  EXPECT_DOUBLE_EQ(worst.back().reading.confidence,
                   50.0 + (10.0 * (journal_probe_entries - 1)));
}

TEST(JournalScoreOverTheProbe, AnEngineThatDoesNotSayLeavesTheListEmpty) {
  // "The engine did not report confidences" and "the engine was not sure"
  // are different facts, and a list of unknowns would say the second.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_probe_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  ASSERT_EQ(report.recognized, journal_probe_entries);
  EXPECT_TRUE(report.quality.empty());
  EXPECT_FALSE(report.reading().known);
}

TEST(JournalScoreOverTheProbe, ANoisyReadingIsStillARealReading) {
  // The fixture spoils the answer and refuses everything the honest one
  // refuses: it delegates, so the right offset, filter, predictor and
  // crop all still have to be right before there is anything to spoil.
  journal_probe_noisy_ocr engine;
  const journal_scan nothing;
  std::string out = "left over";
  EXPECT_FALSE(engine.recognize(nothing, out));
  EXPECT_TRUE(out.empty());
  EXPECT_FALSE(engine.quality().known);
}

}  // namespace
}  // namespace amberfolio::host
