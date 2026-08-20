// SPDX-License-Identifier: AGPL-3.0-only
//
// Writes the directory a person points the desktop host at when they want
// to look at it and listen to it: two programs, both self-written, both
// staged as real MZ files the host's own loader has to get right.
//
//     make_demo_disk <dir>
//     amberfolio <dir> COMPOSIT.EXE --scale 3
//     amberfolio <dir> DEMO.EXE --scale 3
//
// It exists because of #80. Everything between the machine and the window
// — the texture upload, the integer scaling, the audio device, the step
// from an SDL key event to a posted scan code — had been compiled on
// every desktop target and run on none, and the reason it had not been
// run is that there was nothing to run: the smoke disk next door holds
// one program that prints a line and exits before a frame is ever
// composed. Two commands is what settling that ought to cost.
//
// COMPOSIT.EXE is M2-T1's composite program (#56), byte for byte the one
// the test suite asserts and the one the wasm dev page embeds — asked for
// by name rather than rebuilt here, so the thing a person looks at in a
// window is the thing CI checked. It draws, it plays a short tone through
// the timer wait, then it blocks on the keyboard: press any key and it
// echoes the character, writes \RUN.LOG and exits with code 90.
//
// DEMO.EXE is written below and is for the senses rather than for a
// checker. Sixteen colour bars, a 440 Hz tone that plays until you stop
// it, and an echo loop, so "picture on screen, tone audible, keys
// echoing" is one glance and one listen instead of a 20-millisecond blip
// nobody is sure they heard. Escape stops the tone and exits.
//
// A generator rather than committed .EXE files, for make_smoke_disk.cpp's
// reason: a checked-in binary is exactly the kind of opaque blob the
// content guard exists to keep out (CONTRIBUTING.md). Every byte here is
// ours, and the listing beside each instruction is what makes that
// checkable by eye.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <vector>

#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/speaker.h"
#include "programs/assembler.h"
#include "programs/exe.h"
#include "programs/machine_programs.h"

