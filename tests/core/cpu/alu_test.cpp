// SPDX-License-Identifier: AGPL-3.0-only
//
// The ALU flag kernel, against hand-computed cases.
//
// Every expected value below was worked out by hand from the operation's
// definition, not read back out of the implementation — which is the only
// thing that makes them worth having. They are the pre-vector safety net:
// once the conformance harness lands (M1-F4) the SingleStepTests/8088
// vectors become the final word, but ten thousand vectors that all fail
// tell you nothing about *why*, and these do. The cases are chosen at the
// boundaries where the flag definitions actually differ from each other:
// 0x7F+1, 0xFF+1, 0-1, 0x0F+1, NEG 0x80.
//
// Flags are written in the conventional `odiszapc` notation on failure —
// upper case for set — because a hex flag word is unreadable and this is
// the one place where the difference between 0xF096 and 0xF097 is the
// entire content of the test.

#include "amberfolio/cpu/alu.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <ostream>
#include <string>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

// In the namespace the types live in, so gtest finds them by ADL.
namespace alu {

static std::string flag_letters(std::uint16_t flags) {
  const auto letter = [flags](std::uint16_t bit, char set, char clear) {
    return (flags & bit) != 0 ? set : clear;
  };
  return {letter(flag::of, 'O', 'o'),  letter(flag::df, 'D', 'd'),
          letter(flag::if_, 'I', 'i'), letter(flag::tf, 'T', 't'),
          letter(flag::sf, 'S', 's'),  letter(flag::zf, 'Z', 'z'),
          letter(flag::af, 'A', 'a'),  letter(flag::pf, 'P', 'p'),
          letter(flag::cf, 'C', 'c')};
}

void PrintTo(const result& r, std::ostream* os) {
  *os << "value=0x" << std::hex << r.value << std::dec
      << " flags=" << flag_letters(r.flags);
}

}  // namespace alu

namespace {

using alu::flag_letters;

/// No flag set — which is still 0xF002 on this part.
constexpr std::uint16_t clear = flag::reset_value;

/// A flag word with exactly `bits` set, on top of the hardwired ones.
constexpr std::uint16_t only(std::initializer_list<std::uint16_t> bits) {
  std::uint16_t f = flag::reset_value;
  for (const std::uint16_t bit : bits) {
    f = static_cast<std::uint16_t>(f | bit);
  }
  return f;
}

/// Compare a full flag word, reporting both sides in `odiszapc` form.
::testing::AssertionResult FlagsAre(std::uint16_t actual,
                                    std::uint16_t expected) {
  if (actual == expected) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure()
         << "flags are " << flag_letters(actual) << ", expected "
         << flag_letters(expected);
}

// --- ADD -------------------------------------------------------------

// 0x7F + 1 is the signed-overflow boundary and also the aux-carry one:
// nibble F plus 1 carries into bit 4, and +127 plus 1 does not fit.
TEST(Alu, Add8AtTheSignedBoundary) {
  const alu::result r = alu::add(width::byte, 0x7F, 0x01, clear);

  EXPECT_EQ(r.value, 0x80);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::of, flag::sf, flag::af})));
}

// 0xFF + 1 is the unsigned one: it carries out of the top bit without
// overflowing in the signed sense, because the operands' signs differ.
TEST(Alu, Add8AtTheUnsignedBoundary) {
  const alu::result r = alu::add(width::byte, 0xFF, 0x01, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::zf, flag::pf})));
}

// Two negatives summing to zero: carry out *and* signed overflow.
TEST(Alu, Add8OfTwoSignBitsCarriesAndOverflows) {
  const alu::result r = alu::add(width::byte, 0x80, 0x80, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::of, flag::zf, flag::pf})));
}

// The aux carry on its own, with nothing else set: 0x0F + 1 crosses bit 3
// and crosses nothing else.
TEST(Alu, Add8CarriesOutOfBitThreeAlone) {
  const alu::result r = alu::add(width::byte, 0x0F, 0x01, clear);

  EXPECT_EQ(r.value, 0x10);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::af})));
}

TEST(Alu, Add8OfZeroes) {
  const alu::result r = alu::add(width::byte, 0x00, 0x00, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

TEST(Alu, Add16AtTheSignedBoundary) {
  const alu::result r = alu::add(width::word, 0x7FFF, 0x0001, clear);

  EXPECT_EQ(r.value, 0x8000);
  // PF is the parity of the *low byte*, which is 0x00 here — so it is set
  // even though the 16-bit result has a single bit in it.
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::of, flag::sf, flag::af, flag::pf})));
}

