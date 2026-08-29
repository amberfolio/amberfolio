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

#include <algorithm>
#include <cstdint>
#include <span>
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
  bool recognize(const journal_scan&, std::string&) override { return false; }
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
  // the ABI: ask for entry i's scan, recognize it elsewhere, hand the
  // text back, ask for the next. Same loop, different holder.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);

  journal_store store;
  ingester.adopt(store);
  for (std::size_t index = 0; index < ingester.entries(); ++index) {
    ASSERT_EQ(ingester.extract(index), journal_trouble::none) << index;
    if (index == journal_probe_encoded_entry) {
      const std::span<const std::uint8_t> want = journal_probe_encoded(index);
      EXPECT_EQ(ingester.scan().encoding, journal_encoding::jpeg);
      ASSERT_FALSE(ingester.scan().parts.empty());
      EXPECT_TRUE(
          std::ranges::equal(ingester.scan().parts.front().encoded, want));
    } else {
      EXPECT_EQ(ingester.scan().encoding, journal_encoding::gray);
      EXPECT_EQ(ingester.image().pixels, journal_probe_expected(index).pixels);
    }
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
  EXPECT_TRUE(ingester.scan().empty());
  EXPECT_EQ(ingester.entry_at(journal_probe_entries), nullptr);
}

// --- the entry this build does not decode (M5-E3a, #212) ----------------

TEST(JournalIngest, TheEncodedEntryReachesTheEngineAsItsOwnBytes) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  ASSERT_EQ(ingester.extract(journal_probe_encoded_entry),
            journal_trouble::none);

  const journal_scan& scan = ingester.scan();
  EXPECT_EQ(scan.encoding, journal_encoding::jpeg);
  ASSERT_FALSE(scan.parts.empty());

  // Byte for byte what the probe's own encoder wrote, which is the whole
  // claim: the stream is followed to its offset and handed over
  // unaltered — once per piece of the entry.
  const std::span<const std::uint8_t> want =
      journal_probe_encoded(journal_probe_encoded_entry);
  ASSERT_FALSE(want.empty());
  for (const journal_part& part : scan.parts) {
    EXPECT_TRUE(part.gray.empty()) << "there are no samples for it to have";
    EXPECT_EQ(part.encoded.size(), want.size());
    EXPECT_TRUE(std::ranges::equal(part.encoded, want));

    // And it really is a JPEG, rather than whatever happened to be there:
    // the two markers a baseline stream begins and ends with.
    ASSERT_GE(part.encoded.size(), 4U);
    EXPECT_EQ(part.encoded[0], 0xFFU);
    EXPECT_EQ(part.encoded[1], 0xD8U);
    EXPECT_EQ(part.encoded[part.encoded.size() - 2U], 0xFFU);
    EXPECT_EQ(part.encoded[part.encoded.size() - 1U], 0xD9U);
  }
}

TEST(JournalIngest, TheEncodedEntryCarriesItsRegionForTheEngineToApply) {
  // The crop moved to the engine (`journal_ocr.h`), so the rectangle has
  // to arrive with the bytes — and it is the table's rectangle, not a
  // whole-page one.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  ASSERT_EQ(ingester.extract(journal_probe_encoded_entry),
            journal_trouble::none);

  const journal_entry_fact* fact =
      ingester.entry_at(journal_probe_encoded_entry);
  ASSERT_NE(fact, nullptr);
  ASSERT_EQ(ingester.scan().parts.size(), fact->fragments.size());
  for (std::size_t i = 0; i < fact->fragments.size(); ++i) {
    const journal_region& got = ingester.scan().parts[i].region;
    const journal_fragment& want = fact->fragments[i];
    EXPECT_EQ(got.left, want.region.left);
    EXPECT_EQ(got.top, want.region.top);
    EXPECT_EQ(got.width, want.region.width);
    EXPECT_EQ(got.height, want.region.height);
    EXPECT_LT(got.width, want.image.width)
        << "a region that were the whole page would prove nothing";
  }
}

TEST(JournalIngest, AnEntryInMoreThanOnePieceArrivesInAllOfThem) {
  // The shape a real edition needs (M5-E3b, #214): an entry that flows out
  // of one rectangle and resumes in another comes back as both, in the
  // order the fact table put them in.
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  const journal_entry_fact* fact =
      ingester.entry_at(journal_probe_encoded_entry);
  ASSERT_NE(fact, nullptr);
  ASSERT_GT(fact->fragments.size(), 1U)
      << "the probe has to carry a multi-piece entry for this to mean"
         " anything";

  ASSERT_EQ(ingester.extract(journal_probe_encoded_entry),
            journal_trouble::none);
  EXPECT_EQ(ingester.scan().parts.size(), fact->fragments.size());
  // Two pieces of one page, and different parts of it: an extractor that
  // handed the same rectangle over twice would pass every check above.
  EXPECT_NE(ingester.scan().parts.front().region.top,
            ingester.scan().parts.back().region.top);
}

TEST(JournalIngest, TheFixtureRefusesAStreamThatWasAlteredOrMisplaced) {
  // The fixture is not a stub that says yes: it compares the bytes and
  // the rectangle, so a passthrough that dropped either is a refusal.
  journal_probe_ocr engine;
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  ASSERT_EQ(ingester.extract(journal_probe_encoded_entry),
            journal_trouble::none);

  std::string text;
  ASSERT_TRUE(engine.recognize(ingester.scan(), text));
  EXPECT_EQ(text, journal_probe_text(journal_probe_encoded_entry));

  journal_scan altered = ingester.scan();
  altered.parts.front().encoded[altered.parts.front().encoded.size() / 2U] ^=
      0x01U;
  EXPECT_FALSE(engine.recognize(altered, text));

  journal_scan moved = ingester.scan();
  moved.parts.front().region.left += 1U;
  EXPECT_FALSE(engine.recognize(moved, text));

  // And a piece dropped: an entry read from one of its two rectangles is
  // half an entry, which the fixture must not accept (#214).
  journal_scan short_one = ingester.scan();
  short_one.parts.pop_back();
  EXPECT_FALSE(engine.recognize(short_one, text));
}

}  // namespace
}  // namespace amberfolio::host
