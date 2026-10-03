// SPDX-License-Identifier: AGPL-3.0-only
//
// The list-arrows seam (seam_list_arrows.cpp, #423), exercised through its
// mechanism and not through any program: the test stands the processor on
// each of the seam's two points, with a key in AL and the menu-bar
// routine's out-parameter in the frame where the facts say it is, and
// reads AL after one step.
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

/// Where each routine keeps the menu-bar routine's out-parameter, as a
/// distance below its BP.
constexpr std::uint16_t list_flag_below_bp = 0x57;
constexpr std::uint16_t picker_flag_below_bp = 0x2B;

/// The word the program's overlay manager keeps overlay 25's segment in.
constexpr std::uint32_t overlay_word = 0x3C60;

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

TEST(SeamListArrows, IsOnePointInOverlay25AndOneInTheResidentImage) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the arrows work, no key to ask";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 2u);

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

}  // namespace
}  // namespace amberfolio::machine
