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
#include <initializer_list>
#include <memory>
#include <optional>
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
/// The push of the armour class value's column, and the heading drawn
/// above the list once its string has been copied into the frame.
constexpr std::uint16_t ac_column_pushed = 0x140F;
constexpr std::uint16_t heading_copied = 0x135A;

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
constexpr std::uint32_t word_slots = 0x7D0;

constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t temple_segment = 0x6200;
constexpr std::uint16_t post_combat_segment = 0x6400;
constexpr std::uint16_t shop_segment = 0x6600;
constexpr std::uint16_t script_segment = 0x6800;
constexpr std::uint16_t combat_segment = 0x6A00;
constexpr std::uint16_t adventure_segment = 0x6C00;
constexpr std::uint16_t camp_segment = 0x6E00;
constexpr std::uint16_t main_menu_segment = 0x7000;
constexpr std::uint16_t slots_segment = 0x7200;

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
/// The heading's copy in the same frame.
constexpr std::uint16_t local_heading = 0x0E;
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
    manager_says(word_slots, slots_segment);
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

  /// The drawer's frame as the heading finds it: on row two, with a copy
  /// of the heading's shape (stand-in letters, not the program's text).
  void lay_heading(std::uint8_t column) const {
    put_byte(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_column), column);
    put_byte(stack_segment, static_cast<std::uint16_t>(drawer_bp - local_row),
             2);
    const std::array<std::uint8_t, 7> text{6, 'Q', 'R', ' ', ' ', 'S', 'T'};
    for (std::size_t i = 0; i < text.size(); ++i) {
      put_byte(stack_segment,
               static_cast<std::uint16_t>(drawer_bp - local_heading + i),
               text[i]);
    }
  }

  [[nodiscard]] std::vector<std::uint8_t> heading() const {
    constexpr std::uint16_t length_and_text = 7;
    std::vector<std::uint8_t> out;
    out.reserve(length_and_text);
    for (std::uint16_t i = 0; i < length_and_text; ++i) {
      out.push_back(byte(stack_segment, static_cast<std::uint16_t>(
                                            drawer_bp - local_heading + i)));
    }
    return out;
  }

  [[nodiscard]] std::uint8_t column() const {
    return byte(stack_segment,
                static_cast<std::uint16_t>(drawer_bp - local_column));
  }

  /// Stand on a roster point with the stack where it was, and step.
  void arrive_at_roster(std::uint16_t point, std::uint16_t ax = 0) const {
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
    r[cpu::reg16::ax] = ax;
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

TEST(SeamHeroKeys, IsAKeyPointInOverlay25AndFourInsideCallsPointsInTheRoster) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the keys work, nothing to pull";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 5u);

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
  EXPECT_EQ(s.points[3].offset, ac_column_pushed);
  EXPECT_EQ(s.points[4].offset, heading_copied);
  for (std::size_t i = 1; i < 5; ++i) {
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

// --- With list-arrows on at the same point ---------------------------------

/// `list-arrows` has a point at the same instruction and takes the keypad's 8
/// and 2 at the callers whose Home and End step the party cursor (#447); this
/// seam takes the number row's digits. They are told apart by the scan code,
/// so they do not fight, whichever of the two the engine runs first.
class SeamHeroKeysWithListArrows : public testing::TestWithParam<bool> {};

TEST_P(SeamHeroKeysWithListArrows, EachTakesItsOwnDigitsAtTheSharedPoint) {
  const rig r;
  const bool arrows_first = GetParam();
  if (arrows_first) {
    ASSERT_EQ(r.box->seams().enable("list-arrows"), seam_reason::none);
  }
  r.arm();
  if (!arrows_first) {
    ASSERT_EQ(r.box->seams().enable("list-arrows"), seam_reason::none);
  }

  constexpr std::uint16_t pad_8 = 0x4838;
  constexpr std::uint16_t pad_2 = 0x5032;
  constexpr std::uint16_t pad_7 = 0x4737;
  constexpr std::uint16_t pad_1 = 0x4F31;

  // The camp bar is a caller of both.
  const caller& camp = hero_callers[3];

  // The number row's 8 selects the eighth member through this seam's own
  // Home, and nothing else touches it.
  r.lay_party(8, 2);
  EXPECT_EQ(r.press(number_row(8), camp), home);
  EXPECT_EQ(cursor_goes_back(8, r.selected(8)), 7U);

  // The keypad's 8 and 2 are `list-arrows`': the selection is not moved
  // here, and the program's own table turns the 7 and 1 into Home and End.
  r.lay_party(8, 2);
  EXPECT_EQ(r.press(pad_8, camp), pad_7);
  EXPECT_EQ(r.press(pad_2, camp), pad_1);
  EXPECT_EQ(r.selected(8), 2U);

  // The number row's 2 selects the second member.
  r.lay_party(8, 5);
  EXPECT_EQ(r.press(number_row(2), camp), home);
  EXPECT_EQ(cursor_goes_back(8, r.selected(8)), 1U);

  // Where this seam is a caller and `list-arrows` is not (the adventuring
  // bars and the main menu), the keypad's 8 and 2 are the program's own.
  for (const caller* c :
       {&hero_callers[0], &hero_callers[1], &hero_callers[2]}) {
    r.lay_party(8, 2);
    EXPECT_EQ(r.press(pad_8, *c), pad_8) << c->name;
    EXPECT_EQ(r.press(pad_2, *c), pad_2) << c->name;
    EXPECT_EQ(r.press(number_row(8), *c), home) << c->name;
  }

  // And at a caller `list-arrows` has and this seam does not (the
  // party-order screen), the keypad is rewritten and the row's digits are
  // the program's.
  const caller& order = other_callers[0];
  r.lay_party(8, 2);
  EXPECT_EQ(r.press(pad_8, order), pad_7);
  EXPECT_EQ(r.press(number_row(8), order), number_row(8));
  EXPECT_EQ(r.selected(8), 2U);
}

INSTANTIATE_TEST_SUITE_P(EitherOrder, SeamHeroKeysWithListArrows,
                         testing::Bool());

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
  // Fourteen characters from column 0x13 end at 0x20, the last column a
  // name may reach.
  for (const std::uint8_t length :
       {std::uint8_t{1}, std::uint8_t{6}, std::uint8_t{13}, std::uint8_t{14}}) {
    const row_run run = run_a_row(r, 0x11, 6, length);
    EXPECT_EQ(run.calls.size(), 1u) << int{length};
    EXPECT_EQ(run.final_column, 0x11) << int{length};
    EXPECT_EQ(run.final_sp, drawer_sp) << int{length};
  }
}

TEST(SeamHeroKeys, ALongNameKeepsFourteenCharactersBesideTheViewport) {
  const rig r;
  r.arm();
  // Fifteen characters end at 0x21: the tail from 0x21 is cleared, up to
  // where the name would have ended.
  const row_run fifteen = run_a_row(r, 0x11, 7, 15);
  ASSERT_EQ(fifteen.calls.size(), 2u);
  EXPECT_EQ(fifteen.calls[1].paragraph, clear_paragraph);
  EXPECT_EQ(fifteen.calls[1].offset, clear_offset);
  // Left, top, right, bottom, as the drawer pushes them.
  EXPECT_EQ(fifteen.calls[1].args,
            (std::vector<std::uint16_t>{0x21, 7, 0x21, 7}));
  EXPECT_EQ(fifteen.calls[1].ip, name_drawn);
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

// --- The armour class and its heading, one column right ---------------------

TEST(SeamHeroKeys, TheArmourClassColumnMovesOneRightAtEveryWidth) {
  const rig r;
  r.arm();
  r.lay_party(4);
  // The drawer computes 0x20 for a three-character value (-10 or lower),
  // 0x21 for two and 0x22 for one, so the value ends in 0x23 at most and
  // the hit points' 0x24 is kept.
  for (const std::uint8_t column : {std::uint8_t{0x11}, std::uint8_t{0x01}}) {
    for (std::uint16_t ax = 0x20; ax <= 0x22; ++ax) {
      r.lay_row(column, 5, 1, 6);
      r.arrive_at_roster(ac_column_pushed, ax);
      EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], ax + 1U)
          << int{column} << " " << ax;
      EXPECT_EQ(r.column(), column) << "the name's column is not touched";
    }
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamHeroKeys, TheArmourClassColumnIsLeftAloneOffTheFacts) {
  const rig r;
  r.arm();
  r.lay_party(4);
  // A column the drawer does not compute, a row it is not on, and a name
  // column the program is not at between rows.
  r.lay_row(0x11, 5, 1, 6);
  r.arrive_at_roster(ac_column_pushed, 0x1F);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x1Fu);
  r.arrive_at_roster(ac_column_pushed, 0x23);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x23u);
  r.lay_row(0x11, 3, 1, 6);
  r.arrive_at_roster(ac_column_pushed, 0x21);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x21u);
  r.lay_row(0x13, 5, 1, 6);
  r.arrive_at_roster(ac_column_pushed, 0x21);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x21u);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 4u);
}

