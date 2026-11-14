// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam on Curse of the Azure Bonds (seam_fanfare.cpp, #598),
// exercised through its mechanism as seam_fanfare_test.cpp does on Pool
// of Radiance: the titles' overlay put where the overlay manager's word
// says it is, the processor stood on the titles routine's entry with the
// data segment in DS, the clock run with the processor halted, and every
// byte written to the Tandy chip's port logged with its tick.
//
// What differs on this program, restated rather than read out of the
// seam: the titles are overlay 2, the driver's dispatch entry is its own,
// the sound setting holds 1 for the Tandy chip (0 is the speaker), and
// the piece is its own. **Every byte of program here is this file's
// own**: a `push bp` at the titles' entry with a far jump behind it to a
// HLT, a HLT at the dispatch entry and a `retf 2` where it leaves.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "fanfare";

constexpr std::string_view curse_fingerprint =
    "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd";

/// Overlay 2's facts, the word its segment is kept in, and the titles
/// routine's entry in it.
constexpr std::uint32_t titles_file_offset = 218;
constexpr std::uint32_t titles_length = 1911;
constexpr std::uint16_t titles_word = 0x3A0;
constexpr std::uint16_t titles_entry = 0x0632;

/// The driver's dispatch entry, from the image segment, and the requests
/// that start no sound: 0 and 1 around a file read, and 0xFF.
constexpr std::uint16_t dispatch_entry = 0x69B0;
/// Its `retf 2`, and where the titles' call to it returns.
constexpr std::uint16_t dispatch_return = 0x6A61;
constexpr std::uint16_t titles_call_return = 0x0734;
constexpr std::array<std::uint8_t, 3> housekeeping{0x00, 0x01, 0xFF};
constexpr std::uint8_t a_sound = 0x0D;

/// The driver's tick that writes the chip, and the `retn` that leaves it,
/// from the image segment.
constexpr std::uint16_t chip_service = 0x7DBF;
constexpr std::uint16_t chip_service_return = 0x7E3B;

/// The data segment and the sound setting in it.
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xABE);
constexpr std::uint16_t data_sound = 0x5C04;
constexpr std::uint8_t sound_tandy = 1;
constexpr std::uint8_t sound_speaker = 0;

constexpr std::uint16_t titles_segment = 0x2000;
constexpr std::uint16_t idle_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;

/// The piece: 2,072 writes on 1,560 ticks, opening with the four voices
/// silenced on its start, its last write 46,359,494 ticks in. Its closest
/// two ticks are 85 apart.
constexpr std::size_t piece_writes = 2072;
constexpr unsigned piece_ticks = 1560;
constexpr std::array<std::uint8_t, 4> opening{0x9F, 0xBF, 0xDF, 0xFF};
constexpr ticks last_write_at = 46359494;
constexpr ticks piece_and_then_some = 40 * pit_input_hz;

/// The rig's step cost: a prime under the piece's closest two ticks.
constexpr ticks step = 61;

constexpr std::array<std::uint8_t, 11> hand_back{
    0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF, 0xFF};

class chip_log final : public device {
 public:
  explicit chip_log(const machine& box) noexcept : box_(&box) {}

  static constexpr port_range window{.first = 0xC0, .last = 0xC0};

  [[nodiscard]] claims claimed() const noexcept override {
    return {.ports = std::span(&window, 1)};
  }
  void reset() override {}
  void save_state(state_sink& /*out*/) const override {}
  void write_port(std::uint16_t /*port*/, std::uint8_t value) override {
    writes.push_back({.at = box_->time(), .value = value});
  }

  std::vector<chip_write> writes;

 private:
  const machine* box_;
};

struct curse_rig {
  curse_rig()
      : box(std::make_unique<machine>(memory_layout::pc)),
        chip(std::make_unique<chip_log>(*box)) {
    sha256_digest curse;
    EXPECT_TRUE(parse_digest(curse_fingerprint, curse));
    box->seams().loaded(curse, image_load_segment);
    EXPECT_TRUE(box->attach(*chip));
    EXPECT_TRUE(box->set_step_cost(step));
    put_word(image_load_segment, titles_word, titles_segment);
    put_byte(data_segment, data_sound, sound_tandy);
    put_byte(idle_segment, 0, 0xF4);
    put_byte(image_load_segment, dispatch_entry, 0xF4);
    put_far_return(image_load_segment, dispatch_return);
    put_byte(titles_segment, titles_call_return, 0xF4);
  }

