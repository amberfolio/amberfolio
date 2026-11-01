// SPDX-License-Identifier: AGPL-3.0-only
//
// The idioms the machine programs are built out of (machine_programs.cpp's
// top comment): the result-block store, the DOS call and exit, and the
// tests that reduce a call's carry and AX to one word. Here rather than in
// that file because it is at the content guard's size cap and the DOS
// service programs live beside it, in dos_programs.cpp.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "programs/assembler.h"
#include "programs/machine_harness.h"

namespace amberfolio::programs {

/// Register numbers as the encoding numbers them, for the ModRM byte the
/// result store builds. Only the ones a program below actually answers
/// with are named.
inline constexpr unsigned reg_ax = 0;
inline constexpr unsigned reg_cx = 1;
inline constexpr unsigned reg_dx = 2;
inline constexpr unsigned reg_bx = 3;
inline constexpr unsigned reg_bp = 5;
inline constexpr unsigned reg_si = 6;

/// The result block's word `index`, as a displacement.
[[nodiscard]] inline std::uint16_t result_word(std::size_t index) {
  return static_cast<std::uint16_t>(machine_layout::result_offset + 2 * index);
}

/// How every program answers:
///
///     mov  [result + 2*index], <reg>     ; 89 /r, mod=00 rm=110
inline void store(assembler& a, std::size_t index, unsigned reg) {
  a.db({0x89, static_cast<std::uint8_t>(0x06U | (reg << 3U))});
  a.dw(result_word(index));
}

///     mov  ax, imm16                     ; B8 iw
inline void mov_ax(assembler& a, std::uint16_t value) {
  a.db({0xB8});
  a.dw(value);
}

///     mov  dx, offset <label>            ; BA iw
inline void mov_dx_offset(assembler& a, std::string_view label) {
  a.db({0xBA});
  a.dw_label(label);
}

///     mov  ah, function                  ; B4 ib
///     int  21h                           ; CD 21
inline void int21(assembler& a, std::uint8_t function) {
  a.db({0xB4, function, 0xCD, 0x21});
}

/// The only way a machine program is allowed to end (machine_harness.h):
///
///     mov  ax, 4C00h + code              ; B8 iw
///     int  21h                           ; CD 21
inline void exit_with(assembler& a, std::uint8_t code) {
  mov_ax(a, static_cast<std::uint16_t>(0x4C00U | code));
  a.db({0xCD, 0x21});
}

/// Reduce "the call succeeded" to a word — DOS reports failure in CF
/// (service_floor.h), and an expectation is worth more as an equality
/// than as a range:
///
///     mov  ax, 1                         ; B8 01 00
///     jnc  done                          ; 73 rel8
///     xor  ax, ax                        ; 31 C0
///  done:
inline void succeeded(assembler& a, std::string_view tag) {
  mov_ax(a, 1);
  a.jump(0x73, tag);
  a.db({0x31, 0xC0});
  a.label(tag);
}

/// Keep AX when the call succeeded, zero it when it failed. For the calls
/// whose successful answer — a handle, a byte count, a file position —
/// is the thing being asserted and is never zero.
///
///     jnc  done                          ; 73 rel8
///     xor  ax, ax                        ; 31 C0
///  done:
inline void answer_or_zero(assembler& a, std::string_view tag) {
  a.jump(0x73, tag);
  a.db({0x31, 0xC0});
  a.label(tag);
}

/// Keep AX when the call *failed*, zero it when it wrongly succeeded —
/// the error-path mirror of the above. AX is DOS's error code on a
/// carry-set return, and zero is not one of them.
///
///     jc   done                          ; 72 rel8
///     xor  ax, ax                        ; 31 C0
///  done:
inline void error_or_zero(assembler& a, std::string_view tag) {
  a.jump(0x72, tag);
  a.db({0x31, 0xC0});
  a.label(tag);
}

/// Reduce "it succeeded and answered exactly `want`" to 1, and anything
/// else to 0 — for the calls whose right answer is zero, where
/// `answer_or_zero`'s own zero would be ambiguous.
///
///     jc   no                            ; 72 rel8
///     cmp  ax, want                      ; 3D iw
///     jne  no                            ; 75 rel8
///     mov  ax, 1                         ; B8 01 00
///     jmp  done                          ; EB rel8
///  no: xor  ax, ax                       ; 31 C0
///  done:
inline void answered(assembler& a, std::uint16_t want, std::string_view tag) {
  const std::string no = std::string(tag) + "_no";
  a.jump(0x72, no);
  a.db({0x3D});
  a.dw(want);
  a.jump(0x75, no);
  mov_ax(a, 1);
  a.jump(0xEB, tag);
  a.label(no);
  a.db({0x31, 0xC0});
  a.label(tag);
}

/// Reduce "BP is at least `least`" to a word. What a program uses to
/// answer a question whose exact number depends on where in a divisor's
/// period a wait happened to start.
///
///     mov  ax, 1                         ; B8 01 00
///     cmp  bp, least                     ; 83 FD ib
///     jae  done                          ; 73 rel8
///     xor  ax, ax                        ; 31 C0
///  done:
inline void bp_reached(assembler& a, std::uint8_t least, std::string_view tag) {
  mov_ax(a, 1);
  a.db({0x83, 0xFD, least});
  a.jump(0x73, tag);
  a.db({0x31, 0xC0});
  a.label(tag);
}

/// A raw program's finished bytes, checked against the result block it
/// shares a segment with.
///
/// A program that grew past `result_offset` would overwrite its own
/// answers, and the failure would read as a wrong result rather than as
/// the mistake it is. Cheap to check here, impossible to work out from
/// the failure otherwise.
[[nodiscard]] inline std::vector<std::uint8_t> raw_program(const assembler& a) {
  std::vector<std::uint8_t> code = a.assemble();
  if (code.size() > machine_layout::result_offset) {
    throw std::logic_error("the program has grown into its own result block (" +
                           std::to_string(code.size()) + " bytes)");
  }
  return code;
}

/// Bytes of an ASCIZ string, as a program's own data.
inline void asciz(assembler& a, std::string_view label, std::string_view text) {
  a.label(label);
  for (const char c : text) {
    a.db({static_cast<std::uint8_t>(c)});
  }
  a.db({0x00});
}

}  // namespace amberfolio::programs
