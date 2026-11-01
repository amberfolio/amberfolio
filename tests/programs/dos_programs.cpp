// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine programs for the DOS and BIOS services Curse of the Azure
// Bonds' boot reaches (#553), in the listing-and-bytes convention of
// machine_programs.cpp. A file of their own because that one is at the
// content guard's size cap; their entries join its list through
// `append_dos_service_programs()`.

#include <cstdint>
#include <string>
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

// --- The current drive: AH=19h -----------------------------------------
//
//         mov  ah, 19h / int 21h
//         xor  ah, ah                     ; AL alone is the answer
//         <exit 5Ch>

[[nodiscard]] std::vector<std::uint8_t> dosdrive_code() {
  assembler a;
  int21(a, 0x19);
  a.db({0x32, 0xE4});  // xor ah, ah
  store(a, 0, reg_ax);
  exit_with(a, 0x5C);
  return raw_program(a);
}

// --- Free space: AH=36h -------------------------------------------------
//
//         mov  dl, 3 / mov ah, 36h / int 21h   ; C
//         <store ax, bx, cx, dx>
//         mov  dl, 1 / mov ah, 36h / int 21h   ; A, which is not there
//         <store ax>
//         <exit 5Fh>

[[nodiscard]] std::vector<std::uint8_t> dosfree_code() {
  assembler a;
  a.db({0xB2, 0x03});  // mov dl, 3
  int21(a, 0x36);
  store(a, 0, reg_ax);
  store(a, 1, reg_bx);
  store(a, 2, reg_cx);
  store(a, 3, reg_dx);
  a.db({0xB2, 0x01});  // mov dl, 1
  int21(a, 0x36);
  store(a, 4, reg_ax);
  exit_with(a, 0x5F);
  return raw_program(a);
}

// --- The current directory: AH=47h --------------------------------------
//
// Run with `\GAMES\CURSE` current, as a launcher leaves it; ask for it on
// the current drive and on C by number, and on A, which is not there.
//
//         mov  si, offset buffer / xor dl, dl / mov ah, 47h / int 21h
//         mov  si, offset buffer2 / mov dl, 3 / mov ah, 47h / int 21h
//         mov  dl, 1 / mov ah, 47h / int 21h          ; 0Fh
//         <exit 5Dh>

/// What the program leaves current, and the answer it must get back.
constexpr std::string_view cwd_directory = "\\GAMES\\CURSE";
constexpr std::string_view cwd_answer = "GAMES\\CURSE";

///     mov  si, offset <label>            ; BE iw
void mov_si_offset(assembler& a, std::string_view label) {
  a.db({0xBE});
  a.dw_label(label);
}

/// The sum of the bytes at `label`, up to and including its terminator,
/// and the terminator's offset from `label`, in AX and CX:
///
///         mov  si, offset label / xor ax, ax / xor bx, bx / xor cx, cx
///  sum:   mov  bl, [si] / add ax, bx / inc si / or bl, bl / jz done
///         inc  cx / jmp sum
///  done:
void sum_asciz(assembler& a, std::string_view label, std::string_view tag) {
  const std::string top = std::string(tag) + "_sum";
  const std::string done = std::string(tag) + "_done";
  mov_si_offset(a, label);
  a.db({0x31, 0xC0, 0x31, 0xDB, 0x31, 0xC9});
  a.label(top);
  a.db({0x8A, 0x1C, 0x01, 0xD8, 0x46, 0x08, 0xDB});
  a.jump(0x74, done);
  a.db({0x41});
  a.jump(0xEB, top);
  a.label(done);
}

[[nodiscard]] std::uint16_t asciz_sum(std::string_view text) {
  std::uint16_t sum = 0;
  for (const char c : text) {
    sum = static_cast<std::uint16_t>(sum + static_cast<std::uint8_t>(c));
  }
  return sum;
}

