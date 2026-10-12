// SPDX-License-Identifier: AGPL-3.0-only
//
// The aim-watch piece of `modern-controls` (seam_aim_watch.cpp, #523),
// exercised through its mechanism and not through any program: the test
// lays out a fight and one of the aim bar's two frames where the facts put
// them, stands the processor on one of the piece's points (a NOP there),
// takes one step, and reads the scene.
//
// The offsets are restated rather than read out of the seam, which is the
// seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6).

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/scene.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";

/// The piece's five points come last in the seam's table.
constexpr std::size_t piece_count = 5;

/// The word the overlay manager keeps overlay 13's segment in, and where
/// this test puts the overlay.
constexpr std::uint32_t word_aim = 0x690;
constexpr std::uint16_t aim_segment = 0x7000;

/// The points, from the module's start.
constexpr std::uint16_t target_decided = 0x2BB6;
constexpr std::uint16_t target_returned = 0x2C31;
constexpr std::uint16_t manual_line_ends = 0x2F84;
constexpr std::uint16_t manual_decided = 0x310A;
constexpr std::uint16_t manual_returned = 0x3178;

/// From target to target, from BP: the attack flag, the target's record,
/// the range and the bar string.
constexpr std::uint16_t target_attack = 0x08;
constexpr std::uint16_t target_record = 0x0C;
constexpr std::int16_t target_range = -0x30;
constexpr std::int16_t target_bar = -0x2A;
/// Manual, from BP: the attack flag, the panel's record, the column, the
/// row, the range word and the line's byte.
constexpr std::uint16_t manual_attack = 0x10;
constexpr std::uint16_t manual_record = 0x14;
constexpr std::int16_t manual_column = -0x33;
constexpr std::int16_t manual_row = -0x34;
constexpr std::int16_t manual_range = -0x38;
constexpr std::int16_t manual_line = -0x39;

constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

