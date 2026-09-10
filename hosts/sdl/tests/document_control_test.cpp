// SPDX-License-Identifier: AGPL-3.0-only
//
// The document control's two outcomes (#384).
//
// The claim under test is the issue's own: a recognised document says
// what it was recognised as and names the rows it lights; an
// unrecognised one **shows its SHA-256** and stays a clean refusal. The
// second is the half that is easy to get wrong — a control that said
// "not recognised" and stopped there would have thrown away the only
// thing that player can act on.
//
// A gated seam is stood up here rather than looked for, because no seam
// in this build is gated (since #290) and a mechanism nothing exercises
// is a mechanism that has stopped working. The document is stood up for
// the same reason `tests/core/machine/seam_test.cpp` stands one up: a
// test that needed a real document would be a test nobody without that
// document could run.

#include "document_control.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::sdl {
namespace {

constexpr std::string_view claimed_document_hex =
    "4444444444444444444444444444444444444444444444444444444444444444";
constexpr std::string_view stranger_hex =
    "9999999999999999999999999999999999999999999999999999999999999999";

[[nodiscard]] sha256_digest digest_of(std::string_view hex) {
  sha256_digest digest;
  EXPECT_TRUE(machine::parse_digest(hex, digest));
  return digest;
}

constexpr machine::document_edition claimed_document{
    .fingerprint = claimed_document_hex,
    .name = "a code wheel this test claims",
    .kind = machine::document_kind::code_wheel};

void do_nothing(machine::machine& /*box*/, machine::seam_context& /*ctx*/) {}

constexpr std::array<std::string_view, 1> claimed_binaries{
    "1111111111111111111111111111111111111111111111111111111111111111"};
constexpr std::array<machine::seam_point, 1> a_point{
    {{.module = machine::resident_image,
      .offset = 0x0003,
      .run = &do_nothing}}};

constexpr machine::seam_definition gated_seam{
    .id = "test-gated",
    .about = "needs a code wheel",
    .fingerprints = claimed_binaries,
    .points = a_point,
    .gate = machine::document_kind::code_wheel};
constexpr machine::seam_definition journal_gated_seam{
    .id = "test-gated-journal",
    .about = "needs a journal",
    .fingerprints = claimed_binaries,
    .points = a_point,
    .gate = machine::document_kind::journal};
constexpr machine::seam_definition ungated_seam{
    .id = "test-ungated",
    .about = "waits on nothing",
    .fingerprints = claimed_binaries,
    .points = a_point};

/// An engine carrying the three seams above and knowing the claimed
/// document. Empty of everything else: this is about what a host says,
/// not about what the machine does with it.
struct rig {
  rig() {
    EXPECT_TRUE(seams.add(gated_seam));
    EXPECT_TRUE(seams.add(journal_gated_seam));
    EXPECT_TRUE(seams.add(ungated_seam));
    EXPECT_TRUE(seams.add_document(claimed_document));
  }
  machine::seam_engine seams;
};

TEST(DocumentControl, ARecognisedDocumentSaysWhatItIsAndWhatWaitedOnIt) {
  rig r;
  const document_outcome outcome =
      present_document_to(r.seams, digest_of(claimed_document_hex));
  EXPECT_TRUE(outcome.recognized);
  EXPECT_EQ(outcome.name, "a code wheel this test claims");
  EXPECT_EQ(outcome.kind, "code wheel");
  EXPECT_EQ(outcome.fingerprint, claimed_document_hex);
  // The rows this document is for, and only those: the journal-gated
  // seam is not one of them, and neither is the seam that waits on
  // nothing.
  EXPECT_EQ(outcome.waiting, (std::vector<std::string>{"test-gated"}));

  const std::vector<std::string> lines = document_lines(outcome);
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_EQ(lines[0],
            "document a code wheel this test claims (code wheel) sha256=" +
                std::string(claimed_document_hex));
  EXPECT_EQ(lines[1], "the code wheel lights 1 seam: test-gated");
}

TEST(DocumentControl, AnUnrecognisedDocumentShowsItsHash) {
  rig r;
  const document_outcome outcome =
      present_document_to(r.seams, digest_of(stranger_hex));
  EXPECT_FALSE(outcome.recognized);
  EXPECT_TRUE(outcome.name.empty());
  EXPECT_TRUE(outcome.waiting.empty());
  EXPECT_EQ(outcome.fingerprint, stranger_hex);

  const std::vector<std::string> lines = document_lines(outcome);
  ASSERT_EQ(lines.size(), 2U);
  // The whole hash, on the line, because it is what an entry in the
  // table is made of and the only thing this player can act on.
  EXPECT_NE(lines[0].find(stranger_hex), std::string::npos);
  EXPECT_EQ(lines[0],
            "document unrecognized sha256=" + std::string(stranger_hex) +
                " - no gate is satisfied by it");
  EXPECT_EQ(lines[1],
            "nobody here has fingerprinted this one - that sha256 is what an"
            " entry in the table is made of");
}

TEST(DocumentControl, ADocumentNothingWaitsOnSaysSo) {
  // The honest current answer for every real document this build knows:
  // no seam here is gated (#290), so a recognised code wheel lights
  // nothing and the line says that rather than nothing at all.
  machine::seam_engine seams;
  EXPECT_TRUE(seams.add(ungated_seam));
  EXPECT_TRUE(seams.add_document(claimed_document));

  const document_outcome outcome =
      present_document_to(seams, digest_of(claimed_document_hex));
  EXPECT_TRUE(outcome.recognized);
  EXPECT_TRUE(outcome.waiting.empty());
  EXPECT_EQ(document_lines(outcome).at(1),
            "nothing in this build waits on the code wheel");
}

TEST(DocumentControl, TwoWaitingSeamsAreNamedAndCountedAsTwo) {
  machine::seam_engine seams;
  static constexpr machine::seam_definition second_gated{
      .id = "test-gated-too",
      .about = "needs the same code wheel",
      .fingerprints = claimed_binaries,
      .points = a_point,
      .gate = machine::document_kind::code_wheel};
  EXPECT_TRUE(seams.add(gated_seam));
  EXPECT_TRUE(seams.add(second_gated));
  EXPECT_TRUE(seams.add_document(claimed_document));

  const document_outcome outcome =
      present_document_to(seams, digest_of(claimed_document_hex));
  EXPECT_EQ(document_lines(outcome).at(1),
            "the code wheel lights 2 seams: test-gated, test-gated-too");
}

}  // namespace
}  // namespace amberfolio::sdl
