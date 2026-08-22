// SPDX-License-Identifier: AGPL-3.0-only
//
// INT 10h: what AH=00h AL=0Dh leaves programmed, what a program asking
// for anything else gets, the mode-discipline notice a write into the
// video window trips before AH=00h has run, the five functions M3's
// first boot went on to ask for (#87) — mode read-back, the recorded-only
// text mode, page select, the character under the cursor, and the font
// pointers — and the two M4's character creation asked for (#121): the
// cursor move, and the character written at it, out of the program's own
// font or out of the machine's.
//
// Every program below is written here from the encoding, the same
// discipline service_floor_test.cpp follows (PLAN.md §6).

#include "amberfolio/machine/int10.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/font.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/service_floor.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x3000;
constexpr std::uint16_t stack_top = 0x1000;

constexpr std::uint16_t sequencer_index_port = 0x3C4;
constexpr std::uint16_t sequencer_data_port = 0x3C5;
constexpr std::uint16_t graphics_index_port = 0x3CE;
constexpr std::uint16_t graphics_data_port = 0x3CF;

/// A machine with an EGA attached and the video BIOS installed — every
/// test here wants both.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        video(std::make_unique<ega>(*box)) {
    box->attach(*video);
    install_int10(box->services());
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// INT `service::video_vector` ; HLT — every test's whole program, with
  /// AX and BX set beforehand to name the call.
  void call_int10() const {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (const std::uint8_t byte :
         {std::uint8_t{0xCD}, service::video_vector, std::uint8_t{0xF4}}) {
      box->memory().ram()[at] = byte;
      ++at;
    }

    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = code_segment;
    r[cpu::sreg::ss] = code_segment;
    r[cpu::reg16::sp] = stack_top;
    r.ip = 0;
  }

  /// Step until the program halts or the machine stops.
  void run(unsigned cap = 2000) const {
    unsigned steps = 0;
    while (!box->processor().halted() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
};

// --- AH=00h: mode set ----------------------------------------------------

TEST(int10_mode_set, al_0dh_programs_the_documented_register_state) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());
  EXPECT_TRUE(r.pc().video_mode_set());

  // The one load-bearing value: every plane reachable after mode-set.
  r.video->write_port(sequencer_index_port, 0x02);
  EXPECT_EQ(r.video->read_port(sequencer_data_port), 0x0F);

  // The rest of the documented table — inert on this device (ega.h,
  // int10.cpp), but still what mode-set is supposed to leave behind.
  r.video->write_port(graphics_index_port, 0x05);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0x00);
  r.video->write_port(graphics_index_port, 0x06);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0x03);
  r.video->write_port(graphics_index_port, 0x08);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0xFF);

  // The standard 16-colour default palette (int10.h's top comment): the
  // colour bits alone, then the same eight with intensity added.
  constexpr std::array<std::uint8_t, 16> expected{
      0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23};
  for (unsigned i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(r.video->palette_register(i), expected[i]) << "register " << i;
  }
  EXPECT_EQ(r.video->overscan_register(), 0x00);
}

TEST(int10_mode_set, any_other_mode_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0013;  // AH=00h AL=13h: a VGA mode.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0013u);
  EXPECT_FALSE(r.pc().video_mode_set());

  ASSERT_EQ(r.log.stops.size(), 1u);
  EXPECT_EQ(r.log.stops[0], r.pc().stop());
}

// --- AH=00h AL=03h: the mode this machine records and cannot draw --------

TEST(int10_mode_set, al_03h_is_recorded_reported_and_reported_on) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0003;
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());

  // Recorded where a program reads it.
  cpu::processor& cpu = r.pc().processor();
  EXPECT_EQ(cpu.read_byte(bda::segment, bda::video_mode), 0x03);
  EXPECT_EQ(cpu.read_word(bda::segment, bda::video_columns), 80u);
  EXPECT_EQ(cpu.read_byte(bda::segment, bda::video_rows_minus_one), 24);
  EXPECT_EQ(cpu.read_word(bda::segment, bda::character_points), 14u);

  // And said out loud, once, because nothing was programmed and nothing
  // will be drawn (int10.h's "The modes this machine has").
  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::undisplayable_video_mode);
  EXPECT_EQ(r.log.notices[0].at, 0x03u);

  // Nothing reached the adapter: the map mask is still what reset left,
  // not the 0Fh a real mode-set writes.
  r.video->write_port(sequencer_index_port, 0x02);
  EXPECT_NE(r.video->read_port(sequencer_data_port), 0x0F);

  // Not the same question as "has the program programmed a mode" — that
  // flag gates one notice about drawing early and 03h programmed nothing.
  EXPECT_FALSE(r.pc().video_mode_set());
}

