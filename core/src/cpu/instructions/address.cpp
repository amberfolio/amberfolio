// SPDX-License-Identifier: AGPL-3.0-only
//
// The address and convert family (issue #32): LEA, LES, LDS, XLAT, CBW,
// CWD, SAHF, LAHF. Eight opcodes with almost nothing in common except
// that none of them is an ALU operation — the ALU kernel (alu.h) has no
// part in this file.
//
// LEA is the odd one out among the "load a pointer" opcodes: it computes
// an effective address and never touches the bus for it. LES and LDS do
// the opposite of LEA's restraint — they read a 32-bit far pointer out of
// memory and load two registers from it, one of them a segment register,
// which is why they join MOV Sreg,r/m and POP Sreg in holding interrupt
// recognition off for one instruction (interrupts.h explains why: the
// window exists for SS, but this part does not check which register it
// was).
//
// CBW/CWD are sign extensions and touch no flags at all. SAHF/LAHF move
// the low byte of FLAGS to and from AH; SAHF is the one with a genuine
// subtlety, covered on the handler itself.

#include <cstdint>

#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// The low byte of FLAGS: CF, PF, AF, ZF, SF, and the three hardwired
/// bits SAHF/LAHF carry along for the ride (bit 1 fixed at 1, bits 3 and
/// 5 fixed at 0 — registers.h's flag::normalize is the single place that
/// fact lives).
inline constexpr std::uint16_t low_flag_byte_mask = 0x00FFu;

}  // namespace

// --- LEA: the effective address, and nothing else --------------------

void lea_r16_m(processor& cpu) {
  // The decoder has already computed the effective address into
  // current().ea — that offset *is* LEA's answer. No read_rm, no read of
  // any kind: LEA is the one "load a memory operand" instruction on this
  // part that never asks the bus for anything, which is also why it
  // ignores a segment override that only ever decided which segment an
  // *access* would use. There is no access.
  cpu.write_reg(width::word, cpu.current().ea.offset);
}

// --- LES/LDS: a 4-byte far pointer, offset then segment ---------------

namespace {

/// Load `reg` (word) and `dest_segment` from the 4-byte far pointer at
/// the decoded effective address: the offset first, then the segment at
/// ea+2. The +2 is offset arithmetic and wraps in 16 bits *inside* the
/// pointer's own segment, exactly like any other word access here — it
/// must never carry into the segment number, so the two reads are formed
/// from the same `ea.segment` with only the low half of the address
/// advanced.
void load_far_pointer(processor& cpu, sreg dest_segment) {
  const address& ea = cpu.current().ea;
  const std::uint16_t offset_value = cpu.read(width::word, ea);
  const address segment_field{
      .segment = ea.segment,
      .offset = static_cast<std::uint16_t>(ea.offset + 2)};
  const std::uint16_t segment_value = cpu.read(width::word, segment_field);

  cpu.write_reg(width::word, offset_value);
  cpu.regs()[dest_segment] = segment_value;

  // A segment-register load: hold interrupt recognition off for one
  // instruction, the same window STI and MOV/POP Sreg use, and for the
  // same reason (interrupts.h) — an interrupt must never land between a
  // pointer's two halves loading.
  cpu.inhibit_interrupts();
}

}  // namespace

void les_r16_m32(processor& cpu) { load_far_pointer(cpu, sreg::es); }
void lds_r16_m32(processor& cpu) { load_far_pointer(cpu, sreg::ds); }

// --- XLAT: AL := [DS:BX+AL], segment-overridable ----------------------

void xlat(processor& cpu) {
  // XLAT has no ModRM byte, so nothing above this handler has formed its
  // address. Unlike LEA, XLAT does access memory, so — unlike LEA — the
  // override the decoder recorded is this handler's to apply: DS unless
  // a segment prefix said otherwise.
  const prefix_state& prefixes = cpu.current().prefixes;
  const sreg segment =
      prefixes.has_segment_override ? prefixes.segment_override : sreg::ds;

  const std::uint16_t bx = cpu.regs()[reg16::bx];
  const std::uint8_t al = cpu.regs().get(reg8::al);
  // AL is zero-extended before the add, and the sum wraps in 16 bits
  // within the segment — the same rule every other address on this part
  // follows.
  const auto offset =
      static_cast<std::uint16_t>(bx + static_cast<std::uint16_t>(al));

  const std::uint8_t value = cpu.read_byte(cpu.regs()[segment], offset);
  cpu.regs().set(reg8::al, value);
}

// --- CBW/CWD: sign extension, no flags touched -------------------------

void cbw(processor& cpu) {
  const auto al = static_cast<std::int8_t>(cpu.regs().get(reg8::al));
  cpu.regs()[reg16::ax] =
      static_cast<std::uint16_t>(static_cast<std::int16_t>(al));
}

void cwd(processor& cpu) {
  const auto ax = static_cast<std::int16_t>(cpu.regs()[reg16::ax]);
  cpu.regs()[reg16::dx] = (ax < 0) ? std::uint16_t{0xFFFF} : std::uint16_t{0};
}

// --- SAHF/LAHF: AH and the low byte of FLAGS ---------------------------

void sahf(processor& cpu) {
  // SAHF is a merge, not a load: only SF, ZF, AF, PF and CF move from AH
  // into FLAGS. The high byte — TF, IF, DF, OF — has nothing to do with
  // AH and must survive untouched, which is why this composes the new
  // word from the *current* flags' high byte and AH's low byte rather
  // than routing AH alone through load_flags. Bits 1, 3 and 5 of AH ride
  // along into the merge whatever they are; flag::normalize (via
  // load_flags) is what forces them back to the part's fixed values (1,
  // 0, 0) regardless, so the merge does not have to get those three bits
  // right itself.
  const std::uint8_t ah = cpu.regs().get(reg8::ah);
  const auto merged =
      static_cast<std::uint16_t>((cpu.regs().flags & ~low_flag_byte_mask) | ah);
  cpu.regs().load_flags(merged);
}

void lahf(processor& cpu) {
  // The same low byte read back, fixed bits included: AH ends up with
  // bit 1 set and bits 3/5 clear, because FLAGS is always normalized
  // already and this just copies its low half verbatim.
  cpu.regs().set(reg8::ah, static_cast<std::uint8_t>(cpu.regs().flags &
                                                     low_flag_byte_mask));
}

}  // namespace amberfolio::cpu
