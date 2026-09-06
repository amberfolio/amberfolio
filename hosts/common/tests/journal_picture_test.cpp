// SPDX-License-Identifier: AGPL-3.0-only
//
// The entries that are pictures (journal_picture.h, #328): the fit, the
// reduction, the packing, and the two routes a page reaches the reducer
// by.
//
// What CI can prove here and what it cannot is the same split the rest of
// this pipeline has. The **arithmetic** is provable everywhere and is
// proved from the description the probe's document was generated from,
// so a reduction that is wrong anywhere cannot agree with the
// expectation by accident. Whether a real map reduced this way **reads
// as a map on a display** is not a thing a runner can answer, and
// `docs/journal.md` §11 says who owes it.

#include "amberfolio/host/journal_picture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/journal.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

journal_bitmap Flat(std::uint32_t width, std::uint32_t height,
                    std::uint8_t tone) {
  journal_bitmap out;
  out.width = width;
  out.height = height;
  out.pixels.assign(static_cast<std::size_t>(width) * height, tone);
  return out;
}

// --- the fit ---------------------------------------------------------------

/// A picture is fitted to look like the printed one **on the glass**, so
/// a square drawing comes out wider than it is tall in framebuffer
/// pixels. Every number below is the arithmetic the header describes,
/// worked by hand.
TEST(JournalPictureFit, ASquareDrawingIsWiderThanItIsTall) {
  const journal_picture_shape square = fit_picture(264, 264);
  EXPECT_EQ(square.height, machine::journal_art_height);
  // 264 x 160 x 6 / (264 x 5) = 192.
  EXPECT_EQ(square.width, 192);
}

TEST(JournalPictureFit, AWideDrawingFillsTheBoxAcross) {
  // 503 x 195, which is one of the tabled edition's own: the width binds,
  // and 195 x 304 x 5 / (503 x 6) rounds to 98.
  const journal_picture_shape wide = fit_picture(503, 195);
  EXPECT_EQ(wide.width, machine::journal_art_width);
  EXPECT_EQ(wide.height, 98);
}

TEST(JournalPictureFit, NothingLeavesTheBox) {
  for (const journal_picture_shape shape :
       {fit_picture(1, 4000), fit_picture(4000, 1), fit_picture(3, 3),
        fit_picture(577, 331), fit_picture(584, 803)}) {
    EXPECT_GT(shape.width, 0);
    EXPECT_GT(shape.height, 0);
    EXPECT_LE(shape.width, machine::journal_art_width);
    EXPECT_LE(shape.height, machine::journal_art_height);
  }
}

TEST(JournalPictureFit, AnEmptyRectangleHasNoShape) {
  EXPECT_EQ(fit_picture(0, 10), journal_picture_shape{});
  EXPECT_EQ(fit_picture(10, 0), journal_picture_shape{});
}

// --- the reduction ---------------------------------------------------------

TEST(JournalPictureReduce, APageWithNothingOnItIsPaper) {
  // The normalization stretches a page onto its own extremes, so a flat
  // field has to be refused a range rather than stretched into one:
  // otherwise scanner noise on a blank corner comes out as a drawing.
  journal_picture made;
  ASSERT_TRUE(reduce_picture(Flat(40, 40, 0xC8), made));
  for (std::uint32_t y = 0; y < made.height; ++y) {
    for (std::uint32_t x = 0; x < made.width; ++x) {
      ASSERT_EQ(made.level_at(x, y), machine::journal_art_paper)
          << "at " << x << "," << y;
    }
  }
}

TEST(JournalPictureReduce, InkIsTheFirstLevelAndPaperTheLast) {
  journal_bitmap half = Flat(64, 32, 0xF0);
  for (std::uint32_t y = 0; y < 32; ++y) {
    for (std::uint32_t x = 0; x < 32; ++x) {
      half.pixels[(static_cast<std::size_t>(y) * 64U) + x] = 0x10;
    }
  }
  journal_picture made;
  ASSERT_TRUE(reduce_picture(half, made));
  EXPECT_EQ(made.level_at(2, made.height / 2), machine::journal_art_ink);
  EXPECT_EQ(made.level_at(made.width - 3, made.height / 2),
            machine::journal_art_paper);
}

TEST(JournalPictureReduce, TheBytesAreTheShapeTheyClaim) {
  journal_picture made;
  ASSERT_TRUE(reduce_picture(Flat(100, 50, 0x20), made));
  EXPECT_EQ(made.stride(), machine::journal_art_stride(made.width));
  EXPECT_EQ(made.levels.size(), made.stride() * made.height);
  // Outside is paper, which is what the reader wants for a margin.
  EXPECT_EQ(made.level_at(made.width, 0), machine::journal_art_paper);
  EXPECT_EQ(made.level_at(0, made.height), machine::journal_art_paper);
}

