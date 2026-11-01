// SPDX-License-Identifier: AGPL-3.0-only
//
// The program profiles (program_profile.h, #552): a profile is found by
// its program's digest and by nothing else, each is an edition this build
// knows, and every seam is available only on programs that have one.

#include "amberfolio/machine/program_profile.h"

#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] bool has_profile(std::string_view fingerprint) {
  for (const program_profile* known : program_profiles()) {
    if (known->fingerprint == fingerprint) {
      return true;
    }
  }
  return false;
}

TEST(ProgramProfile, IsFoundByItsProgramsDigest) {
  ASSERT_FALSE(program_profiles().empty());
  for (const program_profile* known : program_profiles()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(known->fingerprint, digest)) << known->name;
    EXPECT_EQ(find_program_profile(digest), known) << known->name;
  }
}

TEST(ProgramProfile, AnswersNullForAProgramWithoutOne) {
  EXPECT_EQ(find_program_profile(sha256_digest{}), nullptr);
}

TEST(ProgramProfile, IsAnEditionThisBuildKnows) {
  for (const program_profile* known : program_profiles()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(known->fingerprint, digest));
    EXPECT_NE(find_edition(digest), nullptr) << known->name;
  }
}

TEST(ProgramProfile, OnlyTheBaselineHasOne) {
  ASSERT_EQ(program_profiles().size(), 1U);
  EXPECT_EQ(program_profiles().front(), &pool_of_radiance);
  EXPECT_EQ(pool_of_radiance.dgroup_offset(),
            std::uint32_t{pool_of_radiance.dgroup_paragraphs} * 16U);
}

TEST(ProgramProfile, EverySeamIsAvailableOnlyOnProgramsWithAProfile) {
  for (const std::span<const seam_definition> seams :
       {all_seams(), built_in_fixes()}) {
    for (const seam_definition& seam : seams) {
      EXPECT_FALSE(seam.fingerprints.empty()) << seam.id;
      for (const std::string_view fingerprint : seam.fingerprints) {
        EXPECT_TRUE(has_profile(fingerprint)) << seam.id;
      }
    }
  }
}

}  // namespace
}  // namespace amberfolio::machine