// The tactical grid's (docs/hosts.md §13).
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint16_t data_slot_records = 0x65B9;
constexpr std::uint16_t data_slot_squares = 0x5F27;
constexpr std::uint16_t data_actor = 0x5D92;
constexpr std::uint16_t data_body_count = 0x6715;
constexpr std::uint16_t data_bodies = 0x66D6;
constexpr std::uint16_t data_combat_block = 0x6716;
constexpr std::uint16_t record_in_fight = 0x10D;
constexpr std::uint16_t record_side = 0x10E;
constexpr std::uint16_t record_computer = 0x10F;
constexpr std::uint16_t record_bytes = 0x120;
constexpr std::uint16_t block_segment = 0x5200;
constexpr std::uint16_t records_segment = 0x5400;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    fight();
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    put_word(image_load_segment, static_cast<std::uint16_t>(word_aim),
             aim_segment);
  }

  [[nodiscard]] static constexpr std::uint16_t dgroup() {
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
  void put_far(std::uint16_t segment, std::uint16_t offset,
               std::uint16_t to_segment, std::uint16_t to_offset) const {
    put_word(segment, offset, to_offset);
    put_word(segment, static_cast<std::uint16_t>(offset + 2), to_segment);
  }
  void frame_byte(int offset, std::uint8_t value) const {
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_base + offset),
             value);
  }
  void frame_word(int offset, std::uint16_t value) const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + offset),
             value);
  }

  [[nodiscard]] static std::uint16_t record_of(unsigned slot) {
    return static_cast<std::uint16_t>(slot * record_bytes);
  }

  /// A fight with the window's top-left at (10, 5) and nobody in it.
  void fight() const {
    put_byte(dgroup(), data_game_mode, 5);
    put_far(dgroup(), data_combat_block, block_segment, 0);
    put_byte(block_segment, 2, 10);
    put_byte(block_segment, 3, 5);
  }

  /// One figure in the next slot, one square, in the fight; its slot.
  [[nodiscard]] unsigned figure(std::string_view name, bool enemy,
                                std::uint8_t x, std::uint8_t y) const {
    const auto count_at = static_cast<std::uint16_t>(data_slot_squares + 3);
    const unsigned slot =
        box->memory().ram()[cpu::physical_address(dgroup(), count_at)] + 1U;
    put_byte(dgroup(), count_at, static_cast<std::uint8_t>(slot));
    const std::uint16_t record = record_of(slot);
    put_byte(records_segment, record, static_cast<std::uint8_t>(name.size()));
    for (std::size_t i = 0; i < name.size(); ++i) {
      put_byte(records_segment, static_cast<std::uint16_t>(record + 1 + i),
               static_cast<std::uint8_t>(name[i]));
    }
    put_byte(records_segment,
             static_cast<std::uint16_t>(record + record_in_fight), 1);
    put_byte(records_segment, static_cast<std::uint16_t>(record + record_side),
             enemy ? 1 : 0);
    put_byte(records_segment,
             static_cast<std::uint16_t>(record + record_computer),
             enemy ? 1 : 0);
    put_far(dgroup(),
            static_cast<std::uint16_t>(data_slot_records + (slot * 4)),
            records_segment, record);
    const auto squares =
        static_cast<std::uint16_t>(data_slot_squares + (slot * 4));
    put_byte(dgroup(), squares, x);
    put_byte(dgroup(), static_cast<std::uint16_t>(squares + 1), y);
    put_byte(dgroup(), static_cast<std::uint16_t>(squares + 2),
             static_cast<std::uint8_t>(slot));
    put_byte(dgroup(), static_cast<std::uint16_t>(squares + 3), 1);
    return slot;
  }

  /// The same, when the test needs no slot.
  void add(std::string_view name, bool enemy, std::uint8_t x,
           std::uint8_t y) const {
    (void)figure(name, enemy, x, y);
  }

  void acting(unsigned slot) const {
    put_far(dgroup(), data_actor, records_segment, record_of(slot));
  }

  /// Stand on `offset` in overlay 13 with BP on the frame, and take one
  /// step; the instruction there is a NOP.
  void arrive(std::uint16_t offset) const {
    put_byte(aim_segment, offset, 0x90);
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = aim_segment;
    r.ip = offset;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::bp] = frame_base;
    r[cpu::reg16::sp] = static_cast<std::uint16_t>(frame_base - 0x300);
    box->step();
  }

  /// From target to target, at `slot`, the range `range`, Target offered
  /// or not, an attack.
  void aim_at(unsigned slot, std::uint8_t range, bool offered) const {
    put_far(stack_segment,
            static_cast<std::uint16_t>(frame_base + target_record),
            records_segment, record_of(slot));
    frame_byte(target_attack, 1);
    frame_byte(target_range, range);
    frame_byte(target_bar, offered ? 7 : 0);
  }

  /// Manual, the cursor at (x, y), the line arriving or not, an attack.
  void cursor_at(std::uint8_t x, std::uint8_t y, bool line,
                 std::uint16_t range) const {
    frame_byte(manual_attack, 1);
    frame_byte(manual_column, x);
    frame_byte(manual_row, y);
    frame_word(manual_range, range);
    frame_byte(manual_line, line ? 1 : 0);
    put_far(stack_segment,
            static_cast<std::uint16_t>(frame_base + manual_record), 0, 0);
  }

  [[nodiscard]] scene read() const {
    scene out{};
    EXPECT_EQ(read_scene(*box, out), scene_trouble::none);
    return out;
  }

  [[nodiscard]] std::string json() const {
    scene now{};
    (void)read_scene(*box, now);
    std::string text(scene_json(now, {}) + 1, '\0');
    text.resize(scene_json(now, text));
    return text;
  }

  std::unique_ptr<machine> box;
};

TEST(SeamAimWatch, IsFivePointsInOverlay13AtTheEndOfTheControlsTable) {
  const rig r;
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  ASSERT_GE(s->points.size(), piece_count);
  const std::span<const seam_point> piece =
      s->points.subspan(s->points.size() - piece_count);
  EXPECT_EQ(piece[0].offset, target_decided);
  EXPECT_EQ(piece[1].offset, target_returned);
  EXPECT_EQ(piece[2].offset, manual_line_ends);
  EXPECT_EQ(piece[3].offset, manual_decided);
  EXPECT_EQ(piece[4].offset, manual_returned);
  for (const seam_point& p : piece) {
    EXPECT_EQ(p.module.file, "GAME.OVR");
    EXPECT_EQ(p.module.load_segment_at, word_aim);
    EXPECT_FALSE(p.at_every_step);
    EXPECT_FALSE(p.inside_calls);
  }
}

