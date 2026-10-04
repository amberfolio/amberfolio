// SPDX-License-Identifier: AGPL-3.0-only
//
// The hero-keys seam (seam_hero_keys.cpp, #439), exercised through its
// mechanism and not through any program: the test stands the processor on
// one of the seam's points with the frame laid out where the facts say it
// is, takes one step, and reads what the handler left.
//
// The offsets below are restated rather than read out of the seam, which is
// the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The party is made-up members and
// **every byte here is this file's own** (PLAN.md §6).
//
// What the program's party cursor does is stated in the seam's header and
// restated here as `cursor_goes_back()`, so that "the selection lands on the
// target" is proved against the rule and not against the seam's own
// reading of it, for the first, a middle and the last member, and for a
// party of one.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
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

constexpr std::string_view seam_id = "hero-keys";

// --- Where the facts put things --------------------------------------------

/// The key point, in overlay 25, and the two roster drawer points, in the
/// resident image.
constexpr std::uint16_t key_point = 0x0572;
constexpr std::uint16_t row_cleared = 0x138F;
constexpr std::uint16_t name_drawn = 0x13CB;

/// The words the overlay manager keeps the modules' segments in.
constexpr std::uint32_t word_menu = 0x3C60;
constexpr std::uint32_t word_temple = 0x230;
constexpr std::uint32_t word_post_combat = 0x260;
constexpr std::uint32_t word_shop = 0x290;
constexpr std::uint32_t word_script = 0x2C0;
constexpr std::uint32_t word_combat = 0x360;
constexpr std::uint32_t word_adventure = 0x730;
constexpr std::uint32_t word_camp = 0x760;
constexpr std::uint32_t word_main_menu = 0x790;

constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t temple_segment = 0x6200;
constexpr std::uint16_t post_combat_segment = 0x6400;
constexpr std::uint16_t shop_segment = 0x6600;
constexpr std::uint16_t script_segment = 0x6800;
constexpr std::uint16_t combat_segment = 0x6A00;
constexpr std::uint16_t adventure_segment = 0x6C00;
constexpr std::uint16_t camp_segment = 0x6E00;
constexpr std::uint16_t main_menu_segment = 0x7000;

struct caller {
  std::uint16_t segment;
  std::uint16_t offset;
  const char* name;
};

/// The callers that hand Home and End to the party cursor.
constexpr std::array<caller, 11> hero_callers{{
    {.segment = adventure_segment, .offset = 0x09D5, .name = "overhead bar"},
    {.segment = adventure_segment, .offset = 0x0C45, .name = "3D bar"},
    {.segment = main_menu_segment, .offset = 0x02FD, .name = "main menu"},
    {.segment = camp_segment, .offset = 0x1F24, .name = "camp"},
    {.segment = camp_segment, .offset = 0x1447, .name = "magic"},
    {.segment = camp_segment, .offset = 0x1CA4, .name = "alter"},
    {.segment = script_segment, .offset = 0x16EB, .name = "script prompt"},
    {.segment = post_combat_segment, .offset = 0x1024, .name = "loot"},
    {.segment = post_combat_segment, .offset = 0x0D91, .name = "take"},
    {.segment = shop_segment, .offset = 0x061F, .name = "shop"},
    {.segment = temple_segment, .offset = 0x0DAA, .name = "temple"},
}};

/// Callers of the same routine whose digits are real or whose Home is not
/// the cursor's: the party-order screen, the pick-list, the party picker's
/// neighbour, combat, the rest time, the stat editor, the Yes/No prompt.
constexpr std::array<caller, 8> other_callers{{
    {.segment = camp_segment, .offset = 0x17DA, .name = "party order"},
    {.segment = menu_segment, .offset = 0x0FE0, .name = "pick-list"},
    {.segment = menu_segment, .offset = 0x111E, .name = "yes/no"},
    {.segment = combat_segment, .offset = 0x0AC8, .name = "combat move"},
    {.segment = combat_segment, .offset = 0x0819, .name = "combat bar"},
    {.segment = main_menu_segment, .offset = 0x216E, .name = "stat editor"},
    {.segment = camp_segment, .offset = 0x076E, .name = "rest time"},
    {.segment = adventure_segment, .offset = 0x1234, .name = "unknown"},
}};

/// The menu-bar routine's frame: the return address above BP.
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;

