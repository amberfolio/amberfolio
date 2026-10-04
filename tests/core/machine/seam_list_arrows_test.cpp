// SPDX-License-Identifier: AGPL-3.0-only
//
// The list-arrows seam (seam_list_arrows.cpp, #423, #435), exercised through
// its mechanism and not through any program: the test stands the processor
// on each of the seam's points, with a key in AL and the menu-bar routine's
// out-parameter in the frame where the facts say it is, and reads AL after
// one step; and, for the point inside the menu-bar routine, with a keystroke
// at the head of the BIOS ring and the caller's return address in the frame,
// and reads the ring after one step.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. **Every byte here is this
// file's own** (PLAN.md §6).

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

constexpr std::string_view seam_id = "list-arrows";

/// Where the two points are: in overlay 25, from the module's start, and
/// in the resident image, from the image segment.
constexpr std::uint16_t list_point = 0x0FE0;
constexpr std::uint16_t picker_point = 0x38AA;

/// The third: the call into the key-read routine, in overlay 25.
constexpr std::uint16_t read_point = 0x0572;

/// Where each routine keeps the menu-bar routine's out-parameter, as a
/// distance below its BP.
constexpr std::uint16_t list_flag_below_bp = 0x57;
constexpr std::uint16_t picker_flag_below_bp = 0x2B;

/// The word the program's overlay manager keeps overlay 25's segment in.
constexpr std::uint32_t overlay_word = 0x3C60;

/// The words the program's overlay manager keeps the modules' segments in:
/// overlay 25 above, and the callers' modules: overlays 4 (the temple),
/// 5 (post-combat), 6 (the shop), 7 (the script prompts), 8 (combat), 13
/// (aim), 14 (the adventuring loop), 15 (camp), 16 (the main menu and the
/// stat editor) and 20 (the rest time).
constexpr std::uint32_t word_temple = 0x230;
constexpr std::uint32_t word_post_combat = 0x260;
constexpr std::uint32_t word_shop = 0x290;
constexpr std::uint32_t word_script = 0x2C0;
constexpr std::uint32_t word_combat = 0x360;
constexpr std::uint32_t word_aim = 0x690;
constexpr std::uint32_t word_adventure = 0x730;
constexpr std::uint32_t word_camp = 0x760;
constexpr std::uint32_t word_roster = 0x790;
constexpr std::uint32_t word_rest = 0x8D0;

/// Where each of those modules is, in this test.
constexpr std::uint16_t temple_segment = 0x6200;
constexpr std::uint16_t post_combat_segment = 0x6400;
constexpr std::uint16_t shop_segment = 0x6600;
constexpr std::uint16_t script_segment = 0x6800;
constexpr std::uint16_t combat_segment = 0x6A00;
constexpr std::uint16_t aim_segment = 0x6C00;
constexpr std::uint16_t adventure_segment = 0x6E00;
constexpr std::uint16_t camp_segment = 0x7000;
constexpr std::uint16_t roster_segment = 0x7200;
constexpr std::uint16_t rest_segment = 0x7400;

/// The frame, above BP, and the data segment's pushback slot.
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;
constexpr std::uint16_t data_pushback = 0x8501;
constexpr std::uint16_t ring_first = 0x1E;

/// The keys, as the BIOS ring holds them: scan code high, character low.
constexpr std::uint16_t ring_home = 0x4700;
constexpr std::uint16_t ring_up = 0x4800;
constexpr std::uint16_t ring_end = 0x4F00;
constexpr std::uint16_t ring_down = 0x5000;
constexpr std::uint16_t ring_left = 0x4B00;
constexpr std::uint16_t ring_right = 0x4D00;
constexpr std::uint16_t ring_enter = 0x1C0D;
/// The keypad's 8 and 2 with NumLock on: the same scan codes as Up and Down,
/// with a character.
constexpr std::uint16_t ring_pad_8 = 0x4838;
constexpr std::uint16_t ring_pad_2 = 0x5032;
/// The keypad's 7 and 1, which the menu-bar routine's own table turns into
/// Home's and End's letters: what the seam writes over the keypad's 8 and 2.
constexpr std::uint16_t ring_pad_7 = 0x4737;
constexpr std::uint16_t ring_pad_1 = 0x4F31;
/// The number row's digits, scan code `0x02` for 1 up to `0x0B` for 0.
[[nodiscard]] constexpr std::uint16_t ring_row(unsigned digit) {
  const unsigned scan = digit == 0 ? 0x0BU : 0x01U + digit;
  return static_cast<std::uint16_t>((scan << 8U) | ('0' + digit));
}

