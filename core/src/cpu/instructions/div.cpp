// SPDX-License-Identifier: AGPL-3.0-only
//
// DIV and IDIV (issue #26): F6.6, F6.7, F7.6, F7.7.
//
// The Intel manual documents CF, OF, SF, ZF, AF and PF as *undefined*
// after DIV/IDIV. On real silicon they are not undefined at all — they are
// whatever the division microcode's internal shift-and-subtract loop last
// left in the flag latch, because the loop is built out of ordinary SUB
// steps that set flags exactly like the SUB instruction does, and nothing
// resets them before the instruction ends (successful IDIV is the one
// exception: see CCOF below). That loop is architectural fact, not
// implementation freedom, and the vectors are what proves it: a handler
// that leaves FLAGS alone, or that composes them from the final quotient
// the way this file's first draft did, fails the large majority of both
// files. The only way to land on the bits real 8088 silicon produces is to
// run the same shift-and-subtract loop the microcode runs and keep
// whatever it last set.
//
// The loop below is a direct translation of the 8086/8088 divide
// microcode's CORD, PREIDIV/CORNEGATE and POSTIDIV routines (the names are
// Intel's; the reference used to translate them was the microcode
// disassembly cross-checked against a working, vector-validated 8088
// emulator (MartyPC, github.com/dbalsom/martypc, MIT — its
// crates/marty_core/src/cpu_808x/muldiv.rs was the source read while
// writing this, not copied from — the routines are the manufacturer's
// facts about the silicon, expressed here as plain C++ over this file's
// own `rcl1`/`cord`/`cor_negate` rather than any borrowed code):
//
//   - CORD repeatedly rotates the dividend/quotient pair left through
//     carry and conditionally subtracts the divisor, exactly like a
//     textbook non-restoring divider. Two things about it are not
//     obvious from the textbook version and matter for bit-exactness:
//
//       * Flags are set by the *trial* subtraction on every iteration
//         that takes the "maybe" branch (divisor might not fit), never on
//         the iteration that is forced to subtract because the shift
//         already produced a carry out of the top bit. So the flags
//         surviving to the end of the instruction are whatever the last
//         *trial* subtraction computed, which is not necessarily the
//         final iteration.
//       * The quotient the loop builds up is the one's complement of the
//         true quotient — each loop iteration folds in the complement of
//         the digit it just decided, not the digit itself. Every caller
//         has to invert it back.
//
//   - The very first trial subtraction, before any shifting, is also the
//     whole divide-error test: if the dividend's high half is already >=
//     the divisor with no shift applied at all, the quotient cannot
//     possibly fit and the loop aborts immediately (interrupt_vector::
//     divide_error). This is why a zero divisor always faults (any value
//     minus zero never borrows) and why the flags left behind by a fault
//     are simply whatever that one subtraction produced.
//
//   - IDIV runs the same CORD over the operands' *absolute values*
//     (PREIDIV/`cor_negate` converts them, tracking the sign of the true
//     quotient in a bit this file calls `negate`) and then, only once
//     CORD has completed without faulting, POSTIDIV applies that sign,
//     restores the remainder's sign to match the dividend's, and — this
//     is the one place the flags are touched again — unconditionally
//     clears CF and OF. POSTIDIV has a *second* fault test of its own
//     (CORD's final carry-out), which is the narrower signed-range check:
//     it is what makes a quotient of exactly -128 (or -32768) legal while
//     -129 (or -32769) is not, and a fault taken here, like one taken
//     inside CORD, never reaches the CF/OF clear — so an IDIV fault's
//     flags are never the all-zero CF/OF pattern a successful IDIV always
//     leaves.
//
// Net rule, and the one worth remembering for MUL/IMUL's own undefined
// flags (issue #25 — same silicon, same kind of problem): DIV never
// touches FLAGS beyond what the divide loop naturally leaves; IDIV is
// identical except that a *successful* one additionally clears CF and OF
// as its very last step. Nothing here masks a bit or guesses one — every
// bit is the loop's actual arithmetic, checked bit-for-bit against both
// files' full ten thousand vectors.
//
// On a fault, AL/AH (or AX/DX) keep the vectors' `before` values — this
// file never writes them until it knows the division did not fault. IP is
// not adjusted: `processor::step` has already advanced it past the whole
// instruction by the time a handler runs, and `deliver_interrupt` pushes
// IP exactly as it stands, which is the 8086's "return past the faulting
// instruction" behaviour (interrupts.h) for free.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// Rotate `v` left by one bit through `carry_in`, `w`-wide: the RCL the
/// microcode's shift/subtract loop is built from. The carry out is the
/// bit that was in the top position before the rotate.
struct rotate_result {
  std::uint16_t value;
  bool carry_out;
};

