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

// --- #33: flags, I/O, ESC and misc ------------------------------------
//
// Everything left over once the other fifteen families have their
// opcodes: single-bit flag writes, the eight port instructions,
// undocumented SALC, the coprocessor escapes (no 8087 in this machine),
// and four opcodes with no conformance vectors at all.

// Flag ops. Each touches exactly the one bit its name says and nothing
// else — no ModRM, no immediate, no other flag.
void complement_carry(processor& cpu);      // F5 CMC
void clear_carry(processor& cpu);           // F8 CLC
void set_carry(processor& cpu);             // F9 STC
void clear_interrupt_flag(processor& cpu);  // FA CLI
void set_interrupt_flag(processor& cpu);    // FB STI
void clear_direction_flag(processor& cpu);  // FC CLD
void set_direction_flag(processor& cpu);    // FD STD

// I/O. E4/E5 take an immediate port number; EC/ED take it from DX;
// E6/E7/EE/EF are the OUT twins of the same four forms.
void in_al_imm8(processor& cpu);   // E4
void in_ax_imm8(processor& cpu);   // E5
void out_imm8_al(processor& cpu);  // E6
void out_imm8_ax(processor& cpu);  // E7
void in_al_dx(processor& cpu);     // EC
void in_ax_dx(processor& cpu);     // ED
void out_dx_al(processor& cpu);    // EE
void out_dx_ax(processor& cpu);    // EF

/// D6, undocumented: AL = FF if CF else 00. No ModRM, no flags touched.
void salc(processor& cpu);

/// D8-DF, the coprocessor escapes. One handler for all eight — there is
/// no 8087 in this machine, so every one of them decodes its ModRM byte,
/// performs the memory access the 8086 itself makes on the escape's
/// behalf, and discards the value.
void escape(processor& cpu);

/// 0F, undocumented on the 8086/8088: POP CS. A real segment load, so it
/// holds interrupt recognition off for one instruction like any other.
void pop_cs(processor& cpu);

/// 9B WAIT. There is no TEST pin on this machine for it to poll, so it
/// is, in effect, a NOP: it advances IP past itself and touches nothing.
void wait(processor& cpu);

/// F4 HLT. Enters the halted state; processor::step() is what reports it
/// and what wakes it on an interrupt (#17).
void hlt(processor& cpu);

}  // namespace amberfolio::cpu
