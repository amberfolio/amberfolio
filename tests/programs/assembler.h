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
// Short jumps only. Every one of these programs is a few dozen bytes, a
// rel8 reaches all of it, and assemble() fails loudly rather than
// silently truncating if that ever stops being true.

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

  /// Patch the displacements and answer the finished program.
  ///
  /// Throws std::logic_error on a label that was never defined or a target
  /// out of a rel8's reach — both are mistakes in the program above, not
  /// conditions a caller can do anything about at run time.
  [[nodiscard]] std::vector<std::uint8_t> assemble() const;

 private:
  struct fixup {
    std::size_t at{};
    std::string target;
  };

  std::vector<std::uint8_t> code_;
  std::map<std::string, std::size_t, std::less<>> labels_;
  std::vector<fixup> fixups_;
};

}  // namespace amberfolio::programs
