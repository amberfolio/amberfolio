// SPDX-License-Identifier: AGPL-3.0-only
//
// The deadline queue on its own, with no machine and no clock around it:
// who gets woken, in what order, and with which tick.
//
// The queue is where the determinism rule turns into code, so these are
// mostly order tests. Two of them matter more than the rest: the one that
// pins the tie-break (two deadlines on the same tick fire in registration
// order, every time) and the one that says a participant is handed the
// tick it armed rather than the tick the machine had reached. The second
// is what makes a periodic device unable to drift, and the machine-level
// proof of that is in clock_test.cpp.

#include "amberfolio/machine/scheduler.h"

#include <cstddef>
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::deadline_fired;
using test::recording_participant;

/// A queue, the log every participant in it writes to, and a way to make
/// participants that are already registered.
struct rig {
  recording_participant& participant() {
    made.push_back(std::make_unique<recording_participant>(
        static_cast<int>(made.size()), fired));
    recording_participant& who = *made.back();
    EXPECT_TRUE(queue.add(who));
    return who;
  }

  scheduler queue;
  std::vector<deadline_fired> fired;
  std::vector<std::unique_ptr<recording_participant>> made;
};

// --- Registration ------------------------------------------------------

TEST(scheduler_registration, takes_participants_up_to_its_capacity) {
  rig r;
  for (std::size_t i = 0; i < scheduler::max_participants; ++i) {
    r.participant();
  }
  EXPECT_EQ(r.queue.registered(), scheduler::max_participants);

  std::vector<deadline_fired> spare_log;
  recording_participant spare{99, spare_log};
  EXPECT_FALSE(r.queue.add(spare));
}

TEST(scheduler_registration, refuses_the_same_participant_twice) {
  rig r;
  recording_participant& who = r.participant();

  EXPECT_FALSE(r.queue.add(who));
  EXPECT_EQ(r.queue.registered(), 1u);
}

TEST(scheduler_registration, will_not_arm_a_participant_it_never_took) {
  rig r;
  std::vector<deadline_fired> stray_log;
  recording_participant stray{99, stray_log};

  EXPECT_FALSE(r.queue.arm(stray, 10));
  EXPECT_EQ(r.queue.deadline(stray), never);
  EXPECT_EQ(r.queue.next_deadline(), never);
}

// --- Arming ------------------------------------------------------------

TEST(scheduler_arming, holds_one_deadline_per_participant) {
  rig r;
  recording_participant& who = r.participant();

  EXPECT_TRUE(r.queue.arm(who, 10));
  EXPECT_TRUE(r.queue.arm(who, 20));
  EXPECT_EQ(r.queue.deadline(who), 20u);

  // The 10 is gone, not queued behind the 20.
  r.queue.dispatch_due(100);
  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 20}}));
}

TEST(scheduler_arming, disarm_takes_the_deadline_away) {
  rig r;
  recording_participant& who = r.participant();
  ASSERT_TRUE(r.queue.arm(who, 10));

  r.queue.disarm(who);

  EXPECT_EQ(r.queue.deadline(who), never);
  r.queue.dispatch_due(100);
  EXPECT_TRUE(r.fired.empty());
}

TEST(scheduler_arming, next_deadline_is_the_earliest_armed_one) {
  rig r;
  recording_participant& first = r.participant();
  recording_participant& second = r.participant();

  EXPECT_EQ(r.queue.next_deadline(), never);

  ASSERT_TRUE(r.queue.arm(first, 500));
  EXPECT_EQ(r.queue.next_deadline(), 500u);

  ASSERT_TRUE(r.queue.arm(second, 40));
  EXPECT_EQ(r.queue.next_deadline(), 40u);

  r.queue.disarm(second);
  EXPECT_EQ(r.queue.next_deadline(), 500u);
}

// --- Dispatch ----------------------------------------------------------

TEST(scheduler_dispatch, wakes_nothing_before_its_deadline) {
  rig r;
  recording_participant& who = r.participant();
  ASSERT_TRUE(r.queue.arm(who, 100));

  r.queue.dispatch_due(99);

  EXPECT_TRUE(r.fired.empty());
  EXPECT_EQ(r.queue.deadline(who), 100u);
}

TEST(scheduler_dispatch, hands_over_the_tick_that_was_armed) {
  rig r;
  recording_participant& who = r.participant();
  ASSERT_TRUE(r.queue.arm(who, 41));

  // Virtual time moves in whole steps, so the machine passes 41 without
  // landing on it. The deadline is still 41.
  r.queue.dispatch_due(44);

  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 41}}));
}

TEST(scheduler_dispatch, wakes_a_deadline_already_in_the_past) {
  rig r;
  recording_participant& who = r.participant();

  // "Overdue", which is what a device says when it worked out its next
  // edge from a moment the machine has since gone past.
  ASSERT_TRUE(r.queue.arm(who, 7));
  r.queue.dispatch_due(1000);

  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 7}}));
}

TEST(scheduler_dispatch, wakes_the_earliest_deadline_first) {
  rig r;
  recording_participant& first = r.participant();
  recording_participant& second = r.participant();
  recording_participant& third = r.participant();

  // Armed out of order and out of registration order, so nothing but the
  // deadlines can be deciding this.
  ASSERT_TRUE(r.queue.arm(second, 30));
  ASSERT_TRUE(r.queue.arm(third, 10));
  ASSERT_TRUE(r.queue.arm(first, 20));

  r.queue.dispatch_due(100);

  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 2, .due = 10},
                                                  {.who = 0, .due = 20},
                                                  {.who = 1, .due = 30}}));
}

