// SPDX-License-Identifier: AGPL-3.0-only
//
// modern-controls on Secret of the Silver Blades (#609): the hero keys at
// the party menu's member picker, the selection block, and the two pieces
// that observe, `text-entry` and `aim-watch`, with points of their own. Each test stands
// the processor on one of the program's points (a HLT there, so the step is
// the arrival and nothing else) and reads what the handler left; the
// scene's side of the aim is in scene_test.cpp.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. **Every byte here is this
// file's own** (PLAN.md §6).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_entry.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";
constexpr std::string_view secret_fingerprint =
    "8b937984878ec72aa531335dd381a21bda67675408b5fd8e716a35ccd049999e";

// --- Secret's facts, restated -----------------------------------------------

/// The line editor's first instruction and its one return, in the resident
/// image, from the image segment.
constexpr std::uint16_t editor_entry = 0x5B48;
constexpr std::uint16_t editor_return = 0x5D87;

/// The word the overlay manager keeps overlay 16's segment in, and the aim
/// bar's five points there: target to target's meeting point and its wait's
/// return, then manual's halving, meeting point and its wait's return.
constexpr std::uint16_t word_aim = 0xCA0;
constexpr std::array<std::uint16_t, 5> aim_points{0x2DE8, 0x2E70, 0x31CB,
                                                  0x337C, 0x33DA};

/// The hero keys': the words of overlays 29 (the menu-bar routine), 22 (the
/// party menu's picker) and 27 (the roster drawer); the routine's read
/// call; the picker's call into it, by the instruction after; the drawer's
/// four points and the three in the routine that draws one row again.
constexpr std::uint16_t word_menu = 0x16C0;
constexpr std::uint16_t word_picker = 0x1010;
constexpr std::uint16_t word_summary = 0x1520;
constexpr std::uint16_t read_point = 0x05A0;
constexpr std::uint16_t picker_return = 0x4120;
constexpr std::array<std::uint16_t, 4> roster_points{0x0887, 0x08C5, 0x0909,
                                                     0x084F};
constexpr std::array<std::uint16_t, 3> row_points{0x0A1B, 0x0A50, 0x0A94};
/// The glyph blitter, at its paragraph.
constexpr std::uint16_t glyph_paragraph = 0x0540;
constexpr std::uint16_t glyph_offset = 0x01E7;

/// The selection block's: the bar leaf's three glyph calls and the list
/// routine's highlighted row's string call (overlay 29); the roster drawer's
/// selected member, the hit points' colour chosen and the one-row routine's
/// selected member (overlay 27); an ability score's colour chosen (overlay
/// 22); the name field's string call (overlay 20, word `0xF00`); in the
/// resident image the row write, the step to the next cell, the fill's and
/// the picture's epilogues. The string calls a store follows back to: the
/// hit points', a score's, its mark's, the name's cursor letter's.
constexpr std::uint16_t word_roster = 0xF00;
constexpr std::uint16_t bar_group_call = 0x0271;
constexpr std::uint16_t bar_letter_call = 0x02CB;
constexpr std::uint16_t bar_rest_call = 0x0309;
constexpr std::uint16_t list_row_call = 0x104B;
constexpr std::uint16_t list_bar_return = 0x1651;
constexpr std::uint16_t roster_selected_call = 0x08A9;
constexpr std::uint16_t hit_points_chosen = 0x0BD5;
constexpr std::uint16_t row_selected_call = 0x0A34;
constexpr std::uint16_t score_chosen = 0x0AF9;
constexpr std::uint16_t name_string_call = 0x256B;
constexpr std::array<std::uint16_t, 4> blitter_points{0x62B4, 0x56C0, 0x5573,
                                                      0x3CBE};
constexpr std::uint16_t string_glyph_return = 0x57D4;
constexpr std::uint16_t hit_points_string_call = 0x0C0D;
constexpr std::uint16_t hit_points_maximum_call = 0x0C60;
constexpr std::uint16_t score_string_call = 0x0B5E;
constexpr std::uint16_t mark_string_call = 0x0C8E;
constexpr std::uint16_t name_cursor_call = 0x25A5;

/// The data segment; the selected member, the head and the member whose
/// row is drawn again unselected, far pointers; a record's next member;
/// the pushback slot.
constexpr std::uint16_t dgroup_paragraphs = 0xDE2;
constexpr std::uint16_t data_current = 0x7D38;
constexpr std::uint16_t data_head = 0x7D3C;
constexpr std::uint16_t data_previous = 0x7D34;
constexpr std::uint16_t record_next = 0x19D;
constexpr std::uint16_t data_pushback = 0xA6E9;