TEST(int10_mode_set, the_undisplayable_notice_is_reported_once_per_mode) {
  rig r;
  for (int i = 0; i < 3; ++i) {
    r.call_int10();
    r.regs()[cpu::reg16::ax] = 0x0003;
    r.run();
    ASSERT_FALSE(r.pc().stopped());
  }
  EXPECT_EQ(r.log.notices.size(), 1u);
}

// --- AH=02h: where the cursor is ------------------------------------------

TEST(int10_set_cursor, records_the_position_where_a_program_reads_it) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0200;
  r.regs()[cpu::reg16::bx] = 0x0000;  // BH=00h: the one page.
  r.regs()[cpu::reg16::dx] = 0x0C05;  // DH=12: row. DL=5: column.
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());

  // The BDA word, and the two bytes it is made of — column first, which
  // is the order DX already has (service_floor.h's `cursor_position`).
  cpu::processor& cpu = r.pc().processor();
  EXPECT_EQ(cpu.read_word(bda::segment, bda::cursor_position), 0x0C05u);
  EXPECT_EQ(cpu.read_byte(bda::segment, bda::cursor_position), 5);
  EXPECT_EQ(cpu.read_byte(bda::segment,
                          static_cast<std::uint16_t>(bda::cursor_position + 1)),
            12);

  // Nothing was refused and nothing was accommodated: in the mode this
  // machine draws, a real adapter shows no cursor either (int10.h's
  // "Where the cursor is").
  EXPECT_TRUE(r.log.notices.empty());
}

TEST(int10_set_cursor, nothing_is_range_checked_but_the_page) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0200;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.regs()[cpu::reg16::dx] = 0xC8FF;  // row 200, column 255: off any screen.
  r.run();

  // A real BIOS stores what it was handed and so does this one, so a
  // program that reads its own cursor back reads what it wrote.
  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.pc().processor().read_word(bda::segment, bda::cursor_position),
            0xC8FFu);
}

TEST(int10_set_cursor, any_other_page_is_refused) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0200;
  r.regs()[cpu::reg16::bx] = 0x0100;  // BH=01h: a page this machine has not.
  r.regs()[cpu::reg16::dx] = 0x0000;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0200u);
}

TEST(int10_set_cursor, a_mode_set_puts_it_back_at_the_top_left) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0200;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.regs()[cpu::reg16::dx] = 0x0C05;
  r.run();
  ASSERT_FALSE(r.pc().stopped());

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();

  // One piece of state, not two: the mode set clears the same word this
  // call wrote, the way a real mode set clears the screen and the cursor
  // together.
  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.pc().processor().read_word(bda::segment, bda::cursor_position),
            0u);
}

// --- AH=09h: a character, drawn out of the program's own font -------------
//
// The glyph below is this file's own eight bytes — a box, chosen because
// every scan line differs from its neighbours, so a row that lands in the
// wrong place is visible in the assertion rather than merely wrong.

constexpr std::uint16_t font_segment = 0x2000;
constexpr std::uint16_t font_offset = 0x0100;
constexpr std::array<std::uint8_t, 8> box_glyph{0xFF, 0x81, 0xBD, 0xA5,
                                                0xA5, 0xBD, 0x81, 0xFF};

/// Put the glyph in the emulated machine's memory and point `vector` at
/// it — which is all "the program installed a font" is (int10.h's "Where
/// the glyphs come from").
void install_font(const rig& r, std::uint8_t vector) {
  cpu::processor& cpu = r.pc().processor();
  for (std::size_t i = 0; i < box_glyph.size(); ++i) {
    cpu.write_byte(font_segment, static_cast<std::uint16_t>(font_offset + i),
                   box_glyph[i]);
  }
  const std::uint16_t at = cpu::vector_table_offset(vector);
  cpu.write_word(cpu::vector_table_segment, at, font_offset);
  cpu.write_word(cpu::vector_table_segment, static_cast<std::uint16_t>(at + 2),
                 font_segment);
}