TEST(Alu, Add16AtTheUnsignedBoundary) {
  const alu::result r = alu::add(width::word, 0xFFFF, 0x0001, clear);

  EXPECT_EQ(r.value, 0x0000);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::zf, flag::pf})));
}

TEST(Alu, Add16OfAnOrdinaryPair) {
  const alu::result r = alu::add(width::word, 0x1234, 0x5678, clear);

  EXPECT_EQ(r.value, 0x68AC);
  // 4 + 8 = 0xC: no carry out of bit 3. Low byte 0xAC has four bits set.
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::pf})));
}

// A byte operation must not see anything above its width, even if the
// caller hands it a register's whole 16 bits.
TEST(Alu, Add8IgnoresBitsAboveItsWidth) {
  const alu::result narrow = alu::add(width::byte, 0x7F, 0x01, clear);
  const alu::result wide = alu::add(width::byte, 0xFF7F, 0xFF01, clear);

  EXPECT_EQ(wide, narrow);
}

// --- ADC -------------------------------------------------------------

TEST(Alu, AdcAddsTheIncomingCarry) {
  const alu::result with_carry =
      alu::adc(width::byte, 0x7F, 0x00, only({flag::cf}));

  EXPECT_EQ(with_carry.value, 0x80);
  EXPECT_TRUE(FlagsAre(with_carry.flags, only({flag::of, flag::sf, flag::af})));
}

TEST(Alu, AdcWithoutCarryIsAdd) {
  const alu::result r = alu::adc(width::byte, 0x10, 0x20, clear);

  EXPECT_EQ(r, alu::add(width::byte, 0x10, 0x20, clear));
  EXPECT_EQ(r.value, 0x30);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::pf})));
}

// The carry can be the only thing that pushes it over: 0xFF + 0 + 1.
TEST(Alu, AdcCarriesOutOnTheCarryAlone) {
  const alu::result r = alu::adc(width::byte, 0xFF, 0x00, only({flag::cf}));

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::zf, flag::pf})));
}

TEST(Alu, Adc16CarriesOutOnTheCarryAlone) {
  const alu::result r = alu::adc(width::word, 0xFFFF, 0x0000, only({flag::cf}));

  EXPECT_EQ(r.value, 0x0000);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::zf, flag::pf})));
}

// --- SUB / CMP -------------------------------------------------------

TEST(Alu, Sub8BorrowsBelowZero) {
  const alu::result r = alu::sub(width::byte, 0x00, 0x01, clear);

  EXPECT_EQ(r.value, 0xFF);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf, flag::pf})));
}

// -128 - 1 does not fit: signed overflow with no unsigned borrow.
TEST(Alu, Sub8AtTheSignedBoundary) {
  const alu::result r = alu::sub(width::byte, 0x80, 0x01, clear);

  EXPECT_EQ(r.value, 0x7F);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::of, flag::af})));
}

// 127 - (-1) = 128, which does not fit either — and this one borrows.
TEST(Alu, Sub8OfANegativeCanOverflowUpwards) {
  const alu::result r = alu::sub(width::byte, 0x7F, 0xFF, clear);

  EXPECT_EQ(r.value, 0x80);
  // Nibble F minus nibble F borrows nothing, so AF stays clear.
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::cf, flag::of, flag::sf})));
}

TEST(Alu, Sub8BorrowsOutOfBitFourAlone) {
  const alu::result r = alu::sub(width::byte, 0x10, 0x01, clear);

  EXPECT_EQ(r.value, 0x0F);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::af, flag::pf})));
}

TEST(Alu, Sub8OfEqualOperandsIsZero) {
  const alu::result r = alu::sub(width::byte, 0x05, 0x05, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

TEST(Alu, Sub16BorrowsBelowZero) {
  const alu::result r = alu::sub(width::word, 0x0000, 0x0001, clear);

  EXPECT_EQ(r.value, 0xFFFF);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf, flag::pf})));
}

TEST(Alu, Sub16AtTheSignedBoundary) {
  const alu::result r = alu::sub(width::word, 0x8000, 0x0001, clear);

  EXPECT_EQ(r.value, 0x7FFF);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::of, flag::af, flag::pf})));
}

