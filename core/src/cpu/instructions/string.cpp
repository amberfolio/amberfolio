// SPDX-License-Identifier: AGPL-3.0-only
//
// MOVS, CMPS, STOS, LODS, SCAS (issue #30), and what REP/REPE/REPNE mean
// over them.
//
// None of these five opcodes carries a ModRM byte, so unlike the rest of
// M1's wide phase this handler forms its own operand addresses instead of
// reading `processor::current().ea`. The source side is DS:SI, overridable
// by a segment-override prefix the way any memory operand is; the
// destination side is always ES:DI, on this part, no matter what prefix
// came before the opcode. `read_rm`/`write_rm` have nothing to do with any
// of that, so this file does not call them.
//
// The other thing that makes this family different from the rest of the
// wide phase is the step model (PLAN.md §3, interrupts.h): a repeated run
// is many `step()` calls, one per iteration, so that an interrupt can land
// between them. A handler here therefore never loops. It runs exactly one
// iteration, and if the run has not retired it rewinds IP to
// `instruction::start_ip` — not to the last prefix byte — and calls
// `keep_repeating()`. interrupts.h explains why start_ip is right for an
// *uninterrupted* run (the real part never re-fetches between iterations,
// so every prefix, including a segment override, stays in force) and why
// it is delivery, not this handler, that has to know about the 8086's
// last-prefix quirk when a REP actually gets interrupted.
//
// REP/REPE (F3) and REPNE (F2) are already decoded into
// `current().prefixes.rep`; what follows is what they mean for each of
// the five instructions:
//
//   - CX == 0 on entry to a repeated run executes zero iterations and
//     makes no memory access at all — checked before anything else, per
//     opcode, every time.
//   - Otherwise: one iteration, then CX decrements, then the run decides
//     whether to continue. CX reaching zero always stops it.
//   - For CMPS and SCAS, REPE additionally stops the run the moment ZF
//     comes back clear and REPNE stops it the moment ZF comes back set —
//     tested after the iteration's own flags are written, which is what
//     lets "REPE CMPSB" mean "while equal" and "REPNE SCASB" mean "while
//     not found".
//   - For MOVS/LODS/STOS, REPNE is not a different instruction: the
//     silicon does not consult ZF for them, so it behaves exactly like
//     REP. The vectors confirm it, and it is worth stating because Intel's
//     own notation (REPNE is documented against CMPS/SCAS only) makes it
//     easy to assume STOS ignores the prefix instead of just its ZF half.

#include <cstdint>

#include "amberfolio/cpu/alu.h"
#include "amberfolio/cpu/decoder.h"
#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

/// +1/+2 for DF clear, -1/-2 for DF set — the amount SI and/or DI move by
/// one iteration, at this operand's width. The cast-back-through-int is
/// the standard two's-complement route to "subtract, wrapping in 16
/// bits" without writing the wraparound out by hand at every call site.
[[nodiscard]] std::uint16_t string_step(width w,
                                        const registers& regs) noexcept {
  const std::uint16_t magnitude = w == width::byte ? 1 : 2;
  return regs.flag_set(flag::df) ? static_cast<std::uint16_t>(-magnitude)
                                 : magnitude;
}

/// DS:`offset`, or the override segment if a segment-override prefix was
/// seen. The only one of a string instruction's two addresses that a
/// prefix can move.
[[nodiscard]] address source_address(processor& cpu,
                                     std::uint16_t offset) noexcept {
  const prefix_state& prefixes = cpu.current().prefixes;
  const sreg seg =
      prefixes.has_segment_override ? prefixes.segment_override : sreg::ds;
  return {.segment = cpu.regs()[seg], .offset = offset};
}

/// ES:`offset`. Always ES, prefix or no prefix — the decoder records a
/// segment override on these opcodes like any other, but the destination
/// side of a string instruction is not one of the things it can move.
[[nodiscard]] address dest_address(processor& cpu,
                                   std::uint16_t offset) noexcept {
  return {.segment = cpu.regs()[sreg::es], .offset = offset};
}

[[nodiscard]] bool is_repeated(processor& cpu) noexcept {
  return cpu.current().prefixes.rep != repeat::none;
}

/// The REP/REPE/REPNE special case: CX already zero when the run begins
/// touches nothing at all and retires on the spot.
[[nodiscard]] bool repeat_exhausted(processor& cpu) noexcept {
  return is_repeated(cpu) && cpu.regs()[reg16::cx] == 0;
}

/// Whether a repeated run continues after the iteration that just ran.
/// `check_zf` is false for MOVS/LODS/STOS (REPNE behaves like REP for
/// them) and true for CMPS/SCAS, where REPE stops on ZF clear and REPNE
/// stops on ZF set. Assumes CX has already been decremented.
[[nodiscard]] bool should_continue(processor& cpu, bool check_zf) noexcept {
  if (cpu.regs()[reg16::cx] == 0) {
    return false;
  }
  if (!check_zf) {
    return true;
  }
  const bool zf = cpu.regs().flag_set(flag::zf);
  return cpu.current().prefixes.rep == repeat::repe ? zf : !zf;
}