  /// A `retf 2` at `offset`.
  void put_far_return(std::uint16_t segment, std::uint16_t offset) const {
    put_byte(segment, offset, 0xCA);
    put_word(segment, static_cast<std::uint16_t>(offset + 1), 2);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
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

  void arrive_at_titles() const {
    put_byte(titles_segment, titles_entry, 0x55);
    put_byte(titles_segment, static_cast<std::uint16_t>(titles_entry + 1),
             0xEA);
    put_word(titles_segment, static_cast<std::uint16_t>(titles_entry + 2), 0);
    put_word(titles_segment, static_cast<std::uint16_t>(titles_entry + 4),
             idle_segment);
    stand_at(titles_segment, titles_entry);
    box->step();
  }

  /// The program asks its driver for `request` from a call in `caller`: a
  /// far return and the request above it on the stack. Answers where CS:IP
  /// is after the step, as a linear address.
  [[nodiscard]] std::uint32_t arrive_at_dispatch(
      std::uint8_t request, std::uint16_t caller = idle_segment) const {
    stand_at(image_load_segment, dispatch_entry);
    put_word(stack_segment, stack_top, titles_call_return);
    put_word(stack_segment, stack_top + 2, caller);
    put_word(stack_segment, stack_top + 4, request);
    box->step();
    const cpu::registers& regs = box->processor().regs();
    return cpu::physical_address(regs[cpu::sreg::cs], regs.ip);
  }

  /// The driver's timer reaches its chip routine: a HLT at its entry, a
  /// `retn` where it leaves, and a near return on the stack to a HLT of
  /// its own. Answers where IP is after the step.
  [[nodiscard]] std::uint16_t arrive_at_chip_service() const {
    put_byte(image_load_segment, chip_service, 0xF4);
    put_byte(image_load_segment, chip_service_return, 0xC3);
    put_byte(image_load_segment, returned_to, 0xF4);
    stand_at(image_load_segment, chip_service);
    put_word(stack_segment, stack_top, returned_to);
    box->step();
    return box->processor().regs().ip;
  }

  static constexpr std::uint16_t stack_top = 0x0400;
  static constexpr std::uint16_t returned_to = 0x7F00;

  void stand_at(std::uint16_t cs, std::uint16_t ip) const {
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = cs;
    r.ip = ip;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = stack_top;
  }

  void play_for(ticks span) const {
    static_cast<void>(box->run(box->time() + span));
  }

  std::unique_ptr<machine> box;
  std::unique_ptr<chip_log> chip;
};

TEST(SeamFanfareOnCurse, IsAvailableWithItsOwnPoints) {
  const curse_rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  sha256_digest curse;
  ASSERT_TRUE(parse_digest(curse_fingerprint, curse));
  const std::span<const seam_point> points = points_on(*s, curse);
  ASSERT_EQ(points.size(), 4U);
  EXPECT_EQ(points[0].module.file, "GAME.OVR");
  EXPECT_EQ(points[0].module.file_offset, titles_file_offset);
  EXPECT_EQ(points[0].module.length, titles_length);
  EXPECT_EQ(points[0].module.load_segment_at, titles_word);
  EXPECT_EQ(points[0].offset, titles_entry);
  EXPECT_TRUE(points[1].at_moment);
  EXPECT_TRUE(points[2].module.is_resident_image());
  EXPECT_EQ(points[2].offset, dispatch_entry);
  EXPECT_TRUE(points[3].module.is_resident_image());
  EXPECT_EQ(points[3].offset, chip_service);
}

TEST(SeamFanfareOnCurse, PlaysItsOwnPieceFromTheTitlesToItsEnd) {
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  ASSERT_FALSE(r.chip->writes.empty());
  const ticks start = r.chip->writes[0].at;
  r.play_for(piece_and_then_some);

  const std::vector<chip_write>& w = r.chip->writes;
  ASSERT_EQ(w.size(), piece_writes + hand_back.size());
  for (std::size_t i = 0; i < opening.size(); ++i) {
    EXPECT_EQ(w[i].value, opening[i]) << i;
    EXPECT_EQ(w[i].at, start) << i;
  }
  const ticks last = w[piece_writes - 1].at;
  EXPECT_GE(last, start + last_write_at);
  EXPECT_LT(last, start + last_write_at + step);
  for (std::size_t i = 0; i < hand_back.size(); ++i) {
    EXPECT_EQ(w[piece_writes + i].value, hand_back[i]) << i;
  }
  EXPECT_EQ(r.box->seams().status(seam_id).fired, 1U + piece_ticks)
      << "the titles, and one moment for each tick the piece writes on";
}

TEST(SeamFanfareOnCurse, GivesWayToTheProgramsFirstSound) {
  // Any sound but the titles' own.
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(5 * pit_input_hz);
  const std::size_t played = r.chip->writes.size();
  ASSERT_GT(played, opening.size());
  ASSERT_LT(played, piece_writes);

  static_cast<void>(r.arrive_at_dispatch(a_sound));
  const std::size_t stopped = r.chip->writes.size();
  ASSERT_GE(stopped, played + hand_back.size());
  EXPECT_EQ(r.chip->writes.back().value, hand_back.back());
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), stopped);
}

