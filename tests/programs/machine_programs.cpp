// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine programs, written in the assembly listing above each builder
// and hand-encoded beneath it — programs.cpp's own convention, one layer out.
// The listing is the source of truth for what the program means; the
// bytes are the source of truth for what it does. Both are here so a
// reader can check one against the other.
//
// The idioms every one of them is built out of are at the top, encoded
// once: a result-block store, the DOS exit, an indexed EGA register
// write, the stock PIC init sequence, the vector hook. Each carries its
// own listing, so a program below reads as the sequence of steps it is
// rather than as a wall of hex.

#include "programs/machine_programs.h"

#include <array>
#include <cstddef>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/font.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/sha256.h"
#include "programs/assembler.h"
#include "programs/exe.h"

namespace amberfolio::programs {
namespace {

using machine::ticks;

// --- The idioms ---------------------------------------------------------

/// Register numbers as the encoding numbers them, for the ModRM byte the
/// result store builds. Only the ones a program below actually answers
/// with are named.
constexpr unsigned reg_ax = 0;
constexpr unsigned reg_cx = 1;
constexpr unsigned reg_dx = 2;
constexpr unsigned reg_bx = 3;
constexpr unsigned reg_bp = 5;

/// The result block's word `index`, as a displacement.
[[nodiscard]] std::uint16_t result_word(std::size_t index) {
  return static_cast<std::uint16_t>(machine_layout::result_offset + 2 * index);
}

/// How every program answers:
///
///     mov  [result + 2*index], <reg>     ; 89 /r, mod=00 rm=110
void store(assembler& a, std::size_t index, unsigned reg) {
  a.db({0x89, static_cast<std::uint8_t>(0x06U | (reg << 3U))});
  a.dw(result_word(index));
}

///     mov  ax, imm16                     ; B8 iw
void mov_ax(assembler& a, std::uint16_t value) {
  a.db({0xB8});
  a.dw(value);
}

///     mov  dx, offset <label>            ; BA iw
void mov_dx_offset(assembler& a, std::string_view label) {
  a.db({0xBA});
  a.dw_label(label);
}

///     mov  ah, function                  ; B4 ib
///     int  21h                           ; CD 21
void int21(assembler& a, std::uint8_t function) {
  a.db({0xB4, function, 0xCD, 0x21});
}

/// The only way a machine program is allowed to end (machine_harness.h):
///
///     mov  ax, 4C00h + code              ; B8 iw
///     int  21h                           ; CD 21
void exit_with(assembler& a, std::uint8_t code) {
  mov_ax(a, static_cast<std::uint16_t>(0x4C00U | code));
  a.db({0xCD, 0x21});
}

///     mov  al, value                     ; B0 ib
///     out  port, al                      ; E6 ib
void out8(assembler& a, std::uint8_t port, std::uint8_t value) {
  a.db({0xB0, value, 0xE6, port});
}

/// An index/data register pair on a port an immediate cannot name:
///
///     mov  dx, index_port                ; BA iw
///     mov  al, index                     ; B0 ib
///     out  dx, al                        ; EE
///     inc  dx                            ; 42
///     mov  al, value                     ; B0 ib
///     out  dx, al                        ; EE
///
/// INC DX is the whole of the move from index port to data port, because
/// the EGA's sequencer (3C4h/3C5h) and graphics controller (3CEh/3CFh)
/// both put their data port one above their index one.
void out_indexed(assembler& a, std::uint16_t index_port, std::uint8_t index,
                 std::uint8_t value) {
  a.db({0xBA});
  a.dw(index_port);
  a.db({0xB0, index, 0xEE, 0x42, 0xB0, value, 0xEE});
}

/// The EGA's sequencer and graphics controller, by the names their
/// registers go by (ega.h).
void seq(assembler& a, std::uint8_t index, std::uint8_t value) {
  out_indexed(a, 0x3C4, index, value);
}

void gc(assembler& a, std::uint8_t index, std::uint8_t value) {
  out_indexed(a, 0x3CE, index, value);
}

/// Graphics-controller register numbers, so the pipeline below reads as
/// the stages ega.h names rather than as indices.
constexpr std::uint8_t gc_set_reset = 0x00;
constexpr std::uint8_t gc_enable_set_reset = 0x01;
constexpr std::uint8_t gc_color_compare = 0x02;
constexpr std::uint8_t gc_data_rotate = 0x03;
constexpr std::uint8_t gc_read_map = 0x04;
constexpr std::uint8_t gc_mode = 0x05;
constexpr std::uint8_t gc_color_dont_care = 0x07;
constexpr std::uint8_t gc_bit_mask = 0x08;

/// The sequencer's map mask.
constexpr std::uint8_t seq_map_mask = 0x02;

/// Port 61h, narrowed to the byte an OUT immediate can name.
constexpr auto speaker_control_port =
    static_cast<std::uint8_t>(machine::speaker_port);

/// 65,536 scheduling steps of doing nothing:
///
///     mov  cx, 0                         ; B9 00 00
///  d: loop d                             ; E2 FE
///
/// LOOP with CX=0 wraps to FFFFh and runs the full 65,536 iterations, one
/// scheduling step each (PLAN.md §3) — so at one tick a step this is
/// 65,536 ticks with the machine otherwise idle, three frame deadlines
/// and dozens of tone cycles long.
void delay(assembler& a, std::string_view tag) {
  a.db({0xB9});
  a.dw(0);
  a.label(tag);
  a.jump(0xE2, tag);
}

/// The stock single-controller ICW sequence (pic.h): edge triggered, no
/// slave, ICW4 to follow; vector base 8; 8086 mode. A real ROM BIOS's
/// POST would have done this; this machine has no POST, so the program
/// does it, which is the honest arrangement pic.h argues for.
///
///     mov al, 13h / out 20h, al
///     mov al, 08h / out 21h, al
///     mov al, 01h / out 21h, al
void init_pic(assembler& a) {
  constexpr auto command =
      static_cast<std::uint8_t>(machine::pic::master_command_port);
  constexpr auto data = static_cast<std::uint8_t>(machine::pic::data_port);
  out8(a, command, machine::pic::icw1_edge_single_icw4);
  out8(a, data, machine::pic::expected_vector_base);
  out8(a, data, machine::pic::icw4_8086_mode);
}

/// PIT channel 0 in mode 2 at `divisor`, which is the rate IRQ0 arrives
/// at in ticks — the unit the virtual clock counts in, so a tick count
/// times a divisor is an exactly computable span of virtual time.
///
///     mov al, 34h / out 43h, al          ; channel 0, LSB+MSB, mode 2
///     mov al, lo  / out 40h, al
///     mov al, hi  / out 40h, al
void program_timer(assembler& a, std::uint16_t divisor) {
  out8(a, 0x43, 0x34);
  out8(a, 0x40, static_cast<std::uint8_t>(divisor));
  out8(a, 0x40, static_cast<std::uint8_t>(divisor >> 8U));
}

/// PIT channel 2 in mode 3 at `divisor` — a square wave whose period is
/// `divisor` ticks (pit.h), which is what the speaker gates into the
/// audio timeline.
///
///     mov al, B6h / out 43h, al          ; channel 2, LSB+MSB, mode 3
///     mov al, lo  / out 42h, al
///     mov al, hi  / out 42h, al
void program_tone(assembler& a, std::uint16_t divisor) {
  out8(a, 0x43, 0xB6);
  out8(a, 0x42, static_cast<std::uint8_t>(divisor));
  out8(a, 0x42, static_cast<std::uint8_t>(divisor >> 8U));
}

/// Point the user tick vector at `handler` in this program's own segment,
/// the way a program of the era hooks a periodic routine — four bytes at
/// 0000:0070 and nothing else (service_floor.h).
///
///     xor  ax, ax                        ; 31 C0
///     mov  ds, ax                        ; 8E D8
///     mov  word [0070h], offset handler  ; C7 06 70 00 iw
///     mov  [0072h], cs                   ; 8C 0E 72 00
///     push cs                            ; 0E
///     pop  ds                            ; 1F
void hook_user_tick(assembler& a, std::string_view handler) {
  a.db({0x31, 0xC0, 0x8E, 0xD8});
  a.db({0xC7, 0x06});
  a.dw(static_cast<std::uint16_t>(4U * machine::service::user_tick_vector));
  a.dw_label(handler);
  a.db({0x8C, 0x0E});
  a.dw(
      static_cast<std::uint16_t>(4U * machine::service::user_tick_vector + 2U));
  a.db({0x0E, 0x1F});
}

/// The tick handler every program that counts ticks uses, placed after
/// the exit so nothing falls into it:
///
///  tick: inc bp                          ; 45
///        iret                            ; CF
void tick_handler(assembler& a, std::string_view label) {
  a.label(label);
  a.db({0x45, 0xCF});
}

/// Reduce "the call succeeded" to a word — DOS reports failure in CF
/// (service_floor.h), and an expectation is worth more as an equality
/// than as a range:
///
///     mov  ax, 1                         ; B8 01 00
///     jnc  done                          ; 73 rel8
///     xor  ax, ax                        ; 31 C0
///  done:
void succeeded(assembler& a, std::string_view tag) {
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
void answer_or_zero(assembler& a, std::string_view tag) {
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
void error_or_zero(assembler& a, std::string_view tag) {
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
void answered(assembler& a, std::uint16_t want, std::string_view tag) {
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
void bp_reached(assembler& a, std::uint8_t least, std::string_view tag) {
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
[[nodiscard]] std::vector<std::uint8_t> raw_program(const assembler& a) {
  std::vector<std::uint8_t> code = a.assemble();
  if (code.size() > machine_layout::result_offset) {
    throw std::logic_error("the program has grown into its own result block (" +
                           std::to_string(code.size()) + " bytes)");
  }
  return code;
}

/// Bytes of an ASCIZ string, as a program's own data.
void asciz(assembler& a, std::string_view label, std::string_view text) {
  a.label(label);
  for (const char c : text) {
    a.db({static_cast<std::uint8_t>(c)});
  }
  a.db({0x00});
}

// --- 1. The timer -------------------------------------------------------
//
// PIT -> PIC -> CPU -> BIOS, end to end, in one program: the PIT is
// programmed to a known divisor, the PIC is initialized the way a ROM
// BIOS's POST would have, INT 1Ch is hooked, and the program spins until
// its own handler has been entered ten times. Then it reads 40:6C, the
// tick count the BIOS's own INT 08h handler maintains, which is the other
// end of the same chain — the program's hook counts what it was called
// with, the BDA counts what the BIOS did before calling it, and the two
// have to agree.
//
//         <hook INT 1Ch at tick>
//         <init the PIC>
//         <program channel 0, mode 2, divisor 2000>
//         xor  bp, bp
//         sti
// wait:   cmp  bp, 10
//         jb   wait
//         cli
//         mov  ax, 0040h            ; the BIOS data area
//         mov  ds, ax
//         mov  ax, [006Ch]          ; the tick count, low half
//         mov  dx, [006Eh]          ; and high
//         push cs
//         pop  ds
//         mov  [result+0], bp
//         mov  [result+2], ax
//         mov  [result+4], dx
//         <exit 11h>
// tick:   inc  bp
//         iret

constexpr std::uint16_t timer_divisor = 2000;
constexpr std::uint8_t timer_wanted_ticks = 10;

[[nodiscard]] std::vector<std::uint8_t> timer_code() {
  assembler a;
  hook_user_tick(a, "tick");
  init_pic(a);
  program_timer(a, timer_divisor);

  a.db({0x31, 0xED});  // xor bp, bp
  a.db({0xFB});        // sti
  a.label("wait");
  a.db({0x83, 0xFD, timer_wanted_ticks});  // cmp bp, 10
  a.jump(0x72, "wait");                    // jb wait
  a.db({0xFA});                            // cli

  mov_ax(a, machine::bda::segment);
  a.db({0x8E, 0xD8});  // mov ds, ax
  a.db({0xA1});
  a.dw(machine::bda::timer_ticks);  // mov ax, [006Ch]
  a.db({0x8B, 0x16});
  a.dw(machine::bda::timer_ticks + 2);  // mov dx, [006Eh]
  a.db({0x0E, 0x1F});                   // push cs / pop ds

  store(a, 0, reg_bp);
  store(a, 1, reg_ax);
  store(a, 2, reg_dx);

  exit_with(a, 0x11);
  tick_handler(a, "tick");
  return raw_program(a);
}

// --- 2. The video -------------------------------------------------------
//
// Mode 0Dh through INT 10h, then every stage of the EGA's write pipeline
// and both of its read modes, each checked by reading the planes back
// through the device's own read path rather than by looking at them from
// outside. ega.h's top comment is the specification; this is it, executed.
//
// The five bytes at offsets 0-4 are the pipeline's own scratch and are
// asserted word by word; the two bands drawn afterwards are what makes
// the composed frame worth hashing.
//
//         mov  ax, 000Dh            ; AH=00h, AL=0Dh: 320x200x16
//         int  10h
//         mov  ax, A000h
//         mov  es, ax
//
//   (a)   map mask 0Fh, no set/reset, no rotate, replace, mask FFh
//         mov  al, AAh / mov es:[0], al        ; every plane takes AAh
//
//   (b)   map mask 05h
//         mov  al, 3Ch / mov es:[1], al        ; planes 0 and 2 only
//
//   (c)   enable set/reset 0Fh, set/reset 05h
//         mov  al, 00h / mov es:[2], al        ; the CPU byte is ignored
//
//   (d)   mov  al, es:[0]                      ; loads all four latches
//         data rotate 12h                      ; ror 2, function OR
//         mov  al, C3h / mov es:[0], al        ; ror(C3h,2)=F0h | AAh
//
//   (e)   mov  al, es:[0]                      ; latches = FAh
//         bit mask 0Fh
//         mov  al, 00h / mov es:[0], al        ; 00h&0Fh | FAh&F0h = F0h
//
//   (f)   mov  al, es:[1]                      ; latches = 3Ch,00,3Ch,00
//         write mode 1 / mov es:[3], al        ; the latches, verbatim
//
//   (g)   write mode 2, enable set/reset 0Fh
//         mov  al, 06h / mov es:[4], al        ; bit n expands to plane n
//
//   then every plane byte of interest read back through read mode 0, the
//   colour compare read through read mode 1, and two bands drawn with
//   REP STOSB under a map mask (see `band`, which is a function because
//   of the AL-clobber trap that ordering hides).

/// One plane byte, read the way a program reads one: name the plane, put
/// the controller in read mode 0, and load the byte.
///
///     <read map select = plane>
///     <mode = 00h>
///     mov  al, es:[offset]      ; 26 A0 iw
///     mov  ah, 0                ; B4 00
///     mov  [result + 2*index], ax
void read_plane(assembler& a, std::uint8_t plane, std::uint16_t offset,
                std::size_t index) {
  gc(a, gc_read_map, plane);
  gc(a, gc_mode, 0x00);
  a.db({0x26, 0xA0});
  a.dw(offset);
  a.db({0xB4, 0x00});
  store(a, index, reg_ax);
}

///     mov  al, value            ; B0 ib
///     mov  es:[offset], al      ; 26 A2 iw
void write_vram(assembler& a, std::uint16_t offset, std::uint8_t value) {
  a.db({0xB0, value, 0x26, 0xA2});
  a.dw(offset);
}

///     mov  al, es:[offset]      ; 26 A0 iw — and, on this device, a
///                               ; load of all four latches (ega.h)
void latch_from(assembler& a, std::uint16_t offset) {
  a.db({0x26, 0xA0});
  a.dw(offset);
}

/// A solid band of `rows` scanlines starting at `first_row`, on the
/// planes the map mask names — 40 bytes to a row, eight pixels to a byte.
///
///     <map mask = planes>
///     mov  al, FFh              ; B0 FF
///     mov  di, first_row * 40   ; BF iw
///     mov  cx, rows * 40        ; B9 iw
///     rep  stosb                ; F3 AA
///
/// The MOV AL is *after* the map mask write and not before it, and that
/// ordering is the whole reason this is a function. Programming an
/// indexed register pair means putting the index and then the value in
/// AL and OUTing each (`out_indexed`), so a map mask write leaves the
/// mask itself sitting in AL — and a STOSB trusting an AL loaded
/// beforehand fills the band with the map mask instead of with FFh. It
/// did, once, and the frame hash was perfectly happy to record it; what
/// caught it was a pixel count worked out by hand.
void band(assembler& a, std::uint8_t planes, unsigned first_row,
          unsigned rows) {
  constexpr unsigned bytes_per_row = 40;
  seq(a, seq_map_mask, planes);
  a.db({0xB0, 0xFF, 0xBF});
  a.dw(static_cast<std::uint16_t>(first_row * bytes_per_row));
  a.db({0xB9});
  a.dw(static_cast<std::uint16_t>(rows * bytes_per_row));
  a.db({0xF3, 0xAA});
}

[[nodiscard]] std::vector<std::uint8_t> video_code() {
  assembler a;

  mov_ax(a, 0x000D);
  a.db({0xCD, 0x10});  // int 10h
  mov_ax(a, 0xA000);
  a.db({0x8E, 0xC0});  // mov es, ax

  // (a) The pipeline at rest: every plane takes the CPU byte.
  seq(a, seq_map_mask, 0x0F);
  gc(a, gc_enable_set_reset, 0x00);
  gc(a, gc_data_rotate, 0x00);
  gc(a, gc_mode, 0x00);
  gc(a, gc_bit_mask, 0xFF);
  write_vram(a, 0, 0xAA);

  // (b) The map mask gates the plane, not the bit.
  seq(a, seq_map_mask, 0x05);
  write_vram(a, 1, 0x3C);
  seq(a, seq_map_mask, 0x0F);

  // (c) Set/reset substitutes an expanded bit for the CPU byte.
  gc(a, gc_enable_set_reset, 0x0F);
  gc(a, gc_set_reset, 0x05);
  write_vram(a, 2, 0x00);
  gc(a, gc_enable_set_reset, 0x00);

  // (d) Rotate, then the ALU against the latches.
  latch_from(a, 0);
  gc(a, gc_data_rotate, 0x12);
  write_vram(a, 0, 0xC3);
  gc(a, gc_data_rotate, 0x00);

  // (e) The bit mask selects between the ALU's answer and the latch.
  latch_from(a, 0);
  gc(a, gc_bit_mask, 0x0F);
  write_vram(a, 0, 0x00);
  gc(a, gc_bit_mask, 0xFF);

  // (f) Write mode 1: the latches, verbatim, on every unmasked plane.
  latch_from(a, 1);
  gc(a, gc_mode, 0x01);
  write_vram(a, 3, 0x00);
  gc(a, gc_mode, 0x00);

  // (g) Write mode 2: bit n of the CPU byte expands to plane n, gated by
  // Enable Set/Reset exactly as mode 0's substitution is (ega.h).
  gc(a, gc_enable_set_reset, 0x0F);
  gc(a, gc_mode, 0x02);
  write_vram(a, 4, 0x06);
  gc(a, gc_mode, 0x00);
  gc(a, gc_enable_set_reset, 0x00);

  read_plane(a, 0, 0, 0);
  read_plane(a, 1, 0, 1);
  read_plane(a, 0, 1, 2);
  read_plane(a, 1, 1, 3);
  read_plane(a, 0, 2, 4);
  read_plane(a, 1, 2, 5);
  read_plane(a, 2, 2, 6);
  read_plane(a, 0, 3, 7);
  read_plane(a, 1, 3, 8);
  read_plane(a, 0, 4, 9);
  read_plane(a, 1, 4, 10);
  read_plane(a, 2, 4, 11);

  // Read mode 1: an eight-bit colour-compare answer. Offset 2 holds
  // FFh/00h/FFh/00h across the planes, so a compare of 0101b with every
  // plane considered matches in every bit position.
  gc(a, gc_color_compare, 0x05);
  gc(a, gc_color_dont_care, 0x0F);
  gc(a, gc_mode, 0x08);
  a.db({0x26, 0xA0});
  a.dw(2);
  a.db({0xB4, 0x00});
  store(a, 12, reg_ax);

  // The same bytes against a compare no bit can match.
  gc(a, gc_color_compare, 0x0F);
  a.db({0x26, 0xA0});
  a.dw(2);
  a.db({0xB4, 0x00});
  store(a, 13, reg_ax);

  // And a partial match: offset 1 is 3Ch on planes 0 and 2 and zero on
  // the others, which is exactly the 0101b pattern in the bits 3Ch sets.
  gc(a, gc_color_compare, 0x05);
  a.db({0x26, 0xA0});
  a.dw(1);
  a.db({0xB4, 0x00});
  store(a, 14, reg_ax);
  gc(a, gc_mode, 0x00);
  gc(a, gc_color_compare, 0x00);
  gc(a, gc_color_dont_care, 0x00);

  // Two bands, well clear of the five bytes just asserted: colour 9 for
  // sixteen rows, colour 6 for eight more. 40 bytes to a row, one bit per
  // pixel per plane.
  //
  //         cld
  //         <map mask 09h>
  //         mov  di, 320              ; row 8
  //         mov  al, FFh
  //         mov  cx, 640              ; 16 rows
  //         rep  stosb
  //         <map mask 06h>
  //         mov  di, 1280             ; row 32
  //         mov  cx, 320              ; 8 rows
  //         rep  stosb
  a.db({0xFC});  // cld
  band(a, 0x09, 8, 16);
  band(a, 0x06, 32, 8);
  seq(a, seq_map_mask, 0x0F);

  // The palette, through the BIOS: AH=10h AL=00h sets register BL to
  // colour BH, and AL=01h sets the overscan. Both are part of what the
  // renderer composes, so both are in the frame hash.
  mov_ax(a, 0x1000);
  a.db({0xBB});
  a.dw(0x2A01);  // mov bx, 2A01h: register 1, colour 2Ah
  a.db({0xCD, 0x10});
  mov_ax(a, 0x1001);
  a.db({0xBB});
  a.dw(0x1500);  // mov bx, 1500h: overscan 15h
  a.db({0xCD, 0x10});

  // Long enough for a frame deadline to fall after the last write, which
  // is what makes the composed frame the one being asserted.
  delay(a, "settle");

  exit_with(a, 0x22);
  return raw_program(a);
}

// --- 3. The sound -------------------------------------------------------
//
// Two programmed tones with the gate opened and closed around each, and
// silence between them. The speaker's cone is bit 1 of port 61h ANDed
// with channel 2's output line (speaker.h), so this is both halves of
// that formula exercised in sequence, and the periods that come back out
// of the pulled sample stream are the divisors that went in.
//
// The divisors are multiples of 41, which is exactly how many ticks one
// sample covers at the rate the harness pulls at (machine_harness.h) —
// so every rising edge lands on a sample boundary and the measured period
// is an equality rather than a tolerance band.
//
//         <program channel 2, mode 3, divisor 1230>
//         mov  al, 03h / out 61h, al     ; gate and data enable
//         in   al, 61h                   ; reads back what was written
//         mov  ah, 0 / mov [result+0], ax
//         <delay>
//         mov  al, 00h / out 61h, al     ; silence
//         <delay>
//         <program channel 2, mode 3, divisor 2460>
//         mov  al, 03h / out 61h, al
//         <delay>
//         mov  al, 00h / out 61h, al
//         in   al, 61h
//         mov  ah, 0 / mov [result+2], ax
//         mov  cx, 4000 / loop $         ; let the last silence settle
//         mov  ax, 1230 / mov [result+4], ax
//         mov  ax, 2460 / mov [result+6], ax
//         <exit 33h>

constexpr std::uint16_t first_tone_divisor = 41 * 30;   // 1230
constexpr std::uint16_t second_tone_divisor = 41 * 60;  // 2460

[[nodiscard]] std::vector<std::uint8_t> sound_code() {
  assembler a;

  program_tone(a, first_tone_divisor);
  out8(a, speaker_control_port, 0x03);
  a.db({0xE4, speaker_control_port, 0xB4, 0x00});  // in al, 61h / mov ah, 0
  store(a, 0, reg_ax);
  delay(a, "tone1");

  out8(a, speaker_control_port, 0x00);
  delay(a, "quiet");

  program_tone(a, second_tone_divisor);
  out8(a, speaker_control_port, 0x03);
  delay(a, "tone2");

  out8(a, speaker_control_port, 0x00);
  a.db({0xE4, speaker_control_port, 0xB4, 0x00});
  store(a, 1, reg_ax);

  // A short tail so the horizon settles past the last edge before the
  // machine stops.
  a.db({0xB9});
  a.dw(4000);
  a.label("tail");
  a.jump(0xE2, "tail");

  mov_ax(a, first_tone_divisor);
  store(a, 2, reg_ax);
  mov_ax(a, second_tone_divisor);
  store(a, 3, reg_ax);

  exit_with(a, 0x33);
  return raw_program(a);
}

// --- 4. The keyboard ----------------------------------------------------
//
// Both INT 16h paths, and the difference between them. The first key is
// read by polling — AH=01h until ZF says something is waiting — which is
// only a real poll because the host posts the key thousands of ticks into
// the run, so the loop genuinely spins. The second is read by blocking —
// AH=00h on an empty buffer, which halts the machine without stalling its
// clock (keyboard.h) — and the timer is running by then, so the tick
// count proves the rest of the machine kept going while the program
// waited.
//
// Each key is echoed to the console through DOS AH=02h, which is what
// makes the console byte stream an assertion in its own right.
//
//         xor  bx, bx
// poll:   mov  ah, 01h / int 16h
//         jnz  got
//         inc  bx
//         jmp  poll
// got:    mov  ah, 00h / int 16h
//         mov  [result+0], ax
//         mov  dl, al / mov ah, 02h / int 21h
//         <bx != 0 ? 1 : 0> -> [result+2]
//         <hook INT 1Ch at tick>
//         <init the PIC>
//         <program channel 0, mode 2, divisor 2000>
//         xor  bp, bp
//         sti
//         mov  ah, 00h / int 16h        ; blocks until the host types
//         mov  [result+4], ax
//         mov  dl, al / mov ah, 02h / int 21h
//         mov  ah, 02h / int 16h        ; the shift flags at 40:17
//         xor  ah, ah / mov [result+6], ax
//         cli
//         <bp >= 5 ? 1 : 0> -> [result+8]
//         <exit 44h>
// tick:   inc bp / iret

[[nodiscard]] std::vector<std::uint8_t> keyboard_code() {
  assembler a;

  a.db({0x31, 0xDB});  // xor bx, bx
  a.label("poll");
  a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h
  a.jump(0x75, "got");             // jnz got
  a.db({0x43});                    // inc bx
  a.jump(0xEB, "poll");            // jmp poll
  a.label("got");
  a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h
  store(a, 0, reg_ax);
  a.db({0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21});  // mov dl,al / mov ah,2 / int 21h

  mov_ax(a, 1);
  a.db({0x83, 0xFB, 0x00});  // cmp bx, 0
  a.jump(0x75, "spun");      // jne spun
  a.db({0x31, 0xC0});        // xor ax, ax
  a.label("spun");
  store(a, 1, reg_ax);

  hook_user_tick(a, "tick");
  init_pic(a);
  program_timer(a, timer_divisor);
  a.db({0x31, 0xED});  // xor bp, bp
  a.db({0xFB});        // sti

  a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h — blocking
  store(a, 2, reg_ax);
  a.db({0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21});

  a.db({0xB4, 0x02, 0xCD, 0x16});  // mov ah, 02h / int 16h
  a.db({0x30, 0xE4});              // xor ah, ah
  store(a, 3, reg_ax);

  a.db({0xFA});  // cli
  bp_reached(a, 5, "ticked");
  store(a, 4, reg_ax);

  exit_with(a, 0x44);
  tick_handler(a, "tick");
  return raw_program(a);
}

// --- 5. The DOS files ---------------------------------------------------
//
// A whole file round-trip and every error path beside it, over the memory
// filesystem: make a directory, create a file in it, write, seek, write
// again over what was there, close, reopen read-only, seek to the end to
// learn its size, seek back, read it all, checksum it, and close. Then
// the failures — a file that is not there, a handle that is not open, a
// seek origin DOS does not have, a delete of nothing — and finally a
// second file created only to be deleted, so the filesystem afterwards
// has one file present and one absent.
//
// The handle numbers are asserted, not incidental: DOS boots with 0-4
// open, so the first create is handle 5, and code of the era relies on
// it (dos.h).
//
//         mov  dx, offset dirpath  / mov ah, 39h / int 21h   ; mkdir
//         mov  dx, offset filepath / mov ah, 3Ch / xor cx, cx / int 21h
//         mov  bx, ax
//         mov  dx, offset wbuf / mov cx, 16 / mov ah, 40h / int 21h
//         mov  ax, 4200h / xor cx, cx / mov dx, 4 / int 21h  ; seek
//         mov  dx, offset patch / mov cx, 2 / mov ah, 40h / int 21h
//         mov  ah, 3Eh / int 21h                             ; close
//         mov  dx, offset filepath / mov ax, 3D00h / int 21h ; reopen
//         mov  bx, ax
//         mov  ax, 4202h / xor cx, cx / xor dx, dx / int 21h ; size
//         mov  ax, 4200h / xor cx, cx / xor dx, dx / int 21h ; rewind
//         mov  dx, offset rbuf / mov cx, 16 / mov ah, 3Fh / int 21h
//         <checksum rbuf>
//         mov  dx, offset rbuf / mov cx, 16 / mov ah, 3Fh / int 21h  ; EOF
//         mov  ah, 3Eh / int 21h
//         ... the error paths ...
//         mov  dx, offset banner / mov ah, 09h / int 21h
//         <exit 55h>

/// The sixteen bytes the file is written with, and the two that overwrite
/// its fifth and sixth. Ours, like everything else here.
constexpr std::uint8_t file_first_byte = 0x41;
constexpr std::uint16_t file_length = 16;
constexpr std::uint8_t patch_byte = 0x2D;
constexpr std::uint16_t patch_at = 4;
constexpr std::uint16_t patch_length = 2;

[[nodiscard]] std::vector<std::uint8_t> file_contents() {
  std::vector<std::uint8_t> bytes(file_length, 0);
  for (std::uint16_t i = 0; i < file_length; ++i) {
    bytes[i] = static_cast<std::uint8_t>(file_first_byte + i);
  }
  for (std::uint16_t i = 0; i < patch_length; ++i) {
    bytes[patch_at + i] = patch_byte;
  }
  return bytes;
}

[[nodiscard]] std::uint16_t file_checksum() {
  std::uint16_t sum = 0;
  for (const std::uint8_t byte : file_contents()) {
    sum = static_cast<std::uint16_t>(sum + byte);
  }
  return sum;
}

[[nodiscard]] std::vector<std::uint8_t> dosfiles_code() {
  assembler a;

  mov_dx_offset(a, "dirpath");
  int21(a, 0x39);
  succeeded(a, "mkdir_ok");
  store(a, 0, reg_ax);

  mov_dx_offset(a, "filepath");
  a.db({0x31, 0xC9});  // xor cx, cx — no attributes
  int21(a, 0x3C);
  answer_or_zero(a, "created");
  store(a, 1, reg_ax);
  a.db({0x89, 0xC3});  // mov bx, ax

  mov_dx_offset(a, "wbuf");
  a.db({0xB9});
  a.dw(file_length);
  int21(a, 0x40);
  answer_or_zero(a, "wrote");
  store(a, 2, reg_ax);

  mov_ax(a, 0x4200);   // AH=42h, AL=00h: from the start
  a.db({0x31, 0xC9});  // xor cx, cx
  a.db({0xBA});
  a.dw(patch_at);
  a.db({0xCD, 0x21});
  answer_or_zero(a, "sought");
  store(a, 3, reg_ax);

  mov_dx_offset(a, "patch");
  a.db({0xB9});
  a.dw(patch_length);
  int21(a, 0x40);
  answer_or_zero(a, "patched");
  store(a, 4, reg_ax);

  int21(a, 0x3E);
  succeeded(a, "closed1");
  store(a, 5, reg_ax);

  mov_dx_offset(a, "filepath");
  mov_ax(a, 0x3D00);  // AH=3Dh, AL=00h: read only
  a.db({0xCD, 0x21});
  answer_or_zero(a, "reopened");
  store(a, 6, reg_ax);
  a.db({0x89, 0xC3});  // mov bx, ax

  mov_ax(a, 0x4202);  // seek from the end: the file's length
  a.db({0x31, 0xC9, 0x31, 0xD2});
  a.db({0xCD, 0x21});
  answer_or_zero(a, "sized");
  store(a, 7, reg_ax);

  mov_ax(a, 0x4200);  // and back to the start, whose answer is zero
  a.db({0x31, 0xC9, 0x31, 0xD2});
  a.db({0xCD, 0x21});
  answered(a, 0, "rewound");
  store(a, 8, reg_ax);

  mov_dx_offset(a, "rbuf");
  a.db({0xB9});
  a.dw(file_length);
  int21(a, 0x3F);
  answer_or_zero(a, "read");
  store(a, 9, reg_ax);

  // The checksum of what came back, computed in the program so that the
  // sixteen bytes are checked here as well as on the filesystem.
  //
  //         mov  si, offset rbuf
  //         mov  cx, 16
  //         xor  ax, ax
  //         xor  bx, bx
  //  sum:   mov  bl, [si]
  //         add  ax, bx
  //         inc  si
  //         loop sum
  a.db({0xBE});
  a.dw_label("rbuf");
  a.db({0xB9});
  a.dw(file_length);
  a.db({0x31, 0xC0, 0x31, 0xDB});
  a.label("sum");
  a.db({0x8A, 0x1C, 0x01, 0xD8, 0x46});
  a.jump(0xE2, "sum");
  store(a, 10, reg_ax);

  // At the end of the file a read succeeds and answers nothing, which is
  // not the same thing as failing.
  a.db({0xBB});
  a.dw(5);  // mov bx, 5 — the handle the checksum loop clobbered
  mov_dx_offset(a, "rbuf");
  a.db({0xB9});
  a.dw(file_length);
  int21(a, 0x3F);
  answered(a, 0, "at_eof");
  store(a, 11, reg_ax);

  int21(a, 0x3E);
  succeeded(a, "closed2");
  store(a, 12, reg_ax);

  // --- The error paths ---
  mov_dx_offset(a, "nopath");
  mov_ax(a, 0x3D00);
  a.db({0xCD, 0x21});
  error_or_zero(a, "no_such_file");
  store(a, 13, reg_ax);

  a.db({0xBB});
  a.dw(9);  // a handle nothing opened
  mov_dx_offset(a, "rbuf");
  a.db({0xB9});
  a.dw(1);
  int21(a, 0x3F);
  error_or_zero(a, "no_such_handle");
  store(a, 14, reg_ax);

  a.db({0xBB});
  a.dw(1);            // a handle that is open, so the origin is the fault
  mov_ax(a, 0x4203);  // AL=03h: not one of DOS's three origins
  a.db({0x31, 0xC9, 0x31, 0xD2});
  a.db({0xCD, 0x21});
  error_or_zero(a, "no_such_origin");
  store(a, 15, reg_ax);

  mov_dx_offset(a, "nopath");
  int21(a, 0x41);
  error_or_zero(a, "nothing_to_delete");
  store(a, 16, reg_ax);

  // A name that does not resolve at all: there is one drive and it is C
  // (vfs.h), so this fails inside `canonicalize()` before the filesystem
  // is consulted. Here because that is the naming failure the file
  // channel used to report nowhere (#121) — a program asking for a file
  // on the floppy it was installed from, and a log with nothing in it.
  mov_dx_offset(a, "otherdrive");
  mov_ax(a, 0x3D00);
  a.db({0xCD, 0x21});
  error_or_zero(a, "no_such_drive");
  store(a, 17, reg_ax);

  // --- Created, written, deleted ---
  mov_dx_offset(a, "scratchpath");
  a.db({0x31, 0xC9});
  int21(a, 0x3C);
  answer_or_zero(a, "scratch_made");
  store(a, 18, reg_ax);
  a.db({0x89, 0xC3});

  mov_dx_offset(a, "patch");
  a.db({0xB9});
  a.dw(patch_length);
  int21(a, 0x40);
  answer_or_zero(a, "scratch_written");
  store(a, 19, reg_ax);

  int21(a, 0x3E);
  succeeded(a, "scratch_closed");
  store(a, 20, reg_ax);

  mov_dx_offset(a, "scratchpath");
  int21(a, 0x41);
  succeeded(a, "scratch_deleted");
  store(a, 21, reg_ax);

  mov_dx_offset(a, "scratchpath");
  mov_ax(a, 0x3D00);
  a.db({0xCD, 0x21});
  error_or_zero(a, "scratch_gone");
  store(a, 22, reg_ax);

  mov_dx_offset(a, "banner");
  int21(a, 0x09);

  exit_with(a, 0x55);

  // --- The program's own data ---
  asciz(a, "dirpath", "\\DATA");
  asciz(a, "filepath", "\\DATA\\NOTE.TXT");
  asciz(a, "nopath", "\\NOPE.TXT");
  asciz(a, "otherdrive", "A:\\NOPE.TXT");
  asciz(a, "scratchpath", "\\SCRATCH.TMP");

  a.label("wbuf");
  for (std::uint16_t i = 0; i < file_length; ++i) {
    a.db({static_cast<std::uint8_t>(file_first_byte + i)});
  }
  a.label("patch");
  a.db({patch_byte, patch_byte});
  a.label("rbuf");
  for (std::uint16_t i = 0; i < file_length; ++i) {
    a.db({0x00});
  }
  // AH=09h prints until the '$', which is not printed.
  a.label("banner");
  for (const char c : std::string_view("FILES OK$")) {
    a.db({static_cast<std::uint8_t>(c)});
  }

  return raw_program(a);
}

// --- 6. The loaded EXE --------------------------------------------------
//
// An MZ binary with two real relocations, loaded off the filesystem by
// the loader the way DOS loads one, computing an answer that spans
// segments and exiting with a code the harness checks.
//
// Both relocations are the kind a linker emits and neither is decorative:
// one is a segment immediate (`MOV AX, dataSeg`), the other is a segment
// word sitting in the data area that the program loads and uses as a
// pointer. Neither can produce the right answer unless the loader added
// the load segment to the file's own numbering.
//
// The image is one code segment at paragraph 0 and one data segment at
// paragraph 40h, with the result block at offset 800h in the code segment
// — where the harness reads it from, having been told the load segment by
// the loader.
//
//         mov  ax, ds               ; the PSP segment DOS entered us with
//         mov  bx, cs
//         sub  bx, ax               ; = 10h paragraphs, DOS's own offset
//         push cs / pop ds
//         push cs / pop es
//         mov  [result+0], bx
//         mov  ax, dataSeg          ; RELOCATED
//         mov  es, ax
//         mov  ax, es
//         mov  bx, cs
//         sub  ax, bx               ; = 40h, whatever we were loaded at
//         mov  [result+2], ax
//         mov  ax, [dataword]       ; RELOCATED, a far pointer in data
//         mov  es, ax
//         mov  al, es:[0]           ; the seed byte at dataSeg:0000
//         mov  ah, 0
//         mov  [result+4], ax
//         mov  ax, [0002h]          ; the PSP's top-of-memory word
//         ...
//         <exit 66h>

constexpr std::uint16_t exe_data_paragraph = 0x0040;
constexpr std::uint16_t exe_data_offset = exe_data_paragraph * 16;
constexpr std::uint8_t exe_seed = 0x5A;

/// Where the program's own data lives inside the image, and what is in
/// it: the seed byte the far pointer reaches, then the segment word the
/// loader relocates.
constexpr std::uint16_t exe_dataword_offset = exe_data_offset + 4;

[[nodiscard]] std::vector<std::uint8_t> loaded_exe_file() {
  assembler a;

  a.db({0x8C, 0xD8});  // mov ax, ds — the PSP segment
  a.db({0x8C, 0xCB});  // mov bx, cs
  a.db({0x29, 0xC3});  // sub bx, ax
  a.db({0x0E, 0x1F});  // push cs / pop ds
  a.db({0x0E, 0x07});  // push cs / pop es
  store(a, 0, reg_bx);

  a.db({0xB8});
  a.label("dataseg_immediate");
  a.dw(exe_data_paragraph);  // mov ax, dataSeg — relocated
  a.db({0x8E, 0xC0});        // mov es, ax
  a.db({0x8C, 0xC0});        // mov ax, es
  a.db({0x8C, 0xCB});        // mov bx, cs
  a.db({0x29, 0xD8});        // sub ax, bx
  store(a, 1, reg_ax);

  a.db({0xA1});
  a.dw(exe_dataword_offset);  // mov ax, [dataword] — a relocated pointer
  a.db({0x8E, 0xC0});         // mov es, ax
  a.db({0x26, 0xA0});
  a.dw(0);             // mov al, es:[0000] — the seed, through it
  a.db({0xB4, 0x00});  // mov ah, 0
  store(a, 2, reg_ax);

  // The PSP the loader wrote: its top-of-memory word, which is what a
  // program of the era sizes its own heap against.
  a.db({0x8C, 0xC8});  // mov ax, cs
  a.db({0xBB});
  a.dw(0x0010);        // mov bx, 10h
  a.db({0x29, 0xD8});  // sub ax, bx — the PSP segment
  a.db({0x8E, 0xC0});  // mov es, ax
  a.db({0x26, 0xA1});
  a.dw(machine::psp::top_of_memory_offset);
  store(a, 3, reg_ax);

  exit_with(a, 0x66);

  a.pad_to(exe_data_offset);
  a.db({exe_seed});
  a.db({0x00, 0x00, 0x00});  // padding up to the pointer word
  a.dw(exe_data_paragraph);  // the relocated far pointer
  a.pad_to(machine_layout::result_offset + 0x20);

  return build_exe(
      {.initial_cs = 0,
       .initial_ip = 0,
       .initial_ss = 0,
       .initial_sp = 0x0F00,
       .min_alloc = 0x0100,
       .relocations = {{.offset = static_cast<std::uint16_t>(
                            a.offset_of("dataseg_immediate")),
                        .segment = 0},
                       {.offset = exe_dataword_offset, .segment = 0}},
       .image = a.assemble()});
}

// --- 7. The composite ---------------------------------------------------
//
// Every part of the machine at once, in one loaded EXE: the loader and
// its relocations, the video BIOS and the EGA's write pipeline, the
// speaker gating a programmed tone, the PIT and the PIC delivering the
// timer interrupt into a hooked INT 1Ch, the keyboard's blocking read
// woken by a host keystroke, DOS file I/O and DOS console output — and a
// checked exit code at the end of it.
//
// This is the machine's sieve-of-Eratosthenes moment, and it is what
// M2-H2's dev page (#55) embeds, which is why it is one self-contained
// EXE with one entry point rather than a script the harness drives.
//
// The order is chosen so the parts overlap rather than merely follow one
// another: the tone is playing while the timer interrupt is being counted
// (so the audio being asserted was produced by a machine that was
// servicing interrupts at the time), and the keyboard's blocking wait
// happens after the timer is running (so the wait is spent halted with
// the clock still moving, which is the whole of keyboard.h's design).
//
//         <PSP and relocation checks, as in the loaded EXE above>
//         <mode 0Dh, a pattern, a band, two plane read-backs>
//         <program channel 2 at 1230 and open the gate>
//         <hook INT 1Ch, init the PIC, program channel 0 at 2000, STI>
// wait:   cmp  bp, 12 / jb wait
//         <close the gate>
//         mov  [result+..], bp and the BDA tick count
//         mov  ah, 00h / int 16h        ; blocks until the host types
//         mov  dl, al / mov ah, 02h / int 21h
//         <create \RUN.LOG, write, close>
//         mov  dx, offset banner / mov ah, 09h / int 21h
//         <exit 5Ah>
// tick:   inc bp / iret

constexpr std::uint8_t composite_wanted_ticks = 12;
constexpr std::uint16_t composite_data_paragraph = 0x0040;
constexpr std::uint16_t composite_data_offset = composite_data_paragraph * 16;

[[nodiscard]] std::vector<std::uint8_t> composite_file() {
  assembler a;

  // --- The loader ---
  a.db({0x8C, 0xD8});  // mov ax, ds — the PSP segment
  a.db({0x8C, 0xCB});  // mov bx, cs
  a.db({0x29, 0xC3});  // sub bx, ax
  a.db({0x0E, 0x1F});  // push cs / pop ds
  store(a, 0, reg_bx);

  a.db({0xB8});
  a.label("dataseg_immediate");
  a.dw(composite_data_paragraph);  // mov ax, dataSeg — relocated
  a.db({0x8E, 0xC0});              // mov es, ax
  a.db({0x8C, 0xC0});              // mov ax, es
  a.db({0x8C, 0xCB});              // mov bx, cs
  a.db({0x29, 0xD8});              // sub ax, bx
  store(a, 1, reg_ax);

  // --- The video ---
  mov_ax(a, 0x000D);
  a.db({0xCD, 0x10});
  mov_ax(a, 0xA000);
  a.db({0x8E, 0xC0});  // mov es, ax

  seq(a, seq_map_mask, 0x0F);
  gc(a, gc_enable_set_reset, 0x00);
  gc(a, gc_data_rotate, 0x00);
  gc(a, gc_mode, 0x00);
  gc(a, gc_bit_mask, 0xFF);
  write_vram(a, 0, 0xAA);

  a.db({0xFC});  // cld
  band(a, 0x03, 40, 20);
  seq(a, seq_map_mask, 0x0F);

  read_plane(a, 0, 0, 2);
  read_plane(a, 1, 1600, 3);

  // --- The sound: the tone starts here and plays through the wait ---
  program_tone(a, first_tone_divisor);
  out8(a, speaker_control_port, 0x03);

  // --- The timer ---
  hook_user_tick(a, "tick");
  init_pic(a);
  program_timer(a, timer_divisor);
  a.db({0x31, 0xED});  // xor bp, bp
  a.db({0xFB});        // sti
  a.label("wait");
  a.db({0x83, 0xFD, composite_wanted_ticks});
  a.jump(0x72, "wait");

  // CLI first: BP and 40:6C are read as a pair and have to agree, and a
  // thirteenth tick landing between the two reads would make them
  // disagree for a reason that is not a bug. The timer program does the
  // same, for the same reason.
  a.db({0xFA});                         // cli
  out8(a, speaker_control_port, 0x00);  // and the tone stops
  store(a, 4, reg_bp);

  mov_ax(a, machine::bda::segment);
  a.db({0x8E, 0xC0});  // mov es, ax
  a.db({0x26, 0xA1});
  a.dw(machine::bda::timer_ticks);  // mov ax, es:[006Ch]
  store(a, 5, reg_ax);

  // --- The keyboard: blocked, with the machine still keeping time ---
  //
  // No STI needed and none given: AH=00h on an empty buffer sets IF on
  // the live register file itself before halting, precisely so the wait
  // is not deaf to the timer tick that proves it is a wait and not a
  // stall (keyboard.h).
  a.db({0xB4, 0x00, 0xCD, 0x16});
  store(a, 6, reg_ax);
  a.db({0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21});  // echo it to the console
  a.db({0xFA});                                // cli

  // --- The files ---
  mov_dx_offset(a, "logpath");
  a.db({0x31, 0xC9});
  int21(a, 0x3C);
  answer_or_zero(a, "log_made");
  store(a, 7, reg_ax);
  a.db({0x89, 0xC3});  // mov bx, ax

  mov_dx_offset(a, "logbytes");
  a.db({0xB9});
  a.dw(4);
  int21(a, 0x40);
  answer_or_zero(a, "log_written");
  store(a, 8, reg_ax);

  int21(a, 0x3E);
  succeeded(a, "log_closed");
  store(a, 9, reg_ax);

  // --- The console ---
  mov_dx_offset(a, "banner");
  int21(a, 0x09);

  exit_with(a, 0x5A);
  tick_handler(a, "tick");

  a.pad_to(composite_data_offset);
  asciz(a, "logpath", "\\RUN.LOG");
  a.label("logbytes");
  a.db({'M', '2', 'O', 'K'});
  a.label("banner");
  for (const char c : std::string_view("DONE$")) {
    a.db({static_cast<std::uint8_t>(c)});
  }
  a.pad_to(machine_layout::result_offset + 0x20);

  return build_exe({.initial_cs = 0,
                    .initial_ip = 0,
                    .initial_ss = 0,
                    .initial_sp = 0x0F00,
                    .min_alloc = 0x0100,
                    .relocations = {{.offset = static_cast<std::uint16_t>(
                                         a.offset_of("dataseg_immediate")),
                                     .segment = 0}},
                    .image = a.assemble()});
}

// --- The list -----------------------------------------------------------

/// Every scripted keystroke lands well past the point the program that
/// waits for it has reached, which is what makes a poll a real poll and a
/// blocking read a real block rather than a lucky one.
constexpr ticks first_key_at = 5'000;
constexpr ticks shift_key_at = 40'000;
constexpr ticks second_key_at = 40'100;
constexpr ticks composite_key_at = 60'000;

constexpr std::uint8_t scancode_a = 0x1E;
constexpr std::uint8_t scancode_d = 0x20;
constexpr std::uint8_t scancode_left_shift = 0x2A;

/// AX as INT 16h answers it: the scan code above, the character below.
constexpr std::uint16_t key_a = 0x1E61;
constexpr std::uint16_t key_shifted_d = 0x2044;

/// The composed frame each drawing program leaves behind, hashed over
/// its pixels and its palette (machine_harness.h).
///
/// A golden, and honest about being one: the number was read off a run,
/// not derived. It is asserted only underneath the pixel, area and
/// palette probes in `build_all()`, which *are* derived — by hand, from
/// the write pipeline in ega.h, the plane-to-bit assignment in
/// renderer.h and the DAC scheme both of them share.
///
/// That ordering is not ceremony. The first value written here recorded
/// a picture with a real bug in it (`band`, above), and the hash was
/// perfectly happy: it would have made the bug permanent and blamed
/// whoever later fixed it. What caught it was a pixel count somebody
/// could work out on paper. The hash's own job is the 63,900 pixels no
/// probe names.
/// Pixels in one scanline, and in the whole frame, as counts — what an
/// area probe below is written out of. Named as `std::size_t` rather
/// than multiplied out from `frame_width` on the spot so the arithmetic
/// happens in the type the count is, which is also what keeps
/// clang-tidy's implicit-widening check quiet without a cast in every
/// entry.
constexpr std::size_t pixels_per_row = machine::frame_width;
constexpr std::size_t pixels_per_frame = machine::frame_pixels;

constexpr std::uint64_t video_frame_hash = 0x5A76F1F971C42CA2ULL;
constexpr std::uint64_t composite_frame_hash = 0x280E6B18E8FA79B6ULL;

/// Every program's entry, built by assignment rather than as one
/// designated-initializer aggregate.
///
/// `-Wextra` on a current Clang wants every field of an aggregate named
/// once any of them is — precisely, every field that carries no default
/// member initializer, which is most of `machine_program`'s — and
/// spelling `.exe = {}` on the five programs that are not EXEs, and
/// `.tone_periods = {}` on the five that are silent, would be noise
/// written for a diagnostic rather than for a reader. What a program
/// leaves unset is empty, and empty is asserted (machine_programs.h), so
/// nothing is being waived here.
// --- 8. The synthetic boot ----------------------------------------------
//
// M3-T1 (#91). CI can never run the game; it can run a program shaped
// like one. This is that program: it unpacks itself, loads a module off
// the filesystem and far-calls into it, and asks the machine for every
// service M3's first boot turned out to need — so the surface #85-#90
// added has a test that drives it end to end, on all four targets, and
// not only a unit test each. It keeps growing for the same reason: what
// a later milestone adds because the game asked for it gets its call
// here in the same change (#121), or this program stops being the record
// of what the machine answers.
//
// Nothing here resembles anything. The packed payload is bytes this file
// computes from bytes this file wrote (`rle_xor_pack()`), and the *shape*
// being imitated — a stub that decompresses the rest of itself and jumps
// into it — is a fact about how DOS-era programs were built, not a fact
// about any one of them (PLAN.md §6).
//
//
// The image, offset by offset
// ---------------------------
//
//   0000  the stub: what DOS entered us with, then the unpacker, then a
//         jump into what it unpacked
//   0080  an INT 60h handler, left in the clear because the hook has to
//         point at something the unpacker did not have to produce
//   0100  data: the overlay's filename, and the far pointer the payload
//         calls it through — whose segment half is a real MZ relocation
//   0140  the packed payload
//   0500  where the payload lands, and where it runs
//   0680  where the overlay module is read to
//   0800  the result block (machine_layout)
//
// The payload carries no relocations, which is not an oversight: the
// loader fixes up the *file*, and by the time the payload exists those
// bytes have been through the unpacker. That is exactly why real
// self-unpacking binaries keep their fixups in the stub, and why the far
// call into the overlay goes through a pointer in the clear rather than
// through an immediate inside the payload.

constexpr std::uint16_t boot_handler_offset = 0x0080;
constexpr std::uint16_t boot_data_offset = 0x0100;
constexpr std::uint16_t boot_packed_offset = 0x0140;
constexpr std::uint16_t boot_payload_offset = 0x0500;
constexpr std::uint16_t boot_overlay_offset = 0x0680;

/// The room the payload and its packed form each have. Checked at build
/// time rather than trusted: a payload that outgrew its landing zone
/// would overwrite the place the overlay is about to be read to, and the
/// failure would look like anything but that.
constexpr std::size_t boot_payload_capacity =
    boot_overlay_offset - boot_payload_offset;
constexpr std::size_t boot_packed_capacity =
    boot_payload_offset - boot_packed_offset;

/// The key the packer XORs every emitted byte with, so the packed region
/// in the file looks like nothing in particular.
constexpr std::uint8_t boot_pack_key = 0x5C;

/// Where the overlay's filename sits, and what it says.
constexpr std::uint16_t boot_name_offset = boot_data_offset;
constexpr std::string_view boot_overlay_path = "\\OVL.BIN";

/// The far pointer the payload calls the overlay through: the offset,
/// then the segment word the loader relocates.
constexpr std::uint16_t boot_thunk_offset = boot_data_offset + 0x10;

/// The font this program installs at INT 1Fh, and where it keeps it —
/// in the clear beside the overlay's filename, for the same reason.
///
/// Eight bytes of this file's own invention: a box, whose scan lines all
/// differ, so a row drawn in the wrong place shows up as a wrong answer
/// rather than as a different-looking wrong picture. It is a font in the
/// only sense that matters here — the bytes INT 10h AH=09h reads through
/// the vector the program set (int10.h's "Where the glyphs come from") —
/// and it resembles nothing, which is the rule (PLAN.md §6).
constexpr std::uint16_t boot_font_offset = boot_data_offset + 0x20;
constexpr std::array<std::uint8_t, 8> boot_font_glyph{0xFF, 0x81, 0xBD, 0xA5,
                                                      0xA5, 0xBD, 0x81, 0xFF};

/// The vector the program hooks. 60h is in the range DOS never used and
/// every era program's own interrupts lived in.
constexpr std::uint8_t boot_hook_vector = 0x60;

/// What the hooked handler and the overlay each leave behind: values
/// that cannot appear unless that code actually ran.
constexpr std::uint16_t boot_handler_mark = 0xC0DE;
constexpr std::uint16_t boot_overlay_mark = 0xBEEF;

/// The command tail the harness gives it (#89), and therefore what DOS
/// leaves at PSP:80h for it to read back.
constexpr std::string_view boot_command_tail = " -FIRSTLIGHT";

/// Result-block indices, named: twenty-odd `store(a, 14, ...)` calls are
/// unreadable, and one off-by-one among them would be invisible.
enum boot_result : std::uint8_t {
  boot_entry_flags,
  boot_tail_length,
  boot_tail_first,
  boot_vector_segment_before,
  boot_vector_offset_before,
  boot_vector_segment_after,
  boot_vector_offset_after,
  boot_handler_ran,
  boot_ioctl_console,
  boot_ioctl_sink,
  boot_mode_at_power_on,
  boot_mode_after_text,
  boot_character_under_cursor,
  boot_font_points,
  boot_font_rows,
  boot_font_segment,
  boot_font_pointer,
  boot_mode_after_graphics,
  boot_active_page,
  boot_cursor_position,
  boot_glyph_first_line,
  boot_glyph_second_line,
  boot_retrace_seen,
  boot_overlay_bytes,
  boot_overlay_ran,
  boot_timer_ticked,
  boot_result_count,
};

/// Run-length encode `plain`, XORing every emitted byte with `key`.
///
/// Two bytes per run — a count and the byte — and a zero count ends the
/// stream, which is why a run never reaches 256. The smallest scheme
/// that is genuinely a decompressor rather than a copy, so the stub has
/// something to do that a `rep movsb` would not.
[[nodiscard]] std::vector<std::uint8_t> rle_xor_pack(
    std::span<const std::uint8_t> plain, std::uint8_t key) {
  std::vector<std::uint8_t> packed;
  std::size_t at = 0;
  while (at < plain.size()) {
    std::size_t run = 1;
    while (at + run < plain.size() && plain[at + run] == plain[at] &&
           run < 255) {
      ++run;
    }
    packed.push_back(static_cast<std::uint8_t>(run));
    packed.push_back(static_cast<std::uint8_t>(plain[at] ^ key));
    at += run;
  }
  packed.push_back(0);
  return packed;
}

///     mov  bx, imm16                     ; BB iw
void mov_bx(assembler& a, std::uint16_t value) {
  a.db({0xBB});
  a.dw(value);
}

///     int  n                             ; CD ib
void interrupt(assembler& a, std::uint8_t vector) { a.db({0xCD, vector}); }

/// The payload: everything the program does once it has unpacked itself.
///
/// Assembled on its own and then packed, so nothing in it may name a
/// label the outer image defines — it runs at `boot_payload_offset` and
/// this assembler counts from zero. Every address it names is one of the
/// constants above. That is a discipline rather than a limitation: a
/// real unpacked payload has the same problem and solves it the same way.
[[nodiscard]] std::vector<std::uint8_t> boot_payload() {
  assembler a;

  // --- The vectors (#86) ------------------------------------------------
  //
  // Read 60h before touching it: nothing backs it, so this machine
  // answers with its own stub, which is a real address a program is
  // entitled to read (service_floor.h).
  mov_ax(a, 0x3500U | boot_hook_vector);
  interrupt(a, 0x21);
  a.db({0x8C, 0xC0});  // mov ax, es
  store(a, boot_vector_segment_before, reg_ax);
  store(a, boot_vector_offset_before, reg_bx);

  // Hook it. DS:DX is our own handler, in the clear at a fixed offset
  // because the unpacker could not have produced it.
  a.db({0xBA});
  a.dw(boot_handler_offset);  // mov dx, handler
  mov_ax(a, 0x2500U | boot_hook_vector);
  interrupt(a, 0x21);

  // And read it back, which is the whole claim: what AH=25h wrote is what
  // AH=35h finds, because both of them are the vector table itself.
  mov_ax(a, 0x3500U | boot_hook_vector);
  interrupt(a, 0x21);
  a.db({0x8C, 0xC0});  // mov ax, es
  store(a, boot_vector_segment_after, reg_ax);
  store(a, boot_vector_offset_after, reg_bx);

  // Then take the interrupt. A hook that cannot be called is not a hook.
  interrupt(a, boot_hook_vector);

  // --- What is behind a handle (#86) ------------------------------------
  mov_ax(a, 0x4400);
  mov_bx(a, 1);  // STDOUT: the console sink.
  interrupt(a, 0x21);
  store(a, boot_ioctl_console, reg_dx);

  mov_ax(a, 0x4400);
  mov_bx(a, 0);  // STDIN: the documented sink, which is DOS's own NUL.
  interrupt(a, 0x21);
  store(a, boot_ioctl_sink, reg_dx);

  // --- The video BIOS (#87) ---------------------------------------------
  //
  // The mode before anything has set one: the self test left the one mode
  // this machine can display recorded in the BDA.
  mov_ax(a, 0x0F00);
  interrupt(a, 0x10);
  store(a, boot_mode_at_power_on, reg_ax);

  // Through 80x25 text, the way a program of the era opens. Nothing is
  // programmed and nothing will be drawn; the mode number is recorded and
  // the machine says so once.
  mov_ax(a, 0x0003);
  interrupt(a, 0x10);
  mov_ax(a, 0x0F00);
  interrupt(a, 0x10);
  store(a, boot_mode_after_text, reg_ax);

  // The character under the cursor, out of a text page nothing in this
  // machine answers for: open bus floats high, and that is the answer.
  mov_ax(a, 0x0800);
  mov_bx(a, 0x0000);
  interrupt(a, 0x10);
  store(a, boot_character_under_cursor, reg_ax);

  // Where the character generator is: vector 1Fh, which nothing has set,
  // so the one power-on installed — the top half of this machine's own
  // font, in its own BIOS region (font.h) — plus the cell height and the
  // row count the mode set recorded.
  mov_ax(a, 0x1130);
  mov_bx(a, 0x0000);
  interrupt(a, 0x10);
  store(a, boot_font_points, reg_cx);
  a.db({0xB6, 0x00});  // mov dh, 0
  store(a, boot_font_rows, reg_dx);
  a.db({0x8C, 0xC0});  // mov ax, es
  store(a, boot_font_segment, reg_ax);
  store(a, boot_font_pointer, reg_bp);

  // And into the mode this machine actually draws.
  mov_ax(a, 0x000D);
  interrupt(a, 0x10);
  mov_ax(a, 0x0500);  // page 0, the only one there is
  interrupt(a, 0x10);
  mov_ax(a, 0x0F00);
  interrupt(a, 0x10);
  store(a, boot_mode_after_graphics, reg_ax);
  a.db({0x88, 0xF8});  // mov al, bh
  a.db({0xB4, 0x00});  // mov ah, 0
  store(a, boot_active_page, reg_ax);

  // The cursor (#121). Moved through the BIOS, then read back out of the
  // BDA directly — which is the claim: AH=02h writes the same 40:50 a
  // program reads for itself and AH=08h above indexed the text page
  // with, not a copy of its own.
  mov_ax(a, 0x0200);
  mov_bx(a, 0x0000);  // BH=00h: the one page.
  a.db({0xBA});
  a.dw(0x0C05);  // mov dx, 0C05h — row 12, column 5
  interrupt(a, 0x10);
  a.db({0x1E});  // push ds
  mov_ax(a, machine::bda::segment);
  a.db({0x8E, 0xD8});  // mov ds, ax
  a.db({0xA1});
  a.dw(machine::bda::cursor_position);  // mov ax, [0050h]
  a.db({0x1F});                         // pop ds
  store(a, boot_cursor_position, reg_ax);

  // And a character at it (#121), out of a font this program supplies —
  // which is the only kind there is here, because a font is picture data
  // and this machine ships none (int10.h). Installed through AH=25h, the
  // same way the INT 60h hook above went in, then drawn at the top left
  // and read straight back off plane 0.
  a.db({0xBA});
  a.dw(boot_font_offset);  // mov dx, font
  mov_ax(a, 0x251F);       // AH=25h, AL=1Fh
  interrupt(a, 0x21);

  mov_ax(a, 0x0200);
  mov_bx(a, 0x0000);
  a.db({0x31, 0xD2});  // xor dx, dx — row 0, column 0
  interrupt(a, 0x10);

  mov_ax(a, 0x0980);  // AH=09h, AL=80h: the first glyph of the table
  mov_bx(a, 0x000F);  // page 0, colour 15 — every plane
  a.db({0xB9});
  a.dw(0x0001);  // mov cx, 1
  interrupt(a, 0x10);

  mov_ax(a, 0xA000);
  a.db({0x8E, 0xC0});  // mov es, ax
  a.db({0x26, 0xA0});
  a.dw(0x0000);        // mov al, es:[0] — plane 0, the cell's first line
  a.db({0xB4, 0x00});  // mov ah, 0
  store(a, boot_glyph_first_line, reg_ax);
  a.db({0x26, 0xA0});
  a.dw(0x0028);  // mov al, es:[40] — one scan line down
  a.db({0xB4, 0x00});
  store(a, boot_glyph_second_line, reg_ax);

  // --- The raster (#88) -------------------------------------------------
  //
  // Wait for retrace to end, then for the next one to begin — the poll
  // that spins forever against a constant status register. Reaching the
  // store below *is* the assertion: a machine that answered 3DAh with a
  // constant would run out of step cap here instead.
  a.db({0xBA});
  a.dw(0x03DA);  // mov dx, 3DAh
  a.label("retrace_out");
  a.db({0xEC});        // in al, dx
  a.db({0xA8, 0x08});  // test al, 08h
  a.jump(0x75, "retrace_out");
  a.label("retrace_in");
  a.db({0xEC});
  a.db({0xA8, 0x08});
  a.jump(0x74, "retrace_in");
  mov_ax(a, 1);
  store(a, boot_retrace_seen, reg_ax);

  // --- The overlay ------------------------------------------------------
  //
  // Open it, read it into memory, close it, and far-call what arrived —
  // through the relocated pointer in the clear, which is how an overlay
  // manager reaches a module it has just loaded.
  a.db({0xBA});
  a.dw(boot_name_offset);  // mov dx, filename
  mov_ax(a, 0x3D00);       // AH=3Dh AL=00h: open for reading
  interrupt(a, 0x21);
  a.db({0x89, 0xC3});  // mov bx, ax — the handle
  a.db({0xB4, 0x3F});  // mov ah, 3Fh
  a.db({0xB9});
  a.dw(0x0040);  // mov cx, 40h — more than the module is
  a.db({0xBA});
  a.dw(boot_overlay_offset);
  interrupt(a, 0x21);
  store(a, boot_overlay_bytes, reg_ax);
  a.db({0xB4, 0x3E});  // mov ah, 3Eh: close
  interrupt(a, 0x21);

  a.db({0xFF, 0x1E});
  a.dw(boot_thunk_offset);  // call far [thunk]

  // --- The tick nobody asked for (#87's self test) ----------------------
  //
  // 40:6C, read directly the way an era program reads it, waited on until
  // it moves. This program programs neither the timer nor the controller.
  // If the machine's own self test did not either, this loop never ends.
  mov_ax(a, 0x0040);
  a.db({0x8E, 0xC0});  // mov es, ax
  a.db({0xBF});
  a.dw(0x006C);  // mov di, 6Ch
  a.label("wait_tick");
  a.db({0x26, 0x8B, 0x05});  // mov ax, es:[di]
  a.db({0x09, 0xC0});        // or ax, ax
  a.jump(0x74, "wait_tick");
  mov_ax(a, 1);
  store(a, boot_timer_ticked, reg_ax);

  exit_with(a, 0x77);
  return a.assemble();
}

/// The overlay module: a second self-written program, staged on the
/// filesystem, read into memory by the payload and entered with a far
/// call. It leaves a mark and returns.
[[nodiscard]] std::vector<std::uint8_t> boot_overlay_module() {
  assembler a;
  mov_ax(a, boot_overlay_mark);
  store(a, boot_overlay_ran, reg_ax);
  a.db({0xCB});  // retf
  return a.assemble();
}

[[nodiscard]] std::vector<std::uint8_t> boot_exe_file() {
  const std::vector<std::uint8_t> payload = boot_payload();
  if (payload.size() > boot_payload_capacity) {
    throw std::logic_error("the synthetic boot payload outgrew its landing");
  }
  const std::vector<std::uint8_t> packed = rle_xor_pack(payload, boot_pack_key);
  if (packed.size() > boot_packed_capacity) {
    throw std::logic_error("the packed synthetic boot payload does not fit");
  }

  assembler a;

  // --- The stub ---------------------------------------------------------
  //
  // What DOS entered us with, before anything else touches it: the flag
  // word — IF has to be set (#89) — and the command tail DOS laid at
  // PSP:80h, which DS still points at.
  a.db({0x9C});  // pushf
  a.db({0x58});  // pop ax
  a.db({0x25});
  a.dw(0x0200);        // and ax, 0200h — the interrupt-enable bit alone
  a.db({0x8C, 0xDB});  // mov bx, ds — the PSP segment
  a.db({0x8E, 0xC3});  // mov es, bx
  a.db({0x0E, 0x1F});  // push cs / pop ds — our stores go to our segment
  store(a, boot_entry_flags, reg_ax);

  a.db({0x26, 0xA0});
  a.dw(machine::psp::command_tail_count_offset);  // mov al, es:[80h]
  a.db({0xB4, 0x00});                             // mov ah, 0
  store(a, boot_tail_length, reg_ax);
  a.db({0x26, 0xA0});
  a.dw(machine::psp::command_tail_bytes_offset);  // mov al, es:[81h]
  a.db({0xB4, 0x00});
  store(a, boot_tail_first, reg_ax);

  // --- The unpacker -----------------------------------------------------
  //
  //     cld
  //     push cs / pop es
  //     mov  si, packed
  //     mov  di, payload
  //   next:
  //     lodsb                 ; the run length, or zero at the end
  //     or   al, al
  //     jz   done
  //     mov  cl, al
  //     xor  ch, ch
  //     lodsb                 ; the byte, still masked
  //     xor  al, key
  //     rep  stosb
  //     jmp  next
  //   done:
  //     jmp  payload
  a.db({0xFC});        // cld
  a.db({0x0E, 0x07});  // push cs / pop es
  a.db({0xBE});
  a.dw(boot_packed_offset);
  a.db({0xBF});
  a.dw(boot_payload_offset);
  a.label("unpack_next");
  a.db({0xAC});        // lodsb
  a.db({0x08, 0xC0});  // or al, al
  a.jump(0x74, "unpack_done");
  a.db({0x88, 0xC1});  // mov cl, al
  a.db({0x30, 0xED});  // xor ch, ch
  a.db({0xAC});        // lodsb
  a.db({0x34, boot_pack_key});
  a.db({0xF3, 0xAA});  // rep stosb
  a.jump(0xEB, "unpack_next");
  a.label("unpack_done");
  a.near_jump(0xE9, "payload_lands");

  // --- The INT 60h handler, in the clear --------------------------------
  a.pad_to(boot_handler_offset);
  a.db({0x1E, 0x50, 0x53});  // push ds / push ax / push bx
  a.db({0x8C, 0xCB});        // mov bx, cs
  a.db({0x8E, 0xDB});        // mov ds, bx
  mov_ax(a, boot_handler_mark);
  store(a, boot_handler_ran, reg_ax);
  a.db({0x5B, 0x58, 0x1F});  // pop bx / pop ax / pop ds
  a.db({0xCF});              // iret

  // --- Data, in the clear -----------------------------------------------
  a.pad_to(boot_name_offset);
  for (const char c : boot_overlay_path) {
    a.db({static_cast<std::uint8_t>(c)});
  }
  a.db({0x00});

  a.pad_to(boot_thunk_offset);
  a.dw(boot_overlay_offset);
  a.label("overlay_segment");
  a.dw(0x0000);  // relocated to the load segment

  a.pad_to(boot_font_offset);
  for (const std::uint8_t byte : boot_font_glyph) {
    a.db({byte});
  }

  // --- The packed payload -----------------------------------------------
  a.pad_to(boot_packed_offset);
  for (const std::uint8_t byte : packed) {
    a.db({byte});
  }

  // Where it lands. These bytes are zero in the file; the unpacker puts
  // the payload here and the jump above arrives at it.
  a.pad_to(boot_payload_offset);
  a.label("payload_lands");

  a.pad_to(machine_layout::result_offset + 0x40);

  return build_exe({.initial_cs = 0,
                    .initial_ip = 0,
                    .initial_ss = 0,
                    .initial_sp = 0x0F00,
                    .min_alloc = 0x0100,
                    .relocations = {{.offset = static_cast<std::uint16_t>(
                                         a.offset_of("overlay_segment")),
                                     .segment = 0}},
                    .image = a.assemble()});
}

// --- 9. The seam probe ----------------------------------------------------
//
// M4-F2 (#96). A self-written program with a self-written seam: a
// breakpoint that edits a register and one that posts a key, run twice —
// once with the seam on and once with it off — so the difference between
// an enhanced machine and a plain one is a pair of result words on every
// target, and the toggle is something CI exercises rather than describes.
//
// The seam is keyed to this program's own SHA-256 (`seam_probe_definition`),
// the way every real seam is keyed to the binary its addresses describe,
// and its points are offsets this assembler names — which is the whole
// shape of PLAN.md §5 at the size of a test.
//
//         push cs / pop ds
//         mov  ax, 1111h
// edit:   mov  [result+0], ax        ; the seam sets AX=2222h just before
//         mov  cx, 4000h
// poll:   mov  ah, 01h / int 16h     ; the seam posts 'k' on the way in
//         jnz  got
//         loop poll
//         xor  ax, ax
//         jmp  done
// got:    mov  ah, 00h / int 16h
// done:   mov  [result+2], ax
//         <exit 88h>
//
// With the seam off the poll loop runs its 16,384 iterations against an
// empty buffer and stores zero; with it on, the first poll finds the key
// the seam just posted, which is what makes "nothing went through the
// host's queue" a claim the result block can carry.

/// What the probe program costs the machine when nothing has been done
/// to it: the poll loop runs its 16,384 iterations against an empty
/// buffer and the program exits.
///
/// **The number matters less than the fact that four entries claim the
/// same one** (#96, #161, #163). `seam_probe_off` is the plain machine;
/// `seam_probe_trigger_unpulled` is a trigger on and never asked;
/// `seam_probe_pull_unpulled` is a point with *no address* on and never
/// asked, which is the one that could plausibly have cost something,
/// because "offered at every step boundary" is a sentence about the hot
/// path. `seam_probe_trigger` is a pulled trigger whose one firing edits
/// a register the program overwrites, so it is the same run too.
///
/// If any of them ever diverges from the others, the fidelity invariant
/// has been broken on a target rather than in an argument, and the
/// benchmark says so on every one of them including the browser's
/// (`ctest --preset wasm`).
constexpr std::uint64_t probe_plain_steps = 81'933;

constexpr std::uint16_t probe_plain_ax = 0x1111;
constexpr std::uint16_t probe_edited_ax = 0x2222;
constexpr std::uint8_t probe_key_scancode = 0x25;  // 'k'
constexpr std::uint8_t probe_key_ascii = 'k';
constexpr std::uint16_t probe_keystroke =
    (std::uint16_t{probe_key_scancode} << 8U) | probe_key_ascii;

/// Where the two points are, in the image. Filled in by the one build of
/// the program below, and read by `seam_probe_definition()`.
struct probe_layout {
  std::vector<std::uint8_t> file;
  std::uint32_t edit_offset{};
  std::uint32_t poll_offset{};
  /// An offset in the image that execution never reaches — the byte just
  /// past the exit, which the assembler emits and the program leaves
  /// behind it. `seam_probe_unreached_definition()` puts its one point
  /// here so that "armed and never fired" is a thing this tree can build
  /// deliberately instead of only meeting by accident (#131, #147).
  std::uint32_t unreached_offset{};
  /// Where the program stores its second answer, reached exactly once
  /// however the poll loop ended. `seam_probe_host_definition()` (#169)
  /// puts its second point here rather than on the poll, so that each of
  /// its two callouts happens once and the count the ABI hands back is a
  /// number a reader can check against the program instead of against
  /// the loop bound.
  std::uint32_t done_offset{};
};

[[nodiscard]] const probe_layout& probe() {
  static const probe_layout built = [] {
    assembler a;
    a.db({0x0E, 0x1F});  // push cs / pop ds
    mov_ax(a, probe_plain_ax);
    a.label("edit");
    store(a, 0, reg_ax);
    a.db({0xB9});
    a.dw(0x4000);  // mov cx, 4000h
    a.label("poll");
    a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h
    a.jump(0x75, "got");             // jnz got
    a.jump(0xE2, "poll");            // loop poll
    a.db({0x31, 0xC0});              // xor ax, ax
    a.jump(0xEB, "done");            // jmp done
    a.label("got");
    a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h
    a.label("done");
    store(a, 1, reg_ax);
    exit_with(a, 0x88);
    // Past the exit, and so past everything the program does. The label
    // costs no bytes and names an address a point can be armed at and
    // never reached.
    a.label("unreached");
    a.pad_to(machine_layout::result_offset + 0x10);

    probe_layout out;
    out.edit_offset = static_cast<std::uint32_t>(a.offset_of("edit"));
    out.poll_offset = static_cast<std::uint32_t>(a.offset_of("poll"));
    out.unreached_offset = static_cast<std::uint32_t>(a.offset_of("unreached"));
    out.done_offset = static_cast<std::uint32_t>(a.offset_of("done"));
    out.file = build_exe({.initial_cs = 0,
                          .initial_ip = 0,
                          .initial_ss = 0,
                          .initial_sp = 0x0F00,
                          .min_alloc = 0x0100,
                          .relocations = {},
                          .image = a.assemble()});
    return out;
  }();
  return built;
}

// ---------------------------------------------------------------------------
// The automap's hotkey claim, at program scale (M5-E2, #173)
// ---------------------------------------------------------------------------
//
// The claim `seam_automap.cpp` makes about the keyboard funnel is a claim
// about a *program*: with the automap on, the number of times the program
// polls and the sequence of keys it is handed are what they would have
// been had Tab never been typed. That is not something a handler test can
// say, so it is said here, where all four targets run it.
//
// The stand-in is the real handler at a made-up address — which is what a
// stand-in is. Its offsets are this assembler's; its behaviour is the
// seam's own, read out of the definition rather than re-implemented, so
// nothing here can drift from what ships.
//
//         mov  ax, cs / add ax, 0C7Ch / mov ds, ax   ; the data segment
//         mov  cx, 4000h                             ;  where the seam
// poll:   mov  ah, 01h / int 16h                     ;  insists it is
//         jnz  got
//         loop poll
//         xor  ax, ax
//         jmp  done
// got:    mov  ah, 00h / int 16h
// done:   push cs / pop ds / mov [result+0], ax
//         <exit 89h>
//
// The data segment it points DS at is untouched RAM, so every byte the
// seam reads through it is zero — which is not the adventuring mode, so
// the panel half of the handler does nothing and only the claim runs.
// That is the half being measured.
//
// One thing it does have to stand in for (M5-E2d): the seam declines its
// own key unless the bar the program last put up is the adventuring
// screen's own, and it learns that at a sixth point, from the frame of
// the routine every bar goes up through. So this program builds that
// frame — the menu's far pointer where a caller would have left it, and
// nine words of filler under it — walks through the point, and drops the
// frame again. A stand-in that skipped it would be measuring a decline
// rather than a claim. Those instructions are in every step count below.

/// The paragraph offset of the program's data segment from its image, as
/// `seam_automap.cpp`'s fact table has it. Restated rather than shared:
/// this program is standing in for the real one and has to be laid out
/// the way the facts say, not the way the seam happens to compute.
constexpr std::uint16_t automap_dgroup_paragraphs = 0xC7C;

/// The adventuring screen's own 3D-view command bar, as an offset in
/// that data segment, and how far up the caller's frame the bar's far
/// pointer sits. Restated for the same reason.
constexpr std::uint16_t automap_menu_3d_view = 0x04DF;
constexpr unsigned automap_bar_frame_filler_words = 9;

/// Tab, as the BIOS hands it over. The scan code the host maps the key to
/// and the character that goes with it.
constexpr std::uint8_t automap_tab_scancode = 0x0F;
constexpr std::uint16_t automap_tab_keystroke = 0x0F09;

/// What the loop costs when the poll finds nothing — which is what the
/// quiet run costs, what the run with the seam on and no key costs, and
/// what the run with the seam on and a Tab typed costs. **Three entries
/// claiming one number** is the whole instrument (`machine_program::steps`).
constexpr std::uint64_t automap_unheard_steps = 81'947;

/// What the explored overlay's stand-in costs, both ways. **Two entries
/// claiming each number** is the instrument: a seam that lays fog over a
/// screen costs the program no step at all, so the run with it on and the
/// run without take exactly as long.
constexpr std::uint64_t explored_arrived_steps = 35'371;
constexpr std::uint64_t explored_walked_steps = 35'376;

struct automap_layout {
  std::vector<std::uint8_t> file;
  std::uint32_t poll_offset{};
  std::uint32_t bar_offset{};
};

[[nodiscard]] const automap_layout& automap_probe() {
  static const automap_layout built = [] {
    assembler a;
    a.db({0x8C, 0xC8});  // mov ax, cs
    a.db({0x05});
    a.dw(automap_dgroup_paragraphs);  // add ax, 0C7Ch
    a.db({0x8E, 0xD8});               // mov ds, ax

    // The bar, as its caller leaves it: the far pointer deepest, then
    // nine words of whatever, then the point.
    a.db({0x50});  // push ax  (the menu's segment: DS, still in AX)
    a.db({0xB8});
    a.dw(automap_menu_3d_view);
    a.db({0x50});  // push ax  (the menu's offset)
    for (unsigned i = 0; i < automap_bar_frame_filler_words; ++i) {
      a.db({0x50});  // push ax
    }
    a.label("bar");
    a.db({0x81, 0xC4});  // add sp, 16h — the frame, cleaned as the routine
    a.dw(0x0016);        // itself would clean it

    a.db({0xB9});
    a.dw(0x4000);  // mov cx, 4000h
    a.label("poll");
    a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h
    a.jump(0x75, "got");             // jnz got
    a.jump(0xE2, "poll");            // loop poll
    a.db({0x31, 0xC0});              // xor ax, ax
    a.jump(0xEB, "done");            // jmp done
    a.label("got");
    a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h
    a.label("done");
    a.db({0x0E, 0x1F});  // push cs / pop ds
    store(a, 0, reg_ax);
    exit_with(a, 0x89);
    a.pad_to(machine_layout::result_offset + 0x10);

    automap_layout out;
    out.poll_offset = static_cast<std::uint32_t>(a.offset_of("poll"));
    out.bar_offset = static_cast<std::uint32_t>(a.offset_of("bar"));
    out.file = build_exe({.initial_cs = 0,
                          .initial_ip = 0,
                          .initial_ss = 0,
                          .initial_sp = 0x0F00,
                          .min_alloc = 0x0100,
                          .relocations = {},
                          .image = a.assemble()});
    return out;
  }();
  return built;
}

/// One of the automap seam's own handlers, taken from the definition this
/// build ships — the key claim at its first point, and the bar reader at
/// its last. Null would be a seam table that no longer carries an
/// automap, which the definition test would have failed on first.
[[nodiscard]] machine::seam_handler automap_handler(std::size_t which) {
  for (const machine::seam_definition& seam : machine::all_seams()) {
    if (seam.id == "automap" && which < seam.points.size()) {
      return seam.points[which].run;
    }
  }
  return nullptr;
}

[[nodiscard]] machine::seam_handler automap_key_handler() {
  return automap_handler(0);
}

[[nodiscard]] machine::seam_handler automap_bar_handler() {
  for (const machine::seam_definition& seam : machine::all_seams()) {
    if (seam.id == "automap" && !seam.points.empty()) {
      return seam.points.back().run;
    }
  }
  return nullptr;
}

/// The two handlers. Plain functions with nowhere to keep state, exactly
/// as a real seam's are (machine/seam.h).
void probe_edit_ax(machine::machine& box, machine::seam_context& /*ctx*/) {
  box.processor().regs()[cpu::reg16::ax] = probe_edited_ax;
}

void probe_post_key(machine::machine& /*box*/, machine::seam_context& ctx) {
  static_cast<void>(ctx.inject_keystroke(probe_key_scancode, probe_key_ascii));
}

/// The handler that must never run. If it ever does, it writes a value no
/// assertion anywhere expects over the program's own first answer — so a
/// point that turned out to be reachable after all fails loudly on the
/// result block as well as on the count it was written to keep at zero.
void probe_never(machine::machine& box, machine::seam_context& ctx) {
  const auto segment = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  box.processor().write_byte(segment, machine_layout::result_offset, 0xFF);
  box.processor().write_byte(segment, machine_layout::result_offset + 1, 0xFF);
}

/// What the address-free point writes, into a result word the program
/// itself never touches. `0x3333` because the two words beside it are
/// `0x1111` and `0x2222` and a reader of a failing block should be able
/// to tell at a glance which of the three it is looking at.
constexpr std::uint16_t probe_pulled_mark = 0x3333;

/// The address-free point's handler (#163, `seam_point::at_every_step`):
/// the shape every such handler has to have, at the size of a test.
///
/// It is offered at *every* step boundary while the pull is outstanding
/// and has no address to tell it whether acting is safe, so it asks the
/// machine instead: it declines — which keeps the latch — until the
/// program has stored its own first answer, and acts at the first step
/// after that. Which makes the run say two things at once: that the
/// point really is consulted every step (the program's first answer is
/// stored 30-odd steps in and the mark is there at the exit), and that a
/// guard which does not hold really does keep the pull waiting rather
/// than swallowing it.
void probe_mark_when_ready(machine::machine& box, machine::seam_context& ctx) {
  const auto segment = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  cpu::processor& cpu = box.processor();
  const auto stored = static_cast<std::uint16_t>(
      cpu.read_byte(segment, machine_layout::result_offset) |
      (cpu.read_byte(segment, machine_layout::result_offset + 1) << 8U));
  if (stored != probe_plain_ax) {
    ctx.decline(machine::seam_reason::point_not_recognized);
    return;
  }
  const std::uint16_t mark_at = result_word(2);
  cpu.write_byte(segment, mark_at,
                 static_cast<std::uint8_t>(probe_pulled_mark));
  cpu.write_byte(segment, static_cast<std::uint16_t>(mark_at + 1),
                 static_cast<std::uint8_t>(probe_pulled_mark >> 8U));
}

/// M5-D1 (#169): what the two host-service handlers ask for, and what
/// they write down about the answer.
///
/// The arguments are arbitrary and distinct — the point is that whatever
/// the seam passed is what the engine records and the ABI hands back, so
/// two different numbers are worth more than two of the same.
constexpr std::uint32_t probe_journal_argument = 0x0000'1234;
constexpr std::uint32_t probe_automap_argument = 0x00AB'CDEF;

/// What a handler writes into its result word: this, or zero. Not 1,
/// because 1 is what half the machine's other answers are and a block
/// read by eye should say which check it belongs to.
constexpr std::uint16_t probe_served_mark = 0x5555;

/// Ask the host to open a journal entry, and record whether anyone
/// answered.
///
/// The whole shape of a callout at the size of a test: it does not touch
/// the machine, it does not stop it, and it does not assume it was
/// heard. `call_host()` answering false is a machine with no host
/// attached, which is a run this suite deliberately produces
/// (`seam_probe_host_unserved`) — and the seam's honest response is to
/// write down that nothing happened, not to pretend it did.
void probe_call_journal(machine::machine& box, machine::seam_context& ctx) {
  const bool served = ctx.call_host(machine::seam_host_service::journal_open,
                                    probe_journal_argument);
  const auto segment = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::uint16_t at = result_word(2);
  const auto mark = static_cast<std::uint16_t>(served ? probe_served_mark : 0U);
  box.processor().write_byte(segment, at, static_cast<std::uint8_t>(mark));
  box.processor().write_byte(segment, static_cast<std::uint16_t>(at + 1),
                             static_cast<std::uint8_t>(mark >> 8U));
}

/// The same, for the automap, at the program's other point.
void probe_call_automap(machine::machine& box, machine::seam_context& ctx) {
  const bool served = ctx.call_host(machine::seam_host_service::automap_update,
                                    probe_automap_argument);
  const auto segment = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::uint16_t at = result_word(3);
  const auto mark = static_cast<std::uint16_t>(served ? probe_served_mark : 0U);
  box.processor().write_byte(segment, at, static_cast<std::uint8_t>(mark));
  box.processor().write_byte(segment, static_cast<std::uint16_t>(at + 1),
                             static_cast<std::uint8_t>(mark >> 8U));
}

// --- 10. The camp stand-in ------------------------------------------------
//
// M5-E1 (#172), rebuilt for M5-E1a (#186). The Encamp (F)ix puts a command
// on the camp screen's own bar, watches for the letter coming back off the
// program's menu-bar routine, and drives the game's own rest with two
// keystrokes — and #172 asks for a stand-in for that menu here, so the
// whole mechanism runs on all four targets and not only where GoogleTest
// builds.
//
// **It is the real handler.** The definition below is the build's own
// `encamp-fix`, copied with this program's fingerprint in place of the
// game's and a different id, so what this program drives is the same three
// functions a player's copy would — their guards, their splice, their
// arithmetic, their writes. Only the fingerprint is the test's, because a
// fingerprint is what decides which binary a set of addresses may be
// applied to and this binary is not that one.
//
// The seam's points are addresses in an overlay, resolved through the word
// the program's overlay manager keeps its whereabouts in (#131). So this
// stand-in is its own overlay manager: it writes its own code segment into
// that word, and lays its three routines out at the offsets the seam's
// facts name — which is why the image below is nearly eight kilobytes of
// mostly nothing, and why every stretch of it is jumped over rather than
// fallen through.
//
//         mov  al, es:[80h] -> interrupts  ; the command tail's length
//         <lay the camp out: the mode byte, a command bar of this
//          program's own words, the prompt on the frame, the clock,
//          the out-parameter, and a two-member roster>
//         <write CS into the manager's word>
//         jmp  menu_loop
//
//   0760: the manager's word
//
//   077A: rest_entry:                    ; the game's Rest command
//         mov  ah, 00h / int 16h         ; the seam posts 'R' on the way in
//         mov  [result+0], ax
//         mov  al, [interrupts] -> [changed]   ; what the rest answered
//         jmp  loop_top
//
//   loop_top:                            ; the camp loop's own condition
//         cmp  byte [changed], 0 / jne camp_exit
//
//   1F06: menu_loop:                     ; the seam splices the bar here
//         mov  ax, [bar] / xor ah, ah
//         mov  [result+3], ax            ; the length the program sees
//         mov  cx, 4000h
//   poll: mov  ah, 01h / int 16h
//         jnz  got
//         loop poll
//         xor  ax, ax
//         jmp  at_return
//   got:  mov  ah, 00h / int 16h
//
//   1F24: at_return:                     ; the seam reads AL here
//         cmp  al, 'F' / je loop_top     ; the program's own bar has no F
//         cmp  al, 'R' / je rest_entry   ; and this is its Rest command
//  finish:
//         mov  ax, [days]  / mov [result+1], ax
//         mov  ax, [hours] / mov [result+2], ax
//         <exit 8Ah>
//
//   1FB2: camp_exit:                     ; the seam's fourth point
//         nop                            ; what the machine resumes onto
//         inc  word [exit_visits]
//         jmp  finish
//
// With the seam on and F pressed, the first pass hands the seam its
// letter, the second reads the Rest key it posted, and the rest command's
// entry finds a duration dialled and presses Rest — three points, two
// keys, one player keystroke. With the seam on and some other key pressed,
// or with it off, the pass runs once and the program exits, and **those
// two runs are the same run to the step**.
//
// **And a way out of camp** (M5-E1c, #194), because a rest the game
// interrupts does not hand the camp menu back and the report is then
// owed at a point the first three do not cover. The program's own loop
// condition is `loop_top`: while the out-parameter reads zero, round
// again; when it does not, out. The one thing that ever writes that byte
// is the rest, exactly as in the program — and what it writes is the
// command tail's length byte, so **one image drives both ways out** and
// there is no second binary with its own fingerprint to keep in step.

/// The camp, at the offsets `seam_encamp_fix.cpp` names — restated here
/// because this program is the thing being driven, not the thing being
/// checked, and a stand-in that read them out of the seam would be
/// agreeing with itself.
constexpr std::uint16_t camp_game_mode = 0x49F3;
constexpr std::uint8_t camp_mode_camp = 2;
constexpr std::uint16_t camp_bar = 0x508;
constexpr std::uint16_t camp_roster_head = 0x5D96;
constexpr std::uint16_t camp_rest_hours = 0x6DC8;
constexpr std::uint16_t camp_rest_days = 0x6DCA;
constexpr std::uint16_t camp_rec_max_hit_points = 0x32;
constexpr std::uint16_t camp_rec_next = 0x104;
constexpr std::uint16_t camp_rec_status = 0x10C;
constexpr std::uint16_t camp_rec_hit_points = 0x11B;

/// Where the seam's three points are in the module, and where the module
/// says it is. All four are the seam's facts, restated for the same
/// reason as the offsets above.
constexpr std::uint16_t camp_load_segment_at = 0x760;
constexpr std::uint16_t camp_rest_entry = 0x077A;
constexpr std::uint16_t camp_menu_loop = 0x1F06;
constexpr std::uint16_t camp_at_return = 0x1F24;
constexpr std::uint16_t camp_exit_point = 0x1FB2;

/// The camp loop's frame: where this program puts BP, and the two offsets
/// from it the seam reads — the prompt it blanks and the flag that says
/// whether a command was chosen off the bar.
constexpr std::uint16_t camp_frame_base = 0x0700;
constexpr std::uint16_t camp_frame_prompt = 0x0B;
constexpr std::uint16_t camp_frame_out_flag = 0x04;

/// And the one the fourth point reads (M5-E1c, #194), which is **above**
/// BP: it is the camp menu's own argument rather than one of its locals,
/// a far pointer at a byte the loop's condition tests on every pass.
constexpr std::uint16_t camp_frame_changed = 0x06;

/// **This program's own command bar**: three words of its own invention,
/// in the shape a menu bar has — words separated by spaces, a capital
/// each. The seam knows nothing about what a bar says, and a stand-in
/// carrying any of the program's own text would be the one thing this
/// tree does not do (PLAN.md §6).
constexpr std::string_view camp_bar_text = "Alpha Beta Gamma";
constexpr std::uint16_t camp_bar_plain_length = 16;
constexpr std::uint16_t camp_bar_fixed_length = camp_bar_plain_length + 4;

/// The prompt the loop builds before the bar, of no interest except that
/// the seam blanks it to make room.
constexpr std::string_view camp_prompt_text = "ABCD";

/// The program's own scratch, past the clock and clear of everything the
/// seam's facts name. `camp_changed` is what the frame's out-parameter
/// points at — the byte a rest writes its own answer into, and the whole
/// of how the fourth point tells an interruption from an exit;
/// `camp_interrupts` is what this program will write there when its rest
/// ends, taken from the command tail so that one image drives both ways
/// out; and `camp_exit_visits` counts the way out being taken.
constexpr std::uint16_t camp_changed = 0x6E04;
constexpr std::uint16_t camp_interrupts = 0x6E06;
constexpr std::uint16_t camp_exit_visits = 0x6E08;

/// The two routines the report calls (M5-E1b, #189), at the image offsets
/// `seam_encamp_fix.cpp` names — restated here for the same reason as
/// every other fact in this program.
///
/// **They do nothing but come back**, and that is the whole point: what
/// is being driven is the batch — that the calls are queued, that each
/// frame is built and torn down by the routine's own `retf`, and that the
/// point is offered again when the last one returns. What the drawing
/// looks like is not a thing a test on four targets can check, and a
/// stand-in that pretended to check it would be checking itself.
constexpr std::uint16_t camp_draw_frame = 0x41F8;
constexpr std::uint16_t camp_draw_string = 0x76B6;

/// And the message delay the interrupted report is held by (M5-E1c,
/// #194). It takes no arguments and cleans none, so its stand-in is one
/// instruction and a far return.
constexpr std::uint16_t camp_message_delay = 0x7E4E;

/// What each of them cleans off the stack: the frame drawer takes eight
/// words and the string drawer five.
constexpr std::uint8_t camp_draw_frame_cleans = 0x10;
constexpr std::uint8_t camp_draw_string_cleans = 0x0A;

/// Where each keeps its own count of how many times it was entered, so
/// the report is a thing this program can say happened rather than a
/// thing the seam says it did.
constexpr std::uint16_t camp_frame_calls = 0x6E00;
constexpr std::uint16_t camp_string_calls = 0x6E02;
constexpr std::uint16_t camp_delay_calls = 0x6E0A;

/// What the report draws for this program's party: the frame once, and
/// then the summary and one exception line — the hurt member, who is
/// still seven short because nothing here heals anybody.
constexpr std::uint16_t camp_wanted_frame_calls = 1;
constexpr std::uint16_t camp_wanted_string_calls = 2;

/// Where the two records go in the program's own segment, past everything
/// else it uses.
constexpr std::uint16_t camp_first_record = 0x7000;
constexpr std::uint16_t camp_second_record = 0x7200;

/// The party: one member five hit points into a maximum of twelve, one
/// whole. Seven down, so the seam should dial eight days — the deficit
/// and the day of slack its header explains.
constexpr std::uint8_t camp_hurt_hit_points = 5;
constexpr std::uint8_t camp_hurt_most = 12;
constexpr std::uint8_t camp_whole_hit_points = 9;
constexpr std::uint16_t camp_wanted_days =
    camp_hurt_most - camp_hurt_hit_points + 1;

/// What the camp stand-in costs the machine when the player chooses one of
/// the program's own commands rather than the Fix.
///
/// **Two entries claim this same number** — the Fix on with another key
/// pressed, and the Fix off with the same key pressed — and that equality
/// is what M5-E1a (#186) put in place of the invariant a pulled seam had.
/// The seam is armed in one of them and inert in the other, and the two
/// runs are the same run: the command it offered was not chosen, so the
/// program went exactly where it would have gone.
///
/// **Sixty-six until M5-E1b (#189) and seventy-two until M5-E1c (#194)**,
/// and every step it has grown by is this program's own and not the
/// seam's: counters for the stand-in drawing routines, the instructions
/// that read them back, and now the command tail this program reads its
/// own way out of camp from, the out-parameter it lays down, and the loop
/// condition that tests it. The number moving is fine; the two entries
/// claiming it moving apart would not be.
constexpr std::uint64_t camp_plain_steps = 86;

/// What the program's own rest wrapper would have left in the clock: a
/// memorization time in hours, and no days at all.
constexpr std::uint16_t camp_wrapper_hours = 3;

/// The keys: the one a player presses for the Fix, the one they press for
/// a command of the program's own, and the keystroke the seam posts, as
/// INT 16h hands it back.
constexpr std::uint8_t camp_fix_scancode = 0x21;    // F
constexpr std::uint8_t camp_other_scancode = 0x1F;  // S
constexpr std::uint16_t camp_rest_keystroke = (std::uint16_t{0x13} << 8U) | 'R';

///     mov  byte [off], imm8              ; C6 06 iw ib
void store_byte_at(assembler& a, std::uint16_t offset, std::uint8_t value) {
  a.db({0xC6, 0x06});
  a.dw(offset);
  a.db({value});
}

///     mov  word [off], imm16             ; C7 06 iw iw
void store_word_at(assembler& a, std::uint16_t offset, std::uint16_t value) {
  a.db({0xC7, 0x06});
  a.dw(offset);
  a.dw(value);
}

///     mov  [off], ds                     ; 8C 1E iw
void store_ds_at(assembler& a, std::uint16_t offset) {
  a.db({0x8C, 0x1E});
  a.dw(offset);
}

///     mov  ax, [off]                     ; A1 iw
void load_ax_from(assembler& a, std::uint16_t offset) {
  a.db({0xA1});
  a.dw(offset);
}

/// A Pascal string — length byte then characters — written where the
/// program keeps one.
void store_pascal_at(assembler& a, std::uint16_t offset,
                     std::string_view text) {
  store_byte_at(a, offset, static_cast<std::uint8_t>(text.size()));
  for (std::size_t nth = 0; nth < text.size(); ++nth) {
    store_byte_at(a, static_cast<std::uint16_t>(offset + 1 + nth),
                  static_cast<std::uint8_t>(text[nth]));
  }
}

/// One record: its status, its hit points, its maximum, and its link.
void camp_record(assembler& a, std::uint16_t at, std::uint8_t status,
                 std::uint8_t hit_points, std::uint8_t most,
                 std::uint16_t next) {
  store_byte_at(a, static_cast<std::uint16_t>(at + camp_rec_status), status);
  store_byte_at(a, static_cast<std::uint16_t>(at + camp_rec_hit_points),
                hit_points);
  store_byte_at(a, static_cast<std::uint16_t>(at + camp_rec_max_hit_points),
                most);
  store_word_at(a, static_cast<std::uint16_t>(at + camp_rec_next), next);
  if (next == 0) {
    store_word_at(a, static_cast<std::uint16_t>(at + camp_rec_next + 2), 0);
  } else {
    store_ds_at(a, static_cast<std::uint16_t>(at + camp_rec_next + 2));
  }
}

[[nodiscard]] const std::vector<std::uint8_t>& camp_file() {
  static const std::vector<std::uint8_t> built = [] {
    assembler a;
    // **Before DS is this program's own**, the command tail's length byte
    // off the PSP that ES still names (loader.cpp): non-zero says this
    // camp's rest is one the game interrupts, so one image drives both
    // ways out of camp and there is no second binary, with its own
    // fingerprint, to keep in step.
    a.db({0x26, 0xA0});
    a.dw(0x0080);        // mov al, es:[80h]
    a.db({0x0E, 0x1F});  // push cs / pop ds
    a.db({0xA2});
    a.dw(camp_interrupts);  // mov [interrupts], al

    // The frame the camp loop runs its menu in. SS is the image segment
    // already; only BP is this program's to choose.
    a.db({0xBD});
    a.dw(camp_frame_base);  // mov bp, imm16

    camp_record(a, camp_first_record, 0, camp_hurt_hit_points, camp_hurt_most,
                camp_second_record);
    camp_record(a, camp_second_record, 0, camp_whole_hit_points,
                camp_whole_hit_points, 0);
    store_word_at(a, camp_roster_head, camp_first_record);
    store_ds_at(a, static_cast<std::uint16_t>(camp_roster_head + 2));
    store_byte_at(a, camp_game_mode, camp_mode_camp);
    store_word_at(a, camp_rest_hours, camp_wrapper_hours);
    store_word_at(a, camp_rest_days, 0);
    store_word_at(a, camp_frame_calls, 0);
    store_word_at(a, camp_string_calls, 0);
    store_word_at(a, camp_delay_calls, 0);
    store_word_at(a, camp_exit_visits, 0);
    store_byte_at(a, camp_changed, 0);
    // The out-parameter, as the far pointer the loop's own condition
    // reads it as.
    store_word_at(
        a, static_cast<std::uint16_t>(camp_frame_base + camp_frame_changed),
        camp_changed);
    store_ds_at(a, static_cast<std::uint16_t>(camp_frame_base +
                                              camp_frame_changed + 2));
    store_pascal_at(a, camp_bar, camp_bar_text);
    store_pascal_at(
        a, static_cast<std::uint16_t>(camp_frame_base - camp_frame_prompt),
        camp_prompt_text);
    store_byte_at(
        a, static_cast<std::uint16_t>(camp_frame_base - camp_frame_out_flag),
        0);

    // The overlay manager's own note of where the module is, which this
    // program is: its code segment, in the word the seam's facts name.
    // Written last of the set-up, because until it is written nothing is
    // armed and the seam is inert with a reason (#131).
    a.db({0x8C, 0xC8});  // mov ax, cs
    a.db({0xA3});
    a.dw(camp_load_segment_at);  // mov [imm16], ax

    a.near_jump(0xE9, "loop_top");

    // The word, and then the rest command — both at the offsets the
    // module's facts name, with the gap between them jumped over.
    a.pad_to(camp_load_segment_at);
    a.dw(0);

    a.pad_to(camp_rest_entry);
    a.label("rest_entry");
    a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h
    store(a, 0, reg_ax);
    // **What the rest answered**, which in the program is whether a
    // wandering monster ended it, and here is what the command tail asked
    // for. It goes where the camp menu's own out-parameter points, which
    // is the only thing that ever writes that byte (M5-E1c, #194).
    a.db({0xA0});
    a.dw(camp_interrupts);  // mov al, [interrupts]
    a.db({0xA2});
    a.dw(camp_changed);  // mov [changed], al
    // **And back to the top of the camp loop**, which is where the
    // program goes when a rest is over. It used to exit here, and that
    // made the report untestable on this program: the box is drawn on the
    // pass of the menu *after* the rest, and a stand-in that stopped at
    // the rest never reached it (M5-E1b, #189).
    a.near_jump(0xE9, "loop_top");

    // The loop's own condition, which is the program's and not the
    // seam's: while the out-parameter reads zero, go round; when it does
    // not, leave. The address is this program's own choosing — only the
    // three the seam's facts name are fixed.
    a.label("loop_top");
    a.db({0x80, 0x3E});
    a.dw(camp_changed);
    a.db({0x00});                   // cmp byte [changed], 0
    a.jump(0x74, "into_the_menu");  // je into_the_menu
    a.near_jump(0xE9, "camp_exit");
    a.label("into_the_menu");
    a.near_jump(0xE9, "menu_loop");

    // The menu-bar routine, in exactly the thirty bytes between the two
    // points the seam has here. `pad_to` is the guard on that: a routine
    // that outgrew the gap would be a program that could not be built,
    // rather than a point landing in the middle of an instruction.
    a.pad_to(camp_menu_loop);
    a.label("menu_loop");
    a.db({0xA0});
    a.dw(camp_bar);  // mov al, [bar] — the length byte the routine draws
    a.db({0xA2});
    a.dw(result_word(3));  // mov [result+3], al
    a.db({0xB9});
    a.dw(0x4000);  // mov cx, 4000h
    a.label("poll");
    a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h
    a.jump(0x75, "got");             // jnz got
    a.jump(0xE2, "poll");            // loop poll
    a.db({0x31, 0xC0});              // xor ax, ax
    a.jump(0xEB, "at_return");
    a.label("got");
    a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h
    // The program's own menu-bar routine uppercases what it read before
    // it matches it against the bar, so the letter it answers with is an
    // upper-case one and the seam's fact is that letter.
    a.db({0x24, 0xDF});  // and al, 0DFh
    a.jump(0xEB, "at_return");

    a.pad_to(camp_at_return);
    a.label("at_return");
    a.db({0x3C, 'F'});  // cmp al, 'F'
    a.jump(0x75, "not_fix");
    a.near_jump(0xE9, "menu_loop");  // the loop goes round: an unknown
    a.label("not_fix");              // letter is a letter it ignores
    a.db({0x3C, 'R'});               // cmp al, 'R'
    a.jump(0x75, "finish");
    a.near_jump(0xE9, "rest_entry");

    a.label("finish");
    load_ax_from(a, camp_rest_days);
    store(a, 1, reg_ax);
    load_ax_from(a, camp_rest_hours);
    store(a, 2, reg_ax);
    load_ax_from(a, camp_frame_calls);
    store(a, 4, reg_ax);
    load_ax_from(a, camp_string_calls);
    store(a, 5, reg_ax);
    load_ax_from(a, camp_exit_visits);
    store(a, 6, reg_ax);
    load_ax_from(a, camp_delay_calls);
    store(a, 7, reg_ax);
    exit_with(a, 0x8A);

    // The way out of camp, at the offset the seam's fourth point names.
    // The instruction the point runs before is a `nop` for the same
    // reason every other point's is what it is: the machine is put back
    // on it when a batch ends, so it is offered twice and must cost
    // nothing the second time.
    a.pad_to(camp_exit_point);
    a.label("camp_exit");
    a.db({0x90});  // nop
    a.db({0xFF, 0x06});
    a.dw(camp_exit_visits);  // inc word [exit_visits]
    a.near_jump(0xE9, "finish");

    // The two drawing routines, each counting its own entries and popping
    // its own arguments — far, and cleaning up after itself, which is the
    // shape every routine the seam door calls has (`docs/seams.md` §3).
    a.pad_to(camp_draw_frame);
    a.db({0xFF, 0x06});
    a.dw(camp_frame_calls);  // inc word [frame_calls]
    a.db({0xCA});
    a.dw(camp_draw_frame_cleans);  // retf imm16

    a.pad_to(camp_draw_string);
    a.db({0xFF, 0x06});
    a.dw(camp_string_calls);  // inc word [string_calls]
    a.db({0xCA});
    a.dw(camp_draw_string_cleans);  // retf imm16

    a.pad_to(camp_message_delay);
    a.db({0xFF, 0x06});
    a.dw(camp_delay_calls);  // inc word [delay_calls]
    a.db({0xCB});            // retf — no arguments to clean

    // Room for the camp: the records sit past 0x7000 in this program's
    // own segment, and DOS has to have given it that much.
    return build_exe({.initial_cs = 0,
                      .initial_ip = 0,
                      .initial_ss = 0,
                      .initial_sp = 0x0F00,
                      .min_alloc = 0x0800,
                      .relocations = {},
                      .image = a.assemble()});
  }();
  return built;
}

// ---------------------------------------------------------------------------
// The explored overlay, at program scale (M5-E5, #179)
// ---------------------------------------------------------------------------
//
// What `seam_explored.cpp` claims is a claim about a *program* and a
// *screen*, so it is made here, where all four targets run it:
//
//   * on, the overworld shown, and nothing stood on but the square under
//     the party — the three-by-three around the party is the screen the
//     program composed and the other sixteen squares of the window are
//     covered;
//   * on, and one square walked — the window has scrolled, the reveal is
//     the union of two three-by-threes, and the row ahead of the party is
//     still covered.
//
// Neither run costs the program a step it would not otherwise have taken,
// which is the other half of each pair.
//
// The stand-in is the real handlers at made-up addresses. It sets the
// video mode, lays out the data segment and an area record the way the
// facts say the program lays them out (`docs/explored-overlay.md` §2),
// puts the party's own command bar up through the bar point, polls the
// key routine so the position settles and the square is recorded,
// **paints the window white** — because this seam's drawing is black and
// black on a blank screen is no picture at all — and then runs an
// instruction at the address this seam calls "the present has returned".
// It ends with a spin long enough for a frame boundary to fall after its
// last write, because a frame nobody composed is not a picture anybody
// can assert.
//
// **The paint goes immediately before the present**, which is where the
// program's own composition goes, and it has to: fog is black and black
// does not come off. A stand-in that painted once at the start would
// carry the fog it laid on arrival under the fog it lays after a step,
// and a run that walked would compose the same frame as a run that stood
// still — which is exactly the failure it was written to catch.
//
// **A key decides whether the party walks.** One image, four entries: a
// run with no key stays on the square it arrived on, and a run with a key
// steps one square north and takes its reveal with it.

/// The paragraph offset of the program's data segment from its image,
/// and the offsets in it, as `seam_explored.cpp`'s fact table has them.
/// Restated rather than shared: this program is standing in for the real
/// one and has to be laid out the way the facts say.
constexpr std::uint16_t explored_dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t explored_data_game_mode = 0x49F3;
constexpr std::uint16_t explored_data_view_kind = 0x49FA;
constexpr std::uint16_t explored_data_in_transition = 0x442F;
constexpr std::uint16_t explored_data_area_record = 0x49D2;
constexpr std::uint16_t explored_data_disk_number = 0x5376;
constexpr std::uint16_t explored_data_area_id = 0x84DC;
constexpr std::uint16_t explored_data_column_bias = 0x3C76;
constexpr std::uint16_t explored_menu_area_view = 0x04B6;
constexpr unsigned explored_bar_frame_filler_words = 9;

/// Where this program puts the area record, and the offsets inside it.
/// An offset in the data segment below everything else the seam reads,
/// so the record and the fields cannot overlap.
constexpr std::uint16_t explored_record_offset = 0x1000;
constexpr std::uint16_t explored_record_column = 0x186;
constexpr std::uint16_t explored_record_row = 0x188;
constexpr std::uint16_t explored_record_shown_in_3d = 0x1CC;

/// The wilderness area, the party's square, and the square it walks to.
constexpr std::uint8_t explored_mode_overland = 3;
constexpr std::uint8_t explored_view_kind = 2;
constexpr std::uint8_t explored_disk = 6;
constexpr std::uint8_t explored_area = 0x19;
constexpr std::uint8_t explored_bias = 0;
constexpr std::uint16_t explored_start_column = 3;
constexpr std::uint16_t explored_start_row = 32;
constexpr std::uint16_t explored_walked_row = 31;

/// The window, restated from the facts and not from the seam
/// (`docs/explored-overlay.md` §3): 120 by 120 pixels at (8, 8), five
/// cells of 24, and the frame it sits in is 320 by 200.
constexpr unsigned explored_window_left = 8;
constexpr unsigned explored_window_top = 8;
constexpr unsigned explored_cell_side = 24;
constexpr unsigned explored_window_across = 5;

/// One cell's worth of pixels, and the whole window's.
constexpr std::size_t explored_cell_area =
    std::size_t{explored_cell_side} * explored_cell_side;
constexpr std::size_t explored_window_area =
    explored_cell_area * explored_window_across * explored_window_across;

/// **How many of the twenty-five are left uncovered**, worked out from
/// the facts rather than from the seam. At a reveal radius of one, a
/// party standing at (3, 32) with a window whose top-left cell is
/// (0 + 3 - 2, 32 - 2) = (1, 30) sees map columns 2..4 and rows 31..33,
/// which is a three-by-three of the window; a step north to (3, 31)
/// scrolls the window to (1, 29) and the union of the two reveals is
/// three columns by four rows.
constexpr unsigned explored_clear_on_arrival = 3 * 3;
constexpr unsigned explored_clear_after_a_step = 3 * 4;

/// One pixel inside a cell of the window, by the cell's column and row.
[[nodiscard]] constexpr unsigned explored_pixel_x(unsigned column) {
  return explored_window_left + (column * explored_cell_side) + 1;
}
[[nodiscard]] constexpr unsigned explored_pixel_y(unsigned row) {
  return explored_window_top + (row * explored_cell_side) + 1;
}

/// A key, to make the party walk.
constexpr std::uint8_t explored_walk_scancode = 0x1E;

struct explored_layout {
  std::vector<std::uint8_t> file;
  std::uint32_t present_offset{};
  std::uint32_t poll_offset{};
  std::uint32_t bar_offset{};
};

[[nodiscard]] const explored_layout& explored_probe() {
  static const explored_layout built = [] {
    assembler a;
    mov_ax(a, 0x000D);   // the 320x200 graphics mode the program runs in
    a.db({0xCD, 0x10});  // int 10h

    a.db({0x8C, 0xC8});  // mov ax, cs
    a.db({0x05});
    a.dw(explored_dgroup_paragraphs);  // add ax, 0C7Ch
    a.db({0x8E, 0xD8});                // mov ds, ax

    // The data segment and the area record, laid out from the facts.
    store_word_at(a, explored_data_area_record, explored_record_offset);
    store_ds_at(a, static_cast<std::uint16_t>(explored_data_area_record + 2));
    store_byte_at(a, explored_data_game_mode, explored_mode_overland);
    store_byte_at(a, explored_data_view_kind, explored_view_kind);
    store_byte_at(a, explored_data_in_transition, 0);
    store_byte_at(a, explored_data_disk_number, explored_disk);
    store_byte_at(a, explored_data_area_id, explored_area);
    store_byte_at(a,
                  static_cast<std::uint16_t>(explored_data_column_bias +
                                             explored_view_kind),
                  explored_bias);
    store_word_at(a,
                  static_cast<std::uint16_t>(explored_record_offset +
                                             explored_record_column),
                  explored_start_column);
    store_word_at(a,
                  static_cast<std::uint16_t>(explored_record_offset +
                                             explored_record_row),
                  explored_start_row);
    store_word_at(a,
                  static_cast<std::uint16_t>(explored_record_offset +
                                             explored_record_shown_in_3d),
                  0);

    // The bar, as its caller leaves it: the far pointer deepest, then
    // nine words of whatever, then the point. The menu's segment is the
    // data segment, which is what the seam compares it against.
    a.db({0x8C, 0xD8});  // mov ax, ds
    a.db({0x50});        // push ax  (the menu's segment)
    mov_ax(a, explored_menu_area_view);
    a.db({0x50});  // push ax  (the menu's offset)
    for (unsigned i = 0; i < explored_bar_frame_filler_words; ++i) {
      a.db({0x50});
    }
    a.label("bar");
    a.db({0x81, 0xC4});  // add sp, 16h — the frame, cleaned as the routine
    a.dw(0x0016);        // itself would clean it

    // Two passes of polls, with the walk between them. Both passes go
    // through the *same* armed address, because both have to be seen:
    // the first settles the position and records the square the party
    // arrived on, the second records the one it walked to.
    a.db({0xBB});
    a.dw(0x0002);  // mov bx, 2
    a.label("pass");
    a.db({0xB9});
    a.dw(0x0004);  // mov cx, 4
    a.label("poll");
    a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h
    a.jump(0xE2, "poll");            // loop poll
    a.db({0x4B});                    // dec bx
    a.jump(0x74, "walked");          // jz walked
    a.db({0xB4, 0x01, 0xCD, 0x16});  // mov ah, 01h / int 16h — a key?
    a.jump(0x74, "pass");            // jz pass — no key, no walk
    a.db({0xB4, 0x00, 0xCD, 0x16});  // mov ah, 00h / int 16h — take it
    store_word_at(a,
                  static_cast<std::uint16_t>(explored_record_offset +
                                             explored_record_row),
                  explored_walked_row);
    a.jump(0xEB, "pass");  // jmp pass

    a.label("walked");
    // **The program composes its screen**, which is what makes this a
    // picture and not an accumulation: the window is painted white on all
    // four planes — 120 rows of fifteen whole bytes from byte column 1,
    // which is x = 8 — immediately before the present, exactly as the
    // program repaints the whole window on every move. Without it the fog
    // this seam laid at the keyboard poll would still be on the screen
    // under the fog it lays at the present, and a run that walked would
    // compose the same frame as a run that stood still.
    //
    // The mode set leaves the map mask at 0x0F and the bit mask at 0xFF,
    // and so does this seam, so a plain store reaches every plane.
    mov_ax(a, 0xA000);
    a.db({0x8E, 0xC0});  // mov es, ax
    a.db({0xFC});        // cld
    a.db({0xBB});
    a.dw((8 * 40) + 1);  // mov bx, the first byte of the window's first row
    a.db({0xBA});
    a.dw(120);  // mov dx, the window's height in scanlines
    a.label("paint");
    a.db({0x89, 0xDF});  // mov di, bx
    a.db({0xB9});
    a.dw(15);                  // mov cx, the window's width in bytes
    a.db({0xB0, 0xFF});        // mov al, 0FFh
    a.db({0xF3, 0xAA});        // rep stosb
    a.db({0x83, 0xC3, 0x28});  // add bx, 40 — the next scanline
    a.db({0x4A});              // dec dx
    a.jump(0x75, "paint");     // jnz paint

    // The program has just put the screen up, as far as this stand-in is
    // concerned. One instruction, at the address the seam calls the
    // present's return.
    a.label("present");
    a.db({0x90});  // nop

    // Long enough for a frame boundary to fall after the last write.
    a.db({0xB9});
    a.dw(0x8000);  // mov cx, 8000h
    a.label("spin");
    a.jump(0xE2, "spin");  // loop spin

    exit_with(a, 0x8A);
    a.pad_to(machine_layout::result_offset + 0x10);

    explored_layout out;
    out.present_offset = static_cast<std::uint32_t>(a.offset_of("present"));
    out.poll_offset = static_cast<std::uint32_t>(a.offset_of("poll"));
    out.bar_offset = static_cast<std::uint32_t>(a.offset_of("bar"));
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

/// One of the explored overlay's own handlers, taken from the definition
/// this build ships. Null would be a seam table that no longer carries
/// one, which the definition test would have failed on first.
[[nodiscard]] machine::seam_handler explored_handler(std::size_t which) {
  for (const machine::seam_definition& seam : machine::all_seams()) {
    if (seam.id == "explored" && which < seam.points.size()) {
      return seam.points[which].run;
    }
  }
  return nullptr;
}

// --- 11. The call door ----------------------------------------------------
//
// M5-D4 (#188). A seam asks the program to run one of its own routines,
// and comes back. The real consumers call the game's text and frame
// drawers so that what a seam puts on the game's screen is drawn by the
// game; what that has in common with this is only the frame the engine
// builds, so this program writes a routine of its own that **reports what
// it was handed**.
//
// Three things it is evidence for, and the third is the one a unit test
// cannot make:
//
//   1. the frame the engine builds is the frame the program's own callers
//      build — the words come off the stack where the routine's own
//      `[bp+n]` reads look for them, and a far pointer among them names
//      the bytes the seam placed;
//   2. a batch is a batch: two calls queued in one arrival both run, in
//      order, and the handler is never re-entered part-way;
//   3. **an interrupt during the call is survivable.** The routine hooks
//      nothing itself; the program hooks the user tick before the seam's
//      point is reached, the routine burns enough virtual time that the
//      tick certainly arrives inside the call, and the routine then reads
//      its arguments *afterwards*. A frame that an interrupt had damaged
//      would read the wrong words. The tick handler counts in memory
//      rather than in BP, because BP is what the routine's own frame is
//      built on — which is the whole point.
//
//         start:  <hook the user tick, program the PIT, sti>
//         trigger:                       ; the seam's point
//                 nop                    ; what the machine resumes onto
//                 <store what the routine saw>
//                 exit 8Bh
//
//         routine:
//                 push bp / mov bp, sp / push ds / push cs / pop ds
//                 <sample the tick counter>
//                 mov cx, delay / spin: loop spin
//                 <did it change? then an interrupt happened in here>
//                 mov ax, [bp+0Ch] -> saw_first    ; read AFTER the tick
//                 les di, [bp+6] / mov al, es:[di] -> saw_byte
//                 inc word [count]
//                 pop ds / mov sp, bp / pop bp / retf 8

/// Where the pieces sit in the program's own segment. The routine and
/// the point are at round offsets because the seam's facts name them, and
/// everything in between is jumped over rather than fallen through.
constexpr std::uint16_t door_trigger_offset = 0x0100;
constexpr std::uint16_t door_tick_offset = 0x0180;
constexpr std::uint16_t door_routine_offset = 0x0200;

/// The program's own scratch, clear of its code and of the result block.
constexpr std::uint16_t door_ticks = 0x0700;
constexpr std::uint16_t door_ticks_at_entry = 0x0702;
constexpr std::uint16_t door_ticked = 0x0704;
constexpr std::uint16_t door_saw_first = 0x0706;
constexpr std::uint16_t door_saw_byte = 0x0708;
constexpr std::uint16_t door_count = 0x070A;

/// The two words the seam passes and the byte it places.
constexpr std::uint16_t door_first_word = 0x1234;
constexpr std::uint16_t door_second_word = 0x5678;
constexpr std::uint8_t door_placed_byte = 0x5A;

/// How many calls the handler queues.
constexpr std::uint16_t door_calls = 2;

/// The IRQ0 divisor and the routine's delay, in PIT ticks — which is
/// steps, because these programs run one tick to the step. The delay is
/// twice the divisor, so a tick arrives inside every call and the
/// evidence is not a race.
constexpr std::uint16_t door_divisor = 0x1000;
constexpr std::uint16_t door_delay = 0x2000;

/// The handler the seam runs, in this file rather than in the build:
/// this is the *mechanism's* stand-in, so the thing being driven is the
/// engine and the handler is the test's.
///
/// **Its guard is a word of the program's own memory**, not a counter
/// beside the machine. The machine resumes onto the instruction the
/// handler was called at, so the point is reached again the moment the
/// batch ends — and a handler that queued another batch every time would
/// never stop. The routine's own call counter is what says "this has
/// already happened", which is the shape `seam_encamp_fix.cpp` uses for
/// the same reason (#186).
void door_call_program(machine::machine& box, machine::seam_context& ctx) {
  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  if (box.processor().read_word(image, door_count) != 0) {
    ctx.decline(machine::seam_reason::point_not_recognized);
    return;
  }

  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  const std::array<std::uint8_t, 1> bytes{door_placed_byte};
  if (!ctx.place_bytes(bytes, segment, offset)) {
    ctx.decline(machine::seam_reason::point_not_recognized);
    return;
  }

  // Segment then offset: the offset is pushed last and so lands at the
  // lower address, which is where a `les` looks for a far pointer — the
  // order the program's own callers push one in.
  const std::array<std::uint16_t, 4> words{door_first_word, door_second_word,
                                           segment, offset};
  for (unsigned nth = 0; nth < door_calls; ++nth) {
    if (!ctx.call_program(image, door_routine_offset, words)) {
      ctx.decline(machine::seam_reason::point_not_recognized);
      return;
    }
  }
}

constexpr std::array<machine::seam_point, 1> door_points{
    {{.module = machine::resident_image,
      .offset = door_trigger_offset,
      .run = &door_call_program}}};

[[nodiscard]] const std::vector<std::uint8_t>& door_file() {
  static const std::vector<std::uint8_t> built = [] {
    assembler a;
    // **Before DS is this program's own**, the command tail's length byte
    // off the PSP that ES still names (loader.cpp): non-zero says this
    // camp's rest is one the game interrupts, so one image drives both
    // ways out of camp and there is no second binary, with its own
    // fingerprint, to keep in step.
    a.db({0x26, 0xA0});
    a.dw(0x0080);        // mov al, es:[80h]
    a.db({0x0E, 0x1F});  // push cs / pop ds
    a.db({0xA2});
    a.dw(camp_interrupts);  // mov [interrupts], al

    // The user tick, pointed at this program's own counter, and the
    // timer that drives it. Both before the point, so the interrupt is
    // already live when the seam calls.
    a.db({0x31, 0xC0, 0x8E, 0xD8});  // xor ax, ax / mov ds, ax
    a.db({0xC7, 0x06});
    a.dw(static_cast<std::uint16_t>(4U * machine::service::user_tick_vector));
    a.dw(door_tick_offset);
    a.db({0x8C, 0x0E});
    a.dw(static_cast<std::uint16_t>(4U * machine::service::user_tick_vector +
                                    2U));
    a.db({0x0E, 0x1F});  // push cs / pop ds
    init_pic(a);
    program_timer(a, door_divisor);
    a.db({0xFB});  // sti
    a.near_jump(0xE9, "trigger");

    // The point. The machine resumes onto the instruction here when the
    // batch ends, so it is a NOP and not something that matters.
    a.pad_to(door_trigger_offset);
    a.label("trigger");
    a.db({0x90});  // nop
    load_ax_from(a, door_saw_first);
    store(a, 0, reg_ax);
    load_ax_from(a, door_saw_byte);
    store(a, 1, reg_ax);
    load_ax_from(a, door_count);
    store(a, 2, reg_ax);
    load_ax_from(a, door_ticked);
    store(a, 3, reg_ax);
    exit_with(a, 0x8B);

    // The tick handler, counting in memory. `inc bp` — what every other
    // program here uses — would be incrementing the routine's own frame
    // pointer while the routine is inside its frame.
    a.pad_to(door_tick_offset);
    a.db({0x2E, 0xFF, 0x06});  // inc word cs:[door_ticks]
    a.dw(door_ticks);
    a.db({0xCF});  // iret

    // The routine the seam calls.
    a.pad_to(door_routine_offset);
    a.db({0x55, 0x89, 0xE5});     // push bp / mov bp, sp
    a.db({0x1E, 0x0E, 0x1F});     // push ds / push cs / pop ds
    load_ax_from(a, door_ticks);  // mov ax, [ticks]
    a.db({0xA3});
    a.dw(door_ticks_at_entry);  // mov [ticks_at_entry], ax
    a.db({0xB9});
    a.dw(door_delay);  // mov cx, delay
    a.label("spin");
    a.jump(0xE2, "spin");         // loop spin
    load_ax_from(a, door_ticks);  // mov ax, [ticks]
    a.db({0x2B, 0x06});
    a.dw(door_ticks_at_entry);  // sub ax, [ticks_at_entry]
    a.jump(0x74, "no_tick");    // je no_tick
    store_word_at(a, door_ticked, 1);
    a.label("no_tick");
    // And only now the arguments, so that what they read is a frame an
    // interrupt has already been taken across.
    a.db({0x8B, 0x46, 0x0C});  // mov ax, [bp+0Ch] — the deepest word
    a.db({0xA3});
    a.dw(door_saw_first);
    a.db({0xC4, 0x7E, 0x06});  // les di, [bp+6]
    a.db({0x26, 0x8A, 0x05});  // mov al, es:[di]
    a.db({0x30, 0xE4});        // xor ah, ah
    a.db({0xA3});
    a.dw(door_saw_byte);
    a.db({0xFF, 0x06});
    a.dw(door_count);          // inc word [count]
    a.db({0x1F});              // pop ds
    a.db({0x89, 0xEC, 0x5D});  // mov sp, bp / pop bp
    a.db({0xCA, 0x08, 0x00});  // retf 8

    a.pad_to(machine_layout::result_offset + 0x10);
    return build_exe({.initial_cs = 0,
                      .initial_ip = 0,
                      .initial_ss = 0,
                      .initial_sp = 0x0F00,
                      .min_alloc = 0x0100,
                      .relocations = {},
                      .image = a.assemble()});
  }();
  return built;
}

[[nodiscard]] std::vector<machine_program> build_all() {
  std::vector<machine_program> list;

  {
    machine_program p;
    p.name = "timer";
    p.about = "INT 1Ch ticks counted against a programmed divisor";
    p.setup.code = timer_code();
    p.setup.step_cap = 200'000;
    p.results = {{.what = "ticks the hooked INT 1Ch counted",
                  .value = timer_wanted_ticks},
                 {.what = "40:6C, the BIOS's own count, low half",
                  .value = timer_wanted_ticks},
                 {.what = "40:6C, high half", .value = 0}};
    p.exit_code = 0x11;
    p.least_time = ticks{timer_divisor} * timer_wanted_ticks;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "video";
    p.about = "mode 0Dh, every write mode, the latch and bit-mask paths";
    p.setup.code = video_code();
    p.setup.step_cap = 400'000;
    p.results = {
        {.what = "plane 0 at 0: rotate, OR, then bit mask", .value = 0xF0},
        {.what = "plane 1 at 0", .value = 0xF0},
        {.what = "plane 0 at 1: map mask let it through", .value = 0x3C},
        {.what = "plane 1 at 1: map mask gated it out", .value = 0x00},
        {.what = "plane 0 at 2: set/reset expanded a 1", .value = 0xFF},
        {.what = "plane 1 at 2: set/reset expanded a 0", .value = 0x00},
        {.what = "plane 2 at 2", .value = 0xFF},
        {.what = "plane 0 at 3: write mode 1 copied the latch", .value = 0x3C},
        {.what = "plane 1 at 3", .value = 0x00},
        {.what = "plane 0 at 4: write mode 2 expanded bit 0", .value = 0x00},
        {.what = "plane 1 at 4: and bit 1", .value = 0xFF},
        {.what = "plane 2 at 4: and bit 2", .value = 0xFF},
        {.what = "read mode 1: every bit matches", .value = 0xFF},
        {.what = "read mode 1: no bit matches", .value = 0x00},
        {.what = "read mode 1: 3Ch's bits match", .value = 0x3C}};
    p.exit_code = 0x22;

    // Row 0 is the write pipeline's own scratch, eight pixels to a byte,
    // MSB first, plane n contributing bit n of the palette index
    // (renderer.h). Offset 0 ended F0h on every plane, so its top nibble
    // is colour 15 and its bottom nibble colour 0; offsets 1 and 3 hold
    // 3Ch on planes 0 and 2 only, which is colour 5 in the four bits 3Ch
    // sets; offset 2 is FFh on the same two planes, so all eight;
    // offset 4 is FFh on planes 1 and 2, which is colour 6.
    p.pixels = {{.x = 0, .y = 0, .index = 15},
                {.x = 3, .y = 0, .index = 15},
                {.x = 4, .y = 0, .index = 0},
                {.x = 9, .y = 0, .index = 0},
                {.x = 10, .y = 0, .index = 5},
                {.x = 13, .y = 0, .index = 5},
                {.x = 16, .y = 0, .index = 5},
                {.x = 23, .y = 0, .index = 5},
                {.x = 24, .y = 0, .index = 0},
                {.x = 26, .y = 0, .index = 5},
                {.x = 32, .y = 0, .index = 6},
                {.x = 39, .y = 0, .index = 6},
                // The first band: planes 0 and 3, so colour 9, over the
                // sixteen rows starting at offset 320 (40 bytes a row).
                {.x = 0, .y = 7, .index = 0},
                {.x = 0, .y = 8, .index = 9},
                {.x = 319, .y = 23, .index = 9},
                {.x = 0, .y = 24, .index = 0},
                // The second: planes 1 and 2, colour 6, eight rows from
                // offset 1280.
                {.x = 0, .y = 31, .index = 0},
                {.x = 0, .y = 32, .index = 6},
                {.x = 319, .y = 39, .index = 6},
                {.x = 0, .y = 40, .index = 0}};

    // And the whole frame accounted for, colour by colour: four pixels of
    // 15, sixteen of 5 (four at offset 1, eight at offset 2, four at
    // offset 3), 16 * 320 of 9, 8 * 320 of 6 plus the eight at offset 4,
    // and everything else still black.
    constexpr std::size_t first_band = 16 * pixels_per_row;
    constexpr std::size_t second_band = (8 * pixels_per_row) + 8;
    p.areas = {{.index = 15, .count = 4},
               {.index = 5, .count = 16},
               {.index = 9, .count = first_band},
               {.index = 6, .count = second_band},
               {.index = 0,
                .count = pixels_per_frame - 4 - 16 - first_band - second_band}};

    // The palette INT 10h's mode set installs, through the four wires
    // this machine's display has (ega.h): code 5 is AAh/00h/AAh, code 6
    // — the brown the display makes of RGBI 0110 — is AAh/55h/00h, code
    // 17 at index 9 is 55h/55h/FFh, code 23 is white. Index 1 is not the
    // default at all but the 2Ah this program set through AH=10h, and 2Ah
    // is the case worth having: of its three high bits only bit 4 is
    // wired to anything, and it is the one 2Ah leaves clear, so the two
    // that *are* set change nothing and the colour is plain green.
    p.palette = {
        {.index = 0, .color = {.red = 0x00, .green = 0x00, .blue = 0x00}},
        {.index = 1, .color = {.red = 0x00, .green = 0xAA, .blue = 0x00}},
        {.index = 5, .color = {.red = 0xAA, .green = 0x00, .blue = 0xAA}},
        {.index = 6, .color = {.red = 0xAA, .green = 0x55, .blue = 0x00}},
        {.index = 9, .color = {.red = 0x55, .green = 0x55, .blue = 0xFF}},
        {.index = 15, .color = {.red = 0xFF, .green = 0xFF, .blue = 0xFF}}};

    p.frame_hash = video_frame_hash;
    p.least_frames = 1;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "sound";
    p.about = "two programmed tones, gated on and off at port 61h";
    p.setup.code = sound_code();
    p.setup.step_cap = 400'000;
    p.results = {
        {.what = "port 61h with the gate and data enable set", .value = 0x0003},
        {.what = "port 61h once both are cleared", .value = 0x0000},
        {.what = "the first divisor", .value = first_tone_divisor},
        {.what = "the second divisor", .value = second_tone_divisor}};
    p.exit_code = 0x33;
    p.tone_periods = {first_tone_divisor, second_tone_divisor};
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "keyboard";
    p.about = "INT 16h polled and blocking, echoed through DOS";
    p.setup.code = keyboard_code();
    p.setup.keys = {{.at = first_key_at,
                     .scancode = scancode_a,
                     .action = machine::key_action::down},
                    {.at = shift_key_at,
                     .scancode = scancode_left_shift,
                     .action = machine::key_action::down},
                    {.at = second_key_at,
                     .scancode = scancode_d,
                     .action = machine::key_action::down}};
    p.setup.step_cap = 400'000;
    p.results = {
        {.what = "the polled keystroke", .value = key_a},
        {.what = "the poll loop really spun", .value = 1},
        {.what = "the blocking read's keystroke", .value = key_shifted_d},
        {.what = "40:17, with a shift key still held",
         .value = machine::xt_keyboard::left_shift_mask},
        {.what = "timer ticks accrued during the block", .value = 1}};
    p.exit_code = 0x44;
    p.console = {'a', 'D'};
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "dosfiles";
    p.about = "INT 21h file round-trip, error paths and console output";
    p.setup.code = dosfiles_code();
    p.setup.read_back = {"\\DATA\\NOTE.TXT", "\\SCRATCH.TMP"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "mkdir \\DATA", .value = 1},
        {.what = "create: the first free DOS handle", .value = 5},
        {.what = "bytes written", .value = file_length},
        {.what = "seek to 4 from the start", .value = patch_at},
        {.what = "bytes written over them", .value = patch_length},
        {.what = "close", .value = 1},
        {.what = "reopen: the handle is free again", .value = 5},
        {.what = "seek to the end: the length", .value = file_length},
        {.what = "seek back to the start", .value = 1},
        {.what = "bytes read", .value = file_length},
        {.what = "checksum of what came back", .value = file_checksum()},
        {.what = "a read at the end answers nothing", .value = 1},
        {.what = "close", .value = 1},
        {.what = "open a file that is not there", .value = 0x02},
        {.what = "read from a handle nothing opened", .value = 0x06},
        {.what = "seek from an origin DOS does not have", .value = 0x01},
        {.what = "delete a file that is not there", .value = 0x02},
        {.what = "open one on a drive this machine does not have",
         .value = 0x0F},
        {.what = "create the scratch file", .value = 5},
        {.what = "bytes written to it", .value = patch_length},
        {.what = "close it", .value = 1},
        {.what = "delete it", .value = 1},
        {.what = "and it is gone", .value = 0x02}};
    p.exit_code = 0x55;
    p.console = {'F', 'I', 'L', 'E', 'S', ' ', 'O', 'K'};
    p.files = {{.present = true, .contents = file_contents()},
               {.present = false, .contents = {}}};
    // The whole of what the program asked DOS for by name, in order -
    // including the four refusals, which are the point of the calls
    // that make them (diagnostics.h: a failed open is an answer). The
    // fourth is the one that never reached this list until #121: a name
    // `canonicalize()` refuses outright has no path to report, so it
    // shows as the root with the error that says why.
    p.file_trace = {
        "mkdir \\DATA",
        "create \\DATA\\NOTE.TXT",
        "close \\DATA\\NOTE.TXT",
        "open \\DATA\\NOTE.TXT",
        "close \\DATA\\NOTE.TXT",
        "open \\NOPE.TXT file_not_found",
        "unlink \\NOPE.TXT file_not_found",
        "open \\ invalid_drive",
        "create \\SCRATCH.TMP",
        "close \\SCRATCH.TMP",
        "unlink \\SCRATCH.TMP",
        "open \\SCRATCH.TMP file_not_found",
    };
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "loaded_exe";
    p.about = "an MZ binary with relocations, loaded and run off the VFS";
    p.setup.exe = loaded_exe_file();
    p.setup.exe_path = "\\LOADED.EXE";
    p.setup.step_cap = 100'000;
    p.results = {
        {.what = "paragraphs between the PSP and the image", .value = 0x0010},
        {.what = "the relocated data segment, less CS",
         .value = exe_data_paragraph},
        {.what = "the seed reached through a relocated pointer",
         .value = exe_seed},
        {.what = "the PSP's top-of-memory word",
         .value = static_cast<std::uint16_t>(machine::conventional_ram_size /
                                             machine::paragraph_size)}};
    p.exit_code = 0x66;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "composite";
    p.about = "loader, video, sound, timer, keyboard, files and console";
    p.setup.exe = composite_file();
    p.setup.exe_path = "\\COMPOSIT.EXE";
    p.setup.keys = {{.at = composite_key_at,
                     .scancode = scancode_a,
                     .action = machine::key_action::down}};
    p.setup.read_back = {"\\RUN.LOG"};
    p.setup.step_cap = 400'000;
    p.results = {
        {.what = "paragraphs between the PSP and the image", .value = 0x0010},
        {.what = "the relocated data segment, less CS",
         .value = composite_data_paragraph},
        {.what = "plane 0 at 0, written through the pipeline", .value = 0xAA},
        {.what = "plane 1 in the band", .value = 0xFF},
        {.what = "ticks the hooked INT 1Ch counted",
         .value = composite_wanted_ticks},
        {.what = "40:6C, the BIOS's own count",
         .value = composite_wanted_ticks},
        {.what = "the keystroke the blocking read woke on", .value = key_a},
        {.what = "the DOS handle the log file got", .value = 5},
        {.what = "bytes written to it", .value = 4},
        {.what = "close", .value = 1}};
    p.exit_code = 0x5A;
    p.console = {'a', 'D', 'O', 'N', 'E'};
    p.tone_periods = {first_tone_divisor};
    p.files = {{.present = true, .contents = {'M', '2', 'O', 'K'}}};
    // AAh on all four planes at offset 0 is alternating colour 15 and
    // colour 0 across the first eight pixels; the band is planes 0 and 1
    // only, so colour 3, over the twenty rows starting at offset 1600.
    p.pixels = {{.x = 0, .y = 0, .index = 15},   {.x = 1, .y = 0, .index = 0},
                {.x = 6, .y = 0, .index = 15},   {.x = 7, .y = 0, .index = 0},
                {.x = 0, .y = 39, .index = 0},   {.x = 0, .y = 40, .index = 3},
                {.x = 319, .y = 59, .index = 3}, {.x = 0, .y = 60, .index = 0}};
    constexpr std::size_t band_pixels = 20 * pixels_per_row;
    p.areas = {{.index = 15, .count = 4},
               {.index = 3, .count = band_pixels},
               {.index = 0, .count = pixels_per_frame - 4 - band_pixels}};
    // The mode-set default, untouched: this program never calls AH=10h,
    // so index 3 is code 3 — green and blue primaries, nothing else —
    // which the DAC drives to 00h/AAh/AAh.
    p.palette = {
        {.index = 0, .color = {.red = 0x00, .green = 0x00, .blue = 0x00}},
        {.index = 3, .color = {.red = 0x00, .green = 0xAA, .blue = 0xAA}},
        {.index = 15, .color = {.red = 0xFF, .green = 0xFF, .blue = 0xFF}}};

    p.file_trace = {"create \\RUN.LOG", "close \\RUN.LOG"};
    p.frame_hash = composite_frame_hash;
    p.least_time = ticks{timer_divisor} * composite_wanted_ticks;
    p.least_frames = 1;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "synthetic_boot";
    p.about = "unpacks itself, loads an overlay, and calls the services";
    p.setup.exe = boot_exe_file();
    p.setup.exe_path = "\\BOOT.EXE";
    p.setup.command_tail = boot_command_tail;
    p.setup.files = {
        {.path = boot_overlay_path, .contents = boot_overlay_module()}};
    p.setup.step_cap = 400'000;
    p.results = {
        {.what = "IF, as DOS leaves it at entry", .value = 0x0200},
        {.what = "the command tail's length byte",
         .value = static_cast<std::uint16_t>(boot_command_tail.size())},
        {.what = "its first character",
         .value = static_cast<std::uint16_t>(boot_command_tail.front())},
        {.what = "vector 60h's segment before hooking",
         .value = machine::service::stub_segment},
        {.what = "vector 60h's offset before hooking",
         .value = machine::service::stub_offset(boot_hook_vector)},
        {.what = "vector 60h's segment after hooking",
         .value = machine::image_load_segment},
        {.what = "vector 60h's offset after hooking",
         .value = boot_handler_offset},
        {.what = "the hooked handler ran", .value = boot_handler_mark},
        {.what = "STDOUT is a character device", .value = 0x0082},
        {.what = "STDIN is the documented sink", .value = 0x0084},
        {.what = "the mode the self test left recorded", .value = 0x280D},
        {.what = "the mode after 80x25 text was asked for", .value = 0x5003},
        {.what = "the character under the cursor, off open bus",
         .value = 0xFFFF},
        {.what = "the character cell's height", .value = 14},
        {.what = "rows on screen, less one", .value = 24},
        {.what = "vector 1Fh's segment, as the font pointer",
         .value = machine::service::stub_segment},
        {.what = "and its offset: the top half of the machine's own font",
         .value = static_cast<std::uint16_t>(
             machine::service::font_offset +
             (machine::font::high_half_first * machine::font::glyph_height))},
        {.what = "the mode after 320x200 was programmed", .value = 0x280D},
        {.what = "the active display page", .value = 0},
        {.what = "the cursor, read back out of the BDA", .value = 0x0C05},
        {.what = "the glyph's first line, off plane 0",
         .value = boot_font_glyph[0]},
        {.what = "its second line, one scan line down",
         .value = boot_font_glyph[1]},
        {.what = "retrace both ended and began again", .value = 1},
        {.what = "bytes of the overlay module read in",
         .value = static_cast<std::uint16_t>(boot_overlay_module().size())},
        {.what = "the overlay ran and returned", .value = boot_overlay_mark},
        {.what = "40:6C moved without the program programming anything",
         .value = 1}};
    p.exit_code = 0x77;
    // The overlay, opened by name and closed by handle - the whole of
    // this program's file activity, and the shape a real overlay manager
    // has (M4-G1/#102 found the same two lines under the game).
    p.file_trace = {"open \\OVL.BIN", "close \\OVL.BIN"};
    // The two the video BIOS owes: the text mode this machine records and
    // cannot draw, and the text page nothing answers for.
    p.notices = 2;
    // The first timer tick is one whole channel-0 period away, and the
    // program waits for it (service_floor.h's `post`).
    p.least_time = machine::ticks{0x10000};
    list.push_back(std::move(p));
  }

  {
    // The same program twice, and the seam is the only difference: two
    // entries, so the suite asserts the plain run and the enhanced one
    // each in their own right and neither is inferred from the other.
    machine_program p;
    p.name = "seam_probe";
    p.about = "a self-written seam edits a register and posts a key";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_definition()};
    p.setup.seams = {"probe"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "AX at the edited instruction, seam on",
         .value = probe_edited_ax},
        {.what = "the keystroke the seam posted", .value = probe_keystroke}};
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    // M5-E2 (#173): the automap's key claim, four ways. The three
    // entries that end with an unheard poll claim the same step count,
    // and that equality is the invariant — a program cannot tell a key
    // this seam took from a key nobody typed.
    machine_program p;
    p.name = "automap_probe_quiet";
    p.about = "the poll loop with nothing typed and no seam";
    p.setup.exe = automap_probe_file();
    p.setup.exe_path = "\\AUTOMAP.EXE";
    p.setup.step_cap = 200'000;
    p.results = {{.what = "no key was waiting", .value = 0}};
    p.steps = automap_unheard_steps;
    p.exit_code = 0x89;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "automap_probe_tab_seen";
    p.about = "Tab reaches a program the automap is not watching";
    p.setup.exe = automap_probe_file();
    p.setup.exe_path = "\\AUTOMAP.EXE";
    p.setup.keys = {
        {.at = machine::ticks{0}, .scancode = automap_tab_scancode}};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "the keystroke the program was handed",
                  .value = automap_tab_keystroke}};
    p.exit_code = 0x89;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "automap_probe_tab_claimed";
    p.about = "the automap takes Tab and the program polls as if none came";
    p.setup.exe = automap_probe_file();
    p.setup.exe_path = "\\AUTOMAP.EXE";
    p.setup.keys = {
        {.at = machine::ticks{0}, .scancode = automap_tab_scancode}};
    p.setup.seam_definitions = {&automap_probe_definition()};
    p.setup.seams = {"automap-probe"};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "no key was waiting", .value = 0}};
    p.steps = automap_unheard_steps;
    p.exit_code = 0x89;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "automap_probe_untouched";
    p.about = "the automap on, nothing typed, and the run is the plain one";
    p.setup.exe = automap_probe_file();
    p.setup.exe_path = "\\AUTOMAP.EXE";
    p.setup.seam_definitions = {&automap_probe_definition()};
    p.setup.seams = {"automap-probe"};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "no key was waiting", .value = 0}};
    p.steps = automap_unheard_steps;
    p.exit_code = 0x89;
    list.push_back(std::move(p));
  }