/// Where this test puts the overlays, the party and a stack.
constexpr std::uint16_t aim_segment = 0x6C00;
constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t picker_segment = 0x6400;
constexpr std::uint16_t summary_segment = 0x6800;
constexpr std::uint16_t roster_segment = 0x7000;
constexpr std::uint16_t party_segment = 0x5800;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t ring_first = 0x1E;

/// The keys, as the BIOS ring holds them: the number row's 3, 4 and 5, the
/// keypad's 3 with Num Lock on, Up, and the key the program throws away.
constexpr std::uint16_t ring_3 = 0x0433;
constexpr std::uint16_t ring_4 = 0x0534;
constexpr std::uint16_t ring_5 = 0x0635;
constexpr std::uint16_t ring_keypad_3 = 0x5133;
constexpr std::uint16_t ring_up = 0x4800;
constexpr std::uint16_t ring_ignored = 0x0C2D;

[[nodiscard]] sha256_digest secret_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(secret_fingerprint, digest));
  return digest;
}

struct secret_rig {
  secret_rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(secret_digest(), image_load_segment);
    const std::uint32_t word =
        cpu::physical_address(image_load_segment, word_aim);
    box->memory().ram()[word] = static_cast<std::uint8_t>(aim_segment & 0xFFU);
    box->memory().ram()[word + 1] =
        static_cast<std::uint8_t>(aim_segment >> 8U);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    put_word(image_load_segment, word_menu, menu_segment);
    put_word(image_load_segment, word_picker, picker_segment);
    put_word(image_load_segment, word_summary, summary_segment);
    put_word(image_load_segment, word_roster, roster_segment);
  }

  [[nodiscard]] static std::uint16_t data_segment() {
    return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
  }

  void put_byte(std::uint16_t segment, std::uint16_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = value;
  }

  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(value));
    put_byte(segment, static_cast<std::uint16_t>(offset + 1),
             static_cast<std::uint8_t>(value >> 8U));
  }

  [[nodiscard]] std::uint8_t byte(std::uint16_t segment,
                                  std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        byte(segment, offset) |
        (byte(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
  }

  /// A party of `size`, its members `0x200` apart in their own segment,
  /// each a name of three letters, with `selected` the selected member.
  void lay_party(unsigned size, unsigned selected) const {
    for (unsigned i = 0; i < size; ++i) {
      const auto at = static_cast<std::uint16_t>(i * 0x200U);
      put_byte(party_segment, at, 3);
      const bool last = i + 1U == size;
      put_word(party_segment, static_cast<std::uint16_t>(at + record_next),
               last ? 0 : static_cast<std::uint16_t>(at + 0x200U));
      put_word(party_segment, static_cast<std::uint16_t>(at + record_next + 2),
               last ? 0 : party_segment);
    }
    put_word(data_segment(), data_head, 0);
    put_word(data_segment(), data_head + 2, party_segment);
    put_word(data_segment(), data_current,
             static_cast<std::uint16_t>(selected * 0x200U));
    put_word(data_segment(), data_current + 2, party_segment);
  }

  /// The member a far pointer in the data segment names.
  [[nodiscard]] unsigned member_at(std::uint16_t pointer) const {
    EXPECT_EQ(word(data_segment(), static_cast<std::uint16_t>(pointer + 2)),
              party_segment);
    return word(data_segment(), pointer) / 0x200U;
  }

  /// A HLT at `segment:offset`, the processor on it with DS the data
  /// segment and SS:BP the test's frame.
  void stand_on(std::uint16_t segment, std::uint16_t offset) const {
    put_byte(segment, offset, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = segment;
    r.ip = offset;
    r[cpu::sreg::ds] = data_segment();
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
  }

  /// At the read call inside the menu-bar routine, called from
  /// `segment:offset`, with `key` at the head of the ring: one step, and
  /// what the head then holds.
  [[nodiscard]] std::uint16_t press(std::uint16_t key, std::uint16_t segment,
                                    std::uint16_t offset) const {
    put_word(stack_segment, frame_base + 2, offset);
    put_word(stack_segment, frame_base + 4, segment);
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first + 2);
    put_word(0x40, ring_first, key);
    stand_on(menu_segment, read_point);
    box->step();
    return word(0x40, word(0x40, 0x1A));
  }

  /// Stand on `segment:offset` with BP on a frame of zeros, and take one
  /// step; the instruction there is a HLT.
  void arrive(std::uint16_t segment, std::uint16_t offset) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = segment;
    r.ip = offset;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::bp] = frame_base;
    r[cpu::reg16::sp] = static_cast<std::uint16_t>(frame_base - 0x300);
    box->step();
  }

  std::unique_ptr<machine> box;
};

[[nodiscard]] const seam_definition& definition() {
  for (const seam_definition& s : all_seams()) {
    if (s.id == seam_id) {
      return s;
    }
  }
  ADD_FAILURE() << "no seam is called " << seam_id;
  return all_seams().front();
}

