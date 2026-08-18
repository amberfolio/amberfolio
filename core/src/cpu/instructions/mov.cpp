// SPDX-License-Identifier: AGPL-3.0-only
//
// MOV and XCHG (issue #18): every data-movement and exchange form the
// 8086 has. Neither instruction touches a flag, so there is no ALU kernel
// call anywhere in this file — it is operand plumbing, start to finish.
//
// Two silicon facts the vectors pin down, documented here because nothing
// else in the tree says them:
//
//   * `MOV r/m16, Sreg` (8C) and `MOV Sreg, r/m16` (8E) decode a segment
//     register out of the ModRM `reg` field, which is three bits wide,
//     but there are only four segment registers. The part does not
//     reject reg values 4-7 or trap on them: it wraps, using only the
//     low two bits of the field. That is not documented behaviour, it is
//     read straight off the vectors, and it is why the handlers below
//     mask with `& 3` rather than rejecting anything.
//   * Loading a segment register (8E only — 8C merely reads one) holds
//     off external interrupt recognition for one instruction, the same
//     window STI opens. See interrupts.h for why: the instant between
//     loading SS and loading the SP that goes with it must never be an
//     instant an interrupt can land in, and the 8086 does not special-case
//     which register was loaded to get that guarantee. The conformance
//     vectors never set IF, so they cannot exercise this; it is
//     implemented anyway because interrupts.h specifies it unconditionally.

#include <cstdint>

#include "amberfolio/cpu/decoder.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

// --- MOV, r/m and reg, either direction -------------------------------

/// r/m := reg.
void mov_rm_reg(processor& cpu, width w) {
  const std::uint16_t value = cpu.read_reg(w);
  cpu.write_rm(w, value);
}

/// reg := r/m.
void mov_reg_rm(processor& cpu, width w) {
  const std::uint16_t value = cpu.read_rm(w);
  cpu.write_reg(w, value);
}

// --- MOV, segment registers --------------------------------------------

/// The segment register named by the ModRM `reg` field, wrapped to the
/// four that exist. A fact about the silicon, not a defensive mask — see
/// the file header.
[[nodiscard]] sreg modrm_sreg(const processor& cpu) noexcept {
  return static_cast<sreg>(cpu.current().modrm.reg & 0x3u);
}

// --- MOV, accumulator and a direct address ------------------------------
//
// A0-A3 carry no ModRM byte: a 16-bit offset follows the opcode directly,
// and the decoder has not touched it or the effective address, so the
// handler fetches the offset and applies the segment override itself —
// DS unless a prefix said otherwise.

[[nodiscard]] address moffs_address(processor& cpu) {
  const std::uint16_t offset = cpu.fetch_word();
  const prefix_state& prefixes = cpu.current().prefixes;
  const sreg segment =
      prefixes.has_segment_override ? prefixes.segment_override : sreg::ds;
  return {.segment = cpu.regs()[segment], .offset = offset};
}

/// AL/AX := [moffs].
void mov_acc_moffs(processor& cpu, width w) {
  const address at = moffs_address(cpu);
  const std::uint16_t value = cpu.read(w, at);
  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(value));
  } else {
    cpu.regs()[reg16::ax] = value;
  }
}

/// [moffs] := AL/AX.
void mov_moffs_acc(processor& cpu, width w) {
  const address at = moffs_address(cpu);
  const std::uint16_t value = w == width::byte
                                  ? std::uint16_t{cpu.regs().get(reg8::al)}
                                  : cpu.regs()[reg16::ax];
  cpu.write(w, at, value);
}

// --- MOV, register and immediate ----------------------------------------
//
// B0-BF encode the destination register in the opcode itself, three bits
// low, so there is no ModRM byte and the immediate follows the opcode
// directly.

void mov_reg8_imm8(processor& cpu, reg8 r) {
  const std::uint8_t imm = cpu.fetch_byte();
  cpu.regs().set(r, imm);
}

void mov_reg16_imm16(processor& cpu, reg16 r) {
  const std::uint16_t imm = cpu.fetch_word();
  cpu.regs()[r] = imm;
}

// --- MOV, r/m and immediate ----------------------------------------------
//
// C6 and C7 are group-shaped encodings but are not in the group table
// (dispatch.h): the 8086 ignores their ModRM `reg` field rather than
// decoding it, so they get one handler each and never look at `reg`.

void mov_rm_imm(processor& cpu, width w) {
  const std::uint16_t imm =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();
  cpu.write_rm(w, imm);
}

// --- XCHG, r/m and reg ----------------------------------------------------

void xchg_rm_reg(processor& cpu, width w) {
  // Read both operands into locals before writing either: the read below
  // is not sensitive to evaluation order the way a two-argument ALU call
  // would be, but it is the same rule for the same reason, and it is what
  // keeps this correct if `rm` and `reg` name the same register.
  const std::uint16_t rm_value = cpu.read_rm(w);
  const std::uint16_t reg_value = cpu.read_reg(w);
  cpu.write_rm(w, reg_value);
  cpu.write_reg(w, rm_value);
}