  {
    // M5-E5 (#179), the fog M5-E5f (#263): the explored overlay, four
    // ways. Two pairs, and each pair is one of the seam's two pictures —
    // the window on arrival and the window a step later — with the seam
    // off in one half and on in the other. The step count is claimed by
    // both halves of each pair, which is what says a seam that lays fog
    // over a screen costs the program nothing.
    machine_program p;
    p.name = "explored_probe_quiet";
    p.about = "the overworld painted whole, nothing stood on, and no seam";
    p.setup.exe = explored_probe_file();
    p.setup.exe_path = "\\EXPLORED.EXE";
    p.setup.step_cap = 200'000;
    p.steps = explored_arrived_steps;
    p.areas = {{.index = 15, .count = explored_window_area},
               {.index = 0, .count = pixels_per_frame - explored_window_area}};
    p.least_frames = 1;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "explored_probe_arrived";
    p.about = "on, arrived: the three-by-three around the party and fog";
    p.setup.exe = explored_probe_file();
    p.setup.exe_path = "\\EXPLORED.EXE";
    p.setup.seam_definitions = {&explored_probe_definition()};
    p.setup.seams = {"explored-probe"};
    p.setup.step_cap = 200'000;
    p.steps = explored_arrived_steps;
    constexpr std::size_t clear =
        explored_cell_area * explored_clear_on_arrival;
    p.pixels = {
        // The party's own cell, and the eight around it.
        {.x = explored_pixel_x(2), .y = explored_pixel_y(2), .index = 15},
        {.x = explored_pixel_x(1), .y = explored_pixel_y(1), .index = 15},
        {.x = explored_pixel_x(3), .y = explored_pixel_y(3), .index = 15},
        // The window's outer ring, which the party has not been near.
        {.x = explored_pixel_x(0), .y = explored_pixel_y(0), .index = 0},
        {.x = explored_pixel_x(4), .y = explored_pixel_y(2), .index = 0},
        {.x = explored_pixel_x(2), .y = explored_pixel_y(4), .index = 0},
        // And the pixel one to the left of the window, which the paint
        // never reached and the fog must not either.
        {.x = explored_window_left - 1, .y = explored_pixel_y(2), .index = 0}};
    p.areas = {{.index = 15, .count = clear},
               {.index = 0, .count = pixels_per_frame - clear}};
    p.least_frames = 1;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "explored_probe_walked";
    p.about = "a square walked, with no seam to notice";
    p.setup.exe = explored_probe_file();
    p.setup.exe_path = "\\EXPLORED.EXE";
    p.setup.keys = {
        {.at = machine::ticks{0}, .scancode = explored_walk_scancode}};
    p.setup.step_cap = 200'000;
    p.steps = explored_walked_steps;
    p.areas = {{.index = 15, .count = explored_window_area},
               {.index = 0, .count = pixels_per_frame - explored_window_area}};
    p.least_frames = 1;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "explored_probe_drawn";
    p.about = "on, a square walked: the reveal moves and the fog follows";
    p.setup.exe = explored_probe_file();
    p.setup.exe_path = "\\EXPLORED.EXE";
    p.setup.keys = {
        {.at = machine::ticks{0}, .scancode = explored_walk_scancode}};
    p.setup.seam_definitions = {&explored_probe_definition()};
    p.setup.seams = {"explored-probe"};
    p.setup.step_cap = 200'000;
    p.steps = explored_walked_steps;
    // The party has stepped north to (3, 31), so the window's top-left is
    // (1, 29) and the reveal is columns 1..3 of it by rows 1..4. The
    // corners of that block are checked as pixels, and the row ahead of
    // the party is checked as fog, because a reveal one cell out would
    // otherwise keep the same area.
    constexpr std::size_t clear =
        explored_cell_area * explored_clear_after_a_step;
    p.pixels = {
        {.x = explored_pixel_x(1), .y = explored_pixel_y(1), .index = 15},
        {.x = explored_pixel_x(3), .y = explored_pixel_y(4), .index = 15},
        // The row the window scrolled onto, which nothing has been near.
        {.x = explored_pixel_x(2), .y = explored_pixel_y(0), .index = 0},
        // And its two flanking columns, all the way down.
        {.x = explored_pixel_x(0), .y = explored_pixel_y(4), .index = 0},
        {.x = explored_pixel_x(4), .y = explored_pixel_y(4), .index = 0},
        // A pixel outside the window on each side of it.
        {.x = explored_window_left - 1, .y = explored_pixel_y(2), .index = 0},
        {.x = explored_window_left + 120,
         .y = explored_pixel_y(2),
         .index = 0}};
    p.areas = {{.index = 15, .count = clear},
               {.index = 0, .count = pixels_per_frame - clear}};
    p.least_frames = 1;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    // The trigger, both ways (#161). Pulled: the handler runs once and
    // the program stores the seam's word. On and not pulled: the point
    // is reached, nothing happens, and the result block is the plain
    // machine's — which is the fidelity claim, made where every target
    // runs it.
    machine_program p;
    p.name = "seam_probe_trigger";
    p.about = "a self-written trigger, pulled: it acts once";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_trigger_definition()};
    p.setup.seams = {"probe-trigger"};
    p.setup.pulls = {"probe-trigger"};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "AX at the edited instruction, trigger pulled",
                  .value = probe_edited_ax},
                 {.what = "no keystroke: this seam posts none", .value = 0}};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "seam_probe_trigger_unpulled";
    p.about = "the same trigger, on and never asked: a plain machine";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_trigger_definition()};
    p.setup.seams = {"probe-trigger"};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "AX at the edited instruction, trigger not pulled",
                  .value = probe_plain_ax},
                 {.what = "no keystroke arrived", .value = 0}};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    // The address-free point, both ways (#163). Pulled: it is offered at
    // every step boundary, declines until the program has stored its own
    // first answer, and marks a third result word at the first step
    // after that — so the block says both that the offers happened and
    // that the declines kept the pull waiting. On and not pulled: no
    // offer is ever taken up, and `steps` below says the run is the
    // plain machine's to the step.
    machine_program p;
    p.name = "seam_probe_pull";
    p.about = "a point with no address, pulled: it acts at the first safe step";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_pull_definition()};
    p.setup.seams = {"probe-pull"};
    p.setup.pulls = {"probe-pull"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "AX at the edited instruction: this seam edits no register",
         .value = probe_plain_ax},
        {.what = "no keystroke: this seam posts none", .value = 0},
        {.what = "the mark the address-free point left, once its guard held",
         .value = probe_pulled_mark}};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "seam_probe_pull_unpulled";
    p.about = "the same point, on and never asked: a plain machine";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_pull_definition()};
    p.setup.seams = {"probe-pull"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "AX at the edited instruction", .value = probe_plain_ax},
        {.what = "no keystroke arrived", .value = 0},
        {.what = "and no mark: nobody pulled", .value = 0}};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    // M5-D1 (#169): the seam -> host direction, with a host attached.
    // Two callouts, one at each point, each carrying its own argument,
    // and the run is the plain machine's to the step — a callout costs
    // nothing, because it does not touch the machine.
    machine_program p;
    p.name = "seam_probe_host";
    p.about = "a self-written seam calls two host services, and a host answers";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_host_definition()};
    p.setup.seams = {"probe-host"};
    p.setup.host_services = true;
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "AX at the edited instruction: this seam edits no register",
         .value = probe_plain_ax},
        {.what = "no keystroke: this seam posts none", .value = 0},
        {.what = "the journal callout was served", .value = probe_served_mark},
        {.what = "the automap callout was served", .value = probe_served_mark}};
    p.host_service_calls = {1, 1};
    p.host_service_arguments = {probe_journal_argument, probe_automap_argument};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    // The same seam with **no host attached**, which is the entry that
    // makes the pair worth having. `call_host()` answers false, both
    // marks stay zero, and every count the ABI can be asked for is zero
    // — which is the only way "the callout never reached anybody" is
    // distinguishable from "the callout was served", because in a stream
    // the two look identical (#153).
    machine_program p;
    p.name = "seam_probe_host_unserved";
    p.about = "the same seam with no host attached: both callouts refused";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_host_definition()};
    p.setup.seams = {"probe-host"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "AX at the edited instruction", .value = probe_plain_ax},
        {.what = "no keystroke arrived", .value = 0},
        {.what = "the journal callout was refused", .value = 0},
        {.what = "the automap callout was refused", .value = 0}};
    p.host_service_calls = {0, 0};
    p.host_service_arguments = {0, 0};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "seam_probe_off";
    p.about = "the same program with its seam off: a plain machine";
    p.setup.exe = seam_probe_file();
    p.setup.exe_path = "\\PROBE.EXE";
    p.setup.seam_definitions = {&seam_probe_definition()};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "AX at the edited instruction, seam off",
                  .value = probe_plain_ax},
                 {.what = "no keystroke arrived", .value = 0}};
    p.steps = probe_plain_steps;
    p.exit_code = 0x88;
    list.push_back(std::move(p));
  }

  {
    // M5-D4 (#188): a seam calls a routine of the program's own, twice,
    // with a timer interrupt landing inside each call.
    machine_program p;
    p.name = "call_door";
    p.about = "a seam calls the program's own routine, and comes back";
    p.setup.exe = seam_door_file();
    p.setup.exe_path = "\\DOOR.EXE";
    p.setup.seam_definitions = {&seam_door_definition()};
    p.setup.seams = {"call-door-probe"};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "the deepest word the routine found on its stack",
         .value = door_first_word},
        {.what = "the byte it read through the far pointer",
         .value = door_placed_byte},
        {.what = "calls made, from one arrival", .value = door_calls},
        {.what = "a timer interrupt was taken inside the call", .value = 1}};
    p.exit_code = 0x8B;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "call_door_off";
    p.about = "and with the seam off: the routine is never entered";
    p.setup.exe = seam_door_file();
    p.setup.exe_path = "\\DOOR.EXE";
    p.setup.seam_definitions = {&seam_door_definition()};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "nothing was found on any stack", .value = 0},
        {.what = "and no byte was placed", .value = 0},
        {.what = "no calls were made", .value = 0},
        {.what = "and no call was there to be interrupted", .value = 0}};
    p.exit_code = 0x8B;
    list.push_back(std::move(p));
  }

  {
    // M5-E1 (#172) as M5-E1a (#186) leaves it: the build's own Encamp
    // Fix, offered on this program's command bar and chosen with a
    // keystroke. One entry per way of asking, so the plain run and the
    // enhanced one are each asserted in their own right.
    machine_program p;
    p.name = "encamp_fix";
    p.about = "the Encamp Fix, chosen off the bar: it dials and presses Rest";
    p.setup.exe = seam_camp_file();
    p.setup.exe_path = "\\CAMP.EXE";
    p.setup.seam_definitions = {&seam_camp_definition()};
    p.setup.seams = {"encamp-fix-probe"};
    p.setup.keys = {{.at = machine::ticks{0}, .scancode = camp_fix_scancode}};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "the key the rest screen read", .value = camp_rest_keystroke},
        {.what = "the days the seam dialled", .value = camp_wanted_days},
        {.what = "the hours the program itself dialled, untouched",
         .value = camp_wrapper_hours},
        {.what = "the bar the program drew, with the command on it",
         .value = camp_bar_fixed_length},
        {.what = "the report framed its box, once",
         .value = camp_wanted_frame_calls},
        {.what = "and drew the summary and the one member it left short",
         .value = camp_wanted_string_calls},
        {.what = "the party never left camp", .value = 0},
        {.what = "so nothing had to be held on the screen", .value = 0}};
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    // M5-E1c (#194): the same command, and a rest the game answers by
    // taking the party out of camp. The pass of the menu the report is
    // drawn on never comes, so the fourth point draws it on the way past
    // — and holds it there with the program's own message delay, which
    // is what a box with no command bar under it has instead of a way
    // out.
    machine_program p;
    p.name = "encamp_fix_interrupted";
    p.about = "the same Fix, and a rest the game interrupts: it says so";
    p.setup.exe = seam_camp_file();
    p.setup.exe_path = "\\CAMP.EXE";
    p.setup.command_tail = " X";
    p.setup.seam_definitions = {&seam_camp_definition()};
    p.setup.seams = {"encamp-fix-probe"};
    p.setup.keys = {{.at = machine::ticks{0}, .scancode = camp_fix_scancode}};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "the key the rest screen read", .value = camp_rest_keystroke},
        {.what = "the days the seam dialled", .value = camp_wanted_days},
        {.what = "the hours the program itself dialled, untouched",
         .value = camp_wrapper_hours},
        {.what = "the bar the program drew, with the command on it",
         .value = camp_bar_fixed_length},
        {.what = "the report framed its box on the way out of camp",
         .value = camp_wanted_frame_calls},
        {.what = "and drew the summary and the one member it left short",
         .value = camp_wanted_string_calls},
        {.what = "the party left camp, once", .value = 1},
        {.what = "and the box was held there by the program's own delay",
         .value = 1}};
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "encamp_fix_not_chosen";
    p.about = "the same Fix, offered and not chosen: a plain camp";
    p.setup.exe = seam_camp_file();
    p.setup.exe_path = "\\CAMP.EXE";
    p.setup.seam_definitions = {&seam_camp_definition()};
    p.setup.seams = {"encamp-fix-probe"};
    p.setup.keys = {{.at = machine::ticks{0}, .scancode = camp_other_scancode}};
    p.setup.step_cap = 200'000;
    p.results = {
        {.what = "no rest screen was reached", .value = 0},
        {.what = "and no days were dialled", .value = 0},
        {.what = "the hours the program itself dialled",
         .value = camp_wrapper_hours},
        {.what = "the bar the program drew, with the command on it",
         .value = camp_bar_fixed_length},
        {.what = "and no report was drawn, because none was owed", .value = 0},
        {.what = "nor any line of one", .value = 0},
        {.what = "the party never left camp", .value = 0},
        {.what = "and nothing was held on the screen", .value = 0}};
    p.steps = camp_plain_steps;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  {
    machine_program p;
    p.name = "encamp_fix_off";
    p.about = "and with the Fix off: the same machine, to the step";
    p.setup.exe = seam_camp_file();
    p.setup.exe_path = "\\CAMP.EXE";
    p.setup.seam_definitions = {&seam_camp_definition()};
    p.setup.keys = {{.at = machine::ticks{0}, .scancode = camp_other_scancode}};
    p.setup.step_cap = 200'000;
    p.results = {{.what = "no rest screen was reached", .value = 0},
                 {.what = "and no days were dialled", .value = 0},
                 {.what = "the hours the program itself dialled",
                  .value = camp_wrapper_hours},
                 {.what = "the bar the program drew, which is its own",
                  .value = camp_bar_plain_length},
                 {.what = "and no report, the seam being off", .value = 0},
                 {.what = "nor any line of one", .value = 0},
                 {.what = "the party never left camp", .value = 0},
                 {.what = "and nothing was held on the screen", .value = 0}};
    p.steps = camp_plain_steps;
    p.exit_code = 0x8A;
    list.push_back(std::move(p));
  }

  for (machine_program& p : list) {
    p.setup.result_words = p.results.size();
  }
  return list;
}

