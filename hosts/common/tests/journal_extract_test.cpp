// SPDX-License-Identifier: AGPL-3.0-only
//
// The extractor (journal_extract.h, M5-E3 #174), over a document this
// project built: `journal_probe.h`'s two entries and a handful of rows
// bent on purpose.
//
// The positive cases compare the extractor's output against a bitmap
// generated *from the same description the document was generated from*,
// rather than against a stored copy — so an offset, a predictor or a crop
// that is wrong anywhere cannot agree with the expectation by accident.
//
// The negative cases are most of the file, and that is the right
// proportion: this code is pointed at a file the project did not make, so
// what matters is that every way it can be wrong ends in a named refusal
// rather than in a picture of nothing.

#include "amberfolio/host/journal_extract.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_probe.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

const journal_entry_fact& ProbeFact(std::size_t index) {
  return journal_probe_table().front().entries[index];
}

/// The one piece of an entry that has one, for the tests that are about a
/// fragment rather than about an entry (#214).
journal_fragment ProbePiece(std::size_t index, std::size_t piece = 0) {
  return ProbeFact(index).fragments[piece];
}

/// A row of one fragment, for the tests that are about what `extract_scan`
/// does with a row rather than about how many pieces a row has.
///
/// Not copyable, and deliberately: the fact's span names this object's own
/// fragment, so a copy would leave a span pointing at a dead one.
class OneFragment {
 public:
  explicit OneFragment(journal_fragment piece) : piece_(piece) {
    fact_.number = 1;
    fact_.fragments = std::span(&piece_, 1);
  }
  OneFragment(const OneFragment&) = delete;
  OneFragment& operator=(const OneFragment&) = delete;

  [[nodiscard]] journal_fragment& piece() noexcept { return piece_; }
  [[nodiscard]] const journal_entry_fact& fact() const noexcept {
    return fact_;
  }

 private:
  journal_fragment piece_;
  journal_entry_fact fact_;
};

TEST(JournalExtract, TheGrayEntryComesOutExactly) {
  journal_bitmap got;
  ASSERT_EQ(extract_fragment(journal_probe_pdf(), ProbePiece(0), got),
            journal_trouble::none);

  const journal_bitmap want = journal_probe_expected(0);
  EXPECT_EQ(got.width, want.width);
  EXPECT_EQ(got.height, want.height);
  EXPECT_EQ(got.pixels, want.pixels);
}

TEST(JournalExtract, ThePredictedInvertedBilevelEntryComesOutExactly) {
  // One bit a pixel, `/Decode [1 0]`, and a different PNG row filter on
  // every row — all five of them, which is the whole reason the probe's
  // second entry exists.
  journal_bitmap got;
  ASSERT_EQ(extract_fragment(journal_probe_pdf(), ProbePiece(1), got),
            journal_trouble::none);

  const journal_bitmap want = journal_probe_expected(1);
  EXPECT_EQ(got.width, want.width);
  EXPECT_EQ(got.height, want.height);
  EXPECT_EQ(got.pixels, want.pixels);

  // And the inversion actually happened: an inverted page whose flag was
  // ignored is a perfectly plausible bitmap that no OCR engine can read,
  // so the test says which way round ink is rather than only that the two
  // agree.
  ASSERT_FALSE(got.pixels.empty());
  EXPECT_TRUE(std::ranges::contains(got.pixels, 0x00U))
      << "there is no ink anywhere in it";
  EXPECT_TRUE(std::ranges::contains(got.pixels, 0xFFU))
      << "there is no paper anywhere in it";
}

TEST(JournalExtract, TheWholePageDecodesBeforeItIsCropped) {
  journal_bitmap page;
  ASSERT_EQ(decode_image(journal_probe_pdf(), ProbePiece(0), page),
            journal_trouble::none);
  EXPECT_EQ(page.width, ProbePiece(0).image.width);
  EXPECT_EQ(page.height, ProbePiece(0).image.height);

  journal_bitmap cropped;
  ASSERT_EQ(crop(page, ProbePiece(0).region, cropped), journal_trouble::none);
  EXPECT_EQ(cropped.pixels, journal_probe_expected(0).pixels);
}

TEST(JournalExtract, ARegionOffTheEdgeIsRefusedRatherThanClamped) {
  journal_bitmap page;
  ASSERT_EQ(decode_image(journal_probe_pdf(), ProbePiece(0), page),
            journal_trouble::none);

  journal_bitmap got;
  for (const journal_region& region :
       {journal_region{.left = 40, .top = 0, .width = 32, .height = 8},
        journal_region{.left = 0, .top = 30, .width = 8, .height = 8},
        journal_region{.left = 0, .top = 0, .width = 0, .height = 8},
        journal_region{.left = 64, .top = 0, .width = 1, .height = 1}}) {
    EXPECT_EQ(crop(page, region, got), journal_trouble::region_outside);
    EXPECT_TRUE(got.empty());
  }
}

