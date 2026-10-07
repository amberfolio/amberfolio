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
#include <span>
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

constexpr std::string_view seam_id = "modern-controls";

/// Where this piece's points sit in the one seam's table, and how many it
/// has: the pieces follow one another in the order
/// seam_modern_controls.cpp lists them.
constexpr std::size_t piece_first = 3;
constexpr std::size_t piece_count = 1;

[[nodiscard]] std::span<const seam_point> piece_points(
    const seam_definition& s) {
  return s.points.subspan(piece_first, piece_count);
}

/// The point: the call into the key-read routine, in overlay 25.
constexpr std::uint16_t point = 0x0572;

/// The words the program's overlay manager keeps the modules' segments in:
/// overlay 25, overlay 14 (the adventuring loop), overlay 15 (camp), and
/// the four more that use the arrows: overlays 5 (post-combat), 8 (combat),
/// 13 (aim), 16 (the stat editor) and 20 (the rest time, stepped by
/// decision).
constexpr std::uint32_t word_menu = 0x3C60;
constexpr std::uint32_t word_adventure = 0x730;
constexpr std::uint32_t word_camp = 0x760;
constexpr std::uint32_t word_post_combat = 0x260;
constexpr std::uint32_t word_combat = 0x360;
constexpr std::uint32_t word_aim = 0x690;
constexpr std::uint32_t word_editor = 0x790;
constexpr std::uint32_t word_rest = 0x8D0;
constexpr std::uint32_t word_script = 0x2C0;
/// The modules the Enter audit (#459) added: overlays 4 (the temple), 6
/// (the shop), 17 (the save and load slot bars), 19 (View) and 21 (the
/// temple's appraisal).
constexpr std::uint32_t word_temple = 0x230;
constexpr std::uint32_t word_shop = 0x290;
constexpr std::uint32_t word_slots = 0x7D0;
constexpr std::uint32_t word_view = 0x860;
constexpr std::uint32_t word_appraise = 0x900;

/// Where each module is, in this test.
constexpr std::uint16_t menu_segment = 0x6000;
constexpr std::uint16_t adventure_segment = 0x6400;
constexpr std::uint16_t camp_segment = 0x6800;
constexpr std::uint16_t post_combat_segment = 0x6C00;
constexpr std::uint16_t combat_segment = 0x7000;
constexpr std::uint16_t aim_segment = 0x7400;
constexpr std::uint16_t editor_segment = 0x7800;
constexpr std::uint16_t rest_segment = 0x7C00;
constexpr std::uint16_t script_segment = 0x8000;
constexpr std::uint16_t temple_segment = 0x8400;
constexpr std::uint16_t shop_segment = 0x8800;
constexpr std::uint16_t slots_segment = 0x8C00;
constexpr std::uint16_t view_segment = 0x9000;
constexpr std::uint16_t appraise_segment = 0x9400;

/// The callers' return offsets.
constexpr std::uint16_t ret_yes_no = 0x111E;
constexpr std::uint16_t ret_area = 0x09D5;
constexpr std::uint16_t ret_view = 0x0C45;
constexpr std::uint16_t ret_camp = 0x1F24;
constexpr std::uint16_t ret_magic = 0x1447;
constexpr std::uint16_t ret_alter = 0x1CA4;
constexpr std::uint16_t ret_order = 0x17DA;
constexpr std::uint16_t ret_move = 0x0AC8;
constexpr std::uint16_t ret_aim = 0x3178;
constexpr std::uint16_t ret_editor = 0x216E;
constexpr std::uint16_t ret_rest = 0x076E;
constexpr std::uint16_t ret_share = 0x0AF8;
/// The script runner's call into the routine, in overlay 7.
constexpr std::uint16_t ret_script = 0x16EB;
constexpr std::uint16_t ret_npc_share = 0x14C7;
/// The post-combat Take bar and the temple's keep prompt, whose letters
/// are the arrows' scan codes: not excluded, by decision.
constexpr std::uint16_t ret_take = 0x0D91;
constexpr std::uint16_t ret_keep = 0x1DC1;
/// The pick-list's call into the routine, which is in overlay 25.
constexpr std::uint16_t ret_pick_list = 0x0FE0;