/// A caller of the menu-bar routine: where it is, and the offset of the
/// instruction after its call.
struct caller_at {
  std::uint16_t segment;
  std::uint16_t offset;
  const char* name;
};

/// The keys, as scan codes.
constexpr std::uint8_t home = 0x47;
constexpr std::uint8_t up = 0x48;
constexpr std::uint8_t page_up = 0x49;
constexpr std::uint8_t end = 0x4F;
constexpr std::uint8_t down = 0x50;
constexpr std::uint8_t page_down = 0x51;

/// Where the test puts things: the module in a segment of its own, and a
/// stack and a data segment apart, so a wrong segment register shows up
/// as a wrong answer.
constexpr std::uint16_t overlay_segment = 0x6000;
constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

enum class where : std::uint8_t { list, picker };

/// The callers whose Up and Down become Home and End: the camp bar, Magic,
/// Alter, the party-order screen, the post-combat Take and treasure bars,
/// the shop, the temple and the script prompts.
constexpr std::array<caller_at, 9> allowed_callers{{
    {.segment = camp_segment, .offset = 0x1F24, .name = "the camp bar"},
    {.segment = camp_segment, .offset = 0x1447, .name = "camp's Magic bar"},
    {.segment = camp_segment, .offset = 0x1CA4, .name = "camp's Alter bar"},
    {.segment = camp_segment,
     .offset = 0x17DA,
     .name = "the party-order screen"},
    {.segment = post_combat_segment,
     .offset = 0x0D91,
     .name = "the post-combat Take bar"},
    {.segment = post_combat_segment,
     .offset = 0x1024,
     .name = "the post-combat treasure bar"},
    {.segment = shop_segment, .offset = 0x061F, .name = "the shop's bar"},
    {.segment = temple_segment, .offset = 0x0DAA, .name = "the temple's bar"},
    {.segment = script_segment, .offset = 0x16EB, .name = "the script prompts"},
}};

