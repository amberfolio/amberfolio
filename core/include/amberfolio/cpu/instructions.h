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

// --- #22: INC/DEC ------------------------------------------------------

void inc_ax(processor& cpu);
void inc_cx(processor& cpu);
void inc_dx(processor& cpu);
void inc_bx(processor& cpu);
void inc_sp(processor& cpu);
void inc_bp(processor& cpu);
void inc_si(processor& cpu);
void inc_di(processor& cpu);

void dec_ax(processor& cpu);
void dec_cx(processor& cpu);
void dec_dx(processor& cpu);
void dec_bx(processor& cpu);
void dec_sp(processor& cpu);
void dec_bp(processor& cpu);
void dec_si(processor& cpu);
void dec_di(processor& cpu);

void inc_rm8(processor& cpu);
void dec_rm8(processor& cpu);
void inc_rm16(processor& cpu);
void dec_rm16(processor& cpu);

}  // namespace amberfolio::cpu
