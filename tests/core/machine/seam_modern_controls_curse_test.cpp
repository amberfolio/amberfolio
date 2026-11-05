// SPDX-License-Identifier: AGPL-3.0-only
//
// modern-controls on Curse of the Azure Bonds (#555): the pieces ported to
// it, with points of their own, exercised through their mechanism and not
// through any program. Each test stands the processor on one of Curse's
// points (a HLT there, so the step is the arrival and nothing else) with
// the frame, the ring or AL where Curse's facts put them, and reads what
// the handler left.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. **Every byte here is this
// file's own** (PLAN.md §6).

#include <array>
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
constexpr std::string_view curse_fingerprint =
    "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd";

// --- Curse's facts, restated ------------------------------------------------

/// The load-segment words of the modules the points and the callers are in:
/// overlays 5 (the temple), 6 (post-combat), 7 (the shop), 8 (the script
/// runner), 9 (combat), 14 (aim), 15 (the adventuring loop), 16 (camp),
/// 17 (the save slots), 18 (the main menu and the character editor), 20
/// (the character sheet), 25 (the party summary: the picker and the
/// roster drawer) and 27 (the menu-bar routine).
constexpr std::uint16_t word_temple = 0x430;
constexpr std::uint16_t word_post_combat = 0x460;
constexpr std::uint16_t word_shop = 0x490;
constexpr std::uint16_t word_script = 0x4D0;
constexpr std::uint16_t word_combat = 0x580;
constexpr std::uint16_t word_aim = 0x8E0;
constexpr std::uint16_t word_adventure = 0x9A0;
constexpr std::uint16_t word_camp = 0x9E0;
constexpr std::uint16_t word_slots = 0xA20;
constexpr std::uint16_t word_roster = 0xA80;
constexpr std::uint16_t word_sheet = 0xB00;
constexpr std::uint16_t word_picker = 0xEE0;
constexpr std::uint16_t word_menu = 0x1050;

/// The points: the pick-list's return and the read call in overlay 27; the
/// picker's return in overlay 25; the line editor's read return, entry
/// and return in the resident image; the aim bar's five in overlay 14.
constexpr std::uint16_t list_point = 0x10F3;
constexpr std::uint16_t read_point = 0x05F1;
constexpr std::uint16_t picker_point = 0x2EE5;
constexpr std::uint16_t editor_after_read = 0x5447;
constexpr std::uint16_t editor_entry = 0x53F6;
constexpr std::uint16_t editor_return = 0x5584;
constexpr std::array<std::uint16_t, 5> aim_points{0x314C, 0x31CC, 0x3522,
                                                  0x36B3, 0x3721};

/// The hero keys' four points in the roster drawer (overlay 25), and the
/// routine the number is drawn with: the glyph blitter at its paragraph.
constexpr std::array<std::uint16_t, 4> roster_points{0x0830, 0x086F, 0x08B3,
                                                     0x07FB};
constexpr std::uint16_t glyph_paragraph = 0x04CE;
constexpr std::uint16_t glyph_offset = 0x01DF;

/// The selection block's: the bar leaf's three glyph calls and the
/// pick-list's string call (overlay 27), the roster's selected member and
/// the hit points' colour (overlay 25), an ability score's colour (overlay
/// 20), and in the resident image the row write, the step to the next
/// cell, the fill's and the picture's epilogues.
constexpr std::uint16_t bar_group_call = 0x0285;
constexpr std::uint16_t bar_letter_call = 0x02DF;
constexpr std::uint16_t bar_rest_call = 0x031D;
constexpr std::uint16_t list_row_call = 0x0AFC;
constexpr std::uint16_t roster_selected_call = 0x0853;
constexpr std::uint16_t hit_points_chosen = 0x09E6;
constexpr std::uint16_t score_chosen = 0x09C5;
constexpr std::array<std::uint16_t, 4> blitter_points{0x5A93, 0x4F98, 0x4E4B,
                                                      0x3542};
/// The string routine's near call into the blitter: the instruction after.
constexpr std::uint16_t string_glyph_return = 0x509B;

/// The callers the hero keys and the bars that are not raw are named by.
constexpr std::uint16_t main_menu = 0x0393;
constexpr std::uint16_t save_slot_bar = 0x3FA7;