/// The callers that use Up and Down themselves, or are not for this seam.
constexpr std::array<caller_at, 10> left_out_callers{{
    {.segment = adventure_segment,
     .offset = 0x09D5,
     .name = "the adventuring bar, city"},
    {.segment = adventure_segment,
     .offset = 0x0C45,
     .name = "the adventuring bar, wilderness"},
    {.segment = combat_segment,
     .offset = 0x0AC8,
     .name = "the combat move loop"},
    {.segment = aim_segment, .offset = 0x3178, .name = "the combat aim cursor"},
    {.segment = roster_segment, .offset = 0x216E, .name = "the stat editor"},
    {.segment = rest_segment, .offset = 0x076E, .name = "the rest-time menu"},
    {.segment = camp_segment,
     .offset = 0x1B91,
     .name = "camp's game-speed screen"},
    {.segment = roster_segment, .offset = 0x02FD, .name = "the main menu"},
    {.segment = post_combat_segment,
     .offset = 0x0AF8,
     .name = "the treasure share's prompt"},
    {.segment = post_combat_segment,
     .offset = 0x14C7,
     .name = "the NPC share's prompt"},
}};

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
    manager_says_overlay_at(overlay_segment);
    manager_says(word_temple, temple_segment);
    manager_says(word_post_combat, post_combat_segment);
    manager_says(word_shop, shop_segment);
    manager_says(word_script, script_segment);
    manager_says(word_combat, combat_segment);
    manager_says(word_aim, aim_segment);
    manager_says(word_adventure, adventure_segment);
    manager_says(word_camp, camp_segment);
    manager_says(word_roster, roster_segment);
    manager_says(word_rest, rest_segment);
  }

  /// What the overlay manager writes: where a module begins now.
  void manager_says(std::uint32_t word, std::uint16_t segment) const {
    put_word(image_load_segment, static_cast<std::uint16_t>(word), segment);
  }

  /// What the overlay manager writes: the segment overlay 25 begins at
  /// now, zero when it is not loaded.
  void manager_says_overlay_at(std::uint16_t segment) const {
    put_byte(physical(image_load_segment, overlay_word),
             static_cast<std::uint8_t>(segment));
    put_byte(physical(image_load_segment, overlay_word) + 1,
             static_cast<std::uint8_t>(segment >> 8U));
  }

  static std::uint32_t physical(std::uint16_t segment, std::uint32_t offset) {
    return cpu::physical_address(segment, static_cast<std::uint16_t>(offset));
  }

  void put_byte(std::uint32_t address, std::uint8_t value) const {
    box->memory().ram()[address] = value;
  }

  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    const std::uint32_t at = physical(segment, offset);
    put_byte(at, static_cast<std::uint8_t>(value));
    put_byte(at + 1, static_cast<std::uint8_t>(value >> 8U));
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    const auto& ram = box->memory().ram();
    const std::uint32_t at = physical(segment, offset);
    return static_cast<std::uint16_t>(ram[at] | (ram[at + 1] << 8U));
  }

  /// A keystroke at the head of the BIOS ring, and nothing behind it.
  void ring(std::uint16_t key) const {
    put_word(0x40, 0x1A, ring_first);
    put_word(0x40, 0x1C, ring_first + 2);
    put_word(0x40, ring_first, key);
  }

  [[nodiscard]] std::uint16_t head_word() const {
    return word(0x40, word(0x40, 0x1A));
  }

  /// Stand on the point inside the menu-bar routine, called from
  /// `segment:offset`, with `key` at the head of the ring, and take one
  /// step. The instruction there is a HLT, so the step is the arrival and
  /// nothing else. Answers what the ring's head then holds.
  [[nodiscard]] std::uint16_t press(std::uint16_t key, std::uint16_t segment,
                                    std::uint16_t offset) const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_ip),
             offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_cs),
             segment);
    ring(key);
    put_byte(physical(overlay_segment, read_point), 0xF4);
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = overlay_segment;
    r.ip = read_point;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    box->step();
    return head_word();
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// Stand on `which`'s point with `key` in AL and `flag` in the
  /// out-parameter's byte, and take one step. The instruction there is a
  /// HLT, so the step is the arrival and nothing else. Answers AL.
  [[nodiscard]] std::uint8_t arrive(where which, std::uint8_t key,
                                    std::uint8_t flag) const {
    const bool in_list = which == where::list;
    const std::uint16_t segment =
        in_list ? overlay_segment : image_load_segment;
    const std::uint16_t offset = in_list ? list_point : picker_point;
    const std::uint16_t below =
        in_list ? list_flag_below_bp : picker_flag_below_bp;

    put_byte(physical(segment, offset), 0xF4);
    put_byte(physical(stack_segment, frame_base - below), flag);
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = segment;
    r.ip = offset;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    r.set(cpu::reg8::al, key);
    box->step();
    return r.get(cpu::reg8::al);
  }

  std::unique_ptr<machine> box;
};

constexpr std::array<where, 2> both{where::list, where::picker};

// --- The definition --------------------------------------------------------

