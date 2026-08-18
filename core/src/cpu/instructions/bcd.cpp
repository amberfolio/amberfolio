// SPDX-License-Identifier: AGPL-3.0-only
//
// The BCD adjust family (issue #27): DAA 27, DAS 2F, AAA 37, AAS 3F, and
// the two ASCII-adjust-with-a-real-divisor instructions AAM D4 and AAD D5.
//
// None of these take a ModRM byte — they all work AL, or AL/AH, in place —
// and AAM/AAD each take one immediate byte that the documented mnemonic
// hardcodes to 0Ah but that this silicon treats as a genuine operand: the
// vectors exercise other values, so the handler fetches it rather than
// assuming it.
//
// The Intel manual gives DAA/DAS/AAA/AAS as pseudocode that computes a
// result and then *decides* what the flags should be. That is exactly
// backwards for this exercise: OF, SF, ZF and PF are documented undefined
// for this whole family, and the conformance vectors (real 8088 silicon,
// no masks) pin them bit for bit. Reasoning about what an undefined flag
// "should" be does not reproduce silicon; running the same internal ALU
// steps the part actually runs does, because whatever those steps leave
// in FLAGS *is* the undefined bit — there is nothing else it could be.
//
// So every handler below is written as the literal sequence of ordinary
// ALU operations the adjust performs, composed from alu::add / alu::sub /
// alu::with_szp, with the documented "should the adjustment happen" tests
// evaluated as plain conditions rather than through the ALU. Where the
// documented algorithm calls for forcing a flag to a fixed value rather
// than whatever the last ALU pass computed (AF and CF throughout this
// file, and the "AL AND 0FH" step of AAA/AAS in particular, which does not
// go through the ALU's logic path at all), that forcing is done explicitly
// and is called out in each function's comment as a fact the vectors
// confirmed, not a guess.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// DAA and DAS share this shape: a low-nibble adjustment gated on AF (or
/// the low nibble already being out of decimal range), then a high-nibble
/// adjustment gated on CF (or AL already being out of decimal range) — and
/// both gates are evaluated against AL and CF *as they were when the
/// instruction started*. `combine` is `alu::add` for DAA and `alu::sub`
/// for DAS; the two instructions are otherwise identical, which is exactly
/// what the silicon's shared microcode would look like.
///
/// Two silicon facts the vectors pin, neither of them in the manual:
///
/// The high-nibble gate is not quite "AL > 0x99 || CF". There is one band
/// where it disagrees with that magnitude test: AL from 0x9A to 0x9F
/// (high nibble already 9, low nibble already out of range on its own)
/// with CF clear. There, the gate follows AF *as the instruction started*
/// instead of firing unconditionally the way a plain ">99" would — true
/// AF means it does not fire, false means it does. Nowhere else does AF
/// have anything to do with the high-nibble decision; this one band is
/// where the two digits' adjustments are close enough to fight over the
/// same carry chain.
///
/// OF is not "whatever the last ALU pass left it as", the way this file's
/// other handlers work. Both adjustments' *values* land the same whether
/// they are added one at a time or as a single combined amount (0x00,
/// 0x06, 0x60 or 0x66), because addition is associative — but OF is not:
/// "did adding 6 then 0x60 cross the sign boundary" and "did adding 0x66
/// in one step cross it" are different questions with different answers
/// on some inputs, and the vectors side with the second one. So OF (and,
/// coincidentally, SF/ZF/PF, which only look at the value and so cannot
/// tell the two computations apart) come from one `combine` call over the
/// *combined* amount and the original AL, not from chaining two calls.
/// AF and CF are not read from that call at all — they are the gate
/// booleans, forced on afterward, because a combined single pass has no
/// notion of "the carry after the first digit" for AF to mean.
template <alu::result (*combine)(width, std::uint16_t, std::uint16_t,
                                 std::uint16_t)>
void daa_das(processor& cpu, std::uint16_t low_adjust,
             std::uint16_t high_adjust) {
  const std::uint16_t old_al = cpu.regs().get(reg8::al);
  const bool old_cf = cpu.regs().flag_set(flag::cf);
  const bool old_af = cpu.regs().flag_set(flag::af);
  const bool adjust_low = (old_al & 0x0Fu) > 9 || old_af;

  const bool ambiguous_band =
      (old_al & 0xF0u) == 0x90u && (old_al & 0x0Fu) > 9u;
  const bool adjust_high =
      old_cf || (old_al > 0x99 && !(ambiguous_band && old_af));

  const auto total = static_cast<std::uint16_t>(
      (adjust_low ? low_adjust : 0) + (adjust_high ? high_adjust : 0));
  const alu::result r = combine(width::byte, old_al, total, cpu.regs().flags);

  std::uint16_t flags = flag::with(r.flags, flag::af, adjust_low);
  flags = flag::with(flags, flag::cf, adjust_high);

  cpu.regs().set(reg8::al, static_cast<std::uint8_t>(r.value));
  cpu.regs().flags = flags;
}

/// AAA and AAS share this shape too: a conditional add/subtract of 6 to
/// AL — run through the ALU unconditionally the same way as DAA/DAS,
/// zero in place of 6 when the gate is shut, for the same reason: SF/ZF/PF
/// come out of this step alone and the vectors show them reflecting it
/// even when the gate never opens. A matching +-1 to AH follows, but only
/// when the gate is open, and as a plain register step — AH's change is
/// never itself a source of flags on this instruction, unconditional or
/// not. AL is then masked to 0x0F unconditionally, and — this is the
/// surprising part the vectors pin — that mask does *not* go through the
/// ALU's logic path: it never touches CF/OF/AF/SF/ZF/PF, which is why a
/// vector can show SF set from a sum like 0x7A + 6 = 0x80 even though the
/// masked AL that ends up in the register is only 0x00. AF and CF are
/// forced to the gate, once, at the end, the same way DAA/DAS forces them
/// — `combine`'s own carry/borrow out of the add-or-subtract-of-6 does not
/// reliably match what the gate says they should be.
template <alu::result (*combine)(width, std::uint16_t, std::uint16_t,
                                 std::uint16_t)>
