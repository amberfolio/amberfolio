// SPDX-License-Identifier: AGPL-3.0-only
//
// Software interrupts (issue #31): INT3, INT imm8, INTO, and the IRET
// that unwinds any of them. Three of the four are a line of logic and a
// call into `processor::deliver_interrupt` — interrupts.h owns the
// sequence, and this file never restates it.

#include <cstdint>

#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

/// INT3 — the one-byte breakpoint a debugger writes over an instruction.
/// Always vector 3, no immediate to fetch.
void int3(processor& cpu) {
  cpu.deliver_interrupt(interrupt_vector::breakpoint);
}

/// INT imm8 — a program-chosen vector. The immediate is fetched *before*
/// delivery, which is what leaves IP already past the whole instruction
/// for the return address delivery pushes; nothing here adjusts IP by
/// hand.
void int_imm8(processor& cpu) {
  const std::uint8_t vector = cpu.fetch_byte();
  cpu.deliver_interrupt(vector);
}

/// INTO — vector 4, but only when OF is set. With OF clear this is a
/// fall-through: no stack traffic, no flag change, nothing for delivery
/// to do.
void into(processor& cpu) {
  if (cpu.regs().flag_set(flag::of)) {
    cpu.deliver_interrupt(interrupt_vector::overflow);
  }
}

/// IRET — the other direction. Three pops in the order delivery pushed
/// them: IP, then CS, then FLAGS. The flag word came from the program
/// (it was on the stack, put there by an earlier delivery or by the
/// program itself), so it goes through `load_flags` rather than a direct
/// assignment — the same rule POPF and SAHF follow.
void iret(processor& cpu) {
  const std::uint16_t ip = cpu.pop_word();
  const std::uint16_t cs = cpu.pop_word();
  const std::uint16_t new_flags = cpu.pop_word();

  cpu.regs().ip = ip;
  cpu.regs()[sreg::cs] = cs;
  cpu.regs().load_flags(new_flags);
}

}  // namespace amberfolio::cpu
