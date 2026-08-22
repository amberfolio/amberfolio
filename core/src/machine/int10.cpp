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
#include "amberfolio/machine/font.h"
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

/// The graphics-controller and sequencer indices AH=09h steers one at a
/// time. The mode-set tables below name the same numbers positionally;
/// they are named here because a character write selects them by hand.
constexpr std::uint8_t graphics_enable_set_reset_index = 0x01;
constexpr std::uint8_t graphics_data_rotate_index = 0x03;
constexpr std::uint8_t graphics_mode_index = 0x05;
constexpr std::uint8_t graphics_bit_mask_index = 0x08;
constexpr std::uint8_t sequencer_map_mask_index = 0x02;

/// Write mode 2, colour expand: the CPU byte's low four bits become one
/// bit per plane, spread across all eight pixel positions (ega.h's write
/// pipeline). It is the mode a glyph wants — one colour, eight pixels,
/// selected by a mask.
constexpr std::uint8_t graphics_write_mode_2 = 0x02;

/// The Data Rotate register's function field (bits 4:3) set to 3, XOR.
/// AH=09h's attribute bit 7 asks for it; a zero here is the copy the
/// mode set leaves behind.
constexpr std::uint8_t graphics_function_xor = 0x18;

/// Every plane: what the map mask and Enable Set/Reset both want while a
/// glyph is being expanded across all four.
constexpr std::uint8_t all_planes = 0x0F;

/// The bit mask that changes nothing: every bit of the ALU's result
/// reaches the plane. What mode set leaves, and what AH=09h puts back.
constexpr std::uint8_t whole_byte = 0xFF;

/// Where mode 0Dh's planes are addressed (ega.h's `vram_window`). An
/// offset into it is a 16-bit number and every 16-bit number is inside
/// it, which is why nothing below clips: a cursor pointing off the end of
/// the screen wraps within the segment, exactly as it would on the real
/// card.
constexpr std::uint16_t graphics_page_segment = 0xA000;

/// The attribute byte AH=09h reads in a graphics mode: the foreground
/// colour in the low four bits, and bit 7 asking for XOR against what is
/// already there rather than a replacement. The documented public BIOS
/// convention, like AH=10h's above (int10.h).
constexpr std::uint8_t attribute_colour_mask = 0x0F;
constexpr std::uint8_t attribute_exclusive_or = 0x80;

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

/// The standard 16-colour default palette for a 200-line mode on a Color
/// Display (int10.h's top comment, and ega.h's "The palette codes,
/// mapped to RGB once" for which display this machine has).
/// Index *i* of this array is the value AH=00h writes to palette register
/// *i*: the low eight are the colour bits alone, and the high eight are
/// the same bits with intensity (bit 4) added, which is why they run
/// 16-23 rather than 8-15. The other table an EGA BIOS carries — the one
/// with 20 at index 6 and 56-63 above it — is the Enhanced Color
/// Display's, where all six colour bits reach the screen; on this display
/// bits 5 and 3 reach nothing and that table would put bright red where
/// brown belongs. The program this machine exists to run programs its own
/// palette in exactly the encoding below.
constexpr std::array<std::uint8_t, ega::palette_register_count>
    mode_0d_default_palette{0,  1,  2,  3,  4,  5,  6,  7,
                            16, 17, 18, 19, 20, 21, 22, 23};

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

/// One graphics-controller register, through its index/data pair.
void write_graphics_register(machine& box, std::uint8_t index,
                             std::uint8_t value) {
  box.write_port8(graphics_index_port, index);
  box.write_port8(graphics_data_port, value);
}

/// One sequencer register, the same way.
void write_sequencer_register(machine& box, std::uint8_t index,
                              std::uint8_t value) {
  box.write_port8(sequencer_index_port, index);
  box.write_port8(sequencer_data_port, value);
}