TEST(SeamModernControlsOnSecret,
     IsTheHeroKeysTheSelectionBlockAndTheTwoPiecesThatObserve) {
  const std::span<const seam_point> mine =
      points_on(definition(), secret_digest());
  // Hero keys 8, the selection block 13, text entry 2, aim watch 5.
  ASSERT_EQ(mine.size(), 28U);
  EXPECT_NE(mine.data(), definition().points.data());
  EXPECT_EQ(mine[0].offset, read_point);
  EXPECT_EQ(mine[0].module.load_segment_at, word_menu);
  EXPECT_FALSE(mine[0].inside_calls);
  // The drawer's four and the row's three, inside a batch too: the
  // roster is given back in one.
  for (std::size_t i = 0; i < roster_points.size(); ++i) {
    EXPECT_EQ(mine[1 + i].offset, roster_points[i]) << i;
  }
  for (std::size_t i = 0; i < row_points.size(); ++i) {
    EXPECT_EQ(mine[5 + i].offset, row_points[i]) << i;
  }
  for (std::size_t i = 1; i < 8; ++i) {
    EXPECT_EQ(mine[i].module.load_segment_at, word_summary) << i;
    EXPECT_TRUE(mine[i].inside_calls) << i;
  }
  // The selection block's thirteen: the seven that choose a colour, the
  // row write and the three that settle the margin, the name field's, and
  // the selected member's in the routine that draws one row again.
  const std::array<std::uint16_t, 7> colours{
      bar_group_call,    list_row_call,   roster_selected_call, score_chosen,
      hit_points_chosen, bar_letter_call, bar_rest_call};
  const std::array<std::uint16_t, 7> words{
      word_menu,    word_menu, word_summary, word_picker,
      word_summary, word_menu, word_menu};
  for (std::size_t i = 0; i < colours.size(); ++i) {
    EXPECT_EQ(mine[8 + i].offset, colours[i]) << i;
    EXPECT_EQ(mine[8 + i].module.load_segment_at, words[i]) << i;
    EXPECT_EQ(mine[8 + i].inside_calls, i == 2) << i;
  }
  for (std::size_t i = 0; i < blitter_points.size(); ++i) {
    EXPECT_EQ(mine[15 + i].offset, blitter_points[i]) << i;
    EXPECT_TRUE(mine[15 + i].module.is_resident_image()) << i;
    EXPECT_TRUE(mine[15 + i].inside_calls) << i;
  }
  EXPECT_EQ(mine[19].offset, name_string_call);
  EXPECT_EQ(mine[19].module.load_segment_at, word_roster);
  EXPECT_FALSE(mine[19].inside_calls);
  EXPECT_EQ(mine[20].offset, row_selected_call);
  EXPECT_EQ(mine[20].module.load_segment_at, word_summary);
  EXPECT_TRUE(mine[20].inside_calls) << "a picker's step is drawn in a batch";

  EXPECT_EQ(mine[21].offset, editor_entry);
  EXPECT_EQ(mine[22].offset, editor_return);
  for (std::size_t i = 21; i < 23; ++i) {
    EXPECT_TRUE(mine[i].module.is_resident_image()) << i;
  }
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[23 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[23 + i].module.load_segment_at, word_aim) << i;
  }
  for (std::size_t i = 21; i < mine.size(); ++i) {
    EXPECT_FALSE(mine[i].inside_calls) << i;
  }
  for (const seam_point& p : mine) {
    EXPECT_FALSE(p.at_every_step);
  }
}

// --- The hero keys -----------------------------------------------------------

TEST(SeamModernControlsOnSecret, ADigitAtThePartyMenusPickerSelectsThatMember) {
  const secret_rig r;
  r.arm();
  r.lay_party(4, 1);
  // The number row's 3: the selection goes on the fourth member, whose
  // predecessor the program's cursor steps back to on the Up it reads, and
  // the row the picker draws unselected first is the second's, the one
  // that was selected.
  EXPECT_EQ(r.press(ring_3, picker_segment, picker_return), ring_up);
  EXPECT_EQ(r.member_at(data_current), 3U);
  EXPECT_EQ(r.member_at(data_previous), 1U);
  // The last member: the selection goes on the head, and Up wraps.
  EXPECT_EQ(r.press(ring_4, picker_segment, picker_return), ring_up);
  EXPECT_EQ(r.member_at(data_current), 0U);
  EXPECT_EQ(r.member_at(data_previous), 3U);
  // Nobody behind the digit: the key the routine throws away.
  EXPECT_EQ(r.press(ring_5, picker_segment, picker_return), ring_ignored);
  EXPECT_EQ(r.member_at(data_current), 0U);
}

