// SPDX-License-Identifier: AGPL-3.0-only
//
// A point at a moment (seam.h, "A point at a moment", #538): offered when
// the virtual time its seam asked for comes, once per asking, with the
// moment it asked for and not the tick the machine reached; never while
// nothing is asked; dropped by everything that drops a latch; unable to
// call the program; and costing a run that serves one and writes nothing
// not a byte of machine state. And a seam switched off with a moment
// asked for is owed its stop, once, at the next boundary (#540).
//
// The program is this file's own: a NOP the address point sits on, then
// a HLT the processor idles at while the clock runs.

#include <array>
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
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view claimed_hex =
    "2222222222222222222222222222222222222222222222222222222222222222";
constexpr std::array<std::string_view, 1> claimed_binaries{claimed_hex};

/// What the address point asks for: a moment this many ticks from the
/// arrival, or none.
bool ask_at_arrival = false;
ticks ask_after = 0;

/// What the moment point asks for next: this many ticks after the moment
/// it was handed, while `asks_left` lasts.
ticks period = 0;
int asks_left = 0;

/// What the moment point saw, one entry per arrival.
struct served {
  ticks moment;
  ticks now;
  bool called;
  bool placed;
};
std::vector<served> arrivals;

/// What the stop saw, one entry per run of it.
std::vector<served> stops;

void ask_for_a_moment(machine& box, seam_context& ctx) {
  if (ask_at_arrival) {
    ctx.offer_at(box.time() + ask_after);
  }
}

void at_the_moment(machine& box, seam_context& ctx) {
  const std::array<std::uint16_t, 1> word{1};
  const std::array<std::uint8_t, 2> bytes{1, 2};
  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  arrivals.push_back({.moment = ctx.moment(),
                      .now = box.time(),
                      .called = ctx.call_program(0x1000, 0, word),
                      .placed = ctx.place_bytes(bytes, segment, offset)});
  if (asks_left > 0) {
    --asks_left;
    ctx.offer_at(ctx.moment() + period);
  }
}

void at_the_stop(machine& box, seam_context& ctx) {
  const std::array<std::uint16_t, 1> word{1};
  const std::array<std::uint8_t, 2> bytes{1, 2};
  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  stops.push_back({.moment = ctx.moment(),
                   .now = box.time(),
                   .called = ctx.call_program(0x1000, 0, word),
                   .placed = ctx.place_bytes(bytes, segment, offset)});
}

constexpr std::array<seam_point, 2> moment_points{
    {{.module = resident_image, .offset = 0, .run = &ask_for_a_moment},
     {.module = resident_image, .run = &at_the_moment, .at_moment = true}}};
constexpr seam_definition moment_seam{
    .id = "test-moment",
    .about = "asks for a moment at an address, and is offered it",
    .fingerprints = claimed_binaries,
    .points = moment_points};

constexpr std::array<seam_point, 1> only_moment_points{
    {{.module = resident_image, .run = &at_the_moment, .at_moment = true}}};
constexpr seam_definition only_moment_seam{
    .id = "test-only-moment",
    .about = "a moment point and nothing else",
    .fingerprints = claimed_binaries,
    .points = only_moment_points};

constexpr seam_definition stopping_seam{
    .id = "test-moment-stop",
    .about = "asks for a moment, and has a stop for being switched off",
    .fingerprints = claimed_binaries,
    .points = moment_points,
    .stop = &at_the_stop};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    ask_at_arrival = false;
    ask_after = 0;
    period = 0;
    asks_left = 0;
    arrivals.clear();
    stops.clear();
    EXPECT_TRUE(box->seams().add(moment_seam));
    EXPECT_TRUE(box->seams().add(only_moment_seam));
    EXPECT_TRUE(box->seams().add(stopping_seam));
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(claimed_hex, digest));
    box->seams().loaded(digest, image_load_segment);
    // A NOP for the address point, then a HLT to idle at.
    const std::uint32_t at = cpu::physical_address(image_load_segment, 0);
    box->memory().ram()[at] = 0x90;
    box->memory().ram()[at + 1] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r[cpu::sreg::ss] = image_load_segment;
    r[cpu::reg16::sp] = 0x0400;
    r.ip = 0;
  }

  void on() const {
    ASSERT_EQ(box->seams().enable("test-moment"), seam_reason::none);
  }

  /// Step until the clock reaches `until`, and answer the clock at every
  /// boundary on the way, the last included.
  [[nodiscard]] std::vector<ticks> run_to(ticks until) const {
    std::vector<ticks> boundaries;
    while (box->time() < until) {
      boundaries.push_back(box->time());
      box->step();
    }
    boundaries.push_back(box->time());
    return boundaries;
  }

  std::unique_ptr<machine> box;
};

