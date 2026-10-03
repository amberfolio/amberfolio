// SPDX-License-Identifier: AGPL-3.0-only
//
// The bar-keys seam (seam_bar_keys.cpp, #425), exercised through its
// mechanism and not through any program: the test stands the processor on
// the seam's one point with the menu-bar routine's frame laid out where the
// facts say it is, a keystroke at the head of the BIOS ring, and reads the
// ring after one step.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The bars are made-up words, and
// **every byte here is this file's own** (PLAN.md §6).

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "bar-keys";

/// The point: the call into the key-read routine, in overlay 25.
constexpr std::uint16_t point = 0x0572;

/// The words the program's overlay manager keeps three modules' segments
/// in: overlay 25, overlay 14 (the adventuring loop), overlay 15 (camp).
constexpr std::uint32_t word_menu = 0x3C60;
constexpr std::uint32_t word_adventure = 0x730;
constexpr std::uint32_t word_camp = 0x760;

/// Where each module is, in this test.
constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t adventure_segment = 0x6400;
constexpr std::uint16_t camp_segment = 0x6800;

/// The callers' return offsets.
constexpr std::uint16_t ret_yes_no = 0x111E;
constexpr std::uint16_t ret_area = 0x09D5;
constexpr std::uint16_t ret_view = 0x0C45;
constexpr std::uint16_t ret_camp = 0x1F24;
/// The pick-list's call into the routine, which is in overlay 25.
constexpr std::uint16_t ret_pick_list = 0x0FE0;

/// The frame, below and above BP.
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;
constexpr std::uint16_t frame_raw = 0x0C;
constexpr std::uint16_t local_enter = 0x8F;
constexpr std::uint16_t local_bar = 0x53;

constexpr std::uint16_t data_highlight = 0x6B2B;
constexpr std::uint16_t data_pushback = 0x8501;

/// The keys, as the ring holds them.
constexpr std::uint16_t left = 0x4B00;
constexpr std::uint16_t right = 0x4D00;
constexpr std::uint16_t enter = 0x1C0D;
constexpr std::uint16_t comma = 0x332C;
constexpr std::uint16_t period = 0x342E;

constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

constexpr std::uint16_t ring_first = 0x1E;

/// The character a keystroke word carries.
[[nodiscard]] char letter_of(std::uint16_t key) {
  return static_cast<char>(key & 0xFFU);
}

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] const seam_definition& seam() const {
    const seam_definition* found = box->seams().find(seam_id);
    EXPECT_NE(found, nullptr);
    return *found;
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    manager_says(word_menu, menu_segment);
    manager_says(word_adventure, adventure_segment);
    manager_says(word_camp, camp_segment);
  }

  /// What the overlay manager writes: where a module begins now.
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

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    const auto& ram = box->memory().ram();
    const std::uint32_t at = cpu::physical_address(segment, offset);
    return static_cast<std::uint16_t>(ram[at] | (ram[at + 1] << 8U));
  }

  /// Lay out the routine's parse of `bar` where the facts say it is, by the
  /// rule the routine follows: the table is cleared, every `0-9A-Z`
  /// character begins a group, a group runs to two before the next one
  /// begins, and a bar with no such character still has the one group.
  void lay_bar(std::string_view bar, std::uint8_t highlight,
               std::uint8_t enter_allowed = 1) const {
    const auto table = [](unsigned index) {
      return static_cast<std::uint16_t>(frame_base - local_enter + index);
    };
    for (unsigned i = 0; i < 0x2A; ++i) {
      put_byte(stack_segment, table(i), 0);
    }
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_base - local_bar),
             static_cast<std::uint8_t>(bar.size()));
    unsigned group = 1;
    for (std::size_t i = 0; i < bar.size(); ++i) {
      const char c = bar[i];
      put_byte(stack_segment,
               static_cast<std::uint16_t>(frame_base - local_bar + 1 + i),
               static_cast<std::uint8_t>(c));
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) {
        continue;
      }
      const auto position = static_cast<std::uint8_t>(i + 1);
      if (byte_is_zero(table(2 * group))) {
        put_byte(stack_segment, table(2 * group), position);
      } else {
        put_byte(stack_segment, table(2 * group + 1),
                 static_cast<std::uint8_t>(position - 2));
        ++group;
        put_byte(stack_segment, table(2 * group), position);
      }
    }
    put_byte(stack_segment, table(2 * group + 1),
             static_cast<std::uint8_t>(bar.size()));
    put_byte(stack_segment, table(1), static_cast<std::uint8_t>(group));
    put_byte(stack_segment, table(0), enter_allowed);
    put_byte(data_segment, data_highlight, highlight);
  }

  /// Whether the byte at `offset` in the stack segment is zero.
  [[nodiscard]] bool byte_is_zero(std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(stack_segment, offset)] ==
           0;
  }

  /// Who called, and in what mode.
  void called_from(std::uint16_t segment, std::uint16_t offset,
                   std::uint8_t raw) const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_ip),
             offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_cs),
             segment);
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_base + frame_raw),
             raw);
  }

  /// A keystroke at the head of the ring, and nothing behind it.
  void ring(std::uint16_t key) const {
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first + 2);
    put_word(0x40, ring_first, key);
  }

  void ring_empty() const {
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first);
  }

  [[nodiscard]] std::uint16_t head_word() const {
    return word(0x40, word(0x40, 0x1A));
  }

  /// Stand on the point and take one step. The instruction there is a HLT,
  /// so the step is the arrival and nothing else.
  void arrive() const {
    put_byte(menu_segment, point, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = menu_segment;
    r.ip = point;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    box->step();
  }

  /// One arrival with `key` at the head of the ring, from `segment:offset`
  /// in mode `raw`, answering what the ring then holds.
  [[nodiscard]] std::uint16_t press(std::uint16_t key, std::uint16_t segment,
                                    std::uint16_t offset,
                                    std::uint8_t raw) const {
    called_from(segment, offset, raw);
    ring(key);
    arrive();
    return head_word();
  }

  std::unique_ptr<machine> box;
};