namespace {

using namespace amberfolio;

/// The sequencer's index port and its map mask register — the one EGA
/// register DEMO.EXE touches, because a map mask of `c` and a byte of FFh
/// is the whole of "make these eight pixels colour c" (ega.h).
constexpr std::uint16_t seq_index_port = 0x3C4;
constexpr std::uint8_t seq_map_mask = 0x02;

/// Port 61h, narrowed to the byte an OUT immediate can name.
constexpr auto speaker_control_port =
    static_cast<std::uint8_t>(machine::speaker_port);

constexpr std::uint8_t pit_command_port = 0x43;
constexpr std::uint8_t pit_channel2_port = 0x42;

/// Channel 2, both divisor bytes, mode 3 (square wave), binary.
constexpr std::uint8_t pit_channel2_square = 0xB6;

/// 1,193,182 / 2712 = 440.0 Hz, near enough that the difference is four
/// thousandths of a semitone. Concert A because a tone you can name is a
/// tone you can tell from a tone that is merely present.
constexpr std::uint16_t tone_divisor = 2712;

/// Sixteen bars down a 200-line screen: twelve rows each, and the eight
/// rows left over stay black at the bottom, which is where the last bar
/// ends rather than a mistake.
constexpr std::uint16_t bar_rows = 12;
constexpr std::uint16_t row_bytes = machine::frame_width / 8;
constexpr std::uint16_t bar_bytes = bar_rows * row_bytes;

/// DEMO.EXE.
///
///         mov  ax, 000Dh          ; B8 0D 00
///         int  10h                ; CD 10      ; 320x200x16, planes clear
///         mov  ax, 0A000h         ; B8 00 A0
///         mov  es, ax             ; 8E C0
///         xor  di, di             ; 31 FF
///         cld                     ; FC
///         xor  bl, bl             ; 30 DB      ; the colour index
///         mov  si, 16             ; BE 10 00   ; bars left to draw
/// bar:    mov  dx, 3C4h           ; BA C4 03
///         mov  al, 02h            ; B0 02      ; map mask
///         out  dx, al             ; EE
///         inc  dx                 ; 42
///         mov  al, bl             ; 88 D8
///         out  dx, al             ; EE         ; only bl's planes take a write
///         mov  cx, 480            ; B9 E0 01   ; 12 rows of 40 bytes
///         mov  al, 0FFh           ; B0 FF
///         rep  stosb              ; F3 AA      ; every pixel of them colour bl
///         inc  bl                 ; FE C3
///         dec  si                 ; 4E
///         jnz  bar                ; 75 xx
///
///         mov  al, 0B6h           ; B0 B6      ; channel 2, mode 3
///         out  43h, al            ; E6 43
///         mov  ax, 2712           ; B8 98 0A
///         out  42h, al            ; E6 42      ; divisor, low byte
///         mov  al, ah             ; 88 E0
///         out  42h, al            ; E6 42      ; and high
///         mov  al, 03h            ; B0 03      ; gate on, speaker on
///         out  61h, al            ; E6 61      ; and it plays from here
///
/// echo:   mov  ah, 00h            ; B4 00
///         int  16h                ; CD 16      ; halts until a key arrives
///         cmp  al, 1Bh            ; 3C 1B      ; Escape
///         je   done               ; 74 xx
///         mov  dl, al             ; 88 C2
///         mov  ah, 02h            ; B4 02
///         int  21h                ; CD 21      ; and out to the console
///         jmp  echo               ; EB xx
/// done:   mov  al, 00h            ; B0 00
///         out  61h, al            ; E6 61      ; the tone stops
///         mov  ax, 4C00h          ; B8 00 4C
///         int  21h                ; CD 21      ; exit 0
///
/// No relocations: the one segment it loads is A000h, which is where the
/// video window is on every machine and not a thing the loader relocates.
[[nodiscard]] std::vector<std::uint8_t> demo_file() {
  programs::assembler a;

  a.db({0xB8, 0x0D, 0x00, 0xCD, 0x10});
  a.db({0xB8, 0x00, 0xA0, 0x8E, 0xC0});
  a.db({0x31, 0xFF, 0xFC, 0x30, 0xDB});
  a.db({0xBE});
  a.dw(machine::palette_entries);

  a.label("bar");
  a.db({0xBA});
  a.dw(seq_index_port);
  a.db({0xB0, seq_map_mask, 0xEE, 0x42, 0x88, 0xD8, 0xEE});
  a.db({0xB9});
  a.dw(bar_bytes);
  a.db({0xB0, 0xFF, 0xF3, 0xAA});
  a.db({0xFE, 0xC3, 0x4E});
  a.jump(0x75, "bar");

  a.db({0xB0, pit_channel2_square, 0xE6, pit_command_port});
  a.db({0xB8});
  a.dw(tone_divisor);
  a.db({0xE6, pit_channel2_port, 0x88, 0xE0, 0xE6, pit_channel2_port});
  a.db({0xB0, 0x03, 0xE6, speaker_control_port});

  a.label("echo");
  a.db({0xB4, 0x00, 0xCD, 0x16});
  a.db({0x3C, 0x1B});
  a.jump(0x74, "done");
  a.db({0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21});
  a.jump(0xEB, "echo");

  a.label("done");
  a.db({0xB0, 0x00, 0xE6, speaker_control_port});
  a.db({0xB8, 0x00, 0x4C, 0xCD, 0x21});

  return programs::build_exe({.initial_cs = 0,
                              .initial_ip = 0,
                              .initial_ss = 0,
                              .initial_sp = 0x0F00,
                              .min_alloc = 0x0100,
                              .relocations = {},
                              .image = a.assemble()});
}

[[nodiscard]] bool write_file(const std::filesystem::path& where,
                              const std::vector<std::uint8_t>& bytes) {
  std::ofstream out(where, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

}  // namespace

int main(int argc, char** argv) try {
  if (argc != 2) {
    std::fprintf(stderr, "usage: make_demo_disk <dir>\n");
    return EXIT_FAILURE;
  }

  std::error_code ec;
  const std::filesystem::path root(argv[1]);
  std::filesystem::create_directories(root, ec);
  if (ec) {
    std::fprintf(stderr, "cannot create %s\n", root.string().c_str());
    return EXIT_FAILURE;
  }

  const programs::machine_program* composite =
      programs::find_machine_program("composite");
  if (composite == nullptr || composite->setup.exe.empty()) {
    // Not a run-time condition: the suite next door names this program
    // and asserts it. If it is gone, the two are out of step and saying
    // so is worth more than writing one file and calling it a disk.
    std::fprintf(stderr, "make_demo_disk: there is no composite program\n");
    return EXIT_FAILURE;
  }

  // COMPOSIT.EXE, not COMPOSITE.EXE: DOS names are eight and three, and
  // the VFS says so rather than truncating for us (vfs.h).
  if (!write_file(root / "COMPOSIT.EXE", composite->setup.exe) ||
      !write_file(root / "DEMO.EXE", demo_file())) {
    std::fprintf(stderr, "make_demo_disk: cannot write into %s\n",
                 root.string().c_str());
    return EXIT_FAILURE;
  }

  std::printf("demo disk: %s\n", root.string().c_str());
  return EXIT_SUCCESS;
} catch (const std::exception& e) {
  // Same reason as the host's own main: this allocates, and an escaping
  // exception would fail the test with no explanation of what happened.
  std::fprintf(stderr, "make_demo_disk: %s\n", e.what());
  return EXIT_FAILURE;
}
