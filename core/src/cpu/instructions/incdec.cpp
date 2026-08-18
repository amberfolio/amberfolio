// SPDX-License-Identifier: AGPL-3.0-only
//
// INC and DEC (issue #22): 40-47 and 48-4F increment/decrement a reg16
// directly out of the opcode's own low three bits, and FE.0/FE.1/FF.0/FF.1
// do the same to an r/m8 or r/m16 operand. All six arithmetic flags but one
// come straight from the ALU kernel.
//
// The one that does not: CF. `alu::inc` and `alu::dec` hand back whatever
// CF was passed in, unchanged, and that is not an oversight to route around
// — it is the one thing that makes INC something other than ADD of 1, and
// it is why a multi-word increment loop cannot be built out of it (a real
// carry out of the low word would be silently swallowed). This file never
// touches CF; it just carries the flag word through.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// reg16 := reg16 + 1. Used by 40-47, where the register is the opcode's
/// own low three bits — already in reg16's own enum order (ax cx dx bx sp
/// bp si di), so the caller passes it straight through.
void inc_reg16(processor& cpu, reg16 r) {
  const std::uint16_t before = cpu.regs()[r];
  const alu::result res = alu::inc(width::word, before, cpu.regs().flags);
  cpu.regs()[r] = res.value;
  cpu.regs().flags = res.flags;
}

/// reg16 := reg16 - 1. Used by 48-4F, same register mapping as inc_reg16.
void dec_reg16(processor& cpu, reg16 r) {
  const std::uint16_t before = cpu.regs()[r];
  const alu::result res = alu::dec(width::word, before, cpu.regs().flags);
  cpu.regs()[r] = res.value;
  cpu.regs().flags = res.flags;
}

/// r/m := r/m + 1, at width `w`. Used by FE.0 (byte) and FF.0 (word).
void inc_rm(processor& cpu, width w) {
  const std::uint16_t before = cpu.read_rm(w);
  const alu::result res = alu::inc(w, before, cpu.regs().flags);
  cpu.write_rm(w, res.value);
  cpu.regs().flags = res.flags;
}

/// r/m := r/m - 1, at width `w`. Used by FE.1 (byte) and FF.1 (word).
void dec_rm(processor& cpu, width w) {
  const std::uint16_t before = cpu.read_rm(w);
  const alu::result res = alu::dec(w, before, cpu.regs().flags);
  cpu.write_rm(w, res.value);
  cpu.regs().flags = res.flags;
}

}  // namespace

void inc_ax(processor& cpu) { inc_reg16(cpu, reg16::ax); }
void inc_cx(processor& cpu) { inc_reg16(cpu, reg16::cx); }
void inc_dx(processor& cpu) { inc_reg16(cpu, reg16::dx); }
void inc_bx(processor& cpu) { inc_reg16(cpu, reg16::bx); }
void inc_sp(processor& cpu) { inc_reg16(cpu, reg16::sp); }
void inc_bp(processor& cpu) { inc_reg16(cpu, reg16::bp); }
void inc_si(processor& cpu) { inc_reg16(cpu, reg16::si); }
void inc_di(processor& cpu) { inc_reg16(cpu, reg16::di); }

void dec_ax(processor& cpu) { dec_reg16(cpu, reg16::ax); }
void dec_cx(processor& cpu) { dec_reg16(cpu, reg16::cx); }
void dec_dx(processor& cpu) { dec_reg16(cpu, reg16::dx); }
void dec_bx(processor& cpu) { dec_reg16(cpu, reg16::bx); }
void dec_sp(processor& cpu) { dec_reg16(cpu, reg16::sp); }
void dec_bp(processor& cpu) { dec_reg16(cpu, reg16::bp); }
void dec_si(processor& cpu) { dec_reg16(cpu, reg16::si); }
void dec_di(processor& cpu) { dec_reg16(cpu, reg16::di); }

void inc_rm8(processor& cpu) { inc_rm(cpu, width::byte); }
void dec_rm8(processor& cpu) { dec_rm(cpu, width::byte); }
void inc_rm16(processor& cpu) { inc_rm(cpu, width::word); }
void dec_rm16(processor& cpu) { dec_rm(cpu, width::word); }

}  // namespace amberfolio::cpu