TEST(SeamListArrows, IsTwoPointsInOverlay25AndOneInTheResidentImage) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the arrows work, no key to ask";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 3u);

  const seam_point& list = s.points[0];
  EXPECT_EQ(list.offset, list_point);
  EXPECT_FALSE(list.module.is_resident_image());
  EXPECT_EQ(list.module.file, "GAME.OVR");
  EXPECT_EQ(list.module.file_offset, 182479u);
  EXPECT_EQ(list.module.length, 4682u);
  EXPECT_FALSE(list.module.digest.empty())
      << "the overlay is identified by its bytes as well as its place";
  EXPECT_EQ(list.module.load_segment_at, overlay_word)
      << "and by the program's own note of where it is now (#131)";

  const seam_point& picker = s.points[1];
  EXPECT_EQ(picker.offset, picker_point);
  EXPECT_TRUE(picker.module.is_resident_image());

  const seam_point& bars = s.points[2];
  EXPECT_EQ(bars.offset, read_point);
  EXPECT_FALSE(bars.module.is_resident_image());
  EXPECT_EQ(bars.module.file, list.module.file);
  EXPECT_EQ(bars.module.file_offset, list.module.file_offset);
  EXPECT_EQ(bars.module.length, list.module.length);
  EXPECT_EQ(bars.module.digest, list.module.digest)
      << "the same module as the pick-list's, the one `bar-keys` also names";
  EXPECT_EQ(bars.module.load_segment_at, overlay_word);

  for (const seam_point& point : s.points) {
    EXPECT_FALSE(point.at_every_step);
    EXPECT_FALSE(point.inside_calls);
  }
}

TEST(SeamListArrows, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamListArrows, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamListArrows, IsInertWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.manager_says_overlay_at(0);

  EXPECT_EQ(r.box->seams().status(seam_id).reason,
            seam_reason::module_not_resident);
  EXPECT_EQ(r.arrive(where::list, down, 1), down)
      << "the manager's word reads zero while the module is out of memory";
}

// --- The rewrite -----------------------------------------------------------

TEST(SeamListArrows, UpBecomesHomeAndDownBecomesEndWhereTheKeyIsRaw) {
  for (const where which : both) {
    const rig r;
    r.arm();
    EXPECT_EQ(r.arrive(which, up, 1), home);
    EXPECT_EQ(r.arrive(which, down, 1), end);
  }
}

TEST(SeamListArrows, LeavesTheBarsOwnPrevAloneWhereTheKeyIsACommand) {
  // With the byte clear, 0x50 is the bar's own Prev letter, and 0x48 is
  // some letter the bar answered with; neither is an arrow.
  for (const where which : both) {
    const rig r;
    r.arm();
    EXPECT_EQ(r.arrive(which, down, 0), down);
    EXPECT_EQ(r.arrive(which, up, 0), up);
  }
}

TEST(SeamListArrows, PassesEveryOtherKeyThroughWhateverTheByteSays) {
  const std::array<std::uint8_t, 10> others{0x00, home, end, page_up, page_down,
                                            0x0D, 0x1B, 'A', 'N',     0xFF};
  for (const where which : both) {
    const rig r;
    r.arm();
    for (const std::uint8_t key : others) {
      EXPECT_EQ(r.arrive(which, key, 1), key) << int{key};
      EXPECT_EQ(r.arrive(which, key, 0), key) << int{key};
    }
  }
}

TEST(SeamListArrows, TouchesNothingButAL) {
  for (const where which : both) {
    const rig r;
    r.arm();
    (void)r.arrive(which, down, 1);
    cpu::registers after = r.regs();

    const rig plain;
    ASSERT_EQ(plain.box->seams().status(seam_id).state, seam_state::off);
    (void)plain.arrive(which, down, 1);
    cpu::registers untouched = plain.regs();

    // The two machines ran the same instruction; the only difference the
    // seam made is the register it was there to change.
    after.set(cpu::reg8::al, 0);
    untouched.set(cpu::reg8::al, 0);
    for (const cpu::reg16 reg :
         {cpu::reg16::ax, cpu::reg16::bx, cpu::reg16::cx, cpu::reg16::dx,
          cpu::reg16::sp, cpu::reg16::bp, cpu::reg16::si, cpu::reg16::di}) {
      EXPECT_EQ(after[reg], untouched[reg]);
    }
    EXPECT_EQ(after.ip, untouched.ip);
  }
}

