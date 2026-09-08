// SPDX-License-Identifier: AGPL-3.0-only
//
// The page decoder every build has (journal_jpeg.h, #345).
//
// What is worth asserting here is narrow and it is not "stb_image
// decodes JPEG". It is that *this* wrapper hands the library the right
// bytes, asks it for the right thing, and turns the answer into this
// project's gray rather than the library's own — and that a stream it
// cannot read comes back as a no rather than as a page of something.
//
// The probe's `/DCTDecode` stream is what it is checked against, and the
// two halves of that were written independently: `journal_probe.cpp`
// assembles a baseline JPEG by hand, marker by marker, out of one
// quantization table and two Huffman tables, and the decoder below is
// somebody else's. A hand-written encoder and a third-party decoder
// agreeing on a page is a fact about both of them.

#include "amberfolio/host/journal_jpeg.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_probe.h"

namespace amberfolio::host {
namespace {

/// The shape the probe says its encoded page is, taken off the fact table
/// rather than spelled again: the point of the assertions below is that
/// the decoder agrees with the table, and a test carrying its own copy of
/// the numbers would agree with itself.
[[nodiscard]] const journal_image& encoded_page_shape() {
  return journal_probe_table()
      .front()
      .entries[journal_probe_encoded_entry]
      .fragments.front()
      .image;
}

TEST(JournalJpeg, ReadsTheProbesOwnPageAtTheShapeTheTableClaims) {
  jpeg_page_decoder decoder;
  journal_bitmap page;
  ASSERT_TRUE(
      decoder.decode(journal_probe_encoded(journal_probe_encoded_entry), page));

  const journal_image& shape = encoded_page_shape();
  EXPECT_EQ(page.width, shape.width);
  EXPECT_EQ(page.height, shape.height);
  EXPECT_EQ(page.pixels.size(),
            static_cast<std::size_t>(shape.width) * shape.height);
}

TEST(JournalJpeg, AnswersTheToneTheStreamCarriesAndNotTheFixtures) {
  // A flat field, which is what the probe's encoder wrote. The fixture
  // decoder answers a *pattern* for these same bytes, so a wrapper that
  // had somehow been wired to the fixture would fail here rather than
  // pass by luck (journal_probe.h).
  jpeg_page_decoder decoder;
  journal_bitmap page;
  ASSERT_TRUE(
      decoder.decode(journal_probe_encoded(journal_probe_encoded_entry), page));

  EXPECT_THAT(page.pixels,
              testing::Each(testing::Eq(journal_probe_encoded_tone)));
}

TEST(JournalJpeg, ARoundedGrayIsThisProjectsAndNotTheLibrarys) {
  // The page is one component, so every channel is the same value and
  // any weighting of them is that value. What this pins is the rounding:
  // `journal_gray` is Rec. 601 to three decimals with a half added
  // before the divide, so a gray sample survives the trip exactly, and
  // stb's own `(77r + 150g + 29b) >> 8` would not have (it answers 159
  // for 160).
  EXPECT_EQ(journal_gray(journal_probe_encoded_tone, journal_probe_encoded_tone,
                         journal_probe_encoded_tone),
            journal_probe_encoded_tone);
  for (unsigned tone = 0; tone <= 255U; ++tone) {
    const auto value = static_cast<std::uint8_t>(tone);
    EXPECT_EQ(journal_gray(value, value, value), value) << "tone " << tone;
  }
}

TEST(JournalJpeg, NothingIsNotAPage) {
  jpeg_page_decoder decoder;
  journal_bitmap page;
  EXPECT_FALSE(decoder.decode({}, page));
  EXPECT_TRUE(page.empty());
}

TEST(JournalJpeg, AStreamThatIsNotAJpegIsARefusalAndNotAGuess) {
  // The document's Flate streams reach this only through a table row that
  // is wrong about its filter, and what that has to produce is the same
  // "no" an unreadable page gives -- never samples of something.
  jpeg_page_decoder decoder;
  journal_bitmap page;
  const std::vector<std::uint8_t> flate{0x78U, 0x9CU, 0x01U, 0x00U, 0x00U};
  EXPECT_FALSE(decoder.decode(flate, page));
  EXPECT_TRUE(page.empty());
}

TEST(JournalJpeg, ATruncatedPageIsRefusedRatherThanHalfDrawn) {
  jpeg_page_decoder decoder;
  journal_bitmap page;
  const std::span<const std::uint8_t> whole =
      journal_probe_encoded(journal_probe_encoded_entry);
  ASSERT_GT(whole.size(), 8U);
  // The header and the tables, and none of the scan.
  EXPECT_FALSE(decoder.decode(whole.subspan(0, whole.size() / 3U), page));
}

TEST(JournalJpeg, TheDefaultIsThisOneAndItSaysSo) {
  EXPECT_STREQ(default_page_decoder().name(), jpeg_page_decoder{}.name());
}

}  // namespace
}  // namespace amberfolio::host
