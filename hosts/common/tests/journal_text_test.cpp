// SPDX-License-Identifier: AGPL-3.0-only
//
// The text route (journal_text.h, #398), over the text probe: a document
// this project writes, whose words are text in an encoding of its own
// invention. Nothing real is anywhere in it — CI has never seen a real
// journal and never will — so what this suite can prove is that the
// route reads what the generator meant, by every step it takes, and that
// it refuses by name what it does not read.

#include "amberfolio/host/journal_text.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/host/journal_text_probe.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

[[nodiscard]] std::span<const std::uint8_t> bytes(std::string_view text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

/// The probe edition, with one row swapped for `fragments`: how a test
/// points a box at something the probe's own table never does.
struct one_row {
  journal_entry_fact fact;
  journal_edition edition;

  explicit one_row(std::span<const journal_text_fragment> fragments,
                   std::string_view digest = {})
      : fact{.number = 1, .text = fragments, .text_sha256 = digest},
        edition(journal_text_probe_edition()) {
    edition.entries = std::span<const journal_entry_fact>(&fact, 1);
  }
};

TEST(JournalText, EveryProbeItemReadsAsTheGeneratorMeantIt) {
  const journal_edition& edition = journal_text_probe_edition();
  ASSERT_TRUE(edition.reads_own_text());
  ASSERT_EQ(edition.entries.size(), journal_text_probe_items);
  for (std::size_t i = 0; i < journal_text_probe_items; ++i) {
    std::string text;
    EXPECT_EQ(read_item_text(journal_text_probe_pdf(), edition,
                             edition.entries[i], text),
              journal_trouble::none)
        << i;
    EXPECT_EQ(text, journal_text_probe_text(i)) << i;
  }
}

TEST(JournalText, TheFlowingItemKeepsItsShape) {
  // Said out loud rather than left to the comparison above, because each
  // is a rule a real edition leans on (journal_text.h's six steps).
  const std::string_view entry = journal_text_probe_text(0);
  // The heading, and the indent after it, are a paragraph each...
  EXPECT_EQ(entry.substr(0, 10), "Entry 1:\n\n");
  // ...the discretionary hyphen is gone and the word whole...
  EXPECT_NE(entry.find("folio,"), std::string_view::npos);
  // ...the writer's hyphen stays, two spaces are one, the second column
  // continues the first with a space, and a look-alike is folded.
  EXPECT_NE(entry.find("self-taught scribe, she"), std::string_view::npos);
  EXPECT_NE(entry.find("by night."), std::string_view::npos);
  EXPECT_NE(entry.find("\n\n\u201CTwo"), std::string_view::npos);
  EXPECT_EQ(entry.find('\n', 10), entry.find("\n\n\u201C"));
}

TEST(JournalText, TheIngesterReadsATextEditionWithNoEngine) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_text_probe_pdf()), journal_trouble::none);
  EXPECT_TRUE(ingester.reads_own_text());

  journal_store store;
  const journal_ingest_report report = ingester.run(nullptr, store);
  EXPECT_EQ(report.trouble, journal_trouble::none);
  EXPECT_EQ(report.entries, journal_text_probe_items);
  EXPECT_EQ(report.extracted, journal_text_probe_items);
  EXPECT_EQ(report.recognized, journal_text_probe_items);
  EXPECT_EQ(report.first_trouble, journal_trouble::none);
  EXPECT_EQ(report.art, 1U);
  EXPECT_EQ(report.pictures, 1U);

  EXPECT_EQ(store.engine(), journal_text_engine);
  EXPECT_EQ(store.text({.kind = journal_kind::entry, .number = 1}),
            journal_text_probe_text(0));
  EXPECT_EQ(store.text({.kind = journal_kind::tale, .number = 1}),
            journal_text_probe_text(1));
  EXPECT_EQ(store.picture_count(), 1U);

  // The same store shape as a scanned edition's, so it round-trips as
  // one: nothing about a text edition moved the format.
  journal_store again;
  ASSERT_EQ(again.parse(store.serialize()), journal_trouble::none);
  EXPECT_EQ(again.text({.kind = journal_kind::entry, .number = 1}),
            journal_text_probe_text(0));
}

TEST(JournalText, AnEngineHandedToATextEditionIsNeverAsked) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_text_probe_pdf()), journal_trouble::none);
  // The probe's fixture engine answers only for the scanned probe's
  // images; asked about anything here it would refuse, and the count
  // below would drop.
  journal_probe_ocr engine;
  journal_store store;
  const journal_ingest_report report = ingester.run(&engine, store);
  EXPECT_EQ(report.recognized, journal_text_probe_items);
  EXPECT_EQ(store.engine(), journal_text_engine);
}

TEST(JournalText, TheScannedProbeIsNotATextEdition) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  EXPECT_FALSE(ingester.reads_own_text());
  std::string text;
  EXPECT_EQ(ingester.read_text(0, text), journal_trouble::no_such_entry);
}

TEST(JournalText, AFontTheFactsDoNotNameIsRefusedInsideABox) {
  const std::array<journal_text_fragment, 1> box{
      {{.page = 1, .box = journal_text_probe_unnamed_font}}};
  const one_row row(box);
  std::string text;
  EXPECT_EQ(
      read_text_fragments(journal_text_probe_pdf(), row.edition, box, text),
      journal_trouble::text_unreadable);
}