[[nodiscard]] rotate_result rcl1(width w, std::uint16_t v,
                                 bool carry_in) noexcept {
  v = truncate(w, v);
  const bool msb_before = (v & sign_bit(w)) != 0;
  const std::uint16_t shifted =
      truncate(w, static_cast<std::uint16_t>((v << 1u) | (carry_in ? 1u : 0u)));
  return {.value = shifted, .carry_out = msb_before};
}

/// What CORD leaves behind: the one's-complement quotient and the
/// remainder, the carry POSTIDIV's own fault test consumes, the flag word
/// as of the last flag-setting step inside the loop, and whether the loop
/// aborted (a fault) before completing.
struct cord_result {
  std::uint16_t quotient_complement;
  std::uint16_t remainder;
  bool final_carry;
  std::uint16_t flags;
  bool faulted;
};

/// The 8086 microcode's CORD: divide the `high`:`low` pair by `divisor`,
/// `w` bits at a time, `w`-wide. `high`/`low` are the true dividend for
/// DIV and the absolute value of it for IDIV — CORD itself has no notion
/// of sign, which is exactly why IDIV needs PREIDIV/POSTIDIV around it.
[[nodiscard]] cord_result cord(width w, std::uint16_t high,
                               std::uint16_t divisor, std::uint16_t low,
                               std::uint16_t flags_in) noexcept {
  std::uint16_t tmp_a = truncate(w, high);
  std::uint16_t tmp_c = truncate(w, low);
  const std::uint16_t tmp_b = truncate(w, divisor);
  std::uint16_t flags = flags_in;

  // The trial subtraction before any shift: if the dividend's high half
  // is already at or above the divisor, the quotient overflows the
  // destination before the loop does a single bit of work.
  flags = alu::sub(w, tmp_a, tmp_b, flags).flags;
  bool carry = (flags & flag::cf) != 0;
  if (!carry) {
    return {.quotient_complement = 0,
            .remainder = 0,
            .final_carry = false,
            .flags = flags,
            .faulted = true};
  }

  const unsigned bits = w == width::byte ? 8u : 16u;
  for (unsigned i = 0; i < bits; ++i) {
    const rotate_result shifted_c = rcl1(w, tmp_c, carry);
    tmp_c = shifted_c.value;
    carry = shifted_c.carry_out;

    const rotate_result shifted_a = rcl1(w, tmp_a, carry);
    tmp_a = shifted_a.value;
    carry = shifted_a.carry_out;

    if (carry) {
      // The shift alone already produced a carry out of the top bit, so
      // the subtraction is forced — this iteration's flags are not
      // updated, which is the source of a divide's flags not always
      // reflecting its final iteration.
      carry = false;
      tmp_a = truncate(w, static_cast<std::uint16_t>(tmp_a - tmp_b));
    } else {
      // The undecided case: try the subtraction, keep it only if it
      // does not borrow, and this is a real flag update either way.
      const alu::result trial = alu::sub(w, tmp_a, tmp_b, flags);
      flags = trial.flags;
      carry = (flags & flag::cf) != 0;
      if (!carry) {
        tmp_a = trial.value;
      }
    }
  }

  // Fold the last quotient bit in, then rotate once more without storing
  // it, purely to read out the carry POSTIDIV's own fault test uses. This
  // final rotate's carry out is also, unconditionally, the CF the
  // instruction ends with on a non-faulting completion — the microcode
  // sets it explicitly here, so it is not necessarily the CF the last
  // trial subtraction above left, and a handler that assumes it is fails
  // the large majority of both files' vectors on CF alone.
  const rotate_result folded = rcl1(w, tmp_c, carry);
  tmp_c = folded.value;
  const rotate_result peek = rcl1(w, tmp_c, folded.carry_out);
  flags = flag::with(flags, flag::cf, peek.carry_out);

  return {.quotient_complement = tmp_c,
          .remainder = tmp_a,
          .final_carry = peek.carry_out,
          .flags = flags,
          .faulted = false};
}

