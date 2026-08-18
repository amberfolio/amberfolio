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

  t.primary[0x00] = &add_rm8_r8;
  t.primary[0x01] = &add_rm16_r16;
  t.primary[0x02] = &add_r8_rm8;
  t.primary[0x03] = &add_r16_rm16;
  t.primary[0x04] = &add_al_imm8;
  t.primary[0x05] = &add_ax_imm16;
  t.primary[0x10] = &adc_rm8_r8;
  t.primary[0x11] = &adc_rm16_r16;
  t.primary[0x12] = &adc_r8_rm8;
  t.primary[0x13] = &adc_r16_rm16;
  t.primary[0x14] = &adc_al_imm8;
  t.primary[0x15] = &adc_ax_imm16;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  t.group[group_slot(0x80)][0] = &add_rm8_imm8;
  t.group[group_slot(0x80)][2] = &adc_rm8_imm8;
  t.group[group_slot(0x81)][0] = &add_rm16_imm16;
  t.group[group_slot(0x81)][2] = &adc_rm16_imm16;
  t.group[group_slot(0x82)][0] = &add_rm8_imm8;
  t.group[group_slot(0x82)][2] = &adc_rm8_imm8;
  t.group[group_slot(0x83)][0] = &add_rm16_imm8;
  t.group[group_slot(0x83)][2] = &adc_rm16_imm8;

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
