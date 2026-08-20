// SPDX-License-Identifier: AGPL-3.0-only
//
// The smallest assembler that keeps a displacement honest.
//
// The programs in this directory are machine code written by hand, and the
// one part of writing it by hand that is pure clerical risk is counting
// the bytes between a jump and its target. Get it wrong and the emulator
// runs code that is not the code you meant, which reads exactly like an
// emulator bug and costs an afternoon. So the opcodes stay literal — you
// can check them against the encoding tables byte for byte — and only the
// displacements are computed.
//
// Short jumps were all M1 needed: every one of its programs is a few
// dozen bytes and a rel8 reaches all of it. The machine programs (M2-T1,
// #56) are a few hundred bytes each and do two things a rel8 cannot, so
// there are two more fixups now and no more than two:
//
//   * `near_jump()` — rel16, for the one or two jumps in a machine
//     program that genuinely cross the whole of it, and for CALL, which
//     has no rel8 form at all.
//   * `dw_label()` — a label's own offset as a 16-bit immediate. A
//     program that hooks INT 1Ch stores the offset of its own handler
//     into the vector table, and hand-counting that offset is exactly
//     the clerical risk this file exists to remove.
//
// Both fail loudly rather than silently truncating, the same way the
// rel8 form always has.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace amberfolio::programs {

class assembler {
 public:
  /// Emit literal opcode bytes.
  void db(std::initializer_list<std::uint8_t> bytes);

  /// Emit a 16-bit immediate or displacement, low half first.
  void dw(std::uint16_t value);

  /// Name the next byte to be emitted.
  void label(std::string_view name);

  /// Emit a one-byte opcode and a rel8 displacement to `target`, which may
  /// be defined before or after this call. The displacement is counted
  /// from the byte after it, the way the processor counts it.
  void jump(std::uint8_t opcode, std::string_view target);

  /// The same, with a rel16 displacement: E9 for JMP, E8 for CALL, and
  /// nothing else in the 8086 takes one. Counted from the byte after the
  /// displacement, exactly as `jump()` counts its own.
  void near_jump(std::uint8_t opcode, std::string_view target);

  /// Emit `target`'s offset within this program as a 16-bit immediate.
  ///
  /// The offset is from the first byte the assembler emitted, so it is
  /// the program's own origin that decides what the number means — for
  /// every program in this directory that origin is offset 0 of the
  /// segment the program is loaded at, so the value is the offset a
  /// far pointer to the label wants.
  void dw_label(std::string_view target);

  /// Where `target` sits, for a caller that needs the number itself
  /// rather than an emitted word — an MZ relocation table, say, which
  /// names offsets in a file rather than in the instruction stream.
  ///
  /// Throws std::logic_error if the label is not defined yet: unlike the
  /// fixups above this is answered on the spot, so a forward reference
  /// has nothing to answer with.
  [[nodiscard]] std::size_t offset_of(std::string_view target) const;

  /// How many bytes have been emitted so far. What a program uses to
  /// place a data area at a known offset without counting instructions.
  [[nodiscard]] std::size_t size() const noexcept { return code_.size(); }

  /// Emit zero bytes until the next byte would be at `offset`. Throws if
  /// the assembler is already past it — a program whose code outgrew the
  /// hole it left for itself is a mistake, not something to accommodate.
  void pad_to(std::size_t offset);

  /// Patch the displacements and answer the finished program.
  ///
  /// Throws std::logic_error on a label that was never defined or a target
  /// out of a rel8's reach — both are mistakes in the program above, not
  /// conditions a caller can do anything about at run time.
  [[nodiscard]] std::vector<std::uint8_t> assemble() const;

 private:
  /// What the two bytes at a fixup site are supposed to become.
  enum class fixup_kind : std::uint8_t {
    /// One byte: the displacement from the byte after it to the target.
    relative8,
    /// Two bytes: the same displacement, sixteen bits wide.
    relative16,
    /// Two bytes: the target's own offset in the program.
    absolute16,
  };

  struct fixup {
    std::size_t at{};
    fixup_kind kind{};
    std::string target;
  };

  std::vector<std::uint8_t> code_;
  std::map<std::string, std::size_t, std::less<>> labels_;
  std::vector<fixup> fixups_;
};

}  // namespace amberfolio::programs
