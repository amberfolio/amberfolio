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

// --- #24: Shift and rotate group (D0-D3) ------------------------------
//
// One handler per encoding, not per operation: which of ROL, ROR, RCL,
// RCR, SHL/SAL, SHR, the undocumented reg-6 operation, or SAR runs is
// read from the ModRM reg field at call time. See
// src/cpu/instructions/shift.cpp for why that is the natural shape here
// rather than eight handlers each.

void shift_rotate_rm8_1(processor& cpu);
void shift_rotate_rm16_1(processor& cpu);
void shift_rotate_rm8_cl(processor& cpu);
void shift_rotate_rm16_cl(processor& cpu);

}  // namespace amberfolio::cpu
