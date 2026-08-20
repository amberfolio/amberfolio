// SPDX-License-Identifier: AGPL-3.0-only
//
// The trace ring: that it is off until asked for, that it keeps the last
// N and not the first N, and that a full ring answers about the window it
// still has rather than about the run.
//
// The wraparound cases are the reason this file exists. A ring that is
// wrong by one is a ring that reads correctly on every run short enough
// not to fill it — which is every test that is not this one.

#include "amberfolio/machine/trace.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/diagnostics.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

[[nodiscard]] trace_step step_numbered(std::uint16_t n) {
  // The offset carries the number, so an entry can be recognised by
  // eye and by assertion.
  return trace_step{.cs = 0x1000, .ip = n};
}

[[nodiscard]] service_call call_numbered(std::uint16_t n) {
  return service_call{.vector = 0x21,
                      .ax = n,
                      .caller_cs = 0x2000,
                      .caller_ip = n,
                      .outcome = service_outcome::handled};
}

TEST(TraceRing, RecordsNothingUntilEnabled) {
  trace_ring ring;
  EXPECT_FALSE(ring.enabled());

  ring.record(step_numbered(1));
  ring.record(call_numbered(1));

  EXPECT_EQ(ring.steps_seen(), 0u);
  EXPECT_EQ(ring.step_count(), 0u);
  EXPECT_EQ(ring.calls_seen(), 0u);
  EXPECT_EQ(ring.call_count(), 0u);
}

TEST(TraceRing, KeepsWhatItIsGivenWhileItHasRoom) {
  trace_ring ring;
  ring.enable(true);

  for (std::uint16_t i = 0; i < 10; ++i) {
    ring.record(step_numbered(i));
  }

  EXPECT_EQ(ring.steps_seen(), 10u);
  EXPECT_EQ(ring.step_count(), 10u);
  for (std::size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(ring.step_at(i).ip, i) << "entry " << i;
  }
}

TEST(TraceRing, KeepsTheLastEntriesOnceItIsFull) {
  trace_ring ring;
  ring.enable(true);

  // One and a half times round, so the answer is a window that starts
  // partway through the array rather than at its beginning.
  constexpr std::size_t recorded = trace_ring::step_capacity + 100;
  for (std::size_t i = 0; i < recorded; ++i) {
    ring.record(step_numbered(static_cast<std::uint16_t>(i)));
  }

  EXPECT_EQ(ring.steps_seen(), recorded);
  EXPECT_EQ(ring.step_count(), trace_ring::step_capacity);

  // Oldest first, and the oldest kept is exactly `capacity` back from the
  // newest — the claim the report's step numbering rests on.
  const std::size_t oldest = recorded - trace_ring::step_capacity;
  for (std::size_t i = 0; i < trace_ring::step_capacity; ++i) {
    EXPECT_EQ(ring.step_at(i).ip, static_cast<std::uint16_t>(oldest + i))
        << "entry " << i;
  }
}

TEST(TraceRing, KeepsTheLastCallsOnceItIsFull) {
  trace_ring ring;
  ring.enable(true);

  constexpr std::size_t recorded = trace_ring::call_capacity + 3;
  for (std::size_t i = 0; i < recorded; ++i) {
    ring.record(call_numbered(static_cast<std::uint16_t>(i)));
  }

  EXPECT_EQ(ring.calls_seen(), recorded);
  EXPECT_EQ(ring.call_count(), trace_ring::call_capacity);
  EXPECT_EQ(ring.call_at(0).ax, static_cast<std::uint16_t>(3));
  EXPECT_EQ(ring.call_at(trace_ring::call_capacity - 1).ax,
            static_cast<std::uint16_t>(recorded - 1));
}

TEST(TraceRing, ClearingForgetsTheEntriesAndKeepsTheSetting) {
  trace_ring ring;
  ring.enable(true);
  ring.record(step_numbered(1));
  ring.record(call_numbered(1));

  ring.clear();

  EXPECT_TRUE(ring.enabled()) << "recording is a setting, not state";
  EXPECT_EQ(ring.steps_seen(), 0u);
  EXPECT_EQ(ring.step_count(), 0u);
  EXPECT_EQ(ring.calls_seen(), 0u);
  EXPECT_EQ(ring.call_count(), 0u);
}

TEST(TraceRing, StopsRecordingWhenDisabledAndKeepsWhatItHad) {
  trace_ring ring;
  ring.enable(true);
  ring.record(step_numbered(7));
  ring.enable(false);
  ring.record(step_numbered(8));

  EXPECT_EQ(ring.step_count(), 1u);
  EXPECT_EQ(ring.step_at(0).ip, 7);
}

}  // namespace
}  // namespace amberfolio::machine