TEST(SeamListArrows, DoesNothingWhileItIsOff) {
  for (const where which : both) {
    const rig r;
    EXPECT_EQ(r.arrive(which, up, 1), up);
    EXPECT_EQ(r.arrive(which, down, 1), down);
  }
}

TEST(SeamListArrows, DeclinesAFrameWhoseByteIsNeitherZeroNorOne) {
  for (const where which : both) {
    const rig r;
    r.arm();

    EXPECT_EQ(r.arrive(which, up, 2), up);
    EXPECT_EQ(r.arrive(which, down, 0x80), down);
    EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
    EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::on);
  }
}

TEST(SeamListArrows, ReadsTheByteFromTheStackSegmentNotTheDataSegment) {
  const rig r;
  r.arm();

  // The byte the data segment holds at the same offset says "raw"; the
  // frame's own byte, in SS, says "command". The frame's is the answer.
  r.put_byte(rig::physical(data_segment, frame_base - list_flag_below_bp), 1);
  EXPECT_EQ(r.arrive(where::list, down, 0), down);
}

// --- The command bars: Up and Down become Home and End at a caller ---------

TEST(SeamListArrows, UpBecomesHomeAndDownBecomesEndAtEachAllowedCaller) {
  const rig r;
  r.arm();
  for (const caller_at& c : allowed_callers) {
    EXPECT_EQ(r.press(ring_up, c.segment, c.offset), ring_home) << c.name;
    EXPECT_EQ(r.press(ring_down, c.segment, c.offset), ring_end) << c.name;
  }
}

TEST(SeamListArrows, LeavesAnArrowAloneAtEachCallerThatUsesItOrIsNotItsOwn) {
  const rig r;
  r.arm();
  for (const caller_at& c : left_out_callers) {
    EXPECT_EQ(r.press(ring_up, c.segment, c.offset), ring_up) << c.name;
    EXPECT_EQ(r.press(ring_down, c.segment, c.offset), ring_down) << c.name;
  }
}

TEST(SeamListArrows, LeavesAnArrowAloneAtACallerThatIsNotInTheTable) {
  const rig r;
  r.arm();

  // A made-up caller in a known module, and the pick-list's own call
  // into the menu-bar routine, which has its own point.
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F25), ring_up);
  EXPECT_EQ(r.press(ring_down, camp_segment, 0x1F23), ring_down);
  EXPECT_EQ(r.press(ring_up, overlay_segment, list_point), ring_up);
  EXPECT_EQ(r.press(ring_down, stack_segment, 0), ring_down);
}

TEST(SeamListArrows, TheTableIsAsExactAsTheModuleAndTheOffset) {
  const rig r;
  r.arm();

  // The camp bar's offset in another module's segment is not the camp bar.
  EXPECT_EQ(r.press(ring_up, shop_segment, 0x1F24), ring_up);
  EXPECT_EQ(r.press(ring_down, adventure_segment, 0x1F24), ring_down);
  // And the shop's offset in the camp's.
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x061F), ring_up);

  // While the manager says the module is out of memory, whatever the
  // frame's segment is, the caller is not that module's.
  r.manager_says(word_camp, 0);
  EXPECT_EQ(r.press(ring_up, 0, 0x1F24), ring_up);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_up);
  r.manager_says(word_camp, camp_segment);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_home);
}

TEST(SeamListArrows,
     TheKeypadsEightAndTwoBecomeSevenAndOneAtEachAllowedCaller) {
  const rig r;
  r.arm();
  for (const caller_at& c : allowed_callers) {
    EXPECT_EQ(r.press(ring_pad_8, c.segment, c.offset), ring_pad_7) << c.name;
    EXPECT_EQ(r.press(ring_pad_2, c.segment, c.offset), ring_pad_1) << c.name;
  }
}