TEST(SeamModernControlsOnSecret, EveryOtherCallerAndTheKeypadKeepTheirDigits) {
  const secret_rig r;
  r.arm();
  r.lay_party(4, 1);
  // The keypad's 3 with Num Lock on is the program's.
  EXPECT_EQ(r.press(ring_keypad_3, picker_segment, picker_return),
            ring_keypad_3);
  // The same offset in another module, and another offset in this one:
  // the adventuring bar's and every other bar's digits are the keypad's.
  EXPECT_EQ(r.press(ring_3, summary_segment, picker_return), ring_3);
  EXPECT_EQ(r.press(ring_3, picker_segment, 0x4121), ring_3);
  EXPECT_EQ(r.member_at(data_current), 1U);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

TEST(SeamModernControlsOnSecret, LeavesTheKeyAloneWhileThePushbackIsArmed) {
  const secret_rig r;
  r.arm();
  r.lay_party(4, 1);
  r.put_byte(secret_rig::data_segment(), data_pushback, 0x48);
  EXPECT_EQ(r.press(ring_3, picker_segment, picker_return), ring_3);
  EXPECT_EQ(r.member_at(data_current), 1U);
}

TEST(SeamModernControlsOnSecret, DoesNothingWhileItIsOff) {
  const secret_rig r;
  r.put_word(image_load_segment, word_menu, menu_segment);
  r.put_word(image_load_segment, word_picker, picker_segment);
  r.lay_party(4, 1);
  EXPECT_EQ(r.press(ring_3, picker_segment, picker_return), ring_3);
  EXPECT_EQ(r.member_at(data_current), 1U);
}

/// A row-drawing frame on the party menu's list: column one, the third
/// member's row, its far pointer; the column and row where `column_below`
/// and `row_below` put them.
void lay_row_frame(const secret_rig& r, std::uint16_t column_below,
                   std::uint16_t row_below, std::uint8_t column) {
  r.lay_party(4, 0);
  r.put_byte(stack_segment, frame_base - column_below, column);
  r.put_byte(stack_segment, frame_base - row_below, 6);
  r.put_word(stack_segment, frame_base - 4, 0x0400);
  r.put_word(stack_segment, frame_base - 2, party_segment);
}

/// After an arrival that called the blitter: CS:IP is the blitter's, and the
/// words on the stack its far return to the point and its arguments.
void expect_the_number_drawn(const secret_rig& r, std::uint16_t point) {
  const cpu::registers& regs = r.box->processor().regs();
  EXPECT_EQ(regs[cpu::sreg::cs], image_load_segment + glyph_paragraph);
  EXPECT_TRUE(regs.ip == glyph_offset || regs.ip == glyph_offset + 1U);
  const std::uint16_t sp = regs[cpu::reg16::sp];
  // The far return is the point itself; then the fold, the character, a
  // count of one, black paper, white, the row and the column.
  const std::array<std::uint16_t, 9> expect{
      point, summary_segment, 1, '3', 1, 0, 0x0F, 6, 0x01};
  for (std::size_t i = 0; i < expect.size(); ++i) {
    EXPECT_EQ(r.word(stack_segment, static_cast<std::uint16_t>(sp + (2 * i))),
              expect[i])
        << i;
  }
}

TEST(SeamModernControlsOnSecret, ThePartyMenusRosterDrawsTheNumberWithPaper) {
  const secret_rig r;
  r.arm();
  lay_row_frame(r, 5, 6, 0x01);
  r.put_byte(static_cast<std::uint16_t>(image_load_segment + glyph_paragraph),
             glyph_offset, 0x90);
  r.stand_on(summary_segment, roster_points[0]);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 5), 0x03)
      << "the name moves two columns right";
  expect_the_number_drawn(r, roster_points[0]);
}

TEST(SeamModernControlsOnSecret, ARowDrawnAgainKeepsItsNumberAndItsColumns) {
  const secret_rig r;
  r.arm();
  // The routine that draws one row again keeps its column at BP-9 and its
  // row at BP-0x0A.
  lay_row_frame(r, 9, 0x0A, 0x01);
  r.put_byte(static_cast<std::uint16_t>(image_load_segment + glyph_paragraph),
             glyph_offset, 0x90);
  r.stand_on(summary_segment, row_points[0]);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 9), 0x03);
  expect_the_number_drawn(r, row_points[0]);

  // Reached again, the number drawn: nothing more.
  r.stand_on(summary_segment, row_points[0]);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 9), 0x03);
  EXPECT_EQ(r.box->processor().regs()[cpu::sreg::cs], summary_segment);

  // The name drawn: the column goes back.
  r.stand_on(summary_segment, row_points[1]);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 9), 0x01);

  // The armour class moves one right.
  r.stand_on(summary_segment, row_points[2]);
  r.box->processor().regs()[cpu::reg16::ax] = 0x21;
  r.box->step();
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x22);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

