// SPDX-License-Identifier: AGPL-3.0-only
//
// The fanfare seam (seam_fanfare.cpp), exercised through its mechanism and
// not through any program: the test puts the titles' overlay where the
// overlay manager's word says it is, stands the processor on the titles
// routine's entry with the data segment in DS, and lets the clock run with the
// processor halted, logging every byte written to the Tandy chip's port
// and the tick it was written at. And it stands the processor on the
// driver's dispatch entry, for the program's first sound.
//
// The offsets are restated rather than read out of the seam, which is the
// seam suites' rule, and so are the few facts about the piece the checks
// below need: how many writes it is, its first note, and the tick of its
// last write. **Every byte of program here is this file's own** (PLAN.md
// §6): a `push bp` at the titles' entry and a far jump behind it that
// sends the processor to a HLT, and a HLT at the dispatch entry.

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
#include "amberfolio/machine/psg.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "fanfare";

/// Overlay 1's facts, the word its segment is kept in, and the titles
/// routine's entry in it.
constexpr std::uint32_t titles_file_offset = 212;
constexpr std::uint32_t titles_length = 2062;
constexpr std::uint16_t titles_word = 0x1A0;
constexpr std::uint16_t titles_entry = 0x0627;

/// The driver's dispatch entry, from the image segment.
constexpr std::uint16_t dispatch_entry = 0x8BBC;

/// The data segment and the sound setting in it.
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xC7C);
constexpr std::uint16_t data_sound = 0x5372;
constexpr std::uint8_t sound_tandy = 0;
constexpr std::uint8_t sound_none = 2;

/// Where this test puts the titles' overlay, and the HLT the far jump
/// behind the entry goes to.
constexpr std::uint16_t titles_segment = 0x2000;
constexpr std::uint16_t idle_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;

/// The piece: 5,950 writes, the first four silencing the four voices at
/// its start, its first note a quarter of a second in, its last write
/// 37,132,818 ticks from its start.
constexpr std::size_t piece_writes = 5950;
constexpr ticks first_note_at = 298296;
constexpr std::array<std::uint8_t, 5> first_note{0x8D, 0x17, 0xA2, 0x0E, 0xB5};
constexpr ticks last_write_at = 37132818;
constexpr ticks piece_and_then_some = 33 * pit_input_hz;

/// The rig's step cost, in ticks: a prime, so that the boundaries never
/// line up with the piece's own grid, and far under its shortest gap
/// between two writes, 2,485 ticks.
constexpr ticks step = 97;

/// Whether `went` is where a write due `due` ticks after `start` goes out
/// on a machine whose steps cost `cost`: at the first boundary at or past
/// it.
[[nodiscard]] bool on_time(ticks went, ticks start, ticks due,
                           ticks cost = step) {
  return went >= start + due && went < start + due + cost;
}

/// What the seam writes to hand the chip back as the start-up left it.
constexpr std::array<std::uint8_t, 11> hand_back{
    0x80, 0x00, 0xA0, 0x00, 0xC0, 0x00, 0xE0, 0x9F, 0xBF, 0xDF, 0xFF};

/// The chip's port, logged: every byte and the tick it went out at.
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

struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc)),
        chip(std::make_unique<chip_log>(*box)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    EXPECT_TRUE(box->attach(*chip));
    // A coarse step, so that half a minute of the piece is a few hundred
    // thousand steps: a write goes out at the first boundary at or past
    // its tick, so within `step` of it.
    EXPECT_TRUE(box->set_step_cost(step));
    manager_puts_titles_at(titles_segment);
    put_byte(data_segment, data_sound, sound_tandy);
    put_byte(idle_segment, 0, 0xF4);
    put_byte(image_load_segment, dispatch_entry, 0xF4);
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

  void manager_puts_titles_at(std::uint16_t segment) const {
    put_word(image_load_segment, titles_word, segment);
  }

  /// Stand on the titles' entry in the overlay at `segment`, `opcode`
  /// there and a far jump to the idle HLT behind it, and take the step.
  void arrive_at_titles(std::uint16_t segment = titles_segment,
                        std::uint16_t ds = data_segment,
                        std::uint8_t opcode = 0x55) const {
    put_byte(segment, titles_entry, opcode);
    put_byte(segment, static_cast<std::uint16_t>(titles_entry + 1), 0xEA);
    put_word(segment, static_cast<std::uint16_t>(titles_entry + 2), 0);
    put_word(segment, static_cast<std::uint16_t>(titles_entry + 4),
             idle_segment);
    stand_at(segment, titles_entry, ds);
    box->step();
  }

  /// The program asks its driver for a sound.
  void arrive_at_dispatch() const {
    stand_at(image_load_segment, dispatch_entry, data_segment);
    box->step();
  }

  void stand_at(std::uint16_t cs, std::uint16_t ip, std::uint16_t ds) const {
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = cs;
    r.ip = ip;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
  }

  void play_for(ticks span) const {
    static_cast<void>(box->run(box->time() + span));
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().status(seam_id);
  }

  std::unique_ptr<machine> box;
  std::unique_ptr<chip_log> chip;
};