// CMP is SUB with the difference thrown away, and that is all it is.
TEST(Alu, CmpIsSubWithoutTheWriteback) {
  for (const width w : {width::byte, width::word}) {
    for (const int a :
         {0x0000, 0x0001, 0x007F, 0x0080, 0x00FF, 0x1234, 0x8000, 0xFFFF}) {
      for (const int b :
           {0x0000, 0x0001, 0x000F, 0x0080, 0x00FF, 0x5678, 0x8000, 0xFFFF}) {
        const auto lhs = static_cast<std::uint16_t>(a);
        const auto rhs = static_cast<std::uint16_t>(b);
        EXPECT_EQ(alu::cmp(w, lhs, rhs, clear),
                  alu::sub(w, lhs, rhs, clear).flags)
            << "a=" << a << " b=" << b;
      }
    }
  }
}

// --- SBB -------------------------------------------------------------

TEST(Alu, SbbSubtractsTheIncomingCarry) {
  const alu::result r = alu::sbb(width::byte, 0x00, 0x00, only({flag::cf}));

  EXPECT_EQ(r.value, 0xFF);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf, flag::pf})));
}

// 0 - 127 - 1 = -128: it borrows, but -128 fits, so OF stays clear. The
// case that catches an OF formula that ignores the incoming borrow.
TEST(Alu, SbbBorrowingToExactlyTheSignedMinimumDoesNotOverflow) {
  const alu::result r = alu::sbb(width::byte, 0x00, 0x7F, only({flag::cf}));

  EXPECT_EQ(r.value, 0x80);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf})));
}

// -128 - 0 - 1 = -129: one step further, and it does overflow.
TEST(Alu, SbbOneStepBelowTheSignedMinimumOverflows) {
  const alu::result r = alu::sbb(width::byte, 0x80, 0x00, only({flag::cf}));

  EXPECT_EQ(r.value, 0x7F);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::of, flag::af})));
}

TEST(Alu, SbbWithoutCarryIsSub) {
  const alu::result r = alu::sbb(width::word, 0x1234, 0x0234, clear);

  EXPECT_EQ(r, alu::sub(width::word, 0x1234, 0x0234, clear));
  EXPECT_EQ(r.value, 0x1000);
}

// --- INC / DEC -------------------------------------------------------

TEST(Alu, IncPreservesCarryWhileSettingEverythingElse) {
  const alu::result set = alu::inc(width::byte, 0x7F, only({flag::cf}));
  EXPECT_EQ(set.value, 0x80);
  EXPECT_TRUE(
      FlagsAre(set.flags, only({flag::cf, flag::of, flag::sf, flag::af})));

  // The same operation with CF clear leaves it clear — INC never writes
  // it, in either direction. This is what stops a multi-word increment
  // being built out of INC, and what makes it usable in a loop that is
  // carrying something else in CF.
  const alu::result cleared = alu::inc(width::byte, 0x7F, clear);
  EXPECT_TRUE(FlagsAre(cleared.flags, only({flag::of, flag::sf, flag::af})));
}

// 0xFF + 1 wraps to zero with an aux carry, and CF stays exactly where it
// was even though an ADD here would have set it.
TEST(Alu, IncWrappingToZeroStillLeavesCarryAlone) {
  const alu::result r = alu::inc(width::byte, 0xFF, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::af, flag::zf, flag::pf})));
  EXPECT_NE(alu::add(width::byte, 0xFF, 0x01, clear).flags & flag::cf, 0);
}

TEST(Alu, Inc16AtTheSignedBoundary) {
  const alu::result r = alu::inc(width::word, 0x7FFF, clear);

  EXPECT_EQ(r.value, 0x8000);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::of, flag::sf, flag::af, flag::pf})));
}

TEST(Alu, DecPreservesCarryWhileSettingEverythingElse) {
  const alu::result set = alu::dec(width::byte, 0x80, only({flag::cf}));
  EXPECT_EQ(set.value, 0x7F);
  EXPECT_TRUE(FlagsAre(set.flags, only({flag::cf, flag::of, flag::af})));

  const alu::result cleared = alu::dec(width::byte, 0x80, clear);
  EXPECT_TRUE(FlagsAre(cleared.flags, only({flag::of, flag::af})));
}

TEST(Alu, DecWrappingBelowZeroStillLeavesCarryAlone) {
  const alu::result r = alu::dec(width::byte, 0x00, clear);

  EXPECT_EQ(r.value, 0xFF);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::af, flag::sf, flag::pf})));
  EXPECT_NE(alu::sub(width::byte, 0x00, 0x01, clear).flags & flag::cf, 0);
}

