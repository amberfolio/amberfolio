// SPDX-License-Identifier: AGPL-3.0-only
//
// The 8086 register file and its flag word.
//
// The register enumerations are not arbitrary: they are numbered the way
// the instruction encoding numbers them, so the decoder (M1-F3) can turn a
// ModRM field straight into one of them with a cast and no lookup table.
// Renumbering them silently breaks every instruction — the tests below the
// header pin the mapping.
//
// Byte halves are get/set rather than references, deliberately: AH is the
// top half of a 16-bit value, and handing out a std::uint8_t& into it would
// bake this host's byte order into the emulated machine's.

#pragma once

#include <array>
#include <cstdint>

namespace amberfolio::cpu {

/// The eight 16-bit general registers, in ModRM `reg`/`rm` field order.
enum class reg16 : std::uint8_t { ax, cx, dx, bx, sp, bp, si, di };

/// The eight 8-bit halves, in ModRM `reg`/`rm` field order. The low four
/// are the low halves of AX..BX and the high four are their high halves —
/// which is why `bl` is 3 and `ah` is 4, and why the mapping to reg16 is
/// `n & 3` plus "high half if n >= 4".
enum class reg8 : std::uint8_t { al, cl, dl, bl, ah, ch, dh, bh };

/// The four segment registers, in the order the segment-register field of
/// the encoding (and the 26/2E/36/3E prefixes) numbers them.
enum class sreg : std::uint8_t { es, cs, ss, ds };

/// The two operand widths this machine has — the same distinction reg8
/// and reg16 make, in the form an instruction carries it. Most opcodes
/// have a `w` bit that picks between them, and almost everything below
/// the decoder is written once and parameterized by this.
///
/// Values are carried in a std::uint16_t whatever the width, with a byte
/// value living in the low eight bits. `truncate` is what keeps that
/// honest.
enum class width : std::uint8_t { byte, word };

[[nodiscard]] constexpr std::uint16_t value_mask(width w) noexcept {
  return w == width::byte ? std::uint16_t{0x00FF} : std::uint16_t{0xFFFF};
}

/// The sign bit at this width: bit 7 for a byte, bit 15 for a word.
[[nodiscard]] constexpr std::uint16_t sign_bit(width w) noexcept {
  return w == width::byte ? std::uint16_t{0x0080} : std::uint16_t{0x8000};
}

[[nodiscard]] constexpr std::uint16_t truncate(width w,
                                               std::uint16_t value) noexcept {
  return static_cast<std::uint16_t>(value & value_mask(w));
}

/// The FLAGS bits.
///
/// Nine bits mean something on an 8086; the rest are hardwired. Bit 1 and
/// bits 12-15 read back as 1, bits 3 and 5 as 0 — that is not a convention
/// we chose but what the part does, and it is visible to the program the
/// moment it executes PUSHF. `normalize` is the single place that fact
/// lives; the conformance vectors (SingleStepTests/8088 v2, real silicon)
/// are the authority on it.
namespace flag {

inline constexpr std::uint16_t cf = 0x0001;   ///< carry
inline constexpr std::uint16_t pf = 0x0004;   ///< parity (of the low byte)
inline constexpr std::uint16_t af = 0x0010;   ///< auxiliary (BCD) carry
inline constexpr std::uint16_t zf = 0x0040;   ///< zero
inline constexpr std::uint16_t sf = 0x0080;   ///< sign
inline constexpr std::uint16_t tf = 0x0100;   ///< trap (single-step)
inline constexpr std::uint16_t if_ = 0x0200;  ///< interrupt enable — `if`
                                              ///< is a keyword, hence the _
inline constexpr std::uint16_t df = 0x0400;   ///< direction
inline constexpr std::uint16_t of = 0x0800;   ///< overflow

/// The nine bits above: everything an 8086 actually stores.
inline constexpr std::uint16_t defined =
    cf | pf | af | zf | sf | tf | if_ | df | of;

/// The bits that read back as 1 no matter what was written: bit 1, and
/// bits 12-15. (The 286 frees 12-15 for IOPL and NT; we are not a 286.)
inline constexpr std::uint16_t fixed_ones = 0xF002;

/// Force a flag word into the form the part would actually read back.
/// Every path that takes a flag word *from the program* — POPF, IRET,
/// SAHF — must go through this. Internal flag updates set only bits in
/// `defined` and therefore cannot break the invariant on their own.
[[nodiscard]] constexpr std::uint16_t normalize(std::uint16_t value) noexcept {
  return static_cast<std::uint16_t>((value & defined) | fixed_ones);
}

/// FLAGS after reset: nothing set, which still reads back as 0xF002.
inline constexpr std::uint16_t reset_value = normalize(0);

/// `flags` with every bit in `mask` set to `value`, and nothing else
/// touched. The building block the ALU kernel and the instruction
/// families compose flag updates out of; `registers::set_flag` is the
/// same thing applied in place.
[[nodiscard]] constexpr std::uint16_t with(std::uint16_t flags,
                                           std::uint16_t mask,
                                           bool value) noexcept {
  return static_cast<std::uint16_t>(value ? (flags | mask) : (flags & ~mask));
}

}  // namespace flag

/// The architectural register state, and nothing else — no bus, no
/// decoding, no execution. The conformance harness loads and compares one
/// of these wholesale, so it stays a plain aggregate.
struct registers {
  /// Indexed by `reg16`. Public because the harness fills it in bulk;
  /// `operator[]` is what execution code should use.
  std::array<std::uint16_t, 8> word{};