TEST(SeamHeroKeys, DoesNotMoveTheArmourClassWhileItIsOff) {
  const rig r;
  r.lay_party(4);
  r.lay_row(0x11, 5, 1, 6);
  r.arrive_at_roster(ac_column_pushed, 0x21);
  EXPECT_EQ(r.box->processor().regs()[cpu::reg16::ax], 0x21u);
}

TEST(SeamHeroKeys, TheHeadingsArmourClassMovesAndTheHitPointsStay) {
  const rig r;
  r.arm();
  for (const std::uint8_t column : {std::uint8_t{0x11}, std::uint8_t{0x01}}) {
    r.lay_heading(column);
    r.arrive_at_roster(heading_copied);
    // The same six characters from the same column: the armour class one
    // column on from where it was drawn, and the hit points where they were.
    EXPECT_EQ(r.heading(),
              (std::vector<std::uint8_t>{6, ' ', 'Q', 'R', ' ', 'S', 'T'}))
        << int{column};
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamHeroKeys, TheHeadingIsLeftAloneWhenItIsNotTheOneTheFactsDescribe) {
  const rig r;
  r.arm();
  // A string that is not the heading's shape, one already moved, and a
  // frame whose row is not the heading's.
  r.lay_heading(0x11);
  r.put_byte(stack_segment,
             static_cast<std::uint16_t>(drawer_bp - local_heading + 3), 'X');
  r.arrive_at_roster(heading_copied);
  EXPECT_EQ(r.heading(),
            (std::vector<std::uint8_t>{6, 'Q', 'R', 'X', ' ', 'S', 'T'}));
  r.lay_heading(0x11);
  r.arrive_at_roster(heading_copied);
  r.arrive_at_roster(heading_copied);
  EXPECT_EQ(r.heading(),
            (std::vector<std::uint8_t>{6, ' ', 'Q', 'R', ' ', 'S', 'T'}))
      << "moved once, and never a second time";
  r.lay_heading(0x11);
  r.put_byte(stack_segment, static_cast<std::uint16_t>(drawer_bp - local_row),
             4);
  r.arrive_at_roster(heading_copied);
  EXPECT_EQ(r.heading(),
            (std::vector<std::uint8_t>{6, 'Q', 'R', ' ', ' ', 'S', 'T'}));
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 3u);
}