// --- The definition --------------------------------------------------------

TEST(SeamBarKeys, IsOnePointInOverlay25) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the keys work, nothing to pull";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 1u);

  const seam_point& p = s.points[0];
  EXPECT_EQ(p.offset, point);
  EXPECT_FALSE(p.module.is_resident_image());
  EXPECT_EQ(p.module.file, "GAME.OVR");
  EXPECT_EQ(p.module.file_offset, 182479u);
  EXPECT_EQ(p.module.length, 4682u);
  EXPECT_FALSE(p.module.digest.empty());
  EXPECT_EQ(p.module.load_segment_at, word_menu);
  EXPECT_FALSE(p.at_every_step);
  EXPECT_FALSE(p.inside_calls);
}

TEST(SeamBarKeys, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamBarKeys, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamBarKeys, IsInertWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 1);
  r.manager_says(word_menu, 0);

  EXPECT_EQ(r.box->seams().status(seam_id).reason,
            seam_reason::module_not_resident);
  EXPECT_EQ(r.press(left, stack_segment, 0, 0), left);
}

TEST(SeamBarKeys, DoesNothingWhileItIsOff) {
  const rig r;
  r.manager_says(word_menu, menu_segment);
  r.manager_says(word_camp, camp_segment);
  r.lay_bar("Ant Bee Cow", 2);

  EXPECT_EQ(r.press(left, camp_segment, ret_camp, 0), left);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
}

// --- Left and Right --------------------------------------------------------

TEST(SeamBarKeys, LeftAndRightBecomeTheBarsOwnTwoStepKeysWhereRawModeIsClear) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  EXPECT_EQ(r.press(left, stack_segment, 0x1234, 0), comma);
  EXPECT_EQ(r.press(right, stack_segment, 0x1234, 0), period);
}

TEST(SeamBarKeys, LeftAndRightAreTheCallersWhereRawModeIsSet) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The party's arrows in 3D and in the wilderness, the aiming cursor and
  // the ability-score screen, all of which hand the routine a raw mode. Any
  // non-zero byte is raw: the routine tests it as a byte.
  for (const std::uint8_t raw : std::array<std::uint8_t, 4>{1, 2, 0x0D, 0x80}) {
    EXPECT_EQ(r.press(left, adventure_segment, ret_area, raw), left)
        << int{raw};
    EXPECT_EQ(r.press(right, camp_segment, ret_camp, raw), right) << int{raw};
  }
}