/// The data segment is the image's paragraph 0xC7C on.
constexpr std::uint16_t dgroup_paragraphs = 0xC7C;
constexpr std::uint16_t data_current = 0x5D92;
constexpr std::uint16_t data_head = 0x5D96;
constexpr std::uint16_t data_pushback = 0x8501;
constexpr std::uint16_t record_next = 0x0104;

/// The keys, as the ring holds them: scan code high, character low.
constexpr std::uint16_t home = 0x4700;
constexpr std::uint16_t ignored = 0x0C2D;

/// The number row's digit, and the keypad's, as the BIOS holds them: the
/// row's scan codes run 0x02 for `1` to 0x0A for `9`, and `0` is 0x0B.
[[nodiscard]] constexpr std::uint16_t number_row(unsigned digit) {
  const unsigned scan = digit == 0 ? 0x0BU : 0x01U + digit;
  return static_cast<std::uint16_t>((scan << 8U) | ('0' + digit));
}
[[nodiscard]] constexpr std::uint16_t keypad(unsigned digit) {
  constexpr std::array<std::uint8_t, 10> scan{0x52, 0x4F, 0x50, 0x51, 0x4B,
                                              0x4C, 0x4D, 0x47, 0x48, 0x49};
  return static_cast<std::uint16_t>((scan.at(digit) << 8U) | ('0' + digit));
}

constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;
constexpr std::uint16_t party_segment = 0x4000;
constexpr std::uint16_t ring_first = 0x1E;

/// The roster drawer's frame.
constexpr std::uint16_t local_column = 5;
constexpr std::uint16_t local_row = 6;
constexpr std::uint16_t local_member = 4;
constexpr std::uint16_t drawer_bp = 0x0800;
constexpr std::uint16_t drawer_sp = 0x0780;

/// The program's routines the seam calls, as paragraph and offset.
constexpr std::uint16_t glyph_paragraph = 0x709;
constexpr std::uint16_t glyph_offset = 0x01DF;
constexpr std::uint16_t clear_paragraph = 0x3F1;
constexpr std::uint16_t clear_offset = 0x0137;

