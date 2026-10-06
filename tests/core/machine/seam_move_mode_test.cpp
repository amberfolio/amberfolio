// SPDX-License-Identifier: AGPL-3.0-only
//
// The move-mode piece of `modern-controls` (seam_move_mode.cpp, #479),
// exercised through its mechanism and not through any program: the test
// stands the processor on one of the piece's points with the bar, the frame
// and the ring laid out where the facts say they are, takes one step, and
// reads what the handler left.
//
// The offsets below are restated rather than read out of the seam, which is
// the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The bars are made-up words, and
// **every byte here is this file's own** (PLAN.md §6).

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";

/// The words the overlay manager keeps overlay 25's, overlay 14's and
/// overlay 15's segments in, and where this test puts them.
constexpr std::uint32_t word_menu = 0x3C60;
constexpr std::uint32_t word_adventure = 0x730;
constexpr std::uint32_t word_camp = 0x760;
constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t adventure_segment = 0x6400;
constexpr std::uint16_t camp_segment = 0x6800;

/// The points: either side of the party bar's call, city and wilderness, in
/// overlay 14; and the key read in overlay 25.
constexpr std::uint16_t city_before = 0x09D0;
constexpr std::uint16_t city_after = 0x09D5;
constexpr std::uint16_t wild_before = 0x0C40;
constexpr std::uint16_t wild_after = 0x0C45;
constexpr std::uint16_t key_point = 0x0572;
/// The camp bar's return, a caller of the routine that is not the party's.
constexpr std::uint16_t camp_return = 0x1F24;

/// The data segment is the image's paragraph 0xC7C on, and the facts in it.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t city_bar = 0x04B6;
constexpr std::uint16_t wild_bar = 0x04DF;
constexpr std::uint16_t data_highlight = 0x6B2B;
constexpr std::uint16_t data_overhead = 0x6AAC;
constexpr std::uint16_t data_pushback = 0x8501;

/// The adventuring loop's frame: the out-parameter below BP. The menu-bar
/// routine's: its caller's return above BP, and its copy of the bar below.
constexpr std::uint16_t loop_out_flag = 0x04;
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;
constexpr std::uint16_t routine_bar = 0x53;

constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t ring_first = 0x1E;

/// Keys, as the ring holds them.
constexpr std::uint16_t left = 0x4B00;
constexpr std::uint16_t right = 0x4D00;
constexpr std::uint16_t up = 0x4800;
constexpr std::uint16_t down = 0x5000;
constexpr std::uint16_t home = 0x4700;
constexpr std::uint16_t end = 0x4F00;
constexpr std::uint16_t comma = 0x332C;
constexpr std::uint16_t period = 0x342E;
constexpr std::uint16_t escape = 0x011B;
constexpr std::uint16_t enter = 0x1C0D;
constexpr std::uint16_t ignored = 0x0C2D;
constexpr std::uint16_t key_e = 0x1245;
constexpr std::uint16_t row_8 = 0x0938;
constexpr std::uint16_t row_2 = 0x0332;
constexpr std::uint16_t row_3 = 0x0433;
constexpr std::uint16_t pad_4 = 0x4B34;
constexpr std::uint16_t pad_6 = 0x4D36;
constexpr std::uint16_t letter_h = 0x2368;
constexpr std::uint16_t letter_c = 0x2E63;
constexpr std::uint16_t letter_b = 0x3062;