/// What PREIDIV/`cor_negate` produce: the operands converted to their
/// absolute values (as a `high`:`low` pair and a divisor), and `negate` —
/// whether an odd number of the two operands were negative, which is
/// whether the true quotient must come out negative.
struct absolute_operands {
  std::uint16_t high;
  std::uint16_t divisor;
  std::uint16_t low;
  bool negate;
};

/// PREIDIV and CORNEGATE together: convert `high`:`low` and `divisor` to
/// their absolute values ahead of an unsigned CORD, the way IDIV's
/// microcode does it — negating the (`high`:`low`) pair as one two's
/// complement double-width value when the dividend is negative, and the
/// divisor on its own when it is, with `negate` flipping once per operand
/// that was negative so it ends up tracking their combined sign.
[[nodiscard]] absolute_operands to_absolute(width w, std::uint16_t high,
                                            std::uint16_t divisor,
                                            std::uint16_t low) noexcept {
  bool negate = false;

  if ((high & sign_bit(w)) != 0) {
    // Two's complement of the double-width (high:low) pair: negate the
    // low half, and only the low half's own carry out (i.e. it was
    // nonzero) decides whether the high half gets a two's complement
    // negate of its own or just a one's complement — the standard
    // multi-word negation identity, not a shortcut special to this file.
    const alu::result neg_low = alu::neg(w, low, 0);
    low = neg_low.value;
    const bool low_was_nonzero = (neg_low.flags & flag::cf) != 0;
    high = low_was_nonzero ? truncate(w, static_cast<std::uint16_t>(~high))
                           : alu::neg(w, high, 0).value;
    negate = !negate;
  }

  if ((divisor & sign_bit(w)) != 0) {
    divisor = alu::neg(w, divisor, 0).value;
    negate = !negate;
  }

  return {.high = high, .divisor = divisor, .low = low, .negate = negate};
}

/// AL/AH := AX / r/m8, or AX/DX := DX:AX / r/m16 (unsigned). `deliver`
/// carries the fault straight to `cpu.deliver_interrupt`, matching the
/// vectors' before-state on the destination registers exactly because
/// this never writes them on that path.
void div_unsigned(processor& cpu, width w) {
  const std::uint16_t divisor = cpu.read_rm(w);
  const std::uint16_t high = w == width::byte
                                 ? std::uint16_t{cpu.regs().get(reg8::ah)}
                                 : cpu.regs()[reg16::dx];
  const std::uint16_t low = w == width::byte
                                ? std::uint16_t{cpu.regs().get(reg8::al)}
                                : cpu.regs()[reg16::ax];

  const cord_result r = cord(w, high, divisor, low, cpu.regs().flags);
  // DIV has no CCOF step: whatever the loop last set is what stays,
  // faulted or not.
  cpu.regs().flags = r.flags;

  if (r.faulted) {
    cpu.deliver_interrupt(interrupt_vector::divide_error);
    return;
  }

  const std::uint16_t quotient =
      truncate(w, static_cast<std::uint16_t>(~r.quotient_complement));
  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(quotient));
    cpu.regs().set(reg8::ah, static_cast<std::uint8_t>(r.remainder));
  } else {
    cpu.regs()[reg16::ax] = quotient;
    cpu.regs()[reg16::dx] = r.remainder;
  }
}

