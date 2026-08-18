// SPDX-License-Identifier: AGPL-3.0-only
//
// Flags, I/O, ESC and the miscellaneous leftovers (issue #33): the seven
// single-bit flag writes, the eight port instructions, undocumented SALC,
// the eight coprocessor escapes, and four opcodes with no conformance
// vectors at all — 0F POP CS, 9B WAIT, and F4 HLT (F0/F1 LOCK are
// prefixes the decoder already consumes and never reach this file).
//
// Nothing here derives a flag from first principles: every handler either
// writes one bit with registers::set_flag or leaves the flag word alone
// entirely, so there is no ALU kernel call anywhere in this file.

#include <cstdint>

#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

// --- Flag ops -----------------------------------------------------------
//
// Each of these is a single bit write (or, for CMC, a single bit flip)
// and nothing else. The vectors never set IF or TF, so there is nothing
// here for the interrupt machinery to catch — but STI still calls
// inhibit_interrupts(), because interrupts.h specifies the one-instruction
// recognition delay as a fact about the part, not as something the
// vectors happen to check.

void complement_carry(processor& cpu) {
  cpu.regs().set_flag(flag::cf, !cpu.regs().flag_set(flag::cf));
}

void clear_carry(processor& cpu) { cpu.regs().set_flag(flag::cf, false); }

void set_carry(processor& cpu) { cpu.regs().set_flag(flag::cf, true); }

void clear_interrupt_flag(processor& cpu) {
  cpu.regs().set_flag(flag::if_, false);
}

void set_interrupt_flag(processor& cpu) {
  cpu.regs().set_flag(flag::if_, true);
  // The one-instruction recognition delay: STI is not itself the reason
  // an interrupt cannot land on the very next boundary, this call is.
  cpu.inhibit_interrupts();
}

void clear_direction_flag(processor& cpu) {
  cpu.regs().set_flag(flag::df, false);
}

void set_direction_flag(processor& cpu) { cpu.regs().set_flag(flag::df, true); }

// --- I/O ------------------------------------------------------------------
//
// A word-width port access goes through read_port16/write_port16 and lets
// the bus decide how it turns into byte cycles — bus.h's default is two,
// low half at `port` then `port + 1`, which is what the conformance
// harness's port script expects to see. Open-coding the two halves here
// would work out the same value but issue the transactions itself instead
// of letting the bus own that decision, which is exactly the layering
// bus.h's comment on read_port16 argues against.

void in_al_imm8(processor& cpu) {
  const std::uint16_t port = cpu.fetch_byte();
  cpu.regs().set(reg8::al, cpu.machine_bus().read_port8(port));
}

void in_ax_imm8(processor& cpu) {
  const std::uint16_t port = cpu.fetch_byte();
  cpu.regs()[reg16::ax] = cpu.machine_bus().read_port16(port);
}

void out_imm8_al(processor& cpu) {
  const std::uint16_t port = cpu.fetch_byte();
  const std::uint8_t value = cpu.regs().get(reg8::al);
  cpu.machine_bus().write_port8(port, value);
}

void out_imm8_ax(processor& cpu) {
  const std::uint16_t port = cpu.fetch_byte();
  const std::uint16_t value = cpu.regs()[reg16::ax];
  cpu.machine_bus().write_port16(port, value);
}

void in_al_dx(processor& cpu) {
  const std::uint16_t port = cpu.regs()[reg16::dx];
  cpu.regs().set(reg8::al, cpu.machine_bus().read_port8(port));
}

void in_ax_dx(processor& cpu) {
  const std::uint16_t port = cpu.regs()[reg16::dx];
  cpu.regs()[reg16::ax] = cpu.machine_bus().read_port16(port);
}

void out_dx_al(processor& cpu) {
  const std::uint16_t port = cpu.regs()[reg16::dx];
  const std::uint8_t value = cpu.regs().get(reg8::al);
  cpu.machine_bus().write_port8(port, value);
}

void out_dx_ax(processor& cpu) {
  const std::uint16_t port = cpu.regs()[reg16::dx];
  const std::uint16_t value = cpu.regs()[reg16::ax];
  cpu.machine_bus().write_port16(port, value);
}

// --- SALC -------------------------------------------------------------

void salc(processor& cpu) {
  const std::uint8_t value =
      cpu.regs().flag_set(flag::cf) ? std::uint8_t{0xFF} : std::uint8_t{0x00};
  cpu.regs().set(reg8::al, value);
}

// --- ESC ----------------------------------------------------------------

void escape(processor& cpu) {
  // The decoder has already consumed the ModRM byte (and any
  // displacement) and, when it names memory, computed the effective
  // address into current().ea. There is no 8087 in this machine to hand
  // the value to, but the base part still puts it on the bus on the
  // coprocessor's behalf — that bus cycle is observable (it is in the
  // conformance vectors' read list), so it has to happen even though
  // nothing here uses the result. A mod-3 form names a register instead,
  // and read_rm reading one touches no bus at all, so this one call is
  // correct for both forms.
  (void)cpu.read_rm(width::word);
}

// --- Unvectored opcodes -------------------------------------------------

void pop_cs(processor& cpu) {
  cpu.regs()[sreg::cs] = cpu.pop_word();
  // A segment load: hold interrupt recognition off for one instruction,
  // the same window STI opens and for the same reason (interrupts.h).
  cpu.inhibit_interrupts();
}

void wait(processor& /*cpu*/) {
  // WAIT polls the TEST pin until the 8087 pulls it low. This machine has
  // no coprocessor and no TEST pin, so there is nothing to ever be
  // waiting for: the instruction is a NOP that happens to have a mnemonic
  // from the part's coprocessor-interface days.
}

void hlt(processor& cpu) { cpu.halt(); }

}  // namespace amberfolio::cpu
