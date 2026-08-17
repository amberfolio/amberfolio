// SPDX-License-Identifier: AGPL-3.0-only
//
// The register file: the encoding-order numbering the decoder will rely
// on, byte-half aliasing, and flag normalization.
//
// The numbering tests look tautological — "reg8::bl is the low half of
// bx" — and they are the point. Those enumerations are read straight out
// of an instruction's ModRM byte in M1-F3; get one of them wrong and every
// instruction that names that register is wrong, silently, in a way no
// individual instruction test would localise.
//
// Expected values are written as plain signed literals throughout. A
// std::uint16_t promotes to int before the comparison, so an int literal
// is the one spelling that never produces a signed/unsigned comparison
// warning on any of the four toolchains.

#include "amberfolio/cpu/registers.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace amberfolio::cpu {
namespace {

TEST(Registers, DefaultIsAllZero) {
  const registers r{};

  for (const std::uint16_t w : r.word) {
    EXPECT_EQ(w, 0);
  }
  for (const std::uint16_t s : r.segment) {
    EXPECT_EQ(s, 0);
  }
  EXPECT_EQ(r.ip, 0);
  EXPECT_EQ(r.flags, 0);
}

// The ModRM reg/rm field order. Not alphabetical, not the order the manual
// lists them in a table: AX CX DX BX SP BP SI DI.
TEST(Registers, WordRegistersAreNumberedInEncodingOrder) {
  EXPECT_EQ(static_cast<int>(reg16::ax), 0);
  EXPECT_EQ(static_cast<int>(reg16::cx), 1);
  EXPECT_EQ(static_cast<int>(reg16::dx), 2);
  EXPECT_EQ(static_cast<int>(reg16::bx), 3);
  EXPECT_EQ(static_cast<int>(reg16::sp), 4);
  EXPECT_EQ(static_cast<int>(reg16::bp), 5);
  EXPECT_EQ(static_cast<int>(reg16::si), 6);
  EXPECT_EQ(static_cast<int>(reg16::di), 7);
}

TEST(Registers, ByteRegistersAreNumberedInEncodingOrder) {
  EXPECT_EQ(static_cast<int>(reg8::al), 0);
  EXPECT_EQ(static_cast<int>(reg8::cl), 1);
  EXPECT_EQ(static_cast<int>(reg8::dl), 2);
  EXPECT_EQ(static_cast<int>(reg8::bl), 3);
  EXPECT_EQ(static_cast<int>(reg8::ah), 4);
  EXPECT_EQ(static_cast<int>(reg8::ch), 5);
  EXPECT_EQ(static_cast<int>(reg8::dh), 6);
  EXPECT_EQ(static_cast<int>(reg8::bh), 7);
}

TEST(Registers, SegmentRegistersAreNumberedInEncodingOrder) {
  EXPECT_EQ(static_cast<int>(sreg::es), 0);
  EXPECT_EQ(static_cast<int>(sreg::cs), 1);
  EXPECT_EQ(static_cast<int>(sreg::ss), 2);
  EXPECT_EQ(static_cast<int>(sreg::ds), 3);
}

TEST(Registers, IndexedAccessReachesEachWordRegisterSeparately) {
  registers r{};

  for (int i = 0; i < 8; ++i) {
    r[static_cast<reg16>(i)] = static_cast<std::uint16_t>(0x1000 + i);
  }
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(r[static_cast<reg16>(i)], 0x1000 + i) << "reg16 #" << i;
  }
}

TEST(Registers, IndexedAccessReachesEachSegmentRegisterSeparately) {
  registers r{};

  for (int i = 0; i < 4; ++i) {
    r[static_cast<sreg>(i)] = static_cast<std::uint16_t>(0x2000 + i);
  }
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(r[static_cast<sreg>(i)], 0x2000 + i) << "sreg #" << i;
  }
}

TEST(Registers, ByteHalvesReadTheirWordRegister) {
  registers r{};
  r[reg16::ax] = 0x1234;
  r[reg16::cx] = 0x5678;
  r[reg16::dx] = 0x9ABC;
  r[reg16::bx] = 0xDEF0;

  EXPECT_EQ(r.get(reg8::al), 0x34);
  EXPECT_EQ(r.get(reg8::ah), 0x12);
  EXPECT_EQ(r.get(reg8::cl), 0x78);
  EXPECT_EQ(r.get(reg8::ch), 0x56);
  EXPECT_EQ(r.get(reg8::dl), 0xBC);
  EXPECT_EQ(r.get(reg8::dh), 0x9A);
  EXPECT_EQ(r.get(reg8::bl), 0xF0);
  EXPECT_EQ(r.get(reg8::bh), 0xDE);
}