/// AH=00h AL=0Dh: program the sequencer, the graphics controller and the
/// palette for 320x200x16, and tell the machine a mode is now active
/// (machine.h's "Video mode discipline").
void set_mode_0d(machine& box) {
  for (const register_write& reg : mode_0d_sequencer) {
    write_sequencer_register(box, reg.index, reg.value);
  }
  for (const register_write& reg : mode_0d_graphics) {
    write_graphics_register(box, reg.index, reg.value);
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

/// AH=02h: put the cursor at row DH, column DL of page BH.
///
/// Recording the position is the whole of what a real BIOS does here
/// that this machine can also do. The BDA word at 40:50 *is* the cursor
/// for page 0 — a program reads it directly as often as it asks for it,
/// and AH=08h above indexes the text page with it — so writing it is the
/// real operation and not a stand-in for one. DX already has the byte
/// order the BDA wants: DL is the column and DH the row, which is a word
/// stored low byte first, so the register goes down whole.
///
/// The other half of a real BIOS's job — programming the CRTC's cursor
/// location registers so the blinking underline moves — has nothing to
/// do here. This machine has no CRTC (#47), and in mode 0Dh, the one
/// mode it draws, a real adapter does not display the hardware cursor
/// either: on the screen this call is invisible on both machines. In
/// mode 03h it would be visible on a real screen, and why it is not on
/// this one is already standing in the log from the mode set
/// (`undisplayable_video_mode`); a notice per cursor move would repeat
/// that same sentence for as long as the program ran.
///
/// Nothing is range-checked, because a real BIOS checks nothing either:
/// it stores what it was handed, and a program that asks for row 200
/// reads row 200 back. BH is the exception, refused for exactly the
/// reason AH=05h refuses it — there is one page here.
void set_cursor_position(machine& box) {
  cpu::registers& regs = box.processor().regs();
  if (regs.get(cpu::reg8::bh) != 0) {
    box.stop_unsupported_request(regs[cpu::reg16::ax]);
    return;
  }
  set_bda_word(box, bda::cursor_position, regs[cpu::reg16::dx]);
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

/// A character generator, as a place in the emulated machine's memory:
/// where the glyphs start, and the character code the first of them is
/// for. `points` bytes each, one byte per scan line.
struct glyph_source {
  std::uint16_t segment{};
  std::uint16_t offset{};
  std::uint8_t first{};
};

/// Read a font *pointer* vector, and say whether it names glyphs.
///
/// Power-on puts this machine's own generator in the BIOS region and
/// aims both vectors at it (font.h), so ordinarily this is true and the
/// glyphs are the machine's. It stops being true in exactly two ways,
/// and both of them are worth telling apart from a table: the vector
/// still holds the IRET stub power-on gives every *other* vector, which
/// means something cleared it back to nothing; or it is null, which is
/// the same statement with different bytes. Neither can be indexed, so
/// neither is a font.
///
/// Note what is *not* checked: that the pointer aims somewhere sensible.
/// A program is free to install a table anywhere, including in this
/// machine's own ROM region, and second-guessing where a font may live
/// would be this file inventing a rule the hardware does not have.
[[nodiscard]] bool font_pointer(machine& box, std::uint8_t vector,
                                glyph_source& font) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t at = cpu::vector_table_offset(vector);
  font.offset = cpu.read_word(cpu::vector_table_segment, at);
  font.segment = cpu.read_word(cpu::vector_table_segment,
                               static_cast<std::uint16_t>(at + 2));

  const bool is_stub = font.segment == service::stub_segment &&
                       font.offset == service::stub_offset(vector);
  return !is_stub && (font.segment != 0 || font.offset != 0);
}

/// Which character generator draws `character`, if any does.
///
/// The order is the documented public convention: codes 80h-FFh come
/// from INT 1Fh, the top-half table, and codes 00h-7Fh from INT 43h, the
/// current generator. Power-on aims both at this machine's own font
/// (font.h), so ordinarily both answer; a program that installs a table
/// of its own is read out of that instead, which is the whole reason
/// this goes through the vectors rather than reaching for `font::glyphs`
/// directly.
///
/// The fallback in between is deliberate: an EGA BIOS in a graphics mode
/// draws the *whole* code page out of INT 43h, so a program that
/// replaced only that one is answered from it, indexed from zero. A
/// program that cleared both is refused — there is nothing left to
/// index — and that refusal says exactly that and nothing else.
[[nodiscard]] bool find_glyphs(machine& box, std::uint8_t character,
                               glyph_source& font) {
  if (character >= font::high_half_first &&
      font_pointer(box, font::high_half_vector, font)) {
    font.first = font::high_half_first;
    return true;
  }
  if (font_pointer(box, font::generator_vector, font)) {
    font.first = 0;
    return true;
  }
  return false;
}

/// AH=09h in the 80x25 text mode this machine records and cannot draw:
/// the character and its attribute, at the cursor, CX times.
///
/// The same answer AH=08h gives from the other direction. A real BIOS
/// stores two bytes per cell into the display page; so does this, through
/// the bus, at the address the page is at. Nothing in this machine claims
/// B8000 (memory_map.h), so the writes are dropped and the machine says
/// so once — the honest report of a text mode it has already said it
/// cannot display, not a second accommodation.
void write_character_text(machine& box) {
  const cpu::registers& regs = box.processor().regs();
  const std::uint16_t cursor = bda_word(box, bda::cursor_position);
  const auto column = static_cast<std::uint16_t>(cursor & 0xFFu);
  const auto row = static_cast<std::uint16_t>(cursor >> 8u);
  auto offset = static_cast<std::uint16_t>(
      (row * bda_word(box, bda::video_columns) + column) * 2u);

  cpu::processor& cpu = box.processor();
  for (std::uint16_t left = regs[cpu::reg16::cx]; left > 0; --left) {
    cpu.write_byte(text_page_segment, offset, regs.get(cpu::reg8::al));
    cpu.write_byte(text_page_segment, static_cast<std::uint16_t>(offset + 1),
                   regs.get(cpu::reg8::bl));
    offset = static_cast<std::uint16_t>(offset + 2);
  }
}

/// AH=09h in mode 0Dh: the glyph, in the attribute's colour, at the
/// cursor, CX times.
///
/// This is a real drawing operation, done the way the rest of this file
/// does everything — through the ports and the bus, so the pixels land
/// through the EGA's own write pipeline (ega.h) rather than beside it.
/// Per scan line, with write mode 2 selected and Enable Set/Reset opening
/// all four planes:
///
///   * **Replace.** The whole cell is written as colour 0 with the bit
///     mask wide open, then read back — which is what loads the latches —
///     and written again as the foreground colour with the bit mask set
///     to the glyph's bits. Mask bits of 0 keep the latch, which now
///     holds the cleared cell, so the background stays colour 0 and the
///     glyph stands in the attribute's colour. That two-pass shape is not
///     an inefficiency to fold away: write mode 2 carries one colour per
///     write, and a character cell has two.
///   * **XOR** (attribute bit 7) skips the clearing pass entirely and
///     sets the ALU function to XOR, so only the glyph's own bits change
///     and whatever was underneath survives around them. That is what the
///     bit is *for* — a cursor or a highlight that can be drawn a second
///     time to undo it.
///
/// The repeat count walks one byte forward per character, which is one
/// column in mode 0Dh because a four-bit-per-pixel planar row of 320
/// pixels is 40 bytes and 40 is also the column count. Nothing clips:
/// past the right edge the address carries on into the next scan line of
/// the same character row, which is exactly what a real BIOS's own
/// address arithmetic does and exactly the garbage it draws.
///
/// The adapter is left as the mode set left it rather than as the program
/// had it. A real BIOS clobbers these registers too — its character
/// writer is a table of OUTs like everything else it does — and a program
/// that drives the graphics controller directly re-programs it before its
/// next write for that reason.
void write_character_graphics(machine& box) {
  const cpu::registers& regs = box.processor().regs();
  const std::uint8_t character = regs.get(cpu::reg8::al);

  glyph_source font;
  if (!find_glyphs(box, character, font)) {
    box.stop_unsupported_request(regs[cpu::reg16::ax]);
    return;
  }

  const std::uint16_t columns = bda_word(box, bda::video_columns);
  const std::uint16_t points = bda_word(box, bda::character_points);
  const std::uint16_t cursor = bda_word(box, bda::cursor_position);
  const auto column = static_cast<std::uint16_t>(cursor & 0xFFu);
  const auto row = static_cast<std::uint16_t>(cursor >> 8u);

  const std::uint8_t attribute = regs.get(cpu::reg8::bl);
  const std::uint8_t colour = attribute & attribute_colour_mask;
  const bool exclusive_or = (attribute & attribute_exclusive_or) != 0;

  const auto glyph = static_cast<std::uint16_t>(
      font.offset + static_cast<unsigned>(character - font.first) *
                        static_cast<unsigned>(points));

  write_sequencer_register(box, sequencer_map_mask_index, all_planes);
  write_graphics_register(box, graphics_enable_set_reset_index, all_planes);
  write_graphics_register(box, graphics_mode_index, graphics_write_mode_2);
  write_graphics_register(box, graphics_data_rotate_index,
                          exclusive_or ? graphics_function_xor : 0x00);

  cpu::processor& cpu = box.processor();
  auto cell = static_cast<std::uint16_t>(row * points * columns + column);
  for (std::uint16_t left = regs[cpu::reg16::cx]; left > 0; --left) {
    std::uint16_t at = cell;
    for (std::uint16_t line = 0; line < points; ++line) {
      const std::uint8_t bits =
          cpu.read_byte(font.segment, static_cast<std::uint16_t>(glyph + line));
      if (!exclusive_or) {
        write_graphics_register(box, graphics_bit_mask_index, whole_byte);
        cpu.write_byte(graphics_page_segment, at, 0x00);
      }
      static_cast<void>(cpu.read_byte(graphics_page_segment, at));
      write_graphics_register(box, graphics_bit_mask_index, bits);
      cpu.write_byte(graphics_page_segment, at, colour);
      at = static_cast<std::uint16_t>(at + columns);
    }
    cell = static_cast<std::uint16_t>(cell + 1);
  }

  write_graphics_register(box, graphics_bit_mask_index, whole_byte);
  write_graphics_register(box, graphics_data_rotate_index, 0x00);
  write_graphics_register(box, graphics_mode_index, 0x00);
  write_graphics_register(box, graphics_enable_set_reset_index, 0x00);
}

/// AH=09h: write a character and its attribute at the cursor, CX times,
/// without moving the cursor. Which of the two bodies above runs is the
/// recorded mode's question; any other mode is refused, as is any page
/// but 0.
void write_character(machine& box) {
  const cpu::registers& regs = box.processor().regs();
  if (regs.get(cpu::reg8::bh) != 0) {
    box.stop_unsupported_request(regs[cpu::reg16::ax]);
    return;
  }

  const std::uint8_t mode = bda_byte(box, bda::video_mode);
  if (mode == mode_0dh) {
    write_character_graphics(box);
    return;
  }
  if (mode == mode_03h) {
    write_character_text(box);
    return;
  }

  box.stop_unsupported_request(regs[cpu::reg16::ax]);
}

/// AH=11h AL=30h: where a character generator lives, and how tall its
/// characters are.
///
/// BH picks which one, and four of the eight are answerable here:
///
///   * **BH=00h and BH=01h** ask for the *vectors* 1Fh and 43h, the font
///     pointers a program supplies for itself. They are read straight
///     out of the interrupt vector table, which is where they live and
///     where a program that set one put it. Nothing is invented, and
///     nothing is claimed about what they point at.
///   * **BH=03h and BH=04h** ask for the ROM 8x8 font and for its top
///     half. Since M4 this machine has one — its own, in its own BIOS
///     region (font.h) — so they are answered with its address and with
///     the height of the glyphs actually there, which is the one number
///     a caller must not get from anywhere else.
///   * **BH=02h, 05h, 06h and 07h** ask for the 8x14 and 8x16 fonts.
///     Those are different glyphs, not the same glyphs at a different
///     size, and this machine has only the 8x8 — so they are still
///     refused, and a program that genuinely needs a taller cell appears
///     in the log instead of being handed a table that is the wrong
///     shape.
///
/// CX and DL come out of the BDA for the vectors — the character height
/// in scan lines and the row count less one, both written by the mode
/// set. For the ROM font CX is the table's own height instead: it is a
/// fact about the bytes being pointed at, and the mode's cell size has
/// no vote in it.
void get_font_info(machine& box) {
  cpu::registers& regs = box.processor().regs();

  cpu::processor& cpu = box.processor();
  const std::uint8_t which = regs.get(cpu::reg8::bh);

  if (which == 0x03 || which == 0x04) {
    const auto top_half = static_cast<std::uint16_t>(
        service::font_offset + font::high_half_first * font::glyph_height);
    regs[cpu::sreg::es] = service::stub_segment;
    regs[cpu::reg16::bp] = which == 0x03 ? service::font_offset : top_half;
    regs[cpu::reg16::cx] = font::glyph_height;
    regs.set(cpu::reg8::dl, bda_byte(box, bda::video_rows_minus_one));
    return;
  }

  std::uint8_t vector = 0;
  switch (which) {
    case 0x00:
      vector = font::high_half_vector;
      break;
    case 0x01:
      vector = font::generator_vector;
      break;
    default:
      box.stop_unsupported_request(regs[cpu::reg16::ax]);
      return;
  }

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
    case 0x02:
      set_cursor_position(box);
      return;
    case 0x05:
      set_page(box);
      return;
    case 0x08:
      read_character(box);
      return;
    case 0x09:
      write_character(box);
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