TEST(JournalPictureReduce, EveryPixelIsOneOfTheLevels) {
  journal_bitmap ramp = Flat(64, 8, 0);
  for (std::uint32_t y = 0; y < 8; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      ramp.pixels[(static_cast<std::size_t>(y) * 64U) + x] =
          static_cast<std::uint8_t>(x * 4U);
    }
  }
  journal_picture made;
  ASSERT_TRUE(reduce_picture(ramp, made));
  for (std::uint32_t y = 0; y < made.height; ++y) {
    for (std::uint32_t x = 0; x < made.width; ++x) {
      ASSERT_LT(made.level_at(x, y), machine::journal_art_levels);
    }
  }
}

TEST(JournalPictureReduce, ARampGetsDarkerOneWay) {
  // A left-to-right ramp of tone must come back as a left-to-right ramp
  // of level, which is the whole of what "keeps the tone" means.
  journal_bitmap ramp = Flat(200, 20, 0);
  for (std::uint32_t y = 0; y < 20; ++y) {
    for (std::uint32_t x = 0; x < 200; ++x) {
      ramp.pixels[(static_cast<std::size_t>(y) * 200U) + x] =
          static_cast<std::uint8_t>((x * 255U) / 199U);
    }
  }
  journal_picture made;
  ASSERT_TRUE(reduce_picture(ramp, made));
  std::uint8_t last = machine::journal_art_ink;
  for (std::uint32_t x = 0; x < made.width; ++x) {
    const std::uint8_t level = made.level_at(x, made.height / 2);
    ASSERT_GE(level, last) << "at " << x;
    last = level;
  }
  EXPECT_EQ(made.level_at(0, made.height / 2), machine::journal_art_ink);
  EXPECT_EQ(made.level_at(made.width - 1, made.height / 2),
            machine::journal_art_paper);
}

TEST(JournalPictureReduce, ARectangleWithNoSamplesIsRefused) {
  journal_picture made;
  EXPECT_FALSE(reduce_picture(journal_bitmap{}, made));
  EXPECT_TRUE(made.empty());
  journal_bitmap lying;
  lying.width = 40;
  lying.height = 40;
  lying.pixels.assign(10, 0);
  EXPECT_FALSE(reduce_picture(lying, made));
}

TEST(JournalPictureReduce, TheTailOfARowIsPaperAndNotInk) {
  // A width that is not a multiple of four leaves up to three pixels
  // over at the end of every packed row, and the *ink* level is zero --
  // so a picture packed into zeroed bytes has a bright edge down its
  // right for anything that walks the bytes rather than asking
  // `level_at`, which is what a reader's plane surgery does.
  journal_bitmap page = Flat(70, 63, 0xF4);
  for (std::uint32_t y = 0; y < 63; ++y) {
    page.pixels[static_cast<std::size_t>(y) * 70U] = 0x08;
  }
  journal_picture made;
  ASSERT_TRUE(reduce_picture(page, made));
  ASSERT_NE(made.width % machine::journal_art_pixels_per_byte, 0U)
      << "this test needs a width that does not divide by four";
  const std::size_t last = made.stride() - 1U;
  const unsigned over = machine::journal_art_pixels_per_byte -
                        (made.width % machine::journal_art_pixels_per_byte);
  for (std::uint32_t y = 0; y < made.height; ++y) {
    const std::uint8_t byte = made.levels[(y * made.stride()) + last];
    for (unsigned i = 0; i < over; ++i) {
      const unsigned shift = 2U * i;
      ASSERT_EQ((byte >> shift) & 0x3U, machine::journal_art_paper)
          << "row " << y << " pixel " << i << " past the end";
    }
  }
}

// --- base64 ----------------------------------------------------------------

TEST(JournalPictureBase64, RoundTripsEveryTail) {
  std::vector<std::uint8_t> bytes;
  for (std::size_t length = 0; length < 12; ++length) {
    bytes.assign(length, 0);
    for (std::size_t i = 0; i < length; ++i) {
      bytes[i] = static_cast<std::uint8_t>((i * 37U) + 3U);
    }
    const std::string text = encode_base64(bytes);
    EXPECT_EQ(text.size() % 4U, 0U) << "length " << length;
    std::vector<std::uint8_t> back;
    ASSERT_TRUE(decode_base64(text, back)) << "length " << length;
    EXPECT_EQ(back, bytes) << "length " << length;
  }
}

TEST(JournalPictureBase64, RefusesWhatItCannotRead) {
  std::vector<std::uint8_t> back;
  EXPECT_FALSE(decode_base64("AAA", back));
  EXPECT_FALSE(decode_base64("A===", back));
  EXPECT_FALSE(decode_base64("A?==", back));
  EXPECT_FALSE(decode_base64("AA==AA==", back));
}

// --- the two routes a page reaches the reducer by --------------------------