/// The callers the Enter audit (#459) added to the table.
constexpr std::uint16_t ret_alter_toggles = 0x1DDF;
constexpr std::uint16_t ret_camp_speed = 0x1B91;
constexpr std::uint16_t ret_portrait = 0x3449;
constexpr std::uint16_t ret_shop = 0x061F;
constexpr std::uint16_t ret_temple = 0x0DAA;
constexpr std::uint16_t ret_loot = 0x1024;
constexpr std::uint16_t ret_combat_command = 0x0819;
constexpr std::uint16_t ret_combat_done = 0x0F70;
constexpr std::uint16_t ret_combat_speed = 0x120C;
constexpr std::uint16_t ret_view_bar = 0x0C9C;
constexpr std::uint16_t ret_appraise = 0x1C47;
constexpr std::uint16_t ret_load = 0x16E2;

/// The combat aim bar, where the program's own Enter repeats the last Next
/// or Prev by accident: Enter takes the lit command, by the maintainer's
/// decision (#487).
constexpr std::uint16_t ret_aim_bar = 0x2C31;

/// The callers the audit read and left out: Enter does something there, or
/// the bar is one a stray Enter should not act on.
constexpr std::uint16_t ret_door_bash = 0x0EBF;
constexpr std::uint16_t ret_door_stuck = 0x0FFE;
constexpr std::uint16_t ret_keep_jewel = 0x2114;
constexpr std::uint16_t ret_icon_editor = 0x39FE;
constexpr std::uint16_t ret_save = 0x1DA1;
constexpr std::uint16_t ret_stage = 0x236F;
constexpr std::uint16_t ret_main_menu = 0x02FD;

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
constexpr std::uint16_t escape = 0x011B;
constexpr std::uint16_t comma = 0x332C;
constexpr std::uint16_t period = 0x342E;

constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

constexpr std::uint16_t ring_first = 0x1E;

/// The script runner's allow-Enter argument, above its BP; and where this
/// test puts the runner's frame: above the routine's own.
constexpr std::uint16_t runner_allow = 8;
constexpr std::uint16_t runner_bp = 0x0700;

/// The character a keystroke word carries.
[[nodiscard]] char letter_of(std::uint16_t key) {
  return static_cast<char>(key & 0xFFU);
}