TEST(JournalExtract, AnOffsetPastTheEndIsRefused) {
  journal_fragment fact = ProbePiece(0);
  fact.offset = journal_probe_pdf().size() + 1U;
  journal_bitmap got;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::stream_out_of_bounds);
  EXPECT_TRUE(got.empty());

  fact = ProbePiece(0);
  fact.length = static_cast<std::uint32_t>(journal_probe_pdf().size());
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::stream_out_of_bounds);
}

TEST(JournalExtract, AnOffsetThatMissesTheStreamIsRefused) {
  // The failure a fact table that is off by a page actually produces: the
  // bytes at the offset are not a zlib stream at all.
  journal_fragment fact = ProbePiece(0);
  fact.offset -= 4U;
  journal_bitmap got;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::stream_corrupt);
  EXPECT_TRUE(got.empty());
}

TEST(JournalExtract, AStreamThatDecodesToTheWrongSizeIsRefused) {
  // The second failure a wrong row produces, and the more dangerous one:
  // the stream is a stream and inflates fine, and it is simply not an
  // image of the shape the table claims. Nothing about the bytes says so
  // — only the arithmetic does, which is why the arithmetic is checked.
  journal_fragment fact = ProbePiece(0);
  fact.image.height += 1U;
  fact.region = journal_region{.left = 0, .top = 0, .width = 4, .height = 4};
  journal_bitmap got;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::stream_size_wrong);

  fact = ProbePiece(0);
  fact.image.width -= 1U;
  fact.region = journal_region{.left = 0, .top = 0, .width = 4, .height = 4};
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::stream_size_wrong);
}

TEST(JournalExtract, AFilterThisBuildCannotDecodeHasNoSamplesToAnswerWith) {
  // `extract_entry` answers *pixels*, so every filter this build does not
  // decode is refused here — including the one it can still carry (#212),
  // which `extract_scan` is the way to reach.
  journal_bitmap got;
  for (const journal_filter filter :
       {journal_filter::dct, journal_filter::ccitt, journal_filter::jbig2}) {
    journal_fragment fact = ProbePiece(0);
    fact.image.filter = filter;
    EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
              journal_trouble::filter_unsupported)
        << journal_filter_name(filter);
  }
}

TEST(JournalExtract, AFilterThisBuildCannotCarryAtAllIsRefusedByName) {
  // The two fax filters are still refused by name and still not built for
  // on spec (`docs/journal.md` §4). The day a document in hand asks for
  // one is the day that changes, which is exactly how `dct` got here.
  journal_scan got;
  for (const journal_filter filter :
       {journal_filter::ccitt, journal_filter::jbig2}) {
    OneFragment row(ProbePiece(0));
    row.piece().image.filter = filter;
    EXPECT_FALSE(journal_filter_supported(filter))
        << journal_filter_name(filter);
    EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::filter_unsupported)
        << journal_filter_name(filter);
  }
}

TEST(JournalExtract, TheTwoQuestionsAboutAFilterAreDifferentQuestions) {
  // Carried and decoded came apart in #212, and every consumer that read
  // one of them meaning the other is a consumer that would hand an engine
  // a JPEG and call it pixels.
  EXPECT_TRUE(journal_filter_decoded(journal_filter::none));
  EXPECT_TRUE(journal_filter_decoded(journal_filter::flate));
  EXPECT_FALSE(journal_filter_decoded(journal_filter::dct));

  EXPECT_TRUE(journal_filter_supported(journal_filter::none));
  EXPECT_TRUE(journal_filter_supported(journal_filter::flate));
  EXPECT_TRUE(journal_filter_supported(journal_filter::dct));
  EXPECT_FALSE(journal_filter_supported(journal_filter::ccitt));
  EXPECT_FALSE(journal_filter_supported(journal_filter::jbig2));
}

TEST(JournalExtract, ADecodedEntryComesOutOfExtractScanAsSamples) {
  // The other half of `extract_scan`: an edition this build *does* decode
  // takes the same route it always did, and says so.
  journal_scan got;
  ASSERT_EQ(extract_scan(journal_probe_pdf(), ProbeFact(0), got),
            journal_trouble::none);
  EXPECT_EQ(got.encoding, journal_encoding::gray);
  ASSERT_EQ(got.parts.size(), 1U);
  EXPECT_TRUE(got.parts.front().encoded.empty());
  EXPECT_EQ(got.parts.front().gray.pixels, journal_probe_expected(0).pixels);
}