/// The chip as the program's start-up leaves it: every voice silenced, the
/// noise voice's attenuation named last.
[[nodiscard]] psg_registers after_start_up() {
  psg_registers chip = psg_registers::powered_on();
  constexpr std::array<std::uint8_t, 4> silence{0x9F, 0xBF, 0xDF, 0xFF};
  for (const std::uint8_t value : silence) {
    static_cast<void>(chip.write(value));
  }
  return chip;
}

[[nodiscard]] bool same(const psg_registers& a, const psg_registers& b) {
  return a.latched == b.latched && a.period == b.period && a.noise == b.noise &&
         a.attenuation == b.attenuation;
}

TEST(SeamFanfare, IsTheTitlesEntryAMomentAndTheDispatchEntry) {
  const rig r;
  const seam_definition* s = r.box->seams().find(seam_id);
  ASSERT_NE(s, nullptr);
  ASSERT_EQ(s->points.size(), 3U);

  const seam_point& titles = s->points[0];
  EXPECT_EQ(titles.module.file, "GAME.OVR");
  EXPECT_EQ(titles.module.file_offset, titles_file_offset);
  EXPECT_EQ(titles.module.length, titles_length);
  EXPECT_EQ(titles.module.load_segment_at, titles_word);
  EXPECT_EQ(titles.offset, titles_entry);
  EXPECT_FALSE(titles.at_moment);

  EXPECT_TRUE(s->points[1].at_moment);
  EXPECT_TRUE(s->points[1].module.is_resident_image());

  const seam_point& dispatch = s->points[2];
  EXPECT_TRUE(dispatch.module.is_resident_image());
  EXPECT_EQ(dispatch.offset, dispatch_entry);

  EXPECT_FALSE(s->trigger);
  EXPECT_FALSE(s->setting.present());
  EXPECT_TRUE(s->group.empty());
}

TEST(SeamFanfare, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.status().state, seam_state::off);
  r.arrive_at_titles();
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty());
}

TEST(SeamFanfare, PlaysThePieceFromTheTitlesToItsEnd) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  const ticks start = r.chip->writes.empty() ? 0 : r.chip->writes[0].at;
  r.play_for(piece_and_then_some);

  const std::vector<chip_write>& w = r.chip->writes;
  ASSERT_EQ(w.size(), piece_writes + hand_back.size());
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_EQ(w[i].at, start);
  }
  EXPECT_EQ(w[0].value, 0x9F);
  EXPECT_EQ(w[3].value, 0xFF);
  for (std::size_t i = 0; i < first_note.size(); ++i) {
    EXPECT_TRUE(on_time(w[4 + i].at, start, first_note_at)) << i;
    EXPECT_EQ(w[4 + i].value, first_note[i]) << i;
  }
  for (std::size_t i = 1; i < w.size(); ++i) {
    ASSERT_GE(w[i].at, w[i - 1].at) << i;
  }
  EXPECT_TRUE(on_time(w[piece_writes - 1].at, start, last_write_at));
  for (std::size_t i = 0; i < hand_back.size(); ++i) {
    EXPECT_EQ(w[piece_writes + i].value, hand_back[i]) << i;
    EXPECT_EQ(w[piece_writes + i].at, w[piece_writes - 1].at) << i;
  }
  EXPECT_EQ(r.status().fired, 1U + 4184U)
      << "the titles, and one moment for each tick the piece writes on";
}

TEST(SeamFanfare, StartsAtTheTitlesArrival) {
  const rig r;
  r.arm();
  const ticks before = r.box->time();
  r.arrive_at_titles();
  ASSERT_FALSE(r.chip->writes.empty())
      << "the first moment is the arrival itself";
  EXPECT_GE(r.chip->writes[0].at, before);
  EXPECT_LE(r.chip->writes[0].at, r.box->time());
}

TEST(SeamFanfare, HandsTheChipBackAsTheStartUpLeftIt) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(piece_and_then_some);
  psg_registers chip = after_start_up();
  for (const chip_write& w : r.chip->writes) {
    static_cast<void>(chip.write(w.value));
  }
  EXPECT_TRUE(same(chip, after_start_up()));
}

