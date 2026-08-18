// SPDX-License-Identifier: AGPL-3.0-only
//
// CALL, JMP and RET (issue #29): every architectural transfer of control
// that is not a conditional branch or a software interrupt. None of them
// touch a flag — a vector that shows one change means something else in
// the handler clobbered it, not that one of these instructions does.
//
// The four undocumented aliases are a fact about the silicon, not a
// decision made here: C0, C1, C8 and C9 are what ENTER and LEAVE would
// occupy on a 186, but this is an 8086, which decodes only the low three
// bits of the C0-C3 and C8-CB block and so treats C0 exactly as C2, C1 as
// C3, C8 as CA and C9 as CB. The v2 vectors for C0/C1/C8/C9 confirm it —
// same behaviour, same flags (none) — so the dispatch table just points
// both encodings at one handler apiece rather than this file restating
// anything.

#include <cstdint>

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

/// A 32-bit far pointer: offset first, segment second — the order
/// ptr16:16 is encoded in (9A, EA) and the order the far indirect forms
/// (FF /3, FF /5) store it in memory.
struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;
};

/// 9A and EA's immediate operand: two words read straight out of the
/// instruction stream, offset then segment.
[[nodiscard]] far_pointer fetch_far_pointer(processor& cpu) {
  const std::uint16_t offset = cpu.fetch_word();
  const std::uint16_t segment = cpu.fetch_word();
  return {.offset = offset, .segment = segment};
}

/// FF /3 and FF /5's pointer, read through the normal effective-address
/// path: offset at `ea`, segment at `ea + 2`. The second read wraps in 16
/// bits within the segment like any other word access and never carries
/// into the segment number — the same rule `read_word` already applies to
/// each individual word.
///
/// `ea` is only valid when the ModRM byte names memory. A register (mod
/// 3) operand is an undefined form for both encodings; the v2 vector
/// files contain no such test for either stem, so it is out of scope
/// (issue #29) and this function is never asked to make sense of one.
[[nodiscard]] far_pointer read_far_pointer_indirect(processor& cpu) {
  const address& ea = cpu.current().ea;
  const std::uint16_t offset = cpu.read_word(ea.segment, ea.offset);
  const std::uint16_t segment =
      cpu.read_word(ea.segment, static_cast<std::uint16_t>(ea.offset + 2u));
  return {.offset = offset, .segment = segment};
}

/// A relative jump's target: IP *after* the whole instruction —
/// `cpu.regs().ip` once the displacement has already been fetched — plus
/// the sign-extended displacement, wrapped in 16 bits. It never carries
/// into CS.
[[nodiscard]] std::uint16_t relative_target(processor& cpu,
                                            std::int16_t displacement) {
  const std::uint16_t base = cpu.regs().ip;
  return static_cast<std::uint16_t>(base +
                                    static_cast<std::uint16_t>(displacement));
}

void far_jump(processor& cpu, far_pointer target) {
  cpu.regs()[sreg::cs] = target.segment;
  cpu.regs().ip = target.offset;
}

/// A near call pushes IP as the handler leaves it — after whatever
/// displacement or pointer fetch found `target` — and then jumps.
/// `target` is always a local computed before this runs, so the value
/// pushed is the return address, never the destination.
void near_call(processor& cpu, std::uint16_t target) {
  cpu.push_word(cpu.regs().ip);
  cpu.regs().ip = target;
}

/// A far call pushes CS, then IP, in that order, so that a far RET's
/// IP-then-CS pop takes them back off in the order they went on.
void far_call(processor& cpu, far_pointer target) {
  cpu.push_word(cpu.regs()[sreg::cs]);
  cpu.push_word(cpu.regs().ip);
  far_jump(cpu, target);
}

/// C2/C0 (near) and CA/C8 (far) add an imm16 to SP after popping the
/// return address off it. The pop must read the *old* SP — adding first
/// would pop from the wrong place — so this is always called last.
void adjust_sp(processor& cpu, std::uint16_t imm16) {
  cpu.regs()[reg16::sp] =
      static_cast<std::uint16_t>(cpu.regs()[reg16::sp] + imm16);
}

}  // namespace

// --- CALL --------------------------------------------------------------

void call_rel16(processor& cpu) {
  const auto displacement = static_cast<std::int16_t>(cpu.fetch_word());
  near_call(cpu, relative_target(cpu, displacement));
}

void call_ptr16_16(processor& cpu) { far_call(cpu, fetch_far_pointer(cpu)); }

void call_rm16(processor& cpu) { near_call(cpu, cpu.read_rm(width::word)); }

void call_m16_16(processor& cpu) {
  far_call(cpu, read_far_pointer_indirect(cpu));
}

// --- JMP -----------------------------------------------------------------

void jmp_rel16(processor& cpu) {
  const auto displacement = static_cast<std::int16_t>(cpu.fetch_word());
  cpu.regs().ip = relative_target(cpu, displacement);
}

void jmp_rel8(processor& cpu) {
  // Sign-extended, then widened again, so relative_target sees the same
  // signed 16-bit displacement E9 hands it.
  const auto displacement =
      static_cast<std::int16_t>(sign_extend(cpu.fetch_byte()));
  cpu.regs().ip = relative_target(cpu, displacement);
}

void jmp_ptr16_16(processor& cpu) { far_jump(cpu, fetch_far_pointer(cpu)); }

void jmp_rm16(processor& cpu) { cpu.regs().ip = cpu.read_rm(width::word); }

void jmp_m16_16(processor& cpu) {
  far_jump(cpu, read_far_pointer_indirect(cpu));
}

// --- RET -------------------------------------------------------------

void ret_near(processor& cpu) { cpu.regs().ip = cpu.pop_word(); }

void ret_near_imm16(processor& cpu) {
  // The imm16 sits directly after the opcode, so it is fetched from the
  // instruction stream before any of the pop-and-adjust below happens.
  const std::uint16_t imm16 = cpu.fetch_word();
  cpu.regs().ip = cpu.pop_word();
  adjust_sp(cpu, imm16);
}

void ret_far(processor& cpu) {
  const std::uint16_t new_ip = cpu.pop_word();
  const std::uint16_t new_cs = cpu.pop_word();
  far_jump(cpu, {.offset = new_ip, .segment = new_cs});
}

void ret_far_imm16(processor& cpu) {
  const std::uint16_t imm16 = cpu.fetch_word();
  const std::uint16_t new_ip = cpu.pop_word();
  const std::uint16_t new_cs = cpu.pop_word();
  far_jump(cpu, {.offset = new_ip, .segment = new_cs});
  adjust_sp(cpu, imm16);
}

}  // namespace amberfolio::cpu
