// SPDX-License-Identifier: AGPL-3.0-only
//
// int10.h has the design; this is the mode-set table and the dispatch
// over AH.

#include "amberfolio/machine/int10.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/service_floor.h"

namespace amberfolio::machine {
namespace {

// --- Ports, named the way a program addressing the hardware would ------

constexpr std::uint16_t sequencer_index_port = 0x3C4;
constexpr std::uint16_t sequencer_data_port = 0x3C5;
constexpr std::uint16_t graphics_index_port = 0x3CE;
constexpr std::uint16_t graphics_data_port = 0x3CF;
constexpr std::uint16_t attribute_port = 0x3C0;
constexpr std::uint16_t status_port = 0x3DA;

/// Attribute controller index 11h: overscan. ega.h names the same fact;
/// repeated here because this is the caller's side of the port protocol,
/// not the device's.
constexpr std::uint8_t attribute_overscan_index = 0x11;

/// The one mode this machine's write pipeline and renderer understand
/// (PLAN.md §9).
constexpr std::uint8_t mode_0dh = 0x0D;

/// Mode 0Dh's geometry: 320 pixels across an 8-dot character clock is 40
/// columns, and the character box is 8 scan lines tall. AH=0Fh and
/// AH=11h report them, out of the BDA the mode set writes them into.
constexpr std::uint16_t mode_0d_columns = 40;
constexpr std::uint16_t mode_0d_points = 8;

/// The 80x25 colour text mode a program of the era passes through on its
/// way to graphics. This machine cannot display it — int10.h's "The
/// modes this machine has, and the one it only records" is the argument.
constexpr std::uint8_t mode_03h = 0x03;

/// Mode 03h's geometry, for the BDA's benefit: 80 columns in a
/// 14-scan-line character box, which is what an EGA driving an enhanced
/// display uses. Nothing here draws with either number.
constexpr std::uint16_t mode_03_columns = 80;
constexpr std::uint16_t mode_03_points = 14;

/// Rows on screen. The same 25 in both modes this file knows about.
constexpr std::uint8_t screen_rows = 25;

/// Where the colour text page lives on a PC. Nothing in this machine
/// claims it — memory_map.h leaves B0000-BFFFF unclaimed on purpose — so
/// a read of it is open bus and says so once. AH=08h below is the one
/// function that touches it, and its comment is why that is the honest
/// answer rather than an evasion.
constexpr std::uint16_t text_page_segment = 0xB800;

/// The two font *pointer* vectors AH=11h AL=30h can answer for. Neither
/// is an entry point: INT 1Fh holds the address of the graphics-mode
/// glyphs for characters 128-255, and INT 43h the address of the current
/// character generator. A program stores a far pointer there and the
/// BIOS reads it back; nothing ever executes an INT to either.
constexpr std::uint8_t graphics_font_vector = 0x1F;
constexpr std::uint8_t alternate_font_vector = 0x43;

/// One register write: an index, and the value that goes with it.
struct register_write {
  std::uint8_t index;
  std::uint8_t value;
};

/// The sequencer's mode-0Dh table. Only the map mask (index 02h) is
/// load-bearing — see int10.h's top comment — set to 0Fh so a program's
/// writes after mode-set actually reach all four planes. The rest match
/// ordinary EGA graphics-mode practice (reset released, 8-dot character
/// clock, sequential — not chain-4 — addressing) but this device stores
/// them inertly (ega.h), so their values matter to a test's assertions
/// and not to the machine's behaviour.
constexpr std::array<register_write, 5> mode_0d_sequencer{{
    {.index = 0x00, .value = 0x03},
    {.index = 0x01, .value = 0x01},
    {.index = 0x02, .value = 0x0F},
    {.index = 0x03, .value = 0x00},
    {.index = 0x04, .value = 0x06},
}};

/// The graphics controller's mode-0Dh table. Every value here already
/// matches this device's own reset() defaults (ega.cpp) — write mode 0,
/// read mode 0, no rotate, copy function, bit mask FF — except the
/// Miscellaneous register (06h), which reset() zeroes and this table sets
/// to 03h: bit 0 selects graphics mode, bits 2:1 select the A0000-AFFFF
/// 64K memory map — the field values ega.h documents for that register.
/// Written explicitly and not left to reset()'s defaults, because a real
/// mode-set genuinely writes the whole table and doing the same here
/// means this function does not depend on what state the device was left
/// in.
constexpr std::array<register_write, 9> mode_0d_graphics{{
    {.index = 0x00, .value = 0x00},
    {.index = 0x01, .value = 0x00},
    {.index = 0x02, .value = 0x00},
    {.index = 0x03, .value = 0x00},
    {.index = 0x04, .value = 0x00},
    {.index = 0x05, .value = 0x00},
    {.index = 0x06, .value = 0x03},
    {.index = 0x07, .value = 0x00},
    {.index = 0x08, .value = 0xFF},
}};

/// The standard 16-colour EGA/CGA-compatible default palette (int10.h's
/// top comment). Index *i* of this array is the value AH=00h writes to
/// palette register *i*; the discontinuity at index 6 (20, not 6) and
/// indices 8-15 (56-63, not 8-15) is the historical CGA
/// backward-compatibility quirk every EGA/VGA hardware reference
/// documents, not a typo.
constexpr std::array<std::uint8_t, ega::palette_register_count>
    mode_0d_default_palette{0,  1,  2,  3,  4,  5,  20, 7,
                            56, 57, 58, 59, 60, 61, 62, 63};

/// The attribute controller's index/data protocol (ega.h): a status read
/// resets the flip-flop, then two writes to the one port load the index
/// and the data. Every AH=10h write and the palette half of mode-set go
/// through this.
void write_attribute_register(machine& box, std::uint8_t index,
                              std::uint8_t value) {
  static_cast<void>(box.read_port8(status_port));
  box.write_port8(attribute_port, index);
  box.write_port8(attribute_port, value);
}

/// AH=00h AL=0Dh: program the sequencer, the graphics controller and the
/// palette for 320x200x16, and tell the machine a mode is now active
/// (machine.h's "Video mode discipline").
void set_mode_0d(machine& box) {
  for (const register_write& reg : mode_0d_sequencer) {
    box.write_port8(sequencer_index_port, reg.index);
    box.write_port8(sequencer_data_port, reg.value);
  }
  for (const register_write& reg : mode_0d_graphics) {
    box.write_port8(graphics_index_port, reg.index);
    box.write_port8(graphics_data_port, reg.value);
  }
  for (std::size_t i = 0; i < mode_0d_default_palette.size(); ++i) {
    write_attribute_register(box, static_cast<std::uint8_t>(i),
                             mode_0d_default_palette[i]);
  }
  write_attribute_register(box, attribute_overscan_index, 0x00);

  box.note_video_mode_set();
}

/// AH=10h: the palette register set PLAN.md §3 names, plus the trivial
/// overscan set the mode-set path above already uses. AL=00h: BL =
/// register 0-15, BH = colour. AL=01h: BH = overscan colour. Both are the
/// documented public BIOS convention (int10.h's top comment) and both
/// mask nothing here — the device masks to its own register widths
/// (ega.cpp), and forwarding the raw byte lets an out-of-range BL reach
/// the device's own refusal instead of this file silently reinterpreting
/// it.
void set_palette(machine& box) {
  const cpu::registers& regs = box.processor().regs();
  const std::uint8_t sub = regs.get(cpu::reg8::al);

  if (sub == 0x00) {
    write_attribute_register(box, regs.get(cpu::reg8::bl),
                             regs.get(cpu::reg8::bh));
    return;
  }
  if (sub == 0x01) {
    write_attribute_register(box, attribute_overscan_index,
                             regs.get(cpu::reg8::bh));
    return;
  }

  box.stop_unsupported_request(regs[cpu::reg16::ax]);
}

// --- The BIOS data area, the way a real video BIOS keeps it -------------
//
// Every function below reads and writes the block at 40:49 rather than
// keeping a copy of its own (service_floor.h's `bda` namespace lays it
// out). That is not tidiness: programs of the era read those addresses
// directly instead of paying for an INT, so the block has to be right
// whether or not anyone calls AH=0Fh — and the moment there are two
// copies of the current mode, one of them is wrong.

[[nodiscard]] std::uint8_t bda_byte(machine& box, std::uint16_t at) {
  return box.processor().read_byte(bda::segment, at);
}

void set_bda_byte(machine& box, std::uint16_t at, std::uint8_t value) {
  box.processor().write_byte(bda::segment, at, value);
}

[[nodiscard]] std::uint16_t bda_word(machine& box, std::uint16_t at) {
  return box.processor().read_word(bda::segment, at);
}

void set_bda_word(machine& box, std::uint16_t at, std::uint16_t value) {
  box.processor().write_word(bda::segment, at, value);
}

/// What a mode set records: the block a program reads back, plus the
/// cursor and the page, which a real mode set puts at the top left of
/// page 0 along with everything else it clears.
void record_mode(machine& box, std::uint8_t mode, std::uint16_t columns,
                 std::uint16_t points) {
  set_bda_byte(box, bda::video_mode, mode);
  set_bda_word(box, bda::video_columns, columns);
  set_bda_byte(box, bda::video_rows_minus_one,
               static_cast<std::uint8_t>(screen_rows - 1));
  set_bda_word(box, bda::character_points, points);
  set_bda_word(box, bda::cursor_position, 0);
  set_bda_byte(box, bda::video_active_page, 0);
}

/// AH=0Fh: report the current mode, out of the block the mode set wrote.
/// AL is the mode, AH the character columns, BH the active display page.
void get_mode(machine& box) {
  cpu::registers& regs = box.processor().regs();
  regs.set(cpu::reg8::al, bda_byte(box, bda::video_mode));
  regs.set(cpu::reg8::ah,
           static_cast<std::uint8_t>(bda_word(box, bda::video_columns)));
  regs.set(cpu::reg8::bh, bda_byte(box, bda::video_active_page));
}

/// AH=00h: set a video mode. 0Dh is programmed for real; 03h is recorded
/// and reported and nothing else; everything else is refused. int10.h's
/// "The modes this machine has, and the one it only records" is where
/// that three-way split is argued.
void set_mode(machine& box) {
  cpu::registers& regs = box.processor().regs();
  const std::uint8_t mode = regs.get(cpu::reg8::al);

  if (mode == mode_0dh) {
    set_mode_0d(box);
    record_mode(box, mode_0dh, mode_0d_columns, mode_0d_points);
    return;
  }
  if (mode == mode_03h) {
    record_mode(box, mode_03h, mode_03_columns, mode_03_points);
    box.notice_video_mode(mode_03h);
    return;
  }

  box.stop_unsupported_request(regs[cpu::reg16::ax]);
}

/// AH=05h: select the active display page. There is one page here — mode
/// 0Dh's 32 KiB fills the planes this device has, and there is no CRTC
/// start address to point at a second one (ega.h; "no CRTC" is the rule
/// #47 set). Page 0 is accepted and changes nothing; anything else is
/// refused rather than quietly treated as page 0, because a program
/// drawing on a page this machine would never show is worth stopping
/// for.
void set_page(machine& box) {
  cpu::registers& regs = box.processor().regs();
  if (regs.get(cpu::reg8::al) != 0) {
    box.stop_unsupported_request(regs[cpu::reg16::ax]);
    return;
  }
  set_bda_byte(box, bda::video_active_page, 0);
}

/// AH=08h: the character and attribute under the cursor.
///
/// In a text mode the BIOS reads the two bytes the cursor points at in
/// the display page, so that is exactly what happens here: B800:0 plus
/// the cursor's own offset, through the bus, as ordinary reads. This
/// machine has nothing at B8000, so they float high and the machine
/// reports a touch of nothing once. FFFFh is not an invented answer —
/// it is what this hardware returns — and the notice is the line that
/// says why.
///
/// In a graphics mode there is no character to read: a real BIOS matches
/// the pixels under the cursor against its character generator, and this
/// machine has no character generator at all (AH=11h below says why). So
/// that is a refusal rather than an FFFF, because the difference between
/// "nothing answered the bus" and "there is no such operation here" is
/// the difference between two different worklist lines.
void read_character(machine& box) {
  cpu::registers& regs = box.processor().regs();
  if (regs.get(cpu::reg8::bh) != 0 ||
      bda_byte(box, bda::video_mode) != mode_03h) {
    box.stop_unsupported_request(regs[cpu::reg16::ax]);
    return;
  }

  const std::uint16_t cursor = bda_word(box, bda::cursor_position);
  const auto column = static_cast<std::uint16_t>(cursor & 0xFFu);
  const auto row = static_cast<std::uint16_t>(cursor >> 8u);
  const auto offset = static_cast<std::uint16_t>(
      (row * bda_word(box, bda::video_columns) + column) * 2u);

  cpu::processor& cpu = box.processor();
  regs.set(cpu::reg8::al, cpu.read_byte(text_page_segment, offset));
  regs.set(
      cpu::reg8::ah,
      cpu.read_byte(text_page_segment, static_cast<std::uint16_t>(offset + 1)));
}

/// AH=11h AL=30h: where a character generator lives, and how tall its
/// characters are.
///
/// BH picks which one, and two of the eight are answerable here:
///
///   * **BH=00h and BH=01h** ask for the *vectors* 1Fh and 43h, the font
///     pointers a program supplies for itself. They are read straight
///     out of the interrupt vector table, which is where they live and
///     where a program that set one put it. Nothing is invented, and
///     nothing is claimed about what they point at.
///   * **BH=02h through 07h** ask for a font in the video ROM. This
///     machine has no video ROM: its BIOS is native code rather than an
///     image (service_floor.h), and a font is picture data, which is not
///     something this project ships. They are refused, so that a program
///     that genuinely needs ROM glyphs appears in the boot log instead of
///     being handed an address that is not a font.
///
/// CX and DL come out of the BDA either way — the character height in
/// scan lines and the row count less one, both written by the mode set.
void get_font_info(machine& box) {
  cpu::registers& regs = box.processor().regs();

  std::uint8_t vector = 0;
  switch (regs.get(cpu::reg8::bh)) {
    case 0x00:
      vector = graphics_font_vector;
      break;
    case 0x01:
      vector = alternate_font_vector;
      break;
    default:
      box.stop_unsupported_request(regs[cpu::reg16::ax]);
      return;
  }

  cpu::processor& cpu = box.processor();
  const std::uint16_t at = cpu::vector_table_offset(vector);
  regs[cpu::reg16::bp] = cpu.read_word(cpu::vector_table_segment, at);
  regs[cpu::sreg::es] = cpu.read_word(cpu::vector_table_segment,
                                      static_cast<std::uint16_t>(at + 2));
  regs[cpu::reg16::cx] = bda_word(box, bda::character_points);
  regs.set(cpu::reg8::dl, bda_byte(box, bda::video_rows_minus_one));
}

void video_bios(service_floor& floor, std::uint8_t /*vector*/) {
  machine& box = floor.box();
  const cpu::registers& regs = box.processor().regs();
  const std::uint8_t function = regs.get(cpu::reg8::ah);

  switch (function) {
    case 0x00:
      set_mode(box);
      return;
    case 0x05:
      set_page(box);
      return;
    case 0x08:
      read_character(box);
      return;
    case 0x0F:
      get_mode(box);
      return;
    case 0x10:
      set_palette(box);
      return;
    case 0x11:
      if (regs.get(cpu::reg8::al) == 0x30) {
        get_font_info(box);
      } else {
        box.stop_unsupported_request(regs[cpu::reg16::ax]);
      }
      return;
    default:
      box.stop_unsupported_request(regs[cpu::reg16::ax]);
  }
}

}  // namespace

void install_int10(service_floor& floor) {
  floor.provide(service::video_vector, &video_bios);
}

}  // namespace amberfolio::machine
