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

// --- #23: Stack — PUSH/POP/PUSHF/POPF ---------------------------------

void push_ax(processor& cpu);
void push_cx(processor& cpu);
void push_dx(processor& cpu);
void push_bx(processor& cpu);
void push_sp(processor& cpu);
void push_bp(processor& cpu);
void push_si(processor& cpu);
void push_di(processor& cpu);

void pop_ax(processor& cpu);
void pop_cx(processor& cpu);
void pop_dx(processor& cpu);
void pop_bx(processor& cpu);
void pop_sp(processor& cpu);
void pop_bp(processor& cpu);
void pop_si(processor& cpu);
void pop_di(processor& cpu);

void push_es(processor& cpu);
void push_cs(processor& cpu);
void push_ss(processor& cpu);
void push_ds(processor& cpu);

void pop_es(processor& cpu);
void pop_ss(processor& cpu);
void pop_ds(processor& cpu);

void push_rm16(processor& cpu);
void pop_rm16(processor& cpu);

void pushf(processor& cpu);
void popf(processor& cpu);

}  // namespace amberfolio::cpu
