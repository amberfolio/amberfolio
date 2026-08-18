// SPDX-License-Identifier: AGPL-3.0-only
//
// Group 2: ROL, ROR, RCL, RCR, SHL/SAL, SHR, the undocumented reg-6
// operation, and SAR (issue #24), in all four encodings — D0 (r/m8, 1),
// D1 (r/m16, 1), D2 (r/m8, CL) and D3 (r/m16, CL).
//
// All eight operations and all four encodings share one core: a single
// bit-at-a-time step, applied `count` times. That is not a shortcut taken
// for brevity — it is what the part itself does. RCL and RCR rotate
// through 9-bit (byte) or 17-bit (word) space that includes CF, and a
// count past the teens only makes sense at all if something is looping;
// simulating that loop bit by bit gets the modulo-9/17 wraparound for
// free instead of it being a special case to get subtly wrong.
//
// OF is walked the same way. Intel's textbook definition — "OF is set
// when the operation changes the sign bit" — is well defined for a single
// bit, so it is applied on every one-bit step, and the flag left behind
// after the *last* step is what the instruction reports. Intel only
// documents OF for count == 1, but the vectors show silicon writes
// something for every count, and this — the last of a chain of
// identically-computed one-bit updates — is what it writes. That is
// consistent with every D2/D3 vector this file was checked against: a
// part that implements the CL form as a hardware loop over the same
// one-bit microcode that implements the count == 1 form would produce
// exactly this, OF included, as a side effect rather than as a
// deliberately designed count > 1 result.
//
// CF is the bit that fell out of the last step, which is exactly what
// Intel documents ("CF contains the last bit shifted out").
//
// AF is officially undefined here, and the vectors are the only
// authority on it, and it turns out the eight operations do not even
// agree with each other:
//
//   - The four rotates (ROL, ROR, RCL, RCR) leave it exactly as they
//     found it — they simply never touch it.
//   - SHR, SAR and the undocumented reg 6 clear it unconditionally,
//     every time, whatever it held before.
//   - SHL/SAL is the one genuine surprise: AF comes out set exactly when
//     bit 3 of the value *entering* the step was set — the nibble-carry
//     of doubling a number, which is what a left shift by one bit is
//     arithmetically. That is consistent with SHL sharing its CF and OF
//     computation with ADD of the operand to itself (both are "did the
//     top bit change/carry"), and the vectors show AF follows the same
//     pattern one nibble down. Confirmed against every D0.4/D1.4 vector
//     this file was checked against.
//
// SF, ZF and PF come from the result, but only for the four shift
// operations; the four rotates leave SF, ZF and PF alone, touching only
// CF and OF. Both halves of that split are exactly what the issue says
// and exactly what the vectors show.
//
// The undocumented reg 6 is not a shift at all once the vectors are
// read closely, whatever its mnemonic slot in the group suggests. Every
// D0.6/D1.6 vector — every operand value, every original flag state —
// comes back with the r/m operand set to all-ones (0xFF / 0xFFFF) and
// CF, OF and AF all clear, with SF/ZF/PF computed from that fixed
// all-ones result. It does not shift, rotate, or otherwise depend on the
// operand it started with — which is exactly the "SETMO/SETMOB" name the
// issue gives it (NMOS folklore for "set to minus one"): the count-1
// encodings load a constant, they do not compute one.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// What one bit-at-a-time step of a group-2 operation does: the new
/// value, and the bit that fell out of it (which becomes the new CF).
struct step_result {
  std::uint16_t value;
  bool carry_out;
};

/// True for a rotate (ModRM reg 0-3) rather than a shift (reg 4-7,
/// including the undocumented 6). Rotates leave SF/ZF/PF alone; shifts
/// set them from the result. See the header comment.
[[nodiscard]] constexpr bool is_rotate(unsigned reg) noexcept {
  return reg <= 3;
}