/// A made-up city bar: a first group of a capital `A` and three more, as the
/// facts say the program's is, and two more groups.
constexpr std::string_view a_city_bar = "Alfa Bravo Cobra";
constexpr std::string_view a_wild_bar = "Bravo Cobra";

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] static constexpr std::uint16_t dgroup() {
    return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
  }

  void arm(std::string_view id = seam_id) const {
    ASSERT_EQ(box->seams().enable(id), seam_reason::none);
    manager_says(word_menu, menu_segment);
    manager_says(word_adventure, adventure_segment);
    manager_says(word_camp, camp_segment);
    lay_bars();
  }

  void manager_says(std::uint32_t word, std::uint16_t segment) const {
    put_word(image_load_segment, static_cast<std::uint16_t>(word), segment);
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

  void lay_bars() const {
    put_string(dgroup(), city_bar, a_city_bar);
    put_string(dgroup(), wild_bar, a_wild_bar);
  }

  [[nodiscard]] std::string city() const {
    return string_at(dgroup(), city_bar);
  }
  [[nodiscard]] std::string wild() const {
    return string_at(dgroup(), wild_bar);
  }

  [[nodiscard]] std::uint8_t lit() const {
    return byte(dgroup(), data_highlight);
  }
  void light(std::uint8_t group) const {
    put_byte(dgroup(), data_highlight, group);
  }

  [[nodiscard]] std::uint64_t declined() const {
    return box->seams().status(seam_id).declined;
  }

  // --- The party bar's points ----------------------------------------------

  /// Stand on `point` in overlay 14 with AL and the out-parameter as given,
  /// and take one step (the instruction there is a NOP).
  void arrive(std::uint16_t point, std::uint8_t al = 0,
              std::uint8_t out_flag = 0) const {
    put_byte(adventure_segment, point, 0x90);
    put_byte(stack_segment,
             static_cast<std::uint16_t>(frame_base - loop_out_flag), out_flag);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = adventure_segment;
    r.ip = point;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    r.set(cpu::reg8::al, al);
    box->step();
  }

  [[nodiscard]] std::uint8_t al() const {
    return box->processor().regs().get(cpu::reg8::al);
  }

  /// One whole call of the city bar: the bar goes out, the routine is
  /// "answered" with `letter` and the highlight it would have set, and the
  /// bar comes back.
  void city_call(std::uint8_t letter, std::uint8_t sets_lit,
                 std::uint8_t out_flag = 0) const {
    arrive(city_before);
    light(sets_lit);
    arrive(city_after, letter, out_flag);
  }

  void wild_call(std::uint8_t letter, std::uint8_t sets_lit,
                 std::uint8_t out_flag = 0) const {
    arrive(wild_before);
    light(sets_lit);
    arrive(wild_after, letter, out_flag);
  }

  // --- The key read ----------------------------------------------------------

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
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    box->step();
    return word(0x40, word(0x40, 0x1A));
  }

  [[nodiscard]] std::uint16_t at_the_city_bar(std::uint16_t key) const {
    return press(key, adventure_segment, city_after);
  }

  /// Into walking mode, the way a player gets there: `M` off the bar.
  void start_walking() const { city_call('M', 1); }

  std::unique_ptr<machine> box;
};

// --- Off ----------------------------------------------------------------

TEST(SeamMoveMode, WithTheSeamOffNothingIsTouched) {
  const rig r;
  r.manager_says(word_adventure, adventure_segment);
  r.manager_says(word_menu, menu_segment);
  r.lay_bars();
  r.light(3);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), a_city_bar);
  EXPECT_EQ(r.lit(), 3);
  r.arrive(wild_before);
  EXPECT_EQ(r.wild(), a_wild_bar);
  EXPECT_EQ(r.at_the_city_bar(left), left);
}

// --- Menu mode: the bar ----------------------------------------------------

TEST(SeamMoveMode, TheCityBarsFirstGroupIsMoveAndComesBack) {
  const rig r;
  r.arm();
  r.light(3);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Move Bravo Cobra");
  EXPECT_EQ(r.lit(), 1) << "the party has just arrived: Move is lit";
  r.arrive(city_after, 'B');
  EXPECT_EQ(r.city(), a_city_bar) << "the program's string, byte for byte";
  EXPECT_EQ(r.declined(), 0u);
}