TEST(SeamListArrows, LeavesTheKeypadsEightAndTwoWhereTheArrowsAreLeft) {
  const rig r;
  r.arm();
  // The adventuring bars, where the keypad's 8 and 2 walk, and every other
  // caller the table does not name.
  for (const caller_at& c : left_out_callers) {
    EXPECT_EQ(r.press(ring_pad_8, c.segment, c.offset), ring_pad_8) << c.name;
    EXPECT_EQ(r.press(ring_pad_2, c.segment, c.offset), ring_pad_2) << c.name;
  }
  EXPECT_EQ(r.press(ring_pad_8, camp_segment, 0x1F25), ring_pad_8);
  EXPECT_EQ(r.press(ring_pad_2, overlay_segment, list_point), ring_pad_2);
}

TEST(SeamListArrows, LeavesTheNumberRowsDigitsAlone) {
  const rig r;
  r.arm();
  // The number row's 8 and 2 are the same characters under scan codes
  // 0x09 and 0x03, which is how they are told from the keypad's.
  EXPECT_EQ(ring_row(8), 0x0938);
  EXPECT_EQ(ring_row(2), 0x0332);
  for (const caller_at& c : allowed_callers) {
    for (unsigned digit = 0; digit <= 9; ++digit) {
      EXPECT_EQ(r.press(ring_row(digit), c.segment, c.offset), ring_row(digit))
          << c.name << " row " << digit;
    }
  }
}

TEST(SeamListArrows, LeavesTheKeypadsOtherDigitsAlone) {
  const rig r;
  r.arm();
  // With Num Lock on: 0, 9, 3, 4, 6 and 5, then the 7 and 1 the seam writes.
  const std::array<std::uint16_t, 8> others{
      0x5230, 0x4939, 0x5133, 0x4B34, 0x4D36, 0x4C35, ring_pad_7, ring_pad_1};
  for (const caller_at& c : allowed_callers) {
    for (const std::uint16_t key : others) {
      EXPECT_EQ(r.press(key, c.segment, c.offset), key) << c.name;
    }
  }
}

TEST(SeamListArrows, TheKeypadsDigitNeedsTheKeypadsScanCode) {
  const rig r;
  r.arm();
  // The character alone is not enough, nor is the scan code alone: an 8 or
  // a 2 under any other scan code, or the keypad's scan code under another
  // character, is left where it is.
  const std::array<std::uint16_t, 6> not_the_keypads{0x0938, 0x0332, 0x4832,
                                                     0x5038, 0x4B38, 0x4D32};
  for (const caller_at& c : allowed_callers) {
    for (const std::uint16_t key : not_the_keypads) {
      EXPECT_EQ(r.press(key, c.segment, c.offset), key) << c.name;
    }
  }
}

TEST(SeamListArrows, TheKeypadRewriteWritesOnlyTheHeadWord) {
  const rig r;
  r.arm();
  r.put_word(0x40, ring_first + 2, 0xBEEF);
  EXPECT_EQ(r.press(ring_pad_2, camp_segment, 0x1F24), ring_pad_1);
  EXPECT_EQ(r.word(0x40, ring_first + 2), 0xBEEF) << "the key behind it";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first) << "the head did not move";
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 2) << "nor the tail";
}

TEST(SeamListArrows,
     LeavesTheKeypadAloneWhileThePushbackSlotIsArmedOrTheReaderIsOpen) {
  const rig r;
  r.arm();
  r.put_byte(rig::physical(data_segment, data_pushback), 0x48);
  EXPECT_EQ(r.press(ring_pad_8, camp_segment, 0x1F24), ring_pad_8);
  r.put_byte(rig::physical(data_segment, data_pushback), 0);
  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press(ring_pad_2, camp_segment, 0x1F24), ring_pad_2);
  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(r.press(ring_pad_2, camp_segment, 0x1F24), ring_pad_1);
}

