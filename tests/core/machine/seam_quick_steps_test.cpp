// SPDX-License-Identifier: AGPL-3.0-only
//
// The quick-steps seam (seam_quick_steps.cpp), exercised through its
// mechanism and not through any program: the test stands the processor on
// the sound dispatch's busy-word test with the routine's frame on the stack,
// the request and a far return in it, the footstep's word in the data
// segment and the overlays' segments in the words the program's overlay
// manager keeps them in, and reads where the step left IP.
//
// The offsets are restated rather than read out of the seam, which is the
// seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6): the instruction at the point is a HLT whose next two
// bytes are the operand the seam checks, which is all it reads of the code.

#include <cstdint>
#include <memory>
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

constexpr std::string_view seam_id = "quick-steps";

/// The busy-word test and the routine's epilogue, from the image segment.
/// Both are HLTs here.
constexpr std::uint16_t bracket = 0x8C36;
constexpr std::uint16_t epilogue = 0x8C57;

/// The data segment, the busy word and the footstep's request word.
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xC7C);
constexpr std::uint16_t data_busy = 0x5374;
constexpr std::uint16_t data_footstep = 0x2628;

/// The frame: BP and SP equal, the request six bytes up.
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame = 0x0400;

constexpr std::uint16_t footstep = 0x000A;

/// The two overlays' load-segment words, from the image segment, the
/// segments the test puts in them, and the call sites' far returns.
constexpr std::uint16_t input_word = 0x730;
constexpr std::uint16_t loop_word = 0x200;
constexpr std::uint16_t input_segment = 0x2600;
constexpr std::uint16_t loop_segment = 0x2D00;

struct site {
  std::uint16_t segment;
  std::uint16_t offset;
};
constexpr site turn_left{.segment = input_segment, .offset = 0x0B19};
constexpr site turn_right{.segment = input_segment, .offset = 0x0B5E};
constexpr site step_commit{.segment = input_segment, .offset = 0x0747};
constexpr site after_redraw{.segment = loop_segment, .offset = 0x34D8};
constexpr site script{.segment = loop_segment, .offset = 0x2D91};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    put_word(data_segment, data_footstep, footstep);
    put_word(data_segment, data_busy, 1);
    put_word(image_load_segment, input_word, input_segment);
    put_word(image_load_segment, loop_word, loop_segment);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
  }

  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] =
        static_cast<std::uint8_t>(value);
    box->memory().ram()[cpu::physical_address(
        segment, static_cast<std::uint16_t>(offset + 1))] =
        static_cast<std::uint8_t>(value >> 8U);
  }

  /// Stand on the test with `request` in the frame and take one step;
  /// answer where IP went.
  [[nodiscard]] std::uint16_t arrive(std::uint16_t request,
                                     site from = turn_left,
                                     std::uint16_t operand = data_busy,
                                     std::uint16_t ds = data_segment,
                                     std::uint16_t bp = frame) const {
    box->memory().ram()[cpu::physical_address(image_load_segment, bracket)] =
        0xF4;
    box->memory().ram()[cpu::physical_address(image_load_segment, epilogue)] =
        0xF4;
    put_word(image_load_segment, static_cast<std::uint16_t>(bracket + 2),
             operand);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 2), from.offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 4),
             from.segment);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 6), request);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = bracket;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = frame;
    r[cpu::reg16::bp] = bp;
    box->step();
    return box->processor().regs().ip;
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().status(seam_id);
  }

  std::unique_ptr<machine> box;
};

/// Where a HLT at the test leaves IP, past it, the program's own way; and
/// where the HLT at the epilogue does, past that.
constexpr std::uint16_t untouched = bracket + 1;
constexpr std::uint16_t skipped = epilogue + 1;

TEST(SeamQuickSteps, IsOneResidentPoint) {
  const rig r;
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  ASSERT_EQ(s->points.size(), 1U);
  EXPECT_TRUE(s->points[0].module.is_resident_image());
  EXPECT_EQ(s->points[0].offset, bracket);
  EXPECT_FALSE(s->trigger);
  EXPECT_TRUE(s->group.empty());
}

TEST(SeamQuickSteps, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.arrive(footstep), untouched);
}

TEST(SeamQuickSteps, SkipsTheTurnsWaitsAndTheOneAfterTheRedraw) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep, turn_left), skipped);
  EXPECT_EQ(r.arrive(footstep, turn_right), skipped);
  EXPECT_EQ(r.arrive(footstep, after_redraw), skipped);
}

TEST(SeamQuickSteps, LeavesTheStepsFirstClickAndAScriptsToWait) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep, step_commit), untouched);
  EXPECT_EQ(r.arrive(footstep, script), untouched);
}

TEST(SeamQuickSteps, FollowsTheOverlayManagersWord) {
  const rig r;
  r.arm();
  r.put_word(image_load_segment, input_word, 0x3100);
  EXPECT_EQ(r.arrive(footstep, turn_left), untouched)
      << "overlay 14 is somewhere else, so this is not its code";
  EXPECT_EQ(
      r.arrive(footstep, site{.segment = 0x3100, .offset = turn_left.offset}),
      skipped);
  r.put_word(image_load_segment, input_word, 0);
  EXPECT_EQ(r.arrive(footstep, site{.segment = 0, .offset = turn_left.offset}),
            untouched)
      << "a module that is not loaded has no call sites";
}

TEST(SeamQuickSteps, LeavesEveryOtherSoundToWait) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep + 1), untouched);
  EXPECT_EQ(r.arrive(0), untouched);
}

TEST(SeamQuickSteps, FollowsTheProgramsWordAndNotANumber) {
  const rig r;
  r.arm();
  r.put_word(data_segment, data_footstep, 0x0007);
  EXPECT_EQ(r.arrive(footstep), untouched);
  EXPECT_EQ(r.arrive(0x0007), skipped);
}

TEST(SeamQuickSteps, DeclinesAnInstructionThatIsNotTheTest) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep, turn_left, data_busy + 2), untouched);
  EXPECT_EQ(r.status().fired, 0U);
}

TEST(SeamQuickSteps, DeclinesAnotherDataSegment) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep, turn_left, data_busy, data_segment + 1),
            untouched);
}

TEST(SeamQuickSteps, DeclinesAFrameThatIsNotTheRoutines) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(footstep, turn_left, data_busy, data_segment, frame - 2),
            untouched);
}

TEST(SeamQuickSteps, IsUnavailableOnAnyOtherBinary) {
  const auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_NE(box->seams().enable(seam_id), seam_reason::none);
}

}  // namespace
}  // namespace amberfolio::machine
