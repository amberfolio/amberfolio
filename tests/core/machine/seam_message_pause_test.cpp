// SPDX-License-Identifier: AGPL-3.0-only
//
// The message-pause seam (seam_message_pause.cpp), exercised through its
// mechanism and not through any program: the test stands the processor
// on the pause's `push ax` with the routine's frame on the stack and on
// `Delay`'s countdown with a far return on it, puts keys in the BIOS
// buffer and the approach's count in a game-state block, and reads what
// the step left in AX, DX and the buffer. And the setting the seam takes
// (`seam_setting`, #531), through the engine that keeps it and the
// recording line that carries it.
//
// The offsets are restated rather than read out of the seam, which is the
// seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. **Every byte here is this file's
// own** (PLAN.md §6): the instruction at each point is a HLT.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "message-pause";

/// The pause's `push ax`, its call's far return, and `Delay`'s countdown,
/// from the image segment.
constexpr std::uint16_t push_point = 0x7E5B;
constexpr std::uint16_t pause_return = 0x7E61;
constexpr std::uint16_t countdown = 0xA6B7;

constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xC7C);

/// Overlay 3's load-segment word, the segment the test puts there, and
/// the script opcode's far return.
constexpr std::uint16_t script_word = 0x200;
constexpr std::uint16_t script_segment = 0x2D00;
constexpr std::uint16_t script_return = 0x272B;

/// Overlay 31's load-segment word, the segment the test puts there, and
/// the death routine's two far returns.
constexpr std::uint16_t death_word = 0x3E10;
constexpr std::uint16_t death_segment = 0x2E00;
constexpr std::uint16_t death_in_a_fight = 0x1018;
constexpr std::uint16_t death_elsewhere = 0x0E34;

/// The game-state block's far pointer, a block, and the approach's count.
constexpr std::uint16_t data_state_block = 0x49D6;
constexpr std::uint16_t block_segment = 0x4000;
constexpr std::uint16_t block_offset = 0x0010;
constexpr std::uint16_t approach_frames = 0x582;

constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame = 0x0400;

/// A combat line's caller, somewhere in an overlay the seam knows nothing
/// of.
constexpr std::uint16_t other_segment = 0x3300;
constexpr std::uint16_t other_return = 0x0123;

/// The game's pause at speed 1, and at speed 9.
constexpr std::uint16_t speed_one = 225;
constexpr std::uint16_t speed_nine = 2025;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    put_word(image_load_segment, script_word, script_segment);
    put_word(image_load_segment, death_word, death_segment);
    put_word(data_segment, data_state_block, block_offset);
    put_word(data_segment, static_cast<std::uint16_t>(data_state_block + 2),
             block_segment);
    put_word(bda::segment, bda::keyboard_buffer_head, bda::keyboard_buffer);
    put_word(bda::segment, bda::keyboard_buffer_tail, bda::keyboard_buffer);
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

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        box->memory().ram()[cpu::physical_address(segment, offset)] |
        (box->memory().ram()[cpu::physical_address(
             segment, static_cast<std::uint16_t>(offset + 1))]
         << 8U));
  }

  void set_approach(std::uint16_t frames) const {
    put_word(block_segment,
             static_cast<std::uint16_t>(block_offset + approach_frames),
             frames);
  }

  void type(std::uint16_t key) const {
    const std::uint16_t tail = word(bda::segment, bda::keyboard_buffer_tail);
    put_word(bda::segment, tail, key);
    put_word(bda::segment, bda::keyboard_buffer_tail,
             static_cast<std::uint16_t>(tail + 2));
  }

  [[nodiscard]] unsigned keys_waiting() const {
    return static_cast<unsigned>(
               word(bda::segment, bda::keyboard_buffer_tail) -
               word(bda::segment, bda::keyboard_buffer_head)) /
           2U;
  }

  /// Stand on the `push ax` with `ax` in it and the far return of the
  /// pause's caller in its frame, and take one step; answer AX.
  [[nodiscard]] std::uint16_t pause(std::uint16_t ax,
                                    std::uint16_t from_segment = other_segment,
                                    std::uint16_t from_offset = other_return,
                                    std::uint16_t ds = data_segment,
                                    std::uint16_t bp = frame) const {
    box->memory().ram()[cpu::physical_address(image_load_segment, push_point)] =
        0xF4;
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 2), from_offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 4),
             from_segment);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = push_point;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = frame;
    r[cpu::reg16::bp] = bp;
    r[cpu::reg16::ax] = ax;
    box->step();
    return box->processor().regs()[cpu::reg16::ax];
  }

  /// Stand on `Delay`'s countdown with `dx` milliseconds left,
  /// `returns_to` (from the image segment) as its far return, and the
  /// pause's own frame six bytes up with its caller's far return in it;
  /// answer DX.
  [[nodiscard]] std::uint16_t count_down(
      std::uint16_t dx, std::uint16_t returns_to = pause_return,
      std::uint16_t ds = data_segment,
      std::uint16_t from_segment = other_segment,
      std::uint16_t from_offset = other_return) const {
    box->memory().ram()[cpu::physical_address(image_load_segment, countdown)] =
        0xF4;
    put_word(stack_segment, frame, returns_to);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 2),
             image_load_segment);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 8), from_offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame + 10),
             from_segment);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = countdown;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = frame;
    r[cpu::reg16::bp] = static_cast<std::uint16_t>(frame + 6);
    r[cpu::reg16::dx] = dx;
    box->step();
    return box->processor().regs()[cpu::reg16::dx];
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().status(seam_id);
  }

  std::unique_ptr<machine> box;
};