struct rig {
  /// Where the stack and the routine's frame are. Most tests leave them be.
  mutable std::uint16_t stack = stack_segment;
  mutable std::uint16_t frame = frame_base;

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
    manager_says(word_post_combat, post_combat_segment);
    manager_says(word_combat, combat_segment);
    manager_says(word_aim, aim_segment);
    manager_says(word_editor, editor_segment);
    manager_says(word_rest, rest_segment);
    manager_says(word_script, script_segment);
    manager_says(word_temple, temple_segment);
    manager_says(word_shop, shop_segment);
    manager_says(word_slots, slots_segment);
    manager_says(word_view, view_segment);
    manager_says(word_appraise, appraise_segment);
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
    const auto table = [this](unsigned index) {
      return static_cast<std::uint16_t>(frame - local_enter + index);
    };
    for (unsigned i = 0; i < 0x2A; ++i) {
      put_byte(stack, table(i), 0);
    }
    put_byte(stack, static_cast<std::uint16_t>(frame - local_bar),
             static_cast<std::uint8_t>(bar.size()));
    unsigned group = 1;
    for (std::size_t i = 0; i < bar.size(); ++i) {
      const char c = bar[i];
      put_byte(stack, static_cast<std::uint16_t>(frame - local_bar + 1 + i),
               static_cast<std::uint8_t>(c));
      if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))) {
        continue;
      }
      const auto position = static_cast<std::uint8_t>(i + 1);
      if (byte_is_zero(table(2 * group))) {
        put_byte(stack, table(2 * group), position);
      } else {
        put_byte(stack, table(2 * group + 1),
                 static_cast<std::uint8_t>(position - 2));
        ++group;
        put_byte(stack, table(2 * group), position);
      }
    }
    put_byte(stack, table(2 * group + 1),
             static_cast<std::uint8_t>(bar.size()));
    put_byte(stack, table(1), static_cast<std::uint8_t>(group));
    put_byte(stack, table(0), enter_allowed);
    put_byte(data_segment, data_highlight, highlight);
  }

  /// Whether the byte at `offset` in the stack segment is zero.
  [[nodiscard]] bool byte_is_zero(std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(stack, offset)] == 0;
  }

  /// Who called, and in what mode.
  void called_from(std::uint16_t segment, std::uint16_t offset,
                   std::uint8_t raw) const {
    put_word(stack, static_cast<std::uint16_t>(frame + frame_ip), offset);
    put_word(stack, static_cast<std::uint16_t>(frame + frame_cs), segment);
    put_byte(stack, static_cast<std::uint16_t>(frame + frame_raw), raw);
  }

  /// The routine's caller is the script runner, whose frame is at
  /// `runner_bp` and whose allow-Enter argument is `allow_enter`: the
  /// routine's saved BP is the runner's.
  void runner_frame(std::uint16_t allow_enter,
                    std::uint16_t at_bp = runner_bp) const {
    put_word(stack, frame, at_bp);
    put_word(stack, static_cast<std::uint16_t>(at_bp + runner_allow),
             allow_enter);
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
    r[cpu::sreg::ss] = stack;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame;
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
  ASSERT_EQ(piece_points(s).size(), 1u);

  const seam_point& p = piece_points(s)[0];
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

  // The seam stays armed: its points in the modules that are resident
  // act (#477). This piece's point is the one that does nothing.
  EXPECT_TRUE(r.box->seams().status(seam_id).armed);
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

struct caller_at {
  std::uint16_t segment;
  std::uint16_t offset;
  const char* name;
};

/// The callers that keep their arrows.
/// The party's own two bars are in the piece's exclusion too, and keep
/// their arrows from it; what they get instead is `move-mode`'s, which
/// steps them in menu mode (seam_move_mode_test.cpp).
constexpr std::array<caller_at, 5> excluded{{
    {.segment = combat_segment, .offset = ret_move, .name = "combat move"},
    {.segment = aim_segment, .offset = ret_aim, .name = "aim cursor"},
    {.segment = editor_segment, .offset = ret_editor, .name = "stat editor"},
    {.segment = post_combat_segment, .offset = ret_share, .name = "share"},
    {.segment = post_combat_segment,
     .offset = ret_npc_share,
     .name = "npc share"},
}};

TEST(SeamBarKeys, LeftAndRightBecomeTheBarsOwnTwoStepKeysAtANonRawCaller) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  EXPECT_EQ(r.press(left, stack_segment, 0x1234, 0), comma);
  EXPECT_EQ(r.press(right, stack_segment, 0x1234, 0), period);
}

TEST(SeamBarKeys, LeftAndRightStepAtAnUnlistedRawCaller) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The camp bar, its Magic and Alter bars and the party-order screen hand
  // an arrow to the party cursor and nothing else; the pick-list ignores
  // it; the post-combat Take bar and the temple's keep prompt act on it as
  // the letters `M` and `K`, and are stepped by decision; so is the
  // rest-time menu, which picks its field on them. Any non-zero byte is raw:
  // the routine tests it as a byte.
  const std::array<caller_at, 9> callers{{
      {.segment = camp_segment, .offset = ret_camp, .name = "camp"},
      {.segment = camp_segment, .offset = ret_magic, .name = "magic"},
      {.segment = camp_segment, .offset = ret_alter, .name = "alter"},
      {.segment = camp_segment, .offset = ret_order, .name = "order"},
      {.segment = menu_segment, .offset = ret_pick_list, .name = "pick-list"},
      {.segment = post_combat_segment, .offset = ret_take, .name = "take"},
      {.segment = stack_segment, .offset = ret_keep, .name = "keep"},
      {.segment = stack_segment, .offset = 0x1234, .name = "unknown"},
  }};
  for (const caller_at& c : callers) {
    for (const std::uint8_t raw :
         std::array<std::uint8_t, 4>{1, 2, 0x0D, 0x80}) {
      EXPECT_EQ(r.press(left, c.segment, c.offset, raw), comma)
          << c.name << " " << int{raw};
      EXPECT_EQ(r.press(right, c.segment, c.offset, raw), period)
          << c.name << " " << int{raw};
    }
  }
}

