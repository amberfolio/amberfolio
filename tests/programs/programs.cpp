// SPDX-License-Identifier: AGPL-3.0-only
//
// Three programs, written in the assembly listing above each builder and
// hand-encoded beneath it. The listing is the source of truth for what the
// program means; the bytes are the source of truth for what it does. Both
// are here so a reader can check one against the other.

#include "programs/programs.h"

#include <ostream>

#include "programs/assembler.h"

namespace amberfolio::programs {
namespace {

// --- 1. A million increments -----------------------------------------
//
//         mov  dx, 16
// outer:  mov  cx, 62500
// inner:  inc  ax
//         loop inner
//         dec  dx
//         jnz  outer
//         hlt
//
// The narrowest thing worth measuring: nothing but the fetch-decode-
// dispatch path and a backward branch, with no memory traffic beyond the
// instruction stream. 16 * 62500 = 1,000,000 increments, split in two
// because CX counts to 65535 and a million does not fit.
//
// AX starts at zero and wraps in 16 bits, so it ends at
// 1,000,000 mod 65,536 = 16,960 — which is also a check that the wrap
// happens at all, fifteen times.

std::vector<std::uint8_t> increment_code() {
  assembler a;
  a.db({0xBA});
  a.dw(16);  // mov dx, 16
  a.label("outer");
  a.db({0xB9});
  a.dw(62500);  // mov cx, 62500
  a.label("inner");
  a.db({0x40});           // inc ax
  a.jump(0xE2, "inner");  // loop inner
  a.db({0x4A});           // dec dx
  a.jump(0x75, "outer");  // jnz outer
  a.db({0xF4});           // hlt
  return a.assemble();
}

// --- 2. A sieve of Eratosthenes to 100,000 ---------------------------
//
// One byte per odd number: index i stands for the number 2i+1, so 50,000
// bytes at DS:0000 cover 1 .. 99,999 and the whole array fits inside a
// single 64 KiB segment with no segment arithmetic anywhere. Zero means
// prime.
//
// Crossing out starts at p*p, whose index is ((2i+1)^2 - 1)/2 = 2i(i+1),
// and steps by p indices — consecutive odd multiples of p are 2p apart in
// value, which is p apart in this array. The outer loop is done once
// (2i+1)^2 > 100,000, i.e. after i = 157.
//
// CX ends with the count: every byte still zero, plus one for the prime 2.
// The answer is pi(100,000) = 9592, which is a fact about the integers
// rather than about this implementation — an emulator bug anywhere in
// MUL, the flags, the addressing or the branches moves it.
//
//         mov  byte [0], 1        ; 1 is not prime
//         mov  si, 1
// outer:  cmp  si, 157
//         ja   count
//         cmp  byte [si], 0
//         jne  next
//         mov  ax, si             ; p = 2i+1, kept in bx
//         add  ax, ax
//         inc  ax
//         mov  bx, ax
//         mov  ax, si             ; j = 2i(i+1), kept in di
//         inc  ax
//         mul  si
//         add  ax, ax
//         mov  di, ax
// cross:  cmp  di, 50000
//         jae  next
//         mov  byte [di], 1
//         add  di, bx
//         jmp  cross
// next:   inc  si
//         jmp  outer
// count:  xor  si, si
//         xor  cx, cx
// tally:  cmp  byte [si], 0
//         jne  skip
//         inc  cx
// skip:   inc  si
//         cmp  si, 50000
//         jb   tally
//         inc  cx                 ; the prime 2
//         hlt

std::vector<std::uint8_t> sieve_code() {
  assembler a;
  a.db({0xC6, 0x06});
  a.dw(0);
  a.db({0x01});  // mov byte [0], 1
  a.db({0xBE});
  a.dw(1);  // mov si, 1

  a.label("outer");
  a.db({0x81, 0xFE});
  a.dw(157);                 // cmp si, 157
  a.jump(0x77, "count");     // ja count
  a.db({0x80, 0x3C, 0x00});  // cmp byte [si], 0
  a.jump(0x75, "next");      // jne next

  a.db({0x89, 0xF0});  // mov ax, si
  a.db({0x01, 0xC0});  // add ax, ax
  a.db({0x40});        // inc ax
  a.db({0x89, 0xC3});  // mov bx, ax

  a.db({0x89, 0xF0});  // mov ax, si
  a.db({0x40});        // inc ax
  a.db({0xF7, 0xE6});  // mul si
  a.db({0x01, 0xC0});  // add ax, ax
  a.db({0x89, 0xC7});  // mov di, ax

  a.label("cross");
  a.db({0x81, 0xFF});
  a.dw(50000);               // cmp di, 50000
  a.jump(0x73, "next");      // jae next
  a.db({0xC6, 0x05, 0x01});  // mov byte [di], 1
  a.db({0x01, 0xDF});        // add di, bx
  a.jump(0xEB, "cross");     // jmp cross

  a.label("next");
  a.db({0x46});           // inc si
  a.jump(0xEB, "outer");  // jmp outer

  a.label("count");
  a.db({0x31, 0xF6});  // xor si, si
  a.db({0x31, 0xC9});  // xor cx, cx

  a.label("tally");
  a.db({0x80, 0x3C, 0x00});  // cmp byte [si], 0
  a.jump(0x75, "skip");      // jne skip
  a.db({0x41});              // inc cx

  a.label("skip");
  a.db({0x46});  // inc si
  a.db({0x81, 0xFE});
  a.dw(50000);            // cmp si, 50000
  a.jump(0x72, "tally");  // jb tally
  a.db({0x41});           // inc cx  (the prime 2)
  a.db({0xF4});           // hlt

  return a.assemble();
}

// --- 3. The string instructions --------------------------------------
//
// Two 32 KiB buffers filling the data segment — src at 0000, dst at 8000 —
// and every repeated string instruction the 8086 has, in both directions.
// M2's EGA path will lean on these harder than on anything else in the
// instruction set, and they are the only instructions whose *step* model
// is non-trivial: each iteration retires separately, so that a REP run
// stays interruptible (PLAN.md §3). A program is the only place that
// shows up.
//
// Five things are checked, and each sets its own bit in BP so that a
// failure says which one rather than how many:
//
//   0001  the copy is identical to its source     (repe cmpsb ran out)
//   0002  a single corrupted byte stops the compare where it should
//   0004  ... leaving CX counting the bytes it never reached
//   0008  repne scasb finds the sentinel at the offset it was put at
//   0010  a backwards rep movsw reproduces what the forwards one made
//
// AX ends holding all five: 001F.
//
//         cld
//         mov  ax, 0x1234         ; fill src with a two-byte pattern
//         mov  di, 0x0000
//         mov  cx, 16384
//         rep  stosw
//         mov  byte [0x7FF0], 0x99    ; a sentinel scasb will look for
//
//         mov  si, 0x0000         ; copy src -> dst, forwards
//         mov  di, 0x8000
//         mov  cx, 16384
//         rep  movsw
//
//         mov  si, 0x0000         ; and they must be identical
//         mov  di, 0x8000
//         mov  cx, 32768
//         repe cmpsb
//         jne  skip1
//         cmp  cx, 0
//         jne  skip1
//         or   bp, 0x0001
// skip1:  mov  byte [0x8064], 0x55    ; corrupt dst at byte 100
//         mov  si, 0x0000
//         mov  di, 0x8000
//         mov  cx, 32768
//         repe cmpsb              ; stops having compared byte 100
//         cmp  di, 0x8065
//         jne  skip2
//         or   bp, 0x0002
// skip2:  cmp  cx, 32667          ; 32768 - 101
//         jne  skip3
//         or   bp, 0x0004
// skip3:  mov  al, 0x99           ; find the sentinel in src
//         mov  di, 0x0000
//         mov  cx, 32768
//         repne scasb
//         cmp  di, 0x7FF1
//         jne  skip4
//         or   bp, 0x0008
// skip4:  std                     ; copy dst -> src, backwards
//         mov  si, 0xFFFE
//         mov  di, 0x7FFE
//         mov  cx, 16384
//         rep  movsw
//         cld
//         mov  si, 0x0000         ; which puts the corrupted byte in src
//         mov  di, 0x8000         ; too, so they match again
//         mov  cx, 32768
//         repe cmpsb
//         jne  skip5
//         cmp  cx, 0
//         jne  skip5
//         or   bp, 0x0010
// skip5:  mov  ax, bp
//         hlt

std::vector<std::uint8_t> string_code() {
  assembler a;
  a.db({0xFC});  // cld
  a.db({0xB8});
  a.dw(0x1234);  // mov ax, 0x1234
  a.db({0xBF});
  a.dw(0x0000);  // mov di, 0x0000
  a.db({0xB9});
  a.dw(16384);         // mov cx, 16384
  a.db({0xF3, 0xAB});  // rep stosw
  a.db({0xC6, 0x06});
  a.dw(0x7FF0);
  a.db({0x99});  // mov byte [0x7FF0], 0x99

  a.db({0xBE});
  a.dw(0x0000);  // mov si, 0x0000
  a.db({0xBF});
  a.dw(0x8000);  // mov di, 0x8000
  a.db({0xB9});
  a.dw(16384);         // mov cx, 16384
  a.db({0xF3, 0xA5});  // rep movsw

  a.db({0xBE});
  a.dw(0x0000);  // mov si, 0x0000
  a.db({0xBF});
  a.dw(0x8000);  // mov di, 0x8000
  a.db({0xB9});
  a.dw(32768);            // mov cx, 32768
  a.db({0xF3, 0xA6});     // repe cmpsb
  a.jump(0x75, "skip1");  // jne skip1
  a.db({0x81, 0xF9});
  a.dw(0);                // cmp cx, 0
  a.jump(0x75, "skip1");  // jne skip1
  a.db({0x81, 0xCD});
  a.dw(0x0001);  // or bp, 0x0001

  a.label("skip1");
  a.db({0xC6, 0x06});
  a.dw(0x8064);
  a.db({0x55});  // mov byte [0x8064], 0x55
  a.db({0xBE});
  a.dw(0x0000);  // mov si, 0x0000
  a.db({0xBF});
  a.dw(0x8000);  // mov di, 0x8000
  a.db({0xB9});
  a.dw(32768);         // mov cx, 32768
  a.db({0xF3, 0xA6});  // repe cmpsb
  a.db({0x81, 0xFF});
  a.dw(0x8065);           // cmp di, 0x8065
  a.jump(0x75, "skip2");  // jne skip2
  a.db({0x81, 0xCD});
  a.dw(0x0002);  // or bp, 0x0002

  a.label("skip2");
  a.db({0x81, 0xF9});
  a.dw(32667);            // cmp cx, 32667
  a.jump(0x75, "skip3");  // jne skip3
  a.db({0x81, 0xCD});
  a.dw(0x0004);  // or bp, 0x0004

  a.label("skip3");
  a.db({0xB0, 0x99});  // mov al, 0x99
  a.db({0xBF});
  a.dw(0x0000);  // mov di, 0x0000
  a.db({0xB9});
  a.dw(32768);         // mov cx, 32768
  a.db({0xF2, 0xAE});  // repne scasb
  a.db({0x81, 0xFF});
  a.dw(0x7FF1);           // cmp di, 0x7FF1
  a.jump(0x75, "skip4");  // jne skip4
  a.db({0x81, 0xCD});
  a.dw(0x0008);  // or bp, 0x0008

  a.label("skip4");
  a.db({0xFD});  // std
  a.db({0xBE});
  a.dw(0xFFFE);  // mov si, 0xFFFE
  a.db({0xBF});
  a.dw(0x7FFE);  // mov di, 0x7FFE
  a.db({0xB9});
  a.dw(16384);         // mov cx, 16384
  a.db({0xF3, 0xA5});  // rep movsw
  a.db({0xFC});        // cld

  a.db({0xBE});
  a.dw(0x0000);  // mov si, 0x0000
  a.db({0xBF});
  a.dw(0x8000);  // mov di, 0x8000
  a.db({0xB9});
  a.dw(32768);            // mov cx, 32768
  a.db({0xF3, 0xA6});     // repe cmpsb
  a.jump(0x75, "skip5");  // jne skip5
  a.db({0x81, 0xF9});
  a.dw(0);                // cmp cx, 0
  a.jump(0x75, "skip5");  // jne skip5
  a.db({0x81, 0xCD});
  a.dw(0x0010);  // or bp, 0x0010

  a.label("skip5");
  a.db({0x89, 0xE8});  // mov ax, bp
  a.db({0xF4});        // hlt

  return a.assemble();
}

}  // namespace

std::vector<program> all_programs() {
  return {
      {.name = "inc_loop",
       .about = "INC AX x 1,000,000, two nested counters",
       .code = increment_code(),
       .answer = cpu::reg16::ax,
       .expected = 16960,
       .steps = 2000050,
       .step_cap = 4000000},
      {.name = "sieve_100k",
       .about = "sieve of Eratosthenes to 100,000",
       .code = sieve_code(),
       .answer = cpu::reg16::cx,
       .expected = 9592,
       .steps = 619025,
       .step_cap = 2000000},
      {.name = "string_ops",
       .about = "MOVS/STOS/SCAS/CMPS over two 32 KiB buffers",
       .code = string_code(),
       .answer = cpu::reg16::ax,
       .expected = 0x001F,
       // 147,542 string iterations — 16384 stosw, 16384 + 16384 movsw,
       // 32768 + 101 + 32768 cmpsb, 32753 scasb — and 45 ordinary
       // instructions around them.
       .steps = 147587,
       .step_cap = 2000000},
  };
}

void PrintTo(const program& p, std::ostream* os) { *os << p.name; }

}  // namespace amberfolio::programs
