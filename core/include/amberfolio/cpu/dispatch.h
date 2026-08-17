// SPDX-License-Identifier: AGPL-3.0-only
//
// How an opcode becomes a call. The tables themselves are in
// dispatch.cpp, and so is the rule for adding to them — read that file
// before wiring a family up.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace amberfolio::cpu {

class processor;

/// An instruction's implementation.
///
/// It takes the processor and nothing else. By the time it is called the
/// decoder has consumed the prefixes and, where the opcode has one, the
/// ModRM byte and its displacement, and has computed the effective
/// address — all of it readable from `processor::current()`. Whatever is
/// still ahead in the instruction stream, which is to say the immediate
/// operands, the handler fetches for itself with `processor::fetch_byte`
/// and `fetch_word`; those leave IP exactly where the next instruction
/// begins.
using handler = void (*)(processor&);

/// The opcodes whose ModRM `reg` field names the instruction rather than
/// an operand, so that one opcode is eight instructions.
///
/// 80-83 are the ALU-with-immediate group, D0-D3 the shifts and rotates,
/// F6/F7 the TEST/NOT/NEG/MUL/IMUL/DIV/IDIV group, and FE/FF the
/// INC/DEC/CALL/JMP/PUSH group. They are listed per opcode rather than
/// per group because the eight entries of 80 and of 81 are different
/// instructions — same operation, different operand widths — and they are
/// implemented by different issues.
///
/// 8F, C6 and C7 are *not* here. They are nominally groups too, but the
/// 8086 ignores their reg field entirely rather than decoding it, so they
/// get one handler each like any other opcode.
inline constexpr std::array<std::uint8_t, 12> group_opcodes = {
    0x80, 0x81, 0x82, 0x83, 0xD0, 0xD1, 0xD2, 0xD3, 0xF6, 0xF7, 0xFE, 0xFF};

inline constexpr std::size_t group_count = group_opcodes.size();

/// What `group_slot` answers for an opcode that is not a group opcode.
inline constexpr std::size_t not_a_group = group_count;

[[nodiscard]] constexpr std::size_t group_slot(std::uint8_t opcode) noexcept {
  for (std::size_t i = 0; i < group_opcodes.size(); ++i) {
    if (group_opcodes[i] == opcode) {
      return i;
    }
  }
  return not_a_group;
}

/// A complete instruction set: one entry per opcode, plus eight per group
/// opcode. A null entry is an instruction that is not implemented, and it
/// stops the machine rather than doing nothing (diagnostics.h).
///
/// It is a value rather than a global so that a test can run a processor
/// against a table of its own — which is the only way to exercise the
/// decoder before there are any instructions to decode into.
struct dispatch_table {
  /// Indexed by opcode. The entries for the group opcodes are unused;
  /// `find` never consults them.
  std::array<handler, 256> primary{};

  /// Indexed by [group_slot(opcode)][ModRM reg].
  std::array<std::array<handler, 8>, group_count> group{};

  /// The handler for `opcode`, using `extension` — the ModRM reg field —
  /// when the opcode is a group. Null if there is none.
  [[nodiscard]] handler find(std::uint8_t opcode,
                             std::uint8_t extension) const noexcept;
};

/// The table the machine runs: every instruction M1 implements.
[[nodiscard]] const dispatch_table& instruction_set() noexcept;

}  // namespace amberfolio::cpu
