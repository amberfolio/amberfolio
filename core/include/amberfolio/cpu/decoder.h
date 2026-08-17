// SPDX-License-Identifier: AGPL-3.0-only
//
// Everything between "fetch a byte" and "call the handler": what the
// prefix bytes said, what the ModRM byte said, and where the memory
// operand it named actually lives.
//
// The types here are what an instruction handler reads instead of
// re-decoding anything. The decoding itself is processor::step() and the
// two tables in decoder.cpp, because it needs the bus and the registers;
// this header is the vocabulary.

#pragma once

#include <cstdint>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

/// Where a memory operand lives, before the segment and the offset are
/// folded together.
///
/// They are kept apart because the machine keeps them apart: the offset
/// wraps at 64 KiB inside its segment and never carries out into the
/// segment number. A word at offset FFFF reads FFFF and then 0000 of the
/// *same* segment, not the first byte of the next one — collapse the pair
/// to a physical address too early and that behaviour is gone.
struct address {
  std::uint16_t segment{};
  std::uint16_t offset{};

  friend constexpr bool operator==(const address&, const address&) = default;
};

/// The repeat prefix in force, if any. F3 is REP/REPE and F2 is REPNE;
/// which of the two an instruction cares about depends on the
/// instruction, so the decoder records what it saw and judges nothing.
enum class repeat : std::uint8_t { none, repe, repne };

/// What the prefix bytes ahead of the opcode said.
///
/// Prefixes may appear in any number and any order, and the conformance
/// vectors prepend random ones — including overrides on instructions that
/// have no memory operand to override. Later prefixes of the same kind
/// replace earlier ones, which is what the part does.
struct prefix_state {
  /// Whether a segment-override prefix (26/2E/36/3E) was seen, and the
  /// last one if so. Applied to the effective address by the decoder;
  /// handlers do not have to think about it.
  bool has_segment_override{false};
  sreg segment_override{sreg::ds};

  repeat rep{repeat::none};

  /// F0, and F1 which the 8086 treats the same way. Recorded and
  /// otherwise ignored: there is no second bus master to lock against.
  bool lock{false};

  /// How many prefix bytes there were, and the offset of the last one.
  ///
  /// This pair exists for M1-F7. An 8086 interrupted part-way through a
  /// repeated string instruction resumes it by backing IP up to the
  /// *last* prefix rather than to the start of the instruction — so a
  /// REP with a segment override loses the override on resume, which is
  /// a real, observable bug in the part that software of the era had to
  /// work around. Interrupt delivery needs to be able to reproduce it,
  /// and it can only do that if the decoder wrote down where that byte
  /// was.
  std::uint16_t count{};
  std::uint16_t last_prefix_ip{};

  friend constexpr bool operator==(const prefix_state&,
                                   const prefix_state&) = default;
};

/// A decoded ModRM byte: mod:2, reg:3, rm:3.
///
/// `reg` is a register operand for most opcodes and the instruction's own
/// identity for the group opcodes (see dispatch.h). `rm` is a register
/// when mod is 3 and one of twenty-four addressing forms otherwise.
struct modrm {
  std::uint8_t mod{};
  std::uint8_t reg{};
  std::uint8_t rm{};

  /// True when mod == 3: the r/m operand is a register, and there is no
  /// effective address.
  [[nodiscard]] constexpr bool names_a_register() const noexcept {
    return mod == 3;
  }

  friend constexpr bool operator==(const modrm&, const modrm&) = default;
};

[[nodiscard]] constexpr modrm split_modrm(std::uint8_t byte) noexcept {
  return {.mod = static_cast<std::uint8_t>(byte >> 6),
          .reg = static_cast<std::uint8_t>((byte >> 3) & 7),
          .rm = static_cast<std::uint8_t>(byte & 7)};
}

/// Is this byte a prefix rather than an opcode? 26/2E/36/3E (segment),
/// F0 and F1 (lock), F2 (REPNE), F3 (REP/REPE).
[[nodiscard]] bool is_prefix(std::uint8_t byte) noexcept;

/// The segment a segment-override prefix selects. Only meaningful for
/// the four override bytes.
[[nodiscard]] constexpr sreg override_segment(std::uint8_t prefix) noexcept {
  // 26/2E/36/3E happen to encode ES/CS/SS/DS in bits 3-4, in the same
  // order sreg numbers them. Not a coincidence — it is the same field.
  return static_cast<sreg>((prefix >> 3) & 3);
}

/// Does this opcode carry a ModRM byte? Fixed 8086 encoding fact, not a
/// per-family choice: 68 of the 256 do.
[[nodiscard]] bool has_modrm(std::uint8_t opcode) noexcept;

/// What the decoder worked out about the instruction currently executing.
/// A handler reads this instead of decoding anything for itself.
struct instruction {
  /// Offset of the instruction's first byte, prefixes included. A clean
  /// stop rewinds IP to here.
  std::uint16_t start_ip{};

  std::uint8_t opcode{};

  prefix_state prefixes{};

  /// Valid only when `modrm_present`.
  cpu::modrm modrm{};
  bool modrm_present{false};

  /// The effective address of the r/m operand. Valid only when
  /// `modrm_present` and the ModRM byte does not name a register.
  address ea{};
};

}  // namespace amberfolio::cpu