/// The out-parameters, below BP; the pushback slot; the data segment.
constexpr std::uint16_t list_flag_below_bp = 0x57;
constexpr std::uint16_t picker_flag_below_bp = 0x2B;
constexpr std::uint16_t data_pushback = 0x8CBF;
constexpr std::uint16_t dgroup_paragraphs = 0xABE;

/// The party: the selected member and the head, far pointers; a record's
/// next member at +0x189.
constexpr std::uint16_t data_current = 0x6520;
constexpr std::uint16_t data_head = 0x6524;
constexpr std::uint16_t record_next = 0x189;

/// Callers, by the instruction after their call into the menu-bar routine.
constexpr std::uint16_t camp_bar = 0x24FD;
constexpr std::uint16_t script_prompts = 0x1811;
constexpr std::uint16_t yes_no = 0x1231;
constexpr std::uint16_t adventure_bar = 0x092C;
constexpr std::uint16_t combat_bar = 0x0947;
constexpr std::uint16_t target_bar = 0x31CC;

/// Where the test puts the modules, apart from the stack and the data.
constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t temple_segment = 0x6200;
constexpr std::uint16_t post_combat_segment = 0x6400;
constexpr std::uint16_t shop_segment = 0x6600;
constexpr std::uint16_t script_segment = 0x6800;
constexpr std::uint16_t combat_segment = 0x6A00;
constexpr std::uint16_t aim_segment = 0x6C00;
constexpr std::uint16_t adventure_segment = 0x6E00;
constexpr std::uint16_t camp_segment = 0x7000;
constexpr std::uint16_t roster_segment = 0x7200;
constexpr std::uint16_t picker_segment = 0x7400;
constexpr std::uint16_t slots_segment = 0x7600;
constexpr std::uint16_t sheet_segment = 0x7800;
constexpr std::uint16_t party_segment = 0x5800;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t ring_first = 0x1E;

/// The keys, as the BIOS ring holds them.
constexpr std::uint16_t ring_left = 0x4B00;
constexpr std::uint16_t ring_right = 0x4D00;
constexpr std::uint16_t ring_up = 0x4800;
constexpr std::uint16_t ring_down = 0x5000;
constexpr std::uint16_t ring_home = 0x4700;
constexpr std::uint16_t ring_end = 0x4F00;
constexpr std::uint16_t ring_enter = 0x1C0D;
constexpr std::uint16_t ring_escape = 0x011B;
constexpr std::uint16_t ring_comma = 0x332C;
constexpr std::uint16_t ring_period = 0x342E;
constexpr std::uint16_t ring_no = 0x1C4E;

[[nodiscard]] sha256_digest curse_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(curse_fingerprint, digest));
  return digest;
}

[[nodiscard]] std::uint16_t data_segment() {
  return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
}

struct curse_rig {
  curse_rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(curse_digest(), image_load_segment);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    manager_says(word_menu, menu_segment);
    manager_says(word_temple, temple_segment);
    manager_says(word_post_combat, post_combat_segment);
    manager_says(word_shop, shop_segment);
    manager_says(word_script, script_segment);
    manager_says(word_combat, combat_segment);
    manager_says(word_aim, aim_segment);
    manager_says(word_adventure, adventure_segment);
    manager_says(word_camp, camp_segment);
    manager_says(word_roster, roster_segment);
    manager_says(word_picker, picker_segment);
    manager_says(word_slots, slots_segment);
    manager_says(word_sheet, sheet_segment);
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

  /// The member the selected-member pointer names.
  [[nodiscard]] unsigned selected() const {
    return word(data_segment(), data_current) / 0x200U;
  }