TEST(SeamModernControlsOnSecret,
     BesideTheViewportARowDrawnAgainIsTheProgramsOwn) {
  const secret_rig r;
  r.arm();
  lay_row_frame(r, 9, 0x0A, 0x11);
  r.stand_on(summary_segment, row_points[0]);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 9), 0x11);
  EXPECT_EQ(r.box->processor().regs()[cpu::sreg::cs], summary_segment);
  r.stand_on(summary_segment, row_points[2]);
  r.box->processor().regs()[cpu::reg16::ax] = 0x21;
  r.box->step();
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x21);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

// --- The selection block -----------------------------------------------------

/// The bar leaf's frame for one character of `Save View`, the highlight on
/// its first word, and the glyph call the leaf makes for it, with the
/// program's colour on the ink or on the paper. The menu-bar routine keeps
/// its copy's length at `caller - length_below` and its group table at
/// `caller - groups_below`, and returns to `back` in overlay 29.
void lay_bar_call(const secret_rig& r, std::uint8_t index, std::uint8_t ink,
                  std::uint8_t paper, std::string_view bar = "Save View",
                  std::uint16_t length_below = 0x68,
                  std::uint16_t groups_below = 0x93,
                  std::uint16_t back = 0x0100) {
  constexpr std::uint16_t caller = 0x0800;
  r.put_word(stack_segment, frame_base + 6, caller);
  r.put_byte(stack_segment, frame_base + 8, 1);
  r.put_byte(stack_segment, frame_base - 1, index);
  r.put_word(stack_segment, caller + 2, back);
  r.put_word(stack_segment, caller + 4, menu_segment);
  r.put_byte(stack_segment, caller + 0x0E, 0x0F);
  r.put_byte(stack_segment, caller + 0x10, 0x0A);
  r.put_byte(stack_segment, static_cast<std::uint16_t>(caller - length_below),
             static_cast<std::uint8_t>(bar.size()));
  for (std::size_t i = 0; i < bar.size(); ++i) {
    r.put_byte(stack_segment, static_cast<std::uint16_t>(caller - 0x53 + 1 + i),
               static_cast<std::uint8_t>(bar[i]));
  }
  // `Save` from 1 to 4, then `View` from 6 to 9 where there is one.
  const auto groups = static_cast<std::uint16_t>(caller - groups_below);
  r.put_byte(stack_segment, groups + 2, 1);
  r.put_byte(stack_segment, groups + 3, 4);
  r.put_byte(stack_segment, groups + 4, bar.size() > 4 ? 6 : 0);
  r.put_byte(stack_segment, groups + 5, bar.size() > 4 ? 9 : 0);
  // The call, from the top: fold, character, count, paper, ink, row,
  // column.
  const std::array<std::uint16_t, 7> words{
      1,    static_cast<std::uint16_t>(bar[index - 1U]), 1, paper, ink, 0x18,
      index};
  for (std::size_t i = 0; i < words.size(); ++i) {
    r.put_word(stack_segment, static_cast<std::uint16_t>(0x0400 + (2 * i)),
               words[i]);
  }
}

TEST(SeamModernControlsOnSecret, TheBarsWhitePaperBecomesAWhiteWord) {
  const secret_rig r;
  r.arm();
  // The program draws its highlighted group black on white paper: the word
  // comes out white, key and rest, on black paper, for the row write to
  // make the same white block of.
  lay_bar_call(r, 1, 0, 0x0F);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F) << "the key";
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);

  lay_bar_call(r, 2, 0, 0x0F);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F) << "the rest of the word";
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);

  // Outside the highlight the letters and the rest keep their colours.
  lay_bar_call(r, 6, 0x0F, 0);
  r.stand_on(menu_segment, bar_letter_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F);
  lay_bar_call(r, 7, 0x0A, 0);
  r.stand_on(menu_segment, bar_rest_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0A);
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

TEST(SeamModernControlsOnSecret, ABarInTheFirstTwoProgramsFrameIsNotRead) {
  const secret_rig r;
  r.arm();
  // The length and the group table four bytes higher, where Pool of
  // Radiance's and Curse of the Azure Bonds' routine keeps them: not this
  // program's frame, and nothing is touched.
  lay_bar_call(r, 2, 0, 0x0F, "Save View", 0x64, 0x8F);
  r.put_byte(stack_segment, 0x0800 - 0x68, 0);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x0F);
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x00);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1U);
}

TEST(SeamModernControlsOnSecret, AListsLoneCommandIsDrawnUnselected) {
  const secret_rig r;
  r.arm();
  // `Select` alone under the party menu's list: Return takes the row, which
  // is the selection, so the word is white letters on black.
  lay_bar_call(r, 2, 0, 0x0F, "Save", 0x68, 0x93, list_bar_return);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F);
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);

  // Under any other caller a lone command is lit, as on the first two
  // programs: drawn white on black here, and a block at the row write.
  lay_bar_call(r, 2, 0, 0x0F, "Save", 0x68, 0x93, 0x0100);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F);
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

