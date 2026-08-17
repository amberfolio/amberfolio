// SPDX-License-Identifier: AGPL-3.0-only
//
// The ALU flag kernel: 8086 flag semantics, worked out once.
//
// M1's wide phase is sixteen instruction families implemented in parallel.
// If each of them derives CF, OF and AF for itself, the emulator gets
// sixteen chances to be subtly wrong in a way that only shows up as a
// wrong branch fifty thousand instructions later. So they do not: an
// instruction family is meant to be thin wiring over the primitives here,
// and an implementer should be able to write ADD without once thinking
// about what overflow means.
//
//
// The contract, in one paragraph
// ------------------------------
//
// Every primitive takes the *current* FLAGS word and returns a new one.
// That is the whole trick behind "which flags does this instruction
// leave alone": ADC reads CF out of what you passed in, INC and DEC hand
// the CF you passed in straight back, and TF, IF and DF are carried
// through untouched by everything. Nothing here ever writes a bit outside
// flag::defined, so a normalized word in is a normalized word out.
// Operands are std::uint16_t at both widths, with a byte value in the low
// eight bits; anything above the width is truncated on the way in, so a
// stale high half cannot leak into a flag.
//
//
// What each operation does to the flags
// -------------------------------------
//
//   ADD ADC SUB SBB CMP NEG   CF PF AF ZF SF OF   (all six)
//   INC DEC                      PF AF ZF SF OF   (CF preserved)
//   AND OR XOR TEST           CF PF AF ZF SF OF   (CF, OF and AF cleared)
//
// SF is the result's sign bit at the operation's width. ZF is the result
// being zero. PF is the parity of the result's **low byte** — on a 16-bit
// operation the high half does not participate, which is a genuine 8086
// quirk and not a simplification. AF is the carry out of bit 3, which is
// what the BCD adjust instructions later consume.
//
// Exactness is the point. The SingleStepTests/8088 v2 vectors come off
// real silicon and the harness carries no masks, so officially-undefined
// bits are as binding here as documented ones. Where a bit below is not
// yet confirmed against those vectors, the comment says so by name.
//
//
// What is deliberately not here
// -----------------------------
//
// Shifts and rotates, MUL/IMUL, DIV/IDIV and the BCD adjusts have flag
// rules of their own, and they stay with their family issues (#24-#27)
// rather than being smeared across this header. They are not on their
// own, though: `with_szp` and `flag::with` are exactly the pieces they
// reuse, so the SZP core is still written once.
//
// NOT is not here either, and never will be — on an 8086 it sets no flags
// at all.

#pragma once

#include <cstdint>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu::alu {

/// What an operation that has something to write back produced.
struct result {
  /// The result, truncated to the operation's width.
  std::uint16_t value{};
  /// The complete new FLAGS word — not just the bits that changed.
  std::uint16_t flags{};

  friend constexpr bool operator==(const result&, const result&) = default;
};

// --- The shared SZP core, exported for the families with their own flag
// --- rules (shifts, MUL, BCD) so they reuse it rather than restate it.

/// Parity of the low byte of `value`: true when an even number of its
/// eight bits are set. PF on this part never looks any higher than that,
/// whatever the operation's width.
[[nodiscard]] bool parity(std::uint16_t value) noexcept;

/// SF, ZF and PF of `value` at `w`, as a mask of the bits that are set.
/// No other bit appears in the return value.
[[nodiscard]] std::uint16_t szp(width w, std::uint16_t value) noexcept;

/// `flags` with its SF, ZF and PF replaced by those of `value`, and every
/// other bit — CF, OF, AF, TF, IF, DF — left exactly as it was.
[[nodiscard]] std::uint16_t with_szp(std::uint16_t flags, width w,
                                     std::uint16_t value) noexcept;

// --- Arithmetic.

/// a + b.
[[nodiscard]] result add(width w, std::uint16_t a, std::uint16_t b,
                         std::uint16_t flags) noexcept;

/// a + b + CF, with CF taken from `flags`.
[[nodiscard]] result adc(width w, std::uint16_t a, std::uint16_t b,
                         std::uint16_t flags) noexcept;

/// a - b.
[[nodiscard]] result sub(width w, std::uint16_t a, std::uint16_t b,
                         std::uint16_t flags) noexcept;

/// a - b - CF, with CF taken from `flags`.
[[nodiscard]] result sbb(width w, std::uint16_t a, std::uint16_t b,
                         std::uint16_t flags) noexcept;

/// a - b, discarding the difference: the flags are the whole point, so
/// this returns them alone rather than a `result` with a value no caller
/// may write back.
[[nodiscard]] std::uint16_t cmp(width w, std::uint16_t a, std::uint16_t b,
                                std::uint16_t flags) noexcept;

/// a + 1. CF is preserved — the one thing that makes INC not an ADD of 1,
/// and the reason a multi-word increment loop cannot be built out of it.
[[nodiscard]] result inc(width w, std::uint16_t a,
                         std::uint16_t flags) noexcept;

/// a - 1, CF preserved.
[[nodiscard]] result dec(width w, std::uint16_t a,
                         std::uint16_t flags) noexcept;

/// 0 - a. Which is what it is: NEG is a SUB from zero, flags and all, so
/// CF ends up set for every operand but zero and OF only for the one
/// value that has no positive counterpart (0x80, or 0x8000).
[[nodiscard]] result neg(width w, std::uint16_t a,
                         std::uint16_t flags) noexcept;

// --- Logic. CF and OF are cleared; see the note on AF in alu.cpp.

[[nodiscard]] result bit_and(width w, std::uint16_t a, std::uint16_t b,
                             std::uint16_t flags) noexcept;
[[nodiscard]] result bit_or(width w, std::uint16_t a, std::uint16_t b,
                            std::uint16_t flags) noexcept;
[[nodiscard]] result bit_xor(width w, std::uint16_t a, std::uint16_t b,
                             std::uint16_t flags) noexcept;

/// a & b, discarding the result — the AND counterpart of `cmp`.
[[nodiscard]] std::uint16_t test(width w, std::uint16_t a, std::uint16_t b,
                                 std::uint16_t flags) noexcept;

}  // namespace amberfolio::cpu::alu