TEST(SeamBarKeys, LeftAndRightAreSteppedWhateverBarIsUpAndWhoeverCalled) {
  const rig r;
  r.arm();
  // The arrow rewrite is the routine's own, caller-independent: it throws
  // an arrow away in raw mode clear. Not a bar of any particular shape.
  r.lay_bar("", 1);
  EXPECT_EQ(r.press(left, stack_segment, 0, 0), comma);
  r.lay_bar("Ant", 1, 0);
  EXPECT_EQ(r.press(right, menu_segment, ret_pick_list, 0), period);
}

TEST(SeamBarKeys, EveryOtherKeyIsLeftWhereItIs) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // Up, Down, Home, End, a ctrl-arrow (a different scan code), the digit
  // the keypad's 4 makes with Num Lock on (the right scan code, and a
  // character), a letter, Escape, Space.
  for (const std::uint16_t key :
       {std::uint16_t{0x4800}, std::uint16_t{0x5000}, std::uint16_t{0x4700},
        std::uint16_t{0x4F00}, std::uint16_t{0x7300}, std::uint16_t{0x4B34},
        std::uint16_t{0x1E41}, std::uint16_t{0x011B}, std::uint16_t{0x3920}}) {
    EXPECT_EQ(r.press(key, camp_segment, ret_camp, 0), key) << key;
    EXPECT_EQ(r.press(key, camp_segment, ret_camp, 1), key) << key;
  }
}

// --- Enter -----------------------------------------------------------------

TEST(SeamBarKeys, EnterTakesTheHighlightedCommandAtEachTabledCaller) {
  struct caller {
    std::uint16_t segment;
    std::uint16_t offset;
  };
  const std::array<caller, 4> callers{
      {{.segment = menu_segment, .offset = ret_yes_no},
       {.segment = adventure_segment, .offset = ret_area},
       {.segment = adventure_segment, .offset = ret_view},
       {.segment = camp_segment, .offset = ret_camp}}};
  for (const caller& c : callers) {
    const rig r;
    r.arm();
    for (std::uint8_t group = 1; group <= 3; ++group) {
      r.lay_bar("Ant Bee Cow", group);
      const std::uint16_t answer = r.press(enter, c.segment, c.offset, 1);
      EXPECT_EQ(letter_of(answer), "ABC"[group - 1]) << int{group};
      EXPECT_EQ(answer >> 8U, 0x1Cu) << "posted under Enter's own scan code";
    }
  }
}

TEST(SeamBarKeys, EnterAtTheYesNoPromptTakesTheHighlightedAnswer) {
  // Its two-group bar, the one the prompt starts on the second of.
  const rig r;
  r.arm();
  r.lay_bar("Yup Nay", 2);
  EXPECT_EQ(letter_of(r.press(enter, menu_segment, ret_yes_no, 0)), 'N');
  r.lay_bar("Yup Nay", 1);
  EXPECT_EQ(letter_of(r.press(enter, menu_segment, ret_yes_no, 0)), 'Y');
}

TEST(SeamBarKeys, EnterTakesACommandSplicedOntoTheBarAsItTakesAnyOther) {
  // The journal's `Notes` and the Encamp Fix's `Fix` are groups of the
  // routine's own parse by the time it is asked for a key.
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow Notes", 4);
  EXPECT_EQ(letter_of(r.press(enter, adventure_segment, ret_view, 1)), 'N');
  r.lay_bar("Ant Bee Cow Fix", 4);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'F');
}

