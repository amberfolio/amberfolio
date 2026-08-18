// SPDX-License-Identifier: AGPL-3.0-only
//
//
//                    HOW TO ADD AN INSTRUCTION FAMILY
//                    ================================
//
// This file is the one place sixteen parallel pull requests all touch, so
// it has a rule, and the rule is the reason the wide phase of M1 can be
// worked on by sixteen people at once:
//
//     ONE LINE PER OPCODE in the tables below.
//
// Not a block, not a loop, not a helper that fills a range: one line,
// with the opcode in it, sorted by opcode. Two families adding adjacent
// opcodes then produce a conflict git resolves by keeping both lines,
// instead of a conflict a human has to think about. A `for` loop that
// fills 00-03 saves three lines and costs the next fifteen pull requests
// a merge each.
//
// A group opcode (80-83, D0-D3, F6/F7, FE/FF — see dispatch.h) goes in
// the group table instead, still one line per entry, because its eight
// entries belong to as many as four different families.
//
// The same rule holds in the three other shared files a family has to
// touch: one sorted line in the source list in core/CMakeLists.txt, that
// family's own declaration block in
// include/amberfolio/cpu/instructions.h, and one line per vector file in
// tests/conformance/registry.cpp. Five files in all — and everything
// that is actual work goes in the fifth, the family's own source file
// under src/cpu/instructions/, which nobody else has any reason to open.
//
// Do not add an include to this file: it includes instructions.h and
// nothing else, so the include list is not a shared line either.
//
// Leave an opcode you do not implement alone. A null entry is not a gap
// to be tidied up: it is what stops the machine loudly instead of
// silently doing nothing (diagnostics.h), and it is how the milestone
// knows what is left.
//
// docs/cpu-implementation.md is the full playbook: the workflow, a worked
// example, the exact commands, and how to read a failing vector.

#include "amberfolio/cpu/dispatch.h"

#include <cstdint>

#include "amberfolio/cpu/instructions.h"

namespace amberfolio::cpu {
namespace {

constexpr dispatch_table build_instruction_set() {
  dispatch_table t{};

  // --- Primary table. One line per opcode, sorted. -------------------
  //
  // (M1-F3 leaves it empty: there are no instructions yet, and every
  // opcode therefore stops the machine with a diagnostic naming it. The
  // wide phase fills it family by family; M1-C1 is done when nothing
  // here is null.)

  t.primary[0x60] = &jo;
  t.primary[0x61] = &jno;
  t.primary[0x62] = &jb;
  t.primary[0x63] = &jnb;
  t.primary[0x64] = &jz;
  t.primary[0x65] = &jnz;
  t.primary[0x66] = &jbe;
  t.primary[0x67] = &ja;
  t.primary[0x68] = &js;
  t.primary[0x69] = &jns;
  t.primary[0x6A] = &jp;
  t.primary[0x6B] = &jnp;
  t.primary[0x6C] = &jl;
  t.primary[0x6D] = &jge;
  t.primary[0x6E] = &jle;
  t.primary[0x6F] = &jg;
  t.primary[0x70] = &jo;
  t.primary[0x71] = &jno;
  t.primary[0x72] = &jb;
  t.primary[0x73] = &jnb;
  t.primary[0x74] = &jz;
  t.primary[0x75] = &jnz;
  t.primary[0x76] = &jbe;
  t.primary[0x77] = &ja;
  t.primary[0x78] = &js;
  t.primary[0x79] = &jns;
  t.primary[0x7A] = &jp;
  t.primary[0x7B] = &jnp;
  t.primary[0x7C] = &jl;
  t.primary[0x7D] = &jge;
  t.primary[0x7E] = &jle;
  t.primary[0x7F] = &jg;
  t.primary[0xE0] = &loopnz;
  t.primary[0xE1] = &loopz;
  t.primary[0xE2] = &loop;
  t.primary[0xE3] = &jcxz;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  return t;
}

constexpr dispatch_table the_instruction_set = build_instruction_set();

}  // namespace

handler dispatch_table::find(std::uint8_t opcode,
                             std::uint8_t extension) const noexcept {
  const std::size_t slot = group_slot(opcode);
  if (slot != not_a_group) {
    return group[slot][extension & 7u];
  }
  return primary[opcode];
}

const dispatch_table& instruction_set() noexcept { return the_instruction_set; }

}  // namespace amberfolio::cpu
