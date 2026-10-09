// SPDX-License-Identifier: AGPL-3.0-only
//
// The text-entry piece of `modern-controls` (seam_text_entry.cpp, #504),
// exercised through its mechanism and not through any program: the test
// stands the processor on the line editor's entry and on its return, each a
// HLT where the facts put the instruction, and asks `text_entry_now()`
// afterwards. Nothing else is laid out, because the piece reads nothing.
//
// The offsets are restated rather than read out of the seam, which is the
// seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6).

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_entry.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "modern-controls";

/// The piece's two points come last in the seam's table.
constexpr std::size_t piece_count = 2;

/// The line editor's first instruction and its one return, in the resident
/// image, from the image segment.
constexpr std::uint16_t editor_entry = 0x7A73;
constexpr std::uint16_t editor_return = 0x7BF3;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) { load(); }

  void load() const {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
  }

  /// Stand on `offset` in the resident image and take one step; the
  /// instruction there is a HLT.
  void arrive(std::uint16_t offset) const {
    box->memory().ram()[cpu::physical_address(image_load_segment, offset)] =
        0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = offset;
    r[cpu::sreg::ss] = 0x5000;
    r[cpu::reg16::sp] = 0x0400;
    box->step();
  }

  [[nodiscard]] text_entry now() const { return text_entry_now(*box); }

  std::unique_ptr<machine> box;
};

TEST(SeamTextEntry, IsTwoResidentPointsAtTheEndOfTheControlsTable) {
  const rig r;
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  ASSERT_GE(s->points.size(), piece_count);
  const std::span<const seam_point> piece =
      s->points.subspan(s->points.size() - piece_count);
  EXPECT_EQ(piece[0].offset, editor_entry);
  EXPECT_EQ(piece[1].offset, editor_return);
  for (const seam_point& p : piece) {
    EXPECT_TRUE(p.module.is_resident_image());
    EXPECT_FALSE(p.at_every_step);
    EXPECT_FALSE(p.inside_calls);
  }
}

TEST(SeamTextEntry, IsUnknownWithNoProgram) {
  const auto box = std::make_unique<machine>(memory_layout::pc);
  EXPECT_EQ(text_entry_now(*box), text_entry::unknown);
}

TEST(SeamTextEntry, IsUnknownWhileTheControlsAreOff) {
  const rig r;
  EXPECT_EQ(r.now(), text_entry::unknown);
  r.arrive(editor_entry);
  EXPECT_EQ(r.now(), text_entry::unknown) << "nothing was watching";
}

TEST(SeamTextEntry, IsUnknownOnAnyOtherBinary) {
  const auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(text_entry_now(*box), text_entry::unknown);
}

TEST(SeamTextEntry, IsNotReadingUntilTheEditorIsEntered) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.now(), text_entry::not_reading);
}

TEST(SeamTextEntry, ReadsFromTheEditorsEntryUntilItsReturn) {
  const rig r;
  r.arm();
  r.arrive(editor_entry);
  EXPECT_EQ(r.now(), text_entry::reading);
  r.arrive(editor_return);
  EXPECT_EQ(r.now(), text_entry::not_reading);
  r.arrive(editor_entry);
  EXPECT_EQ(r.now(), text_entry::reading) << "a second line, a second entry";
}

TEST(SeamTextEntry, ForgetsAReadingWhenTheControlsGoOffAndOnAgain) {
  const rig r;
  r.arm();
  r.arrive(editor_entry);
  ASSERT_EQ(r.now(), text_entry::reading);
  ASSERT_EQ(r.box->seams().disable(seam_id), seam_reason::none);
  EXPECT_EQ(r.now(), text_entry::unknown);
  r.arm();
  EXPECT_EQ(r.now(), text_entry::not_reading)
      << "the return it missed is not a reading it may still claim";
}

TEST(SeamTextEntry, ForgetsAReadingWhenTheNextProgramLoads) {
  const rig r;
  r.arm();
  r.arrive(editor_entry);
  ASSERT_EQ(r.now(), text_entry::reading);
  r.load();
  EXPECT_EQ(r.now(), text_entry::unknown) << "a loaded program starts off";
  r.arm();
  EXPECT_EQ(r.now(), text_entry::not_reading);
}

TEST(SeamTextEntry, WritesNothingOfTheMachines) {
  const rig on;
  on.arm();
  const rig off;
  for (const std::uint16_t at : {editor_entry, editor_return}) {
    on.arrive(at);
    off.arrive(at);
    const std::span<const std::uint8_t> a = on.box->memory().ram();
    const std::span<const std::uint8_t> b = off.box->memory().ram();
    ASSERT_EQ(a.size(), b.size());
    EXPECT_TRUE(std::equal(a.begin(), a.end(), b.begin())) << at;
    EXPECT_EQ(on.box->processor().regs().ip, off.box->processor().regs().ip);
    EXPECT_EQ(on.box->processor().regs()[cpu::reg16::sp],
              off.box->processor().regs()[cpu::reg16::sp]);
  }
}

}  // namespace
}  // namespace amberfolio::machine