TEST(SeamMessagePause, IsTwoResidentPointsAndANumber) {
  const rig r;
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  ASSERT_EQ(s->points.size(), 2U);
  EXPECT_TRUE(s->points[0].module.is_resident_image());
  EXPECT_EQ(s->points[0].offset, push_point);
  EXPECT_TRUE(s->points[1].module.is_resident_image());
  EXPECT_EQ(s->points[1].offset, countdown);
  EXPECT_FALSE(s->trigger);
  EXPECT_TRUE(s->group.empty());
  ASSERT_TRUE(s->setting.present());
  EXPECT_EQ(s->setting.name, "pause");
  EXPECT_EQ(s->setting.unit, "ms");
  EXPECT_EQ(s->setting.minimum, 0U);
  EXPECT_EQ(s->setting.maximum, 10000U);
  EXPECT_EQ(s->setting.step, 250U);
  EXPECT_EQ(s->setting.initial, 1500U);
}

TEST(SeamMessagePause, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.pause(speed_one), speed_one);
  r.type(0x1C0D);
  EXPECT_EQ(r.count_down(900), 900U);
  EXPECT_EQ(r.keys_waiting(), 1U);
}

TEST(SeamMessagePause, RaisesTheGamesPauseToTheSetting) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.pause(speed_one), 1500U);
  EXPECT_EQ(r.pause(0), 1500U) << "game speed 0 pauses for nothing";
}

TEST(SeamMessagePause, LeavesALongerGameSpeedAsThePlayerChoseIt) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.pause(speed_nine), speed_nine);
}

TEST(SeamMessagePause, FollowsTheSetting) {
  const rig r;
  r.arm();
  ASSERT_EQ(r.box->seams().set_setting(seam_id, 3000), seam_reason::none);
  EXPECT_EQ(r.pause(speed_one), 3000U);
  ASSERT_EQ(r.box->seams().set_setting(seam_id, 0), seam_reason::none);
  EXPECT_EQ(r.pause(speed_one), speed_one)
      << "zero keeps the game's own length, and only adds the key";
}

TEST(SeamMessagePause, LeavesTheBeatsOfAnApproach) {
  const rig r;
  r.arm();
  r.set_approach(2);
  EXPECT_EQ(r.pause(speed_one, script_segment, script_return), speed_one);
  r.set_approach(0);
  EXPECT_EQ(r.pause(speed_one, script_segment, script_return), 1500U)
      << "the script's pause after the approach is a message's";
  r.set_approach(2);
  EXPECT_EQ(r.pause(speed_one), 1500U)
      << "a count left over from a script does not reach a combat line";
}

