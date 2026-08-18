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

  t.primary[0x9A] = &call_ptr16_16;
  t.primary[0xC0] = &ret_near_imm16;  // undocumented alias of C2
  t.primary[0xC1] = &ret_near;        // undocumented alias of C3
  t.primary[0xC2] = &ret_near_imm16;
  t.primary[0xC3] = &ret_near;
  t.primary[0xC8] = &ret_far_imm16;  // undocumented alias of CA
  t.primary[0xC9] = &ret_far;        // undocumented alias of CB
  t.primary[0xCA] = &ret_far_imm16;
  t.primary[0xCB] = &ret_far;
  t.primary[0xE8] = &call_rel16;
  t.primary[0xE9] = &jmp_rel16;
  t.primary[0xEA] = &jmp_ptr16_16;
  t.primary[0xEB] = &jmp_rel8;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  t.group[group_slot(0xFF)][2] = &call_rm16;
  t.group[group_slot(0xFF)][3] = &call_m16_16;
  t.group[group_slot(0xFF)][4] = &jmp_rm16;
  t.group[group_slot(0xFF)][5] = &jmp_m16_16;

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