/// The row write's frames for a glyph of a string the string routine draws
/// in `colour` for a caller that returns to `segment:offset`, the caller's
/// own frame at `caller`, and AH the glyph's row.
void lay_string_glyph(const secret_rig& r, std::uint16_t colour,
                      std::uint16_t segment, std::uint16_t offset,
                      std::uint16_t caller = 0) {
  constexpr std::uint16_t blitter = frame_base + 0x40;
  constexpr std::uint16_t drawer = frame_base + 0x80;
  // The EGA path's frame (BP) holds the blitter's BP; the blitter's frame
  // returns into the string routine, whose frame returns to the caller.
  r.put_word(stack_segment, frame_base, blitter);
  r.put_word(stack_segment, blitter, drawer);
  r.put_word(stack_segment, blitter + 2, string_glyph_return);
  r.put_word(stack_segment, blitter + 4, image_load_segment);
  r.put_word(stack_segment, drawer, caller);
  r.put_word(stack_segment, drawer + 2, offset);
  r.put_word(stack_segment, drawer + 4, segment);
  r.put_word(stack_segment, drawer + 0x0C, colour);
  r.stand_on(image_load_segment, blitter_points[0]);
  r.box->processor().regs().set(cpu::reg8::ah, 0x3C);
}

[[nodiscard]] std::uint8_t row_after_step(const secret_rig& r) {
  r.box->step();
  return r.box->processor().regs().get(cpu::reg8::ah);
}

/// The row write's frames for a character of the bar `lay_bar_call` laid,
/// drawn through the leaf's call at `call`: the EGA path's frame holds the
/// blitter's BP, whose frame returns into the leaf and holds the leaf's BP,
/// its arguments the call's words.
void stand_on_bar_row_write(const secret_rig& r, std::uint16_t call) {
  constexpr std::uint16_t path = 0x0300;
  constexpr std::uint16_t blitter = 0x0400 - 6;
  r.put_word(stack_segment, path, blitter);
  r.put_word(stack_segment, blitter, frame_base);
  r.put_word(stack_segment, blitter + 2, call + 5U);
  r.put_word(stack_segment, blitter + 4, menu_segment);
  r.stand_on(image_load_segment, blitter_points[0]);
  r.box->processor().regs()[cpu::reg16::bp] = path;
  r.box->processor().regs().set(cpu::reg8::ah, 0x3C);
}

TEST(SeamModernControlsOnSecret, TheLitWordIsABlockAndAListsLoneCommandIsNot) {
  const secret_rig r;
  r.arm();
  // A character of the lit word, as the point left it: white ink.
  lay_bar_call(r, 2, 0x0F, 0);
  stand_on_bar_row_write(r, bar_group_call);
  EXPECT_EQ(row_after_step(r), 0xC3);
  // A character outside it.
  lay_bar_call(r, 7, 0x0A, 0);
  stand_on_bar_row_write(r, bar_rest_call);
  EXPECT_EQ(row_after_step(r), 0x3C);
  // A lone command: lit under any caller but the list routine.
  lay_bar_call(r, 2, 0x0F, 0, "Save", 0x68, 0x93, 0x0100);
  stand_on_bar_row_write(r, bar_group_call);
  EXPECT_EQ(row_after_step(r), 0xC3);
  lay_bar_call(r, 2, 0x0F, 0, "Save", 0x68, 0x93, list_bar_return);
  stand_on_bar_row_write(r, bar_group_call);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, AListsRowIsHandedWhiteInkAndInverted) {
  const secret_rig r;
  r.arm();
  // The highlight-on leaf's string call: black ink on the list's white
  // paper, the list's context saying white.
  constexpr std::uint16_t context = 0x0700;
  r.put_word(stack_segment, frame_base + 6, context);
  r.put_byte(stack_segment, context + 0x20, 0x0F);
  // From the top: the string's offset and segment, the paper, the ink.
  r.put_word(stack_segment, 0x0400, 0x0300);
  r.put_word(stack_segment, 0x0402, stack_segment);
  r.put_word(stack_segment, 0x0404, 0x0F);
  r.put_word(stack_segment, 0x0406, 0x00);
  r.stand_on(menu_segment, list_row_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0404), 0x00) << "black paper";
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x0F) << "white ink";
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);

  lay_string_glyph(r, 0x0F, menu_segment, list_row_call + 5U);
  EXPECT_EQ(row_after_step(r), 0xC3);
  // Black, the program's own ink, is not a selection; nor is white from a
  // call nobody named.
  lay_string_glyph(r, 0x00, menu_segment, list_row_call + 5U);
  EXPECT_EQ(row_after_step(r), 0x3C);
  lay_string_glyph(r, 0x0F, menu_segment, 0x0100);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, TheSelectedMemberIsAWhiteBlockInBothRoutines) {
  const secret_rig r;
  r.arm();
  // The drawer's selected-member arm and the one-row routine's draw white,
  // and white is the selection here.
  for (const std::uint16_t call : {roster_selected_call, row_selected_call}) {
    lay_string_glyph(r, 0x0F, summary_segment, call + 5U);
    EXPECT_EQ(row_after_step(r), 0xC3) << call;
    lay_string_glyph(r, 0x0B, summary_segment, call + 5U);
    EXPECT_EQ(row_after_step(r), 0x3C) << call;
  }
}