TEST(SeamBarKeys, LeftAndRightAreTheCallersAtEachExcludedCaller) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  for (const caller_at& c : excluded) {
    for (const std::uint8_t raw : std::array<std::uint8_t, 3>{0, 1, 0x80}) {
      EXPECT_EQ(r.press(left, c.segment, c.offset, raw), left)
          << c.name << " " << int{raw};
      EXPECT_EQ(r.press(right, c.segment, c.offset, raw), right)
          << c.name << " " << int{raw};
    }
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, TheExclusionIsAsExactAsTheEnterTable) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // A listed offset in the wrong module, a listed module at an offset the
  // table does not name, and a return segment that is not the module's.
  EXPECT_EQ(r.press(left, camp_segment, ret_area, 1), comma);
  EXPECT_EQ(r.press(left, adventure_segment, ret_camp, 1), comma);
  EXPECT_EQ(r.press(right, combat_segment, ret_aim, 1), period);
  EXPECT_EQ(r.press(right, aim_segment, ret_move, 1), period);
  EXPECT_EQ(r.press(right, rest_segment, ret_editor, 0), period);
  EXPECT_EQ(r.press(right, adventure_segment, 0, 1), period);
  EXPECT_EQ(r.press(right, 0, ret_area, 1), period);
  // The module's word says it is out: it is not a caller.
  r.manager_says(word_combat, 0);
  EXPECT_EQ(r.press(left, combat_segment, ret_move, 1), comma);
  r.manager_says(word_combat, combat_segment);
  EXPECT_EQ(r.press(left, combat_segment, ret_move, 1), left);
}

TEST(SeamBarKeys, TheCursorsKeysAreTheProgramsAtTheCampBarStill) {
  // Home, End and the keypad's 7 and 1 step the selected member, and the
  // rewrite is of Left and Right only. (Up and Down are not the program's
  // here once `modern-controls` is on: its list-arrows piece writes them as
  // Home and End, which seam_list_arrows_test.cpp pins.)
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  for (const std::uint16_t key :
       {std::uint16_t{0x4700}, std::uint16_t{0x4F00}, std::uint16_t{0x4737},
        std::uint16_t{0x4F31}}) {
    EXPECT_EQ(r.press(key, camp_segment, ret_camp, 1), key) << key;
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

  // Home, End, a ctrl-arrow (a different scan code), the digit the keypad's
  // 4 makes with Num Lock on (the right scan code, and a character), a
  // letter, Escape, Space. Up and Down are the list-arrows piece's.
  for (const std::uint16_t key :
       {std::uint16_t{0x4700}, std::uint16_t{0x4F00}, std::uint16_t{0x7300},
        std::uint16_t{0x4B34}, std::uint16_t{0x1E41}, std::uint16_t{0x011B},
        std::uint16_t{0x3920}}) {
    EXPECT_EQ(r.press(key, camp_segment, ret_camp, 0), key) << key;
    EXPECT_EQ(r.press(key, camp_segment, ret_camp, 1), key) << key;
  }
}

// --- Enter -----------------------------------------------------------------

struct enter_caller {
  std::uint16_t segment;
  std::uint16_t offset;
  const char* name;
};

/// Every caller in the Enter table, the audit's (#459) included.
constexpr std::array<enter_caller, 24> tabled{{
    {.segment = menu_segment, .offset = ret_yes_no, .name = "yes/no"},
    {.segment = adventure_segment, .offset = ret_area, .name = "overhead"},
    {.segment = adventure_segment, .offset = ret_view, .name = "3D"},
    {.segment = camp_segment, .offset = ret_camp, .name = "camp"},
    {.segment = camp_segment, .offset = ret_magic, .name = "magic"},
    {.segment = camp_segment, .offset = ret_alter, .name = "alter"},
    {.segment = camp_segment,
     .offset = ret_alter_toggles,
     .name = "alter's portraits and monsters"},
    {.segment = camp_segment, .offset = ret_camp_speed, .name = "camp speed"},
    {.segment = editor_segment, .offset = ret_portrait, .name = "portrait"},
    {.segment = editor_segment,
     .offset = ret_icon_editor,
     .name = "icon editor"},
    {.segment = shop_segment, .offset = ret_shop, .name = "shop"},
    {.segment = temple_segment, .offset = ret_temple, .name = "temple"},
    {.segment = post_combat_segment, .offset = ret_loot, .name = "loot"},
    {.segment = post_combat_segment, .offset = ret_take, .name = "take"},
    {.segment = combat_segment,
     .offset = ret_combat_command,
     .name = "combat command"},
    {.segment = combat_segment, .offset = ret_combat_done, .name = "done"},
    {.segment = combat_segment,
     .offset = ret_combat_speed,
     .name = "combat speed"},
    {.segment = view_segment, .offset = ret_view_bar, .name = "view"},
    {.segment = appraise_segment, .offset = ret_appraise, .name = "appraise"},
    {.segment = slots_segment, .offset = ret_load, .name = "load"},
    {.segment = rest_segment, .offset = ret_rest, .name = "rest time"},
    {.segment = adventure_segment, .offset = ret_door_bash, .name = "door"},
    {.segment = adventure_segment,
     .offset = ret_door_stuck,
     .name = "stuck door"},
    {.segment = aim_segment, .offset = ret_aim_bar, .name = "aim bar"},
}};

TEST(SeamBarKeys, EnterTakesTheHighlightedCommandAtEachTabledCaller) {
  for (const enter_caller& c : tabled) {
    const rig r;
    r.arm();
    for (std::uint8_t group = 1; group <= 3; ++group) {
      r.lay_bar("Ant Bee Cow", group);
      for (const std::uint8_t raw : std::array<std::uint8_t, 2>{0, 1}) {
        const std::uint16_t answer = r.press(enter, c.segment, c.offset, raw);
        EXPECT_EQ(letter_of(answer), "ABC"[group - 1])
            << c.name << " " << int{group};
        EXPECT_EQ(answer >> 8U, 0x1Cu)
            << c.name << ": posted under Enter's own scan code";
      }
    }
  }
}

TEST(SeamBarKeys, EnterAtEveryTabledCallerHasTheGuardsEveryBarHas) {
  for (const enter_caller& c : tabled) {
    const rig r;
    r.arm();
    // The last-match guard, a bar that is not drawn, and a highlight that
    // is not set yet.
    r.lay_bar("Ant Bee Axe", 1);
    EXPECT_EQ(r.press(enter, c.segment, c.offset, 1), enter) << c.name;
    r.lay_bar("Ant Bee Cow", 2, 0);
    EXPECT_EQ(r.press(enter, c.segment, c.offset, 1), enter) << c.name;
    r.lay_bar("Ant Bee Cow", 0);
    EXPECT_EQ(r.press(enter, c.segment, c.offset, 1), enter) << c.name;
    EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u) << c.name;
  }
}

