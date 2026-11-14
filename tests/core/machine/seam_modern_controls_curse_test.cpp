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
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
/// The character editor's name field, highlighted: its string call (overlay
/// 18), the cursor letter's after it.
constexpr std::uint16_t name_string_call = 0x2775;
constexpr std::uint16_t name_cursor_call = 0x27E5;
/// The instruction after the name field's second key read; the editor's
/// call into the menu-bar routine, by the instruction after it.
constexpr std::uint16_t name_field_read = 0x295A;
constexpr std::uint16_t modify_bar = 0x29AA;
constexpr std::array<std::uint16_t, 4> blitter_points{0x5A93, 0x4F98, 0x4E4B,
                                                      0x3542};
/// The string routine's near call into the blitter: the instruction after.
constexpr std::uint16_t string_glyph_return = 0x509B;

/// The main menu's loop (overlay 18): its call into the menu-bar routine,
/// the menu cursor's second point. The callers the hero keys, the menu
/// cursor and the bars that are not raw are named by.
constexpr std::uint16_t loop_call = 0x038E;
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
/// The party bar's call itself: the move mode's point before it.
constexpr std::uint16_t party_bar_call = 0x0927;
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
  // list arrows 4, bar keys 1, menu cursor 2, hero keys 5, edit keys 1, the
  // selection block 12, the move mode 3, text entry 2, aim watch 5.
  ASSERT_EQ(mine.size(), 35U);
  EXPECT_NE(mine.data(), definition().points.data());

  EXPECT_EQ(mine[0].offset, list_point);
  EXPECT_EQ(mine[0].module.load_segment_at, word_menu);
  EXPECT_EQ(mine[1].offset, picker_point);
  EXPECT_EQ(mine[1].module.load_segment_at, word_picker);
  // Modify's name field, which reads its keys itself: Curse's only.
  EXPECT_EQ(mine[3].offset, name_field_read);
  EXPECT_EQ(mine[3].module.load_segment_at, word_roster);
  // The read call: the list arrows' first, then the bar keys', the menu
  // cursor's and, after the loop's call, the hero keys'.
  for (const std::size_t at : {2U, 4U, 5U, 7U}) {
    EXPECT_EQ(mine[at].offset, read_point) << at;
    EXPECT_EQ(mine[at].module.load_segment_at, word_menu) << at;
  }
  EXPECT_EQ(mine[6].offset, loop_call);
  EXPECT_EQ(mine[6].module.load_segment_at, word_roster);
  EXPECT_FALSE(mine[6].inside_calls);
  // The roster drawer's four, inside a batch too.
  for (std::size_t i = 0; i < roster_points.size(); ++i) {
    EXPECT_EQ(mine[8 + i].offset, roster_points[i]) << i;
    EXPECT_EQ(mine[8 + i].module.load_segment_at, word_picker) << i;
    EXPECT_TRUE(mine[8 + i].inside_calls) << i;
  }
  EXPECT_EQ(mine[12].offset, editor_after_read);
  // The selection block's twelve.
  const std::array<std::uint16_t, 7> colours{
      bar_group_call,    list_row_call,   roster_selected_call, score_chosen,
      hit_points_chosen, bar_letter_call, bar_rest_call};
  const std::array<std::uint16_t, 7> words{word_menu,  word_menu,   word_picker,
                                           word_sheet, word_picker, word_menu,
                                           word_menu};
  for (std::size_t i = 0; i < colours.size(); ++i) {
    EXPECT_EQ(mine[13 + i].offset, colours[i]) << i;
    EXPECT_EQ(mine[13 + i].module.load_segment_at, words[i]) << i;
  }
  EXPECT_TRUE(mine[15].inside_calls) << "the roster is drawn back in a batch";
  for (std::size_t i = 0; i < blitter_points.size(); ++i) {
    EXPECT_EQ(mine[20 + i].offset, blitter_points[i]) << i;
    EXPECT_TRUE(mine[20 + i].module.is_resident_image()) << i;
    EXPECT_TRUE(mine[20 + i].inside_calls) << i;
  }
  // Modify's name field, Curse's only.
  EXPECT_EQ(mine[24].offset, name_string_call);
  EXPECT_EQ(mine[24].module.load_segment_at, word_roster);
  EXPECT_FALSE(mine[24].inside_calls);
  // The move mode's: either side of the party bar's call, and the read
  // call, the last of the five pieces there.
  EXPECT_EQ(mine[25].offset, party_bar_call);
  EXPECT_EQ(mine[25].module.load_segment_at, word_adventure);
  EXPECT_EQ(mine[26].offset, adventure_bar);
  EXPECT_EQ(mine[26].module.load_segment_at, word_adventure);
  EXPECT_EQ(mine[27].offset, read_point);
  EXPECT_EQ(mine[27].module.load_segment_at, word_menu);
  for (const std::size_t at : {12U, 28U, 29U}) {
    EXPECT_TRUE(mine[at].module.is_resident_image()) << at;
  }
  EXPECT_EQ(mine[28].offset, editor_entry);
  EXPECT_EQ(mine[29].offset, editor_return);
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[30 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[30 + i].module.load_segment_at, word_aim) << i;
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
  // At the adventuring bar the move mode, in menu mode, makes them the
  // same two.
  EXPECT_EQ(r.press(ring_up, adventure_segment, adventure_bar), ring_home);
  EXPECT_EQ(r.press(ring_down, adventure_segment, adventure_bar), ring_end);
}

