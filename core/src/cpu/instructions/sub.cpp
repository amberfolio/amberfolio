// SPDX-License-Identifier: AGPL-3.0-only
//
// SUB, SBB, CMP and NEG (issue #20). All four are the same subtraction at
// bottom — SBB is SUB with a borrow-in, CMP is SUB that only keeps the
// flags, and NEG is SUB from zero — so this file is thin wiring over
// `alu::sub`, `alu::sbb`, `alu::cmp` and `alu::neg`, plus the operand
// plumbing each encoding needs to get there.
//
// 82 is not implemented separately: on the 8086 it decodes exactly as 80
// does (byte operand, byte immediate) and dispatch.cpp wires it to the
// same handlers. 83 sign-extends its immediate byte to 16 bits before the
// operation runs, which is the one place this file does arithmetic of its
// own rather than handing raw bytes to the kernel.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// The shape `alu::sub` and `alu::sbb` share: an old flag word in, a value
/// and a complete new flag word out. Parameterizing the operand plumbing
/// below on this is what keeps SUB and SBB from being the same code typed
/// twice.
using binary_op = alu::result (*)(width, std::uint16_t, std::uint16_t,
                                  std::uint16_t) noexcept;

/// r/m := r/m op reg.
void rm_op_reg(processor& cpu, width w, binary_op op) {
  // Read both operands into locals before calling the kernel. Argument
  // evaluation order is unspecified in C++, and read_rm can touch the
  // bus — the harness compares what the CPU asked memory for, so the
  // order is observable.
  const std::uint16_t dst = cpu.read_rm(w);
  const std::uint16_t src = cpu.read_reg(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_rm(w, r.value);
  cpu.regs().flags = r.flags;
}

/// reg := reg op r/m.
void reg_op_rm(processor& cpu, width w, binary_op op) {
  const std::uint16_t dst = cpu.read_reg(w);
  const std::uint16_t src = cpu.read_rm(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_reg(w, r.value);
  cpu.regs().flags = r.flags;
}

/// AL/AX := AL/AX op imm. No ModRM byte, so the immediate follows the
/// opcode directly and the handler fetches it.
void acc_op_imm(processor& cpu, width w, binary_op op) {
  const std::uint16_t dst = w == width::byte
                                ? std::uint16_t{cpu.regs().get(reg8::al)}
                                : cpu.regs()[reg16::ax];
  const std::uint16_t src =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(r.value));
  } else {
    cpu.regs()[reg16::ax] = r.value;
  }
  cpu.regs().flags = r.flags;
}

