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