/// The same cursor rule the seam's header states: `G` from the head goes to
/// the tail, from anybody else to the member whose next is the current.
[[nodiscard]] unsigned cursor_goes_back(unsigned size, unsigned current) {
  return current == 0 ? size - 1U : current - 1U;
}

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] static constexpr std::uint16_t dgroup() {
    return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
  }

  [[nodiscard]] const seam_definition& seam() const {
    const seam_definition* found = box->seams().find(seam_id);
    EXPECT_NE(found, nullptr);
    return *found;
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    manager_says(word_menu, menu_segment);
    manager_says(word_temple, temple_segment);
    manager_says(word_post_combat, post_combat_segment);
    manager_says(word_shop, shop_segment);
    manager_says(word_script, script_segment);
    manager_says(word_combat, combat_segment);
    manager_says(word_adventure, adventure_segment);
    manager_says(word_camp, camp_segment);
    manager_says(word_main_menu, main_menu_segment);
    // The program's routines the handlers send the processor to: a NOP
    // each, so that the step that follows the redirect is the arrival and
    // nothing else.
    nop_at(glyph_paragraph, glyph_offset);
    nop_at(clear_paragraph, clear_offset);
  }

  void nop_at(std::uint16_t paragraph, std::uint16_t offset) const {
    put_byte(static_cast<std::uint16_t>(image_load_segment + paragraph), offset,
             0x90);
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

  // --- The party -----------------------------------------------------------

  /// Where member `index` (from zero) of the party is.
  [[nodiscard]] static std::uint16_t member_offset(unsigned index) {
    return static_cast<std::uint16_t>(0x0200U * index + 0x0010U);
  }

  /// A party of `count` made-up members linked head to tail, the head
  /// pointer and the selection (at `current`, from zero) in the data
  /// segment. Each name is `length` characters.
  void lay_party(unsigned count, unsigned current = 0,
                 std::uint8_t length = 6) const {
    for (unsigned i = 0; i < count; ++i) {
      put_byte(party_segment, member_offset(i), length);
      const bool last = i + 1 == count;
      put_word(party_segment,
               static_cast<std::uint16_t>(member_offset(i) + record_next),
               last ? std::uint16_t{0} : member_offset(i + 1));
      put_word(party_segment,
               static_cast<std::uint16_t>(member_offset(i) + record_next + 2),
               last ? std::uint16_t{0} : party_segment);
    }
    put_word(dgroup(), data_head, count == 0 ? 0 : member_offset(0));
    put_word(dgroup(), static_cast<std::uint16_t>(data_head + 2),
             count == 0 ? 0 : party_segment);
    select(count == 0 ? 0 : member_offset(current),
           count == 0 ? 0 : party_segment);
    put_byte(dgroup(), data_pushback, 0);
  }

  void select(std::uint16_t offset, std::uint16_t segment) const {
    put_word(dgroup(), data_current, offset);
    put_word(dgroup(), static_cast<std::uint16_t>(data_current + 2), segment);
  }

  /// Which member the selection names, from zero, or `~0u` for none.
  [[nodiscard]] unsigned selected(unsigned count) const {
    const std::uint16_t offset = word(dgroup(), data_current);
    for (unsigned i = 0; i < count; ++i) {
      if (offset == member_offset(i) &&
          word(dgroup(), static_cast<std::uint16_t>(data_current + 2)) ==
              party_segment) {
        return i;
      }
    }
    return ~0U;
  }

  // --- The menu-bar routine's point ----------------------------------------

  void called_from(std::uint16_t segment, std::uint16_t offset) const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_ip),
             offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_cs),
             segment);
  }

  void ring(std::uint16_t key) const {
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first + 2);
    put_word(0x40, ring_first, key);
  }

  [[nodiscard]] std::uint16_t head_word() const {
    return word(0x40, word(0x40, 0x1A));
  }

  /// Stand on the key point and take one step, with `ds` the data segment.
  void arrive_at_key(std::uint16_t ds = dgroup()) const {
    put_byte(menu_segment, key_point, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = menu_segment;
    r.ip = key_point;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    box->step();
  }

  /// One press of `key` from `from`, answering what the ring then holds.
  [[nodiscard]] std::uint16_t press(std::uint16_t key,
                                    const caller& from) const {
    called_from(from.segment, from.offset);
    ring(key);
    arrive_at_key();
    return head_word();
  }

  // --- The roster drawer's points ------------------------------------------

  /// The drawer's frame for a member drawn at `column` on `row`.
  void lay_row(std::uint8_t column, std::uint8_t row, unsigned member,
               std::uint8_t length) const {
    put_byte(party_segment, member_offset(member), length);
    put_byte(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_column), column);
    put_byte(stack_segment, static_cast<std::uint16_t>(drawer_bp - local_row),
             row);
    put_word(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_member),
             member_offset(member));
    put_word(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_member + 2),
             party_segment);
  }

  [[nodiscard]] std::uint8_t column() const {
    return byte(stack_segment,
                static_cast<std::uint16_t>(drawer_bp - local_column));
  }

  /// Stand on a roster point with the stack where it was, and step.
  void arrive_at_roster(std::uint16_t point) const {
    const std::uint32_t at = cpu::physical_address(image_load_segment, point);
    box->memory().ram()[at] = 0x90;  // NOP: the step is the arrival.
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = point;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = drawer_sp;
    r[cpu::reg16::bp] = drawer_bp;
    box->step();
  }

  /// Continue from where the last step left the processor: at the program's
  /// routine, which does nothing but return (a NOP then its `retf n`).
  void arrive_again(std::uint16_t point) const {
    box->memory().ram()[cpu::physical_address(image_load_segment, point)] =
        0x90;
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = point;
    box->step();
  }

  std::unique_ptr<machine> box;
};

/// What one call of the program's routine looks like on the stack once the
/// handler has set it up: the return address on top, then the arguments.
struct pending_call {
  std::uint16_t cs;
  std::uint16_t ip;
  std::uint16_t paragraph;
  std::uint16_t offset;
  std::vector<std::uint16_t> args;  // first pushed first
};

[[nodiscard]] pending_call read_call(const rig& r, unsigned argument_words) {
  const cpu::registers& regs = r.box->processor().regs();
  const std::uint16_t sp = regs[cpu::reg16::sp];
  pending_call out{};
  out.ip = r.word(stack_segment, sp);
  out.cs = r.word(stack_segment, static_cast<std::uint16_t>(sp + 2));
  out.paragraph =
      static_cast<std::uint16_t>(regs[cpu::sreg::cs] - image_load_segment);
  out.offset = static_cast<std::uint16_t>(regs.ip - 1U);  // past the NOP
  for (unsigned i = 0; i < argument_words; ++i) {
    // The deepest word was pushed first.
    out.args.push_back(r.word(
        stack_segment,
        static_cast<std::uint16_t>(sp + 4U + 2U * (argument_words - 1U - i))));
  }
  return out;
}