TEST(SeamMoment, NothingAskedIsNothingOffered) {
  const rig r;
  r.on();
  static_cast<void>(r.run_to(10'000));
  EXPECT_TRUE(arrivals.empty());
  EXPECT_FALSE(r.box->seams().due(r.box->time()));
  EXPECT_FALSE(r.box->seams().due(~ticks{} - 1));
}

TEST(SeamMoment, IsOfferedOnceAtTheFirstBoundaryAtOrAfterIt) {
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 1'000;
  const std::vector<ticks> boundaries = r.run_to(5'000);
  ASSERT_EQ(arrivals.size(), 1U);
  const ticks asked = boundaries.front() + ask_after;
  EXPECT_EQ(arrivals[0].moment, asked)
      << "the moment it asked for, not the tick the machine reached";
  EXPECT_GE(arrivals[0].now, asked);
  // The boundary before the one it was served at had not reached it.
  bool earlier_one_before = false;
  for (const ticks t : boundaries) {
    if (t < arrivals[0].now) {
      earlier_one_before = true;
      EXPECT_LT(t, asked) << "offered later than the first boundary past it";
    }
  }
  EXPECT_TRUE(earlier_one_before);
  EXPECT_FALSE(r.box->seams().due(r.box->time())) << "once, not again";
}

TEST(SeamMoment, AHandlerThatAsksAgainFromItsMomentNeverDrifts) {
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 500;
  period = 37;  // not a whole number of any step's cost
  asks_left = 20;
  static_cast<void>(r.run_to(5'000));
  ASSERT_EQ(arrivals.size(), 21U);
  for (std::size_t i = 1; i < arrivals.size(); ++i) {
    EXPECT_EQ(arrivals[i].moment, arrivals[0].moment + i * period) << i;
    EXPECT_GE(arrivals[i].now, arrivals[i].moment) << i;
  }
}

TEST(SeamMoment, AMomentAlreadyPastIsStillOffered) {
  // Asked for now at the NOP, and served at that boundary; asked again
  // for the moment just served, which is behind the clock by then, and
  // served again rather than lost.
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 0;
  period = 0;
  asks_left = 1;
  static_cast<void>(r.run_to(1'000));
  ASSERT_EQ(arrivals.size(), 2U);
  EXPECT_EQ(arrivals[1].moment, arrivals[0].moment);
  EXPECT_GE(arrivals[1].now, arrivals[0].now);
}

TEST(SeamMoment, CannotCallTheProgram) {
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 100;
  static_cast<void>(r.run_to(1'000));
  ASSERT_EQ(arrivals.size(), 1U);
  EXPECT_FALSE(arrivals[0].called) << "a moment is at no known instruction";
  EXPECT_FALSE(arrivals[0].placed);
}

TEST(SeamMoment, DisablingDropsIt) {
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 1'000'000;
  r.box->step();
  ASSERT_TRUE(r.box->seams().due(1'000'000 + r.box->time()));
  ASSERT_EQ(r.box->seams().disable("test-moment"), seam_reason::none);
  EXPECT_FALSE(r.box->seams().due(~ticks{} - 1));
  ASSERT_EQ(r.box->seams().enable("test-moment"), seam_reason::none);
  EXPECT_FALSE(r.box->seams().due(~ticks{} - 1))
      << "switched on again, it starts from its own points";
  static_cast<void>(r.run_to(r.box->time() + 2'000'000));
  EXPECT_TRUE(arrivals.empty());
}

TEST(SeamMoment, AResetDropsIt) {
  const rig r;
  r.on();
  ask_at_arrival = true;
  ask_after = 1'000;
  r.box->step();
  ASSERT_TRUE(r.box->seams().due(r.box->time() + 1'000));
  r.box->reset();
  EXPECT_FALSE(r.box->seams().due(~ticks{} - 1));
}

TEST(SeamMoment, CountsNoArrivalAndIsNoAddress) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-only-moment"), seam_reason::none);
  const seam_status row = r.box->seams().status("test-only-moment");
  EXPECT_TRUE(row.armed);
  EXPECT_FALSE(row.addressed) << "no address to warn about never reaching";
  EXPECT_EQ(seam_reading_of(row), seam_reading::nothing_to_say);

  r.on();
  ask_at_arrival = true;
  ask_after = 100;
  static_cast<void>(r.run_to(1'000));
  const seam_status moment = r.box->seams().status("test-moment");
  EXPECT_EQ(moment.reached, 1U) << "the NOP, and the moment counts none";
  EXPECT_EQ(moment.fired, 2U) << "the NOP and the moment";
}

TEST(SeamMoment, SwitchedOffWithOneAskedItsStopRunsOnceAtTheNextBoundary) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-moment-stop"), seam_reason::none);
  ask_at_arrival = true;
  ask_after = 1'000'000;
  r.box->step();
  ASSERT_EQ(r.box->seams().disable("test-moment-stop"), seam_reason::none);
  EXPECT_TRUE(stops.empty()) << "configuration runs no handler";
  EXPECT_TRUE(r.box->seams().armed());
  EXPECT_TRUE(r.box->seams().due(r.box->time())) << "due now";

  const ticks boundary = r.box->time();
  r.box->step();
  ASSERT_EQ(stops.size(), 1U);
  EXPECT_EQ(stops[0].now, boundary);
  EXPECT_EQ(stops[0].moment, 0U) << "a stop is no moment";
  EXPECT_FALSE(stops[0].called) << "a stop is at no known instruction";
  EXPECT_FALSE(stops[0].placed);
  EXPECT_FALSE(r.box->seams().armed()) << "and the machine is plain again";

  static_cast<void>(r.run_to(r.box->time() + 2'000'000));
  EXPECT_EQ(stops.size(), 1U);
  EXPECT_TRUE(arrivals.empty()) << "nor is its moment served";
}

TEST(SeamMoment, SwitchedOffWithNoneAskedIsOwedNoStop) {
  const rig unasked;
  ASSERT_EQ(unasked.box->seams().enable("test-moment-stop"), seam_reason::none);
  unasked.box->step();
  ASSERT_EQ(unasked.box->seams().disable("test-moment-stop"),
            seam_reason::none);
  EXPECT_FALSE(unasked.box->seams().armed());
  static_cast<void>(unasked.run_to(10'000));
  EXPECT_TRUE(stops.empty());

  // Nor after its last moment was served and it asked for no other.
  const rig served_last;
  ASSERT_EQ(served_last.box->seams().enable("test-moment-stop"),
            seam_reason::none);
  ask_at_arrival = true;
  ask_after = 100;
  static_cast<void>(served_last.run_to(1'000));
  ASSERT_EQ(arrivals.size(), 1U);
  ASSERT_EQ(served_last.box->seams().disable("test-moment-stop"),
            seam_reason::none);
  static_cast<void>(served_last.run_to(served_last.box->time() + 1'000));
  EXPECT_TRUE(stops.empty());
}

TEST(SeamMoment, SwitchedBackOnBeforeTheBoundaryTheStopStillRuns) {
  // What the stop puts back is still out there whether or not the seam
  // is on again, and the seam on again starts from its own points.
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-moment-stop"), seam_reason::none);
  ask_at_arrival = true;
  ask_after = 1'000;
  r.box->step();
  ASSERT_EQ(r.box->seams().disable("test-moment-stop"), seam_reason::none);
  ASSERT_EQ(r.box->seams().enable("test-moment-stop"), seam_reason::none);
  static_cast<void>(r.run_to(r.box->time() + 10'000));
  EXPECT_EQ(stops.size(), 1U);
  EXPECT_TRUE(arrivals.empty());
}

TEST(SeamMoment, AResetDropsAStopOwed) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-moment-stop"), seam_reason::none);
  ask_at_arrival = true;
  ask_after = 1'000;
  r.box->step();
  ASSERT_EQ(r.box->seams().disable("test-moment-stop"), seam_reason::none);
  r.box->reset();
  EXPECT_FALSE(r.box->seams().armed());
  EXPECT_FALSE(r.box->seams().due(~ticks{} - 1));
}

TEST(SeamMoment, ServingOneThatWritesNothingLeavesTheRunIdentical) {
  // The moment point itself moves nothing: a run that asks for moments
  // and serves them is the run with the seam off, byte for byte, when
  // what is served writes nothing (the handler here only looks).
  const rig plain;
  static_cast<void>(plain.run_to(5'000));
  const state_hashes plain_hash = hash_state(*plain.box);
  const ticks plain_time = plain.box->time();

  const rig armed;
  armed.on();
  ask_at_arrival = true;
  ask_after = 300;
  period = 41;
  asks_left = 50;
  static_cast<void>(armed.run_to(5'000));
  ASSERT_GT(arrivals.size(), 10U);
  EXPECT_EQ(plain_time, armed.box->time());
  EXPECT_EQ(plain_hash.whole, hash_state(*armed.box).whole);
}

}  // namespace
}  // namespace amberfolio::machine