TEST(SeamMoveMode, TheWildernessBarGetsMoveInFrontAndComesBack) {
  const rig r;
  r.arm();
  r.arrive(wild_before);
  EXPECT_EQ(r.wild(), "Move Bravo Cobra");
  r.arrive(wild_after, 'B');
  EXPECT_EQ(r.wild(), a_wild_bar);
  EXPECT_EQ(r.declined(), 0u);
}

TEST(SeamMoveMode, ASecondArrivalBeforeTheBarComesBackRewritesNothing) {
  // The point is offered again after a batch: the bar is already the
  // piece's, and is not given a second `Move`.
  const rig r;
  r.arm();
  r.arrive(wild_before);
  r.arrive(wild_before);
  EXPECT_EQ(r.wild(), "Move Bravo Cobra");
  r.arrive(wild_after, 'B');
  EXPECT_EQ(r.wild(), a_wild_bar);
}

TEST(SeamMoveMode, ABarThatIsNotTheFactsShapeIsLeftAndDeclined) {
  const rig r;
  r.arm();
  r.put_string(rig::dgroup(), city_bar, "Bravo Cobra");
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Bravo Cobra");
  EXPECT_EQ(r.declined(), 1u);
  r.put_string(rig::dgroup(), wild_bar, "Bravo Cobra Delta Echo Foxtrot Golfs");
  r.arrive(wild_before);
  EXPECT_EQ(r.wild(), "Bravo Cobra Delta Echo Foxtrot Golfs")
      << "no room for five more";
}

// --- Menu mode: the highlight ----------------------------------------------

TEST(SeamMoveMode, TheCitysHighlightIsTheProgramsNumberingOutsideTheCall) {
  const rig r;
  r.arm();
  r.city_call('C', 3);
  EXPECT_EQ(r.lit(), 3) << "Cobra, the third group on both bars";
  r.arrive(city_before);
  EXPECT_EQ(r.lit(), 3) << "the party's place is kept";
  r.light(1);  // the player steps onto Move and takes an Up
  r.arrive(city_after, 0x47, 1);
  r.arrive(city_before);
  EXPECT_EQ(r.lit(), 1) << "and Move stays lit";
}

TEST(SeamMoveMode, TheWildernesssHighlightIsMovedForTheInsertedGroup) {
  const rig r;
  r.arm();
  r.light(2);
  r.arrive(wild_before);
  EXPECT_EQ(r.lit(), 1) << "arrived: Move";
  r.light(3);
  r.arrive(wild_after, 'C');
  EXPECT_EQ(r.lit(), 2) << "Cobra is the program's second group";
  r.arrive(wild_before);
  EXPECT_EQ(r.lit(), 3) << "and the bar's third";
  r.light(1);
  r.arrive(wild_after, 0x47, 1);
  EXPECT_EQ(r.lit(), 3 - 1) << "Move has no group of the program's";
  r.arrive(wild_before);
  EXPECT_EQ(r.lit(), 1);
}

TEST(SeamMoveMode, NotesChosenPutsTheHighlightBackAsItWasEntered) {
  const rig r;
  r.arm();
  r.city_call('C', 3);
  r.city_call('N', 4);
  EXPECT_EQ(r.lit(), 3);
}

// --- Walking mode --------------------------------------------------------

TEST(SeamMoveMode, MoveStartsWalkingAndTheBarIsExitAloneLit) {
  const rig r;
  r.arm();
  r.light(5);
  r.start_walking();
  EXPECT_EQ(r.city(), a_city_bar);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Exit");
  EXPECT_EQ(r.lit(), 1);
  r.arrive(city_after, 0x48, 1);  // a step: still walking
  EXPECT_EQ(r.city(), a_city_bar);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Exit");
  r.arrive(city_after, 0x4B, 1);
  EXPECT_EQ(r.declined(), 0u);
}

