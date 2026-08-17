// SPDX-License-Identifier: AGPL-3.0-only
//
// The C ABI's M0 surface. The wasm smoke check already calls af_version()
// from JS, but only on the wasm target and only as a whole-module check —
// these run on every native target and pin the packing itself, which is
// what the JS side (and any future host) unpacks by hand.

#include "amberfolio/abi.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

#include "amberfolio/version.h"

namespace {

using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;

TEST(Abi, VersionAgreesWithTheCxxApi) {
  const std::uint32_t packed = af_version();
  const amberfolio::version v = amberfolio::linked_version();

  EXPECT_EQ(AF_VERSION_MAJOR(packed), static_cast<std::uint32_t>(v.major));
  EXPECT_EQ(AF_VERSION_MINOR(packed), static_cast<std::uint32_t>(v.minor));
  EXPECT_EQ(AF_VERSION_PATCH(packed), static_cast<std::uint32_t>(v.patch));
}

// abi.h documents the layout as 0x00MMmmpp. The top byte being clear is
// part of that contract, not an accident of the current version number.
TEST(Abi, VersionLeavesTheTopByteClear) {
  EXPECT_THAT(af_version(), AllOf(Ge(0x0u), Le(0x00FFFFFFu)));
}

// The accessors are macros, so nothing type-checks them: a swapped shift
// would still compile and would still agree with itself. Feed them a value
// whose three bytes are distinguishable.
TEST(Abi, AccessorsUnpackEachByteFromItsOwnField) {
  constexpr std::uint32_t packed = 0x00123456u;

  EXPECT_EQ(AF_VERSION_MAJOR(packed), 0x12u);
  EXPECT_EQ(AF_VERSION_MINOR(packed), 0x34u);
  EXPECT_EQ(AF_VERSION_PATCH(packed), 0x56u);
}

// Whatever is in the bits above the packed triple is not the accessors'
// business — a future revision could use them for flags without breaking
// a host that unpacks a version the documented way.
TEST(Abi, AccessorsIgnoreBitsAboveTheTriple) {
  EXPECT_EQ(AF_VERSION_MAJOR(0xFF123456u), 0x12u);
  EXPECT_EQ(AF_VERSION_MINOR(0xFF123456u), 0x34u);
  EXPECT_EQ(AF_VERSION_PATCH(0xFF123456u), 0x56u);
}

}  // namespace
