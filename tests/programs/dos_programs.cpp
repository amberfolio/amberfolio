// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine programs for the DOS services Curse of the Azure Bonds'
// boot reaches (#553), in the listing-and-bytes convention of
// machine_programs.cpp. A file of their own because that one is at the
// content guard's size cap; their entries join its list through
// `append_dos_service_programs()`.

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "programs/assembler.h"
#include "programs/machine_harness.h"
#include "programs/machine_idioms.h"
#include "programs/machine_programs.h"

namespace amberfolio::programs {
namespace {

// --- The directory search: AH=1Ah, 4Eh, 4Fh -----------------------------
//
// What a roster screen does: point the DTA at a record of its own, find
// the first file matching a pattern, and find the next until DOS says
// there are none. Then the refusals — a directory that is not there, a
// pattern nothing matches — and a search below the root that asks for
// directories, whose first answer is `.` (dos.h).
//
//         mov  dx, offset dta / mov ah, 1Ah / int 21h
//         mov  dx, offset spec / xor cx, cx / mov ah, 4Eh / int 21h
//         <ok>, [dta+1Eh], [dta+1Ah]
//         mov  ah, 4Fh / int 21h  ; twice: the second file, then 12h
//         ... the refusals ...
//         mov  dx, offset subdir / mov ah, 39h / int 21h
//         mov  dx, offset dirspec / mov cx, 10h / mov ah, 4Eh / int 21h
//         <exit 5Bh>

/// The two files the search must find, in the order it must find them,
/// and one beside them it must not.
constexpr std::string_view search_first_path = "\\A.SAV";
constexpr std::string_view search_second_path = "\\B.SAV";
constexpr std::string_view search_other_path = "\\C.TXT";
constexpr std::uint16_t search_first_size = 3;
constexpr std::uint16_t search_second_size = 5;

///     xor  ax, ax                        ; 31 C0
///     mov  al, [label]                   ; A0 iw
void load_byte(assembler& a, std::string_view label) {
  a.db({0x31, 0xC0, 0xA0});
  a.dw_label(label);
}

///     mov  ax, [label]                   ; A1 iw
void load_word(assembler& a, std::string_view label) {
  a.db({0xA1});
  a.dw_label(label);
}

[[nodiscard]] std::vector<std::uint8_t> dossearch_code() {
  assembler a;

  mov_dx_offset(a, "dta");
  int21(a, 0x1A);

  mov_dx_offset(a, "spec");
  a.db({0x31, 0xC9});  // xor cx, cx — plain files only
  int21(a, 0x4E);
  succeeded(a, "first_ok");
  store(a, 0, reg_ax);
  load_byte(a, "dta_name");
  store(a, 1, reg_ax);
  load_word(a, "dta_size");
  store(a, 2, reg_ax);

  int21(a, 0x4F);
  succeeded(a, "next_ok");
  store(a, 3, reg_ax);
  load_byte(a, "dta_name");
  store(a, 4, reg_ax);
  load_word(a, "dta_size");
  store(a, 5, reg_ax);

  int21(a, 0x4F);
  error_or_zero(a, "ran_out");
  store(a, 6, reg_ax);

  mov_dx_offset(a, "nodir");
  a.db({0x31, 0xC9});
  int21(a, 0x4E);
  error_or_zero(a, "no_dir");
  store(a, 7, reg_ax);

  mov_dx_offset(a, "nomatch");
  a.db({0x31, 0xC9});
  int21(a, 0x4E);
  error_or_zero(a, "no_match");
  store(a, 8, reg_ax);

  mov_dx_offset(a, "subdir");
  int21(a, 0x39);
  mov_dx_offset(a, "dirspec");
  a.db({0xB9});
  a.dw(0x0010);  // mov cx, 10h — directories too
  int21(a, 0x4E);
  succeeded(a, "dot_ok");
  store(a, 9, reg_ax);
  load_byte(a, "dta_name");
  store(a, 10, reg_ax);
  load_byte(a, "dta_attribute");
  store(a, 11, reg_ax);

  exit_with(a, 0x5B);

  asciz(a, "spec", "\\*.SAV");
  asciz(a, "nodir", "\\NOPE\\*.*");
  asciz(a, "nomatch", "\\*.DAT");
  asciz(a, "subdir", "\\SUB");
  asciz(a, "dirspec", "\\SUB\\*.*");

  // The DTA: DOS's 21 bytes, then the answer's fields at their offsets.
  a.label("dta");
  for (int i = 0; i < 0x15; ++i) {
    a.db({0x00});
  }
  a.label("dta_attribute");
  for (int i = 0x15; i < 0x1A; ++i) {
    a.db({0x00});
  }
  a.label("dta_size");
  for (int i = 0x1A; i < 0x1E; ++i) {
    a.db({0x00});
  }
  a.label("dta_name");
  for (int i = 0x1E; i < 0x2B; ++i) {
    a.db({0x00});
  }

  return raw_program(a);
}

}  // namespace

void append_dos_service_programs(std::vector<machine_program>& list) {
  {
    machine_program p;
    p.name = "dossearch";
    p.about = "INT 21h set DTA, find first and find next over the VFS";
    p.setup.code = dossearch_code();
    // Staged out of order: the search answers in the VFS's name order,
    // not in the order the files were made.
    p.setup.files = {
        {.path = search_other_path, .contents = {'C'}},
        {.path = search_second_path,
         .contents = std::vector<std::uint8_t>(search_second_size, 'B')},
        {.path = search_first_path,
         .contents = std::vector<std::uint8_t>(search_first_size, 'A')}};
    p.setup.step_cap = 100'000;
    p.results = {
        {.what = "find first", .value = 1},
        {.what = "its name's first letter", .value = 'A'},
        {.what = "its size", .value = search_first_size},
        {.what = "find next", .value = 1},
        {.what = "its name's first letter", .value = 'B'},
        {.what = "its size", .value = search_second_size},
        {.what = "and then no more files", .value = 0x12},
        {.what = "a directory that is not there", .value = 0x03},
        {.what = "a pattern nothing matches", .value = 0x12},
        {.what = "a search below the root asking for directories", .value = 1},
        {.what = "whose first answer is '.'", .value = '.'},
        {.what = "a directory", .value = 0x10}};
    p.exit_code = 0x5B;
    p.file_trace = {"find \\", "find \\NOPE path_not_found",
                    "find \\ no_more_files", "mkdir \\SUB", "find \\SUB"};
    list.push_back(std::move(p));
  }
}

}  // namespace amberfolio::programs
