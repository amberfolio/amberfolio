// SPDX-License-Identifier: AGPL-3.0-only
//
// Where instruction handlers are declared, so that dispatch.cpp — the
// file every family's pull request has to touch — includes this one
// header and never grows an include list that sixteen branches fight
// over.
//
// A family adds its own block below, under its own heading, and defines
// the handlers in its own source file under src/cpu/instructions/. Blocks
// are ordered the way M1's issues are (#18 MOV/XCHG, #19 ADD/ADC, #20
// SUB/SBB/CMP/NEG, and so on), which keeps two families' additions apart
// in the file even when their opcodes are adjacent.
//
// Handlers take the processor and return nothing; dispatch.h says what
// the decoder has already done for them by the time they are called.
// docs/cpu-implementation.md (M1-F6) is the how-to.

#pragma once

#include "amberfolio/cpu/dispatch.h"

namespace amberfolio::cpu {

// --- #20: SUB/SBB/CMP/NEG ----------------------------------------------

void sub_rm8_r8(processor& cpu);
void sub_rm16_r16(processor& cpu);
void sub_r8_rm8(processor& cpu);
void sub_r16_rm16(processor& cpu);
void sub_al_imm8(processor& cpu);
void sub_ax_imm16(processor& cpu);
void sub_rm8_imm8(processor& cpu);
void sub_rm16_imm16(processor& cpu);
void sub_rm16_imm8(processor& cpu);

void sbb_rm8_r8(processor& cpu);
void sbb_rm16_r16(processor& cpu);
void sbb_r8_rm8(processor& cpu);
void sbb_r16_rm16(processor& cpu);
void sbb_al_imm8(processor& cpu);
void sbb_ax_imm16(processor& cpu);
void sbb_rm8_imm8(processor& cpu);
void sbb_rm16_imm16(processor& cpu);
void sbb_rm16_imm8(processor& cpu);

void cmp_rm8_r8(processor& cpu);
void cmp_rm16_r16(processor& cpu);
void cmp_r8_rm8(processor& cpu);
void cmp_r16_rm16(processor& cpu);
void cmp_al_imm8(processor& cpu);
void cmp_ax_imm16(processor& cpu);
void cmp_rm8_imm8(processor& cpu);
void cmp_rm16_imm16(processor& cpu);
void cmp_rm16_imm8(processor& cpu);

void neg_rm8(processor& cpu);
void neg_rm16(processor& cpu);

}  // namespace amberfolio::cpu