TEST(SeamBarKeys, EnterFollowsTheHighlightAtTheRestTimeMenuNotTheProgramsRest) {
  // The program maps Enter to `R` there. The bar's `Y` is in the middle of
  // its word, and the highlighted word's own capital is what Enter becomes.
  const rig r;
  r.arm();
  const std::string_view bar = "Rest daYs Hours Mins Inc Dec Exit";
  const std::array<char, 7> letters{'R', 'Y', 'H', 'M', 'I', 'D', 'E'};
  for (std::size_t group = 1; group <= letters.size(); ++group) {
    r.lay_bar(bar, static_cast<std::uint8_t>(group));
    const std::uint16_t answer = r.press(enter, rest_segment, ret_rest, 1);
    EXPECT_EQ(letter_of(answer), letters[group - 1]) << group;
    EXPECT_EQ(answer >> 8U, 0x1Cu);
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EnterIsLeftAloneWhereATabledOffsetIsInTheWrongModule) {
  // Every tabled offset, asked from every module that does not own it: the
  // table is exact in both halves of a caller.
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);
  for (const enter_caller& c : tabled) {
    for (const enter_caller& other : tabled) {
      bool owned = false;
      for (const enter_caller& t : tabled) {
        owned = owned || (t.segment == other.segment && t.offset == c.offset);
      }
      if (!owned) {
        EXPECT_EQ(r.press(enter, other.segment, c.offset, 1), enter)
            << c.name << " from " << other.name;
      }
    }
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);

  r.manager_says(word_slots, 0);
  EXPECT_EQ(r.press(enter, 0, ret_load, 1), enter);
  r.manager_says(word_slots, slots_segment);
  EXPECT_EQ(letter_of(r.press(enter, slots_segment, ret_load, 1)), 'B');
}

