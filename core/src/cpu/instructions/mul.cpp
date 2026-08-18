// SPDX-License-Identifier: AGPL-3.0-only
//
// MUL and IMUL (issue #25): the F6/F7 group's reg fields 4 and 5.
//
// MUL   AL := AL * r/m8    (unsigned, product in AX)
// MUL   AX := AX * r/m16   (unsigned, product in DX:AX)
// IMUL  AL := AL * r/m8    (signed,   product in AX)
// IMUL  AX := AX * r/m16   (signed,   product in DX:AX)
//
// CF and OF are the documented pair: set when the high half of the product
// (AH for the byte forms, DX for the word forms) carries information the
// low half does not already have. SF, ZF, PF and AF are what Intel calls
// undefined, and this codebase does not get to leave a bit undefined — the
// SingleStepTests/8088 v2 vectors come off real silicon and the harness
// checks every bit, no masks.
//
// Where the undefined bits actually come from
// --------------------------------------------
//
// They are not arbitrary. Ken Shirriff's reverse-engineering of the 8086's
// multiply microcode from the die ("Reverse-engineering the multiplication
// algorithm in the Intel 8086 processor", righto.com, 2023 — see
// NOTICE.md) traces the whole instruction: a shift-and-add loop (CORX)
// builds the product one multiplier bit at a time into a pair of temporary
// registers, tmpA (the high half) and tmpC (the low half). For IMUL, the
// operands are first converted to their magnitudes (PREIMUL negates
// whichever operand is negative and remembers how many times it did, in
// the single-bit flag the part also uses for REP), the unsigned magnitude
// product is formed by the same CORX loop MUL uses, and then NEGATE
// two's-complements tmpA:tmpC back to the true sign if the remembered
// count was odd.
//
// Either way, once tmpA:tmpC holds the real, final DX:AX, the part still
// has to compute CF and OF, and it does that with one more visible ALU
// operation rather than a dedicated comparator:
//
//   MULCOF (unsigned): passes tmpA through the ALU on its own, testing it
//   against zero. That is an AND/OR-shaped operation — same shape as this
//   codebase's own `alu.cpp` logic path — so AF always comes out clear,
//   and SF/ZF/PF are simply tmpA's (i.e. the high half's).
//
//   IMULCOF (signed): rotates tmpC to put its top bit — the low half's
//   sign — into carry, then does an add-with-carry of zero into tmpA. That
//   is exactly the arithmetic test for "is the high half the low half's
//   sign extension": adding the low half's sign bit to the high half lands
//   on zero precisely when it is. Because it is a real ADD, it leaves a
//   real AF behind — the carry out of bit 3 of that add — not a cleared
//   one.
//
// So MUL and IMUL differ only in what gets carried into that final add:
// always 0 for MUL, the low half's sign bit for IMUL. One formula covers
// both, below, and every vector in all four files (F6.4, F6.5, F7.4, F7.5
// — 10,000 apiece, checked in full) agrees with it in every flag bit.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// CF, OF and the four "undefined" flags for this family, all in one place
/// because on the real part they all come from one ALU operation: the high
/// half of the product plus `carry_in` (see the file header — 0 for MUL,
/// the low half's sign bit for IMUL).
[[nodiscard]] std::uint16_t high_half_flags(width w, std::uint16_t high,
                                            unsigned carry_in,
                                            std::uint16_t flags) noexcept {
  const std::uint16_t r =
      truncate(w, static_cast<std::uint16_t>(high + carry_in));

  std::uint16_t f = alu::with_szp(flags, w, r);
  // Carry out of bit 3 of `high + carry_in`: the same aux-carry identity
  // ADD uses (bit 4 of a^b^result), applied here because this is a real
  // add and not exported from alu.h — MUL/IMUL's AF is this family's own
  // quirk, not ADD's.
  f = flag::with(f, flag::af, ((high ^ carry_in ^ r) & 0x0010u) != 0);
  // The add lands on zero exactly when the high half held nothing beyond
  // the low half's sign (or, for MUL, nothing beyond zero) — which is
  // what CF and OF, always equal here, are documented to mean.
  f = flag::with(f, flag::cf | flag::of, r != 0);
  return f;
}

}  // namespace

/// MUL AL, r/m8: unsigned AL * r/m8 -> AX.
void mul_al_rm8(processor& cpu) {
  const auto al = static_cast<std::uint16_t>(cpu.regs().get(reg8::al));
  const std::uint16_t rm = cpu.read_rm(width::byte);

  const auto product = static_cast<std::uint16_t>(al * rm);
  cpu.regs()[reg16::ax] = product;

  const auto high = static_cast<std::uint16_t>(product >> 8);
  cpu.regs().flags = high_half_flags(width::byte, high, 0, cpu.regs().flags);
}

/// MUL AX, r/m16: unsigned AX * r/m16 -> DX:AX.
void mul_ax_rm16(processor& cpu) {
  const std::uint32_t ax = cpu.regs()[reg16::ax];
  const std::uint32_t rm = cpu.read_rm(width::word);

  const std::uint32_t product = ax * rm;
  cpu.regs()[reg16::ax] = static_cast<std::uint16_t>(product & 0xFFFFu);
  const auto high = static_cast<std::uint16_t>(product >> 16u);
  cpu.regs()[reg16::dx] = high;

  cpu.regs().flags = high_half_flags(width::word, high, 0, cpu.regs().flags);
}

/// IMUL AL, r/m8: signed AL * r/m8 -> AX.
void imul_al_rm8(processor& cpu) {
  const auto al = static_cast<std::int8_t>(cpu.regs().get(reg8::al));
  const auto rm = static_cast<std::int8_t>(cpu.read_rm(width::byte));

  const auto product =
      static_cast<std::int16_t>(static_cast<int>(al) * static_cast<int>(rm));
  const auto value = static_cast<std::uint16_t>(product);
  cpu.regs()[reg16::ax] = value;

  const auto high = static_cast<std::uint16_t>(value >> 8);
  const unsigned carry_in = (value & 0x0080u) != 0 ? 1u : 0u;
  cpu.regs().flags =
      high_half_flags(width::byte, high, carry_in, cpu.regs().flags);
}

/// IMUL AX, r/m16: signed AX * r/m16 -> DX:AX.
void imul_ax_rm16(processor& cpu) {
  const auto ax = static_cast<std::int32_t>(
      static_cast<std::int16_t>(cpu.regs()[reg16::ax]));
  const auto rm = static_cast<std::int32_t>(
      static_cast<std::int16_t>(cpu.read_rm(width::word)));

  const std::int64_t product = static_cast<std::int64_t>(ax) * rm;
  const auto low = static_cast<std::uint16_t>(product & 0xFFFFu);
  const auto high = static_cast<std::uint16_t>((product >> 16) & 0xFFFFu);
  cpu.regs()[reg16::ax] = low;
  cpu.regs()[reg16::dx] = high;

  const unsigned carry_in = (low & 0x8000u) != 0 ? 1u : 0u;
  cpu.regs().flags =
      high_half_flags(width::word, high, carry_in, cpu.regs().flags);
}

}  // namespace amberfolio::cpu