TEST(SeamHeroKeys, DoesNotMoveTheHeadingWhileItIsOff) {
  const rig r;
  r.lay_heading(0x11);
  r.arrive_at_roster(heading_copied);
  EXPECT_EQ(r.heading(),
            (std::vector<std::uint8_t>{6, 'Q', 'R', ' ', ' ', 'S', 'T'}));
}

// --- At a bar that is not raw (#469) ----------------------------------------

/// Callers of the menu-bar routine with raw mode off whose screen shows the
/// party list: a locked door's bar, a stuck door's, and camp's Portraits and
/// Monsters bar. The routine throws Home, End and the arrows away for them,
/// so there is no caller to hand a Home to: the seams write the selection
/// and ask the program to draw the list again.
constexpr std::array<caller, 4> bar_callers{{
    {.segment = adventure_segment, .offset = 0x0EBF, .name = "a locked door"},
    {.segment = adventure_segment, .offset = 0x0FFE, .name = "a stuck door"},
    {.segment = camp_segment,
     .offset = 0x1DDF,
     .name = "camp's Portraits and Monsters bar"},
    {.segment = slots_segment, .offset = 0x1DA1, .name = "the save slot bar"},
}};

/// Above BP: the raw-mode argument. Below it: the bar as a Pascal string.
constexpr std::uint16_t frame_raw_mode = 0x0C;
constexpr std::uint16_t bar_below_bp = 0x53;