TEST(SeamBarKeys, EnterIsLeftAloneAtTheCallersTheAuditReadAndKeptOut) {
  // Each does something with Enter already, or is a bar a stray Enter
  // should not act on (docs/seams.md §10 has the verdict for each).
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  const std::array<enter_caller, 14> kept_out{{
      {.segment = camp_segment, .offset = ret_order, .name = "party order"},
      {.segment = combat_segment, .offset = ret_move, .name = "combat move"},
      {.segment = aim_segment, .offset = ret_aim, .name = "aim cursor"},
      {.segment = appraise_segment, .offset = ret_keep, .name = "keep gem"},
      {.segment = appraise_segment,
       .offset = ret_keep_jewel,
       .name = "keep jewel"},
      {.segment = post_combat_segment, .offset = ret_share, .name = "share"},
      {.segment = post_combat_segment,
       .offset = ret_npc_share,
       .name = "npc share"},
      {.segment = editor_segment, .offset = ret_editor, .name = "stat editor"},
      {.segment = editor_segment, .offset = ret_main_menu, .name = "main menu"},
      {.segment = slots_segment, .offset = ret_save, .name = "save slot"},
      {.segment = slots_segment, .offset = ret_stage, .name = "stage prompt"},
      {.segment = stack_segment, .offset = 0x1234, .name = "unknown"},
      {.segment = script_segment, .offset = ret_load, .name = "load, in 7"},
      {.segment = slots_segment, .offset = ret_script, .name = "script, in 17"},
  }};
  for (const enter_caller& c : kept_out) {
    for (const std::uint8_t raw : std::array<std::uint8_t, 2>{0, 1}) {
      EXPECT_EQ(r.press(enter, c.segment, c.offset, raw), enter) << c.name;
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

TEST(SeamBarKeys, EnterInAPickListTakesTheBarCommandThePlayerMovedTo) {
  // The list opens with its bar on the first command; a highlight anywhere
  // else was moved there, and Enter takes it rather than the row.
  const rig r;
  r.arm();
  r.lay_bar("Memo Exit", 2);
  EXPECT_EQ(letter_of(r.press(enter, menu_segment, ret_pick_list, 1)), 'E');
  r.lay_bar("Memo Next Exit", 2);
  EXPECT_EQ(letter_of(r.press(enter, menu_segment, ret_pick_list, 1)), 'N');
  r.lay_bar("Memo Exit", 1);
  EXPECT_EQ(r.press(enter, menu_segment, ret_pick_list, 1), enter);
}

TEST(SeamBarKeys, EnterIsLeftAloneAtACallerThatIsNotInTheTable) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 2);

  // The pick-list with its bar on the first command, where it opens: Enter
  // is the list's own and confirms the row.
  r.lay_bar("Ant Bee Cow", 1);
  EXPECT_EQ(r.press(enter, menu_segment, ret_pick_list, 1), enter);
  r.lay_bar("Ant Bee Cow", 2);
  // The member-order screen, where Enter toggles between picking a member
  // up and putting it down: its arrows are tabled, its Enter is not.
  EXPECT_EQ(r.press(enter, camp_segment, ret_order, 1), enter);
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

  // A highlight one past the group count (the routine clamps one that is
  // past it, so it is never there), and a bar of no command letters, whose
  // one group begins nowhere.
  r.lay_bar("Ant Bee Cow", 4);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  r.lay_bar("a b c", 1);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
}

TEST(SeamBarKeys, EnterIsLeftAloneBeforeAnyGroupHasBeenHighlighted) {
  // The index is one-based and starts at zero: nothing is highlighted, so
  // there is no command to take, and it is not a frame to refuse.
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 0);
  EXPECT_EQ(r.press(enter, camp_segment, ret_camp, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EnterReadsTheHighlightFromTheDataSegmentNotTheStack) {
  const rig r;
  r.arm();
  r.lay_bar("Ant Bee Cow", 3);
  // The stack segment holds a different answer at the same offset.
  r.put_byte(stack_segment, data_highlight, 1);
  EXPECT_EQ(letter_of(r.press(enter, camp_segment, ret_camp, 1)), 'C');
}

// --- The script prompts ----------------------------------------------------

TEST(SeamBarKeys, EnterTakesTheHighlightAtAScriptPromptThatDoesNotAllowIt) {
  const rig r;
  r.arm();
  r.runner_frame(0);

  r.lay_bar("Yes No", 1);
  const std::uint16_t answer = r.press(enter, script_segment, ret_script, 1);
  EXPECT_EQ(letter_of(answer), 'Y');
  EXPECT_EQ(answer >> 8U, 0x1Cu) << "posted under Enter's own scan code";
  r.lay_bar("Yes No", 2);
  EXPECT_EQ(letter_of(r.press(enter, script_segment, ret_script, 1)), 'N');
  // Any bar, not only a Yes/No: it is the script's own set of hotkeys.
  r.lay_bar("Ant Bee Cow", 3);
  EXPECT_EQ(letter_of(r.press(enter, script_segment, ret_script, 1)), 'C');
}

TEST(SeamBarKeys, EnterIsTheProgramsOwnAtAScriptPromptThatAllowsIt) {
  const rig r;
  r.arm();
  r.lay_bar("Yes No", 2);

  // The runner answers Enter with the first choice itself.
  for (const std::uint16_t allow : {std::uint16_t{1}, std::uint16_t{0xFF}}) {
    r.runner_frame(allow);
    EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter) << allow;
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EnterAtAScriptPromptHasTheGuardsEveryBarHas) {
  const rig r;
  r.arm();
  r.runner_frame(0);

  // The last-match guard, the bar that is not drawn, and the highlight that
  // is not set yet.
  r.lay_bar("Ant Bee Axe", 1);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
  r.lay_bar("Yes No", 2, 0);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  r.lay_bar("Yes No", 0);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
}

TEST(SeamBarKeys, EnterIsLeftAloneWhereTheRunnerIsNotTheCallerOfRecord) {
  const rig r;
  r.arm();
  r.lay_bar("Yes No", 2);
  r.runner_frame(0);

  // The runner's offset in another module, another offset in the runner's
  // module, and the runner's module while the manager says it is out.
  EXPECT_EQ(r.press(enter, camp_segment, ret_script, 1), enter);
  EXPECT_EQ(r.press(enter, script_segment, 0x16EC, 1), enter);
  EXPECT_EQ(r.press(enter, script_segment, 0x1688, 1), enter);
  r.manager_says(word_script, 0);
  EXPECT_EQ(r.press(enter, 0, ret_script, 1), enter);
  r.manager_says(word_script, script_segment);
  EXPECT_EQ(letter_of(r.press(enter, script_segment, ret_script, 1)), 'N');
}

TEST(SeamBarKeys, EnterReadsTheRunnersFrameOnlyWhereItIsTheRunnersFrame) {
  const rig r;
  r.arm();
  r.lay_bar("Yes No", 2);

  // A saved BP that is not above the routine's own is not a caller's frame.
  r.runner_frame(0, r.frame);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  r.runner_frame(0, static_cast<std::uint16_t>(r.frame - 2));
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  r.runner_frame(0, 0);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EnterNeverReadsTheRunnersFramePastConventionalRam) {
  // The stack stands at the top of the RAM the program owns: a saved BP that
  // puts the argument past it is not read, and the key is left alone.
  const rig r;
  r.arm();
  r.stack = 0x9F00;
  r.frame = 0x0F00;
  r.lay_bar("Yes No", 2);

  r.runner_frame(0, 0x0FF0);
  EXPECT_EQ(letter_of(r.press(enter, script_segment, ret_script, 1)), 'N')
      << "the argument is the last bytes inside RAM";
  r.runner_frame(0, 0x0FFC);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  r.runner_frame(0, 0xFFF0);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

// --- Esc at a Yes/No question ----------------------------------------------

TEST(SeamBarKeys, EscAnswersNoAtTheYesNoPrompt) {
  const rig r;
  r.arm();
  // Whichever answer is highlighted, and whatever mode the call is in.
  for (std::uint8_t group = 1; group <= 2; ++group) {
    r.lay_bar("Yup Nay", group);
    for (const std::uint8_t raw : std::array<std::uint8_t, 2>{0, 1}) {
      const std::uint16_t answer =
          r.press(escape, menu_segment, ret_yes_no, raw);
      EXPECT_EQ(letter_of(answer), 'N');
      EXPECT_EQ(answer >> 8U, 0x1Cu) << "posted as the Enter letters are";
    }
  }
}

TEST(SeamBarKeys, EscAnswersNoAtAScriptPromptWhoseLettersAreYAndN) {
  const rig r;
  r.arm();

  // Whether or not the runner takes Enter itself, and with the answers in
  // either order.
  for (const std::uint16_t allow : {std::uint16_t{0}, std::uint16_t{1}}) {
    r.runner_frame(allow);
    r.lay_bar("Yes No", 1);
    EXPECT_EQ(r.press(escape, script_segment, ret_script, 1), 0x1C4E) << allow;
    r.lay_bar("No Yes", 2);
    EXPECT_EQ(r.press(escape, script_segment, ret_script, 1), 0x1C4E) << allow;
  }
}

TEST(SeamBarKeys, EscIsTheProgramsAtAScriptPromptWithAnyOtherHotkeys) {
  const rig r;
  r.arm();
  r.runner_frame(0);

  // A third letter, a digit, one answer only, a different pair, and a bar
  // with no command letter in it at all.
  for (const std::string_view bar :
       {"Yes No Maybe", "Yes No 3", "Yes", "No", "Ant Bee", "yes no", ""}) {
    r.lay_bar(bar, 1);
    EXPECT_EQ(r.press(escape, script_segment, ret_script, 1), escape) << bar;
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EscIsTheProgramsAtEveryOtherBar) {
  const rig r;
  r.arm();
  r.runner_frame(0);
  // A Yes/No-shaped bar at a caller that is not a Yes/No question.
  r.lay_bar("Yup Nay", 2);

  struct caller {
    std::uint16_t segment;
    std::uint16_t offset;
  };
  for (const caller& c : std::array<caller, 11>{{
           {.segment = adventure_segment, .offset = ret_area},
           {.segment = adventure_segment, .offset = ret_view},
           {.segment = camp_segment, .offset = ret_camp},
           {.segment = camp_segment, .offset = ret_magic},
           {.segment = camp_segment, .offset = ret_alter},
           {.segment = camp_segment, .offset = ret_order},
           {.segment = menu_segment, .offset = ret_pick_list},
           {.segment = combat_segment, .offset = ret_move},
           {.segment = rest_segment, .offset = ret_rest},
           {.segment = stack_segment, .offset = 0x1234},
           {.segment = camp_segment, .offset = ret_yes_no},
       }}) {
    EXPECT_EQ(r.press(escape, c.segment, c.offset, 0), escape)
        << std::hex << c.segment << ":" << c.offset;
  }
  // The runner's own offset in another module, and the Yes/No's in one that
  // is not overlay 25.
  EXPECT_EQ(r.press(escape, camp_segment, ret_script, 1), escape);
  EXPECT_EQ(r.press(escape, script_segment, ret_yes_no, 1), escape);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamBarKeys, EscIsLeftAloneWhileThePushbackSlotIsArmedOrTheReaderIsOpen) {
  const rig r;
  r.arm();
  r.lay_bar("Yup Nay", 1);

  r.put_byte(data_segment, data_pushback, 0x4B);
  EXPECT_EQ(r.press(escape, menu_segment, ret_yes_no, 0), escape);
  r.put_byte(data_segment, data_pushback, 0);
  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press(escape, menu_segment, ret_yes_no, 0), escape);
  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(letter_of(r.press(escape, menu_segment, ret_yes_no, 0)), 'N');
}

TEST(SeamBarKeys, EscAndEnterAreNotRewrittenWhileTheSeamIsOff) {
  const rig r;
  r.manager_says(word_menu, menu_segment);
  r.manager_says(word_script, script_segment);
  r.lay_bar("Yes No", 1);
  r.runner_frame(0);
  EXPECT_EQ(r.press(escape, menu_segment, ret_yes_no, 0), escape);
  EXPECT_EQ(r.press(escape, script_segment, ret_script, 1), escape);
  EXPECT_EQ(r.press(enter, script_segment, ret_script, 1), enter);
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
  // nothing: only the four keys look at the frame at all.
  const rig r;
  r.arm();
  r.lay_bar("", 0, 0);
  EXPECT_EQ(r.press(0x1E41, camp_segment, ret_camp, 1), 0x1E41);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

}  // namespace
}  // namespace amberfolio::machine