TEST(SeamBarKeys, EnterIsLeftAloneAtACallerThatIsNotInTheTable) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The pick-list, which is in overlay 25 and confirms its row on Enter.
  EXPECT_EQ(r.press(enter, menu_segment, ret_pick_list, 0), enter);
  // A tabled offset in the wrong module, a tabled module at the wrong
  // offset, and a stack that names no module at all.
  EXPECT_EQ(r.press(enter, camp_segment, ret_area, 1), enter);
  EXPECT_EQ(r.press(enter, adventure_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.press(enter, menu_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.press(enter, menu_segment, 0, 1), enter);
  EXPECT_EQ(r.press(enter, 0, ret_yes_no, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u)
      << "a caller the table does not name is not a refusal";
}

TEST(SeamBarKeys, EnterIsLeftAloneWhileTheManagerSaysTheCallersModuleIsOut) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The return segment equals a stale word's value only if the manager's
  // word still holds it. Zero means "not loaded", and zero is not a
  // segment a caller returns to.
  r.manager_says(word_camp, 0);
  EXPECT_EQ(r.press(enter, 0, ret_camp, 1), enter);
  r.manager_says(word_camp, camp_segment);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'B');
}

TEST(SeamBarKeys, EnterIsLeftAloneAtABarTheRoutineDoesNotDraw) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2, 0);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter)
      << "the routine hands Enter back only from a bar it draws";
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EnterIsRefusedWhereALaterGroupHoldsTheSameLetter) {
  // The routine's scan does not stop at its first match: typed, `A` would
  // select the third group, so Enter on the first must not be an `A`.
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Axe", 1);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::on);

  // On the third it is the last match, and the same letter is right.
  r.lay_bar("Ant Bee Axe", 3);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'A');
  // And the middle group's letter is its own.
  r.lay_bar("Ant Bee Axe", 2);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'B');
}

TEST(SeamBarKeys, EnterDeclinesAFrameThatIsNotTheOneTheFactsDescribe) {
  const rig r;
  r.arm();

  // A highlight of zero, one past the group count, and a bar of no groups.
  r.lay_bar("Ant Bee Cow", 0);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  r.lay_bar("Ant Bee Cow", 4);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  r.lay_bar("a b c", 1);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 3u);
}

TEST(SeamBarKeys, EnterReadsTheHighlightFromTheDataSegmentNotTheStack) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 3);
  // The stack segment holds a different answer at the same offset.
  r.put_byte(stack_segment, data_highlight, 1);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'C');
}

// --- What the seam leaves alone --------------------------------------------

TEST(SeamBarKeys, LeavesTheRingAloneWhenItIsEmpty) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);
  r.called_from(camp_segment, ret_camp, 0);
  r.ring_empty();
  r.put_word(0x40, ring_first, left);
  r.arrive();

  // The word sits where a key was once; the pointers say there is none.
  EXPECT_EQ(r.word(0x40, ring_first), left);
  EXPECT_EQ(r.word(0x40, 0x1A), r.word(0x40, 0x1C));
}

TEST(SeamBarKeys, LeavesTheKeyAloneWhileThePushbackSlotIsArmed) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The poll answers "a key" from the slot, and the head of the ring is
  // not what the program is about to read.
  r.put_byte(data_segment, data_pushback, 0x4B);
  EXPECT_EQ(r.press(left, camp_segment, ret_camp, 0), left);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
}

TEST(SeamBarKeys, LeavesTheKeyAloneWhileTheJournalReaderIsOpen) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press(left, camp_segment, ret_camp, 0), left);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);

  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(r.press(left, camp_segment, ret_camp, 0), comma);
}

TEST(SeamBarKeys, WritesNothingButTheHeadWordAndTouchesNoRegister) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);
  r.called_from(camp_segment, ret_camp, 1);
  r.ring(enter);
  r.put_word(0x40, ring_first + 2, 0xBEEF);
  r.arrive();

  EXPECT_EQ(r.word(0x40, ring_first + 2), 0xBEEF) << "the key behind it";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first) << "the head did not move";
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 2) << "nor the tail";

  const cpu::registers& regs = r.box->processor().regs();
  EXPECT_EQ(regs[cpu::reg16::bp], frame_base);
  EXPECT_EQ(regs[cpu::reg16::sp], 0x0400);
}

TEST(SeamBarKeys, DoesNotReadTheFrameForAKeyItHasNoBusinessWith) {
  // A letter at a bar whose frame is garbage costs nothing and declines
  // nothing: only the three keys look at the frame at all.
  const rig r;
  r.arm();
  r.lay_bar("", 0, 0);
  EXPECT_EQ(r.press(0x1E41, camp_segment, ret_camp, 1), 0x1E41);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

}  // namespace
}  // namespace amberfolio::machine