// --- XCHG, AX and a register -----------------------------------------------
//
// 90-97 encode the other register in the opcode itself, the same way
// B8-BF do. 90 is AX with itself, which is XCHG's encoding of NOP; it
// goes through the same helper rather than a special case; swapping a
// register with itself is already a no-op.

void xchg_ax_reg16(processor& cpu, reg16 r) {
  const std::uint16_t ax_value = cpu.regs()[reg16::ax];
  const std::uint16_t reg_value = cpu.regs()[r];
  cpu.regs()[reg16::ax] = reg_value;
  cpu.regs()[r] = ax_value;
}

}  // namespace

// --- 88-8B: MOV, r/m <-> reg ----------------------------------------------

void mov_rm8_r8(processor& cpu) { mov_rm_reg(cpu, width::byte); }
void mov_rm16_r16(processor& cpu) { mov_rm_reg(cpu, width::word); }
void mov_r8_rm8(processor& cpu) { mov_reg_rm(cpu, width::byte); }
void mov_r16_rm16(processor& cpu) { mov_reg_rm(cpu, width::word); }

// --- 8C, 8E: MOV, r/m16 <-> Sreg -------------------------------------------

/// r/m16 := Sreg. A pure read of the segment register: no interrupt
/// window, because nothing about the stack or the address space has
/// changed.
void mov_rm16_sreg(processor& cpu) {
  const sreg s = modrm_sreg(cpu);
  const std::uint16_t value = cpu.regs()[s];
  cpu.write_rm(width::word, value);
}

/// Sreg := r/m16. This is the one that can hand the stack half a pointer
/// for one instruction (MOV SS, ax before the matching MOV SP, bx), so it
/// inhibits interrupt recognition across the next instruction.
void mov_sreg_rm16(processor& cpu) {
  const std::uint16_t value = cpu.read_rm(width::word);
  const sreg s = modrm_sreg(cpu);
  cpu.regs()[s] = value;
  cpu.inhibit_interrupts();
}

// --- A0-A3: MOV, accumulator <-> moffs -------------------------------------

void mov_al_moffs8(processor& cpu) { mov_acc_moffs(cpu, width::byte); }
void mov_ax_moffs16(processor& cpu) { mov_acc_moffs(cpu, width::word); }
void mov_moffs8_al(processor& cpu) { mov_moffs_acc(cpu, width::byte); }
void mov_moffs16_ax(processor& cpu) { mov_moffs_acc(cpu, width::word); }

// --- B0-B7: MOV, reg8 <- imm8 ----------------------------------------------

void mov_al_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::al); }
void mov_cl_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::cl); }
void mov_dl_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::dl); }
void mov_bl_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::bl); }
void mov_ah_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::ah); }
void mov_ch_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::ch); }
void mov_dh_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::dh); }
void mov_bh_imm8(processor& cpu) { mov_reg8_imm8(cpu, reg8::bh); }

// --- B8-BF: MOV, reg16 <- imm16 ---------------------------------------------

void mov_ax_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::ax); }
void mov_cx_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::cx); }
void mov_dx_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::dx); }
void mov_bx_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::bx); }
void mov_sp_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::sp); }
void mov_bp_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::bp); }
void mov_si_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::si); }
void mov_di_imm16(processor& cpu) { mov_reg16_imm16(cpu, reg16::di); }

// --- C6, C7: MOV, r/m <- imm -------------------------------------------------

void mov_rm8_imm8(processor& cpu) { mov_rm_imm(cpu, width::byte); }
void mov_rm16_imm16(processor& cpu) { mov_rm_imm(cpu, width::word); }

// --- 86, 87: XCHG, r/m <-> reg -----------------------------------------------

void xchg_rm8_r8(processor& cpu) { xchg_rm_reg(cpu, width::byte); }
void xchg_rm16_r16(processor& cpu) { xchg_rm_reg(cpu, width::word); }

// --- 90-97: XCHG, AX <-> reg16 (90 is NOP) -----------------------------------

void xchg_ax_ax(processor& cpu) { xchg_ax_reg16(cpu, reg16::ax); }
void xchg_ax_cx(processor& cpu) { xchg_ax_reg16(cpu, reg16::cx); }
void xchg_ax_dx(processor& cpu) { xchg_ax_reg16(cpu, reg16::dx); }
void xchg_ax_bx(processor& cpu) { xchg_ax_reg16(cpu, reg16::bx); }
void xchg_ax_sp(processor& cpu) { xchg_ax_reg16(cpu, reg16::sp); }
void xchg_ax_bp(processor& cpu) { xchg_ax_reg16(cpu, reg16::bp); }
void xchg_ax_si(processor& cpu) { xchg_ax_reg16(cpu, reg16::si); }
void xchg_ax_di(processor& cpu) { xchg_ax_reg16(cpu, reg16::di); }

}  // namespace amberfolio::cpu