  void manager_says(std::uint16_t word, std::uint16_t segment) const {
    put_word(image_load_segment, word, segment);
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

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// A HLT at `segment:offset`, the processor on it with DS the data
  /// segment and SS:BP the test's frame.
  void stand_on(std::uint16_t segment, std::uint16_t offset) const {
    put_byte(segment, offset, 0xF4);
    box->processor().reset();
    cpu::registers& r = regs();
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

  /// At a return from the menu-bar routine with `key` in AL and `flag` in
  /// the out-parameter: one step, and AL.
  [[nodiscard]] std::uint8_t arrive(std::uint16_t segment, std::uint16_t offset,
                                    std::uint16_t below, std::uint8_t key,
                                    std::uint8_t flag) const {
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_base - below),
             flag);
    stand_on(segment, offset);
    regs().set(cpu::reg8::al, key);
    box->step();
    return regs().get(cpu::reg8::al);
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

// --- The definition ----------------------------------------------------------

TEST(SeamModernControlsOnCurse, IsAvailableWithThePortedPiecesPoints) {
  const curse_rig r;
  const std::span<const seam_point> mine =
      points_on(definition(), curse_digest());
  // list arrows 3, bar keys 1, hero keys 5, edit keys 1, the selection
  // block 11, text entry 2, aim watch 5.
  ASSERT_EQ(mine.size(), 28U);
  EXPECT_NE(mine.data(), definition().points.data());

  EXPECT_EQ(mine[0].offset, list_point);
  EXPECT_EQ(mine[0].module.load_segment_at, word_menu);
  EXPECT_EQ(mine[1].offset, picker_point);
  EXPECT_EQ(mine[1].module.load_segment_at, word_picker);
  // The read call: the list arrows' first, the bar keys' and the hero
  // keys' after.
  for (const std::size_t at : {2U, 3U, 4U}) {
    EXPECT_EQ(mine[at].offset, read_point) << at;
    EXPECT_EQ(mine[at].module.load_segment_at, word_menu) << at;
  }
  // The roster drawer's four, inside a batch too.
  for (std::size_t i = 0; i < roster_points.size(); ++i) {
    EXPECT_EQ(mine[5 + i].offset, roster_points[i]) << i;
    EXPECT_EQ(mine[5 + i].module.load_segment_at, word_picker) << i;
    EXPECT_TRUE(mine[5 + i].inside_calls) << i;
  }
  EXPECT_EQ(mine[9].offset, editor_after_read);
  // The selection block's eleven.
  const std::array<std::uint16_t, 7> colours{
      bar_group_call,    list_row_call,   roster_selected_call, score_chosen,
      hit_points_chosen, bar_letter_call, bar_rest_call};
  const std::array<std::uint16_t, 7> words{word_menu,  word_menu,   word_picker,
                                           word_sheet, word_picker, word_menu,
                                           word_menu};
  for (std::size_t i = 0; i < colours.size(); ++i) {
    EXPECT_EQ(mine[10 + i].offset, colours[i]) << i;
    EXPECT_EQ(mine[10 + i].module.load_segment_at, words[i]) << i;
  }
  EXPECT_TRUE(mine[12].inside_calls) << "the roster is drawn back in a batch";
  for (std::size_t i = 0; i < blitter_points.size(); ++i) {
    EXPECT_EQ(mine[17 + i].offset, blitter_points[i]) << i;
    EXPECT_TRUE(mine[17 + i].module.is_resident_image()) << i;
    EXPECT_TRUE(mine[17 + i].inside_calls) << i;
  }
  for (const std::size_t at : {9U, 21U, 22U}) {
    EXPECT_TRUE(mine[at].module.is_resident_image()) << at;
  }
  EXPECT_EQ(mine[21].offset, editor_entry);
  EXPECT_EQ(mine[22].offset, editor_return);
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[23 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[23 + i].module.load_segment_at, word_aim) << i;
  }
  EXPECT_EQ(r.box->seams().enable(seam_id), seam_reason::none);
}

// --- The list arrows ---------------------------------------------------------

TEST(SeamModernControlsOnCurse, UpAndDownStepThePickListAndThePicker) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.arrive(menu_segment, list_point, list_flag_below_bp, 0x48, 1),
            0x47);
  EXPECT_EQ(r.arrive(menu_segment, list_point, list_flag_below_bp, 0x50, 1),
            0x4F);
  // With the out-parameter clear, 0x50 is the bar's own P.
  EXPECT_EQ(r.arrive(menu_segment, list_point, list_flag_below_bp, 0x50, 0),
            0x50);
  EXPECT_EQ(
      r.arrive(picker_segment, picker_point, picker_flag_below_bp, 0x50, 1),
      0x4F);
  EXPECT_EQ(
      r.arrive(picker_segment, picker_point, picker_flag_below_bp, 0x48, 1),
      0x47);
}