/// Let the program's routine return: pop its far return and its
/// arguments, as the `retf n` does.
void return_from(const rig& r, unsigned argument_words) {
  cpu::registers& regs = r.box->processor().regs();
  const std::uint16_t sp = regs[cpu::reg16::sp];
  const std::uint16_t ip = r.word(stack_segment, sp);
  const std::uint16_t cs =
      r.word(stack_segment, static_cast<std::uint16_t>(sp + 2));
  regs[cpu::reg16::sp] =
      static_cast<std::uint16_t>(sp + 4U + 2U * argument_words);
  regs[cpu::sreg::cs] = cs;
  regs.ip = ip;
}

// --- The definition --------------------------------------------------------

TEST(SeamHeroKeys, IsAKeyPointInOverlay25AndTwoInsideCallsPointsInTheRoster) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the keys work, nothing to pull";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 3u);

  const seam_point& key = s.points[0];
  EXPECT_EQ(key.offset, key_point);
  EXPECT_FALSE(key.module.is_resident_image());
  EXPECT_EQ(key.module.file, "GAME.OVR");
  EXPECT_EQ(key.module.file_offset, 182479u);
  EXPECT_EQ(key.module.length, 4682u);
  EXPECT_FALSE(key.module.digest.empty());
  EXPECT_EQ(key.module.load_segment_at, word_menu);
  EXPECT_FALSE(key.inside_calls);

  // The roster is drawn inside the automap's and the journal's batches,
  // which offer a point only if it says so.
  EXPECT_EQ(s.points[1].offset, row_cleared);
  EXPECT_EQ(s.points[2].offset, name_drawn);
  for (std::size_t i = 1; i < 3; ++i) {
    EXPECT_TRUE(s.points[i].module.is_resident_image());
    EXPECT_TRUE(s.points[i].inside_calls);
    EXPECT_FALSE(s.points[i].at_every_step);
  }
}

TEST(SeamHeroKeys, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamHeroKeys, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamHeroKeys, EveryBuiltInSeamFitsTheEngineAtOnce) {
  // With every seam on there are more points than thirty-two, which was
  // once the engine's limit and refused the last seam a player switched
  // on. The faces are alternatives, so only one of them is counted.
  const rig r;
  for (const seam_definition& seam : all_seams()) {
    EXPECT_EQ(r.box->seams().enable(seam.id), seam_reason::none) << seam.id;
  }
}

TEST(SeamHeroKeys, DoesNothingWhileItIsOff) {
  const rig r;
  r.manager_says(word_menu, menu_segment);
  r.manager_says(word_camp, camp_segment);
  r.lay_party(4);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), number_row(3));
  EXPECT_EQ(r.selected(4), 0u);
}

// --- Keys: landing ----------------------------------------------------------

TEST(SeamHeroKeys, LandsOnTheFirstAMiddleAndTheLastMemberOfAnyParty) {
  const rig r;
  r.arm();
  for (unsigned count = 1; count <= 8; ++count) {
    for (unsigned from = 0; from < count; ++from) {
      for (unsigned hero = 1; hero <= count; ++hero) {
        r.lay_party(count, from);
        const std::uint16_t answer = r.press(number_row(hero), hero_callers[3]);

        // The caller is handed Home, which it reads as `G` and gives the
        // cursor, which steps back from wherever the seam left the
        // selection. Where that lands is the rule's, restated.
        EXPECT_EQ(answer, home) << count << " " << from << " " << hero;
        const unsigned left_at = r.selected(count);
        ASSERT_NE(left_at, ~0U);
        EXPECT_EQ(cursor_goes_back(count, left_at), hero - 1U)
            << "party of " << count << ", from " << from << ", hero " << hero;
      }
    }
  }
}

TEST(SeamHeroKeys, ALoneMemberIsThePartyAndTheSelectionIsTheHead) {
  const rig r;
  r.arm();
  r.lay_party(1);
  EXPECT_EQ(r.press(number_row(1), hero_callers[0]), home);
  EXPECT_EQ(r.selected(1), 0u) << "from the head the cursor goes to the tail";
  r.lay_party(1);
  EXPECT_EQ(r.press(number_row(2), hero_callers[0]), ignored)
      << "there is no second member";
  EXPECT_EQ(r.selected(1), 0u);
}

