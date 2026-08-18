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

  t.primary[0x08] = &or_rm8_r8;
  t.primary[0x09] = &or_rm16_r16;
  t.primary[0x0A] = &or_r8_rm8;
  t.primary[0x0B] = &or_r16_rm16;
  t.primary[0x0C] = &or_al_imm8;
  t.primary[0x0D] = &or_ax_imm16;
  t.primary[0x20] = &and_rm8_r8;
  t.primary[0x21] = &and_rm16_r16;
  t.primary[0x22] = &and_r8_rm8;
  t.primary[0x23] = &and_r16_rm16;
  t.primary[0x24] = &and_al_imm8;
  t.primary[0x25] = &and_ax_imm16;
  t.primary[0x30] = &xor_rm8_r8;
  t.primary[0x31] = &xor_rm16_r16;
  t.primary[0x32] = &xor_r8_rm8;
  t.primary[0x33] = &xor_r16_rm16;
  t.primary[0x34] = &xor_al_imm8;
  t.primary[0x35] = &xor_ax_imm16;
  t.primary[0x84] = &test_rm8_r8;
  t.primary[0x85] = &test_rm16_r16;
  t.primary[0xA8] = &test_al_imm8;
  t.primary[0xA9] = &test_ax_imm16;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  t.group[group_slot(0x80)][1] = &or_rm8_imm8;
  t.group[group_slot(0x80)][4] = &and_rm8_imm8;
  t.group[group_slot(0x80)][6] = &xor_rm8_imm8;
  t.group[group_slot(0x81)][1] = &or_rm16_imm16;
  t.group[group_slot(0x81)][4] = &and_rm16_imm16;
  t.group[group_slot(0x81)][6] = &xor_rm16_imm16;
  t.group[group_slot(0x82)][1] = &or_rm8_imm8;
  t.group[group_slot(0x82)][4] = &and_rm8_imm8;
  t.group[group_slot(0x82)][6] = &xor_rm8_imm8;
  t.group[group_slot(0x83)][1] = &or_rm16_imm8;
  t.group[group_slot(0x83)][4] = &and_rm16_imm8;
  t.group[group_slot(0x83)][6] = &xor_rm16_imm8;
  t.group[group_slot(0xF6)][0] = &test_rm8_imm8;
  t.group[group_slot(0xF6)][1] = &test_rm8_imm8;
  t.group[group_slot(0xF6)][2] = &not_rm8;
  t.group[group_slot(0xF7)][0] = &test_rm16_imm16;
  t.group[group_slot(0xF7)][1] = &test_rm16_imm16;
  t.group[group_slot(0xF7)][2] = &not_rm16;

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
