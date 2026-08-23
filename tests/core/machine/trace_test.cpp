// SPDX-License-Identifier: AGPL-3.0-only
//
// The trace ring: that it is off until asked for, that each of its three
// rings keeps the last N and not the first N, and that a full one answers
// about the window it still has rather than about the run.
//
// The wraparound cases are the reason this file exists. A ring that is
// wrong by one is a ring that reads correctly on every run short enough
// not to fill it — which is every test that is not this one.

#include "amberfolio/machine/trace.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/vfs.h"
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

/// A naming call carrying the number in its handle and its caller, so an
/// entry is recognisable by eye the way the other two are. The path is
/// the root, because what this file is about is the ring and not the
/// rendering — `dos_test.cpp` is where a real path goes through it.
[[nodiscard]] file_event file_numbered(std::uint16_t n) {
  return file_event{.what = file_action::open,
                    .path = dos_path{},
                    .handle = n,
                    .error = vfs_error::file_not_found,
                    .caller_cs = 0x3000,
                    .caller_ip = n};
}

TEST(TraceRing, RecordsNothingUntilEnabled) {
  trace_ring ring;
  EXPECT_FALSE(ring.enabled());

  ring.record(step_numbered(1));
  ring.record(call_numbered(1));
  ring.record(file_numbered(1));

  EXPECT_EQ(ring.steps_seen(), 0u);
  EXPECT_EQ(ring.step_count(), 0u);
  EXPECT_EQ(ring.calls_seen(), 0u);
  EXPECT_EQ(ring.call_count(), 0u);
  EXPECT_EQ(ring.files_seen(), 0u);
  EXPECT_EQ(ring.file_count(), 0u);
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

TEST(TraceRing, KeepsTheLastNamingCallsOnceItIsFull) {
  // The third ring (#121). Its entries are the big ones — a whole
  // `dos_path` apiece — which is exactly why the wraparound is worth
  // asserting separately rather than assumed to follow from the others.
  trace_ring ring;
  ring.enable(true);

  constexpr std::size_t recorded = trace_ring::file_capacity + 5;
  for (std::size_t i = 0; i < recorded; ++i) {
    ring.record(file_numbered(static_cast<std::uint16_t>(i)));
  }

  EXPECT_EQ(ring.files_seen(), recorded);
  EXPECT_EQ(ring.file_count(), trace_ring::file_capacity);
  EXPECT_EQ(ring.file_at(0).handle, static_cast<std::uint16_t>(5));
  EXPECT_EQ(ring.file_at(trace_ring::file_capacity - 1).handle,
            static_cast<std::uint16_t>(recorded - 1));
}

TEST(TraceRing, ClearingForgetsTheEntriesAndKeepsTheSetting) {
  trace_ring ring;
  ring.enable(true);
  ring.record(step_numbered(1));
  ring.record(call_numbered(1));
  ring.record(file_numbered(1));

  ring.clear();

  EXPECT_TRUE(ring.enabled()) << "recording is a setting, not state";
  EXPECT_EQ(ring.steps_seen(), 0u);
  EXPECT_EQ(ring.step_count(), 0u);
  EXPECT_EQ(ring.calls_seen(), 0u);
  EXPECT_EQ(ring.call_count(), 0u);
  EXPECT_EQ(ring.files_seen(), 0u);
  EXPECT_EQ(ring.file_count(), 0u);
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