TEST(SeamHeroKeys, ADigitWithNoMemberBehindItDoesNothingButBeIgnored) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);
  for (unsigned digit = 5; digit <= 8; ++digit) {
    EXPECT_EQ(r.press(number_row(digit), hero_callers[2]), ignored) << digit;
    EXPECT_EQ(r.selected(4), 2u) << "the selection is not moved";
  }
  r.lay_party(0);
  EXPECT_EQ(r.press(number_row(1), hero_callers[2]), ignored)
      << "an empty party has no member";
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamHeroKeys, WritesTheSelectionAndTheHeadWordAndNothingElse) {
  const rig r;
  r.arm();
  r.lay_party(5, 4);
  r.called_from(hero_callers[3].segment, hero_callers[3].offset);
  r.ring(number_row(3));
  r.put_word(0x40, ring_first + 2, 0xBEEF);
  r.arrive_at_key();

  EXPECT_EQ(r.word(0x40, ring_first + 2), 0xBEEF) << "the key behind it";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first) << "the head did not move";
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 2) << "nor the tail";
  EXPECT_EQ(r.word(rig::dgroup(), data_head), rig::member_offset(0))
      << "the party's head is the program's";

  const cpu::registers& regs = r.box->processor().regs();
  EXPECT_EQ(regs[cpu::reg16::bp], frame_base);
  EXPECT_EQ(regs[cpu::reg16::sp], 0x0400);
}

// --- Keys: which keys and which callers -------------------------------------

TEST(SeamHeroKeys, TakesTheNumberRowAtEveryCallerInTheTable) {
  const rig r;
  r.arm();
  for (const caller& c : hero_callers) {
    for (unsigned hero = 1; hero <= 8; ++hero) {
      r.lay_party(8);
      EXPECT_EQ(r.press(number_row(hero), c), home) << c.name << " " << hero;
    }
  }
}

TEST(SeamHeroKeys, LeavesNineZeroAndTheKeypadAlone) {
  const rig r;
  r.arm();
  r.lay_party(8, 3);
  for (const caller& c : hero_callers) {
    EXPECT_EQ(r.press(number_row(9), c), number_row(9)) << c.name;
    EXPECT_EQ(r.press(number_row(0), c), number_row(0)) << c.name;
    for (unsigned digit = 0; digit <= 9; ++digit) {
      EXPECT_EQ(r.press(keypad(digit), c), keypad(digit))
          << c.name << " keypad " << digit;
    }
    EXPECT_EQ(r.selected(8), 3u) << "nothing was selected";
  }
}

TEST(SeamHeroKeys, ANumberRowCharacterUnderAnotherScanCodeIsNotTheNumberRow) {
  const rig r;
  r.arm();
  r.lay_party(8, 3);
  // The keypad's 8 with Num Lock on is scan 0x48 and character '8'; a
  // character '3' under scan 0x02 is a '1' key shifted into something.
  EXPECT_EQ(r.press(0x4838, hero_callers[0]), 0x4838);
  EXPECT_EQ(r.press(0x0233, hero_callers[0]), 0x0233);
  EXPECT_EQ(r.press(0x0A38, hero_callers[0]), 0x0A38);
  EXPECT_EQ(r.selected(8), 3u);
}

TEST(SeamHeroKeys, LeavesEveryOtherCallerItsDigits) {
  const rig r;
  r.arm();
  r.lay_party(8, 3);
  for (const caller& c : other_callers) {
    EXPECT_EQ(r.press(number_row(3), c), number_row(3)) << c.name;
    EXPECT_EQ(r.press(number_row(8), c), number_row(8)) << c.name;
  }
  EXPECT_EQ(r.selected(8), 3u);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamHeroKeys, TheCallerIsAsExactAsItsModuleAndOffset) {
  const rig r;
  r.arm();
  r.lay_party(8, 3);
  // A listed offset in the wrong module, a listed module at an offset the
  // table does not name, and a return segment that is not the module's.
  EXPECT_EQ(r.press(number_row(2),
                    {.segment = camp_segment, .offset = 0x09D5, .name = ""}),
            number_row(2));
  EXPECT_EQ(
      r.press(number_row(2),
              {.segment = adventure_segment, .offset = 0x1F24, .name = ""}),
      number_row(2));
  EXPECT_EQ(r.press(number_row(2),
                    {.segment = stack_segment, .offset = 0x1F24, .name = ""}),
            number_row(2));
  // The module's word says it is out: it is not a caller.
  r.manager_says(word_camp, 0);
  EXPECT_EQ(
      r.press(number_row(2), {.segment = 0, .offset = 0x1F24, .name = ""}),
      number_row(2));
  r.manager_says(word_camp, camp_segment);
  EXPECT_EQ(r.press(number_row(2), hero_callers[3]), home);
}

