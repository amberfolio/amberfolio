// SPDX-License-Identifier: AGPL-3.0-only
//
// The move mode of `modern-controls` on Curse of the Azure Bonds (#555,
// seam_move_mode.cpp), exercised through its mechanism and not through the
// program: the test stands the processor on one of Curse's points (a HLT
// there, so the step is the arrival and nothing else) with the bar, the
// frame and the ring laid out where Curse's facts put them, and reads what
// the handler left.
//
// The offsets below are restated rather than read out of the seam, which is
// the seam suites' rule. The bar is made-up words, and **every byte here is
// this file's own** (PLAN.md §6).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";
constexpr std::string_view curse_fingerprint =
    "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd";

// --- Curse's facts, restated -------------------------------------------------

/// The load-segment words of overlay 15 (the adventuring loop), 16 (camp)
/// and 27 (the menu-bar routine), and where the test puts them.
constexpr std::uint16_t word_adventure = 0x9A0;
constexpr std::uint16_t word_camp = 0x9E0;
constexpr std::uint16_t word_menu = 0x1050;
constexpr std::uint16_t adventure_segment = 0x6400;
constexpr std::uint16_t camp_segment = 0x6800;
constexpr std::uint16_t menu_segment = 0x6000;

/// The points: either side of the party bar's call in overlay 15, and the
/// key read in overlay 27.
constexpr std::uint16_t bar_before = 0x0927;
constexpr std::uint16_t bar_after = 0x092C;
constexpr std::uint16_t key_point = 0x05F1;
/// Pool of Radiance's city call, which is not a point here.
constexpr std::uint16_t baseline_city_before = 0x09D0;

/// Callers by their returns: the camp bar, and the two door bars in the
/// adventuring loop's module.
constexpr std::uint16_t camp_return = 0x24FD;
constexpr std::uint16_t door_bar_locked = 0x0CD7;
constexpr std::uint16_t door_bar_stuck = 0x0E16;

/// The data segment and the facts in it.
constexpr std::uint16_t dgroup_paragraphs = 0xABE;
constexpr std::uint16_t party_bar = 0x03D8;
constexpr std::uint16_t data_highlight = 0x72AE;
constexpr std::uint16_t data_overhead = 0x7228;
constexpr std::uint16_t data_pushback = 0x8CBF;

/// The loop's frame: the out-parameter and the copy below its BP. The
/// routine's: its caller's return above BP, its copy of the bar below.
constexpr std::uint16_t loop_out_flag = 0x04;
constexpr std::uint16_t loop_copy = 0x32;
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;
constexpr std::uint16_t routine_bar = 0x53;
/// At the call: the bar's far pointer above SP.
constexpr std::uint16_t stack_bar_offset = 14;
constexpr std::uint16_t stack_bar_segment = 16;

constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t stack_top = 0x0400;
constexpr std::uint16_t ring_first = 0x1E;

/// Keys, as the ring holds them.
constexpr std::uint16_t left = 0x4B00;
constexpr std::uint16_t up = 0x4800;
constexpr std::uint16_t down = 0x5000;
constexpr std::uint16_t home = 0x4700;
constexpr std::uint16_t end = 0x4F00;
constexpr std::uint16_t comma = 0x332C;
constexpr std::uint16_t escape = 0x011B;
constexpr std::uint16_t ignored = 0x0C2D;
constexpr std::uint16_t key_e = 0x1245;
constexpr std::uint16_t row_8 = 0x0938;
constexpr std::uint16_t letter_h = 0x2368;
constexpr std::uint16_t letter_b = 0x3062;

