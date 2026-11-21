// SPDX-License-Identifier: AGPL-3.0-only
//
// modern-controls on Secret of the Silver Blades (#609): the hero keys at
// the party menu's member picker, and the two pieces that observe,
// `text-entry` and `aim-watch`, with points of their own. Each test stands
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

TEST(SeamModernControlsOnSecret, IsTheHeroKeysAndTheTwoPiecesThatObserve) {
  const std::span<const seam_point> mine =
      points_on(definition(), secret_digest());
  // Hero keys 8, text entry 2, aim watch 5.
  ASSERT_EQ(mine.size(), 15U);
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
  EXPECT_EQ(mine[8].offset, editor_entry);
  EXPECT_EQ(mine[9].offset, editor_return);
  for (std::size_t i = 8; i < 10; ++i) {
    EXPECT_TRUE(mine[i].module.is_resident_image()) << i;
  }
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[10 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[10 + i].module.load_segment_at, word_aim) << i;
  }
  for (std::size_t i = 8; i < mine.size(); ++i) {
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
