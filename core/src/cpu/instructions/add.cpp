// SPDX-License-Identifier: AGPL-3.0-only
//
// ADD and ADC (issue #19): addition with and without carry, in all of
// their encodings. All flag effects come from the ALU kernel (alu::add,
// alu::adc) — this file is operand plumbing and nothing else.
//
// Ten encodings apiece, and they fall into four shapes shared between the
// two operations:
//
//   - r/m <-> reg, byte and word, in both directions (00-03, 10-13)
//   - AL/AX with an immediate that follows the opcode directly (04 05,
//     14 15)
//   - the group-1 r/m-with-immediate forms, reg field 0 for ADD and 2 for
//     ADC: 80 (byte r/m, byte imm), 81 (word r/m, word imm), 83 (word
//     r/m, byte imm sign-extended to a word)
//
// 82 is a documented alias of 80 on the 8086 — same byte r/m, byte
// immediate encoding, reachable a second way because the w bit that
// would normally distinguish 80 from 81 is duplicated into a bit the
// 8086 does not actually decode for the /2 (register-destination) case.
// It gets its own dispatch line, per the one-line rule, but shares 80's
// handler rather than restating it.
//
// Each shape below is written once, parameterized over which ALU
// primitive to call, and the fourteen dispatch targets are thin
// instantiations of it — genuinely just wiring, as the issue predicted.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// One of the ALU kernel's two-operand, flags-and-value primitives — the
/// shape `add` and `adc` share, and the thing every helper below is
/// parameterized over so it does not care which of the two it is wiring.
using alu_op = alu::result (*)(width, std::uint16_t, std::uint16_t,
                               std::uint16_t);

/// r/m := r/m `op` reg. Opcodes 00/01 (ADD) and 10/11 (ADC).
void rm_op_reg(processor& cpu, width w, alu_op op) {
  // Read both operands into locals before calling the kernel: argument
  // evaluation order is unspecified in C++, and read_rm can touch the
  // bus. The harness compares which addresses the CPU asked for, so the
  // order in which they are read is observable.
  const std::uint16_t dst = cpu.read_rm(w);
  const std::uint16_t src = cpu.read_reg(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_rm(w, r.value);
  // The whole word, not an OR: the kernel returns a complete flag word
  // with everything it does not touch carried through unchanged.
  cpu.regs().flags = r.flags;
}

/// reg := reg `op` r/m. Opcodes 02/03 (ADD) and 12/13 (ADC) — the same
/// operation as above with the operands' roles swapped, per the d bit.
void reg_op_rm(processor& cpu, width w, alu_op op) {
  const std::uint16_t dst = cpu.read_reg(w);
  const std::uint16_t src = cpu.read_rm(w);

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_reg(w, r.value);
  cpu.regs().flags = r.flags;
}

/// AL/AX := AL/AX `op` imm. Opcodes 04/05 (ADD) and 14/15 (ADC). No
/// ModRM byte, so the immediate follows the opcode directly and the
/// handler fetches it itself.
void acc_op_imm(processor& cpu, width w, alu_op op) {
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

/// r/m := r/m `op` imm, immediate the same width as the operand. Group-1
/// reg-field 0/2, opcodes 80 (byte, and its 82 alias) and 81 (word).
void rm_op_imm(processor& cpu, width w, alu_op op) {
  const std::uint16_t dst = cpu.read_rm(w);
  const std::uint16_t src =
      w == width::byte ? std::uint16_t{cpu.fetch_byte()} : cpu.fetch_word();

  const alu::result r = op(w, dst, src, cpu.regs().flags);

  cpu.write_rm(w, r.value);
  cpu.regs().flags = r.flags;
}

/// r/m16 := r/m16 `op` sign-extended imm8. Opcode 83, group-1 reg-field
/// 0/2. The immediate is always a byte regardless of the r/m width this
/// group otherwise carries; 83 only ever addresses a word operand.
void rm_op_imm8_sx(processor& cpu, alu_op op) {
  const std::uint16_t dst = cpu.read_rm(width::word);
  // Sign-extend through a signed type, not a bare cast: a value with bit
  // 7 set has to become 0xFFxx, not 0x00xx.
  const auto imm8 = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto src = static_cast<std::uint16_t>(static_cast<std::int16_t>(imm8));

  const alu::result r = op(width::word, dst, src, cpu.regs().flags);

  cpu.write_rm(width::word, r.value);
  cpu.regs().flags = r.flags;
}

}  // namespace

// --- ADD ---------------------------------------------------------------

void add_rm8_r8(processor& cpu) { rm_op_reg(cpu, width::byte, &alu::add); }
void add_rm16_r16(processor& cpu) { rm_op_reg(cpu, width::word, &alu::add); }
void add_r8_rm8(processor& cpu) { reg_op_rm(cpu, width::byte, &alu::add); }
void add_r16_rm16(processor& cpu) { reg_op_rm(cpu, width::word, &alu::add); }
void add_al_imm8(processor& cpu) { acc_op_imm(cpu, width::byte, &alu::add); }
void add_ax_imm16(processor& cpu) { acc_op_imm(cpu, width::word, &alu::add); }

/// 80 /0: byte r/m, byte immediate. Also 82 /0 — the documented 8086
/// alias, wired to this same handler by its own dispatch line.
void add_rm8_imm8(processor& cpu) { rm_op_imm(cpu, width::byte, &alu::add); }
/// 81 /0: word r/m, word immediate.
void add_rm16_imm16(processor& cpu) { rm_op_imm(cpu, width::word, &alu::add); }
/// 83 /0: word r/m, byte immediate sign-extended to a word.
void add_rm16_imm8(processor& cpu) { rm_op_imm8_sx(cpu, &alu::add); }

// --- ADC -----------------------------------------------------------------

void adc_rm8_r8(processor& cpu) { rm_op_reg(cpu, width::byte, &alu::adc); }
void adc_rm16_r16(processor& cpu) { rm_op_reg(cpu, width::word, &alu::adc); }
void adc_r8_rm8(processor& cpu) { reg_op_rm(cpu, width::byte, &alu::adc); }
void adc_r16_rm16(processor& cpu) { reg_op_rm(cpu, width::word, &alu::adc); }
void adc_al_imm8(processor& cpu) { acc_op_imm(cpu, width::byte, &alu::adc); }
void adc_ax_imm16(processor& cpu) { acc_op_imm(cpu, width::word, &alu::adc); }

/// 80 /2: byte r/m, byte immediate. Also 82 /2, the same alias as above.
void adc_rm8_imm8(processor& cpu) { rm_op_imm(cpu, width::byte, &alu::adc); }
/// 81 /2: word r/m, word immediate.
void adc_rm16_imm16(processor& cpu) { rm_op_imm(cpu, width::word, &alu::adc); }
/// 83 /2: word r/m, byte immediate sign-extended to a word.
void adc_rm16_imm8(processor& cpu) { rm_op_imm8_sx(cpu, &alu::adc); }

}  // namespace amberfolio::cpu