/// AL/AH := AX / r/m8, or AX/DX := DX:AX / r/m16 (signed, truncated
/// toward zero). Runs CORD over the operands' absolute values and then
/// applies POSTIDIV: the sign of the quotient and of the remainder, the
/// narrower signed-range fault test, and — only on success — CCOF.
void div_signed(processor& cpu, width w) {
  const auto divisor_raw = cpu.read_rm(w);
  const std::uint16_t high = w == width::byte
                                 ? std::uint16_t{cpu.regs().get(reg8::ah)}
                                 : cpu.regs()[reg16::dx];
  const std::uint16_t low = w == width::byte
                                ? std::uint16_t{cpu.regs().get(reg8::al)}
                                : cpu.regs()[reg16::ax];
  const bool dividend_negative = (high & sign_bit(w)) != 0;

  const absolute_operands abs = to_absolute(w, high, divisor_raw, low);
  const cord_result r =
      cord(w, abs.high, abs.divisor, abs.low, cpu.regs().flags);

  if (r.faulted) {
    // CORD's own fault (the quotient does not fit even the full
    // unsigned width): POSTIDIV, and its CCOF, are never reached.
    cpu.regs().flags = r.flags;
    cpu.deliver_interrupt(interrupt_vector::divide_error);
    return;
  }

  if (!r.final_carry) {
    // POSTIDIV's own fault: the magnitude fits eight (or sixteen) bits
    // unsigned but not the narrower signed range — the check that
    // decides -128/-32768 are legal and -129/-32769 are not. Still no
    // CCOF: the flags are exactly what CORD's loop left.
    cpu.regs().flags = r.flags;
    cpu.deliver_interrupt(interrupt_vector::divide_error);
    return;
  }

  std::uint16_t remainder = r.remainder;
  if (dividend_negative) {
    remainder = alu::neg(w, remainder, 0).value;
  }

  // The quotient is the one's complement CORD produced; POSTIDIV turns
  // that into the true magnitude (negate == false) or straight into its
  // two's complement negative (negate == true) — `complement + 1` is
  // `-(~complement)`, i.e. the negative of that same magnitude, so this
  // is one expression either way rather than negating a magnitude twice.
  std::uint16_t quotient =
      abs.negate
          ? truncate(w, static_cast<std::uint16_t>(r.quotient_complement + 1))
          : truncate(w, static_cast<std::uint16_t>(~r.quotient_complement));

  // A REP or REPNE prefix ahead of IDIV — meaningless on paper, since
  // IDIV is not a string instruction — negates the quotient on real
  // silicon. This is not a documented instruction effect; it is a fact
  // about the 8086/8088 microcode reusing the same F1 "negate" latch STOS
  // and friends use for their own purposes, and the vectors exercise it
  // because they prepend prefixes at random. The remainder is unaffected.
  if (cpu.current().prefixes.rep != repeat::none) {
    quotient = alu::neg(w, quotient, 0).value;
  }

  std::uint16_t flags = r.flags;
  flags = flag::with(flags, flag::cf, false);
  flags = flag::with(flags, flag::of, false);
  cpu.regs().flags = flags;

  if (w == width::byte) {
    cpu.regs().set(reg8::al, static_cast<std::uint8_t>(quotient));
    cpu.regs().set(reg8::ah, static_cast<std::uint8_t>(remainder));
  } else {
    cpu.regs()[reg16::ax] = quotient;
    cpu.regs()[reg16::dx] = remainder;
  }
}

}  // namespace

void div_rm8(processor& cpu) { div_unsigned(cpu, width::byte); }
void div_rm16(processor& cpu) { div_unsigned(cpu, width::word); }
void idiv_rm8(processor& cpu) { div_signed(cpu, width::byte); }
void idiv_rm16(processor& cpu) { div_signed(cpu, width::word); }

}  // namespace amberfolio::cpu