TEST(SeamModernControlsOnCurse, UpAndDownBecomeHomeAndEndAtThePartyBars) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.press(ring_up, camp_segment, camp_bar), ring_home);
  EXPECT_EQ(r.press(ring_down, camp_segment, camp_bar), ring_end);
  EXPECT_EQ(r.press(ring_down, script_segment, script_prompts), ring_end);
  // The adventuring bar walks with them.
  EXPECT_EQ(r.press(ring_up, adventure_segment, adventure_bar), ring_up);
  EXPECT_EQ(r.press(ring_down, adventure_segment, adventure_bar), ring_down);
}

// --- The bar keys ------------------------------------------------------------

TEST(SeamModernControlsOnCurse, LeftAndRightStepABarButNotWhereTheyMove) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.press(ring_left, camp_segment, camp_bar), ring_comma);
  EXPECT_EQ(r.press(ring_right, camp_segment, camp_bar), ring_period);
  // The party turns, the fighter moves, the target bar goes to the manual
  // aim: all three keep the arrows.
  EXPECT_EQ(r.press(ring_left, adventure_segment, adventure_bar), ring_left);
  EXPECT_EQ(r.press(ring_right, combat_segment, combat_bar), ring_right);
  EXPECT_EQ(r.press(ring_right, aim_segment, target_bar), ring_right);
}

TEST(SeamModernControlsOnCurse, EscapeAnswersNoAtTheYesNoPrompt) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.press(ring_escape, menu_segment, yes_no), ring_no);
  // Anywhere else it is the program's.
  EXPECT_EQ(r.press(ring_escape, camp_segment, camp_bar), ring_escape);
}

TEST(SeamModernControlsOnCurse, EnterIsTheProgramsOwn) {
  // Curse's routine hands Enter back as the highlighted command itself.
  const curse_rig r;
  r.arm();
  r.put_byte(data_segment(), 0x72AE, 2);
  EXPECT_EQ(r.press(ring_enter, camp_segment, camp_bar), ring_enter);
  EXPECT_EQ(r.press(ring_enter, menu_segment, yes_no), ring_enter);
}

TEST(SeamModernControlsOnCurse, LeavesTheRingAloneWhileThePushbackIsArmed) {
  const curse_rig r;
  r.arm();
  r.put_byte(data_segment(), data_pushback, 0x4B);
  EXPECT_EQ(r.press(ring_left, camp_segment, camp_bar), ring_left);
  EXPECT_EQ(r.press(ring_up, camp_segment, camp_bar), ring_up);
}

// --- The bars that are not raw -----------------------------------------------

TEST(SeamModernControlsOnCurse, UpAndDownAtTheSaveSlotBarStepTheMember) {
  // One rig a key: a step queues the roster's redraw, a batch of its own.
  const auto at_the_save_bar = [](std::uint16_t key, std::uint8_t raw,
                                  std::uint16_t& head) {
    const curse_rig r;
    r.arm();
    r.lay_party(4, 1);
    // Raw mode off, as the save slot bar calls the routine.
    r.put_byte(stack_segment, frame_base + 0x0C, raw);
    head = r.press(key, slots_segment, save_slot_bar);
    return r.selected();
  };
  std::uint16_t head = 0;
  // The key is answered with the one the program throws away, `-`.
  EXPECT_EQ(at_the_save_bar(ring_down, 0, head), 2U);
  EXPECT_EQ(head, 0x0C2D);
  EXPECT_EQ(at_the_save_bar(ring_up, 0, head), 0U);
  EXPECT_EQ(head, 0x0C2D);
  // Raw mode on is not that caller's frame: nothing is selected.
  EXPECT_EQ(at_the_save_bar(ring_down, 1, head), 1U);
  EXPECT_EQ(head, ring_down);
}

// --- The hero keys -----------------------------------------------------------

TEST(SeamModernControlsOnCurse, ADigitAtTheMainMenuSelectsThatMember) {
  const curse_rig r;
  r.arm();
  r.lay_party(4, 0);
  // The number row's 3: the selection goes on the fourth member, whose
  // predecessor the program's cursor steps back to on the Home it reads.
  EXPECT_EQ(r.press(0x0433, roster_segment, main_menu), ring_home);
  EXPECT_EQ(r.selected(), 3U);
  // The last member: the selection goes on the head, and Home wraps.
  EXPECT_EQ(r.press(0x0534, roster_segment, main_menu), ring_home);
  EXPECT_EQ(r.selected(), 0U);
  // Nobody behind the digit.
  EXPECT_EQ(r.press(0x0635, roster_segment, main_menu), 0x0C2D);
  // Another caller's digits are its own.
  EXPECT_EQ(r.press(0x0433, camp_segment, camp_bar), 0x0433);
}