TEST(Alu, Dec16AtTheSignedBoundary) {
  const alu::result r = alu::dec(width::word, 0x8000, clear);

  EXPECT_EQ(r.value, 0x7FFF);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::of, flag::af, flag::pf})));
}

// --- NEG -------------------------------------------------------------

TEST(Alu, NegOfZeroIsZeroAndBorrowsNothing) {
  const alu::result r = alu::neg(width::byte, 0x00, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

// Every other operand borrows, which is why NEG sets CF for all of them.
TEST(Alu, NegOfAnythingElseBorrows) {
  const alu::result r = alu::neg(width::byte, 0x01, clear);

  EXPECT_EQ(r.value, 0xFF);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf, flag::pf})));
}

// 0x80 is the one byte with no positive counterpart: negating it gives
// back 0x80, and that is the only NEG that sets OF.
TEST(Alu, NegOfTheSignedMinimumOverflows) {
  const alu::result r = alu::neg(width::byte, 0x80, clear);

  EXPECT_EQ(r.value, 0x80);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::cf, flag::of, flag::sf})));
}

TEST(Alu, Neg16OfTheSignedMinimumOverflows) {
  const alu::result r = alu::neg(width::word, 0x8000, clear);

  EXPECT_EQ(r.value, 0x8000);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::of, flag::sf, flag::pf})));
}

TEST(Alu, Neg16OfOne) {
  const alu::result r = alu::neg(width::word, 0x0001, clear);

  EXPECT_EQ(r.value, 0xFFFF);
  EXPECT_TRUE(
      FlagsAre(r.flags, only({flag::cf, flag::af, flag::sf, flag::pf})));
}

TEST(Alu, NegIsSubtractionFromZero) {
  for (const width w : {width::byte, width::word}) {
    for (const int a :
         {0x0000, 0x0001, 0x000F, 0x0080, 0x00FF, 0x8000, 0xFFFF}) {
      const auto operand = static_cast<std::uint16_t>(a);
      EXPECT_EQ(alu::neg(w, operand, clear), alu::sub(w, 0, operand, clear))
          << "a=" << a;
    }
  }
}

// --- Logic -----------------------------------------------------------

TEST(Alu, AndOfDisjointOperandsIsZero) {
  const alu::result r = alu::bit_and(width::byte, 0xF0, 0x0F, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

TEST(Alu, AndKeepsTheSignBit) {
  const alu::result r = alu::bit_and(width::byte, 0xFF, 0x80, clear);

  EXPECT_EQ(r.value, 0x80);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::sf})));
}

// CF, OF and AF are cleared by a logical operation whatever they were,
// and TF, IF and DF are none of its business. (AF after a logical
// operation is officially undefined; see the note in alu.cpp — it is the
// one bit here still to be confirmed against the vectors.)
TEST(Alu, LogicClearsCarryOverflowAndAuxAndTouchesNothingElse) {
  const std::uint16_t all = flag::normalize(0xFFFF);
  const alu::result r = alu::bit_and(width::byte, 0xFF, 0xFF, all);

  EXPECT_EQ(r.value, 0xFF);
  EXPECT_TRUE(FlagsAre(
      r.flags, only({flag::tf, flag::if_, flag::df, flag::sf, flag::pf})));
}

TEST(Alu, OrOfZeroes) {
  const alu::result r = alu::bit_or(width::byte, 0x00, 0x00, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

TEST(Alu, OrCombinesBits) {
  const alu::result r = alu::bit_or(width::word, 0xAA00, 0x0055, clear);

  EXPECT_EQ(r.value, 0xAA55);
  // Low byte 0x55 has four bits set; the sign bit of 0xAA55 is set.
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::sf, flag::pf})));
}

TEST(Alu, XorOfEqualOperandsIsZero) {
  const alu::result r = alu::bit_xor(width::byte, 0xFF, 0xFF, clear);

  EXPECT_EQ(r.value, 0x00);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::zf, flag::pf})));
}

TEST(Alu, Xor16OfComplementaryOperands) {
  const alu::result r = alu::bit_xor(width::word, 0xAAAA, 0x5555, clear);

  EXPECT_EQ(r.value, 0xFFFF);
  EXPECT_TRUE(FlagsAre(r.flags, only({flag::sf, flag::pf})));
}