TEST(SeamHeroKeys, EveryOtherKeyIsLeftWhereItIs) {
  const rig r;
  r.arm();
  r.lay_party(8, 3);
  for (const std::uint16_t key :
       {std::uint16_t{0x4800}, std::uint16_t{0x5000}, std::uint16_t{0x4700},
        std::uint16_t{0x4F00}, std::uint16_t{0x4B00}, std::uint16_t{0x4D00},
        std::uint16_t{0x1E41}, std::uint16_t{0x011B}, std::uint16_t{0x3920},
        std::uint16_t{0x1C0D}, std::uint16_t{0x0231 + 0x0100}}) {
    EXPECT_EQ(r.press(key, hero_callers[3]), key) << key;
  }
  EXPECT_EQ(r.selected(8), 3u);
}

TEST(SeamHeroKeys, LeavesTheRingAloneWhenItIsEmpty) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);
  r.called_from(camp_segment, 0x1F24);
  r.put_word(0x40, 0x1A, ring_first);
  r.put_word(0x40, 0x1C, ring_first);
  r.put_word(0x40, ring_first, number_row(3));
  r.arrive_at_key();

  EXPECT_EQ(r.word(0x40, ring_first), number_row(3));
  EXPECT_EQ(r.selected(4), 2u);
}

TEST(SeamHeroKeys, LeavesTheKeyAloneWhileThePushbackSlotIsArmed) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);
  r.put_byte(rig::dgroup(), data_pushback, 0x4B);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), number_row(3));
  EXPECT_EQ(r.selected(4), 2u);
}

TEST(SeamHeroKeys, LeavesTheKeyAloneWhileTheJournalReaderIsOpen) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);

  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), number_row(3));
  EXPECT_EQ(r.selected(4), 2u);

  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), home);
}

TEST(SeamHeroKeys, StepsAsideWhileTheMapHasTheRostersCellsOnThePartysBar) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);

  // The map takes the keys that step the party cursor, and would take the
  // Home this seam rewrites a digit to, with the selection half moved.
  automap_state& map = r.box->automap();
  map.set_panel_open(true);
  map.set_at_command_bar(true);
  EXPECT_EQ(r.press(number_row(3), hero_callers[0]), number_row(3));
  EXPECT_EQ(r.selected(4), 2u);

  // Something else has the cells, or the bar is not the party's: the map
  // takes nothing, and a digit selects.
  map.set_panel_covered(true);
  EXPECT_EQ(r.press(number_row(3), hero_callers[0]), home);
  map.set_panel_covered(false);
  map.set_at_command_bar(false);
  r.lay_party(4, 2);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), home);
  map.set_at_command_bar(true);
  map.set_panel_open(false);
  r.lay_party(4, 2);
  EXPECT_EQ(r.press(number_row(3), hero_callers[0]), home);
}

TEST(SeamHeroKeys, IsInertWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.manager_says(word_menu, 0);
  r.lay_party(4, 2);

  EXPECT_EQ(r.box->seams().status(seam_id).reason,
            seam_reason::module_not_resident);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), number_row(3));
}

// --- Keys: what it refuses --------------------------------------------------

TEST(SeamHeroKeys, DeclinesADataSegmentThatIsNotTheOneTheFactsName) {
  const rig r;
  r.arm();
  r.lay_party(4, 2);
  r.called_from(camp_segment, 0x1F24);
  r.ring(number_row(3));
  r.arrive_at_key(stack_segment);
  EXPECT_EQ(r.head_word(), number_row(3));
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
}

