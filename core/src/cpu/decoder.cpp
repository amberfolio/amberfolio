// SPDX-License-Identifier: AGPL-3.0-only
//
// The two facts about an opcode byte that the decoder needs before it
// knows anything else: whether it is a prefix, and whether the byte after
// it is a ModRM byte.
//
// Both are properties of the 8086's encoding, not of which instructions
// we have got round to implementing, so they live here rather than in the
// dispatch table. A family never touches this file.

#include "amberfolio/cpu/decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace amberfolio::cpu {
namespace {

constexpr std::array<bool, 256> build_prefix_table() {
  std::array<bool, 256> t{};
  t[0x26] = true;  // ES:
  t[0x2E] = true;  // CS:
  t[0x36] = true;  // SS:
  t[0x3E] = true;  // DS:
  t[0xF0] = true;  // LOCK
  // F1 is not documented, but the 8086 decodes it exactly as it decodes
  // F0. Treating it as an unknown opcode instead would be a guess in the
  // other direction, and a program that hits it would stop dead.
  t[0xF1] = true;
  t[0xF2] = true;  // REPNE
  t[0xF3] = true;  // REP / REPE
  return t;
}

constexpr std::array<bool, 256> build_modrm_table() {
  std::array<bool, 256> t{};

  // The eight ALU operations, each in its four r/m forms: r/m8,r8 —
  // r/m16,r16 — r8,r/m8 — r16,r/m16. ADD OR ADC SBB AND SUB XOR CMP, at
  // 0x00, 0x08, ... 0x38.
  for (std::size_t base = 0x00; base <= 0x38; base += 0x08) {
    for (std::size_t i = 0; i < 4; ++i) {
      t[base + i] = true;
    }
  }

  // The rest, by opcode. Written out rather than looped, because the
  // ranges are not the point — being able to check the list against an
  // opcode map is.
  for (const std::size_t op : {
           std::size_t{0x80},  // ALU r/m, imm8      (group)
           std::size_t{0x81},  // ALU r/m16, imm16   (group)
           std::size_t{0x82},  // ALU r/m8, imm8     (group; 80's twin)
           std::size_t{0x83},  // ALU r/m16, imm8sx  (group)
           std::size_t{0x84},  // TEST r/m8, r8
           std::size_t{0x85},  // TEST r/m16, r16
           std::size_t{0x86},  // XCHG r/m8, r8
           std::size_t{0x87},  // XCHG r/m16, r16
           std::size_t{0x88},  // MOV r/m8, r8
           std::size_t{0x89},  // MOV r/m16, r16
           std::size_t{0x8A},  // MOV r8, r/m8
           std::size_t{0x8B},  // MOV r16, r/m16
           std::size_t{0x8C},  // MOV r/m16, Sreg
           std::size_t{0x8D},  // LEA r16, m
           std::size_t{0x8E},  // MOV Sreg, r/m16
           std::size_t{0x8F},  // POP r/m16
           std::size_t{0xC4},  // LES r16, m16:16
           std::size_t{0xC5},  // LDS r16, m16:16
           std::size_t{0xC6},  // MOV r/m8, imm8
           std::size_t{0xC7},  // MOV r/m16, imm16
           std::size_t{0xD0},  // shift r/m8, 1      (group)
           std::size_t{0xD1},  // shift r/m16, 1     (group)
           std::size_t{0xD2},  // shift r/m8, CL     (group)
           std::size_t{0xD3},  // shift r/m16, CL    (group)
           std::size_t{0xF6},  // TEST/NOT/NEG/MUL.. (group)
           std::size_t{0xF7},  // ditto, 16-bit      (group)
           std::size_t{0xFE},  // INC/DEC r/m8       (group)
           std::size_t{0xFF},  // INC/DEC/CALL/JMP/PUSH r/m16 (group)
       }) {
    t[op] = true;
  }

  // ESC — the coprocessor escapes. There is no 8087 in this machine, but
  // the encoding is the encoding: D8-DF take a ModRM byte, and skipping
  // it would leave IP in the middle of an instruction.
  for (std::size_t op = 0xD8; op <= 0xDF; ++op) {
    t[op] = true;
  }

  return t;
}

constexpr std::array<bool, 256> prefix_table = build_prefix_table();
constexpr std::array<bool, 256> modrm_table = build_modrm_table();

}  // namespace

bool is_prefix(std::uint8_t byte) noexcept { return prefix_table[byte]; }

bool has_modrm(std::uint8_t opcode) noexcept { return modrm_table[opcode]; }

}  // namespace amberfolio::cpu