// --- The checker --------------------------------------------------------

[[nodiscard]] std::string hex(std::uint64_t value, unsigned digits) {
  constexpr std::string_view alphabet = "0123456789ABCDEF";
  std::string text(digits, '0');
  for (unsigned i = 0; i < digits; ++i) {
    text[digits - 1 - i] = alphabet[(value >> (4U * i)) & 0xFU];
  }
  return text;
}

[[nodiscard]] std::string bytes_as_text(std::span<const std::uint8_t> bytes) {
  std::string text = "\"";
  for (const std::uint8_t byte : bytes) {
    if (byte >= 0x20 && byte < 0x7F) {
      text += static_cast<char>(byte);
    } else {
      text += "\\x" + hex(byte, 2);
    }
  }
  return text + "\"";
}

}  // namespace

const machine::seam_definition& seam_probe_definition() {
  // Everything the definition points at has to outlive every engine it
  // is registered with, so it all lives here: the fingerprint text, the
  // point table, and the definition itself.
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 2> points{
      {{.module = machine::resident_image,
        .offset = probe().edit_offset,
        .run = &probe_edit_ax},
       {.module = machine::resident_image,
        .offset = probe().poll_offset,
        .run = &probe_post_key}}};
  static const machine::seam_definition definition{
      .id = "probe",
      .about = "the test seam: edits AX and posts a keystroke",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

const machine::seam_definition& automap_probe_definition() {
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(automap_probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 2> points{
      {{.module = machine::resident_image,
        .offset = automap_probe().bar_offset,
        .run = automap_bar_handler()},
       {.module = machine::resident_image,
        .offset = automap_probe().poll_offset,
        .run = automap_key_handler()}}};
  static const machine::seam_definition definition{
      .id = "automap-probe",
      .about = "the automap's own key claim, at a made-up address",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

const machine::seam_definition& explored_probe_definition() {
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(explored_probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 3> points{
      {{.module = machine::resident_image,
        .offset = explored_probe().present_offset,
        .run = explored_handler(0)},
       {.module = machine::resident_image,
        .offset = explored_probe().poll_offset,
        .run = explored_handler(1)},
       {.module = machine::resident_image,
        .offset = explored_probe().bar_offset,
        .run = explored_handler(2)}}};
  static const machine::seam_definition definition{
      .id = "explored-probe",
      .about = "the explored overlay's own handlers, at made-up addresses",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

const machine::seam_definition& seam_probe_unreached_definition() {
  // Same fingerprint as the seam above — it is the same program, and
  // that is the point: this seam is available, enable-able and armable
  // exactly as a working one is, and differs from it in nothing a host
  // can see except what `fired` says afterwards.
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 1> points{
      {{.module = machine::resident_image,
        .offset = probe().unreached_offset,
        .run = &probe_never}}};
  static const machine::seam_definition definition{
      .id = "probe-unreached",
      .about = "a seam armed where the program never goes: fires nothing",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

const machine::seam_definition& seam_probe_trigger_definition() {
  // The same program and the same point as `probe`'s first, and the one
  // difference is that nothing happens there until somebody pulls it
  // (#161). Two entries in the list below drive it both ways, so what
  // "on but not asked" costs the machine is a result word on every
  // target rather than a sentence in a header.
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 1> points{
      {{.module = machine::resident_image,
        .offset = probe().edit_offset,
        .run = &probe_edit_ax}}};
  static const machine::seam_definition definition{
      .id = "probe-trigger",
      .about = "the test trigger: edits AX, once, when pulled",
      .fingerprints = fingerprints,
      .points = points,
      .trigger = true};
  return definition;
}

const machine::seam_definition& seam_probe_pull_definition() {
  // The fourth (#163): the same program's fingerprint, a trigger like
  // the one above, and a point with **no address at all** — offered at
  // every step boundary while the pull is outstanding. The pair of
  // entries it drives is the fidelity claim for that mechanism at
  // program scale: pulled, the guard declines its way through the
  // program's first thirty-odd steps and then marks a result word; not
  // pulled, the run is the plain machine's, step for step.
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 1> points{
      {{.module = machine::resident_image,
        .run = &probe_mark_when_ready,
        .at_every_step = true}}};
  static const machine::seam_definition definition{
      .id = "probe-pull",
      .about = "the test trigger with no address: acts at the first safe step",
      .fingerprints = fingerprints,
      .points = points,
      .trigger = true};
  return definition;
}

const machine::seam_definition& seam_probe_host_definition() {
  // The fifth (M5-D1, #169). The same program's fingerprint again, and
  // two points that are reached exactly once each — the register edit
  // and the second store — so that the call counts the ABI hands back
  // are one and one, and a reader checks them against the program
  // rather than against a loop bound.
  //
  // Not a trigger: a callout is not an act on the machine, and there is
  // nothing here for a person to decide the moment of. The two entries
  // this drives differ in one thing only, and it is not the seam — it is
  // whether a host was attached at all.
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(probe().file);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const std::array<machine::seam_point, 2> points{
      {{.module = machine::resident_image,
        .offset = probe().edit_offset,
        .run = &probe_call_journal},
       {.module = machine::resident_image,
        .offset = probe().done_offset,
        .run = &probe_call_automap}}};
  static const machine::seam_definition definition{
      .id = "probe-host",
      .about = "the test seam that calls out: one host service at each point",
      .fingerprints = fingerprints,
      .points = points};
  return definition;
}

const machine::seam_definition& seam_door_definition() {
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(door_file());
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const machine::seam_definition definition{
      .id = "call-door-probe",
      .about = "the test seam that calls a routine of the program's own",
      .fingerprints = fingerprints,
      .points = door_points};
  return definition;
}

const std::vector<std::uint8_t>& seam_door_file() { return door_file(); }

const machine::seam_definition& seam_camp_definition() {
  static const std::string fingerprint = [] {
    const sha256_digest digest = sha256(camp_file());
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    return std::string(hex.data(), sha256_digest::text_length);
  }();
  static const std::array<std::string_view, 1> fingerprints{fingerprint};
  static const machine::seam_definition definition = [] {
    const machine::seam_definition* built_in = nullptr;
    for (const machine::seam_definition& seam : machine::all_seams()) {
      if (seam.id == "encamp-fix") {
        built_in = &seam;
      }
    }
    if (built_in == nullptr) {
      throw std::logic_error("the build carries no encamp-fix seam");
    }
    // Everything but the identity: the points — and so the handler — are
    // the build's own, which is the whole reason this entry is worth
    // running. `points` is a span over a static array in
    // seam_encamp_fix.cpp, so the copy outlives every engine it is
    // registered with.
    machine::seam_definition copy = *built_in;
    copy.id = "encamp-fix-probe";
    copy.about = "the build's Encamp Fix, keyed to the camp stand-in";
    copy.fingerprints = fingerprints;
    return copy;
  }();
  return definition;
}

const std::vector<std::uint8_t>& seam_camp_file() { return camp_file(); }

const std::vector<std::uint8_t>& seam_probe_file() { return probe().file; }

const std::vector<std::uint8_t>& automap_probe_file() {
  return automap_probe().file;
}

const std::vector<std::uint8_t>& explored_probe_file() {
  return explored_probe().file;
}

std::vector<machine_program> all_machine_programs() { return build_all(); }

const machine_program* find_machine_program(std::string_view name) {
  static const std::vector<machine_program> list = build_all();
  for (const machine_program& p : list) {
    if (p.name == name) {
      return &p;
    }
  }
  return nullptr;
}

std::vector<std::string> check_machine_program(const machine_program& expected,
                                               const machine_outcome& got) {
  std::vector<std::string> wrong;
  const auto fail = [&wrong](std::string line) {
    wrong.push_back(std::move(line));
  };

  // Reported first, because a stop tells you what the machine refused to
  // invent, and that is a better opening line than a wrong result word.
  if (got.load_error != machine::loader_error::none) {
    fail("the loader refused the EXE: error " +
         std::to_string(static_cast<unsigned>(got.load_error)));
    return wrong;
  }
  if (got.capped) {
    fail("ran past its step cap of " + std::to_string(expected.setup.step_cap) +
         " without exiting");
  }
  if (!got.exited()) {
    fail("did not exit through DOS: stop reason " +
         std::to_string(static_cast<unsigned>(got.stop.reason)) + " at " +
         hex(got.stop.at, 5) + ", CPU stop reason " +
         std::to_string(static_cast<unsigned>(got.cpu_stop.reason)) +
         " on opcode " + hex(got.cpu_stop.opcode, 2) + " at " +
         hex(got.cpu_stop.cs, 4) + ":" + hex(got.cpu_stop.ip, 4));
  } else if (got.exit_code() != expected.exit_code) {
    fail("exit code " + hex(got.exit_code(), 2) + ", expected " +
         hex(expected.exit_code, 2));
  }

  if (got.notices != expected.notices) {
    fail(std::to_string(got.notices) +
         " touch(es) of an address or a port nothing answers for, expected " +
         std::to_string(expected.notices));
  }
  if (got.device_stops != 0) {
    fail(std::to_string(got.device_stops) + " device fault(s)");
  }

  // What the seams asked of the host (#169), per service. Checked even
  // when both are expected to be zero: a callout nobody wanted is as
  // wrong as a missing one, and zero-with-a-seam-on is the only shape
  // "it never reached anybody" has.
  for (std::size_t i = 0; i < machine::seam_host_service_count; ++i) {
    const auto which = static_cast<machine::seam_host_service>(i);
    if (got.host_service_calls[i] != expected.host_service_calls[i]) {
      fail(std::string(machine::seam_host_service_name(which)) + " served " +
           std::to_string(got.host_service_calls[i]) + " time(s), expected " +
           std::to_string(expected.host_service_calls[i]));
    }
    if (got.host_service_arguments[i] != expected.host_service_arguments[i]) {
      fail(std::string(machine::seam_host_service_name(which)) +
           " last carried " + hex(got.host_service_arguments[i], 8) +
           ", expected " + hex(expected.host_service_arguments[i], 8));
    }
  }
  if (got.underruns != 0) {
    fail(std::to_string(got.underruns) +
         " audio pull(s) ran out of settled time");
  }

  for (std::size_t i = 0; i < expected.results.size(); ++i) {
    const std::uint16_t answer = i < got.results.size() ? got.results[i] : 0;
    if (answer != expected.results[i].value) {
      fail("result " + std::to_string(i) + " (" +
           std::string(expected.results[i].what) + ") is " + hex(answer, 4) +
           ", expected " + hex(expected.results[i].value, 4));
    }
  }

  if (got.console != expected.console) {
    fail("console output " + bytes_as_text(got.console) + ", expected " +
         bytes_as_text(expected.console));
  }

  const std::vector<ticks> periods = tone_periods(got.audio);
  if (periods != expected.tone_periods) {
    std::string line = "tone periods {";
    for (const ticks period : periods) {
      line += " " + std::to_string(period);
    }
    line += " }, expected {";
    for (const ticks period : expected.tone_periods) {
      line += " " + std::to_string(period);
    }
    fail(line + " }");
  }

  {
    // Rendered the same way a host renders it, so a line in a failure
    // here and a line under `--trace` are the same text.
    std::vector<std::string> trace;
    trace.reserve(got.file_events.size());
    for (const machine::file_event& event : got.file_events) {
      std::array<char, machine::dos_path_capacity> path{};
      machine::format_dos_path(event.path, path);
      std::string line = machine::file_action_name(event.what);
      line += ' ';
      line += path.data();
      if (!event.ok()) {
        line += ' ';
        line += machine::vfs_error_name(event.error);
      }
      trace.push_back(std::move(line));
    }
    if (trace != expected.file_trace) {
      std::string line = "the file trace is {";
      for (const std::string& entry : trace) {
        line += " \"" + entry + "\"";
      }
      line += " }, expected {";
      for (const std::string& entry : expected.file_trace) {
        line += " \"" + entry + "\"";
      }
      fail(line + " }");
    }
  }

  for (std::size_t i = 0; i < expected.files.size(); ++i) {
    const std::string_view path = i < expected.setup.read_back.size()
                                      ? expected.setup.read_back[i]
                                      : std::string_view("?");
    const harvested_file& want = expected.files[i];
    const harvested_file empty{};
    const harvested_file& have = i < got.files.size() ? got.files[i] : empty;
    if (have.present != want.present) {
      fail(std::string(path) +
           (want.present ? " is missing" : " still exists"));
      continue;
    }
    if (have.contents != want.contents) {
      fail(std::string(path) + " holds " + bytes_as_text(have.contents) +
           ", expected " + bytes_as_text(want.contents));
    }
  }

  // The picture, before the hash of it. Each of these is a number a
  // reader can derive by hand from what the program wrote and from the
  // rules in ega.h, renderer.h and int10.h; the hash below only says
  // that the rest of the frame did not move.
  for (const pixel_probe& probe : expected.pixels) {
    const std::size_t at =
        static_cast<std::size_t>(probe.y) * machine::frame_width + probe.x;
    const std::uint8_t index =
        at < got.frame_pixels.size() ? got.frame_pixels[at] : 0xFFU;
    if (index != probe.index) {
      fail("pixel (" + std::to_string(probe.x) + "," + std::to_string(probe.y) +
           ") is colour " + std::to_string(index) + ", expected " +
           std::to_string(probe.index));
    }
  }

  for (const area_probe& probe : expected.areas) {
    std::size_t count = 0;
    for (const std::uint8_t index : got.frame_pixels) {
      if (index == probe.index) {
        ++count;
      }
    }
    if (count != probe.count) {
      fail(std::to_string(count) + " pixel(s) of colour " +
           std::to_string(probe.index) + ", expected " +
           std::to_string(probe.count));
    }
  }

  for (const palette_probe& probe : expected.palette) {
    const machine::rgb color = probe.index < got.frame_palette.size()
                                   ? got.frame_palette[probe.index]
                                   : machine::rgb{};
    if (!(color == probe.color)) {
      fail("palette entry " + std::to_string(probe.index) + " is " +
           hex(color.red, 2) + "/" + hex(color.green, 2) + "/" +
           hex(color.blue, 2) + ", expected " + hex(probe.color.red, 2) + "/" +
           hex(probe.color.green, 2) + "/" + hex(probe.color.blue, 2));
    }
  }

  if (expected.frame_hash != 0 && got.frame_hash != expected.frame_hash) {
    fail("frame hash " + hex(got.frame_hash, 16) + ", expected " +
         hex(expected.frame_hash, 16));
  }
  if (got.frames < expected.least_frames) {
    fail(std::to_string(got.frames) + " frame(s) composed, expected at least " +
         std::to_string(expected.least_frames));
  }
  if (got.time < expected.least_time) {
    fail("took " + std::to_string(got.time) +
         " ticks of virtual time, which is less than the " +
         std::to_string(expected.least_time) + " the program asked for");
  }
  if (expected.steps != 0 && got.steps != expected.steps) {
    fail("took " + std::to_string(got.steps) + " steps, expected exactly " +
         std::to_string(expected.steps) +
         " — the entries that claim this number claim it together, and one"
         " of them costing the machine something is what that is for");
  }

  return wrong;
}

void PrintTo(const machine_program& p, std::ostream* os) { *os << p.name; }

}  // namespace amberfolio::programs
