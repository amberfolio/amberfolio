// SPDX-License-Identifier: AGPL-3.0-only
//
// The edit-keys seam (seam_edit_keys.cpp, #455), exercised through its
// mechanism and not through any program: the test stands the processor on
// the seam's one point, with the editor's key in AL and the keyboard read's
// pushback slot in the data segment where the facts say it is, and reads the
// slot after one step.
//
// The offsets below are restated rather than read out of the seam, which is
// the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6).

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

constexpr std::string_view seam_id = "edit-keys";

/// Where the point is: the resident image, from the image segment. It is the
/// instruction after the editor's call into the program's key read, which
/// stores AL.
constexpr std::uint16_t editor_point = 0x7AC0;

/// The data segment, in paragraphs after the image segment, and the keyboard
/// read's slot for an extended key's second half.
constexpr std::uint16_t dgroup_paragraphs = 0x0C7C;
constexpr std::uint16_t data_pushback = 0x8501;

/// The stack and the keyboard's own segment, apart from the data segment so
/// a wrong segment register shows up as a wrong answer.
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t ring_first = 0x1E;

/// The keys' scan codes: the ones the issue names, and every other.
constexpr std::uint8_t scan_home = 0x47;
constexpr std::uint8_t scan_up = 0x48;
constexpr std::uint8_t scan_page_up = 0x49;
constexpr std::uint8_t scan_left = 0x4B;
constexpr std::uint8_t scan_right = 0x4D;
constexpr std::uint8_t scan_end = 0x4F;
constexpr std::uint8_t scan_down = 0x50;

constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);

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

  [[nodiscard]] std::uint8_t byte(std::uint16_t segment,
                                  std::uint16_t offset) const {
    return box->memory().ram()[physical(segment, offset)];
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    return static_cast<std::uint16_t>(byte(segment, offset) |
                                      (byte(segment, offset + 1U) << 8U));
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// Stand on the point with `al` in AL and `pushback` in the slot, DS as
  /// given, and take one step. The instruction there is a HLT, so the step
  /// is the arrival and nothing else. Answers the slot afterwards.
  [[nodiscard]] std::uint8_t arrive(std::uint8_t al, std::uint8_t pushback,
                                    std::uint16_t ds = data_segment) const {
    put_byte(physical(image_load_segment, editor_point), 0xF4);
    put_byte(physical(data_segment, data_pushback), pushback);
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = editor_point;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r.set(cpu::reg8::al, al);
    box->step();
    return byte(data_segment, data_pushback);
  }

  std::unique_ptr<machine> box;
};

// --- The definition --------------------------------------------------------

TEST(SeamEditKeys, IsOnePointInTheResidentImage) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the arrows are dropped, no key to ask";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 1u);

  const seam_point& point = s.points[0];
  EXPECT_EQ(point.offset, editor_point);
  EXPECT_TRUE(point.module.is_resident_image());
  EXPECT_FALSE(point.at_every_step);
  EXPECT_FALSE(point.inside_calls);
}

TEST(SeamEditKeys, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamEditKeys, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamEditKeys, IsArmedAtOnceBecauseTheResidentImageIsNeverOut) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::on);
  EXPECT_TRUE(r.box->seams().status(seam_id).armed);
}

// --- The first half of an extended key -------------------------------------

TEST(SeamEditKeys, ThrowsAwayTheScanCodeOfEveryExtendedKey) {
  const rig r;
  r.arm();
  // Every scan code the keyboard read can have kept: the arrows, Home, End,
  // the page keys, Insert, Delete, the function keys and the Alt
  // combinations, none of which the editor is to be handed.
  for (unsigned scan = 1; scan <= 0xFF; ++scan) {
    EXPECT_EQ(r.arrive(0, static_cast<std::uint8_t>(scan)), 0u)
        << "scan code " << scan;
  }
}

TEST(SeamEditKeys, ThrowsAwayTheFourArrowsAndTheKeysBesideThem) {
  const rig r;
  r.arm();
  for (const std::uint8_t scan : {scan_up, scan_down, scan_left, scan_right,
                                  scan_home, scan_end, scan_page_up}) {
    EXPECT_EQ(r.arrive(0, scan), 0u) << int{scan};
  }
}

TEST(SeamEditKeys, LeavesTheEditorTheZeroItIgnoresAnyway) {
  const rig r;
  r.arm();
  (void)r.arrive(0, scan_right);
  EXPECT_EQ(r.regs().get(cpu::reg8::al), 0u)
      << "the editor is handed the zero, as it ignores any control character";
}

