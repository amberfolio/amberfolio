// SPDX-License-Identifier: AGPL-3.0-only
//
// OR, AND, XOR, TEST and NOT (issue #21). AND/OR/XOR/TEST are thin wiring
// over the ALU kernel's `bit_and`/`bit_or`/`bit_xor`/`test`; NOT is not in
// the kernel at all, because on an 8086 it is the one operation that
// leaves every flag exactly where it found it.
//
// Three encodings repeat across OR/AND/XOR and are written once, generic
// over which ALU primitive to call:
//
//   - r/m <-> reg, with a direction bit (the *0/*1/*2/*3 opcodes);
//   - AL/AX with an immediate that follows the opcode directly (*4/*5);
//   - r/m with an immediate, which is the group-1 opcodes 80/81/82/83.
//     82 is a byte-immediate alias of 80 — both entries in the dispatch
//     table below point at the same handler. 83 is the sign-extended
//     imm8-into-word-op form; Intel does not document it for the logic
//     reg values (1/4/6) but the silicon has it anyway, so it is wired
//     the same as it would be for an arithmetic group-1 op.
//
// TEST shares the r/m<->reg and accumulator-immediate shapes with
// AND/OR/XOR but writes nothing back — it is `cmp` to their `sub`, and
// `alu::test` already returns flags only for exactly that reason. Its
// F6/F7 group forms are TEST-with-immediate, and reg values 0 and 1 are
// both wired to it: 8088 silicon decodes the "reg=1" slot of the F6/F7
// group as an undocumented alias of TEST rather than leaving it a trap,
// and the conformance vectors (stems F6.1/F7.1) confirm it byte for
// byte against reg=0.
//
// On AF: alu.cpp's `logic()` helper clears CF, OF and AF after every
// logical operation and says so is unconfirmed against real silicon,
// naming this issue as where that gets checked. All ~40 vector files
// enabled below — every OR/AND/XOR/TEST stem in this family, several
// thousand cases apiece — pass with AF cleared exactly as the kernel
// already does it. Intel's "undefined" was, on this part, "clear it".

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// Sign-extend a displacement or immediate byte, the way the encoding
/// means it. Spelled as a function over a named parameter rather than a
/// cast applied to a call's result, which is how processor.cpp writes the
/// same conversion and what keeps clang-tidy from reading it as the
/// accidental kind of signed-char widening.
[[nodiscard]] constexpr std::uint16_t sign_extend(std::uint8_t byte) noexcept {
  return static_cast<std::uint16_t>(static_cast<std::int8_t>(byte));
}

/// The signature `alu::bit_and`, `alu::bit_or` and `alu::bit_xor` share,
/// so the three shapes below take the operation as a parameter instead of
/// being written three times over.
using logic_op = alu::result (*)(width, std::uint16_t, std::uint16_t,
                                 std::uint16_t);

/// r/m := r/m OP reg, or reg := reg OP r/m — the direction bit picks which
/// operand is the destination. Both operands are read into locals before
/// either kernel call or write-back: argument evaluation order is
/// unspecified in C++ and both reads can touch the bus.
void logic_rm_reg(processor& cpu, width w, logic_op op) {
  const std::uint16_t dst = cpu.read_rm(w);
  const std::uint16_t src = cpu.read_reg(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_rm(w, r.value);
  cpu.regs().flags = r.flags;
}

void logic_reg_rm(processor& cpu, width w, logic_op op) {
  const std::uint16_t dst = cpu.read_reg(w);
  const std::uint16_t src = cpu.read_rm(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_reg(w, r.value);
  cpu.regs().flags = r.flags;
}

/// AL/AX := AL/AX OP imm. No ModRM byte, so the immediate follows the
/// opcode directly.
void logic_acc_imm(processor& cpu, width w, logic_op op) {
  const std::uint16_t dst = w == width::byte
                                ? std::uint16_t{cpu.regs().get(reg8::al)}
                                : cpu.regs()[reg16::ax];
  const std::uint16_t imm =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();

  const alu::result r = op(w, dst, imm, cpu.regs().flags);

  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(r.value));
  } else {
    cpu.regs()[reg16::ax] = r.value;
  }
  cpu.regs().flags = r.flags;
}