/// Mode 0Dh, and the cursor at row DH column DL — the two calls every
/// drawing test below opens with.
void graphics_at(const rig& r, std::uint16_t cursor) {
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();
  ASSERT_FALSE(r.pc().stopped());

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0200;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.regs()[cpu::reg16::dx] = cursor;
  r.run();
  ASSERT_FALSE(r.pc().stopped());
}

/// AH=09h with the character in AL, the attribute in BL, `count` in CX.
void write_character(const rig& r, std::uint8_t character,
                     std::uint8_t attribute, std::uint16_t count) {
  r.call_int10();
  r.regs()[cpu::reg16::ax] =
      static_cast<std::uint16_t>(0x0900u | static_cast<unsigned>(character));
  r.regs()[cpu::reg16::bx] = attribute;
  r.regs()[cpu::reg16::cx] = count;
  r.run();
}

TEST(int10_write_character, draws_the_glyph_in_the_attribute_colour) {
  rig r;
  install_font(r, 0x1F);
  graphics_at(r, 0x0000);

  // Colour 9 is 1001b: planes 0 and 3 take the glyph, planes 1 and 2 are
  // cleared to the background the replacing form writes.
  write_character(r, 0x80, 0x09, 1);
  ASSERT_FALSE(r.pc().stopped());

  for (unsigned line = 0; line < box_glyph.size(); ++line) {
    const auto at = static_cast<std::uint16_t>(line * 40);
    EXPECT_EQ(r.video->plane_byte(0, at), box_glyph[line]) << "line " << line;
    EXPECT_EQ(r.video->plane_byte(1, at), 0x00) << "line " << line;
    EXPECT_EQ(r.video->plane_byte(2, at), 0x00) << "line " << line;
    EXPECT_EQ(r.video->plane_byte(3, at), box_glyph[line]) << "line " << line;
  }
}

TEST(int10_write_character, the_cursor_says_where_and_does_not_move) {
  rig r;
  install_font(r, 0x1F);
  graphics_at(r, 0x0207);  // row 2, column 7.

  write_character(r, 0x80, 0x0F, 1);
  ASSERT_FALSE(r.pc().stopped());

  // Row 2 of 8-line cells, 40 bytes to the scan line: 2*8*40 + 7.
  constexpr std::uint16_t cell = (2 * 8 * 40) + 7;
  EXPECT_EQ(r.video->plane_byte(0, cell), box_glyph[0]);
  EXPECT_EQ(r.video->plane_byte(0, cell + 40), box_glyph[1]);
  EXPECT_EQ(r.video->plane_byte(0, 0), 0x00);

  // AH=09h leaves the cursor exactly where it found it.
  EXPECT_EQ(r.pc().processor().read_word(bda::segment, bda::cursor_position),
            0x0207u);
}

TEST(int10_write_character, cx_repeats_it_along_the_row) {
  rig r;
  install_font(r, 0x1F);
  graphics_at(r, 0x0000);

  write_character(r, 0x80, 0x0F, 3);
  ASSERT_FALSE(r.pc().stopped());

  for (std::uint16_t column = 0; column < 3; ++column) {
    EXPECT_EQ(r.video->plane_byte(0, column), box_glyph[0])
        << "column " << column;
  }
  EXPECT_EQ(r.video->plane_byte(0, 3), 0x00);
}

TEST(int10_write_character, bit_7_of_the_attribute_xors_instead_of_replacing) {
  rig r;
  install_font(r, 0x1F);
  graphics_at(r, 0x0000);

  write_character(r, 0x80, 0x0F, 1);
  ASSERT_FALSE(r.pc().stopped());
  ASSERT_EQ(r.video->plane_byte(0, 0), box_glyph[0]);

  // The same glyph, the same colour, XORed: everything it drew comes back
  // off and nothing around it moved. That is what the bit is for.
  write_character(r, 0x80, 0x8F, 1);
  ASSERT_FALSE(r.pc().stopped());
  for (unsigned line = 0; line < box_glyph.size(); ++line) {
    const auto at = static_cast<std::uint16_t>(line * 40);
    EXPECT_EQ(r.video->plane_byte(0, at), 0x00) << "line " << line;
    EXPECT_EQ(r.video->plane_byte(3, at), 0x00) << "line " << line;
  }
}

