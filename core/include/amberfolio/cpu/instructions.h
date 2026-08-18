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

// --- #18: MOV/XCHG -----------------------------------------------------

void mov_rm8_r8(processor& cpu);
void mov_rm16_r16(processor& cpu);
void mov_r8_rm8(processor& cpu);
void mov_r16_rm16(processor& cpu);
void mov_rm16_sreg(processor& cpu);
void mov_sreg_rm16(processor& cpu);
void mov_al_moffs8(processor& cpu);
void mov_ax_moffs16(processor& cpu);
void mov_moffs8_al(processor& cpu);
void mov_moffs16_ax(processor& cpu);
void mov_al_imm8(processor& cpu);
void mov_cl_imm8(processor& cpu);
void mov_dl_imm8(processor& cpu);
void mov_bl_imm8(processor& cpu);
void mov_ah_imm8(processor& cpu);
void mov_ch_imm8(processor& cpu);
void mov_dh_imm8(processor& cpu);
void mov_bh_imm8(processor& cpu);
void mov_ax_imm16(processor& cpu);
void mov_cx_imm16(processor& cpu);
void mov_dx_imm16(processor& cpu);
void mov_bx_imm16(processor& cpu);
void mov_sp_imm16(processor& cpu);
void mov_bp_imm16(processor& cpu);
void mov_si_imm16(processor& cpu);
void mov_di_imm16(processor& cpu);
void mov_rm8_imm8(processor& cpu);
void mov_rm16_imm16(processor& cpu);
void xchg_rm8_r8(processor& cpu);
void xchg_rm16_r16(processor& cpu);
void xchg_ax_ax(processor& cpu);
void xchg_ax_cx(processor& cpu);
void xchg_ax_dx(processor& cpu);
void xchg_ax_bx(processor& cpu);
void xchg_ax_sp(processor& cpu);
void xchg_ax_bp(processor& cpu);
void xchg_ax_si(processor& cpu);
void xchg_ax_di(processor& cpu);

// --- #19: ADD/ADC ------------------------------------------------------

void add_rm8_r8(processor& cpu);
void add_rm16_r16(processor& cpu);
void add_r8_rm8(processor& cpu);
void add_r16_rm16(processor& cpu);
void add_al_imm8(processor& cpu);
void add_ax_imm16(processor& cpu);
void add_rm8_imm8(processor& cpu);
void add_rm16_imm16(processor& cpu);
void add_rm16_imm8(processor& cpu);

void adc_rm8_r8(processor& cpu);
void adc_rm16_r16(processor& cpu);
void adc_r8_rm8(processor& cpu);
void adc_r16_rm16(processor& cpu);
void adc_al_imm8(processor& cpu);
void adc_ax_imm16(processor& cpu);
void adc_rm8_imm8(processor& cpu);
void adc_rm16_imm16(processor& cpu);
void adc_rm16_imm8(processor& cpu);

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

// --- #26: DIV/IDIV -----------------------------------------------------

void div_rm8(processor& cpu);
void div_rm16(processor& cpu);
void idiv_rm8(processor& cpu);
void idiv_rm16(processor& cpu);

// --- #30: MOVS/CMPS/STOS/LODS/SCAS + REP ------------------------------

void movsb(processor& cpu);
void movsw(processor& cpu);
void cmpsb(processor& cpu);
void cmpsw(processor& cpu);
void stosb(processor& cpu);
void stosw(processor& cpu);
void lodsb(processor& cpu);
void lodsw(processor& cpu);
void scasb(processor& cpu);
void scasw(processor& cpu);

}  // namespace amberfolio::cpu