TEST(SeamMessagePause, FollowsTheOverlayManagersWord) {
  const rig r;
  r.arm();
  r.set_approach(2);
  r.put_word(image_load_segment, script_word, 0x3100);
  EXPECT_EQ(r.pause(speed_one, script_segment, script_return), 1500U)
      << "overlay 3 is somewhere else, so this is not its code";
  EXPECT_EQ(r.pause(speed_one, 0x3100, script_return), speed_one);
  r.put_word(image_load_segment, script_word, 0);
  EXPECT_EQ(r.pause(speed_one, 0, script_return), 1500U)
      << "a module that is not loaded has no call sites";
}

TEST(SeamMessagePause, LeavesADeathsSoundItsLength) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.pause(speed_one, death_segment, death_in_a_fight), speed_one)
      << "the gate goes back on after it, and that ends the sound";
  EXPECT_EQ(r.pause(speed_one, death_segment, death_elsewhere), speed_one);
  r.type(0x1C0D);
  EXPECT_EQ(r.count_down(900, pause_return, data_segment, death_segment,
                         death_in_a_fight),
            900U)
      << "and a key does not cut it shorter than the game does";
  EXPECT_EQ(r.keys_waiting(), 1U);
}

TEST(SeamMessagePause, FindsTheDeathRoutineWhereTheManagerPutIt) {
  const rig r;
  r.arm();
  r.put_word(image_load_segment, death_word, 0x3100);
  EXPECT_EQ(r.pause(speed_one, death_segment, death_in_a_fight), 1500U)
      << "overlay 31 is somewhere else, so this is not its code";
  EXPECT_EQ(r.pause(speed_one, 0x3100, death_in_a_fight), speed_one);
}

TEST(SeamMessagePause, AKeyDoesNotCutAnApproachBeat) {
  const rig r;
  r.arm();
  r.set_approach(2);
  r.type(0x1C0D);
  EXPECT_EQ(r.count_down(900, pause_return, data_segment, script_segment,
                         script_return),
            900U);
  EXPECT_EQ(r.keys_waiting(), 1U);
}

TEST(SeamMessagePause, DeclinesAnotherDataSegment) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.pause(speed_one, other_segment, other_return, data_segment + 1),
            speed_one);
  EXPECT_EQ(r.status().fired, 0U);
}

TEST(SeamMessagePause, DeclinesAFrameThatIsNotTheRoutines) {
  const rig r;
  r.arm();
  EXPECT_EQ(
      r.pause(speed_one, other_segment, other_return, data_segment, frame - 2),
      speed_one);
  EXPECT_EQ(r.status().fired, 0U);
}

TEST(SeamMessagePause, AKeyEndsThePauseAndIsTaken) {
  const rig r;
  r.arm();
  r.type(0x1C0D);
  r.type(0x1E61);
  EXPECT_EQ(r.count_down(900), 1U) << "the next `dec dx` ends the loop";
  EXPECT_EQ(r.keys_waiting(), 1U) << "one key taken, the next one left";
  EXPECT_EQ(
      r.word(bda::segment, r.word(bda::segment, bda::keyboard_buffer_head)),
      0x1E61);
}

TEST(SeamMessagePause, NoKeyNoChange) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.count_down(900), 900U);
}

TEST(SeamMessagePause, LeavesEveryOtherDelayAlone) {
  const rig r;
  r.arm();
  r.type(0x1C0D);
  EXPECT_EQ(r.count_down(900, 0x1234), 900U)
      << "the typing, the music and every other wait run as they did";
  EXPECT_EQ(r.keys_waiting(), 1U);
}

TEST(SeamMessagePause, TakesTheKeyAcrossTheBuffersEnd) {
  const rig r;
  r.arm();
  const auto last = static_cast<std::uint16_t>(bda::keyboard_buffer_end - 2U);
  r.put_word(bda::segment, bda::keyboard_buffer_head, last);
  r.put_word(bda::segment, last, 0x1C0D);
  r.put_word(bda::segment, bda::keyboard_buffer_tail, bda::keyboard_buffer);
  EXPECT_EQ(r.count_down(900), 1U);
  EXPECT_EQ(r.word(bda::segment, bda::keyboard_buffer_head),
            bda::keyboard_buffer);
}

TEST(SeamMessagePause, DeclinesAnotherDataSegmentAtTheCountdown) {
  const rig r;
  r.arm();
  r.type(0x1C0D);
  EXPECT_EQ(r.count_down(900, pause_return, data_segment + 1), 900U);
  EXPECT_EQ(r.keys_waiting(), 1U);
}