TEST(JournalPictureRoutes, TheDecodedPageIsReducedWithNoDecoderAtAll) {
  const journal_entry_fact& fact =
      journal_probe_table().front().entries[journal_probe_art_decoded_entry];
  ASSERT_EQ(fact.art.size(), 1U);
  std::vector<journal_picture> made;
  EXPECT_EQ(reduce_entry_pictures(journal_probe_pdf(), fact, nullptr, made),
            journal_trouble::none);
  ASSERT_EQ(made.size(), 1U);

  journal_picture want;
  ASSERT_TRUE(reduce_picture(journal_probe_art_expected(0), want));
  EXPECT_EQ(made.front().width, want.width);
  EXPECT_EQ(made.front().height, want.height);
  EXPECT_EQ(made.front().levels, want.levels);
  EXPECT_EQ(made.front().what.number, fact.number);
  EXPECT_EQ(made.front().nth, 0);
}

TEST(JournalPictureRoutes, AnEncodedPageWithNoDecoderIsRefusedByName) {
  const journal_entry_fact& fact =
      journal_probe_table().front().entries[journal_probe_art_encoded_entry];
  ASSERT_EQ(fact.art.size(), 1U);
  std::vector<journal_picture> made;
  EXPECT_EQ(reduce_entry_pictures(journal_probe_pdf(), fact, nullptr, made),
            journal_trouble::filter_unsupported);
  EXPECT_TRUE(made.empty());
}

TEST(JournalPictureRoutes, AnEncodedPageIsReducedWhenAHostSuppliesADecoder) {
  const journal_entry_fact& fact =
      journal_probe_table().front().entries[journal_probe_art_encoded_entry];
  journal_probe_decoder decoder;
  std::vector<journal_picture> made;
  EXPECT_EQ(reduce_entry_pictures(journal_probe_pdf(), fact, &decoder, made),
            journal_trouble::none);
  EXPECT_EQ(decoder.calls(), 1U);
  ASSERT_EQ(made.size(), 1U);

  journal_picture want;
  ASSERT_TRUE(reduce_picture(journal_probe_art_expected(1), want));
  EXPECT_EQ(made.front().width, want.width);
  EXPECT_EQ(made.front().height, want.height);
  EXPECT_EQ(made.front().levels, want.levels);
}

/// A decoder that answers a page of the wrong size is a table row that is
/// off by a page, and it has to say so rather than reduce whatever it
/// got. It is the check the decoded route gets free from its own size
/// arithmetic.
class WrongSizeDecoder final : public journal_page_decoder {
 public:
  [[nodiscard]] bool decode(std::span<const std::uint8_t> /*stream*/,
                            journal_bitmap& out) override {
    out.width = 8;
    out.height = 8;
    out.pixels.assign(64, 0x40);
    return true;
  }
  [[nodiscard]] const char* name() const noexcept override { return "wrong"; }
};

TEST(JournalPictureRoutes, ADecoderThatAnswersAnotherPageIsCaught) {
  const journal_entry_fact& fact =
      journal_probe_table().front().entries[journal_probe_art_encoded_entry];
  WrongSizeDecoder decoder;
  std::vector<journal_picture> made;
  EXPECT_EQ(reduce_entry_pictures(journal_probe_pdf(), fact, &decoder, made),
            journal_trouble::stream_size_wrong);
  EXPECT_TRUE(made.empty());
}

TEST(JournalPictureRoutes, AnEntryWithNoArtProducesNothingAndSaysNothing) {
  const journal_entry_fact& fact = journal_probe_table().front().entries[1];
  ASSERT_TRUE(fact.art.empty());
  std::vector<journal_picture> made;
  EXPECT_EQ(reduce_entry_pictures(journal_probe_pdf(), fact, nullptr, made),
            journal_trouble::none);
  EXPECT_TRUE(made.empty());
}

// --- through an ingestion --------------------------------------------------

TEST(JournalPictureIngest, PicturesAreMadeWithNoOcrEngineAtAll) {
  // A drawing has no words in it, so an ingestion with no engine still
  // produces every picture this build can decode -- and the report says
  // both numbers, so "this build has no decoder" cannot read as "this
  // journal has no drawings".
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  journal_store store;
  const journal_ingest_report report = ingester.run(nullptr, store);
  EXPECT_EQ(report.art, journal_probe_art);
  EXPECT_EQ(report.pictures, 1U);
  EXPECT_EQ(report.first_art_trouble, journal_trouble::filter_unsupported);
  EXPECT_EQ(store.picture_count(), 1U);
  EXPECT_EQ(store.recognized(), 0U);
}

TEST(JournalPictureIngest, ADecoderMakesTheOtherOne) {
  journal_ingester ingester(journal_probe_table());
  ASSERT_EQ(ingester.begin(journal_probe_pdf()), journal_trouble::none);
  journal_probe_decoder decoder;
  ingester.set_page_decoder(&decoder);
  journal_store store;
  journal_probe_ocr engine;
  const journal_ingest_report report = ingester.run(&engine, store);
  EXPECT_EQ(report.art, journal_probe_art);
  EXPECT_EQ(report.pictures, journal_probe_art);
  EXPECT_EQ(report.first_art_trouble, journal_trouble::none);
  EXPECT_EQ(store.picture_count(), journal_probe_art);
  // And the text is untouched by any of it.
  EXPECT_EQ(report.recognized, journal_probe_entries);
}

}  // namespace
}  // namespace amberfolio::host