/// The selected member's string call at the top of the stack: the member's
/// far pointer, black paper, white, the row and the column; and the frame
/// of the routine that makes it, with its column and row bytes
/// `column_below` and `row_below` below BP.
void lay_selected_call(const secret_rig& r, std::uint16_t column_below,
                       std::uint16_t row_below) {
  lay_row_frame(r, column_below, row_below, 0x03);
  const std::array<std::uint16_t, 6> words{0x0400, party_segment, 0, 0x0F,
                                           6,      0x03};
  for (std::size_t i = 0; i < words.size(); ++i) {
    r.put_word(stack_segment, static_cast<std::uint16_t>(0x0400 + (2 * i)),
               words[i]);
  }
}

TEST(SeamModernControlsOnSecret, EachSelectedMemberCallIsReadInItsOwnFrame) {
  const secret_rig r;
  r.arm();
  lay_selected_call(r, 5, 6);
  r.stand_on(summary_segment, roster_selected_call);
  r.box->step();
  lay_selected_call(r, 9, 0x0A);
  r.stand_on(summary_segment, row_selected_call);
  r.box->step();
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
  EXPECT_EQ(r.word(stack_segment, 0x0406), 0x0F) << "white is left as it is";

  // The drawer's frame at the one-row routine's call is not the call these
  // facts describe.
  r.put_byte(stack_segment, frame_base - 9, 0x00);
  r.put_byte(stack_segment, frame_base - 0x0A, 0x00);
  lay_selected_call(r, 5, 6);
  r.stand_on(summary_segment, row_selected_call);
  r.box->step();
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1U);
}

TEST(SeamModernControlsOnSecret, ModifysHighlightedScoreAndItsMarkAreWhite) {
  const secret_rig r;
  r.arm();
  // A highlighted draw (a nonzero first argument) in light magenta.
  r.put_byte(stack_segment, frame_base + 6, 1);
  r.put_byte(stack_segment, frame_base - 0x2B, 0x0D);
  r.stand_on(picker_segment, score_chosen);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 0x2B), 0x0F);

  // The score, its percentage and the mark beside it are blocks when the
  // draw is the highlighted one.
  constexpr std::uint16_t score = frame_base + 0x100;
  for (const std::uint16_t call : {score_string_call, mark_string_call}) {
    r.put_byte(stack_segment, score + 6, 1);
    lay_string_glyph(r, 0x0F, picker_segment, call + 5U, score);
    EXPECT_EQ(row_after_step(r), 0xC3) << call;
    r.put_byte(stack_segment, score + 6, 0);
    lay_string_glyph(r, 0x0F, picker_segment, call + 5U, score);
    EXPECT_EQ(row_after_step(r), 0x3C) << call;
  }
}