TEST(SeamModernControlsOnCurse, UpAndDownStepModifysFields) {
  const curse_rig r;
  r.arm();
  // At a score, the editor's bar: Home and End step the field there.
  EXPECT_EQ(r.press(ring_up, roster_segment, modify_bar), ring_home);
  EXPECT_EQ(r.press(ring_down, roster_segment, modify_bar), ring_end);
  // In the name field the editor reads the scan code itself.
  r.stand_on(roster_segment, name_field_read);
  r.regs().set(cpu::reg8::al, 0x48);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0x47);
  r.stand_on(roster_segment, name_field_read);
  r.regs().set(cpu::reg8::al, 0x50);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0x4F);
  // Its own cursor keys are its own.
  r.stand_on(roster_segment, name_field_read);
  r.regs().set(cpu::reg8::al, 0x4D);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0x4D);
}

// --- The bar keys ------------------------------------------------------------

TEST(SeamModernControlsOnCurse, LeftAndRightStepABarButNotWhereTheyMove) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.press(ring_left, camp_segment, camp_bar), ring_comma);
  EXPECT_EQ(r.press(ring_right, camp_segment, camp_bar), ring_period);
  // The combat command bar is stepped, as Pool of Radiance's is: there an
  // arrow would start a move.
  EXPECT_EQ(r.press(ring_left, combat_segment, combat_bar), ring_comma);
  EXPECT_EQ(r.press(ring_right, combat_segment, combat_bar), ring_period);
  // The target bar goes to the manual aim and keeps the arrows. The party's
  // bar keeps them from this piece, and the move mode, in menu mode, makes
  // them its own steps.
  EXPECT_EQ(r.press(ring_left, adventure_segment, adventure_bar), ring_comma);
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

// --- The menu cursor ---------------------------------------------------------

/// The main menu's twelve records (DGROUP `0x0553`, forty-two bytes each,
/// the enable byte at +0x29), made-up words a letter to each; Drop is the
/// second and Load the ninth.
constexpr std::uint16_t records = 0x0553;
constexpr std::uint16_t record_stride = 0x2A;
constexpr std::uint16_t record_enable = 0x29;
constexpr std::uint8_t record_drop = 1;
constexpr std::uint8_t record_load = 8;
constexpr std::array<std::string_view, 12> menu_words{
    "Cargo", "Dune", "Mist", "Tide", "Heron", "Vale",
    "Aster", "Reed", "Lark", "Sage", "Birch", "Elm"};
constexpr std::string_view menu_bar = "C D M T H V A R L S B E J";
constexpr std::uint16_t data_menu_bar = 0x052A;

/// The string drawer, image `0x5029`: column, row, colour, paper, then the
/// string's far pointer. The stand-in logs every call: the six words and the
/// forty-one bytes at the pointer.
constexpr std::uint16_t draw_string = 0x5029;
constexpr std::uint16_t log_pointer = 0x7400;
constexpr std::uint16_t log_base = 0x7410;
constexpr std::uint16_t log_stride = 12 + 41;