  /// Indexed by `sreg`.
  std::array<std::uint16_t, 4> segment{};

  std::uint16_t ip{};

  /// Always normalized (see flag::normalize). Assign a program-supplied
  /// value through `load_flags`, not directly.
  std::uint16_t flags{};

  [[nodiscard]] constexpr std::uint16_t& operator[](reg16 r) noexcept {
    return word[static_cast<std::size_t>(r)];
  }
  [[nodiscard]] constexpr std::uint16_t operator[](reg16 r) const noexcept {
    return word[static_cast<std::size_t>(r)];
  }

  [[nodiscard]] constexpr std::uint16_t& operator[](sreg s) noexcept {
    return segment[static_cast<std::size_t>(s)];
  }
  [[nodiscard]] constexpr std::uint16_t operator[](sreg s) const noexcept {
    return segment[static_cast<std::size_t>(s)];
  }

  [[nodiscard]] constexpr std::uint8_t get(reg8 r) const noexcept {
    const auto n = static_cast<unsigned>(r);
    const std::uint16_t w = word[n & 3u];
    return static_cast<std::uint8_t>(n >= 4u ? (w >> 8u) : w);
  }

  constexpr void set(reg8 r, std::uint8_t value) noexcept {
    const auto n = static_cast<unsigned>(r);
    std::uint16_t& w = word[n & 3u];
    const auto v = static_cast<unsigned>(value);
    w = static_cast<std::uint16_t>(n >= 4u ? ((w & 0x00FFu) | (v << 8u))
                                           : ((w & 0xFF00u) | v));
  }

  /// True if every bit in `mask` is set. Callers pass one flag; passing
  /// several asks for "all of them", which is what the name says.
  [[nodiscard]] constexpr bool flag_set(std::uint16_t mask) const noexcept {
    return (flags & mask) == mask;
  }

  constexpr void set_flag(std::uint16_t mask, bool value) noexcept {
    flags = flag::with(flags, mask, value);
  }

  /// Write the whole flag word from a value the program produced.
  constexpr void load_flags(std::uint16_t value) noexcept {
    flags = flag::normalize(value);
  }

  friend constexpr bool operator==(const registers&,
                                   const registers&) = default;
};

}  // namespace amberfolio::cpu