TEST(JournalText, ACodeTheMapDoesNotNameIsRefusedInsideABox) {
  const std::array<journal_text_fragment, 1> box{
      {{.page = 1, .box = journal_text_probe_unmapped_code}}};
  const one_row row(box);
  std::string text;
  EXPECT_EQ(
      read_text_fragments(journal_text_probe_pdf(), row.edition, box, text),
      journal_trouble::text_unreadable);
}

TEST(JournalText, TextTheDigestDoesNotNameIsNotKept) {
  const journal_edition& probe = journal_text_probe_edition();
  // Tale one's pieces under entry one's digest: a real read, of the
  // wrong thing.
  const one_row row(probe.entries[1].text, probe.entries[0].text_sha256);
  std::string text = "left over";
  EXPECT_EQ(
      read_item_text(journal_text_probe_pdf(), row.edition, row.fact, text),
      journal_trouble::text_mismatch);
  EXPECT_TRUE(text.empty());
}

TEST(JournalText, APageTheEditionDoesNotHaveIsNoSuchEntry) {
  const std::array<journal_text_fragment, 1> box{
      {{.page = 9, .box = {.left = 0, .bottom = 0, .right = 400, .top = 300}}}};
  const one_row row(box);
  std::string text;
  EXPECT_EQ(
      read_text_fragments(journal_text_probe_pdf(), row.edition, box, text),
      journal_trouble::no_such_entry);
}

TEST(JournalText, AStreamThatIsNotWhereTheTableSaysIsRefused) {
  journal_edition edition = journal_text_probe_edition();
  std::array<journal_text_page, 2> pages{edition.pages[0], edition.pages[1]};
  edition.pages = pages;

  std::string text;
  pages[0].decoded += 1U;
  EXPECT_EQ(read_item_text(journal_text_probe_pdf(), edition,
                           edition.entries[0], text),
            journal_trouble::stream_size_wrong);
  pages[0] = journal_text_probe_edition().pages[0];
  pages[1].offset = journal_text_probe_pdf().size();
  EXPECT_EQ(read_item_text(journal_text_probe_pdf(), edition,
                           edition.entries[1], text),
            journal_trouble::stream_out_of_bounds);
}

TEST(JournalText, ACMapReadsInBothOfItsFormsAndFoldsLookAlikes) {
  const std::string_view cmap =
      "begincmap 3 beginbfchar <41> <0422> <42> <D83DDE00> <0043> <0063>\n"
      "endbfchar 2 beginbfrange <61> <62> <0078> <70> <71> [<0031> <0032>]\n"
      "endbfrange <0100> <0041> endcmap";
  journal_font_map map;
  ASSERT_EQ(read_to_unicode(bytes(cmap), map), journal_trouble::none);
  EXPECT_EQ(map.text['A'], "T");                 // a Cyrillic twin, folded
  EXPECT_EQ(map.text['B'], "\xF0\x9F\x98\x80");  // a surrogate pair
  EXPECT_EQ(map.text['C'], "c");                 // a two-byte source below 256
  EXPECT_EQ(map.text['a'], "x");
  EXPECT_EQ(map.text['b'], "y");
  EXPECT_EQ(map.text['p'], "1");
  EXPECT_EQ(map.text['q'], "2");
  EXPECT_FALSE(map.mapped['D']);

  journal_font_map broken;
  EXPECT_EQ(
      read_to_unicode(bytes("1 beginbfchar <41> <D83D> endbfchar"), broken),
      journal_trouble::text_unreadable);
  EXPECT_EQ(read_to_unicode(bytes("no map in here"), broken),
            journal_trouble::text_unreadable);
}

TEST(JournalText, RunsArePlacedByTheirOrigin) {
  // The text matrix is scaled here, so `Td` moves by eleven times its
  // operand; `cm` moves the whole text object; `Ts` lifts a run.
  const std::string_view content =
      "q 1 0 0 1 100 0 cm BT /A 1 Tf 11 0 0 11 10 200 Tm (x) Tj"
      " 0 -1 Td <4142 4> Tj 2 Ts (\\(\\)\\101) Tj ET Q"
      " BT /B 9 Tf [(p) -300 (q)] TJ ET";
  std::vector<journal_text_run> runs;
  ASSERT_EQ(read_text_runs(bytes(content), runs), journal_trouble::none);
  ASSERT_EQ(runs.size(), 4U);
  EXPECT_DOUBLE_EQ(runs[0].x, 110.0);
  EXPECT_DOUBLE_EQ(runs[0].y, 200.0);
  EXPECT_EQ(runs[0].font, "A");
  EXPECT_DOUBLE_EQ(runs[1].y, 189.0);
  EXPECT_EQ(runs[1].codes, (std::vector<std::uint8_t>{0x41, 0x42, 0x40}));
  EXPECT_DOUBLE_EQ(runs[2].y, 189.0 + 22.0);
  EXPECT_EQ(runs[2].codes, (std::vector<std::uint8_t>{'(', ')', 'A'}));
  EXPECT_EQ(runs[3].font, "B");
  EXPECT_EQ(runs[3].codes, (std::vector<std::uint8_t>{'p', 'q'}));

  std::vector<journal_text_run> none;
  EXPECT_EQ(read_text_runs(bytes("BT (never closed Tj ET"), none),
            journal_trouble::text_unreadable);
  EXPECT_TRUE(none.empty());
}

}  // namespace
}  // namespace amberfolio::host