TEST(JournalExtract, AnEncodedEntryIsBoundsCheckedWithoutBeingDecoded) {
  // What this build can still check about a stream it will not look
  // inside: that the bytes are this document's, and that the rectangle is
  // inside the shape the table gives. The second is the check the crop
  // used to make for free (`journal_extract.h`).
  journal_scan got;
  const journal_fragment good = ProbePiece(journal_probe_encoded_entry);
  {
    OneFragment row(good);
    ASSERT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::none);
  }
  {
    OneFragment row(good);
    row.piece().offset = journal_probe_pdf().size() - 2U;
    row.piece().length = 64U;
    EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::stream_out_of_bounds);
  }
  {
    OneFragment row(good);
    row.piece().length = 0U;
    EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::stream_size_wrong);
  }
  {
    OneFragment row(good);
    row.piece().region.left = row.piece().image.width;
    EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::region_outside);
  }
  {
    OneFragment row(good);
    row.piece().region.width = row.piece().image.width + 1U;
    EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
              journal_trouble::region_outside);
  }
}

TEST(JournalExtract, AnEncodedScanIsClearedBeforeAFailureLeavesIt) {
  // The same rule the decoded path has: a scan left over from the
  // previous entry is the one thing that could make a failure look like a
  // success one call later.
  journal_scan got;
  ASSERT_EQ(extract_scan(journal_probe_pdf(),
                         ProbeFact(journal_probe_encoded_entry), got),
            journal_trouble::none);
  ASSERT_FALSE(got.parts.empty());

  OneFragment row(ProbePiece(journal_probe_encoded_entry));
  row.piece().offset = journal_probe_pdf().size() + 1U;
  EXPECT_EQ(extract_scan(journal_probe_pdf(), row.fact(), got),
            journal_trouble::stream_out_of_bounds);
  EXPECT_TRUE(got.empty());
  EXPECT_TRUE(got.parts.empty());
}

TEST(JournalExtract, AnEntryWithAPieceItCannotGetFailsWhole) {
  // Half an entry read as though it were the whole one is the outcome
  // nothing downstream could detect (`journal_extract.h`), so a fragment
  // that fails takes the entry with it rather than leaving what it had.
  const journal_fragment good = ProbePiece(journal_probe_encoded_entry);
  journal_fragment broken = good;
  broken.offset = journal_probe_pdf().size() + 1U;
  const std::array<journal_fragment, 2> pieces{good, broken};
  const journal_entry_fact fact{.number = 1, .fragments = pieces};

  journal_scan got;
  EXPECT_EQ(extract_scan(journal_probe_pdf(), fact, got),
            journal_trouble::stream_out_of_bounds);
  EXPECT_TRUE(got.empty())
      << "the piece that worked must not be left looking like the entry";
}

TEST(JournalExtract, AnEntryOfNoPiecesIsNoEntry) {
  const journal_entry_fact fact{.number = 1, .fragments = {}};
  journal_scan got;
  EXPECT_EQ(extract_scan(journal_probe_pdf(), fact, got),
            journal_trouble::no_such_entry);
  EXPECT_TRUE(got.empty());
}

TEST(JournalExtract, AnEntryThatMixesDecodedAndCarriedPiecesIsRefused) {
  // One engine call reads the whole entry, so an entry that were half
  // pixels and half stream is one no engine could be handed.
  const std::array<journal_fragment, 2> pieces{
      ProbePiece(0), ProbePiece(journal_probe_encoded_entry)};
  const journal_entry_fact fact{.number = 1, .fragments = pieces};

  journal_scan got;
  EXPECT_EQ(extract_scan(journal_probe_pdf(), fact, got),
            journal_trouble::filter_unsupported);
  EXPECT_TRUE(got.empty());
}

TEST(JournalExtract, AnImageShapeThisBuildCannotExpandIsRefused) {
  journal_bitmap got;
  journal_fragment fact = ProbePiece(0);

  fact.image.bits_per_component = 4;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::image_unsupported);

  fact = ProbePiece(0);
  fact.image.components = 4;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::image_unsupported);

  // TIFF's predictor 2, which is neither "none" nor one of PNG's, and is
  // the one a table written from a document's own `/DecodeParms` could
  // plausibly carry.
  fact = ProbePiece(0);
  fact.image.predictor = 2;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::image_unsupported);

  fact = ProbePiece(0);
  fact.image.width = 0;
  EXPECT_EQ(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::image_unsupported);
}