TEST(SeamFanfareOnCurse, PlaysOnOverTheTitlesOwnSound) {
  // At their fourth picture, about twenty seconds in: the request leaves
  // the driver at once and the piece plays to its end.
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(5 * pit_input_hz);
  const std::uint32_t at = r.arrive_at_dispatch(a_sound, titles_segment);
  EXPECT_TRUE(at ==
                  cpu::physical_address(image_load_segment, dispatch_return) ||
              at == cpu::physical_address(titles_segment, titles_call_return))
      << "sent to the driver's retf 2, not into the driver: " << at;
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), piece_writes + hand_back.size())
      << "the whole piece, and the hand-back at its end";
}

TEST(SeamFanfareOnCurse, GivesWayToTheTitlesSoundWhenTheDriverIsNotItsOwn) {
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(5 * pit_input_hz);
  r.put_byte(image_load_segment, dispatch_return, 0xCB);
  const std::uint32_t at = r.arrive_at_dispatch(a_sound, titles_segment);
  EXPECT_EQ(at, cpu::physical_address(image_load_segment, dispatch_entry + 1))
      << "into the driver: its HLT";
  const std::size_t stopped = r.chip->writes.size();
  EXPECT_EQ(r.chip->writes.back().value, hand_back.back());
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), stopped);
}

TEST(SeamFanfareOnCurse, LeavesTheTitlesSoundAloneOnceThePieceIsOver) {
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.arrive_at_dispatch(a_sound, titles_segment),
            cpu::physical_address(image_load_segment, dispatch_entry + 1));
}

TEST(SeamFanfareOnCurse, PlaysOnThroughTheRequestsThatStartNoSound) {
  // The titles read a picture between their screens, and the program asks
  // its driver for 0 before each read and 1 after it.
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(2 * pit_input_hz);
  for (const std::uint8_t request : housekeeping) {
    static_cast<void>(r.arrive_at_dispatch(request));
    r.play_for(pit_input_hz);
  }
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), piece_writes + hand_back.size())
      << "the whole piece, and the hand-back at its end";
}

TEST(SeamFanfareOnCurse, KeepsTheDriversTickOffTheChipWhileItPlays) {
  const curse_rig r;
  r.arm();
  EXPECT_EQ(r.arrive_at_chip_service(),
            static_cast<std::uint16_t>(chip_service + 1))
      << "before the piece, the driver's own: its HLT";

  r.arrive_at_titles();
  const std::uint16_t ip = r.arrive_at_chip_service();
  EXPECT_TRUE(ip == chip_service_return || ip == curse_rig::returned_to)
      << "while it plays, the routine leaves at once: " << ip;

  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.arrive_at_chip_service(),
            static_cast<std::uint16_t>(chip_service + 1))
      << "and after it, the driver's own again";
}

TEST(SeamFanfareOnCurse, DeclinesATickWhoseExitIsNotAReturn) {
  const curse_rig r;
  r.arm();
  r.arrive_at_titles();
  r.put_byte(image_load_segment, chip_service, 0xF4);
  r.put_byte(image_load_segment, chip_service_return, 0x90);
  r.stand_at(image_load_segment, chip_service);
  r.box->step();
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1U);
}

TEST(SeamFanfareOnCurse, PlaysNothingWhenTheGameSoundsThroughTheSpeaker) {
  // 0 is Pool of Radiance's value for the chip and this program's for the
  // speaker.
  const curse_rig r;
  r.arm();
  r.put_byte(data_segment, data_sound, sound_speaker);
  r.arrive_at_titles();
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty());
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0U);
}

}  // namespace
}  // namespace amberfolio::machine