TEST(SeamEditKeys, ReachesTheEditorsPointAndActs) {
  const rig r;
  r.arm();
  (void)r.arrive(0, scan_right);
  EXPECT_EQ(r.box->seams().status(seam_id).fired, 1u);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

// --- Everything else is the program's --------------------------------------

TEST(SeamEditKeys, LeavesEveryCharacterTheEditorActsOnAlone) {
  const rig r;
  r.arm();
  // Typing, Backspace, Return and Esc, and the keys the editor ignores
  // that are not extended: with the slot empty, as the read leaves it.
  for (unsigned al = 1; al <= 0xFF; ++al) {
    EXPECT_EQ(r.arrive(static_cast<std::uint8_t>(al), 0), 0u) << al;
    EXPECT_EQ(r.regs().get(cpu::reg8::al), al) << al;
  }
}

TEST(SeamEditKeys, LeavesTheScanCodeAnExtendedKeyAnswersWithAlone) {
  // The second read of an extended key answers the scan code and has already
  // emptied the slot: the seam never takes the second half, because it has
  // taken the slot at the first.
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(scan_right, 0), 0u);
  EXPECT_EQ(r.regs().get(cpu::reg8::al), scan_right);
}

TEST(SeamEditKeys, ReadsNoSlotForACharacter) {
  const rig r;
  r.arm();
  // A non-zero AL with the slot armed is not a state the keyboard read
  // makes; whatever it is, it is not the first half of an extended key.
  EXPECT_EQ(r.arrive('A', scan_right), scan_right);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamEditKeys, LeavesAnEmptySlotAlone) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(0, 0), 0u) << "a zero with nothing kept: a Ctrl-Break "
                                   "marker or a read the seam never saw begin";
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamEditKeys, DoesNothingWhileItIsOff) {
  const rig r;
  EXPECT_EQ(r.arrive(0, scan_right), scan_right);
  EXPECT_EQ(r.arrive(0, scan_up), scan_up);
}

TEST(SeamEditKeys, StopsWhenItIsDisabled) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.arrive(0, scan_up), 0u);
  r.box->seams().disable(seam_id);
  EXPECT_EQ(r.arrive(0, scan_up), scan_up);
}

// --- The guard -------------------------------------------------------------

TEST(SeamEditKeys, DeclinesADataSegmentThatIsNotTheOneTheFactsName) {
  const rig r;
  r.arm();

  EXPECT_EQ(r.arrive(0, scan_right, 0x3000), scan_right)
      << "the slot is not written through a segment the facts do not name";
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::on);
}

TEST(SeamEditKeys, DoesNotLookAtTheSegmentForACharacter) {
  const rig r;
  r.arm();
  (void)r.arrive('A', 0, 0x3000);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u)
      << "a character is nothing the seam acts on, whatever DS holds";
}

// --- What it touches -------------------------------------------------------

TEST(SeamEditKeys, TouchesNothingButTheSlot) {
  const rig r;
  r.arm();
  // The BIOS ring is the keyboard's, not the seam's: a key behind the one
  // the editor was handed is where it was.
  r.put_word(0x40, 0x1A, ring_first);
  r.put_word(0x40, 0x1C, ring_first + 2);
  r.put_word(0x40, ring_first, 0x4D00);
  (void)r.arrive(0, scan_up);
  cpu::registers after = r.regs();

  EXPECT_EQ(r.word(0x40, 0x1A), ring_first);
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 2);
  EXPECT_EQ(r.word(0x40, ring_first), 0x4D00u);

  const rig plain;
  (void)plain.arrive(0, scan_up);
  const cpu::registers untouched = plain.regs();
  for (const cpu::reg16 reg :
       {cpu::reg16::ax, cpu::reg16::bx, cpu::reg16::cx, cpu::reg16::dx,
        cpu::reg16::sp, cpu::reg16::bp, cpu::reg16::si, cpu::reg16::di}) {
    EXPECT_EQ(after[reg], untouched[reg]);
  }
  EXPECT_EQ(after.ip, untouched.ip) << "the instruction there runs as it was";
}

TEST(SeamEditKeys, ThrowsAwayOnlyTheSlotAndNotTheByteBesideIt) {
  const rig r;
  r.arm();
  r.put_byte(rig::physical(data_segment, data_pushback - 1), 0xA5);
  r.put_byte(rig::physical(data_segment, data_pushback + 1), 0x5A);
  (void)r.arrive(0, scan_down);
  EXPECT_EQ(r.byte(data_segment, data_pushback - 1), 0xA5u);
  EXPECT_EQ(r.byte(data_segment, data_pushback + 1), 0x5Au);
}

}  // namespace
}  // namespace amberfolio::machine
