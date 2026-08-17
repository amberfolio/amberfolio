// SPDX-License-Identifier: AGPL-3.0-only
//
// The flag math. alu.h has the contract; this file has the reasons.

#include "amberfolio/cpu/alu.h"

#include <bit>
#include <cstdint>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu::alu {
namespace {

/// Carry out of bit 3, which is what AF is: the half-carry the BCD adjust
/// instructions read. It falls out of the operands and the result without
/// any special arithmetic — bit 4 of (a XOR b XOR result) differs from
/// what a borrow-free, carry-free sum would give exactly when bit 3
/// carried or borrowed — and the same expression works for addition and
/// subtraction, with or without an incoming carry.
[[nodiscard]] bool aux_carry(std::uint16_t a, std::uint16_t b,
                             std::uint16_t value) noexcept {
  return ((a ^ b ^ value) & 0x10u) != 0;
}

/// a + b + carry_in. Shared by ADD, ADC and INC.
[[nodiscard]] result add_core(width w, std::uint16_t a, std::uint16_t b,
                              unsigned carry_in, std::uint16_t flags) noexcept {
  a = truncate(w, a);
  b = truncate(w, b);

  const std::uint32_t full = static_cast<std::uint32_t>(a) + b + carry_in;
  const std::uint16_t value = truncate(w, static_cast<std::uint16_t>(full));

  std::uint16_t f = with_szp(flags, w, value);
  // Carry out of the top bit. The sum is computed a width wider than the
  // operation, so it is there to be looked at rather than reconstructed.
  f = flag::with(f, flag::cf, full > static_cast<std::uint32_t>(value_mask(w)));
  f = flag::with(f, flag::af, aux_carry(a, b, value));
  // Signed overflow on an addition: both operands agreed about their sign
  // and the result disagreed. Two operands of opposite sign can never
  // overflow, which is why this cannot be written as "the sign changed".
  f = flag::with(f, flag::of, ((value ^ a) & (value ^ b) & sign_bit(w)) != 0);

  return {.value = value, .flags = f};
}

/// a - b - borrow_in. Shared by SUB, SBB, CMP, DEC and NEG.
[[nodiscard]] result sub_core(width w, std::uint16_t a, std::uint16_t b,
                              unsigned borrow_in,
                              std::uint16_t flags) noexcept {
  a = truncate(w, a);
  b = truncate(w, b);

  // Unsigned wraparound is the borrow detector: subtracting more than
  // there was leaves a value far above the width's mask.
  const std::uint32_t full = static_cast<std::uint32_t>(a) - b - borrow_in;
  const std::uint16_t value = truncate(w, static_cast<std::uint16_t>(full));

  std::uint16_t f = with_szp(flags, w, value);
  f = flag::with(f, flag::cf, full > static_cast<std::uint32_t>(value_mask(w)));
  f = flag::with(f, flag::af, aux_carry(a, b, value));
  // Signed overflow on a subtraction: the operands disagreed about their
  // sign and the result took the subtrahend's side.
  f = flag::with(f, flag::of, ((a ^ b) & (a ^ value) & sign_bit(w)) != 0);

  return {.value = value, .flags = f};
}

/// The flag half of AND, OR, XOR and TEST, which differ only in the value
/// they hand it.
///
/// CF and OF are cleared: documented, and not interesting.
///
/// AF is the interesting one. Intel documents it as *undefined* after a
/// logical operation, and this kernel does not get to leave a bit
/// undefined — the conformance vectors are real silicon and the harness
/// compares every bit. Clearing it is what an 8086 is reported to do and
/// what emulators validated against these vectors do, but it is the one
/// value in this file that has not been checked against the vectors
/// themselves, because they do not exist in the tree until M1-F4. Issue
/// #21 (AND/OR/XOR/TEST/NOT) is where it gets confirmed or corrected;
/// this comment is the marker for whoever picks that up.
[[nodiscard]] result logic(width w, std::uint16_t value,
                           std::uint16_t flags) noexcept {
  const std::uint16_t v = truncate(w, value);

  std::uint16_t f = with_szp(flags, w, v);
  f = flag::with(f, flag::cf | flag::of | flag::af, false);

  return {.value = v, .flags = f};
}

}  // namespace

bool parity(std::uint16_t value) noexcept {
  return (std::popcount(static_cast<unsigned>(value & 0xFFu)) % 2) == 0;
}

std::uint16_t szp(width w, std::uint16_t value) noexcept {
  const std::uint16_t v = truncate(w, value);

  std::uint16_t f = 0;
  f = flag::with(f, flag::sf, (v & sign_bit(w)) != 0);
  f = flag::with(f, flag::zf, v == 0);
  f = flag::with(f, flag::pf, parity(v));
  return f;
}

std::uint16_t with_szp(std::uint16_t flags, width w,
                       std::uint16_t value) noexcept {
  constexpr std::uint16_t core = flag::sf | flag::zf | flag::pf;
  return static_cast<std::uint16_t>((flags & ~core) | szp(w, value));
}

result add(width w, std::uint16_t a, std::uint16_t b,
           std::uint16_t flags) noexcept {
  return add_core(w, a, b, 0, flags);
}

result adc(width w, std::uint16_t a, std::uint16_t b,
           std::uint16_t flags) noexcept {
  return add_core(w, a, b, (flags & flag::cf) != 0 ? 1u : 0u, flags);
}

result sub(width w, std::uint16_t a, std::uint16_t b,
           std::uint16_t flags) noexcept {
  return sub_core(w, a, b, 0, flags);
}

result sbb(width w, std::uint16_t a, std::uint16_t b,
           std::uint16_t flags) noexcept {
  return sub_core(w, a, b, (flags & flag::cf) != 0 ? 1u : 0u, flags);
}

std::uint16_t cmp(width w, std::uint16_t a, std::uint16_t b,
                  std::uint16_t flags) noexcept {
  return sub_core(w, a, b, 0, flags).flags;
}

result inc(width w, std::uint16_t a, std::uint16_t flags) noexcept {
  result r = add_core(w, a, 1, 0, flags);
  r.flags = flag::with(r.flags, flag::cf, (flags & flag::cf) != 0);
  return r;
}

result dec(width w, std::uint16_t a, std::uint16_t flags) noexcept {
  result r = sub_core(w, a, 1, 0, flags);
  r.flags = flag::with(r.flags, flag::cf, (flags & flag::cf) != 0);
  return r;
}

result neg(width w, std::uint16_t a, std::uint16_t flags) noexcept {
  return sub_core(w, 0, a, 0, flags);
}

result bit_and(width w, std::uint16_t a, std::uint16_t b,
               std::uint16_t flags) noexcept {
  return logic(w, static_cast<std::uint16_t>(a & b), flags);
}

result bit_or(width w, std::uint16_t a, std::uint16_t b,
              std::uint16_t flags) noexcept {
  return logic(w, static_cast<std::uint16_t>(a | b), flags);
}

result bit_xor(width w, std::uint16_t a, std::uint16_t b,
               std::uint16_t flags) noexcept {
  return logic(w, static_cast<std::uint16_t>(a ^ b), flags);
}

std::uint16_t test(width w, std::uint16_t a, std::uint16_t b,
                   std::uint16_t flags) noexcept {
  return bit_and(w, a, b, flags).flags;
}

}  // namespace amberfolio::cpu::alu
