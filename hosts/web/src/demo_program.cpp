// SPDX-License-Identifier: AGPL-3.0-only
//
// The demo program's assembly listing, and its hand encoding beneath it —
// the same discipline tests/programs/programs.cpp follows and the same
// tool (`programs::assembler`) it uses: opcodes are literal and checkable
// against the encoding tables byte for byte, only displacements are
// computed (assembler.h's own top comment). Built into the wasm module at
// compile time, from the same in-tree machinery M2-T1 (#56) will use for
// its own suite (see demo_program.h for exactly how #56's composite is
// meant to replace this).
//
// What it does, in order:
//
//   1. INT 10h AH=00h AL=0Dh — set mode 0Dh (320x200x16). This is the one
//      video mode the machine has (PLAN.md §9); after it the sequencer's
//      map mask is 0Fh and the graphics controller is in write mode 0
//      with set/reset disabled and the bit mask wide open (int10.h's own
//      top comment), so a plain byte write to A000:xxxx lands the same
//      byte on all four planes unmodified.
//   2. A tight loop over the whole 8000-byte visible window (320/8 = 40
//      bytes/row * 200 rows) writing an incrementing, wrapping byte
//      value. Because that byte reaches every plane identically (the
//      point above), each 8-pixel group's colour index is 0 wherever the
//      byte's bit is 0 and 15 wherever it is 1 — so the counting byte
//      turns into a deterministic dither of black (index 0) and white
//      (index 15) pixels, different every 8-pixel group, repeating every
//      256 bytes. It is not meant to look like anything; it is meant to
//      be a pattern a hash can pin (hosts/web/tests/smoke.mjs does) and
//      proof that a write pipeline this simple reaches every plane.
//   3. Program PIT channel 2 for a square wave at 1,193,182 / 2712 ≈
//      440 Hz (concert A) — mode 3 (square wave), both bytes of the
//      divisor — then set port 61h to 03h: bit 0 keeps the gate high (the
//      channel counts), bit 1 lets its OUT line reach the speaker
//      (speaker.h). The tone plays continuously from here on; there is
//      no timer interrupt in this program to time a duration against
//      (see below), and continuous is simplest for a page whose whole
//      point is "prove the tone crosses the boundary."
//   4. An echo loop: INT 16h AH=00h (the blocking read — an empty buffer
//      halts the virtual CPU until a host posts a key, keyboard.h's own
//      "AH=00h on an empty buffer" design) puts the keystroke's ASCII in
//      AL; DL is loaded from it and INT 21h AH=02h (console output)
//      writes it to the console byte stream (dos.h) — which is what
//      reaches the page's <pre> and CI's console assertion. Then it loops
//      forever, one character at a time.
//
// No IRQ0/INT 1Ch timer use, and so no 8259 programming: PLAN.md §3's
// timer path is real and available (core/src/abi.cpp's reference_devices
// attaches the PIT and the minimal 8259), but this program does not need
// it — the tone drives PIT channel 2 directly, and the echo loop's own
// blocking read is timer-free. A future composite program (#56) that
// wants the interrupt path is free to add it; this one stays the
// smallest thing that exercises video, sound and the keyboard.

#include "demo_program.h"

#include "programs/assembler.h"

namespace amberfolio::web {

std::vector<std::uint8_t> demo_program_bytes() {
  programs::assembler a;

  // --- 1. Set mode 0Dh --------------------------------------------------
  //
  //         mov  ax, 0x000D
  //         int  0x10
  a.db({0xB8});
  a.dw(0x000D);
  a.db({0xCD, 0x10});

  // --- 2. Draw a deterministic dither pattern ---------------------------
  //
  //         mov  ax, 0xA000     ; the EGA VRAM segment (ega.h)
  //         mov  es, ax
  //         xor  di, di
  //         xor  al, al
  // draw:   mov  [es:di], al
  //         inc  di
  //         inc  al
  //         cmp  di, 8000
  //         jb   draw
  a.db({0xB8});
  a.dw(0xA000);
  a.db({0x8E, 0xC0});  // mov es, ax
  a.db({0x31, 0xFF});  // xor di, di
  a.db({0x30, 0xC0});  // xor al, al

  a.label("draw");
  a.db({0x26, 0x88, 0x05});  // mov [es:di], al  (26 = ES: override)
  a.db({0x47});              // inc di
  a.db({0xFE, 0xC0});        // inc al
  a.db({0x81, 0xFF});
  a.dw(8000);            // cmp di, 8000
  a.jump(0x72, "draw");  // jb draw

  // --- 3. Program a ~440 Hz tone and enable the speaker ------------------
  //
  //         mov  al, 0xB6       ; channel 2, RW=both, mode 3, binary
  //         mov  dx, 0x43       ; PIT control word port (pit.h)
  //         out  dx, al
  //         mov  ax, 2712       ; 1,193,182 / 2712 ~= 440 Hz
  //         mov  dx, 0x42       ; PIT channel 2 data port
  //         out  dx, al         ; low byte
  //         mov  al, ah
  //         out  dx, al         ; high byte
  //         mov  al, 0x03       ; gate on (bit 0), speaker data on (bit 1)
  //         mov  dx, 0x61       ; port 61h (speaker.h)
  //         out  dx, al
  a.db({0xB0, 0xB6});  // mov al, 0xB6
  a.db({0xBA});
  a.dw(0x0043);  // mov dx, 0x43
  a.db({0xEE});  // out dx, al
  a.db({0xB8});
  a.dw(2712);  // mov ax, 2712
  a.db({0xBA});
  a.dw(0x0042);        // mov dx, 0x42
  a.db({0xEE});        // out dx, al (low byte)
  a.db({0x88, 0xE0});  // mov al, ah
  a.db({0xEE});        // out dx, al (high byte)
  a.db({0xB0, 0x03});  // mov al, 0x03
  a.db({0xBA});
  a.dw(0x0061);  // mov dx, 0x61
  a.db({0xEE});  // out dx, al

  // --- 4. Echo keys through the DOS console sink -------------------------
  //
  // echo:   mov  ah, 0x00       ; INT 16h AH=00h: blocking key read
  //         int  0x16
  //         mov  dl, al         ; AL holds the ASCII byte (keyboard.h)
  //         mov  ah, 0x02       ; INT 21h AH=02h: console output
  //         int  0x21
  //         jmp  echo
  a.label("echo");
  a.db({0xB4, 0x00});  // mov ah, 0x00
  a.db({0xCD, 0x16});  // int 0x16
  a.db({0x88, 0xC2});  // mov dl, al
  a.db({0xB4, 0x02});  // mov ah, 0x02
  a.db({0xCD, 0x21});  // int 0x21
  a.jump(0xEB, "echo");

  return a.assemble();
}

}  // namespace amberfolio::web