TEST(SeamModernControlsOnCurse, TheMainMenusRosterDrawsTheNumberWithPaper) {
  const curse_rig r;
  r.arm();
  r.lay_party(4, 0);
  // The drawer's frame on the main menu: column one, the third member's
  // row, the member's far pointer.
  r.put_byte(stack_segment, frame_base - 5, 0x01);
  r.put_byte(stack_segment, frame_base - 6, 6);
  r.put_word(stack_segment, frame_base - 4, 0x0400);
  r.put_word(stack_segment, frame_base - 2, party_segment);
  r.put_byte(static_cast<std::uint16_t>(image_load_segment + glyph_paragraph),
             glyph_offset, 0x90);
  r.stand_on(picker_segment, roster_points[0]);
  r.box->step();

  EXPECT_EQ(r.byte(stack_segment, frame_base - 5), 0x03)
      << "the name moves two columns right";
  cpu::registers& regs = r.regs();
  EXPECT_EQ(regs[cpu::sreg::cs], image_load_segment + glyph_paragraph);
  const std::uint16_t sp = regs[cpu::reg16::sp];
  // The far return is the point itself; then the fold, the character, a
  // count of one, black paper, white, the row and the column.
  const std::array<std::uint16_t, 9> expect{
      roster_points[0], picker_segment, 1, '3', 1, 0, 0x0F, 6, 0x01};
  for (std::size_t i = 0; i < expect.size(); ++i) {
    EXPECT_EQ(r.word(stack_segment, static_cast<std::uint16_t>(sp + (2 * i))),
              expect[i])
        << i;
  }
  EXPECT_TRUE(regs.ip == glyph_offset || regs.ip == glyph_offset + 1U);
}

// --- The selection block -----------------------------------------------------

/// The bar leaf's frame for one character of `Save View`, the highlight on
/// its first word, and the glyph call the leaf makes for it at `call`,
/// with the program's colour on the ink or on the paper.
void lay_bar_call(const curse_rig& r, std::uint8_t index, std::uint8_t ink,
                  std::uint8_t paper) {
  constexpr std::uint16_t caller = 0x0800;
  constexpr std::string_view bar = "Save View";
  r.put_word(stack_segment, frame_base + 6, caller);
  r.put_byte(stack_segment, frame_base + 8, 1);
  r.put_byte(stack_segment, frame_base - 1, index);
  r.put_byte(stack_segment, caller + 0x0E, 0x0F);
  r.put_byte(stack_segment, caller + 0x10, 0x0A);
  r.put_byte(stack_segment, caller - 0x64,
             static_cast<std::uint8_t>(bar.size()));
  for (std::size_t i = 0; i < bar.size(); ++i) {
    r.put_byte(stack_segment, static_cast<std::uint16_t>(caller - 0x53 + 1 + i),
               static_cast<std::uint8_t>(bar[i]));
  }
  // Two groups: `Save` from 1 to 4, `View` from 6 to 9.
  r.put_byte(stack_segment, caller - 0x8F + 2, 1);
  r.put_byte(stack_segment, caller - 0x8F + 3, 4);
  r.put_byte(stack_segment, caller - 0x8F + 4, 6);
  r.put_byte(stack_segment, caller - 0x8F + 5, 9);
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

TEST(SeamModernControlsOnCurse, TheBarsWhitePaperBecomesAYellowWord) {
  const curse_rig r;
  r.arm();
  // The program draws its highlighted group black on white paper: the
  // word's key comes out white and its rest yellow, on black paper, for
  // the row write to make a block of.
  lay_bar_call(r, 1, 0, 0x0F);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F) << "the key";
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);

  lay_bar_call(r, 2, 0, 0x0F);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0E) << "the rest of the word";
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);

  // Outside the highlight the letters and the rest keep their colours.
  lay_bar_call(r, 7, 0x0A, 0);
  r.stand_on(menu_segment, bar_rest_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0A);
  EXPECT_EQ(r.byte(stack_segment, 0x0406), 0x00);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);

  // Ink on the highlight's paper is not the call these facts describe.
  lay_bar_call(r, 2, 0x0F, 0x0F);
  r.stand_on(menu_segment, bar_group_call);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, 0x0408), 0x0F);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1U);
}

