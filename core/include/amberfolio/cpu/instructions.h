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

// --- #28: Conditional jumps and LOOP family ---------------------------

void jo(processor& cpu);
void jno(processor& cpu);
void jb(processor& cpu);
void jnb(processor& cpu);
void jz(processor& cpu);
void jnz(processor& cpu);
void jbe(processor& cpu);
void ja(processor& cpu);
void js(processor& cpu);
void jns(processor& cpu);
void jp(processor& cpu);
void jnp(processor& cpu);
void jl(processor& cpu);
void jge(processor& cpu);
void jle(processor& cpu);
void jg(processor& cpu);
void loopnz(processor& cpu);
void loopz(processor& cpu);
void loop(processor& cpu);
void jcxz(processor& cpu);

}  // namespace amberfolio::cpu