/// The roster drawer, as the paragraph it was linked at and the offset in it.
constexpr std::uint16_t drawer_paragraph = 0x0BA;
constexpr std::uint16_t drawer_offset = 0x0767;

constexpr std::uint16_t key_up = 0x4800;
constexpr std::uint16_t key_down = 0x5000;

/// The cursor's step, restated: back from the head wraps to the tail, forward
/// from the tail stays.
[[nodiscard]] unsigned cursor_steps(unsigned size, unsigned current,
                                    bool forward) {
  if (forward) {
    return current + 1U == size ? current : current + 1U;
  }
  return cursor_goes_back(size, current);
}

void set_bar(const rig& r, std::string_view bar) {
  const auto at = static_cast<std::uint16_t>(frame_base - bar_below_bp);
  r.put_byte(stack_segment, at, static_cast<std::uint8_t>(bar.size()));
  for (std::size_t i = 0; i < bar.size(); ++i) {
    r.put_byte(stack_segment, static_cast<std::uint16_t>(at + 1U + i),
               static_cast<std::uint8_t>(bar[i]));
  }
}

void set_raw_mode(const rig& r, std::uint8_t raw) {
  r.put_byte(stack_segment,
             static_cast<std::uint16_t>(frame_base + frame_raw_mode), raw);
}

/// The member the roster drawer was last asked to draw the list for, if the
/// last press asked it.
std::optional<unsigned> last_drew;

[[nodiscard]] bool drew_for(unsigned member) {
  return last_drew.has_value() && *last_drew == member;
}

/// The program's roster drawer, as a stand-in that keeps what it was handed:
/// how many times it was called and the far pointer of the last call, in the
/// data segment, and then returns and cleans its one argument. A far pointer
/// is pushed segment first, so the offset is the nearer word.
///
///     push bp / mov bp, sp
///     mov ax, [bp+6] / mov [log_offset], ax
///     mov ax, [bp+8] / mov [log_segment], ax
///     inc word [log_calls]
///     pop bp / retf 4
constexpr std::uint16_t log_offset = 0x7000;
constexpr std::uint16_t log_segment = 0x7002;
constexpr std::uint16_t log_calls = 0x7004;

void install_the_drawer(const rig& r) {
  const auto where =
      static_cast<std::uint16_t>(image_load_segment + drawer_paragraph);
  std::uint16_t put = drawer_offset;
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    for (const std::uint8_t b : bytes) {
      r.put_byte(where, put++, b);
    }
  };
  emit({0x55, 0x89, 0xE5});
  emit({0x8B, 0x46, 0x06, 0xA3, static_cast<std::uint8_t>(log_offset & 0xFFU),
        static_cast<std::uint8_t>(log_offset >> 8U)});
  emit({0x8B, 0x46, 0x08, 0xA3, static_cast<std::uint8_t>(log_segment & 0xFFU),
        static_cast<std::uint8_t>(log_segment >> 8U)});
  emit({0xFF, 0x06, static_cast<std::uint8_t>(log_calls & 0xFFU),
        static_cast<std::uint8_t>(log_calls >> 8U)});
  emit({0x5D, 0xCA, 0x04, 0x00});
}