// The tie-break, twice: same deadlines, opposite registration order. A
// 100 Hz timer and a 200 Hz one share every other edge exactly, so this
// is not a corner case, and something has to go first. It is registration
// order — how the machine was wired — because that is the only ordering
// nothing the emulated program does can disturb.
TEST(scheduler_dispatch, breaks_a_tie_on_one_tick_by_registration_order) {
  rig r;
  recording_participant& first = r.participant();
  recording_participant& second = r.participant();

  // Armed in the reverse order, to prove that arming order is not it.
  ASSERT_TRUE(r.queue.arm(second, 500));
  ASSERT_TRUE(r.queue.arm(first, 500));

  r.queue.dispatch_due(500);

  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 500},
                                                  {.who = 1, .due = 500}}));
}

TEST(scheduler_dispatch,
     keeps_that_tie_break_when_the_arming_order_is_reversed) {
  rig r;
  recording_participant& first = r.participant();
  recording_participant& second = r.participant();

  ASSERT_TRUE(r.queue.arm(first, 500));
  ASSERT_TRUE(r.queue.arm(second, 500));

  r.queue.dispatch_due(500);

  // Participant 0 is still first, because it was registered first — the
  // point being that the answer is fixed by the wiring and not by which
  // of the two happened to arm first.
  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 500},
                                                  {.who = 1, .due = 500}}));
}

TEST(scheduler_dispatch, is_a_one_shot) {
  rig r;
  recording_participant& who = r.participant();
  ASSERT_TRUE(r.queue.arm(who, 10));

  r.queue.dispatch_due(10);
  r.queue.dispatch_due(1000);

  EXPECT_EQ(r.fired.size(), 1u);
  EXPECT_EQ(r.queue.deadline(who), never);
}

TEST(scheduler_dispatch, delivers_every_edge_of_a_short_period) {
  rig r;
  recording_participant& who = r.participant();
  who.queue = &r.queue;
  who.period = 3;
  who.rearms = 100;
  ASSERT_TRUE(r.queue.arm(who, 0));

  // A PIT with a small divisor really does produce several edges in the
  // time one instruction takes. Merging them into one would be faking.
  r.queue.dispatch_due(10);

  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 0},
                                                  {.who = 0, .due = 3},
                                                  {.who = 0, .due = 6},
                                                  {.who = 0, .due = 9}}));
  EXPECT_EQ(r.queue.deadline(who), 12u);
}

TEST(scheduler_dispatch, interleaves_two_periods_by_deadline) {
  rig r;
  recording_participant& slow = r.participant();
  recording_participant& fast = r.participant();
  for (recording_participant* who : {&slow, &fast}) {
    who->queue = &r.queue;
    who->rearms = 100;
  }
  slow.period = 4;
  fast.period = 2;
  ASSERT_TRUE(r.queue.arm(slow, 4));
  ASSERT_TRUE(r.queue.arm(fast, 2));

  r.queue.dispatch_due(8);

  // Tick 4 and tick 8 belong to both, and `slow` is registered first.
  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 1, .due = 2},
                                                  {.who = 0, .due = 4},
                                                  {.who = 1, .due = 4},
                                                  {.who = 1, .due = 6},
                                                  {.who = 0, .due = 8},
                                                  {.who = 1, .due = 8}}));
}

TEST(scheduler_dispatch, refuses_a_rearm_that_would_never_let_the_step_end) {
  rig r;
  recording_participant& who = r.participant();
  who.queue = &r.queue;
  who.period = 0;  // "wake me again at the tick you just woke me at"
  who.rearms = 100;
  ASSERT_TRUE(r.queue.arm(who, 10));

  r.queue.dispatch_due(10);

  EXPECT_FALSE(who.rearmed);
  EXPECT_EQ(r.fired.size(), 1u);
  EXPECT_EQ(r.queue.deadline(who), never);
}

TEST(scheduler_dispatch, sorts_a_rearm_in_against_a_pending_deadline) {
  rig r;
  recording_participant& periodic = r.participant();
  recording_participant& once = r.participant();

  periodic.queue = &r.queue;
  periodic.period = 5;
  periodic.rearms = 1;
  ASSERT_TRUE(r.queue.arm(periodic, 10));
  ASSERT_TRUE(r.queue.arm(once, 12));

  r.queue.dispatch_due(20);

  // The re-arm to 15 lands behind a deadline that was already sitting
  // there, so the pass is not simply "everyone in turn": it is a re-scan
  // after every handler, which is what a periodic device needs to stay
  // interleaved with everything else.
  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 10},
                                                  {.who = 1, .due = 12},
                                                  {.who = 0, .due = 15}}));
}

// --- Reset -------------------------------------------------------------

TEST(scheduler_reset, disarm_all_keeps_the_registrations) {
  rig r;
  recording_participant& first = r.participant();
  recording_participant& second = r.participant();
  ASSERT_TRUE(r.queue.arm(first, 10));
  ASSERT_TRUE(r.queue.arm(second, 20));

  r.queue.disarm_all();

  EXPECT_EQ(r.queue.next_deadline(), never);
  EXPECT_EQ(r.queue.registered(), 2u);

  // Still known, so still able to arm: who is wired to the scheduler
  // survives a reset, what they had posted does not.
  EXPECT_TRUE(r.queue.arm(first, 30));
  r.queue.dispatch_due(100);
  EXPECT_EQ(r.fired, (std::vector<deadline_fired>{{.who = 0, .due = 30}}));
}

}  // namespace
}  // namespace amberfolio::machine
