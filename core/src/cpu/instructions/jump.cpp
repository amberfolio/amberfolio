// SPDX-License-Identifier: AGPL-3.0-only
//
// Conditional jumps and the LOOP family (issue #28): the sixteen Jcc
// opcodes 70-7F, their undocumented aliases 60-6F, and LOOPNE/LOOPNZ,
// LOOPE/LOOPZ, LOOP, JCXZ (E0-E3).
//
// On the 8086/8088, opcode bit 4 simply is not decoded for this block:
// 60-6F and 70-7F are the same sixteen instructions twice over. PUSHA and
// POPA, which occupy 60/61 from the 186 onward, do not exist yet — there
// is nothing here to conflict with them.
//
// None of these instructions touch a flag; they only read the ones some
// earlier instruction set. That makes the whole family read-only wiring
// over the flag word, plus one CX decrement that is deliberately *not*
// alu::dec — the LOOP family's decrement sets no flags at all, which is
// the one place a handler here could accidentally reach for the ALU
// kernel and be wrong to do it.

#include <cstdint>

#include "amberfolio/cpu/instructions.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {
namespace {

// --- The sixteen conditions, in opcode order from 70h. -------------------
//
// Derived once from the flag word here, so each opcode handler below is a
// single dispatch line: fetch the displacement, test the condition, branch
// if it holds.

[[nodiscard]] bool cond_o(const registers& r) noexcept {
  return r.flag_set(flag::of);
}
[[nodiscard]] bool cond_no(const registers& r) noexcept {
  return !r.flag_set(flag::of);
}
[[nodiscard]] bool cond_b(const registers& r) noexcept {
  return r.flag_set(flag::cf);
}
[[nodiscard]] bool cond_nb(const registers& r) noexcept {
  return !r.flag_set(flag::cf);
}
[[nodiscard]] bool cond_z(const registers& r) noexcept {
  return r.flag_set(flag::zf);
}
[[nodiscard]] bool cond_nz(const registers& r) noexcept {
  return !r.flag_set(flag::zf);
}
[[nodiscard]] bool cond_be(const registers& r) noexcept {
  return r.flag_set(flag::cf) || r.flag_set(flag::zf);
}
[[nodiscard]] bool cond_a(const registers& r) noexcept { return !cond_be(r); }
[[nodiscard]] bool cond_s(const registers& r) noexcept {
  return r.flag_set(flag::sf);
}
[[nodiscard]] bool cond_ns(const registers& r) noexcept {
  return !r.flag_set(flag::sf);
}
[[nodiscard]] bool cond_p(const registers& r) noexcept {
  return r.flag_set(flag::pf);
}
[[nodiscard]] bool cond_np(const registers& r) noexcept {
  return !r.flag_set(flag::pf);
}
[[nodiscard]] bool cond_l(const registers& r) noexcept {
  return r.flag_set(flag::sf) != r.flag_set(flag::of);
}
[[nodiscard]] bool cond_ge(const registers& r) noexcept { return !cond_l(r); }
[[nodiscard]] bool cond_le(const registers& r) noexcept {
  return r.flag_set(flag::zf) || cond_l(r);
}
[[nodiscard]] bool cond_g(const registers& r) noexcept { return !cond_le(r); }

/// Fetch the rel8 displacement and branch if `condition` holds.
///
/// The fetch happens whether or not the branch is taken — a not-taken
/// conditional jump still consumes the displacement byte, and that is
/// exactly what leaves IP at the next instruction. The displacement
/// sign-extends, and the target is relative to IP *after* the whole
/// instruction, which is where IP already sits once fetch_byte has read
/// it; the addition wraps in 16 bits and never carries into CS.
void jcc(processor& cpu, bool condition) {
  const auto rel = static_cast<std::int8_t>(cpu.fetch_byte());
  if (condition) {
    cpu.regs().ip = static_cast<std::uint16_t>(cpu.regs().ip + rel);
  }
}

}  // namespace

void jo(processor& cpu) { jcc(cpu, cond_o(cpu.regs())); }
void jno(processor& cpu) { jcc(cpu, cond_no(cpu.regs())); }
void jb(processor& cpu) { jcc(cpu, cond_b(cpu.regs())); }
void jnb(processor& cpu) { jcc(cpu, cond_nb(cpu.regs())); }
void jz(processor& cpu) { jcc(cpu, cond_z(cpu.regs())); }
void jnz(processor& cpu) { jcc(cpu, cond_nz(cpu.regs())); }
void jbe(processor& cpu) { jcc(cpu, cond_be(cpu.regs())); }
void ja(processor& cpu) { jcc(cpu, cond_a(cpu.regs())); }
void js(processor& cpu) { jcc(cpu, cond_s(cpu.regs())); }
void jns(processor& cpu) { jcc(cpu, cond_ns(cpu.regs())); }
void jp(processor& cpu) { jcc(cpu, cond_p(cpu.regs())); }
void jnp(processor& cpu) { jcc(cpu, cond_np(cpu.regs())); }
void jl(processor& cpu) { jcc(cpu, cond_l(cpu.regs())); }
void jge(processor& cpu) { jcc(cpu, cond_ge(cpu.regs())); }
void jle(processor& cpu) { jcc(cpu, cond_le(cpu.regs())); }
void jg(processor& cpu) { jcc(cpu, cond_g(cpu.regs())); }

// --- LOOPNE/LOOPNZ, LOOPE/LOOPZ, LOOP, JCXZ (E0-E3) -----------------------
//
// The LOOP family decrements CX first and tests it after: a plain 16-bit
// wrapping decrement, not alu::dec, and it sets no flags — unlike every
// other decrement in the instruction set. JCXZ tests CX without touching
// it.

void loopnz(processor& cpu) {
  const auto rel = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto cx = static_cast<std::uint16_t>(cpu.regs()[reg16::cx] - 1);
  cpu.regs()[reg16::cx] = cx;
  if (cx != 0 && !cpu.regs().flag_set(flag::zf)) {
    cpu.regs().ip = static_cast<std::uint16_t>(cpu.regs().ip + rel);
  }
}

void loopz(processor& cpu) {
  const auto rel = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto cx = static_cast<std::uint16_t>(cpu.regs()[reg16::cx] - 1);
  cpu.regs()[reg16::cx] = cx;
  if (cx != 0 && cpu.regs().flag_set(flag::zf)) {
    cpu.regs().ip = static_cast<std::uint16_t>(cpu.regs().ip + rel);
  }
}

void loop(processor& cpu) {
  const auto rel = static_cast<std::int8_t>(cpu.fetch_byte());
  const auto cx = static_cast<std::uint16_t>(cpu.regs()[reg16::cx] - 1);
  cpu.regs()[reg16::cx] = cx;
  if (cx != 0) {
    cpu.regs().ip = static_cast<std::uint16_t>(cpu.regs().ip + rel);
  }
}

void jcxz(processor& cpu) {
  const auto rel = static_cast<std::int8_t>(cpu.fetch_byte());
  if (cpu.regs()[reg16::cx] == 0) {
    cpu.regs().ip = static_cast<std::uint16_t>(cpu.regs().ip + rel);
  }
}

}  // namespace amberfolio::cpu