TEST(JournalExtract, AnUnfilteredStreamIsReadStraight) {
  // The `none` filter: no edition is known to want it, and it is two
  // lines, and having it means the predictor and the expansion can be
  // tested without a stream in the way.
  const journal_fragment fact{
      .page = 1,
      .offset = 2,
      .length = 6,
      .image = {.width = 3,
                .height = 2,
                .bits_per_component = 8,
                .components = 1,
                .predictor = 1,
                .filter = journal_filter::none,
                .inverted = false},
      .region = {.left = 1, .top = 0, .width = 2, .height = 2}};
  const std::vector<std::uint8_t> document{0xAA, 0xBB, 1, 2, 3, 4, 5, 6, 0xCC};

  journal_bitmap got;
  ASSERT_EQ(extract_fragment(document, fact, got), journal_trouble::none);
  EXPECT_EQ(got.width, 2U);
  EXPECT_EQ(got.height, 2U);
  EXPECT_EQ(got.pixels, (std::vector<std::uint8_t>{2, 3, 5, 6}));
}

TEST(JournalExtract, ThreeComponentsBecomeOneGray) {
  const journal_fragment fact{
      .page = 1,
      .offset = 0,
      .length = 6,
      .image = {.width = 2,
                .height = 1,
                .bits_per_component = 8,
                .components = 3,
                .predictor = 0,
                .filter = journal_filter::none,
                .inverted = false},
      .region = {.left = 0, .top = 0, .width = 2, .height = 1}};
  // White, and a mid gray with the three channels equal, so the answer
  // does not depend on the exact rounding of the luma weights.
  const std::vector<std::uint8_t> document{0xFF, 0xFF, 0xFF, 0x40, 0x40, 0x40};

  journal_bitmap got;
  ASSERT_EQ(extract_fragment(document, fact, got), journal_trouble::none);
  EXPECT_EQ(got.pixels, (std::vector<std::uint8_t>{0xFF, 0x40}));
}

TEST(JournalExtract, AnInvertedGrayImageComesBackTheOtherWayRound) {
  journal_fragment fact{
      .page = 1,
      .offset = 0,
      .length = 2,
      .image = {.width = 2,
                .height = 1,
                .bits_per_component = 8,
                .components = 1,
                .predictor = 1,
                .filter = journal_filter::none,
                .inverted = true},
      .region = {.left = 0, .top = 0, .width = 2, .height = 1}};
  const std::vector<std::uint8_t> document{0x00, 0xF0};

  journal_bitmap got;
  ASSERT_EQ(extract_fragment(document, fact, got), journal_trouble::none);
  EXPECT_EQ(got.pixels, (std::vector<std::uint8_t>{0xFF, 0x0F}));
}

TEST(JournalExtract, ARowWhoseFilterByteIsNotOneOfThePngFiveIsRefused) {
  // Five filters and a sixth answer. Guessing which of the five was meant
  // would be the one place in the extractor where a wrong answer could
  // look like a right one.
  const journal_fragment fact{
      .page = 1,
      .offset = 0,
      .length = 4,
      .image = {.width = 3,
                .height = 1,
                .bits_per_component = 8,
                .components = 1,
                .predictor = 12,
                .filter = journal_filter::none,
                .inverted = false},
      .region = {.left = 0, .top = 0, .width = 3, .height = 1}};
  const std::vector<std::uint8_t> document{9, 1, 2, 3};

  journal_bitmap got;
  EXPECT_EQ(extract_fragment(document, fact, got),
            journal_trouble::stream_corrupt);
}

TEST(JournalExtract, TheOutputIsClearedEvenWhenItFails) {
  // A bitmap left over from the previous entry is the one thing that
  // could make a failure look like a success one call later — which
  // matters, because the ingester reuses one buffer for every entry.
  journal_bitmap got;
  ASSERT_EQ(extract_fragment(journal_probe_pdf(), ProbePiece(0), got),
            journal_trouble::none);
  ASSERT_FALSE(got.empty());

  journal_fragment fact = ProbePiece(0);
  fact.offset = journal_probe_pdf().size();
  EXPECT_NE(extract_fragment(journal_probe_pdf(), fact, got),
            journal_trouble::none);
  EXPECT_TRUE(got.empty());
  EXPECT_EQ(got.width, 0U);
  EXPECT_EQ(got.height, 0U);
}

TEST(JournalExtract, TheProbeDocumentIsTheSameBytesEveryTime) {
  // The fact table's offsets are only facts if the generator is
  // deterministic, and the probe's fingerprint is only a fingerprint if
  // the bytes are the same on every target.
  const std::vector<std::uint8_t>& first = journal_probe_pdf();
  EXPECT_EQ(first, journal_probe_pdf());
  EXPECT_GT(first.size(), 64U);
  EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(first.data()), 5),
            "%PDF-");
}

}  // namespace
}  // namespace amberfolio::host
