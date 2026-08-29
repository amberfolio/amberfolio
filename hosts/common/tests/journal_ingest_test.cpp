// SPDX-License-Identifier: AGPL-3.0-only
//
// Ingestion end to end (journal_ingest.h, M5-E3 #174), over the probe
// document: fingerprint, fact table, inflate, predictor, crop, engine,
// store — every stage, in one run, with nothing real anywhere in it.
//
// The engine here is `journal_probe_ocr`, which answers for exactly one
// image per entry and refuses anything else. So a store with the probe's
// words in it is not evidence that an OCR call was made; it is evidence
// that the right bytes reached the engine, which is the only claim this
// suite can honestly make and the only one worth making.

#include "amberfolio/host/journal_ingest.h"

#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

TEST(JournalIngest, TheProbeGoesAllTheWayThrough) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  ASSERT_NE(ingester.edition(), nullptr);
  EXPECT_EQ(ingester.entries(), journal_probe_entries);

  journal_probe_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);

  EXPECT_EQ(report.trouble, journal_trouble::none);
  EXPECT_EQ(report.entries, journal_probe_entries);
  EXPECT_EQ(report.extracted, journal_probe_entries);
  EXPECT_EQ(report.recognized, journal_probe_entries);
  EXPECT_EQ(report.first_trouble, journal_trouble::none);
  EXPECT_EQ(report.first_failure, 0U);

  EXPECT_EQ(store.edition(), ingester.fingerprint_hex());
  EXPECT_EQ(store.engine(), engine.engine());
  EXPECT_EQ(store.text(1), journal_probe_text(0));
  EXPECT_EQ(store.text(2), journal_probe_text(1));
}

TEST(JournalIngest, AnUnrecognizedDocumentIsReportedWithItsFingerprint) {
  // The friendly path PLAN.md §9 asks for, and the reason the offsets are
  // never followed into a document they were not gathered from.
  const std::vector<std::uint8_t> stranger{'a', 'b', 'c'};
  journal_ingester ingester(journal_probe_table());
  EXPECT_EQ(ingester.begin(stranger), journal_trouble::unrecognized_edition);
  EXPECT_EQ(ingester.edition(), nullptr);
  EXPECT_EQ(ingester.entries(), 0U);
  // FIPS 180-4 appendix B.1, which is the same answer every other
  // SHA-256 in the world gives for those three bytes.
  EXPECT_EQ(ingester.fingerprint_hex(),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  journal_probe_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  EXPECT_EQ(report.trouble, journal_trouble::unrecognized_edition);
  EXPECT_EQ(report.entries, 0U);
  // And nothing was written: an unrecognized document must not disturb a
  // store a player already has.
  EXPECT_TRUE(store.empty());
  EXPECT_TRUE(store.edition().empty());
}

TEST(JournalIngest, TheShippedTableRecognizesNothingYet) {
  // The state of the world, asserted rather than assumed: with no edition
  // in `known_journals()`, the default ingester refuses every document,
  // the probe's included.
  journal_ingester ingester;
  EXPECT_EQ(ingester.begin(journal_probe_pdf()),
            journal_trouble::unrecognized_edition);
}

TEST(JournalIngest, WithNoEngineTheImagesAreStillRead) {
  // What a player with no OCR engine installed gets, and the reason the
  // report carries four numbers instead of a bool: every image decoded
  // and nothing was recognized, which is a missing engine and not a
  // broken fact table.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_store store;
  const journal_ingest_report report = ingester.run(nullptr, store);
  EXPECT_EQ(report.trouble, journal_trouble::none);
  EXPECT_EQ(report.extracted, journal_probe_entries);
  EXPECT_EQ(report.recognized, 0U);
  EXPECT_EQ(report.first_trouble, journal_trouble::no_engine);
  EXPECT_EQ(report.first_failure, 1U);
  EXPECT_EQ(store.engine(), "none");
  EXPECT_TRUE(store.empty());
}

/// An engine that reads nothing, for the entry-level failure path.
class refusing_ocr final : public journal_ocr {
 public:
  bool recognize(const journal_bitmap&, std::string&) override { return false; }
  [[nodiscard]] std::string_view engine() const override {
    return "refusing fixture";
  }
};

TEST(JournalIngest, AnEngineThatCannotReadAnEntryIsAFindingAboutTheEntry) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  refusing_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  // The run finished — it is the entries that failed, not the ingestion.
  EXPECT_EQ(report.trouble, journal_trouble::none);
  EXPECT_EQ(report.extracted, journal_probe_entries);
  EXPECT_EQ(report.recognized, 0U);
  EXPECT_EQ(report.first_trouble, journal_trouble::engine_failed);
}

TEST(JournalIngest, ACorrectionSurvivesReIngestion) {
  // The property #174 asks for, through the whole pipeline rather than
  // only through the store: ingest, correct, ingest again.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_probe_ocr engine;
  journal_store store;
  static_cast<void>(ingester.run(&engine, store));
  ASSERT_TRUE(store.correct(1, "what a person wrote instead"));

  const journal_ingest_report again = ingester.run(&engine, store);
  EXPECT_EQ(again.recognized, journal_probe_entries);
  EXPECT_EQ(store.text(1), "what a person wrote instead");
  const journal_text* entry = store.find(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_EQ(entry->scanned, journal_probe_text(0));
}

TEST(JournalIngest, AStoreOfAnotherEditionIsClearedRatherThanMerged) {
  // Entry 12 of one printing is not entry 12 of another, and keeping text
  // across editions would be the one failure that looks like it worked.
  journal_store store;
  store.set_edition(
      "9999999999999999999999999999999999999999999999999999999999999999");
  ASSERT_TRUE(store.record_scan(1, "text from a different printing"));
  ASSERT_TRUE(store.correct(1, "a correction to it"));

  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  journal_probe_ocr engine;
  static_cast<void>(ingester.run(&engine, store));

  EXPECT_EQ(store.edition(), ingester.fingerprint_hex());
  EXPECT_EQ(store.text(1), journal_probe_text(0));
  const journal_text* entry = store.find(1);
  ASSERT_NE(entry, nullptr);
  EXPECT_TRUE(entry->corrected.empty())
      << "a correction to another edition's entry 1 was kept";
}

TEST(JournalIngest, AHostMayDriveTheLoopItself) {
  // The shape the browser needs, where the engine is on the far side of
  // the ABI: ask for entry i's pixels, recognize them elsewhere, hand the
  // text back, ask for the next. Same loop, different holder.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_store store;
  ingester.adopt(store);
  for (std::size_t index = 0; index < ingester.entries(); ++index) {
    ASSERT_EQ(ingester.extract(index), journal_trouble::none) << index;
    EXPECT_EQ(ingester.image().pixels, journal_probe_expected(index).pixels);
    ASSERT_TRUE(store.record_scan(ingester.entry_at(index)->number,
                                  journal_probe_text(index)));
  }
  EXPECT_EQ(store.recognized(), journal_probe_entries);
  EXPECT_EQ(store.edition(), ingester.fingerprint_hex());
}

TEST(JournalIngest, AnEntryThatIsNotThereIsSaidSo) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  EXPECT_EQ(ingester.extract(journal_probe_entries),
            journal_trouble::no_such_entry);
  EXPECT_TRUE(ingester.image().empty());
  EXPECT_EQ(ingester.entry_at(journal_probe_entries), nullptr);
}

}  // namespace
}  // namespace amberfolio::host