TEST(SeamAimWatch, SaysNothingAboutTheBarWhileTheControlsAreOff) {
  const rig r;
  r.add("HULK", false, 12, 8);
  const scene s = r.read();
  EXPECT_FALSE(s.aim_kept);
  EXPECT_EQ(r.json().find("\"aim\""), std::string::npos) << r.json();
}

TEST(SeamAimWatch, IsNullUntilTheBarWaits) {
  const rig r;
  r.arm();
  r.add("HULK", false, 12, 8);
  const scene s = r.read();
  EXPECT_TRUE(s.aim_kept);
  EXPECT_EQ(s.aim.mode, scene_aim_mode::none);
  EXPECT_NE(r.json().find(R"("aim":null)"), std::string::npos) << r.json();
}

TEST(SeamAimWatch, FromTargetToTargetTheCursorIsOnTheTarget) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.aim_at(orc, 2, true);
  r.arrive(target_decided);
  const scene s = r.read();
  EXPECT_EQ(s.aim.mode, scene_aim_mode::target);
  EXPECT_EQ(s.aim.x, 14U);
  EXPECT_EQ(s.aim.y, 6U);
  EXPECT_EQ(s.aim.under, 1);
  EXPECT_EQ(s.aim.range, 2);
  EXPECT_TRUE(s.aim.target);
  EXPECT_NE(r.json().find(R"("aim":{"mode":"target","x":14,"y":6,"under":1,)"
                          R"("range":2,"target":true})"),
            std::string::npos)
      << r.json();
}

TEST(SeamAimWatch, TargetIsWhatTheBarDecidedBeforeItsStringWasRebuilt) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.aim_at(orc, 9, false);
  r.arrive(target_decided);
  // The bar string becomes the whole bar after the meeting point.
  r.frame_byte(target_bar, 28);
  EXPECT_FALSE(r.read().aim.target);
}

TEST(SeamAimWatch, ASpellsAimPrintsNoRange) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.aim_at(orc, 2, true);
  r.frame_byte(target_attack, 0);
  r.arrive(target_decided);
  EXPECT_EQ(r.read().aim.range, -1);
}

TEST(SeamAimWatch, ATargetOutsideTheWindowIsAimedAtButNotListed) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned far_orc = r.figure("ORC", true, 30, 6);
  r.aim_at(far_orc, 18, false);
  r.arrive(target_decided);
  const scene s = r.read();
  EXPECT_EQ(s.aim.mode, scene_aim_mode::target);
  EXPECT_EQ(s.aim.x, 30U);
  EXPECT_EQ(s.aim.under, -1);
  EXPECT_NE(r.json().find(R"("under":null)"), std::string::npos) << r.json();
}

TEST(SeamAimWatch, ATargetThatIsNoFigureIsNoAim) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  r.aim_at(9, 2, true);
  r.arrive(target_decided);
  EXPECT_EQ(r.read().aim.mode, scene_aim_mode::none);
}

TEST(SeamAimWatch, TheWaitsReturnEndsTheAim) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.aim_at(orc, 2, true);
  r.arrive(target_decided);
  ASSERT_EQ(r.read().aim.mode, scene_aim_mode::target);
  r.arrive(target_returned);
  EXPECT_EQ(r.read().aim.mode, scene_aim_mode::none);
}

TEST(SeamAimWatch, ManualSaysTheCursorAndWhoTheSheetIsFor) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.cursor_at(14, 6, true, 2);
  r.arrive(manual_line_ends);
  r.put_far(stack_segment,
            static_cast<std::uint16_t>(frame_base + manual_record),
            records_segment, rig::record_of(orc));
  r.arrive(manual_decided);
  const scene s = r.read();
  EXPECT_EQ(s.aim.mode, scene_aim_mode::manual);
  EXPECT_EQ(s.aim.x, 14U);
  EXPECT_EQ(s.aim.y, 6U);
  EXPECT_EQ(s.aim.under, 1);
  EXPECT_EQ(s.aim.range, 2);
  EXPECT_TRUE(s.aim.target);
  r.arrive(manual_returned);
  EXPECT_EQ(r.read().aim.mode, scene_aim_mode::none);
}

