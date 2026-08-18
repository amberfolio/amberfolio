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

  t.primary[0x40] = &inc_ax;
  t.primary[0x41] = &inc_cx;
  t.primary[0x42] = &inc_dx;
  t.primary[0x43] = &inc_bx;
  t.primary[0x44] = &inc_sp;
  t.primary[0x45] = &inc_bp;
  t.primary[0x46] = &inc_si;
  t.primary[0x47] = &inc_di;
  t.primary[0x48] = &dec_ax;
  t.primary[0x49] = &dec_cx;
  t.primary[0x4A] = &dec_dx;
  t.primary[0x4B] = &dec_bx;
  t.primary[0x4C] = &dec_sp;
  t.primary[0x4D] = &dec_bp;
  t.primary[0x4E] = &dec_si;
  t.primary[0x4F] = &dec_di;

  // --- Group tables. One line per (opcode, reg) entry. ---------------

  t.group[group_slot(0xFE)][0] = &inc_rm8;
  t.group[group_slot(0xFE)][1] = &dec_rm8;
  t.group[group_slot(0xFF)][0] = &inc_rm16;
  t.group[group_slot(0xFF)][1] = &dec_rm16;

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
