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

// --- #29: CALL/JMP/RET -------------------------------------------------

void call_rel16(processor& cpu);
void call_ptr16_16(processor& cpu);
void call_rm16(processor& cpu);
void call_m16_16(processor& cpu);

void jmp_rel16(processor& cpu);
void jmp_rel8(processor& cpu);
void jmp_ptr16_16(processor& cpu);
void jmp_rm16(processor& cpu);
void jmp_m16_16(processor& cpu);

void ret_near(processor& cpu);
void ret_near_imm16(processor& cpu);
void ret_far(processor& cpu);
void ret_far_imm16(processor& cpu);

}  // namespace amberfolio::cpu
