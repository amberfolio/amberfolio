// SPDX-License-Identifier: AGPL-3.0-only
//
// The select-yellow seam's stand-in program, at program scale (#453). It is
// in a file of its own because machine_programs.cpp is at the content
// guard's size cap; the three idioms it needs are restated here, and its
// two `machine_program` entries are in that file's list.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "programs/assembler.h"
#include "programs/exe.h"
#include "programs/machine_harness.h"
#include "programs/machine_programs.h"

namespace amberfolio::programs {
namespace {

constexpr unsigned reg_bx = 3;

/// The result block's word `index`, as a displacement.
[[nodiscard]] std::uint16_t result_word(std::size_t index) {
  return static_cast<std::uint16_t>(machine_layout::result_offset + 2 * index);
}

///     mov  [result + 2*index], <reg>     ; 89 /r, mod=00 rm=110
void store(assembler& a, std::size_t index, unsigned reg) {
  a.db({0x89, static_cast<std::uint8_t>(0x06U | (reg << 3U))});
  a.dw(result_word(index));
}

///     mov  ax, 4C00h + code ; int 21h
void exit_with(assembler& a, std::uint8_t code) {
  a.db({0xB8});
  a.dw(static_cast<std::uint16_t>(0x4C00U | code));
  a.db({0xCD, 0x21});
}

// The program
//
// #453. The seam rewrites one colour, in a pushed word or a local, before the
// program draws with it. This program lays out three frames where the
// seam's facts say they are, and reaches the build's own handlers at made-up
// addresses: the bar leaf's glyph call (a made-up bar, the second character
// of its first word, which is not a command letter), the pick-list leaf's
// string call, and the hit-point value's colour local (a highlighted draw).
// Each reads the colour back from where the program would have read it.
//
//         mov  bx, 0A00h                      ; the bar routine's frame
//         <bar's colours, length, text "Ab Cd", two group starts>
//         mov  bp, 0800h                      ; the leaf's frame
//         <caller, group 1, index 2, the call's six words at 0600h>
//         mov  sp, di
// bar:    nop                                 ; the seam's point
//         mov  bl, [di+6] ; store
//         ... the list's string call and context, then
// list:   nop ; mov bl, [di+4] ; store
//         ... the hit points' frame, then
// hp:     nop ; mov bl, [bp-1] ; store

constexpr std::uint16_t yellow_frame = 0x0800;
constexpr std::uint16_t yellow_caller = 0x0A00;
constexpr std::uint16_t yellow_context = 0x0C00;
constexpr std::uint16_t yellow_words = 0x0600;

constexpr std::uint8_t yellow_white = 0x0F;
constexpr std::uint8_t yellow_magenta = 0x0D;

struct select_yellow_layout {
  std::vector<std::uint8_t> file;
  std::uint32_t bar_offset{};
  std::uint32_t list_offset{};
  std::uint32_t hp_offset{};
};

/// `mov byte ss:[bx + disp16], imm8`: the frames are in the stack segment,
/// and a base of BX or DI would otherwise address the data segment.
void yellow_caller_byte(assembler& a, std::int16_t displacement,
                        std::uint8_t value) {
  a.db({0x36, 0xC6, 0x87});
  a.dw(static_cast<std::uint16_t>(displacement));
  a.db({value});
}

/// `mov word ss:[di + disp8], imm16`
void yellow_word(assembler& a, std::uint8_t displacement, std::uint16_t value) {
  a.db({0x36, 0xC7, 0x45, displacement});
  a.dw(value);
}

[[nodiscard]] const select_yellow_layout& select_yellow_probe() {
  static const select_yellow_layout built = [] {
    assembler a;
    a.db({0xBB});
    a.dw(yellow_caller);                        // mov bx, 0A00h
    yellow_caller_byte(a, 0x0E, yellow_white);  // color_hi
    yellow_caller_byte(a, 0x10, 0x0A);          // color_lo
    yellow_caller_byte(a, -0x64, 5);            // the bar's length
    yellow_caller_byte(a, -0x53, 5);            // and its Pascal length
    yellow_caller_byte(a, -0x52, 'A');
    yellow_caller_byte(a, -0x51, 'b');
    yellow_caller_byte(a, -0x50, ' ');
    yellow_caller_byte(a, -0x4F, 'C');
    yellow_caller_byte(a, -0x4E, 'd');
    yellow_caller_byte(a, -0x8F + 2, 1);  // group 1 starts at 1
    yellow_caller_byte(a, -0x8F + 4, 4);  // group 2 at the C

    a.db({0xBD});
    a.dw(yellow_frame);  // mov bp, 0800h
    a.db({0xC7, 0x46, 0x06});
    a.dw(yellow_caller);  // mov word [bp+6], 0A00h
    a.db({0xC7, 0x46, 0x08});
    a.dw(1);                         // mov word [bp+8], 1: the group
    a.db({0xC6, 0x46, 0xFF, 0x02});  // mov byte [bp-1], 2: the index
    a.db({0xBF});
    a.dw(yellow_words);                         // mov di, 0600h
    yellow_word(a, 0, 0x0A01);                  // the fold flag, junk above
    yellow_word(a, 2, 'b');                     // the character
    yellow_word(a, 4, 0x0A01);                  // the count
    yellow_word(a, 6, 0x0A00U | yellow_white);  // the colour
    yellow_word(a, 8, 0x0A18);                  // the row
    yellow_word(a, 10, 7);                      // the column
    a.db({0x8B, 0xE7});                         // mov sp, di
    a.label("bar");
    a.db({0x90});                    // the seam's point
    a.db({0x36, 0x8A, 0x5D, 0x06});  // mov bl, ss:[di+6]
    a.db({0x0E, 0x1F});              // push cs / pop ds
    a.db({0x30, 0xFF});              // xor bh, bh
    store(a, 0, reg_bx);

    // The list's leaf: a context with a colour, and a string call.
    a.db({0xBB});
    a.dw(yellow_context);                          // mov bx, 0C00h
    a.db({0x36, 0xC6, 0x47, 0x20, yellow_white});  // mov byte ss:[bx+20h], 0Fh
    a.db({0xC7, 0x46, 0x06});
    a.dw(yellow_context);                       // mov word [bp+6], 0C00h
    yellow_word(a, 0, 0x0700);                  // the string's offset
    a.db({0x36, 0x8C, 0x55, 0x02});             // mov ss:[di+2], ss
    yellow_word(a, 4, 0x0A00U | yellow_white);  // the colour
    yellow_word(a, 6, 9);
    yellow_word(a, 8, 3);
    a.label("list");
    a.db({0x90});
    a.db({0x36, 0x8A, 0x5D, 0x04});  // mov bl, ss:[di+4]
    a.db({0x30, 0xFF});
    store(a, 1, reg_bx);

    // Hit points, highlighted: the first argument set, the colour local
    // the program chose.
    a.db({0xC7, 0x46, 0x06});
    a.dw(1);                                   // mov word [bp+6], 1
    a.db({0xC6, 0x46, 0xFF, yellow_magenta});  // mov byte [bp-1], 0Dh
    a.label("hp");
    a.db({0x90});
    a.db({0x8A, 0x5E, 0xFF});  // mov bl, [bp-1]
    a.db({0x30, 0xFF});
    store(a, 2, reg_bx);
    exit_with(a, 0x8E);

    select_yellow_layout out;
    out.bar_offset = static_cast<std::uint32_t>(a.offset_of("bar"));
    out.list_offset = static_cast<std::uint32_t>(a.offset_of("list"));
    out.hp_offset = static_cast<std::uint32_t>(a.offset_of("hp"));
    out.file = build_exe({.initial_cs = 0,
                          .initial_ip = 0,
                          .initial_ss = 0,
                          .initial_sp = 0x0F00,
                          .min_alloc = 0x1600,
                          .relocations = {},
                          .image = a.assemble()});
    return out;
  }();
  return built;
}

/// One of the `select-yellow` handlers, from the `modern-controls`
/// definition this build ships: the piece's seven points are the table's
/// last (seam_modern_controls.cpp lists the order).
[[nodiscard]] machine::seam_handler select_yellow_handler(std::size_t which) {
  constexpr std::size_t first = 12;
  for (const machine::seam_definition& seam : machine::all_seams()) {
    if (seam.id == "modern-controls" && first + which < seam.points.size()) {
      return seam.points[first + which].run;
    }
  }
  return nullptr;
}

}  // namespace

const std::vector<std::uint8_t>& select_yellow_probe_file() {
  return select_yellow_probe().file;
}

const machine::seam_definition& select_yellow_probe_definition() {
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(select_yellow_probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 3> points{
      {{.module = machine::resident_image,
        .offset = select_yellow_probe().bar_offset,
        .run = select_yellow_handler(0)},
       {.module = machine::resident_image,
        .offset = select_yellow_probe().list_offset,
        .run = select_yellow_handler(1)},
       {.module = machine::resident_image,
        .offset = select_yellow_probe().hp_offset,
        .run = select_yellow_handler(4)}}};
  static const machine::seam_definition definition{
      .id = "select-yellow-probe",
      .about = "the select-yellow handlers, at made-up addresses",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

}  // namespace amberfolio::programs