TEST(SeamHeroKeys, DeclinesAPartyItCannotRead) {
  const rig r;
  r.arm();

  // A ninth member.
  r.lay_party(8, 2);
  r.put_word(party_segment,
             static_cast<std::uint16_t>(rig::member_offset(7) + record_next),
             rig::member_offset(8));
  r.put_word(
      party_segment,
      static_cast<std::uint16_t>(rig::member_offset(7) + record_next + 2),
      party_segment);
  EXPECT_EQ(r.press(number_row(3), hero_callers[3]), number_row(3));
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);

  // A link that points above conventional memory.
  r.lay_party(3, 2);
  r.put_word(
      party_segment,
      static_cast<std::uint16_t>(rig::member_offset(1) + record_next + 2),
      0xB800);
  EXPECT_EQ(r.press(number_row(1), hero_callers[3]), number_row(1));
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
  EXPECT_EQ(r.selected(3), 2u) << "nothing was written";
}

// --- The roster drawer ------------------------------------------------------

TEST(SeamHeroKeys, TheNumberIsDrawnBeforeTheNameMovesRight) {
  const rig r;
  r.arm();
  r.lay_party(4);
  for (unsigned member = 0; member < 4; ++member) {
    for (const std::uint8_t column : {std::uint8_t{0x11}, std::uint8_t{0x01}}) {
      const auto row = static_cast<std::uint8_t>(4 + member);
      r.lay_row(column, row, member, 6);
      r.arrive_at_roster(row_cleared);

      EXPECT_EQ(r.column(), column + 2) << "the name moves right";
      const pending_call call = read_call(r, 6);
      // The glyph blitter, at the paragraph the program linked it at; the
      // far return is the point itself.
      EXPECT_EQ(call.paragraph, glyph_paragraph);
      EXPECT_EQ(call.offset, glyph_offset);
      EXPECT_EQ(call.cs, image_load_segment);
      EXPECT_EQ(call.ip, row_cleared);
      // Column, row, white, a count of one, the character, the fold.
      const std::vector<std::uint16_t> expect{
          column, row, 0x000F, 1, static_cast<std::uint16_t>('1' + member), 1};
      EXPECT_EQ(call.args, expect);
    }
  }
}

TEST(SeamHeroKeys, ReachedAgainAfterTheNumberTheProgramGoesOn) {
  const rig r;
  r.arm();
  r.lay_party(4);
  r.lay_row(0x11, 5, 1, 6);
  r.arrive_at_roster(row_cleared);
  return_from(r, 6);
  const std::uint16_t sp = r.box->processor().regs()[cpu::reg16::sp];
  EXPECT_EQ(sp, drawer_sp) << "the stack is the program's again";

  r.arrive_again(row_cleared);
  EXPECT_EQ(r.column(), 0x13) << "the name is not moved a second time";
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::sp], drawer_sp);
  EXPECT_EQ(r.box->processor().regs().ip, row_cleared + 1U)
      << "the program's own instruction ran: no second number";
}

/// One member's roster row from the cleared row to the column put back,
/// the way the program would run it: every routine the handlers ask for is
/// let to return. Answers the routines called, in order, and the column at
/// the end.
struct row_run {
  std::vector<pending_call> calls;
  std::uint8_t final_column{};
  std::uint16_t final_sp{};
};

[[nodiscard]] row_run run_a_row(const rig& r, std::uint8_t column,
                                std::uint8_t row, std::uint8_t length) {
  r.lay_party(8);
  r.lay_row(column, row, row - 4U, length);
  row_run out;

  r.arrive_at_roster(row_cleared);
  out.calls.push_back(read_call(r, 6));
  return_from(r, 6);
  r.arrive_again(row_cleared);

  // The program draws the name; the next thing it reaches is the join.
  r.arrive_again(name_drawn);
  if (r.box->processor().regs()[cpu::sreg::cs] != image_load_segment ||
      r.box->processor().regs().ip != name_drawn + 1U) {
    out.calls.push_back(read_call(r, 4));
    return_from(r, 4);
    r.arrive_again(name_drawn);
  }
  out.final_column = r.column();
  out.final_sp = r.box->processor().regs()[cpu::reg16::sp];
  return out;
}

TEST(SeamHeroKeys, ANameThatFitsIsMovedAndNeverCutAndTheColumnComesBack) {
  const rig r;
  r.arm();
  // Thirteen characters from column 0x13 end at 0x1F, the last column the
  // name had before.
  for (const std::uint8_t length :
       {std::uint8_t{1}, std::uint8_t{6}, std::uint8_t{13}}) {
    const row_run run = run_a_row(r, 0x11, 6, length);
    EXPECT_EQ(run.calls.size(), 1u) << int{length};
    EXPECT_EQ(run.final_column, 0x11) << int{length};
    EXPECT_EQ(run.final_sp, drawer_sp) << int{length};
  }
}