void aaa_aas(processor& cpu, std::uint16_t ah_step) {
  const std::uint16_t old_al = cpu.regs().get(reg8::al);
  std::uint16_t ah = cpu.regs().get(reg8::ah);
  std::uint16_t flags = cpu.regs().flags;

  const bool adjust = (old_al & 0x0Fu) > 9 || cpu.regs().flag_set(flag::af);

  const alu::result r = combine(
      width::byte, old_al, adjust ? std::uint16_t{6} : std::uint16_t{0}, flags);
  std::uint16_t al = r.value;
  flags = r.flags;

  if (adjust) {
    ah = truncate(width::byte, static_cast<std::uint16_t>(ah + ah_step));
  }

  al = static_cast<std::uint16_t>(al & 0x0Fu);
  flags = flag::with(flags, flag::af | flag::cf, adjust);

  cpu.regs().set(reg8::al, static_cast<std::uint8_t>(al));
  cpu.regs().set(reg8::ah, static_cast<std::uint8_t>(ah));
  cpu.regs().flags = flags;
}

}  // namespace

void daa(processor& cpu) { daa_das<&alu::add>(cpu, 0x06, 0x60); }

void das(processor& cpu) { daa_das<&alu::sub>(cpu, 0x06, 0x60); }

void aaa(processor& cpu) { aaa_aas<&alu::add>(cpu, 1); }

// AH -= 1, expressed as the truncating add `aaa_aas` performs: adding
// 0xFFFF and keeping only the low byte is subtracting 1 modulo 256, and it
// lets AAA and AAS share the same helper body instead of one of them
// branching on direction.
void aas(processor& cpu) { aaa_aas<&alu::sub>(cpu, 0xFFFFu); }

/// AAM D4: fetch the divisor byte first, so that a divide by zero pushes
/// the return address *after* this whole instruction — deliver_interrupt
/// takes IP as the caller leaves it, and fetch_byte is what leaves it
/// there. AH := AL / divisor, AL := AL % divisor, computed directly (no
/// ALU primitive for division exists, and none is needed: this instruction
/// invents its own flag rule rather than composing DIV's). SF/ZF/PF come
/// from the remainder that ends up in AL, via the same `with_szp` the ALU
/// kernel exports for exactly this — a family with its own flag rule still
/// gets the SZP core written once. CF, OF and AF are forced clear, which
/// the vectors confirm this instruction never sets.
///
/// A zero divisor is a fault, not a result, but the vectors show FLAGS
/// pushed by that fault are not simply left alone: SF, AF, OF and CF read
/// back clear and ZF and PF read back set — exactly `with_szp`'s answer
/// for a remainder of 0, the value AH/AL would hold if the division
/// circuit's remainder register is 0 at the moment the zero divisor is
/// caught, because nothing has decremented it yet. So the same flag step
/// runs on that hypothetical zero remainder before the fault is
/// delivered, even though AL and AH themselves are never written.
void aam(processor& cpu) {
  const std::uint16_t al = cpu.regs().get(reg8::al);
  const std::uint8_t divisor = cpu.fetch_byte();

  if (divisor == 0) {
    std::uint16_t flags = alu::with_szp(cpu.regs().flags, width::byte, 0);
    flags = flag::with(flags, flag::cf | flag::of | flag::af, false);
    cpu.regs().flags = flags;
    cpu.deliver_interrupt(interrupt_vector::divide_error);
    return;
  }

  const auto quotient = static_cast<std::uint16_t>(al / divisor);
  const auto remainder = static_cast<std::uint16_t>(al % divisor);

  std::uint16_t flags = alu::with_szp(cpu.regs().flags, width::byte, remainder);
  flags = flag::with(flags, flag::cf | flag::of | flag::af, false);

  cpu.regs().set(reg8::ah, static_cast<std::uint8_t>(quotient));
  cpu.regs().set(reg8::al, static_cast<std::uint8_t>(remainder));
  cpu.regs().flags = flags;
}

/// AAD D5: the ASCII adjust that runs *before* a divide, so there is no
/// division here at all — it folds AH*multiplier into AL and clears AH.
/// AL := AL + AH * multiplier, AH := 0, computed with alu::add so CF/AF/OF
/// come from that addition's own carry chain rather than being invented;
/// the vectors confirm this (unlike AAM, AAD does not force them). SF/ZF/PF
/// come from the same add, at the final AL value.
void aad(processor& cpu) {
  const std::uint16_t al = cpu.regs().get(reg8::al);
  const std::uint16_t ah = cpu.regs().get(reg8::ah);
  const std::uint8_t multiplier = cpu.fetch_byte();

  const auto product =
      static_cast<std::uint16_t>(ah * static_cast<std::uint16_t>(multiplier));
  const alu::result r = alu::add(width::byte, al, product, cpu.regs().flags);

  cpu.regs().set(reg8::al, static_cast<std::uint8_t>(r.value));
  cpu.regs().set(reg8::ah, 0);
  cpu.regs().flags = r.flags;
}

}  // namespace amberfolio::cpu