/// A made-up bar: a first group of a capital `A` and three more, as the
/// facts say the program's is, and two more groups.
constexpr std::string_view a_bar = "Alfa Bravo Cobra";

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest curse;
    EXPECT_TRUE(parse_digest(curse_fingerprint, curse));
    box->seams().loaded(curse, image_load_segment);
  }

  [[nodiscard]] static constexpr std::uint16_t dgroup() {
    return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    put_word(image_load_segment, word_adventure, adventure_segment);
    put_word(image_load_segment, word_camp, camp_segment);
    put_word(image_load_segment, word_menu, menu_segment);
    put_string(dgroup(), party_bar, a_bar);
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

  void put_string(std::uint16_t segment, std::uint16_t at,
                  std::string_view text) const {
    put_byte(segment, at, static_cast<std::uint8_t>(text.size()));
    for (std::size_t i = 0; i < text.size(); ++i) {
      put_byte(segment, static_cast<std::uint16_t>(at + 1U + i),
               static_cast<std::uint8_t>(text[i]));
    }
  }

  [[nodiscard]] std::string string_at(std::uint16_t segment,
                                      std::uint16_t at) const {
    std::string out;
    const std::uint8_t length = byte(segment, at);
    for (unsigned i = 1; i <= length; ++i) {
      out.push_back(
          static_cast<char>(byte(segment, static_cast<std::uint16_t>(at + i))));
    }
    return out;
  }

  [[nodiscard]] std::uint8_t lit() const {
    return byte(dgroup(), data_highlight);
  }

  /// Stand on `point` in overlay 15 with AL and the out-parameter as given,
  /// and take one step.
  void arrive(std::uint16_t point, std::uint8_t al = 0,
              std::uint8_t out_flag = 0) const {
    put_byte(adventure_segment, point, 0xF4);
    put_byte(stack_segment,
             static_cast<std::uint16_t>(frame_base - loop_out_flag), out_flag);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = adventure_segment;
    r.ip = point;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = stack_top;
    r[cpu::reg16::bp] = frame_base;
    r.set(cpu::reg8::al, al);
    box->step();
  }

  /// Before the call, as a fresh call handed the program's bar.
  void before(std::uint16_t point = bar_before) const {
    put_word(stack_segment,
             static_cast<std::uint16_t>(stack_top + stack_bar_offset),
             party_bar);
    put_word(stack_segment,
             static_cast<std::uint16_t>(stack_top + stack_bar_segment),
             dgroup());
    arrive(point);
  }

  /// The bar the call will be handed.
  [[nodiscard]] std::string handed() const {
    return string_at(
        word(stack_segment,
             static_cast<std::uint16_t>(stack_top + stack_bar_segment)),
        word(stack_segment,
             static_cast<std::uint16_t>(stack_top + stack_bar_offset)));
  }

  [[nodiscard]] bool handed_the_copy() const {
    return word(stack_segment,
                static_cast<std::uint16_t>(stack_top + stack_bar_offset)) ==
               static_cast<std::uint16_t>(frame_base - loop_copy) &&
           word(stack_segment,
                static_cast<std::uint16_t>(stack_top + stack_bar_segment)) ==
               stack_segment;
  }

  [[nodiscard]] std::uint8_t al() const {
    return box->processor().regs().get(cpu::reg8::al);
  }

  /// One press of `key` at the key read, from the caller at
  /// `segment:offset`, with `bar` as the routine's copy of the bar.
  [[nodiscard]] std::uint16_t press(std::uint16_t key, std::uint16_t segment,
                                    std::uint16_t offset,
                                    std::string_view bar = "Move Bravo") const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_ip),
             offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_cs),
             segment);
    put_string(stack_segment,
               static_cast<std::uint16_t>(frame_base - routine_bar), bar);
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first + 2);
    put_word(0x40, ring_first, key);
    put_byte(dgroup(), data_pushback, 0);
    put_byte(menu_segment, key_point, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = menu_segment;
    r.ip = key_point;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = stack_top;
    r[cpu::reg16::bp] = frame_base;
    box->step();
    return word(0x40, word(0x40, 0x1A));
  }

  [[nodiscard]] std::uint16_t at_the_party_bar(std::uint16_t key) const {
    return press(key, adventure_segment, bar_after);
  }

  /// Into walking mode, the way a player gets there: `M` off the bar.
  void start_walking() const {
    before();
    arrive(bar_after, 'M');
  }

  std::unique_ptr<machine> box;
};