TEST(Alu, TestIsAndWithoutTheWriteback) {
  for (const width w : {width::byte, width::word}) {
    for (const int a : {0x0000, 0x00F0, 0x0080, 0xAAAA, 0xFFFF}) {
      for (const int b : {0x0000, 0x000F, 0x0080, 0x5555, 0xFFFF}) {
        const auto lhs = static_cast<std::uint16_t>(a);
        const auto rhs = static_cast<std::uint16_t>(b);
        EXPECT_EQ(alu::test(w, lhs, rhs, clear),
                  alu::bit_and(w, lhs, rhs, clear).flags)
            << "a=" << a << " b=" << b;
      }
    }
  }
}

// --- What every operation leaves alone -------------------------------

// TF, IF and DF belong to the program, not to the ALU. An instruction
// family that reassigns the whole flag word from a primitive's result —
// which is exactly what they are meant to do — must not silently turn
// interrupts off.
TEST(Alu, NothingTouchesTheControlFlags) {
  const std::uint16_t control = only({flag::tf, flag::if_, flag::df});
  constexpr std::uint16_t mask = flag::tf | flag::if_ | flag::df;

  EXPECT_EQ(alu::add(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::adc(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::sub(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::sbb(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::cmp(width::byte, 1, 1, control) & mask, mask);
  EXPECT_EQ(alu::inc(width::byte, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::dec(width::byte, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::neg(width::byte, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::bit_and(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::bit_or(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::bit_xor(width::byte, 1, 1, control).flags & mask, mask);
  EXPECT_EQ(alu::test(width::byte, 1, 1, control) & mask, mask);
}

// A normalized word in is a normalized word out: nothing here writes a
// bit outside flag::defined, so the hardwired bits survive untouched and
// no caller has to re-normalize after an ALU operation.
TEST(Alu, NothingWritesOutsideTheDefinedFlags) {
  EXPECT_EQ(alu::add(width::word, 0x1234, 0x5678, clear).flags & ~flag::defined,
            flag::fixed_ones);
  EXPECT_EQ(alu::neg(width::byte, 0x80, clear).flags & ~flag::defined,
            flag::fixed_ones);
  EXPECT_EQ(alu::bit_xor(width::byte, 0xF0, 0x0F, clear).flags & ~flag::defined,
            flag::fixed_ones);
}

// --- The shared SZP core ---------------------------------------------

// PF counts the low byte and only the low byte, at either width. It is
// the 8086 quirk most often got wrong, and the shift, MUL and BCD
// families reuse this same helper rather than restating it.
TEST(Alu, ParityIsTheLowByteAlone) {
  EXPECT_TRUE(alu::parity(0x00));
  EXPECT_FALSE(alu::parity(0x01));
  EXPECT_TRUE(alu::parity(0x03));
  EXPECT_FALSE(alu::parity(0x07));
  EXPECT_TRUE(alu::parity(0xFF));
  EXPECT_FALSE(alu::parity(0x7F));

  // The high byte does not participate, however many bits are in it.
  EXPECT_TRUE(alu::parity(0xFF00));
  EXPECT_FALSE(alu::parity(0x0001));
  EXPECT_FALSE(alu::parity(0xFF01));
}

TEST(Alu, SzpReportsSignZeroAndParityAndNothingElse) {
  EXPECT_EQ(alu::szp(width::byte, 0x00), flag::zf | flag::pf);
  EXPECT_EQ(alu::szp(width::byte, 0x80), flag::sf);
  EXPECT_EQ(alu::szp(width::byte, 0xFF), flag::sf | flag::pf);

  // 0xFF00 is negative at word width and zero at byte width — the sign
  // bit and the truncation are both the width's business.
  EXPECT_EQ(alu::szp(width::word, 0xFF00), flag::sf | flag::pf);
  EXPECT_EQ(alu::szp(width::byte, 0xFF00), flag::zf | flag::pf);
}

TEST(Alu, WithSzpReplacesOnlySignZeroAndParity) {
  const std::uint16_t before =
      only({flag::cf, flag::of, flag::af, flag::tf, flag::if_, flag::df,
            flag::sf, flag::zf, flag::pf});

  const std::uint16_t after = alu::with_szp(before, width::byte, 0x01);

  // SF, ZF and PF now describe 0x01; everything else is as it was.
  EXPECT_TRUE(FlagsAre(after, only({flag::cf, flag::of, flag::af, flag::tf,
                                    flag::if_, flag::df})));
}

}  // namespace
}  // namespace amberfolio::cpu