struct drawn {
  std::uint16_t column;
  std::uint16_t row;
  std::uint16_t colour;
  std::uint16_t paper;
  std::string text;

  bool operator==(const drawn&) const = default;
};

[[nodiscard]] std::uint16_t enable_of(std::uint8_t record) {
  return static_cast<std::uint16_t>(records + (record * record_stride) +
                                    record_enable);
}

/// The records, these shown and every other off, as the redraw leaves them;
/// the routine's copy of the bar; the drawer's stand-in.
void lay_menu(const curse_rig& r, std::initializer_list<std::uint8_t> shown) {
  for (std::size_t i = 0; i < menu_words.size(); ++i) {
    const auto base = static_cast<std::uint16_t>(records + (i * record_stride));
    r.put_byte(data_segment(), base,
               static_cast<std::uint8_t>(menu_words[i].size()));
    for (std::size_t c = 0; c < menu_words[i].size(); ++c) {
      r.put_byte(data_segment(), static_cast<std::uint16_t>(base + 1U + c),
                 static_cast<std::uint8_t>(menu_words[i][c]));
    }
    r.put_byte(data_segment(), enable_of(static_cast<std::uint8_t>(i)), 0);
  }
  for (const std::uint8_t i : shown) {
    r.put_byte(data_segment(), enable_of(i), 1);
  }
  r.put_byte(stack_segment, frame_base - 0x53,
             static_cast<std::uint8_t>(menu_bar.size()));
  for (std::size_t c = 0; c < menu_bar.size(); ++c) {
    r.put_byte(stack_segment,
               static_cast<std::uint16_t>(frame_base - 0x53 + 1U + c),
               static_cast<std::uint8_t>(menu_bar[c]));
  }
  //     push bp / mov bp, sp / push ds / push ds / pop es
  //     mov di, [log_pointer]
  //     six times: mov ax, [bp+k] / mov [di], ax / add di, 2
  //     lds si, [bp+6] / mov cx, 41 / cld / rep movsb
  //     pop ds / mov [log_pointer], di / pop bp / retf 0Ch
  std::uint16_t put = draw_string;
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) {
      r.put_byte(image_load_segment, put++, b);
    }
  };
  constexpr auto log_low = static_cast<std::uint8_t>(log_pointer & 0xFFU);
  constexpr auto log_high = static_cast<std::uint8_t>(log_pointer >> 8U);
  emit({0x55, 0x89, 0xE5, 0x1E, 0x1E, 0x07, 0x8B, 0x3E, log_low, log_high});
  for (const std::uint8_t k :
       std::array<std::uint8_t, 6>{0x10, 0x0E, 0x0C, 0x0A, 0x08, 0x06}) {
    emit({0x8B, 0x46, k, 0x89, 0x05, 0x83, 0xC7, 0x02});
  }
  emit({0xC5, 0x76, 0x06, 0xB9, 0x29, 0x00, 0xFC, 0xF3, 0xA4, 0x1F, 0x89, 0x3E,
        log_low, log_high, 0x5D, 0xCA, 0x0C, 0x00});
  r.put_word(data_segment(), log_pointer, log_base);
}

[[nodiscard]] std::vector<drawn> drawn_calls(const curse_rig& r) {
  std::vector<drawn> out;
  const std::uint16_t end_of_log = r.word(data_segment(), log_pointer);
  for (std::uint16_t at = log_base; at < end_of_log; at += log_stride) {
    const auto at_word = [&](unsigned k) {
      return r.word(data_segment(), static_cast<std::uint16_t>(at + k));
    };
    drawn d{.column = at_word(0),
            .row = at_word(2),
            .colour = at_word(4),
            .paper = at_word(6),
            .text = {}};
    const std::uint8_t length =
        r.byte(data_segment(), static_cast<std::uint16_t>(at + 12U));
    for (std::uint8_t c = 0; c < length; ++c) {
      d.text.push_back(static_cast<char>(
          r.byte(data_segment(), static_cast<std::uint16_t>(at + 13U + c))));
    }
    out.push_back(d);
  }
  return out;
}

/// Steps until the batch an arrival queued has run out and the machine is
/// back on the point's HLT.
void run_the_calls(const curse_rig& r) {
  for (unsigned nth = 0; nth < 4096 && !r.box->processor().halted(); ++nth) {
    r.box->step();
  }
}