TEST(SeamMoveModeOnCurse, TheBarIsHandedMoveInAreasPlace) {
  const rig r;
  r.arm();
  r.put_byte(rig::dgroup(), data_highlight, 3);
  r.before();
  EXPECT_TRUE(r.handed_the_copy());
  EXPECT_EQ(r.handed(), "Move Bravo Cobra");
  EXPECT_EQ(r.string_at(rig::dgroup(), party_bar), a_bar)
      << "the program's string is never written";
  EXPECT_EQ(r.lit(), 1) << "the party has just arrived: Move is lit";
  r.arrive(bar_after, 'B');
  EXPECT_EQ(r.string_at(rig::dgroup(), party_bar), a_bar);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

TEST(SeamMoveModeOnCurse, MoveStartsWalkingAndExitEndsIt) {
  const rig r;
  r.arm();
  r.start_walking();
  r.before();
  EXPECT_EQ(r.handed(), "Exit");
  EXPECT_EQ(r.lit(), 1);
  r.arrive(bar_after, 0x48, 1);  // a step: still walking
  r.before();
  EXPECT_EQ(r.handed(), "Exit");
  r.arrive(bar_after, 'E');
  EXPECT_EQ(r.al(), '-') << "the program loops on this, and does not camp";
  r.before();
  EXPECT_EQ(r.handed(), "Move Bravo Cobra");
  EXPECT_EQ(r.lit(), 1) << "back at the menu with Move lit";
}

TEST(SeamMoveModeOnCurse, InMenuModeTheArrowsStepTheBarAndTheParty) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.at_the_party_bar(left), comma);
  EXPECT_EQ(r.at_the_party_bar(up), home);
  EXPECT_EQ(r.at_the_party_bar(down), end);
  EXPECT_EQ(r.at_the_party_bar(row_8), home);
  EXPECT_EQ(r.at_the_party_bar(letter_h), ignored);
  EXPECT_EQ(r.at_the_party_bar(letter_b), letter_b) << "Bravo's";
}

TEST(SeamMoveModeOnCurse, InWalkingModeTheArrowsWalkAndEscIsExit) {
  const rig r;
  r.arm();
  r.start_walking();
  EXPECT_EQ(r.press(left, adventure_segment, bar_after, "Exit"), left);
  EXPECT_EQ(r.press(up, adventure_segment, bar_after, "Exit"), up);
  EXPECT_EQ(r.press(escape, adventure_segment, bar_after, "Exit"), key_e);
  EXPECT_EQ(r.press(letter_b, adventure_segment, bar_after, "Exit"), ignored);
}

TEST(SeamMoveModeOnCurse, ADoorsBarKeepsTheWalkAndAnyOtherEndsIt) {
  const rig r;
  r.arm();
  r.start_walking();
  for (const std::uint16_t door : {door_bar_locked, door_bar_stuck}) {
    EXPECT_EQ(r.press(letter_b, adventure_segment, door), letter_b);
    r.before();
    EXPECT_EQ(r.handed(), "Exit") << door;
    r.arrive(bar_after, 0x48, 1);
  }
  EXPECT_EQ(r.press(letter_b, camp_segment, camp_return), letter_b);
  r.before();
  EXPECT_EQ(r.handed(), "Move Bravo Cobra");
  EXPECT_EQ(r.lit(), 1);
}

TEST(SeamMoveModeOnCurse, TheOverheadViewIsLeftForThe3DViewBeforeTheBar) {
  const rig r;
  r.arm();
  r.put_byte(rig::dgroup(), data_overhead, 1);
  r.before();
  EXPECT_EQ(r.byte(rig::dgroup(), data_overhead), 0);
  EXPECT_FALSE(r.handed_the_copy()) << "the bar waits for the redraw";
}

TEST(SeamMoveModeOnCurse, PoolOfRadiancesCityCallIsNotAPointHere) {
  const rig r;
  r.arm();
  r.before(baseline_city_before);
  EXPECT_FALSE(r.handed_the_copy());
  EXPECT_EQ(r.handed(), a_bar);
}

}  // namespace
}  // namespace amberfolio::machine