TEST(int10_write_character, a_low_code_comes_from_the_int_43h_generator) {
  rig r;
  install_font(r, 0x43);
  graphics_at(r, 0x0000);

  // Indexed from zero, unlike INT 1Fh's table (int10.h).
  write_character(r, 0x00, 0x0F, 1);
  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.video->plane_byte(0, 0), box_glyph[0]);
  EXPECT_EQ(r.video->plane_byte(0, 40), box_glyph[1]);
}

TEST(int10_write_character, a_program_that_supplies_no_font_gets_the_machines) {
  rig r;
  graphics_at(r, 0x0000);

  // Nothing installed anything: power-on's own generator answers, which
  // is what a real adapter's ROM does (font.h).
  write_character(r, 'A', 0x0F, 1);
  ASSERT_FALSE(r.pc().stopped());

  const std::span<const std::uint8_t> glyphs = font::glyphs();
  for (unsigned line = 0; line < font::glyph_height; ++line) {
    EXPECT_EQ(r.video->plane_byte(0, static_cast<std::uint16_t>(line * 40)),
              glyphs[('A' * font::glyph_height) + line])
        << "line " << line;
  }
}

TEST(int10_write_character, with_both_generators_cleared_there_is_nothing) {
  rig r;
  // Put the two vectors back to the IRET stub every other vector gets,
  // which is what "no generator" looks like from inside the machine.
  cpu::processor& cpu = r.pc().processor();
  for (const std::uint8_t vector :
       {font::high_half_vector, font::generator_vector}) {
    const std::uint16_t at = cpu::vector_table_offset(vector);
    cpu.write_word(cpu::vector_table_segment, at, service::stub_offset(vector));
    cpu.write_word(cpu::vector_table_segment,
                   static_cast<std::uint16_t>(at + 2), service::stub_segment);
  }
  graphics_at(r, 0x0000);

  write_character(r, 0x80, 0x0F, 1);

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0980u);
  EXPECT_EQ(r.video->plane_byte(0, 0), 0x00);
}

TEST(int10_write_character, any_page_but_zero_is_refused) {
  rig r;
  install_font(r, 0x1F);
  graphics_at(r, 0x0000);

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0980;
  r.regs()[cpu::reg16::bx] = 0x010F;  // BH=01h.
  r.regs()[cpu::reg16::cx] = 1;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
}

TEST(int10_write_character, in_text_mode_it_writes_a_page_nothing_answers_for) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0003;
  r.run();
  ASSERT_FALSE(r.pc().stopped());
  const std::size_t notices_after_mode_set = r.log.notices.size();

  write_character(r, 'A', 0x07, 2);

  // The writes go to the bus at the address the text page is at, and
  // nothing there takes them — the same answer AH=08h gets reading.
  ASSERT_FALSE(r.pc().stopped());
  ASSERT_EQ(r.log.notices.size(), notices_after_mode_set + 1);
  EXPECT_EQ(r.log.notices.back().what, notice_kind::unmapped_memory_write);
  EXPECT_EQ(r.log.notices.back().at, 0xB8000u);
  EXPECT_EQ(r.log.notices.back().value, 'A');
}

// --- AH=0Fh: report the current mode --------------------------------------

TEST(int10_get_mode, answers_the_power_on_block_before_any_mode_set) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0F00;
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  // The self test leaves the block describing the one mode this machine
  // can display (service_floor.h's `bda` video block).
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0x0D);
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 40);
  EXPECT_EQ(r.regs().get(cpu::reg8::bh), 0);
}

TEST(int10_get_mode, reads_back_whatever_mode_set_recorded) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0003;
  r.run();
  ASSERT_FALSE(r.pc().stopped());

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0F00;
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0x03);
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 80);
}

// --- AH=05h: the one display page -----------------------------------------

TEST(int10_set_page, page_zero_is_accepted_and_recorded) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0500;
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.pc().processor().read_byte(bda::segment, bda::video_active_page),
            0);
}

TEST(int10_set_page, any_other_page_is_refused) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0501;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0501u);
}

// --- AH=08h: the character under the cursor -------------------------------

TEST(int10_read_character, in_text_mode_it_reads_the_bus_and_finds_nothing) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0003;
  r.run();
  ASSERT_FALSE(r.pc().stopped());
  const std::size_t notices_after_mode_set = r.log.notices.size();

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0800;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  // Open bus floats high, and that is the answer — not an invented space
  // (int10.cpp's AH=08h comment).
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0xFFFFu);

  ASSERT_EQ(r.log.notices.size(), notices_after_mode_set + 1);
  EXPECT_EQ(r.log.notices.back().what, notice_kind::unmapped_memory_read);
  EXPECT_EQ(r.log.notices.back().at, 0xB8000u);
}