/// One press at a bar, and then the batch the handler may have queued run to
/// its end: what the roster drawer was asked to draw for is noted in
/// `last_drew`, and the key at the head of the ring is returned.
[[nodiscard]] std::uint16_t press_bar(const rig& r, std::uint16_t key,
                                      const caller& from) {
  last_drew.reset();
  r.put_word(rig::dgroup(), log_calls, 0);
  const std::uint16_t head = r.press(key, from);
  cpu::processor& cpu = r.box->processor();
  for (unsigned nth = 0; nth < 256 && !cpu.halted(); ++nth) {
    r.box->step();
  }
  EXPECT_TRUE(cpu.halted()) << "the batch ended where it began";
  const std::uint16_t calls = r.word(rig::dgroup(), log_calls);
  EXPECT_LE(calls, 1u) << "the list is drawn once";
  if (calls == 1) {
    EXPECT_EQ(r.word(rig::dgroup(), log_segment), party_segment);
    for (unsigned i = 0; i < 8; ++i) {
      if (r.word(rig::dgroup(), log_offset) == rig::member_offset(i)) {
        last_drew = i;
      }
    }
  }
  return head;
}

class SeamHeroKeysAtABarThatIsNotRaw : public testing::Test {
 protected:
  void SetUp() override {
    r.arm();
    install_the_drawer(r);
    ASSERT_EQ(r.box->seams().enable("list-arrows"), seam_reason::none);
    set_bar(r, " Alfa Beta Echo");
    set_raw_mode(r, 0);
  }

  const rig r;
};

TEST_F(SeamHeroKeysAtABarThatIsNotRaw,
       ADigitSelectsTheMemberAndTheListIsDrawn) {
  for (const caller& c : bar_callers) {
    for (unsigned count = 1; count <= 8; ++count) {
      for (unsigned hero = 1; hero <= count; ++hero) {
        r.lay_party(count, count - 1U);
        EXPECT_EQ(press_bar(r, number_row(hero), c), ignored) << c.name;
        EXPECT_EQ(r.selected(count), hero - 1U) << c.name << " " << hero;
        EXPECT_TRUE(drew_for(hero - 1U)) << c.name << " " << hero;
      }
    }
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw,
       UpAndDownStepTheSelectionByTheCursorsRule) {
  for (const caller& c : bar_callers) {
    for (unsigned count = 1; count <= 8; ++count) {
      for (unsigned from = 0; from < count; ++from) {
        for (const bool forward : {false, true}) {
          r.lay_party(count, from);
          const unsigned to = cursor_steps(count, from, forward);
          EXPECT_EQ(press_bar(r, forward ? key_down : key_up, c), ignored)
              << c.name << " " << count << " " << from;
          EXPECT_EQ(r.selected(count), to)
              << c.name << " " << count << " " << from
              << (forward ? " down" : " up");
          if (to != from) {
            EXPECT_TRUE(drew_for(to));
          } else {
            EXPECT_FALSE(last_drew.has_value())
                << "a step that goes nowhere draws nothing";
          }
        }
      }
    }
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw, ADigitWithNobodyBehindItIsOnlyIgnored) {
  r.lay_party(3, 1);
  for (unsigned digit = 4; digit <= 8; ++digit) {
    EXPECT_EQ(press_bar(r, number_row(digit), bar_callers[0]), ignored);
    EXPECT_EQ(r.selected(3), 1u);
    EXPECT_FALSE(last_drew.has_value());
  }
  r.lay_party(0);
  EXPECT_EQ(press_bar(r, number_row(1), bar_callers[0]), ignored);
  EXPECT_EQ(press_bar(r, key_down, bar_callers[0]), ignored);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw,
       ADigitThatIsOneOfTheBarsCommandsStaysTheBars) {
  r.lay_party(4, 0);
  set_bar(r, " 1 Alfa 2 Beta");
  EXPECT_EQ(press_bar(r, number_row(1), bar_callers[0]), number_row(1));
  EXPECT_EQ(press_bar(r, number_row(2), bar_callers[0]), number_row(2));
  EXPECT_EQ(r.selected(4), 0u);
  EXPECT_FALSE(last_drew.has_value());
  EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), ignored)
      << "a digit the bar does not use is still the party's";
  EXPECT_EQ(r.selected(4), 2u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw, LeavesTheKeysItDoesNotTake) {
  r.lay_party(4, 1);
  for (const std::uint16_t key :
       {number_row(9), number_row(0), keypad(3), keypad(8), keypad(2),
        std::uint16_t{0x4B00}, std::uint16_t{0x4D00}, home,
        std::uint16_t{0x4F00}, std::uint16_t{0x1C0D}, std::uint16_t{0x011B},
        std::uint16_t{0x1E41}}) {
    EXPECT_EQ(press_bar(r, key, bar_callers[0]), key);
    EXPECT_EQ(r.selected(4), 1u);
    EXPECT_FALSE(last_drew.has_value());
  }
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw, LeavesACallerThatIsNotInTheTable) {
  r.lay_party(4, 1);
  for (const caller& c : other_callers) {
    EXPECT_EQ(press_bar(r, number_row(3), c), number_row(3)) << c.name;
    if (c.offset != 0x17DA) {  // the party-order screen is list-arrows' own
      EXPECT_EQ(press_bar(r, key_down, c), key_down) << c.name;
    }
  }
  // The same offset in the wrong module is not the door's.
  EXPECT_EQ(press_bar(r, number_row(3),
                      {.segment = camp_segment, .offset = 0x0EBF, .name = ""}),
            number_row(3));
  EXPECT_EQ(r.selected(4), 1u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw, DeclinesAFrameWhoseRawModeIsSet) {
  // The caller is the table's, and the routine is in raw mode: not the
  // frame these facts describe.
  r.lay_party(4, 1);
  set_raw_mode(r, 1);
  EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), number_row(3));
  EXPECT_EQ(press_bar(r, key_down, bar_callers[0]), key_down);
  EXPECT_EQ(r.selected(4), 1u);
  EXPECT_GE(r.box->seams().status(seam_id).declined, 1u);
}