TEST(SeamHeroKeys, ALongNameKeepsThirteenCharactersBesideTheViewport) {
  const rig r;
  r.arm();
  // Fourteen characters end at 0x20, fifteen at 0x21: the tail from 0x20 is
  // cleared, up to where the name would have ended.
  const row_run fourteen = run_a_row(r, 0x11, 5, 14);
  ASSERT_EQ(fourteen.calls.size(), 2u);
  EXPECT_EQ(fourteen.calls[1].paragraph, clear_paragraph);
  EXPECT_EQ(fourteen.calls[1].offset, clear_offset);
  // Left, top, right, bottom, as the drawer pushes them.
  EXPECT_EQ(fourteen.calls[1].args,
            (std::vector<std::uint16_t>{0x20, 5, 0x20, 5}));
  EXPECT_EQ(fourteen.calls[1].ip, name_drawn);
  EXPECT_EQ(fourteen.final_column, 0x11);
  EXPECT_EQ(fourteen.final_sp, drawer_sp);

  const row_run fifteen = run_a_row(r, 0x11, 7, 15);
  ASSERT_EQ(fifteen.calls.size(), 2u);
  EXPECT_EQ(fifteen.calls[1].args,
            (std::vector<std::uint16_t>{0x20, 7, 0x21, 7}));
  EXPECT_EQ(fifteen.final_column, 0x11);
  EXPECT_EQ(fifteen.final_sp, drawer_sp);
}

TEST(SeamHeroKeys, TheMainMenusNamesAreNeverCut) {
  const rig r;
  r.arm();
  for (const std::uint8_t length : {std::uint8_t{6}, std::uint8_t{15}}) {
    const row_run run = run_a_row(r, 0x01, 4, length);
    EXPECT_EQ(run.calls.size(), 1u) << int{length};
    EXPECT_EQ(run.final_column, 0x01);
    EXPECT_EQ(run.final_sp, drawer_sp);
  }
}

TEST(SeamHeroKeys, ANameOfNoCharactersIsNotCut) {
  const rig r;
  r.arm();
  const row_run run = run_a_row(r, 0x11, 4, 0);
  EXPECT_EQ(run.calls.size(), 1u);
  EXPECT_EQ(run.final_column, 0x11);
}

TEST(SeamHeroKeys, TheJoinLeavesAColumnItDidNotMoveAlone) {
  // Reached with the column where the program has it: the first point
  // declined, or the name was never moved.
  const rig r;
  r.arm();
  r.lay_party(4);
  r.lay_row(0x11, 4, 0, 15);
  r.arrive_at_roster(name_drawn);
  EXPECT_EQ(r.column(), 0x11);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::sp], drawer_sp);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamHeroKeys, DeclinesARosterFrameTheFactsDoNotDescribe) {
  const rig r;
  r.arm();
  r.lay_party(4);

  // A row above the first member's and below the last possible, a column
  // the drawer never uses, and a member pointer above conventional memory.
  r.lay_row(0x11, 3, 0, 6);
  r.arrive_at_roster(row_cleared);
  EXPECT_EQ(r.column(), 0x11);
  r.lay_row(0x11, 12, 0, 6);
  r.arrive_at_roster(row_cleared);
  EXPECT_EQ(r.column(), 0x11);
  r.lay_row(0x05, 5, 0, 6);
  r.arrive_at_roster(row_cleared);
  EXPECT_EQ(r.column(), 0x05);
  r.lay_row(0x11, 5, 0, 6);
  r.put_word(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_member + 2), 0xB800);
  r.arrive_at_roster(row_cleared);
  EXPECT_EQ(r.column(), 0x11);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 4u);
}

TEST(SeamHeroKeys, DrawsTheNumbersOneToEightForTheRowsFourToEleven) {
  const rig r;
  r.arm();
  r.lay_party(8);
  for (std::uint8_t row = 4; row <= 11; ++row) {
    r.lay_row(0x11, row, row - 4U, 6);
    r.arrive_at_roster(row_cleared);
    const pending_call call = read_call(r, 6);
    EXPECT_EQ(call.args[4], static_cast<std::uint16_t>('1' + row - 4))
        << int{row};
    EXPECT_EQ(call.args[1], row);
  }
}

}  // namespace
}  // namespace amberfolio::machine