TEST(SeamMessagePause, IsUnavailableOnAnyOtherBinary) {
  const auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_NE(box->seams().enable(seam_id), seam_reason::none);
}

// --- The setting, through the engine (#531) --------------------------------

TEST(SeamSetting, StartsAtTheDefinitionsInitial) {
  const rig r;
  EXPECT_EQ(r.box->seams().setting(seam_id), 1500U);
  EXPECT_EQ(r.box->seams().setting("code-wheel"), 0U)
      << "a seam that takes no number answers zero";
  EXPECT_EQ(r.box->seams().setting("no-such-seam"), 0U);
}

TEST(SeamSetting, RefusesWhatItCannotTakeAndChangesNothing) {
  const rig r;
  EXPECT_EQ(r.box->seams().set_setting("no-such-seam", 1),
            seam_reason::unknown_seam);
  EXPECT_EQ(r.box->seams().set_setting("code-wheel", 1),
            seam_reason::no_setting);
  EXPECT_EQ(r.box->seams().set_setting(seam_id, 10001),
            seam_reason::setting_out_of_range);
  EXPECT_EQ(r.box->seams().setting(seam_id), 1500U);
  EXPECT_EQ(r.box->seams().set_setting(seam_id, 10000), seam_reason::none);
  EXPECT_EQ(r.box->seams().setting(seam_id), 10000U);
}

TEST(SeamSetting, IsSetWhetherTheSeamIsOnOrOff) {
  const rig r;
  EXPECT_EQ(r.box->seams().set_setting(seam_id, 2000), seam_reason::none);
  r.arm();
  EXPECT_EQ(r.pause(speed_one), 2000U);
  ASSERT_EQ(r.box->seams().disable(seam_id), seam_reason::none);
  r.arm();
  EXPECT_EQ(r.box->seams().setting(seam_id), 2000U)
      << "an enable does not start the number over";
}

TEST(SeamSetting, OutlivesAReset) {
  const rig r;
  ASSERT_EQ(r.box->seams().set_setting(seam_id, 2000), seam_reason::none);
  r.box->reset();
  EXPECT_EQ(r.box->seams().setting(seam_id), 2000U)
      << "a person's choice outlasts the run it was made in";
}

TEST(SeamSetting, IsNotMachineState) {
  const rig a;
  const rig b;
  ASSERT_EQ(b.box->seams().set_setting(seam_id, 2000), seam_reason::none);
  EXPECT_EQ(hash_state(*a.box).whole, hash_state(*b.box).whole);
}

// --- The setting, in a recording (#531) ------------------------------------

TEST(SeamSetting, ASeamLineCarriesItsNumber) {
  replay_event event{};
  constexpr std::string_view line = "seam message-pause 2750";
  ASSERT_TRUE(parse_replay_line(std::span<const char>(line), event));
  EXPECT_EQ(event.kind, replay_line::seam);
  EXPECT_EQ(event.id_text(), seam_id);
  EXPECT_TRUE(event.have_setting);
  EXPECT_EQ(event.setting, 2750U);

  std::array<char, 64> out{};
  const std::size_t length = format_replay_line(event, out);
  EXPECT_EQ(std::string_view(out.data(), length), "seam message-pause 2750\n");
}

TEST(SeamSetting, ASeamLineWithoutANumberIsTheOldLine) {
  replay_event event{};
  constexpr std::string_view line = "seam quick-steps";
  ASSERT_TRUE(parse_replay_line(std::span<const char>(line), event));
  EXPECT_FALSE(event.have_setting);
  std::array<char, 64> out{};
  const std::size_t length = format_replay_line(event, out);
  EXPECT_EQ(std::string_view(out.data(), length), "seam quick-steps\n");
}

TEST(SeamSetting, ASeamLineRefusesWhatIsNotANumber) {
  replay_event event{};
  for (const std::string_view line :
       {"seam message-pause x", "seam message-pause 65536",
        "seam message-pause 1 2"}) {
    EXPECT_FALSE(parse_replay_line(std::span<const char>(line), event)) << line;
  }
}

}  // namespace
}  // namespace amberfolio::machine
