// SPDX-License-Identifier: AGPL-3.0-only
//
// held_keys.h (#313): what a focus loss lets go of, and in which order.
//
// The claims are small: a key is held from its make until its break, a
// focus loss releases every held key exactly once and in a fixed order,
// and afterwards nothing is held. The scan codes are the XT ones the
// machine's own table names (machine/keyboard.h): 0x1D is Ctrl, 0x38 is
// Alt, 0x2A is the left shift, 0x1E is `A`.

#include "amberfolio/host/held_keys.h"

#include <cstdint>
#include <vector>

#include "amberfolio/machine/platform.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

using machine::key_action;

constexpr std::uint8_t ctrl = 0x1D;
constexpr std::uint8_t left_shift = 0x2A;
constexpr std::uint8_t alt = 0x38;
constexpr std::uint8_t key_a = 0x1E;

TEST(HeldKeys, NothingHeldUntilAMake) {
  held_keys held;
  EXPECT_TRUE(held.empty());
  EXPECT_FALSE(held.held(alt));
  EXPECT_TRUE(held.release_all().empty());
}

TEST(HeldKeys, AKeyIsHeldFromItsMakeToItsBreak) {
  held_keys held;
  held.note(alt, key_action::down);
  EXPECT_TRUE(held.held(alt));
  EXPECT_FALSE(held.empty());
  held.note(alt, key_action::up);
  EXPECT_FALSE(held.held(alt));
  EXPECT_TRUE(held.empty());
}

TEST(HeldKeys, ARepeatedMakeIsStillOneKey) {
  held_keys held;
  held.note(key_a, key_action::down);
  held.note(key_a, key_action::down);
  EXPECT_EQ(held.release_all(), (std::vector<std::uint8_t>{key_a}));
}

TEST(HeldKeys, FocusLossReleasesEveryHeldKeyInAscendingOrder) {
  held_keys held;
  // AltGr on a Hungarian layout, plus a letter still down: posted in the
  // order a person made them, released in scan-code order.
  held.note(alt, key_action::down);
  held.note(ctrl, key_action::down);
  held.note(left_shift, key_action::down);
  held.note(key_a, key_action::down);
  held.note(left_shift, key_action::up);

  EXPECT_EQ(held.release_all(), (std::vector<std::uint8_t>{ctrl, key_a, alt}));
  EXPECT_TRUE(held.empty());
  EXPECT_FALSE(held.held(alt));
  EXPECT_FALSE(held.held(ctrl));
  // A second loss of focus has nothing left to let go of.
  EXPECT_TRUE(held.release_all().empty());
}

TEST(HeldKeys, ABreakForAKeyNeverMadeIsNothing) {
  held_keys held;
  held.note(ctrl, key_action::up);
  EXPECT_TRUE(held.empty());
}

TEST(HeldKeys, CodesOffTheWireAreIgnored) {
  held_keys held;
  held.note(0x00, key_action::down);
  held.note(0x80, key_action::down);
  held.note(0xFF, key_action::down);
  EXPECT_TRUE(held.empty());
  EXPECT_FALSE(held.held(0x80));
  EXPECT_FALSE(held.held(0xFF));
}

}  // namespace
}  // namespace amberfolio::host