TEST(int10_read_character, in_a_graphics_mode_there_is_nothing_to_read) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();
  ASSERT_FALSE(r.pc().stopped());

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0800;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
}

// --- AH=11h AL=30h: where the character generator is ----------------------

TEST(int10_font_info, bh_00h_answers_with_the_int_1fh_vector) {
  rig r;
  // A program's own font pointer, stored the way a program stores one:
  // four bytes at the vector's place in the table.
  cpu::processor& cpu = r.pc().processor();
  cpu.write_word(cpu::vector_table_segment, cpu::vector_table_offset(0x1F),
                 0x1234);
  cpu.write_word(cpu::vector_table_segment,
                 static_cast<std::uint16_t>(cpu::vector_table_offset(0x1F) + 2),
                 0x5678);

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1130;
  r.regs()[cpu::reg16::bx] = 0x0000;
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.regs()[cpu::reg16::bp], 0x1234u);
  EXPECT_EQ(r.regs()[cpu::sreg::es], 0x5678u);
  EXPECT_EQ(r.regs()[cpu::reg16::cx], 8u);     // points, from the BDA
  EXPECT_EQ(r.regs().get(cpu::reg8::dl), 24);  // rows less one
}

TEST(int10_font_info, bh_03h_and_04h_answer_with_the_machines_own_font) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1130;
  r.regs()[cpu::reg16::bx] = 0x0300;  // BH=03h: the ROM 8x8 font.
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(r.regs()[cpu::sreg::es], service::stub_segment);
  EXPECT_EQ(r.regs()[cpu::reg16::bp], service::font_offset);
  // The height of the table pointed at, not the mode's cell size.
  EXPECT_EQ(r.regs()[cpu::reg16::cx], font::glyph_height);

  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1130;
  r.regs()[cpu::reg16::bx] = 0x0400;  // BH=04h: its top half.
  r.run();

  ASSERT_FALSE(r.pc().stopped());
  EXPECT_EQ(
      r.regs()[cpu::reg16::bp],
      service::font_offset + (font::high_half_first * font::glyph_height));
}

TEST(int10_font_info, a_taller_cell_is_refused_because_there_is_only_the_8x8) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1130;
  r.regs()[cpu::reg16::bx] = 0x0600;  // BH=06h: the ROM 8x16 font.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
}

TEST(int10_font_info, any_other_subfunction_of_ah_11h_is_refused) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1100;  // load a user font: not here.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
}

// --- AH=10h: palette register set -----------------------------------------

TEST(int10_palette, al_00h_sets_one_palette_register) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1000;
  r.regs()[cpu::reg16::bx] = 0x2A05;  // BH = 2Ah colour, BL = register 5.
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.video->palette_register(5), 0x2A);
}

TEST(int10_palette, al_01h_sets_the_overscan_colour) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1001;
  r.regs()[cpu::reg16::bx] = 0x3F00;  // BH = 3Fh colour.
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.video->overscan_register(), 0x3F);
}

TEST(int10_palette, any_other_al_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1099;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x1099u);
}

// --- Anything but AH=00h/10h -----------------------------------------------

TEST(int10_dispatch, any_other_function_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0E41;  // AH=0Eh: teletype output, not ours.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0E41u);

  // The vector was reached and did have a handler — this is the
  // handler's own refusal, not a missing vector.
  ASSERT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.calls[0].outcome, service_outcome::handled);
}

// --- Mode discipline -------------------------------------------------------

TEST(int10_mode_discipline, a_vram_write_before_mode_set_is_noticed) {
  rig r;

  r.pc().write_memory(0xA0000, 0x11);

  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::video_write_before_mode_set);
  EXPECT_EQ(r.log.notices[0].at, 0xA0000u);
  EXPECT_EQ(r.log.notices[0].value, 0x11);
}

TEST(int10_mode_discipline, no_notice_once_a_mode_has_been_set) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();
  ASSERT_TRUE(r.pc().video_mode_set());
  r.log.notices.clear();

  r.pc().write_memory(0xA0000, 0x11);

  EXPECT_TRUE(r.log.notices.empty());
}

}  // namespace
}  // namespace amberfolio::machine