TEST(SeamMoveMode, ExitStopsWalkingAndIsNotTheProgramsCamp) {
  const rig r;
  r.arm();
  r.start_walking();
  r.arrive(city_before);
  r.arrive(city_after, 'E');
  EXPECT_EQ(r.al(), '-') << "the program loops on this, and does not camp";
  EXPECT_EQ(r.city(), a_city_bar);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Move Bravo Cobra");
  EXPECT_EQ(r.lit(), 1) << "back at the menu with Move lit";
}

TEST(SeamMoveMode, TheWalkingBarHasNoNotesWithTheJournalOn) {
  const rig r;
  r.arm();
  r.arm("journal");
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Move Bravo Cobra Notes") << "menu mode has it";
  r.arrive(city_after, 'M');
  EXPECT_EQ(r.city(), a_city_bar);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Exit");
  r.arrive(city_after, 'E');
  EXPECT_EQ(r.city(), a_city_bar) << "nothing left behind";
}

TEST(SeamMoveMode, AnyOtherBarEndsTheWalkAndLightsMove) {
  const rig r;
  r.arm();
  r.start_walking();
  EXPECT_EQ(r.press(letter_b, camp_segment, camp_return), letter_b);
  r.arrive(city_before);
  EXPECT_EQ(r.city(), "Move Bravo Cobra");
  EXPECT_EQ(r.lit(), 1);
}

TEST(SeamMoveMode, TheOverheadViewIsLeftForThe3DViewBeforeTheBar) {
  const rig r;
  r.arm();
  r.put_byte(rig::dgroup(), data_overhead, 1);
  r.arrive(city_before);
  EXPECT_EQ(r.byte(rig::dgroup(), data_overhead), 0);
  EXPECT_EQ(r.city(), a_city_bar) << "the bar waits for the redraw";
}

// --- Keys --------------------------------------------------------------------

TEST(SeamMoveMode, InMenuModeTheArrowsStepTheBarAndTheParty) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.at_the_city_bar(left), comma);
  EXPECT_EQ(r.at_the_city_bar(right), period);
  EXPECT_EQ(r.at_the_city_bar(up), home);
  EXPECT_EQ(r.at_the_city_bar(down), end);
  EXPECT_EQ(r.at_the_city_bar(pad_4), comma);
  EXPECT_EQ(r.at_the_city_bar(pad_6), period);
  EXPECT_EQ(r.at_the_city_bar(row_8), home);
  EXPECT_EQ(r.at_the_city_bar(row_2), end);
  EXPECT_EQ(r.press(left, adventure_segment, wild_after), comma);
}

TEST(SeamMoveMode, InMenuModeALetterOffTheBarIsThrownAway) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.at_the_city_bar(letter_h), ignored) << "a typed H does not walk";
  EXPECT_EQ(r.at_the_city_bar(letter_b), letter_b) << "Bravo's";
  EXPECT_EQ(r.at_the_city_bar(enter), enter) << "bar-keys' own";
  EXPECT_EQ(r.at_the_city_bar(row_3), row_3);
}

TEST(SeamMoveMode, InWalkingModeTheArrowsWalkAndEscIsExit) {
  const rig r;
  r.arm();
  r.start_walking();
  for (const std::uint16_t key : {left, right, up, down, row_8, pad_4}) {
    EXPECT_EQ(r.at_the_city_bar(key), key) << key;
  }
  EXPECT_EQ(r.at_the_city_bar(escape), key_e);
  EXPECT_EQ(r.at_the_city_bar(letter_c), ignored);
  EXPECT_EQ(r.at_the_city_bar(0x1265), 0x1265) << "e is Exit's own";
}

TEST(SeamMoveMode, AnotherCallersKeysAreLeftAlone) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.press(letter_h, camp_segment, camp_return), letter_h);
  EXPECT_EQ(r.press(row_8, camp_segment, camp_return), row_8);
}

TEST(SeamMoveMode, LeavesTheKeyAloneWhileTheJournalReaderIsOpen) {
  const rig r;
  r.arm();
  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.at_the_city_bar(letter_h), letter_h);
}

}  // namespace
}  // namespace amberfolio::machine
