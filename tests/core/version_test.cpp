// SPDX-License-Identifier: AGPL-3.0-only
//
// The C++ side of the core's M0 surface. Small, but not a placeholder:
// linked_version() is the one symbol that has to survive compilation and
// linking for the rig to mean anything, so these cases are also the
// end-to-end proof that tests/ really links amberfolio-core.

#include "amberfolio/version.h"

#include <gtest/gtest.h>

#include <ostream>

namespace amberfolio {

// Found by ADL, so a failed EXPECT_EQ prints "0.0.1" instead of a byte
// dump of the struct.
void PrintTo(const version& v, std::ostream* os) {
  *os << v.major << '.' << v.minor << '.' << v.patch;
}

}  // namespace amberfolio

namespace {

// The headers this test compiled against and the library it linked to are
// built from the same project() call, so they must agree. Once the core is
// a wasm module behind the C ABI they can genuinely diverge — this is the
// check that keeps the mechanism honest in the build where it is cheap.
TEST(Version, LinkedVersionMatchesTheHeaders) {
  EXPECT_EQ(amberfolio::linked_version(), amberfolio::core_version);
}

TEST(Version, LinkedVersionIsNotAllZeroes) {
  const amberfolio::version v = amberfolio::linked_version();
  EXPECT_GT(v, (amberfolio::version{0, 0, 0}));
}

// The defaulted operator<=> is a promise the header makes ("ordered"), and
// member order is what implements it. Reordering the members would silently
// change the ordering, so pin it: major dominates minor dominates patch.
TEST(Version, OrdersMajorThenMinorThenPatch) {
  constexpr amberfolio::version low{1, 2, 3};

  EXPECT_LT(low, (amberfolio::version{1, 2, 4}));
  EXPECT_LT(low, (amberfolio::version{1, 3, 0}));
  EXPECT_LT(low, (amberfolio::version{2, 0, 0}));
  EXPECT_EQ(low, (amberfolio::version{1, 2, 3}));
}

TEST(Version, IsUsableInConstantExpressions) {
  // A runtime EXPECT would pass even if these stopped being constexpr; the
  // static_asserts are the real assertion, and the case exists so the
  // requirement is visible in the test list.
  static_assert(amberfolio::version{1, 0, 0} > amberfolio::version{0, 9, 9});
  static_assert(amberfolio::core_version == amberfolio::core_version);
  SUCCEED();
}

}  // namespace
