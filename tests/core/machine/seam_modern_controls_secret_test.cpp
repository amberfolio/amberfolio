// SPDX-License-Identifier: AGPL-3.0-only
//
// modern-controls on Secret of the Silver Blades (#609): the two pieces
// that observe, `text-entry` and `aim-watch`, with points of their own and
// no piece that changes a key or a colour. Each test stands the processor
// on one of the program's points (a HLT there, so the step is the arrival
// and nothing else) and reads what the handler left; the scene's side of
// the aim is in scene_test.cpp.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. **Every byte here is this
// file's own** (PLAN.md §6).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_entry.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";
constexpr std::string_view secret_fingerprint =
    "8b937984878ec72aa531335dd381a21bda67675408b5fd8e716a35ccd049999e";

// --- Secret's facts, restated -----------------------------------------------

/// The line editor's first instruction and its one return, in the resident
/// image, from the image segment.
constexpr std::uint16_t editor_entry = 0x5B48;
constexpr std::uint16_t editor_return = 0x5D87;

/// The word the overlay manager keeps overlay 16's segment in, and the aim
/// bar's five points there: target to target's meeting point and its wait's
/// return, then manual's halving, meeting point and its wait's return.
constexpr std::uint16_t word_aim = 0xCA0;
constexpr std::array<std::uint16_t, 5> aim_points{0x2DE8, 0x2E70, 0x31CB,
                                                  0x337C, 0x33DA};

/// Where this test puts the overlay and a stack.
constexpr std::uint16_t aim_segment = 0x6C00;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

[[nodiscard]] sha256_digest secret_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(secret_fingerprint, digest));
  return digest;
}

struct secret_rig {
  secret_rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(secret_digest(), image_load_segment);
    const std::uint32_t word =
        cpu::physical_address(image_load_segment, word_aim);
    box->memory().ram()[word] = static_cast<std::uint8_t>(aim_segment & 0xFFU);
    box->memory().ram()[word + 1] =
        static_cast<std::uint8_t>(aim_segment >> 8U);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
  }

  /// Stand on `segment:offset` with BP on a frame of zeros, and take one
  /// step; the instruction there is a HLT.
  void arrive(std::uint16_t segment, std::uint16_t offset) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = segment;
    r.ip = offset;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::bp] = frame_base;
    r[cpu::reg16::sp] = static_cast<std::uint16_t>(frame_base - 0x300);
    box->step();
  }

  std::unique_ptr<machine> box;
};

[[nodiscard]] const seam_definition& definition() {
  for (const seam_definition& s : all_seams()) {
    if (s.id == seam_id) {
      return s;
    }
  }
  ADD_FAILURE() << "no seam is called " << seam_id;
  return all_seams().front();
}

TEST(SeamModernControlsOnSecret, IsTheTwoPiecesThatObserve) {
  const std::span<const seam_point> mine =
      points_on(definition(), secret_digest());
  // Text entry 2, aim watch 5, and nothing that takes a key or draws.
  ASSERT_EQ(mine.size(), 7U);
  EXPECT_NE(mine.data(), definition().points.data());
  EXPECT_EQ(mine[0].offset, editor_entry);
  EXPECT_EQ(mine[1].offset, editor_return);
  for (std::size_t i = 0; i < 2; ++i) {
    EXPECT_TRUE(mine[i].module.is_resident_image()) << i;
  }
  for (std::size_t i = 0; i < aim_points.size(); ++i) {
    EXPECT_EQ(mine[2 + i].offset, aim_points[i]) << i;
    EXPECT_EQ(mine[2 + i].module.load_segment_at, word_aim) << i;
  }
  for (const seam_point& p : mine) {
    EXPECT_FALSE(p.at_every_step);
    EXPECT_FALSE(p.inside_calls);
  }
}

TEST(SeamModernControlsOnSecret, IsUnknownUntilItIsOn) {
  const secret_rig r;
  EXPECT_EQ(text_entry_now(*r.box), text_entry::unknown);
  r.arrive(image_load_segment, editor_entry);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::unknown)
      << "nothing was watching";
}

TEST(SeamModernControlsOnSecret, NotesTheLineEditorFromEntryToReturn) {
  const secret_rig r;
  r.arm();
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
  r.arrive(image_load_segment, editor_entry);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::reading);
  r.arrive(image_load_segment, editor_return);
  EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading);
}

TEST(SeamModernControlsOnSecret, TheOtherProgramsEditorsAreNotPointsHere) {
  const secret_rig r;
  r.arm();
  // Pool of Radiance's and Curse of the Azure Bonds' editor entries.
  for (const std::uint16_t other :
       {std::uint16_t{0x7A73}, std::uint16_t{0x53F6}}) {
    r.arrive(image_load_segment, other);
    EXPECT_EQ(text_entry_now(*r.box), text_entry::not_reading) << other;
  }
}

TEST(SeamModernControlsOnSecret, WritesNothingOfTheMachines) {
  // Every point, with the seam on and with it off: the same memory and the
  // same registers after the step, which is the whole of its fidelity
  // claim on this program.
  const secret_rig on;
  on.arm();
  const secret_rig off;
  const auto same = [&](std::uint16_t segment, std::uint16_t offset) {
    on.arrive(segment, offset);
    off.arrive(segment, offset);
    const std::span<const std::uint8_t> a = on.box->memory().ram();
    const std::span<const std::uint8_t> b = off.box->memory().ram();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin())) << offset;
    EXPECT_EQ(on.box->processor().regs().ip, off.box->processor().regs().ip);
    EXPECT_EQ(on.box->processor().regs()[cpu::reg16::sp],
              off.box->processor().regs()[cpu::reg16::sp]);
    EXPECT_EQ(on.box->processor().regs()[cpu::reg16::ax],
              off.box->processor().regs()[cpu::reg16::ax]);
  };
  same(image_load_segment, editor_entry);
  same(image_load_segment, editor_return);
  for (const std::uint16_t point : aim_points) {
    same(aim_segment, point);
  }
}

}  // namespace
}  // namespace amberfolio::machine
