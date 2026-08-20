// SPDX-License-Identifier: AGPL-3.0-only
//
// Seven programs, written in the assembly listing above each builder and
// hand-encoded beneath it — programs.cpp's own convention, one layer out.
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

#include <cstddef>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

#include "amberfolio/machine/service_floor.h"
#include "programs/assembler.h"

namespace amberfolio::programs {
namespace {

using machine::ticks;

// --- The idioms ---------------------------------------------------------

/// Register numbers as the encoding numbers them, for the ModRM byte the
/// result store builds. Only the ones a program below actually answers
/// with are named.
constexpr unsigned reg_ax = 0;
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

// --- The MZ container ---------------------------------------------------
//
// A second copy of what tests/core/machine/loader_test.cpp builds, and
// deliberately so: that one lives inside the GoogleTest rig, which does
// not build under Emscripten, and this directory may not depend on it
// (machine_harness.h). The format is 28 fixed bytes and a relocation
// table; sharing it would cost a new header for less than it saves.

struct reloc_entry {
  std::uint16_t offset{};
  std::uint16_t segment{};
};

struct exe_spec {
  std::uint16_t initial_cs{};
  std::uint16_t initial_ip{};
  std::uint16_t initial_ss{};
  std::uint16_t initial_sp{};
  std::uint16_t min_alloc{};
  std::vector<reloc_entry> relocations;
  std::vector<std::uint8_t> image;
};

void put16(std::vector<std::uint8_t>& bytes, std::size_t at,
           std::uint16_t value) {
  bytes[at] = static_cast<std::uint8_t>(value);
  bytes[at + 1] = static_cast<std::uint8_t>(value >> 8U);
}

/// A complete, well-formed MZ file: the fixed header, the relocation
/// table immediately after it, and the image after that, with every
/// length field computed from the pieces actually given — the last-page
/// rule included.
[[nodiscard]] std::vector<std::uint8_t> build_exe(const exe_spec& spec) {
  constexpr std::uint16_t table_offset = machine::mz::encoded_size;

  const auto reloc_count = static_cast<std::uint16_t>(spec.relocations.size());
  const std::uint32_t table_bytes = static_cast<std::uint32_t>(reloc_count) *
                                    machine::mz::relocation_entry_size;
  const auto header_paragraphs = static_cast<std::uint16_t>(
      (table_offset + table_bytes + machine::paragraph_size - 1) /
      machine::paragraph_size);
  const std::uint32_t header_size =
      static_cast<std::uint32_t>(header_paragraphs) * machine::paragraph_size;

  const auto total =
      static_cast<std::uint32_t>(header_size + spec.image.size());
  const auto last_page =
      static_cast<std::uint16_t>(total % machine::mz::page_size);
  const auto pages = static_cast<std::uint16_t>(total / machine::mz::page_size +
                                                (last_page != 0 ? 1 : 0));

  std::vector<std::uint8_t> file(header_size + spec.image.size(), 0);
  file[0] = 'M';
  file[1] = 'Z';
  put16(file, 0x02, last_page);
  put16(file, 0x04, pages);
  put16(file, 0x06, reloc_count);
  put16(file, 0x08, header_paragraphs);
  put16(file, 0x0A, spec.min_alloc);
  put16(file, 0x0C, 0xFFFF);  // MAXALLOC: never consulted (loader.h).
  put16(file, 0x0E, spec.initial_ss);
  put16(file, 0x10, spec.initial_sp);
  put16(file, 0x12, 0);  // checksum: nothing reads it.
  put16(file, 0x14, spec.initial_ip);
  put16(file, 0x16, spec.initial_cs);
  put16(file, 0x18, table_offset);
  put16(file, 0x1A, 0);  // overlay number.

  for (std::size_t i = 0; i < spec.relocations.size(); ++i) {
    const std::size_t at =
        table_offset + i * machine::mz::relocation_entry_size;
    put16(file, at, spec.relocations[i].offset);
    put16(file, at + 2, spec.relocations[i].segment);
  }
  for (std::size_t i = 0; i < spec.image.size(); ++i) {
    file[header_size + i] = spec.image[i];
  }
  return file;
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

  // --- Created, written, deleted ---
  mov_dx_offset(a, "scratchpath");
  a.db({0x31, 0xC9});
  int21(a, 0x3C);
  answer_or_zero(a, "scratch_made");
  store(a, 17, reg_ax);
  a.db({0x89, 0xC3});

  mov_dx_offset(a, "patch");
  a.db({0xB9});
  a.dw(patch_length);
  int21(a, 0x40);
  answer_or_zero(a, "scratch_written");
  store(a, 18, reg_ax);

  int21(a, 0x3E);
  succeeded(a, "scratch_closed");
  store(a, 19, reg_ax);

  mov_dx_offset(a, "scratchpath");
  int21(a, 0x41);
  succeeded(a, "scratch_deleted");
  store(a, 20, reg_ax);

  mov_dx_offset(a, "scratchpath");
  mov_ax(a, 0x3D00);
  a.db({0xCD, 0x21});
  error_or_zero(a, "scratch_gone");
  store(a, 21, reg_ax);

  mov_dx_offset(a, "banner");
  int21(a, 0x09);

  exit_with(a, 0x55);

  // --- The program's own data ---
  asciz(a, "dirpath", "\\DATA");
  asciz(a, "filepath", "\\DATA\\NOTE.TXT");
  asciz(a, "nopath", "\\NOPE.TXT");
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

constexpr std::uint64_t video_frame_hash = 0x23EE1B0F7C12E66EULL;
constexpr std::uint64_t composite_frame_hash = 0x280E6B18E8FA79B6ULL;

/// Every program's entry, built by assignment rather than as one
/// designated-initializer aggregate.
///
/// `-Wextra` on a current Clang wants every field of an aggregate named
/// once any of them is, and spelling `.exe = {}` on the five programs
/// that are not EXEs — and `.tone_periods = {}` on the five that are
/// silent — would be noise written for a diagnostic rather than for a
/// reader. What a program leaves unset is empty, and empty is asserted
/// (machine_programs.h), so nothing is being waived here.
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

    // The palette INT 10h's mode set installs, through the EGA DAC's own
    // two-bits-a-channel scheme (ega.h): code 5 is AAh/00h/AAh, code 20
    // — the CGA-compatibility brown at index 6 — is AAh/55h/00h, code 57
    // at index 9 is 55h/55h/FFh, code 63 is white. Index 1 is not the
    // default 1 at all but the 2Ah this program set through AH=10h,
    // which is 55h/AAh/55h.
    p.palette = {
        {.index = 0, .color = {.red = 0x00, .green = 0x00, .blue = 0x00}},
        {.index = 1, .color = {.red = 0x55, .green = 0xAA, .blue = 0x55}},
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
        {.what = "create the scratch file", .value = 5},
        {.what = "bytes written to it", .value = patch_length},
        {.what = "close it", .value = 1},
        {.what = "delete it", .value = 1},
        {.what = "and it is gone", .value = 0x02}};
    p.exit_code = 0x55;
    p.console = {'F', 'I', 'L', 'E', 'S', ' ', 'O', 'K'};
    p.files = {{.present = true, .contents = file_contents()},
               {.present = false, .contents = {}}};
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

    p.frame_hash = composite_frame_hash;
    p.least_time = ticks{timer_divisor} * composite_wanted_ticks;
    p.least_frames = 1;
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

  if (got.notices != 0) {
    fail(std::to_string(got.notices) +
         " touch(es) of an address or a port nothing answers for");
  }
  if (got.device_stops != 0) {
    fail(std::to_string(got.device_stops) + " device fault(s)");
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

  return wrong;
}

void PrintTo(const machine_program& p, std::ostream* os) { *os << p.name; }

}  // namespace amberfolio::programs