/// The loop has drawn the menu and calls the routine: the call's words, from
/// the top of the stack, with a register's leftover in each byte argument's
/// high half; one step, and the batch run out.
void menu_drawn(const curse_rig& r) {
  const std::array<std::uint16_t, 9> call{
      0x0300, stack_segment, 0xA000,        0xA001,        0xA000,
      0xA000, 0xA00D,        data_menu_bar, data_segment()};
  for (std::size_t i = 0; i < call.size(); ++i) {
    r.put_word(stack_segment, static_cast<std::uint16_t>(0x0400 + (2 * i)),
               call[i]);
  }
  r.stand_on(roster_segment, loop_call);
  r.box->step();
  run_the_calls(r);
}

/// A key at the main menu's read, the batch run out: the head of the ring.
[[nodiscard]] std::uint16_t press_at_the_menu(const curse_rig& r,
                                              std::uint16_t key) {
  static_cast<void>(r.press(key, roster_segment, main_menu));
  run_the_calls(r);
  return r.word(0x40, r.word(0x40, 0x1A));
}

/// A selection's colour word on Curse: white, marked a selection (#483).
constexpr std::uint16_t lit = 0x800F;

TEST(SeamModernControlsOnCurse, TheMainMenusCursorIsAWhiteBlockFromTheStart) {
  const curse_rig r;
  r.arm();
  // No party: Create, Add, Load and Exit, and the cursor lives in Load's.
  lay_menu(r, {0, 6, 8, 11});
  menu_drawn(r);
  EXPECT_EQ(drawn_calls(r), (std::vector<drawn>{{2, 0x0C, lit, 0, "C"},
                                                {3, 0x0C, lit, 0, "argo"}}))
      << "the key and the rest of the word white, marked, on black";
  EXPECT_EQ(r.byte(data_segment(), enable_of(record_load)), 2);
  EXPECT_EQ(r.byte(data_segment(), enable_of(record_drop)), 0);

  // The loop comes back to the call: nothing is drawn twice.
  menu_drawn(r);
  EXPECT_EQ(drawn_calls(r).size(), 2U);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

TEST(SeamModernControlsOnCurse, UpAndDownMoveTheMainMenusCursorOverItsRows) {
  const curse_rig r;
  r.arm();
  // A party: nine rows, and the cursor lives in Drop's byte.
  lay_menu(r, {0, 1, 2, 5, 6, 7, 9, 10, 11});
  menu_drawn(r);

  EXPECT_EQ(press_at_the_menu(r, ring_down), 0x0C2D) << "answered";
  EXPECT_EQ(r.byte(data_segment(), enable_of(record_drop)), 3);
  const std::vector<drawn> seen = drawn_calls(r);
  ASSERT_EQ(seen.size(), 6U);
  EXPECT_EQ(seen[2], (drawn{2, 0x0D, lit, 0, "D"}));
  EXPECT_EQ(seen[3], (drawn{3, 0x0D, lit, 0, "une"}));
  EXPECT_EQ(seen[4], (drawn{2, 0x0C, 0x0F, 0, "C"})) << "put back";
  EXPECT_EQ(seen[5], (drawn{3, 0x0C, 0x0A, 0, "argo"}));

  // Up twice: the first row, then the last, wrapping.
  static_cast<void>(press_at_the_menu(r, ring_up));
  static_cast<void>(press_at_the_menu(r, ring_up));
  EXPECT_EQ(r.byte(data_segment(), enable_of(record_drop)), 2 + 8);
  ASSERT_EQ(drawn_calls(r).size(), 14U);
  EXPECT_EQ(drawn_calls(r)[10], (drawn{2, 0x0C + 8, lit, 0, "E"}));

  // Return takes it, as its letter under Return's scan code.
  EXPECT_EQ(press_at_the_menu(r, ring_enter), 0x1C45);
}

TEST(SeamModernControlsOnCurse, ReturnAtTheMainMenuIsTheProgramsWithNoCursor) {
  const curse_rig r;
  r.arm();
  lay_menu(r, {0, 6, 8, 11});
  // The menu is up, but no cursor is drawn yet (its byte is one).
  EXPECT_EQ(press_at_the_menu(r, ring_enter), ring_enter);
  EXPECT_TRUE(drawn_calls(r).empty());
  // Another caller's Up is not the menu's.
  EXPECT_EQ(r.press(ring_up, menu_segment, yes_no), ring_up);
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

TEST(SeamModernControlsOnCurse, TheBarsWhitePaperBecomesAWhiteWord) {
  const curse_rig r;
  r.arm();
  // The program draws its highlighted group black on white paper: the
  // word comes out white, key and rest, on black paper, for the row write
  // to make the same white block of. Curse's selection is its own white.
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

TEST(SeamModernControlsOnCurse, ARowOfAPickListRowDrawnWhiteIsInverted) {
  const curse_rig r;
  r.arm();
  lay_string_glyph(r, 0x0F, menu_segment, list_row_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0xC3);

  // The same call in another colour is not a selection: the program's own
  // black ink, and Pool of Radiance's yellow.
  for (const std::uint16_t colour : std::array<std::uint16_t, 2>{0x00, 0x0E}) {
    lay_string_glyph(r, colour, menu_segment, list_row_call + 5U);
    r.box->step();
    EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C) << colour;
  }

  // Nor is white from a call nobody named.
  lay_string_glyph(r, 0x0F, menu_segment, 0x0100);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);
}

TEST(SeamModernControlsOnCurse, TheListsRowIsHandedWhiteInk) {
  const curse_rig r;
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
}

TEST(SeamModernControlsOnCurse, TheSelectedMemberIsAWhiteBlock) {
  const curse_rig r;
  r.arm();
  // The roster's selected-member arm draws white, and white is the
  // selection here: inverted, where Pool of Radiance's yellow would not be.
  lay_string_glyph(r, 0x0F, picker_segment, roster_selected_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0xC3);
  lay_string_glyph(r, 0x0E, picker_segment, roster_selected_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);
}

TEST(SeamModernControlsOnCurse, ModifysHighlightedScoreIsWhite) {
  const curse_rig r;
  r.arm();
  // A highlighted draw (a nonzero first argument) in light magenta.
  r.put_byte(stack_segment, frame_base + 6, 1);
  r.put_byte(stack_segment, frame_base - 0x2B, 0x0D);
  r.stand_on(sheet_segment, score_chosen);
  r.box->step();
  EXPECT_EQ(r.byte(stack_segment, frame_base - 0x2B), 0x0F);
}

/// The name field's string call at the top of the stack: the name's far
/// pointer, the paper, the colour, the row and the column.
void lay_name_call(const curse_rig& r, std::uint16_t colour,
                   std::uint16_t row) {
  const std::array<std::uint16_t, 6> words{0x0010, party_segment, 0,
                                           colour, row,           1};
  for (std::size_t i = 0; i < words.size(); ++i) {
    r.put_word(stack_segment, static_cast<std::uint16_t>(0x0400 + (2 * i)),
               words[i]);
  }
  r.stand_on(roster_segment, name_string_call);
}

TEST(SeamModernControlsOnCurse, ModifysHighlightedNameIsWhite) {
  const curse_rig r;
  r.arm();
  // Light magenta at the sheet's first cell; the high half of the pushed
  // register is the program's leftover and stays.
  lay_name_call(r, 0x050D, 1);
  r.box->step();
  EXPECT_EQ(r.word(stack_segment, 0x0406), 0x050F);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);

  // Another colour or another cell is not the call these facts describe.
  lay_name_call(r, 0x000A, 1);
  r.box->step();
  EXPECT_EQ(r.word(stack_segment, 0x0406), 0x000A);
  lay_name_call(r, 0x000D, 2);
  r.box->step();
  EXPECT_EQ(r.word(stack_segment, 0x0406), 0x000D);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2U);
}

TEST(SeamModernControlsOnCurse, TheNameIsABlockAndItsCursorLetterIsNot) {
  const curse_rig r;
  r.arm();
  lay_string_glyph(r, 0x0F, roster_segment, name_string_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0xC3);
  // Still magenta is not a selection: the point declined it.
  lay_string_glyph(r, 0x0D, roster_segment, name_string_call + 5U);
  r.box->step();
  EXPECT_EQ(r.regs().get(cpu::reg8::ah), 0x3C);
  // The cursor letter, white on black after the name, is the gap in it.
  lay_string_glyph(r, 0x0F, roster_segment, name_cursor_call + 5U);
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
