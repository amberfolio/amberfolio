// SPDX-License-Identifier: AGPL-3.0-only
//
// The timer fix (seam_timer_fix.cpp), a built-in fix: on whenever the
// program it names is loaded, and listed nowhere. Exercised through its
// mechanism and not through any program: the test stands the processor on
// the sound driver's install routine with the busy word and the timer's
// vector set as each case needs, and reads where the step left IP.
//
// The offsets are restated rather than read out of the fix, which is the
// seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6): the routine is a HLT at its entry, a `mov dx` opcode
// and the handler's offset where the fix reads them, and a `retf` back to
// a caller the frame names.

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

constexpr std::string_view seam_id = "timer-fix";

/// The install routine's entry from the image segment, and the driver's
/// segment the test calls it in: the routine at its offset there.
constexpr std::uint32_t entry = 0x9E5C;
constexpr std::uint16_t driver_segment =
    static_cast<std::uint16_t>(image_load_segment + 0x8AE);
constexpr std::uint16_t entry_ip = static_cast<std::uint16_t>(
    entry - ((driver_segment - image_load_segment) * 16U));

/// From the entry: the `mov dx` that loads the handler's offset, and the
/// `retf`.
constexpr std::uint16_t handler_load = 0x1C;
constexpr std::uint16_t install_return = 0x34;
constexpr std::uint16_t handler = 0x13EA;

/// The data segment and the busy word.
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xC7C);
constexpr std::uint16_t data_busy = 0x5374;

/// The caller's far return, in the frame at SP.
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame = 0x0400;
constexpr std::uint16_t caller = 0x0200;

/// A BIOS's timer handler, somewhere that is not the driver.
constexpr std::uint16_t bios_segment = 0xF000;
constexpr std::uint16_t bios_handler = 0xFEA5;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    put_byte(driver_segment, entry_ip, 0xF4);
    put_byte(driver_segment, at(handler_load), 0xBA);
    put_word(driver_segment, at(handler_load + 1), handler);
    put_byte(driver_segment, at(install_return), 0xCB);
    busy(1);
    hooked_by(bios_segment, bios_handler);
  }

  [[nodiscard]] static std::uint16_t at(int delta) {
    return static_cast<std::uint16_t>(entry_ip + delta);
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

  void busy(std::uint16_t word) const {
    put_word(data_segment, data_busy, word);
  }

  void hooked_by(std::uint16_t segment, std::uint16_t offset) const {
    put_word(0, 0x20, offset);
    put_word(0, 0x22, segment);
  }

  /// Stand on the routine's entry and take one step; answer where IP went.
  [[nodiscard]] std::uint16_t arrive() const {
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = driver_segment;
    r.ip = entry_ip;
    r[cpu::sreg::ds] = data_segment;
    put_word(stack_segment, frame, caller);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 2),
             driver_segment);
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = frame;
    box->step();
    return box->processor().regs().ip;
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().fix_status(seam_id);
  }

  std::unique_ptr<machine> box;
};

/// Where the HLT at the entry leaves IP, the program's own way; and where
/// the `retf` does, back in the caller.
constexpr std::uint16_t untouched = entry_ip + 1;
constexpr std::uint16_t returned = caller;

TEST(SeamTimerFix, IsOnePointAtTheInstallRoutinesEntry) {
  const seam_definition* fix = nullptr;
  for (const seam_definition& s : built_in_fixes()) {
    if (s.id == seam_id) {
      fix = &s;
    }
  }
  ASSERT_NE(fix, nullptr);
  EXPECT_FALSE(fix->about.empty());
  EXPECT_FALSE(fix->trigger);
  EXPECT_TRUE(fix->group.empty());
  ASSERT_EQ(fix->points.size(), 1U);
  EXPECT_TRUE(fix->points[0].module.is_resident_image());
  EXPECT_EQ(fix->points[0].offset, entry);
}

TEST(SeamTimerFix, IsOnAsSoonAsTheProgramIsLoadedAndListedNowhere) {
  const rig r;
  EXPECT_EQ(r.status().state, seam_state::on);
  EXPECT_EQ(r.box->seams().find(seam_id), nullptr);
  EXPECT_EQ(r.box->seams().enable(seam_id), seam_reason::unknown_seam);
  EXPECT_EQ(r.box->seams().disable(seam_id), seam_reason::unknown_seam);
}

TEST(SeamTimerFix, LeavesTheBiosesVectorToBeKept) {
  const rig r;
  EXPECT_EQ(r.arrive(), untouched);
}

TEST(SeamTimerFix, ReturnsWhenTheDriverIsAlreadyHooked) {
  const rig r;
  r.hooked_by(driver_segment, handler);
  EXPECT_EQ(r.arrive(), returned);
}

TEST(SeamTimerFix, LeavesTheResidentModeToTheProgram) {
  const rig r;
  r.hooked_by(driver_segment, handler);
  r.busy(0);
  EXPECT_EQ(r.arrive(), untouched);
}

TEST(SeamTimerFix, ComparesTheWholeVector) {
  const rig r;
  r.hooked_by(bios_segment, handler);
  EXPECT_EQ(r.arrive(), untouched);
  r.hooked_by(driver_segment, bios_handler);
  EXPECT_EQ(r.arrive(), untouched);
}

TEST(SeamTimerFix, ReadsTheHandlerFromTheRoutine) {
  const rig r;
  r.put_word(driver_segment, rig::at(handler_load + 1), 0x1400);
  r.hooked_by(driver_segment, 0x1400);
  EXPECT_EQ(r.arrive(), returned);
}

TEST(SeamTimerFix, DeclinesARoutineThatIsNotTheInstall) {
  const rig r;
  r.hooked_by(driver_segment, handler);
  r.put_byte(driver_segment, rig::at(handler_load), 0x90);
  EXPECT_EQ(r.arrive(), untouched);
  EXPECT_EQ(r.status().declined, 1U);

  const rig s;
  s.hooked_by(driver_segment, handler);
  s.put_byte(driver_segment, rig::at(install_return), 0x90);
  EXPECT_EQ(s.arrive(), untouched);
  EXPECT_EQ(s.status().declined, 1U);
}

TEST(SeamTimerFix, IsUnavailableOnAnyOtherBinary) {
  const auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(box->seams().fix_status(seam_id).state, seam_state::unavailable);
}

}  // namespace
}  // namespace amberfolio::machine