/// r/m := r/m op imm8. The 80 group, and 82 aliased onto it.
void rm_op_imm8(processor& cpu, binary_op op) {
  const std::uint16_t dst = cpu.read_rm(width::byte);
  const std::uint16_t src = cpu.fetch_byte();

  const alu::result r = op(width::byte, dst, src, cpu.regs().flags);

  cpu.write_rm(width::byte, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m := r/m op imm16. The 81 group.
void rm_op_imm16(processor& cpu, binary_op op) {
  const std::uint16_t dst = cpu.read_rm(width::word);
  const std::uint16_t src = cpu.fetch_word();

  const alu::result r = op(width::word, dst, src, cpu.regs().flags);

  cpu.write_rm(width::word, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m := r/m op sign-extend(imm8). The 83 group: a word operation whose
/// immediate is one byte, sign-extended to 16 bits before it meets the
/// kernel — a static_cast through std::int8_t, not a bare truncating one,
/// so 0x80 becomes 0xFF80 and not 0x0080.
void rm_op_imm8_sext(processor& cpu, binary_op op) {
  const std::uint16_t dst = cpu.read_rm(width::word);
  const auto byte = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto src = static_cast<std::uint16_t>(static_cast<std::int16_t>(byte));

  const alu::result r = op(width::word, dst, src, cpu.regs().flags);

  cpu.write_rm(width::word, r.value);
  cpu.regs().flags = r.flags;
}

// --- CMP's own plumbing. alu::cmp returns flags alone, precisely so that
// no caller can write a value back — there is no `binary_op`-shaped
// version of it, so CMP gets its own small set of the functions above
// rather than sharing them.

void cmp_rm_reg(processor& cpu, width w) {
  const std::uint16_t a = cpu.read_rm(w);
  const std::uint16_t b = cpu.read_reg(w);
  cpu.regs().flags = alu::cmp(w, a, b, cpu.regs().flags);
}

void cmp_reg_rm(processor& cpu, width w) {
  const std::uint16_t a = cpu.read_reg(w);
  const std::uint16_t b = cpu.read_rm(w);
  cpu.regs().flags = alu::cmp(w, a, b, cpu.regs().flags);
}

void cmp_acc_imm(processor& cpu, width w) {
  const std::uint16_t a = w == width::byte
                              ? std::uint16_t{cpu.regs().get(reg8::al)}
                              : cpu.regs()[reg16::ax];
  const std::uint16_t b =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();
  cpu.regs().flags = alu::cmp(w, a, b, cpu.regs().flags);
}

void cmp_group_imm8(processor& cpu) {
  const std::uint16_t a = cpu.read_rm(width::byte);
  const std::uint16_t b = cpu.fetch_byte();
  cpu.regs().flags = alu::cmp(width::byte, a, b, cpu.regs().flags);
}

void cmp_group_imm16(processor& cpu) {
  const std::uint16_t a = cpu.read_rm(width::word);
  const std::uint16_t b = cpu.fetch_word();
  cpu.regs().flags = alu::cmp(width::word, a, b, cpu.regs().flags);
}

void cmp_group_imm8_sext(processor& cpu) {
  const std::uint16_t a = cpu.read_rm(width::word);
  const auto byte = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto b = static_cast<std::uint16_t>(static_cast<std::int16_t>(byte));
  cpu.regs().flags = alu::cmp(width::word, a, b, cpu.regs().flags);
}

// --- NEG. F6/3 and F7/3 of the F6/F7 group; the other seven entries of
// that group belong to the logic, MUL and DIV families and are none of
// this file's business.

void neg_op(processor& cpu, width w) {
  const std::uint16_t a = cpu.read_rm(w);
  const alu::result r = alu::neg(w, a, cpu.regs().flags);
  cpu.write_rm(w, r.value);
  cpu.regs().flags = r.flags;
}

}  // namespace

// --- SUB ---------------------------------------------------------------

void sub_rm8_r8(processor& cpu) { rm_op_reg(cpu, width::byte, &alu::sub); }
void sub_rm16_r16(processor& cpu) { rm_op_reg(cpu, width::word, &alu::sub); }
void sub_r8_rm8(processor& cpu) { reg_op_rm(cpu, width::byte, &alu::sub); }
void sub_r16_rm16(processor& cpu) { reg_op_rm(cpu, width::word, &alu::sub); }
void sub_al_imm8(processor& cpu) { acc_op_imm(cpu, width::byte, &alu::sub); }
void sub_ax_imm16(processor& cpu) { acc_op_imm(cpu, width::word, &alu::sub); }
void sub_rm8_imm8(processor& cpu) { rm_op_imm8(cpu, &alu::sub); }
void sub_rm16_imm16(processor& cpu) { rm_op_imm16(cpu, &alu::sub); }
void sub_rm16_imm8(processor& cpu) { rm_op_imm8_sext(cpu, &alu::sub); }

// --- SBB -----------------------------------------------------------------

void sbb_rm8_r8(processor& cpu) { rm_op_reg(cpu, width::byte, &alu::sbb); }
void sbb_rm16_r16(processor& cpu) { rm_op_reg(cpu, width::word, &alu::sbb); }
void sbb_r8_rm8(processor& cpu) { reg_op_rm(cpu, width::byte, &alu::sbb); }
void sbb_r16_rm16(processor& cpu) { reg_op_rm(cpu, width::word, &alu::sbb); }
void sbb_al_imm8(processor& cpu) { acc_op_imm(cpu, width::byte, &alu::sbb); }
void sbb_ax_imm16(processor& cpu) { acc_op_imm(cpu, width::word, &alu::sbb); }
void sbb_rm8_imm8(processor& cpu) { rm_op_imm8(cpu, &alu::sbb); }
void sbb_rm16_imm16(processor& cpu) { rm_op_imm16(cpu, &alu::sbb); }
void sbb_rm16_imm8(processor& cpu) { rm_op_imm8_sext(cpu, &alu::sbb); }

// --- CMP -----------------------------------------------------------------

void cmp_rm8_r8(processor& cpu) { cmp_rm_reg(cpu, width::byte); }
void cmp_rm16_r16(processor& cpu) { cmp_rm_reg(cpu, width::word); }
void cmp_r8_rm8(processor& cpu) { cmp_reg_rm(cpu, width::byte); }
void cmp_r16_rm16(processor& cpu) { cmp_reg_rm(cpu, width::word); }
void cmp_al_imm8(processor& cpu) { cmp_acc_imm(cpu, width::byte); }
void cmp_ax_imm16(processor& cpu) { cmp_acc_imm(cpu, width::word); }
void cmp_rm8_imm8(processor& cpu) { cmp_group_imm8(cpu); }
void cmp_rm16_imm16(processor& cpu) { cmp_group_imm16(cpu); }
void cmp_rm16_imm8(processor& cpu) { cmp_group_imm8_sext(cpu); }

// --- NEG -----------------------------------------------------------------

void neg_rm8(processor& cpu) { neg_op(cpu, width::byte); }
void neg_rm16(processor& cpu) { neg_op(cpu, width::word); }

}  // namespace amberfolio::cpu