TEST(SeamAimWatch, ManualOnAnEmptySquareInSightHasARangeAndNoOne) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  r.cursor_at(13, 10, true, 2);
  r.arrive(manual_line_ends);
  // Nothing to aim at: the line's byte is cleared before the bar is built.
  r.frame_byte(manual_line, 0);
  r.arrive(manual_decided);
  const scene s = r.read();
  EXPECT_EQ(s.aim.mode, scene_aim_mode::manual);
  EXPECT_EQ(s.aim.under, -1);
  EXPECT_EQ(s.aim.range, 2);
  EXPECT_FALSE(s.aim.target);
}

TEST(SeamAimWatch, ManualOutOfSightPrintsNoRange) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  r.cursor_at(15, 10, false, 3);
  r.arrive(manual_line_ends);
  r.arrive(manual_decided);
  const scene s = r.read();
  EXPECT_EQ(s.aim.mode, scene_aim_mode::manual);
  EXPECT_EQ(s.aim.range, -1);
  EXPECT_NE(r.json().find(R"("range":null,"target":false})"), std::string::npos)
      << r.json();
}

TEST(SeamAimWatch, ManualOnAFallenPartyMemberIsThatMember) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned thief = r.figure("THIEF", false, 13, 8);
  // Fallen: off the grid, a body where it fell.
  r.put_byte(rig::dgroup(),
             static_cast<std::uint16_t>(data_slot_squares + (thief * 4) + 3),
             0);
  r.put_byte(
      records_segment,
      static_cast<std::uint16_t>(rig::record_of(thief) + record_in_fight), 0);
  r.put_byte(rig::dgroup(), data_body_count, 1);
  r.put_far(rig::dgroup(), data_bodies, records_segment, rig::record_of(thief));
  r.put_byte(rig::dgroup(), static_cast<std::uint16_t>(data_bodies + 4), 13);
  r.put_byte(rig::dgroup(), static_cast<std::uint16_t>(data_bodies + 5), 9);
  r.cursor_at(13, 9, true, 1);
  r.arrive(manual_line_ends);
  r.put_far(stack_segment,
            static_cast<std::uint16_t>(frame_base + manual_record),
            records_segment, rig::record_of(thief));
  r.arrive(manual_decided);
  EXPECT_EQ(r.read().aim.under, 1);
}

TEST(SeamAimWatch, ACursorOffTheGridIsNoAim) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  r.cursor_at(50, 6, true, 2);
  r.arrive(manual_line_ends);
  r.arrive(manual_decided);
  EXPECT_EQ(r.read().aim.mode, scene_aim_mode::none);
}

TEST(SeamAimWatch, ForgetsTheAimWhenTheControlsGoOffAndOnAgain) {
  const rig r;
  r.arm();
  r.acting(r.figure("HULK", false, 12, 8));
  const unsigned orc = r.figure("ORC", true, 14, 6);
  r.aim_at(orc, 2, true);
  r.arrive(target_decided);
  ASSERT_EQ(r.read().aim.mode, scene_aim_mode::target);
  ASSERT_EQ(r.box->seams().disable(seam_id), seam_reason::none);
  EXPECT_FALSE(r.read().aim_kept);
  r.arm();
  EXPECT_EQ(r.read().aim.mode, scene_aim_mode::none)
      << "the return it missed is not an aim it may still claim";
}

TEST(SeamAimWatch, WritesNothingOfTheMachines) {
  const rig on;
  on.arm();
  const rig off;
  off.put_word(image_load_segment, static_cast<std::uint16_t>(word_aim),
               aim_segment);
  for (const rig* r : {&on, &off}) {
    r->acting(r->figure("HULK", false, 12, 8));
    r->aim_at(r->figure("ORC", true, 14, 6), 2, true);
  }
  for (const std::uint16_t at :
       {target_decided, target_returned, manual_line_ends, manual_decided,
        manual_returned}) {
    on.arrive(at);
    off.arrive(at);
    const std::span<const std::uint8_t> a = on.box->memory().ram();
    const std::span<const std::uint8_t> b = off.box->memory().ram();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin())) << at;
    EXPECT_EQ(on.box->processor().regs().ip, off.box->processor().regs().ip);
  }
}

}  // namespace
}  // namespace amberfolio::machine