/// r/m := r/m OP imm8, group-1 opcodes 80 and 82 (82 is a byte-op alias of
/// 80; both dispatch entries point here). The immediate is fetched first:
/// it is the next byte in the instruction stream, ahead of the r/m read
/// that is part of executing the operation.
void logic_rm_imm8(processor& cpu, logic_op op) {
  const std::uint16_t imm = cpu.fetch_byte();
  const std::uint16_t dst = cpu.read_rm(width::byte);

  const alu::result r = op(width::byte, dst, imm, cpu.regs().flags);

  cpu.write_rm(width::byte, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m := r/m OP imm16, group-1 opcode 81.
void logic_rm_imm16(processor& cpu, logic_op op) {
  const std::uint16_t imm = cpu.fetch_word();
  const std::uint16_t dst = cpu.read_rm(width::word);

  const alu::result r = op(width::word, dst, imm, cpu.regs().flags);

  cpu.write_rm(width::word, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m := r/m OP sign-extend(imm8), group-1 opcode 83. Undocumented for
/// AND/OR/XOR specifically, but real: the sign-extension is a property of
/// the 83 encoding itself, not of which reg value rides on it.
void logic_rm_simm8(processor& cpu, logic_op op) {
  const std::uint16_t imm = sign_extend(cpu.fetch_byte());
  const std::uint16_t dst = cpu.read_rm(width::word);

  const alu::result r = op(width::word, dst, imm, cpu.regs().flags);

  cpu.write_rm(width::word, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m & reg, flags only.
void test_rm_reg(processor& cpu, width w) {
  const std::uint16_t a = cpu.read_rm(w);
  const std::uint16_t b = cpu.read_reg(w);
  cpu.regs().flags = alu::test(w, a, b, cpu.regs().flags);
}

/// AL/AX & imm, flags only.
void test_acc_imm(processor& cpu, width w) {
  const std::uint16_t a = w == width::byte
                              ? std::uint16_t{cpu.regs().get(reg8::al)}
                              : cpu.regs()[reg16::ax];
  const std::uint16_t imm =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();
  cpu.regs().flags = alu::test(w, a, imm, cpu.regs().flags);
}

/// r/m & imm, flags only — the F6/F7 group's reg=0 slot, and reg=1's
/// undocumented alias of it (see the file header).
void test_rm_imm(processor& cpu, width w) {
  const std::uint16_t imm =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();
  const std::uint16_t a = cpu.read_rm(w);
  cpu.regs().flags = alu::test(w, a, imm, cpu.regs().flags);
}

/// r/m := ~r/m. No flags at all — NOT is absent from the ALU kernel for
/// exactly that reason, so there is nothing here to route through it.
void logic_not(processor& cpu, width w) {
  const std::uint16_t v = cpu.read_rm(w);
  cpu.write_rm(w, truncate(w, static_cast<std::uint16_t>(~v)));
}

}  // namespace

// --- OR ----------------------------------------------------------------

void or_rm8_r8(processor& cpu) { logic_rm_reg(cpu, width::byte, &alu::bit_or); }
void or_rm16_r16(processor& cpu) {
  logic_rm_reg(cpu, width::word, &alu::bit_or);
}
void or_r8_rm8(processor& cpu) { logic_reg_rm(cpu, width::byte, &alu::bit_or); }
void or_r16_rm16(processor& cpu) {
  logic_reg_rm(cpu, width::word, &alu::bit_or);
}
void or_al_imm8(processor& cpu) {
  logic_acc_imm(cpu, width::byte, &alu::bit_or);
}
void or_ax_imm16(processor& cpu) {
  logic_acc_imm(cpu, width::word, &alu::bit_or);
}
void or_rm8_imm8(processor& cpu) { logic_rm_imm8(cpu, &alu::bit_or); }
void or_rm16_imm16(processor& cpu) { logic_rm_imm16(cpu, &alu::bit_or); }
void or_rm16_imm8(processor& cpu) { logic_rm_simm8(cpu, &alu::bit_or); }

// --- AND -----------------------------------------------------------------

void and_rm8_r8(processor& cpu) {
  logic_rm_reg(cpu, width::byte, &alu::bit_and);
}
void and_rm16_r16(processor& cpu) {
  logic_rm_reg(cpu, width::word, &alu::bit_and);
}
void and_r8_rm8(processor& cpu) {
  logic_reg_rm(cpu, width::byte, &alu::bit_and);
}
void and_r16_rm16(processor& cpu) {
  logic_reg_rm(cpu, width::word, &alu::bit_and);
}
void and_al_imm8(processor& cpu) {
  logic_acc_imm(cpu, width::byte, &alu::bit_and);
}
void and_ax_imm16(processor& cpu) {
  logic_acc_imm(cpu, width::word, &alu::bit_and);
}
void and_rm8_imm8(processor& cpu) { logic_rm_imm8(cpu, &alu::bit_and); }
void and_rm16_imm16(processor& cpu) { logic_rm_imm16(cpu, &alu::bit_and); }
void and_rm16_imm8(processor& cpu) { logic_rm_simm8(cpu, &alu::bit_and); }

// --- XOR -----------------------------------------------------------------

void xor_rm8_r8(processor& cpu) {
  logic_rm_reg(cpu, width::byte, &alu::bit_xor);
}
void xor_rm16_r16(processor& cpu) {
  logic_rm_reg(cpu, width::word, &alu::bit_xor);
}
void xor_r8_rm8(processor& cpu) {
  logic_reg_rm(cpu, width::byte, &alu::bit_xor);
}
void xor_r16_rm16(processor& cpu) {
  logic_reg_rm(cpu, width::word, &alu::bit_xor);
}
void xor_al_imm8(processor& cpu) {
  logic_acc_imm(cpu, width::byte, &alu::bit_xor);
}
void xor_ax_imm16(processor& cpu) {
  logic_acc_imm(cpu, width::word, &alu::bit_xor);
}
void xor_rm8_imm8(processor& cpu) { logic_rm_imm8(cpu, &alu::bit_xor); }
void xor_rm16_imm16(processor& cpu) { logic_rm_imm16(cpu, &alu::bit_xor); }
void xor_rm16_imm8(processor& cpu) { logic_rm_simm8(cpu, &alu::bit_xor); }

// --- TEST ------------------------------------------------------------

void test_rm8_r8(processor& cpu) { test_rm_reg(cpu, width::byte); }
void test_rm16_r16(processor& cpu) { test_rm_reg(cpu, width::word); }
void test_al_imm8(processor& cpu) { test_acc_imm(cpu, width::byte); }
void test_ax_imm16(processor& cpu) { test_acc_imm(cpu, width::word); }
void test_rm8_imm8(processor& cpu) { test_rm_imm(cpu, width::byte); }
void test_rm16_imm16(processor& cpu) { test_rm_imm(cpu, width::word); }

// --- NOT -----------------------------------------------------------------

void not_rm8(processor& cpu) { logic_not(cpu, width::byte); }
void not_rm16(processor& cpu) { logic_not(cpu, width::word); }

}  // namespace amberfolio::cpu