TEST_F(SeamHeroKeysAtABarThatIsNotRaw, StepsAsideWhileTheMapIsOverTheRoster) {
  r.lay_party(4, 1);
  automap_state& map = r.box->automap();
  map.set_panel_open(true);
  map.set_panel_on_screen(true);
  EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), number_row(3));
  EXPECT_EQ(press_bar(r, key_down, bar_callers[0]), key_down);
  EXPECT_EQ(r.selected(4), 1u);
  map.set_panel_on_screen(false);
  EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), ignored);
  EXPECT_EQ(r.selected(4), 2u);
}

TEST(SeamHeroKeysAtABarThatIsNotRawAlone, EachSeamTakesItsOwnKeys) {
  // hero-keys alone takes digits and not arrows; list-arrows alone takes
  // arrows and not digits.
  {
    const rig r;
    r.arm();
    install_the_drawer(r);
    set_bar(r, " Alfa Beta Echo");
    set_raw_mode(r, 0);
    r.lay_party(4, 1);
    EXPECT_EQ(press_bar(r, key_down, bar_callers[0]), key_down);
    EXPECT_EQ(press_bar(r, key_up, bar_callers[0]), key_up);
    EXPECT_EQ(r.selected(4), 1u);
    EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), ignored);
    EXPECT_EQ(r.selected(4), 2u);
  }
  {
    const rig r;
    ASSERT_EQ(r.box->seams().enable("list-arrows"), seam_reason::none);
    r.manager_says(word_menu, menu_segment);
    r.manager_says(word_adventure, adventure_segment);
    r.manager_says(word_slots, slots_segment);
    install_the_drawer(r);
    set_bar(r, " Alfa Beta Echo");
    set_raw_mode(r, 0);
    r.lay_party(4, 1);
    EXPECT_EQ(press_bar(r, number_row(3), bar_callers[0]), number_row(3));
    EXPECT_EQ(press_bar(r, key_down, bar_callers[0]), ignored);
    EXPECT_EQ(r.selected(4), 2u);
  }
}

}  // namespace
}  // namespace amberfolio::machine
