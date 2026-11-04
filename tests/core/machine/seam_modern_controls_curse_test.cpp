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
/// 18 (the main menu and the character editor), 25 (the picker) and 27
/// (the menu-bar routine).
constexpr std::uint16_t word_temple = 0x430;
constexpr std::uint16_t word_post_combat = 0x460;
constexpr std::uint16_t word_shop = 0x490;
constexpr std::uint16_t word_script = 0x4D0;
constexpr std::uint16_t word_combat = 0x580;
constexpr std::uint16_t word_aim = 0x8E0;
constexpr std::uint16_t word_adventure = 0x9A0;
constexpr std::uint16_t word_camp = 0x9E0;
constexpr std::uint16_t word_roster = 0xA80;
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

/// The out-parameters, below BP; the pushback slot; the data segment.
constexpr std::uint16_t list_flag_below_bp = 0x57;
constexpr std::uint16_t picker_flag_below_bp = 0x2B;
constexpr std::uint16_t data_pushback = 0x8CBF;
constexpr std::uint16_t dgroup_paragraphs = 0xABE;

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
  // list arrows 3, bar keys 1, edit keys 1, text entry 2, aim watch 5.
  ASSERT_EQ(mine.size(), 12U);
  EXPECT_NE(mine.data(), definition().points.data());

  EXPECT_EQ(mine[0].offset, list_point);
  EXPECT_EQ(mine[0].module.load_segment_at, word_menu);
  EXPECT_EQ(mine[1].offset, picker_point);
  EXPECT_EQ(mine[1].module.load_segment_at, word_picker);
  // The read call: the list arrows' first, the bar keys' after.
  for (const std::size_t at : {2U, 3U}) {
    EXPECT_EQ(mine[at].offset, read_point) << at;
    EXPECT_EQ(mine[at].module.load_segment_at, word_menu) << at;
  }
  for (const std::size_t at : {4U, 5U, 6U}) {
    EXPECT_TRUE(mine[at].module.is_resident_image()) << at;
  }
  EXPECT_EQ(mine[4].offset, editor_after_read);
  EXPECT_EQ(mine[5].offset, editor_entry);
  EXPECT_EQ(mine[6].offset, editor_return);
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[7 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[7 + i].module.load_segment_at, word_aim) << i;
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