/// The row write's frames for a glyph of a string the string routine draws
/// in `colour` for a caller that returns to `segment:offset`, and AH the
/// glyph's row.
void lay_string_glyph(const curse_rig& r, std::uint16_t colour,
                      std::uint16_t segment, std::uint16_t offset) {
  constexpr std::uint16_t blitter = frame_base + 0x40;
  constexpr std::uint16_t drawer = frame_base + 0x80;
  // The EGA path's frame (BP) holds the blitter's BP; the blitter's frame
  // returns into the string routine, whose frame returns to the caller.
  r.put_word(stack_segment, frame_base, blitter);
  r.put_word(stack_segment, blitter, drawer);
  r.put_word(stack_segment, blitter + 2, string_glyph_return);
  r.put_word(stack_segment, blitter + 4, image_load_segment);
  r.put_word(stack_segment, drawer + 2, offset);
  r.put_word(stack_segment, drawer + 4, segment);
  r.put_word(stack_segment, drawer + 0x0C, colour);
  r.stand_on(image_load_segment, blitter_points[0]);
  r.regs().set(cpu::reg8::ah, 0x3C);
}

TEST(SeamModernControlsOnCurse, ARowOfAPickListRowDrawnYellowIsInverted) {
  const curse_rig r;
  r.arm();
  lay_string_glyph(r, 0x0E, menu_segment, list_row_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0xC3);

  // The same call in another colour is not a selection.
  lay_string_glyph(r, 0x0F, menu_segment, list_row_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);

  // Nor is yellow from a call nobody named.
  lay_string_glyph(r, 0x0E, menu_segment, 0x0100);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);
}

TEST(SeamModernControlsOnCurse, ASeamsMarkedStringIsInverted) {
  const curse_rig r;
  r.arm();
  // A string a batch draws, its colour word marked a selection (#483).
  lay_string_glyph(r, 0x800E, 0xF000, 0x0800);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0xC3);
  lay_string_glyph(r, 0x000E, 0xF000, 0x0800);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);
}

// --- The edit keys and text entry --------------------------------------------

TEST(SeamModernControlsOnCurse, TheEditorDropsAnExtendedKeysScanCode) {
  const curse_rig r;
  r.arm();
  r.put_byte(data_segment(), data_pushback, 0x4D);
  r.stand_on(image_load_segment, editor_after_read);
  r.regs().set(cpu::reg8::al, 0);
  r.box->step();
  EXPECT_EQ(r.byte(data_segment(), data_pushback), 0U);

  // A character is the editor's.
  r.put_byte(data_segment(), data_pushback, 0x4D);
  r.stand_on(image_load_segment, editor_after_read);
  r.regs().set(cpu::reg8::al, 'A');
  r.box->step();
  EXPECT_EQ(r.byte(data_segment(), data_pushback), 0x4DU);
}

TEST(SeamModernControlsOnCurse, NotesTheLineEditorFromEntryToReturn) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
  r.stand_on(image_load_segment, editor_entry);
  r.box->step();
  EXPECT_EQ(text_entry_now(*r.box), text_entry::reading);
  r.stand_on(image_load_segment, editor_return);
  r.box->step();
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
}

// --- Pool of Radiance's addresses are not points here ------------------------

TEST(SeamModernControlsOnCurse, PoolOfRadiancesReadCallIsNotAPointHere) {
  const curse_rig r;
  r.arm();
  // Overlay 27 at Pool of Radiance's read offset: nothing is rewritten.
  r.put_word(stack_segment, frame_base + 2, camp_bar);
  r.put_word(stack_segment, frame_base + 4, camp_segment);
  r.put_word(0x40, 0x1A, ring_first);
  r.put_word(0x40, 0x1C, ring_first + 2);
  r.put_word(0x40, ring_first, ring_left);
  r.stand_on(menu_segment, 0x0572);
  r.box->step();
  EXPECT_EQ(r.word(0x40, ring_first), ring_left);
}

}  // namespace
}  // namespace amberfolio::machine