[[nodiscard]] std::vector<std::uint8_t> doscwd_code() {
  assembler a;

  mov_si_offset(a, "buffer");
  a.db({0x30, 0xD2});  // xor dl, dl — the current drive
  int21(a, 0x47);
  answer_or_zero(a, "current_ok");
  store(a, 0, reg_ax);
  sum_asciz(a, "buffer", "current");
  store(a, 1, reg_ax);
  store(a, 2, reg_cx);

  mov_si_offset(a, "buffer2");
  a.db({0xB2, 0x03});  // mov dl, 3 — C, one-based
  int21(a, 0x47);
  answer_or_zero(a, "c_ok");
  store(a, 3, reg_ax);
  sum_asciz(a, "buffer2", "c");
  store(a, 4, reg_ax);

  a.db({0xB2, 0x01});  // mov dl, 1 — A
  int21(a, 0x47);
  error_or_zero(a, "a_refused");
  store(a, 5, reg_ax);

  exit_with(a, 0x5D);

  a.label("buffer");
  for (int i = 0; i < 64; ++i) {
    a.db({0xEE});  // Not zero, so a missing terminator shows.
  }
  a.label("buffer2");
  for (int i = 0; i < 64; ++i) {
    a.db({0xEE});
  }
  return raw_program(a);
}

// --- The text window cleared: INT 10h AH=06h ----------------------------
//
// The opening Curse's boot makes before it goes to graphics: 80x25 text,
// clear the whole screen, home the cursor, and read what is under it.
//
//         mov  ax, 0003h / int 10h
//         mov  ax, 0600h / mov bh, 07h / xor cx, cx / mov dx, 184Fh
//         int  10h
//         mov  ah, 02h / xor bh, bh / xor dx, dx / int 10h
//         mov  ah, 08h / int 10h                       ; FFFFh: open bus
//         <exit 5Eh>

[[nodiscard]] std::vector<std::uint8_t> textscroll_code() {
  assembler a;
  mov_ax(a, 0x0003);
  a.db({0xCD, 0x10});
  mov_ax(a, 0x0600);
  a.db({0xB7, 0x07});  // mov bh, 07h
  a.db({0x31, 0xC9});  // xor cx, cx
  a.db({0xBA});
  a.dw(0x184F);  // mov dx, 184Fh — row 24, column 79
  a.db({0xCD, 0x10});
  mov_ax(a, 1);  // Still running: the scroll was not refused.
  store(a, 0, reg_ax);
  a.db({0xB4, 0x02, 0x30, 0xFF, 0x31, 0xD2, 0xCD, 0x10});
  a.db({0xB4, 0x08, 0xCD, 0x10});
  store(a, 1, reg_ax);
  exit_with(a, 0x5E);
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

  {
    machine_program p;
    p.name = "dosdrive";
    p.about = "INT 21h AH=19h answers the one drive";
    p.setup.code = dosdrive_code();
    p.setup.step_cap = 10'000;
    p.results = {{.what = "the current drive, zero-based: C", .value = 2}};
    p.exit_code = 0x5C;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "dosfree";
    p.about = "INT 21h AH=36h states the drive and counts what is on it";
    p.setup.code = dosfree_code();
    p.setup.step_cap = 10'000;
    p.results = {
        {.what = "sectors per cluster", .value = 8},
        {.what = "free clusters: all, on a drive with no files", .value = 8192},
        {.what = "bytes per sector", .value = 512},
        {.what = "clusters in all", .value = 8192},
        {.what = "drive A: not there", .value = 0xFFFF},
    };
    p.exit_code = 0x5F;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "doscwd";
    p.about = "INT 21h AH=47h reads back the directory the host set";
    p.setup.code = doscwd_code();
    p.setup.current_directory = cwd_directory;
    p.setup.step_cap = 20'000;
    p.results = {
        {.what = "the current drive's directory: MS-DOS's AX", .value = 0x0100},
        {.what = "its bytes, summed", .value = asciz_sum(cwd_answer)},
        {.what = "its length: no drive, no leading backslash",
         .value = static_cast<std::uint16_t>(cwd_answer.size())},
        {.what = "C's, by number", .value = 0x0100},
        {.what = "the same bytes", .value = asciz_sum(cwd_answer)},
        {.what = "A's: a drive this machine does not have", .value = 0x0F}};
    p.exit_code = 0x5D;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "textscroll";
    p.about = "INT 10h AH=06h clears the 80x25 text page nothing displays";
    p.setup.code = textscroll_code();
    p.setup.step_cap = 50'000;
    p.results = {
        {.what = "the clear was carried out", .value = 1},
        {.what = "the cell under the cursor, off open bus", .value = 0xFFFF}};
    p.exit_code = 0x5E;
    // The text mode this machine records and cannot draw, and the page
    // nothing answers for, reported once, on the clear's first blank.
    p.notices = 2;
    list.push_back(std::move(p));
  }
}

}  // namespace amberfolio::programs