TEST(SeamListArrows, LeavesEveryOtherKeyWhereItIsAtAnAllowedCaller) {
  const rig r;
  r.arm();
  const std::array<std::uint16_t, 8> others{
      ring_home,  ring_end,       ring_left,         ring_right,
      ring_enter, 0x1E41 /* A */, 0x4900 /* PgUp */, 0x5100 /* PgDn */};
  for (const caller_at& c : allowed_callers) {
    for (const std::uint16_t key : others) {
      EXPECT_EQ(r.press(key, c.segment, c.offset), key) << c.name;
    }
  }
}

TEST(SeamListArrows, DoesNothingAtTheBarsWhileItIsOff) {
  const rig r;
  r.manager_says_overlay_at(overlay_segment);
  r.manager_says(word_camp, camp_segment);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_up);
  EXPECT_EQ(r.press(ring_down, camp_segment, 0x1F24), ring_down);
}

TEST(SeamListArrows, IsInertAtTheBarsWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.manager_says_overlay_at(0);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_up);
}

TEST(SeamListArrows, LeavesTheRingAloneWhenItIsEmpty) {
  const rig r;
  r.arm();
  r.put_word(stack_segment, frame_base + frame_ip, 0x1F24);
  r.put_word(stack_segment, frame_base + frame_cs, camp_segment);
  r.put_word(0x40, 0x1A, ring_first);
  r.put_word(0x40, 0x1C, ring_first);
  r.put_word(0x40, ring_first, ring_up);
  r.put_byte(rig::physical(overlay_segment, read_point), 0xF4);
  r.box->processor().reset();
  cpu::registers& regs = r.regs();
  regs[cpu::sreg::cs] = overlay_segment;
  regs.ip = read_point;
  regs[cpu::sreg::ds] = data_segment;
  regs[cpu::sreg::ss] = stack_segment;
  regs[cpu::reg16::sp] = 0x0400;
  regs[cpu::reg16::bp] = frame_base;
  r.box->step();

  // The word sits where a key was once; the pointers say there is none.
  EXPECT_EQ(r.word(0x40, ring_first), ring_up);
}

TEST(SeamListArrows, LeavesTheKeyAloneWhileThePushbackSlotIsArmed) {
  const rig r;
  r.arm();
  r.put_byte(rig::physical(data_segment, data_pushback), 0x48);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_up);
}

TEST(SeamListArrows, LeavesTheKeyAloneWhileTheJournalReaderIsOpen) {
  const rig r;
  r.arm();

  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_up);

  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(r.press(ring_up, camp_segment, 0x1F24), ring_home);
}

TEST(SeamListArrows, WritesNothingAtTheBarsButTheHeadWordAndNoRegister) {
  const rig r;
  r.arm();
  r.put_word(0x40, ring_first + 2, 0xBEEF);
  EXPECT_EQ(r.press(ring_down, camp_segment, 0x1F24), ring_end);

  EXPECT_EQ(r.word(0x40, ring_first + 2), 0xBEEF) << "the key behind it";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first) << "the head did not move";
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 2) << "nor the tail";
  EXPECT_EQ(r.regs()[cpu::reg16::bp], frame_base);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], 0x0400);
}

TEST(SeamListArrows, ReadsTheCallersFrameFromTheStackSegment) {
  const rig r;
  r.arm();
  // The data segment holds the camp bar's frame at the same offsets; the
  // stack segment's own frame is another caller's.
  r.put_word(data_segment, frame_base + frame_ip, 0x1F24);
  r.put_word(data_segment, frame_base + frame_cs, camp_segment);
  EXPECT_EQ(r.press(ring_up, rest_segment, 0x076E), ring_up);
}

TEST(SeamListArrows, TheOtherTwoPointsAreUnchangedByTheBarsPoint) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(where::list, down, 1), end);
  EXPECT_EQ(r.arrive(where::picker, up, 1), home);
  EXPECT_EQ(r.arrive(where::list, down, 0), down);
}

}  // namespace
}  // namespace amberfolio::machine
