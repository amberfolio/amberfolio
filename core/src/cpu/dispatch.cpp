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
  t.primary[0x08] = &or_rm8_r8;
  t.primary[0x09] = &or_rm16_r16;
  t.primary[0x0A] = &or_r8_rm8;
  t.primary[0x0B] = &or_r16_rm16;
  t.primary[0x0C] = &or_al_imm8;
  t.primary[0x0D] = &or_ax_imm16;
  t.primary[0x10] = &adc_rm8_r8;
  t.primary[0x11] = &adc_rm16_r16;
  t.primary[0x12] = &adc_r8_rm8;
  t.primary[0x13] = &adc_r16_rm16;
  t.primary[0x14] = &adc_al_imm8;
  t.primary[0x15] = &adc_ax_imm16;
  t.primary[0x18] = &sbb_rm8_r8;
  t.primary[0x19] = &sbb_rm16_r16;
  t.primary[0x1A] = &sbb_r8_rm8;
  t.primary[0x1B] = &sbb_r16_rm16;
  t.primary[0x1C] = &sbb_al_imm8;
  t.primary[0x1D] = &sbb_ax_imm16;
  t.primary[0x20] = &and_rm8_r8;
  t.primary[0x21] = &and_rm16_r16;
  t.primary[0x22] = &and_r8_rm8;
  t.primary[0x23] = &and_r16_rm16;
  t.primary[0x24] = &and_al_imm8;
  t.primary[0x25] = &and_ax_imm16;
  t.primary[0x28] = &sub_rm8_r8;
  t.primary[0x29] = &sub_rm16_r16;
  t.primary[0x2A] = &sub_r8_rm8;
  t.primary[0x2B] = &sub_r16_rm16;
  t.primary[0x2C] = &sub_al_imm8;
  t.primary[0x2D] = &sub_ax_imm16;
  t.primary[0x30] = &xor_rm8_r8;
  t.primary[0x31] = &xor_rm16_r16;
  t.primary[0x32] = &xor_r8_rm8;
  t.primary[0x33] = &xor_r16_rm16;
  t.primary[0x34] = &xor_al_imm8;
  t.primary[0x35] = &xor_ax_imm16;
  t.primary[0x38] = &cmp_rm8_r8;
  t.primary[0x39] = &cmp_rm16_r16;
  t.primary[0x3A] = &cmp_r8_rm8;
  t.primary[0x3B] = &cmp_r16_rm16;
  t.primary[0x3C] = &cmp_al_imm8;
  t.primary[0x3D] = &cmp_ax_imm16;
  t.primary[0x84] = &test_rm8_r8;
  t.primary[0x85] = &test_rm16_r16;
  t.primary[0x86] = &xchg_rm8_r8;
  t.primary[0x87] = &xchg_rm16_r16;
  t.primary[0x88] = &mov_rm8_r8;
  t.primary[0x89] = &mov_rm16_r16;
  t.primary[0x8A] = &mov_r8_rm8;
  t.primary[0x8B] = &mov_r16_rm16;
  t.primary[0x8C] = &mov_rm16_sreg;
  t.primary[0x8E] = &mov_sreg_rm16;
  t.primary[0x90] = &xchg_ax_ax;
  t.primary[0x91] = &xchg_ax_cx;
  t.primary[0x92] = &xchg_ax_dx;
  t.primary[0x93] = &xchg_ax_bx;
  t.primary[0x94] = &xchg_ax_sp;
  t.primary[0x95] = &xchg_ax_bp;
  t.primary[0x96] = &xchg_ax_si;
  t.primary[0x97] = &xchg_ax_di;
  t.primary[0x9A] = &call_ptr16_16;
  t.primary[0xA0] = &mov_al_moffs8;
  t.primary[0xA1] = &mov_ax_moffs16;
  t.primary[0xA2] = &mov_moffs8_al;
  t.primary[0xA3] = &mov_moffs16_ax;
  t.primary[0xA4] = &movsb;
  t.primary[0xA5] = &movsw;
  t.primary[0xA6] = &cmpsb;
  t.primary[0xA7] = &cmpsw;
  t.primary[0xA8] = &test_al_imm8;
  t.primary[0xA9] = &test_ax_imm16;
  t.primary[0xAA] = &stosb;
  t.primary[0xAB] = &stosw;
  t.primary[0xAC] = &lodsb;
  t.primary[0xAD] = &lodsw;
  t.primary[0xAE] = &scasb;
  t.primary[0xAF] = &scasw;
  t.primary[0xB0] = &mov_al_imm8;
  t.primary[0xB1] = &mov_cl_imm8;
  t.primary[0xB2] = &mov_dl_imm8;
  t.primary[0xB3] = &mov_bl_imm8;
  t.primary[0xB4] = &mov_ah_imm8;
  t.primary[0xB5] = &mov_ch_imm8;
  t.primary[0xB6] = &mov_dh_imm8;
  t.primary[0xB7] = &mov_bh_imm8;
  t.primary[0xB8] = &mov_ax_imm16;
  t.primary[0xB9] = &mov_cx_imm16;
  t.primary[0xBA] = &mov_dx_imm16;
  t.primary[0xBB] = &mov_bx_imm16;
  t.primary[0xBC] = &mov_sp_imm16;
  t.primary[0xBD] = &mov_bp_imm16;
  t.primary[0xBE] = &mov_si_imm16;
  t.primary[0xBF] = &mov_di_imm16;
  t.primary[0xC0] = &ret_near_imm16;  // undocumented alias of C2
  t.primary[0xC1] = &ret_near;        // undocumented alias of C3
  t.primary[0xC2] = &ret_near_imm16;
  t.primary[0xC3] = &ret_near;
  t.primary[0xC6] = &mov_rm8_imm8;
  t.primary[0xC7] = &mov_rm16_imm16;
  t.primary[0xC8] = &ret_far_imm16;  // undocumented alias of CA
  t.primary[0xC9] = &ret_far;        // undocumented alias of CB
  t.primary[0xCA] = &ret_far_imm16;
  t.primary[0xCB] = &ret_far;
  t.primary[0xE8] = &call_rel16;
  t.primary[0xE9] = &jmp_rel16;
  t.primary[0xEA] = &jmp_ptr16_16;
  t.primary[0xEB] = &jmp_rel8;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  t.group[group_slot(0x80)][0] = &add_rm8_imm8;
  t.group[group_slot(0x80)][1] = &or_rm8_imm8;
  t.group[group_slot(0x80)][2] = &adc_rm8_imm8;
  t.group[group_slot(0x80)][3] = &sbb_rm8_imm8;
  t.group[group_slot(0x80)][4] = &and_rm8_imm8;
  t.group[group_slot(0x80)][5] = &sub_rm8_imm8;
  t.group[group_slot(0x80)][6] = &xor_rm8_imm8;
  t.group[group_slot(0x80)][7] = &cmp_rm8_imm8;
  t.group[group_slot(0x81)][0] = &add_rm16_imm16;
  t.group[group_slot(0x81)][1] = &or_rm16_imm16;
  t.group[group_slot(0x81)][2] = &adc_rm16_imm16;
  t.group[group_slot(0x81)][3] = &sbb_rm16_imm16;
  t.group[group_slot(0x81)][4] = &and_rm16_imm16;
  t.group[group_slot(0x81)][5] = &sub_rm16_imm16;
  t.group[group_slot(0x81)][6] = &xor_rm16_imm16;
  t.group[group_slot(0x81)][7] = &cmp_rm16_imm16;
  t.group[group_slot(0x82)][0] = &add_rm8_imm8;
  t.group[group_slot(0x82)][1] = &or_rm8_imm8;
  t.group[group_slot(0x82)][2] = &adc_rm8_imm8;
  t.group[group_slot(0x82)][3] = &sbb_rm8_imm8;
  t.group[group_slot(0x82)][4] = &and_rm8_imm8;
  t.group[group_slot(0x82)][5] = &sub_rm8_imm8;
  t.group[group_slot(0x82)][6] = &xor_rm8_imm8;
  t.group[group_slot(0x82)][7] = &cmp_rm8_imm8;
  t.group[group_slot(0x83)][0] = &add_rm16_imm8;
  t.group[group_slot(0x83)][1] = &or_rm16_imm8;
  t.group[group_slot(0x83)][2] = &adc_rm16_imm8;
  t.group[group_slot(0x83)][3] = &sbb_rm16_imm8;
  t.group[group_slot(0x83)][4] = &and_rm16_imm8;
  t.group[group_slot(0x83)][5] = &sub_rm16_imm8;
  t.group[group_slot(0x83)][6] = &xor_rm16_imm8;
  t.group[group_slot(0x83)][7] = &cmp_rm16_imm8;
  t.group[group_slot(0xD0)][0] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][1] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][2] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][3] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][4] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][5] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][6] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD0)][7] = &shift_rotate_rm8_1;
  t.group[group_slot(0xD1)][0] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][1] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][2] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][3] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][4] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][5] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][6] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD1)][7] = &shift_rotate_rm16_1;
  t.group[group_slot(0xD2)][0] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][1] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][2] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][3] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][4] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][5] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][6] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD2)][7] = &shift_rotate_rm8_cl;
  t.group[group_slot(0xD3)][0] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][1] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][2] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][3] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][4] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][5] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][6] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xD3)][7] = &shift_rotate_rm16_cl;
  t.group[group_slot(0xF6)][0] = &test_rm8_imm8;
  t.group[group_slot(0xF6)][1] = &test_rm8_imm8;
  t.group[group_slot(0xF6)][2] = &not_rm8;
  t.group[group_slot(0xF6)][3] = &neg_rm8;
  t.group[group_slot(0xF6)][6] = &div_rm8;
  t.group[group_slot(0xF6)][7] = &idiv_rm8;
  t.group[group_slot(0xF7)][0] = &test_rm16_imm16;
  t.group[group_slot(0xF7)][1] = &test_rm16_imm16;
  t.group[group_slot(0xF7)][2] = &not_rm16;
  t.group[group_slot(0xF7)][3] = &neg_rm16;
  t.group[group_slot(0xF7)][6] = &div_rm16;
  t.group[group_slot(0xF7)][7] = &idiv_rm16;
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