TEST(SeamModernControlsOnSecret, ModifysHighlightedHitPointsAreWhite) {
  const secret_rig r;
  r.arm();
  // The draw's highlighted argument at BP+8 and its colour below a buffer
  // of its own, at BP-0x101; BP+6 says whether the maximum follows.
  r.put_byte(stack_segment, frame_base + 6, 0);
  r.put_byte(stack_segment, frame_base + 8, 1);
  r.put_byte(stack_segment, frame_base - 0x101, 0x0D);
  r.stand_on(summary_segment, hit_points_chosen);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 0x101), 0x0F);

  // Not highlighted, with the maximum to follow: a hurt member's yellow is
  // left, and is not a block.
  r.put_byte(stack_segment, frame_base + 6, 1);
  r.put_byte(stack_segment, frame_base + 8, 0);
  r.put_byte(stack_segment, frame_base - 0x101, 0x0E);
  r.stand_on(summary_segment, hit_points_chosen);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 0x101), 0x0E);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);

  constexpr std::uint16_t draw = frame_base + 0x100;
  r.put_byte(stack_segment, draw + 6, 0);
  r.put_byte(stack_segment, draw + 8, 1);
  lay_string_glyph(r, 0x0F, summary_segment, hit_points_string_call + 5U, draw);
  EXPECT_EQ(row_after_step(r), 0xC3);
  // The maximum after it is the program's green, and is not a block.
  lay_string_glyph(r, 0x0F, summary_segment, hit_points_maximum_call + 5U,
                   draw);
  EXPECT_EQ(row_after_step(r), 0x3C);
  // The maximum's flag alone is not highlighted.
  r.put_byte(stack_segment, draw + 6, 1);
  r.put_byte(stack_segment, draw + 8, 0);
  lay_string_glyph(r, 0x0F, summary_segment, hit_points_string_call + 5U, draw);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, TheNameIsABlockAndItsCursorLetterIsNot) {
  const secret_rig r;
  r.arm();
  // The name field's string call: the name's far pointer, the paper, light
  // magenta, the first row and the first column.
  const std::array<std::uint16_t, 6> words{0x0010, party_segment, 0, 0x050D, 1,
                                           1};
  for (std::size_t i = 0; i < words.size(); ++i) {
    r.put_word(stack_segment, static_cast<std::uint16_t>(0x0400 + (2 * i)),
               words[i]);
  }
  r.stand_on(roster_segment, name_string_call);
  r.box->step();
  EXPECT_EQ(r.word(stack_segment, 0x0406), 0x050F);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);

  lay_string_glyph(r, 0x0F, roster_segment, name_string_call + 5U);
  EXPECT_EQ(row_after_step(r), 0xC3);
  lay_string_glyph(r, 0x0F, roster_segment, name_cursor_call + 5U);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, ASeamsMarkedStringIsInverted) {
  const secret_rig r;
  r.arm();
  // A string a batch draws, its colour word marked a selection (#483).
  lay_string_glyph(r, 0x800F, 0xF000, 0x0800);
  EXPECT_EQ(row_after_step(r), 0xC3);
  lay_string_glyph(r, 0x000F, 0xF000, 0x0800);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, CursesRowWriteIsNotAPointHere) {
  const secret_rig r;
  r.arm();
  lay_string_glyph(r, 0x800F, 0xF000, 0x0800);
  r.stand_on(image_load_segment, 0x5A93);
  r.box->processor().regs().set(cpu::reg8::ah, 0x3C);
  EXPECT_EQ(row_after_step(r), 0x3C);
}

TEST(SeamModernControlsOnSecret, IsUnknownUntilItIsOn) {
  const secret_rig r;
  EXPECT_EQ(text_entry_now(*r.box), text_entry::unknown);
  r.arrive(image_load_segment, editor_entry);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::unknown)
      << "nothing was watching";
}

TEST(SeamModernControlsOnSecret, NotesTheLineEditorFromEntryToReturn) {
  const secret_rig r;
  r.arm();
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
  r.arrive(image_load_segment, editor_entry);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::reading);
  r.arrive(image_load_segment, editor_return);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
}

TEST(SeamModernControlsOnSecret, TheOtherProgramsEditorsAreNotPointsHere) {
  const secret_rig r;
  r.arm();
  // Pool of Radiance's and Curse of the Azure Bonds' editor entries.
  for (const std::uint16_t other :
       {std::uint16_t{0x7A73}, std::uint16_t{0x53F6}}) {
    r.arrive(image_load_segment, other);
    EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading) << other;
  }
}

TEST(SeamModernControlsOnSecret,
     ThePiecesThatObserveWriteNothingOfTheMachines) {
  // Their every point, with the seam on and with it off: the same memory
  // and the same registers after the step.
  const secret_rig on;
  on.arm();
  const secret_rig off;
  off.put_word(image_load_segment, word_menu, menu_segment);
  off.put_word(image_load_segment, word_picker, picker_segment);
  off.put_word(image_load_segment, word_summary, summary_segment);
  off.put_word(image_load_segment, word_roster, roster_segment);
  const auto same = [&](std::uint16_t segment, std::uint16_t offset) {
    on.arrive(segment, offset);
    off.arrive(segment, offset);
    const std::span<const std::uint8_t> a = on.box->memory().ram();
    const std::span<const std::uint8_t> b = off.box->memory().ram();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin())) << offset;
    EXPECT_EQ(on.box->processor().regs().ip, off.box->processor().regs().ip);
    EXPECT_EQ(on.box->processor().regs()[cpu::reg16::sp],
              off.box->processor().regs()[cpu::reg16::sp]);
    EXPECT_EQ(on.box->processor().regs()[cpu::reg16::ax],
              off.box->processor().regs()[cpu::reg16::ax]);
  };
  same(image_load_segment, editor_entry);
  same(image_load_segment, editor_return);
  for (const std::uint16_t point : aim_points) {
    same(aim_segment, point);
  }
}

}  // namespace
}  // namespace amberfolio::machine
