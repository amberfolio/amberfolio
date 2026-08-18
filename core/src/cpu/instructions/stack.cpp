// SPDX-License-Identifier: AGPL-3.0-only
//
// The stack family (issue #23): PUSH, POP, PUSHF, POPF. Every encoding
// moves exactly one word through processor::push_word / pop_word, which
// already own the SP arithmetic and its 16-bit wrap (processor.h); this
// file is the operand plumbing on top of that, and, for PUSH SP, the one
// genuine silicon quirk in the family.

#include <cstdint>

#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// PUSH reg16 (50-57). `push_word` decrements SP and then writes at the
/// new value, so for every register but SP the value to push is simply
/// what the register holds. SP is the exception: on the 8086/8088 the
/// value pushed is SP *after* that decrement, not before — the part
/// reads the register from the same place the write is about to use.
/// (The 80286 and later push the original value instead; the 54 vectors
/// are what confirm this part does not.) Computing SP-2 here and handing
/// it to push_word as an ordinary value reproduces that without push_word
/// having to know which register is being pushed.
void push_reg16(processor& cpu, reg16 r) {
  if (r == reg16::sp) {
    const auto decremented =
        static_cast<std::uint16_t>(cpu.regs()[reg16::sp] - 2);
    cpu.push_word(decremented);
  } else {
    cpu.push_word(cpu.regs()[r]);
  }
}

/// POP reg16 (58-5F). No special case for SP: pop_word reads at the old
/// SP and then increments it, and this simply overwrites the destination
/// register — SP included — with the popped value afterwards. For POP SP
/// that discards the intermediate incremented value in favour of
/// whatever was on the stack, which is exactly what the part does.
void pop_reg16(processor& cpu, reg16 r) { cpu.regs()[r] = cpu.pop_word(); }

void push_sreg(processor& cpu, sreg s) { cpu.push_word(cpu.regs()[s]); }

/// POP ES/SS/DS (07/17/1F). Loading any segment register holds off
/// external interrupt recognition for one instruction (interrupts.h) —
/// the SS/SP two-instruction pair is why the mechanism exists, and this
/// part does not distinguish which register was loaded, so the call is
/// unconditional here too. The vectors cannot observe this; it is done
/// because interrupts.h specifies it, not because a test demands it.
void pop_sreg(processor& cpu, sreg s) {
  cpu.regs()[s] = cpu.pop_word();
  cpu.inhibit_interrupts();
}

}  // namespace

void push_ax(processor& cpu) { push_reg16(cpu, reg16::ax); }
void push_cx(processor& cpu) { push_reg16(cpu, reg16::cx); }
void push_dx(processor& cpu) { push_reg16(cpu, reg16::dx); }
void push_bx(processor& cpu) { push_reg16(cpu, reg16::bx); }
void push_sp(processor& cpu) { push_reg16(cpu, reg16::sp); }
void push_bp(processor& cpu) { push_reg16(cpu, reg16::bp); }
void push_si(processor& cpu) { push_reg16(cpu, reg16::si); }
void push_di(processor& cpu) { push_reg16(cpu, reg16::di); }

void pop_ax(processor& cpu) { pop_reg16(cpu, reg16::ax); }
void pop_cx(processor& cpu) { pop_reg16(cpu, reg16::cx); }
void pop_dx(processor& cpu) { pop_reg16(cpu, reg16::dx); }
void pop_bx(processor& cpu) { pop_reg16(cpu, reg16::bx); }
void pop_sp(processor& cpu) { pop_reg16(cpu, reg16::sp); }
void pop_bp(processor& cpu) { pop_reg16(cpu, reg16::bp); }
void pop_si(processor& cpu) { pop_reg16(cpu, reg16::si); }
void pop_di(processor& cpu) { pop_reg16(cpu, reg16::di); }

void push_es(processor& cpu) { push_sreg(cpu, sreg::es); }
void push_cs(processor& cpu) { push_sreg(cpu, sreg::cs); }
void push_ss(processor& cpu) { push_sreg(cpu, sreg::ss); }
void push_ds(processor& cpu) { push_sreg(cpu, sreg::ds); }

// There is deliberately no pop_cs: 0F (POP CS) exists on this part but has
// no vector file and belongs to the misc family (#33), not this one.
void pop_es(processor& cpu) { pop_sreg(cpu, sreg::es); }
void pop_ss(processor& cpu) { pop_sreg(cpu, sreg::ss); }
void pop_ds(processor& cpu) { pop_sreg(cpu, sreg::ds); }

/// PUSH r/m16 (FF /6, and its undocumented alias FF /7 — dispatch.cpp
/// wires both group entries to this same handler). There is no r/m
/// addressing mode based on SP (bx+si, bx+di, bp+si, bp+di, si, di,
/// bp+disp, bx — never sp), so a *memory* operand can never depend on the
/// decrement push_word is about to make. But mod 11 rm 100 is r/m = SP as
/// a register, which is register-direct PUSH SP under this encoding
/// instead of 50-57's — and the FF.6/FF.7 vectors show the identical
/// already-decremented-value quirk on it, so it is routed through the
/// same push_reg16 that opcode 54 uses rather than a plain read-then-push.
void push_rm16(processor& cpu) {
  const modrm& m = cpu.current().modrm;
  if (m.names_a_register()) {
    push_reg16(cpu, static_cast<reg16>(m.rm));
    return;
  }
  // Read the operand into a local before pushing: push_word moves SP, and
  // the harness compares what the CPU asked memory for, so read-then-push
  // is the order that matters even though this operand cannot be SP.
  const std::uint16_t value = cpu.read_rm(width::word);
  cpu.push_word(value);
}

/// POP r/m16 (8F). This encoding looks like a group opcode but is not
/// one: the 8086 ignores its ModRM reg field entirely rather than
/// decoding it (dispatch.h), so it gets one primary-table handler like
/// any other opcode instead of eight group entries. Pop first, write
/// second — that is the order the part uses, and it is also what makes
/// r/m = SP (mod 11, rm 100) land the popped value in SP rather than the
/// intermediate incremented one, the same as pop_reg16 above.
void pop_rm16(processor& cpu) {
  const std::uint16_t value = cpu.pop_word();
  cpu.write_rm(width::word, value);
}

/// PUSHF (9C). regs().flags is always normalized (registers.h): the
/// always-one bits (1, 12-15) and always-zero bits (3, 5) already read
/// back the way the part would present them, so pushing the word straight
/// through is correct — confirmed against the vectors.
void pushf(processor& cpu) { cpu.push_word(cpu.regs().flags); }

/// POPF (9D). The popped word comes from the program, so it goes through
/// load_flags rather than a direct assignment to regs().flags — that is
/// the one path that re-normalizes bits 1, 3, 5 and 12-15 regardless of
/// what the program put there. Whether a newly-set TF or IF takes effect
/// this instruction or the next is processor::step()'s business (#17);
/// this handler only has to get the flag values themselves right.
void popf(processor& cpu) { cpu.regs().load_flags(cpu.pop_word()); }

}  // namespace amberfolio::cpu
