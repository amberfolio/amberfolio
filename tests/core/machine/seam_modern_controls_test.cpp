// SPDX-License-Identifier: AGPL-3.0-only
//
// The modern-controls seam as one definition (seam_modern_controls.cpp,
// #473): what the merge of the six controls seams is, and what it must not
// have changed. Each piece is exercised in a suite of its own
// (seam_list_arrows_test.cpp and the rest), through the slice of this
// table that is its; this file is about the table.

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";

/// The six seams this one replaced, which no definition may still carry.
constexpr std::array<std::string_view, 6> retired{"list-arrows",   "bar-keys",
                                                  "menu-cursor",   "hero-keys",
                                                  "select-yellow", "edit-keys"};

[[nodiscard]] const seam_definition& definition() {
  for (const seam_definition& s : all_seams()) {
    if (s.id == seam_id) {
      return s;
    }
  }
  ADD_FAILURE() << "no seam is called " << seam_id;
  return all_seams().front();
}

TEST(SeamModernControls, IsOneDefinitionAndTheSixAreGone) {
  std::size_t found = 0;
  for (const seam_definition& s : all_seams()) {
    if (s.id == seam_id) {
      ++found;
    }
    for (const std::string_view old : retired) {
      EXPECT_NE(s.id, old) << "a seam that was merged into " << seam_id;
    }
  }
  EXPECT_EQ(found, 1u);
}

TEST(SeamModernControls, IsASettingOnTheBaselineEditionAndNothingElse) {
  const seam_definition& s = definition();
  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: there is no key to ask";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.fingerprints.size(), 1u);
  EXPECT_EQ(s.fingerprints[0], known_editions().front().fingerprint)
      << "every piece's addresses are facts about that one program";
}

TEST(SeamModernControls,
     HasTheSevenPiecesPointsInTheOrderTheyWereRegisteredIn) {
  // list-arrows 3, bar-keys 1, menu-cursor 2, hero-keys 5, edit-keys 1,
  // select-yellow 11, move-mode 5. Where several pieces have a point at one
  // instruction the engine offers it in table order, so the order is
  // behaviour.
  const seam_definition& s = definition();
  ASSERT_EQ(s.points.size(), 28u);

  // The menu-bar routine's key read, overlay 25 `0x0572`: five pieces
  // listen at it, the arrows' piece first and the party bar's move mode
  // last.
  constexpr std::array<std::size_t, 5> key_read{2, 3, 4, 6, 27};
  for (const std::size_t at : key_read) {
    EXPECT_EQ(s.points[at].offset, 0x0572u) << at;
    EXPECT_FALSE(s.points[at].module.is_resident_image()) << at;
    EXPECT_EQ(s.points[at].module.load_segment_at, 0x3C60u) << at;
  }
  // The line editor's, in the resident image, between the roster's and the
  // selection's.
  EXPECT_TRUE(s.points[11].module.is_resident_image());
  EXPECT_EQ(s.points[11].offset, 0x7AC0u);
  // The selection's colours: the bar leaf's glyph call first, then the
  // blitter's two row stores, then the margin's two points.
  EXPECT_EQ(s.points[12].offset, 0x0273u);
  EXPECT_EQ(s.points[19].offset, 0x74A2u);
  EXPECT_EQ(s.points[20].offset, 0x74C8u);
  EXPECT_EQ(s.points[21].offset, 0x7667u);
  EXPECT_EQ(s.points[22].offset, 0x71FBu);
  // And the move mode's two pairs around the party bar's two calls, in the
  // adventuring loop's module.
  constexpr std::array<std::uint32_t, 4> party_bar{0x09D0, 0x09D5, 0x0C40,
                                                   0x0C45};
  for (std::size_t i = 0; i < party_bar.size(); ++i) {
    EXPECT_EQ(s.points[23 + i].offset, party_bar[i]) << i;
    EXPECT_EQ(s.points[23 + i].module.load_segment_at, 0x730u) << i;
  }
}

TEST(SeamModernControls, AskedWithEverySeamOnItStillFitsTheEngine) {
  // The faces are alternatives, so one of them counts.
  std::size_t points = 0;
  for (const seam_definition& s : all_seams()) {
    if (s.id != "font-chisel") {
      points += s.points.size();
    }
  }
  EXPECT_LE(points, seam_engine::max_points);

  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest baseline;
  ASSERT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
  box->seams().loaded(baseline, 0x1000);
  for (const seam_definition& s : all_seams()) {
    EXPECT_EQ(box->seams().enable(s.id), seam_reason::none) << s.id;
  }
  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::on);
}

TEST(SeamModernControls, IsOffByDefaultAndUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest baseline;
  ASSERT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
  box->seams().loaded(baseline, 0x1000);
  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::off);

  auto other = std::make_unique<machine>(memory_layout::pc);
  sha256_digest elsewhere{};
  elsewhere.bytes[0] = 1;
  other->seams().loaded(elsewhere, 0x1000);
  EXPECT_EQ(other->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(other->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(other->seams().enable(seam_id), seam_reason::wrong_binary);
}

}  // namespace
}  // namespace amberfolio::machine