TEST(Registers, WritingAByteHalfLeavesTheOtherHalfAlone) {
  registers r{};
  r[reg16::bx] = 0xDEF0;

  r.set(reg8::bl, 0x11);
  EXPECT_EQ(r[reg16::bx], 0xDE11);

  r.set(reg8::bh, 0x22);
  EXPECT_EQ(r[reg16::bx], 0x2211);
}

// SP, BP, SI and DI have no byte halves on an 8086, so nothing that writes
// a byte register may reach word[4..7]. The `n & 3` mapping is what
// guarantees it; this is the test that would catch an `n & 7`.
TEST(Registers, ByteWritesNeverReachThePointerRegisters) {
  registers r{};

  for (int i = 0; i < 8; ++i) {
    r.set(static_cast<reg8>(i), 0xFF);
  }

  EXPECT_EQ(r[reg16::ax], 0xFFFF);
  EXPECT_EQ(r[reg16::cx], 0xFFFF);
  EXPECT_EQ(r[reg16::dx], 0xFFFF);
  EXPECT_EQ(r[reg16::bx], 0xFFFF);
  EXPECT_EQ(r[reg16::sp], 0);
  EXPECT_EQ(r[reg16::bp], 0);
  EXPECT_EQ(r[reg16::si], 0);
  EXPECT_EQ(r[reg16::di], 0);
}

TEST(Flags, BitsSitWhereTheEncodingPutsThem) {
  EXPECT_EQ(flag::cf, 1 << 0);
  EXPECT_EQ(flag::pf, 1 << 2);
  EXPECT_EQ(flag::af, 1 << 4);
  EXPECT_EQ(flag::zf, 1 << 6);
  EXPECT_EQ(flag::sf, 1 << 7);
  EXPECT_EQ(flag::tf, 1 << 8);
  EXPECT_EQ(flag::if_, 1 << 9);
  EXPECT_EQ(flag::df, 1 << 10);
  EXPECT_EQ(flag::of, 1 << 11);
  EXPECT_EQ(flag::defined, 0x0FD5);
}

// Bits 1 and 12-15 are hardwired to 1 on an 8086, and bits 3 and 5 to 0.
// A program sees this the first time it executes PUSHF, and the
// conformance vectors — captured off real silicon — see it in every final
// state, so it cannot be treated as a detail.
TEST(Flags, NormalizeForcesTheHardwiredBits) {
  EXPECT_EQ(flag::normalize(0x0000), 0xF002);
  EXPECT_EQ(flag::normalize(0xFFFF), 0xFFD7);
  EXPECT_EQ(flag::reset_value, 0xF002);

  // Bits 3 and 5 cannot be set, whatever is written.
  EXPECT_EQ(flag::normalize(0x0028) & 0x0028, 0);
  // Bit 1 and bits 12-15 cannot be cleared.
  EXPECT_EQ(flag::normalize(0x0000) & 0xF002, 0xF002);
}

TEST(Flags, NormalizeKeepsEveryDefinedFlag) {
  for (const std::uint16_t bit :
       {flag::cf, flag::pf, flag::af, flag::zf, flag::sf, flag::tf, flag::if_,
        flag::df, flag::of}) {
    EXPECT_EQ(flag::normalize(bit) & bit, bit) << "flag bit " << bit;
  }
}

TEST(Flags, LoadFlagsNormalizesButSetFlagDoesNot) {
  registers r{};

  r.load_flags(0x0001);
  EXPECT_EQ(r.flags, 0xF003);
  EXPECT_TRUE(r.flag_set(flag::cf));

  // set_flag touches one bit and leaves the rest of the word as it found
  // it, which is what an ALU writeback needs.
  r.set_flag(flag::cf, false);
  EXPECT_EQ(r.flags, 0xF002);
  EXPECT_FALSE(r.flag_set(flag::cf));

  r.set_flag(flag::of, true);
  EXPECT_EQ(r.flags, 0xF802);
}

TEST(Flags, FlagSetAsksForEveryBitInTheMask) {
  registers r{};
  r.load_flags(flag::cf | flag::zf);

  EXPECT_TRUE(r.flag_set(flag::cf));
  EXPECT_TRUE(r.flag_set(flag::zf));
  EXPECT_TRUE(r.flag_set(flag::cf | flag::zf));
  EXPECT_FALSE(r.flag_set(flag::cf | flag::sf));
  EXPECT_FALSE(r.flag_set(flag::sf));
}

}  // namespace
}  // namespace amberfolio::cpu