/// One bit of ROL, ROR, RCL, RCR, SHL/SAL, SHR, the undocumented reg-6
/// operation, or SAR, chosen by the ModRM reg field. `carry_in` is CF
/// going into the step — read only by RCL and RCR, the two operations
/// whose rotation ring includes it.
[[nodiscard]] step_result group2_step(width w, unsigned reg,
                                      std::uint16_t value,
                                      bool carry_in) noexcept {
  const std::uint16_t top = sign_bit(w);
  switch (reg) {
    case 0: {  // ROL: MSB out, wraps around to the LSB.
      const bool out = (value & top) != 0;
      std::uint16_t v = truncate(w, static_cast<std::uint16_t>(value << 1u));
      v = static_cast<std::uint16_t>(v | (out ? 1u : 0u));
      return {.value = v, .carry_out = out};
    }
    case 1: {  // ROR: LSB out, wraps around to the MSB.
      const bool out = (value & 1u) != 0;
      std::uint16_t v = static_cast<std::uint16_t>(value >> 1u);
      v = static_cast<std::uint16_t>(v | (out ? top : 0u));
      return {.value = v, .carry_out = out};
    }
    case 2: {  // RCL: MSB out, CF comes in at the LSB.
      const bool out = (value & top) != 0;
      std::uint16_t v = truncate(w, static_cast<std::uint16_t>(value << 1u));
      v = static_cast<std::uint16_t>(v | (carry_in ? 1u : 0u));
      return {.value = v, .carry_out = out};
    }
    case 3: {  // RCR: LSB out, CF comes in at the MSB.
      const bool out = (value & 1u) != 0;
      std::uint16_t v = static_cast<std::uint16_t>(value >> 1u);
      v = static_cast<std::uint16_t>(v | (carry_in ? top : 0u));
      return {.value = v, .carry_out = out};
    }
    case 4: {  // SHL/SAL. (Reg 6 is not this — see the header comment
               // and `group2` below.)
      const bool out = (value & top) != 0;
      const std::uint16_t v =
          truncate(w, static_cast<std::uint16_t>(value << 1u));
      return {.value = v, .carry_out = out};
    }
    case 5: {  // SHR: zero shifted into the MSB.
      const bool out = (value & 1u) != 0;
      const std::uint16_t v = static_cast<std::uint16_t>(value >> 1u);
      return {.value = v, .carry_out = out};
    }
    default: {  // SAR (7): the sign bit shifted into the MSB.
      const bool out = (value & 1u) != 0;
      const std::uint16_t sign = value & top;
      std::uint16_t v = static_cast<std::uint16_t>(value >> 1u);
      v = static_cast<std::uint16_t>(v | sign);
      return {.value = v, .carry_out = out};
    }
  }
}

/// Run `count` one-bit steps of the ModRM reg field's operation and
/// write back the value and the flags they leave. `count` is CL as-is
/// for D2/D3 — never masked, see the header comment — or 1 for D0/D1.
void group2(processor& cpu, width w, std::uint16_t count) {
  const unsigned reg = cpu.current().modrm.reg;
  // Read the operand before deciding anything: even a zero count still
  // reads it (the D2/D3 vectors bear this out), it just does not write
  // it back or touch a flag.
  const std::uint16_t before = cpu.read_rm(w);

  if (count == 0) {
    // Reachable only through D2/D3 with CL == 0. A documented no-op: no
    // write, and not one flag bit changes. Confirmed against the D2/D3
    // vectors.
    return;
  }

  std::uint16_t value = before;
  bool carry = (cpu.regs().flags & flag::cf) != 0;
  bool overflow = (cpu.regs().flags & flag::of) != 0;
  // Only SHL/SAL (reg 4) ends up using this — see the header comment —
  // but it costs nothing to track it for every operation and pick it up
  // only where it is wanted.
  bool nibble_carry = false;

  if (reg == 6) {
    // The undocumented operation: a constant load, not a computation —
    // see the header comment. It does not consult `before` or loop on
    // `count` at all; every vector with a nonzero count comes back the
    // same way.
    value = value_mask(w);
    carry = false;
    overflow = false;
  } else {
    for (std::uint16_t i = 0; i < count; ++i) {
      const bool msb_before = (value & sign_bit(w)) != 0;
      nibble_carry = (value & 0x08u) != 0;
      const step_result step = group2_step(w, reg, value, carry);
      value = step.value;
      carry = step.carry_out;
      overflow = msb_before != ((value & sign_bit(w)) != 0);
    }
  }

  cpu.write_rm(w, value);

  std::uint16_t flags = cpu.regs().flags;
  flags = flag::with(flags, flag::cf, carry);
  flags = flag::with(flags, flag::of, overflow);
  if (!is_rotate(reg)) {
    // The shifts (reg 4-7) set SF/ZF/PF from the result. AF follows: set
    // from the last step's nibble carry for SHL/SAL, cleared for
    // everything else in this group — see the header comment.
    flags = alu::with_szp(flags, w, value);
    flags = flag::with(flags, flag::af, reg == 4 && nibble_carry);
  }
  cpu.regs().flags = flags;
}

}  // namespace

void shift_rotate_rm8_1(processor& cpu) { group2(cpu, width::byte, 1); }
void shift_rotate_rm16_1(processor& cpu) { group2(cpu, width::word, 1); }

void shift_rotate_rm8_cl(processor& cpu) {
  group2(cpu, width::byte, cpu.regs().get(reg8::cl));
}
void shift_rotate_rm16_cl(processor& cpu) {
  group2(cpu, width::word, cpu.regs().get(reg8::cl));
}

}  // namespace amberfolio::cpu