/// The common tail of every repeated iteration: decrement CX, decide
/// whether to continue, and if so rewind to the top of the instruction
/// and tell the step loop this run has not retired. A non-repeated
/// instruction (`is_repeated` false) does none of this — one iteration
/// and it is done, which `step()` already reports by falling through to
/// `ran`.
void finish_iteration(processor& cpu, bool check_zf) {
  if (!is_repeated(cpu)) {
    return;
  }
  registers& regs = cpu.regs();
  regs[reg16::cx] = static_cast<std::uint16_t>(regs[reg16::cx] - 1);
  if (should_continue(cpu, check_zf)) {
    regs.ip = cpu.current().start_ip;
    cpu.keep_repeating();
  }
}

// --- MOVS: [ES:DI] := [DS:SI], both pointers advance. ------------------

void movs(processor& cpu, width w) {
  if (repeat_exhausted(cpu)) {
    return;
  }
  registers& regs = cpu.regs();

  // Read before write: with one of each there is no ordering hazard
  // between them, but reading the operand into a local before touching
  // the bus a second time is the house rule regardless.
  const std::uint16_t value = cpu.read(w, source_address(cpu, regs[reg16::si]));
  cpu.write(w, dest_address(cpu, regs[reg16::di]), value);

  const std::uint16_t step = string_step(w, regs);
  regs[reg16::si] = static_cast<std::uint16_t>(regs[reg16::si] + step);
  regs[reg16::di] = static_cast<std::uint16_t>(regs[reg16::di] + step);

  finish_iteration(cpu, /*check_zf=*/false);
}

// --- CMPS: flags of [DS:SI] - [ES:DI]; both pointers advance. ----------

void cmps(processor& cpu, width w) {
  if (repeat_exhausted(cpu)) {
    return;
  }
  registers& regs = cpu.regs();

  // Both operands into locals before the kernel call: CMPS writes
  // nothing back, but the harness still checks the order the two reads
  // happened in, so the source (SI) side is read first, matching the
  // order the mnemonic and the "source minus destination" flag rule both
  // suggest.
  const std::uint16_t src = cpu.read(w, source_address(cpu, regs[reg16::si]));
  const std::uint16_t dst = cpu.read(w, dest_address(cpu, regs[reg16::di]));

  regs.flags = alu::cmp(w, src, dst, regs.flags);

  const std::uint16_t step = string_step(w, regs);
  regs[reg16::si] = static_cast<std::uint16_t>(regs[reg16::si] + step);
  regs[reg16::di] = static_cast<std::uint16_t>(regs[reg16::di] + step);

  finish_iteration(cpu, /*check_zf=*/true);
}

// --- STOS: [ES:DI] := AL/AX; DI advances. -------------------------------

void stos(processor& cpu, width w) {
  if (repeat_exhausted(cpu)) {
    return;
  }
  registers& regs = cpu.regs();

  const std::uint16_t value =
      w == width::byte ? std::uint16_t{regs.get(reg8::al)} : regs[reg16::ax];
  cpu.write(w, dest_address(cpu, regs[reg16::di]), value);

  regs[reg16::di] =
      static_cast<std::uint16_t>(regs[reg16::di] + string_step(w, regs));

  finish_iteration(cpu, /*check_zf=*/false);
}

// --- LODS: AL/AX := [DS:SI]; SI advances. -------------------------------

void lods(processor& cpu, width w) {
  if (repeat_exhausted(cpu)) {
    return;
  }
  registers& regs = cpu.regs();

  const std::uint16_t value = cpu.read(w, source_address(cpu, regs[reg16::si]));
  if (w == width::byte) {
    regs.set(reg8::al, static_cast<std::uint8_t>(value));
  } else {
    regs[reg16::ax] = value;
  }

  regs[reg16::si] =
      static_cast<std::uint16_t>(regs[reg16::si] + string_step(w, regs));

  finish_iteration(cpu, /*check_zf=*/false);
}

// --- SCAS: flags of AL/AX - [ES:DI]; DI advances. -----------------------

void scas(processor& cpu, width w) {
  if (repeat_exhausted(cpu)) {
    return;
  }
  registers& regs = cpu.regs();

  const std::uint16_t acc =
      w == width::byte ? std::uint16_t{regs.get(reg8::al)} : regs[reg16::ax];
  const std::uint16_t mem = cpu.read(w, dest_address(cpu, regs[reg16::di]));

  regs.flags = alu::cmp(w, acc, mem, regs.flags);

  regs[reg16::di] =
      static_cast<std::uint16_t>(regs[reg16::di] + string_step(w, regs));

  finish_iteration(cpu, /*check_zf=*/true);
}

}  // namespace

void movsb(processor& cpu) { movs(cpu, width::byte); }
void movsw(processor& cpu) { movs(cpu, width::word); }
void cmpsb(processor& cpu) { cmps(cpu, width::byte); }
void cmpsw(processor& cpu) { cmps(cpu, width::word); }
void stosb(processor& cpu) { stos(cpu, width::byte); }
void stosw(processor& cpu) { stos(cpu, width::word); }
void lodsb(processor& cpu) { lods(cpu, width::byte); }
void lodsw(processor& cpu) { lods(cpu, width::word); }
void scasb(processor& cpu) { scas(cpu, width::byte); }
void scasw(processor& cpu) { scas(cpu, width::word); }

}  // namespace amberfolio::cpu