TEST(SeamFanfare, KeepsItsTempoWhateverAStepCosts) {
  // Virtual time is the piece's clock, so a machine whose steps cost
  // seven times less writes the same bytes, each at the first boundary
  // past the same tick: the two never drift further apart than the
  // coarser step, however far into the piece.
  const rig coarse;
  coarse.arm();
  coarse.arrive_at_titles();
  coarse.play_for(piece_and_then_some);

  const rig fine;
  constexpr ticks fine_step = 13;
  ASSERT_TRUE(fine.box->set_step_cost(fine_step));
  fine.arm();
  fine.arrive_at_titles();
  fine.play_for(piece_and_then_some);

  const std::vector<chip_write>& a = coarse.chip->writes;
  const std::vector<chip_write>& b = fine.chip->writes;
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    ASSERT_EQ(a[i].value, b[i].value) << i;
    const ticks went_a = a[i].at - a[0].at;
    const ticks went_b = b[i].at - b[0].at;
    ASSERT_LT(went_a > went_b ? went_a - went_b : went_b - went_a, step) << i;
  }
}

TEST(SeamFanfare, GivesWayToTheProgramsFirstSound) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(5 * pit_input_hz);
  const std::size_t played = r.chip->writes.size();
  ASSERT_GT(played, 4U);
  ASSERT_LT(played, piece_writes);

  // The boundary the program's request arrives at may also be a moment
  // that is due, which goes first; then the chip goes back.
  r.arrive_at_dispatch();
  const std::size_t stopped = r.chip->writes.size();
  ASSERT_GE(stopped, played + hand_back.size());
  const std::size_t from = stopped - hand_back.size();
  for (std::size_t i = 0; i < hand_back.size(); ++i) {
    EXPECT_EQ(r.chip->writes[from + i].value, hand_back[i]) << i;
  }
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), stopped)
      << "for good: the rest of the piece is not played";
}

TEST(SeamFanfare, ASoundWithNothingPlayingIsLeftAlone) {
  const rig r;
  r.arm();
  r.arrive_at_dispatch();
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty());

  r.arrive_at_titles();
  r.play_for(piece_and_then_some);
  r.arrive_at_dispatch();
  EXPECT_EQ(r.chip->writes.size(), piece_writes + hand_back.size())
      << "nor after the piece has ended";
}

TEST(SeamFanfare, StopsWhenTheSoundIsNoLongerTheChip) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(5 * pit_input_hz);
  const std::size_t played = r.chip->writes.size();

  r.put_byte(data_segment, data_sound, sound_none);
  r.play_for(pit_input_hz);
  ASSERT_EQ(r.chip->writes.size(), played + hand_back.size());
  EXPECT_EQ(r.chip->writes.back().value, hand_back.back());

  r.put_byte(data_segment, data_sound, sound_tandy);
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), played + hand_back.size());
}

TEST(SeamFanfare, PlaysNothingWhenTheSoundIsNotTheChipAtTheTitles) {
  const rig r;
  r.arm();
  r.put_byte(data_segment, data_sound, 1);
  r.arrive_at_titles();
  r.play_for(piece_and_then_some);
  EXPECT_TRUE(r.chip->writes.empty());
  EXPECT_EQ(r.status().declined, 0U) << "a choice, not a wrong fact";
}

TEST(SeamFanfare, DeclinesAnotherDataSegment) {
  const rig r;
  r.arm();
  r.arrive_at_titles(titles_segment, 0x1234);
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty());
  EXPECT_EQ(r.status().declined, 1U);
}

TEST(SeamFanfare, DeclinesAnInstructionThatIsNotAPushBp) {
  const rig r;
  r.arm();
  r.arrive_at_titles(titles_segment, data_segment, 0xF4);
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty());
  EXPECT_EQ(r.status().declined, 1U);
}

TEST(SeamFanfare, FollowsTheOverlayManagersWord) {
  const rig r;
  r.arm();
  r.manager_puts_titles_at(0x2400);
  r.arrive_at_titles(titles_segment);
  r.play_for(pit_input_hz);
  EXPECT_TRUE(r.chip->writes.empty()) << "where the overlay was, not is";

  r.arrive_at_titles(0x2400);
  r.play_for(pit_input_hz);
  EXPECT_FALSE(r.chip->writes.empty());
}

TEST(SeamFanfare, TheTitlesReachedAgainDoNotStartItAgain) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(2 * pit_input_hz);
  r.arrive_at_titles();
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), piece_writes + hand_back.size());
}

TEST(SeamFanfare, AResetEndsIt) {
  const rig r;
  r.arm();
  r.arrive_at_titles();
  r.play_for(2 * pit_input_hz);
  const std::size_t played = r.chip->writes.size();
  r.box->reset();
  r.play_for(piece_and_then_some);
  EXPECT_EQ(r.chip->writes.size(), played);
}

TEST(SeamFanfare, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other;
  ASSERT_TRUE(parse_digest(
      "3333333333333333333333333333333333333333333333333333333333333333",
      other));
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

}  // namespace
}  // namespace amberfolio::machine
