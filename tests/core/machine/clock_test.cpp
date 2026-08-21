// SPDX-License-Identifier: AGPL-3.0-only
//
// The virtual clock on the machine: what a step costs, what the speed
// governor does to that, what `run()` does with it, and — the point of
// the whole thing — that a device's deadlines land on exactly the ticks
// it asked for whatever the program happens to be executing.
//
// The last of those is M2-F2's exit criterion, and the test that states
// it runs two programs that could hardly be less alike (a two-instruction
// jump loop, and a 65535-byte string copy that never retires) over the
// same span of virtual time, and demands the same list of deadlines from
// both. If step cost, dispatch, or the tick a handler is handed were even
// slightly sloppy, those two lists would differ.
//
// The queue's own ordering rules are tested without a machine around them
// in scheduler_test.cpp.

#include "amberfolio/machine/clock.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/scheduler.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::deadline_fired;
using test::recording_diagnostics;
using test::recording_participant;

constexpr std::uint16_t code_segment = 0x1000;

/// A machine and its sink. The same rig machine_test.cpp uses, restated
/// rather than shared: it is six lines, and a fixture that two files can
/// change out from under each other is worth less than the six lines.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {}

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  /// Put an instruction stream at `code_segment:0000` and point the
  /// processor at it. Through memory().ram(), because this is the machine
  /// loading a program rather than the program writing memory.
  void program(std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[at] = byte;
      ++at;
    }

    box->processor().reset();
    box->processor().regs()[cpu::sreg::cs] = code_segment;
    box->processor().regs().ip = 0;
  }

  /// A prefix run longer than any real instruction: the one thing the
  /// interpreter refuses outright, and so the shortest way to a stopped
  /// machine (machine_test.cpp uses the same one).
  void refusable_at(std::uint16_t offset) const {
    for (std::uint32_t i = 0; i < 300; ++i) {
      box->memory().ram()[cpu::physical_address(
          code_segment, static_cast<std::uint16_t>(offset + i))] = 0xF2;
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

/// NOP, then a jump back to it. Every step is `ran`, and it never ends.
constexpr std::initializer_list<std::uint8_t> jump_loop{0x90, 0xEB, 0xFD};

/// CLD; MOV CX,FFFF; MOV SI,2000; MOV DI,3000; REP MOVSB. After four
/// instructions every step is `repeating`, for the next 65535 of them —
/// far more than any run in this file gets through.
constexpr std::initializer_list<std::uint8_t> endless_string_copy{
    0xFC, 0xB9, 0xFF, 0xFF, 0xBE, 0x00, 0x20, 0xBF, 0x00, 0x30, 0xF3, 0xA4};

/// Run `code` on a fresh machine at `preset` until virtual tick `until`,
/// with one participant that wakes every `period` ticks from tick zero,
/// and answer every deadline it was handed.
std::vector<deadline_fired> deadlines_of(
    std::initializer_list<std::uint8_t> code, ticks period, ticks until,
    speed_preset preset) {
  const rig r;
  r.pc().set_speed(preset);
  r.program(code);

  std::vector<deadline_fired> fired;
  recording_participant ticker{0, fired};
  ticker.queue = &r.pc().deadlines();
  ticker.period = period;
  ticker.rearms = 10000;
  EXPECT_TRUE(r.pc().schedule(ticker));
  EXPECT_TRUE(r.pc().deadlines().arm(ticker, 0));

  const run_result done = r.pc().run(until);

  EXPECT_FALSE(r.pc().stopped());
  EXPECT_EQ(done.elapsed, r.pc().time());
  EXPECT_EQ(done.elapsed,
            done.steps * r.pc().step_cost_subticks() / subticks_per_tick);
  return fired;
}

/// A participant that does what the PIT will do when it exists: put the
/// timer interrupt on the line at its deadline.
class interrupting_participant final : public scheduled {
 public:
  explicit interrupting_participant(machine& pc) : pc_(&pc) {}

  void on_deadline(ticks due) override {
    woken_at = due;
    pc_->processor().raise_intr(0x08);
  }

  ticks woken_at{never};

 private:
  machine* pc_;
};

// --- What a step costs -------------------------------------------------

TEST(machine_clock, starts_at_zero_on_the_period_preset) {
  const rig r;

  EXPECT_EQ(r.pc().time(), 0u);
  EXPECT_EQ(r.pc().step_cost_subticks(),
            subticks_per_step(speed_preset::pc_xt));
}

TEST(machine_clock, charges_a_step_that_ran_an_instruction) {
  const rig r;
  r.program({0x90, 0x90, 0x90});  // NOP, NOP, NOP

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 4u);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 8u);
}

TEST(machine_clock, charges_every_iteration_of_a_repeated_string_move) {
  const rig r;
  r.program({0xFC, 0xB9, 0x03, 0x00, 0xF3, 0xA4});  // CLD; MOV CX,3; REP MOVSB

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);  // CLD
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);  // MOV CX, 3
  EXPECT_EQ(r.pc().time(), 8u);

  // Three iterations, three steps, three charges. PLAN.md §3: treating a
  // whole REP run as one step would stall the clock and block the timer
  // interrupt through a large copy, which is exactly the thing this
  // charge exists to stop.
  ASSERT_EQ(r.pc().step(), cpu::step_status::repeating);
  EXPECT_EQ(r.pc().time(), 12u);
  ASSERT_EQ(r.pc().step(), cpu::step_status::repeating);
  EXPECT_EQ(r.pc().time(), 16u);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 20u);
}

TEST(machine_clock, charges_an_interrupt_delivery) {
  const rig r;
  r.program({0x90, 0x90, 0x90});
  r.pc().processor().regs().set_flag(cpu::flag::if_, true);

  interrupting_participant timer{r.pc()};
  ASSERT_TRUE(r.pc().schedule(timer));
  ASSERT_TRUE(r.pc().deadlines().arm(timer, 4));

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 4u);

  // The deadline falls at the boundary this step begins on, the device
  // raises the line there, and the processor takes it instead of running
  // an instruction — one step, one charge.
  EXPECT_EQ(r.pc().step(), cpu::step_status::serviced);
  EXPECT_EQ(timer.woken_at, 4u);
  EXPECT_EQ(r.pc().time(), 8u);
}

TEST(machine_clock, charges_an_idle_tick_while_halted) {
  const rig r;
  r.program({0xF4});  // HLT

  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 4u);

  // Nothing was executed and time passed anyway. It has to: a halted
  // machine is waiting for an interrupt, and the only thing that can
  // bring one is a deadline arriving.
  ASSERT_EQ(r.pc().step(), cpu::step_status::halted);
  EXPECT_EQ(r.pc().time(), 8u);
  ASSERT_EQ(r.pc().step(), cpu::step_status::halted);
  EXPECT_EQ(r.pc().time(), 12u);
}

TEST(machine_clock, charges_nothing_for_a_step_taken_past_a_stop) {
  const rig r;
  r.program({});
  r.refusable_at(0);

  ASSERT_EQ(r.pc().step(), cpu::step_status::stopped);
  ASSERT_TRUE(r.pc().stopped());

  // Nothing happened, so nothing is owed — and a caller that loops on a
  // stopped machine cannot run the clock away while it does.
  EXPECT_EQ(r.pc().time(), 0u);
  ASSERT_EQ(r.pc().step(), cpu::step_status::stopped);
  ASSERT_EQ(r.pc().step(), cpu::step_status::stopped);
  EXPECT_EQ(r.pc().time(), 0u);
}

// --- The speed governor ------------------------------------------------

TEST(machine_governor, has_a_step_cost_for_every_preset) {
  EXPECT_EQ(subticks_per_step(speed_preset::pc_xt), 4u * subticks_per_tick);
  EXPECT_EQ(subticks_per_step(speed_preset::turbo_xt), 2u * subticks_per_tick);
  EXPECT_EQ(subticks_per_step(speed_preset::at), subticks_per_tick);

  // The one preset that is not a whole number of ticks: a 386 retires
  // about five instructions inside one (clock.h).
  EXPECT_LT(subticks_per_step(speed_preset::pc_386), subticks_per_tick);

  // What those are in steps a second, which is the number the presets are
  // actually claiming. M4's playtests are what retune them (clock.h).
  EXPECT_EQ(steps_per_second(speed_preset::pc_xt), 298295u);
  EXPECT_EQ(steps_per_second(speed_preset::turbo_xt), 596591u);
  EXPECT_EQ(steps_per_second(speed_preset::at), 1193182u);
  EXPECT_EQ(steps_per_second(speed_preset::pc_386), 5989305u);
}

// --- Costs smaller than a tick (clock.h's subtick accumulator) ---------
//
// The three original presets are whole numbers of ticks and always were.
// A 386 is not: it retires about five instructions inside one, so the
// clock has to be able to stand still for four steps out of five and
// still keep exact time over the long run. These are the tests that say
// it does.

/// A page of NOPs, so a test can take as many steps as it likes without
/// the program running out from under it.
void nop_forever(const rig& r) {
  for (std::uint32_t i = 0; i < 512; ++i) {
    r.pc().memory().ram()[cpu::physical_address(
        code_segment, static_cast<std::uint16_t>(i))] = 0x90;
  }
  r.pc().processor().reset();
  r.pc().processor().regs()[cpu::sreg::cs] = code_segment;
  r.pc().processor().regs().ip = 0;
}

TEST(machine_subticks, a_whole_tick_machine_advances_exactly_as_it_always_did) {
  const rig r;
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_xt);

  // The claim the accumulator must not break: on a whole-tick preset the
  // clock moves by the cost on every single step, with nothing carried.
  for (unsigned i = 1; i <= 8; ++i) {
    r.pc().step();
    EXPECT_EQ(r.pc().time(), ticks{i} * 4u) << "after " << i << " steps";
  }
}

TEST(machine_subticks, a_386_stands_still_for_most_steps_and_still_keeps_time) {
  const rig r;
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_386);

  // Five steps to a tick and a bit, so the clock is unmoved far more
  // often than it moves — and never moves backwards.
  ticks last = r.pc().time();
  unsigned stood_still = 0;
  for (unsigned i = 0; i < 256; ++i) {
    r.pc().step();
    const ticks now = r.pc().time();
    EXPECT_GE(now, last);
    if (now == last) {
      ++stood_still;
    }
    last = now;
  }
  EXPECT_GT(stood_still, 200u) << "a 386 step should usually cost no tick";

  // And over those 256 steps the total is exactly the arithmetic, with no
  // drift: 256 * 51 subticks is 51 whole ticks.
  EXPECT_EQ(r.pc().time(), 51u);
}

TEST(machine_subticks, the_carried_fraction_does_not_survive_a_speed_change) {
  const rig r;
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_386);
  r.pc().step();  // leaves 51/256 of a tick carried

  r.pc().set_speed(speed_preset::at);
  const ticks before = r.pc().time();
  r.pc().step();

  // Exactly one tick, not one-and-a-carried-fraction: the leftover
  // belonged to the machine that produced it (machine.h).
  EXPECT_EQ(r.pc().time(), before + 1u);
}

TEST(machine_subticks, a_reset_forgets_the_carried_fraction) {
  const rig r;
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_386);
  for (unsigned i = 0; i < 3; ++i) {
    r.pc().step();
  }

  r.pc().reset();
  EXPECT_EQ(r.pc().time(), 0u);
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_386);

  // If the fraction had survived, the first 256 steps of the new run
  // would land a tick early.
  for (unsigned i = 0; i < 256; ++i) {
    r.pc().step();
  }
  EXPECT_EQ(r.pc().time(), 51u);
}

TEST(machine_subticks, set_step_cost_still_means_whole_ticks) {
  const rig r;
  nop_forever(r);
  ASSERT_TRUE(r.pc().set_step_cost(3));
  EXPECT_EQ(r.pc().step_cost_subticks(), 3u * subticks_per_tick);
  r.pc().step();
  EXPECT_EQ(r.pc().time(), 3u);
}

TEST(machine_subticks, a_sub_tick_cost_can_be_set_directly) {
  const rig r;
  nop_forever(r);
  ASSERT_TRUE(r.pc().set_step_cost_subticks(subticks_per_tick / 4));
  EXPECT_FALSE(r.pc().set_step_cost_subticks(0));

  // Four steps to the tick, and the first three cost nothing.
  for (unsigned i = 0; i < 3; ++i) {
    r.pc().step();
    EXPECT_EQ(r.pc().time(), 0u);
  }
  r.pc().step();
  EXPECT_EQ(r.pc().time(), 1u);
}

TEST(machine_subticks, time_after_steps_answers_where_the_clock_will_be) {
  const rig r;
  nop_forever(r);
  r.pc().set_speed(speed_preset::pc_386);

  // The question `--steps N` asks the machine, and the reason it is the
  // machine's to answer: only it knows the carried fraction (machine.h).
  for (const std::uint64_t n : {std::uint64_t{1}, std::uint64_t{5},
                                std::uint64_t{100}, std::uint64_t{257}}) {
    const rig fresh;
    nop_forever(fresh);
    fresh.pc().set_speed(speed_preset::pc_386);
    const ticks predicted = fresh.pc().time_after_steps(n);
    for (std::uint64_t i = 0; i < n; ++i) {
      fresh.pc().step();
    }
    EXPECT_EQ(fresh.pc().time(), predicted) << "after " << n << " steps";
  }
}

TEST(machine_governor, puts_the_machine_on_the_preset_it_is_given) {
  const rig r;

  r.pc().set_speed(speed_preset::at);
  EXPECT_EQ(r.pc().step_cost_subticks(), subticks_per_tick);

  r.pc().set_speed(speed_preset::turbo_xt);
  EXPECT_EQ(r.pc().step_cost_subticks(), 2u * subticks_per_tick);
}

TEST(machine_governor, takes_a_step_cost_directly) {
  const rig r;

  EXPECT_TRUE(r.pc().set_step_cost(100));
  EXPECT_EQ(r.pc().step_cost_subticks(), 100u * subticks_per_tick);

  r.program({0x90, 0x90});
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  EXPECT_EQ(r.pc().time(), 100u);
}

TEST(machine_governor, refuses_a_step_that_would_cost_no_time) {
  const rig r;

  // A clock that never moves is a machine whose deadlines never arrive
  // and whose run() would never return.
  EXPECT_FALSE(r.pc().set_step_cost(0));
  EXPECT_EQ(r.pc().step_cost_subticks(),
            subticks_per_step(speed_preset::pc_xt));
}

TEST(machine_governor, buys_more_steps_with_the_same_virtual_time) {
  const rig xt;
  xt.pc().set_speed(speed_preset::pc_xt);
  xt.program(jump_loop);

  const rig fast;
  fast.pc().set_speed(speed_preset::at);
  fast.program(jump_loop);

  EXPECT_EQ(xt.pc().run(1000).steps, 250u);
  EXPECT_EQ(fast.pc().run(1000).steps, 1000u);
  EXPECT_EQ(xt.pc().time(), fast.pc().time());
}

TEST(machine_governor, changes_the_pace_without_changing_the_order_of_events) {
  // The same span of virtual time, the same periodic device, three
  // different amounts of work done inside it — and the same events, at
  // the same ticks, in the same order. That is what "the governor is
  // outside machine-visible time" means.
  const std::vector<deadline_fired> xt =
      deadlines_of(jump_loop, 137, 1000, speed_preset::pc_xt);
  const std::vector<deadline_fired> turbo =
      deadlines_of(jump_loop, 137, 1000, speed_preset::turbo_xt);
  const std::vector<deadline_fired> at =
      deadlines_of(jump_loop, 137, 1000, speed_preset::at);

  EXPECT_EQ(xt, turbo);
  EXPECT_EQ(xt, at);
}

// --- run() -------------------------------------------------------------

TEST(machine_run, steps_until_the_clock_reaches_the_tick_it_was_given) {
  const rig r;
  r.program(jump_loop);

  const run_result done = r.pc().run(100);

  EXPECT_EQ(done.steps, 25u);
  EXPECT_EQ(done.elapsed, 100u);
  EXPECT_EQ(r.pc().time(), 100u);
}

TEST(machine_run, overshoots_by_less_than_a_step_without_accumulating_it) {
  const rig r;
  r.program(jump_loop);

  // A step is indivisible, so 101 costs 26 of them. Stopping short would
  // mean a run() that never advanced whenever the remaining time was
  // shorter than one step.
  EXPECT_EQ(r.pc().run(101).steps, 26u);
  EXPECT_EQ(r.pc().time(), 104u);

  // The caller's next point is on its own schedule, not on this one, so
  // the overshoot is spent rather than carried.
  EXPECT_EQ(r.pc().run(200).steps, 24u);
  EXPECT_EQ(r.pc().time(), 200u);
}

TEST(machine_run, does_nothing_when_the_clock_is_already_there) {
  const rig r;
  r.program(jump_loop);
  ASSERT_EQ(r.pc().run(100).elapsed, 100u);

  const run_result done = r.pc().run(50);

  EXPECT_EQ(done.steps, 0u);
  EXPECT_EQ(done.elapsed, 0u);
  EXPECT_EQ(r.pc().time(), 100u);
}

TEST(machine_run, comes_back_early_when_the_machine_stops) {
  const rig r;
  r.program({0x90});  // NOP, then something the interpreter refuses
  r.refusable_at(1);

  const run_result done = r.pc().run(1000);

  // What it did up to the stop, and no more. Why it came back is in
  // stop(), which is sticky — run_result deliberately does not restate
  // it.
  EXPECT_EQ(done.steps, 1u);
  EXPECT_EQ(done.elapsed, 4u);
  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::processor);

  // And a second run on the stopped machine costs nothing at all.
  const run_result again = r.pc().run(2000);
  EXPECT_EQ(again.steps, 0u);
  EXPECT_EQ(again.elapsed, 0u);
}

// --- Deadlines, through the machine ------------------------------------

// M2-F2's exit criterion.
TEST(machine_deadlines, fire_at_exact_virtual_times_whatever_the_program_runs) {
  const std::vector<deadline_fired> expected{
      {.who = 0, .due = 0},   {.who = 0, .due = 137}, {.who = 0, .due = 274},
      {.who = 0, .due = 411}, {.who = 0, .due = 548}, {.who = 0, .due = 685},
      {.who = 0, .due = 822}, {.who = 0, .due = 959}};

  // 137 is prime to the step cost on purpose: every one of these
  // deadlines falls *between* two step boundaries, and is still handed
  // over as the tick it was armed for.
  EXPECT_EQ(deadlines_of(jump_loop, 137, 1000, speed_preset::pc_xt), expected);
  EXPECT_EQ(deadlines_of(endless_string_copy, 137, 1000, speed_preset::pc_xt),
            expected);
}

TEST(machine_deadlines, are_taken_by_the_instruction_at_the_same_boundary) {
  const rig r;
  r.program({0x90, 0x90, 0x90});
  r.pc().processor().regs().set_flag(cpu::flag::if_, true);

  interrupting_participant timer{r.pc()};
  ASSERT_TRUE(r.pc().schedule(timer));
  ASSERT_TRUE(r.pc().deadlines().arm(timer, 0));

  // Deadlines are dispatched at the top of the step, which is where the
  // processor recognizes interrupts — so the line this device raises is
  // taken by the very step that woke it, and no instruction ran first.
  EXPECT_EQ(r.pc().step(), cpu::step_status::serviced);
  EXPECT_EQ(timer.woken_at, 0u);
}

TEST(machine_deadlines, do_not_fire_partway_through_a_repeated_string_move) {
  const rig r;
  ASSERT_TRUE(r.pc().set_step_cost(1000));
  r.program({0xFC, 0xB9, 0x03, 0x00, 0xF3, 0xA4});

  std::vector<deadline_fired> fired;
  recording_participant ticker{0, fired};
  ASSERT_TRUE(r.pc().schedule(ticker));
  ASSERT_TRUE(r.pc().deadlines().arm(ticker, 2500));

  // 2500 falls inside the third step. It is not delivered until the
  // boundary at 3000, which is a step boundary and therefore between two
  // iterations of the REP rather than inside one.
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  ASSERT_EQ(r.pc().step(), cpu::step_status::ran);
  ASSERT_EQ(r.pc().step(), cpu::step_status::repeating);
  EXPECT_TRUE(fired.empty());

  ASSERT_EQ(r.pc().step(), cpu::step_status::repeating);
  EXPECT_EQ(fired, (std::vector<deadline_fired>{{.who = 0, .due = 2500}}));
}

TEST(machine_schedule, refuses_more_participants_than_the_scheduler_holds) {
  const rig r;
  std::vector<deadline_fired> fired;
  std::vector<std::unique_ptr<recording_participant>> participants;

  for (std::size_t i = 0; i < scheduler::max_participants; ++i) {
    participants.push_back(
        std::make_unique<recording_participant>(static_cast<int>(i), fired));
    EXPECT_TRUE(r.pc().schedule(*participants.back()));
  }

  recording_participant spare{99, fired};
  EXPECT_FALSE(r.pc().schedule(spare));

  // A wiring mistake, caught where it is made — the same answer
  // attach() gives for the same kind of mistake.
  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::conflicting_claim);
  ASSERT_EQ(r.log.stops.size(), 1u);
}

// --- Reset -------------------------------------------------------------

TEST(machine_reset, puts_the_clock_back_to_zero_and_disarms_every_deadline) {
  const rig r;
  r.program(jump_loop);

  std::vector<deadline_fired> fired;
  recording_participant ticker{0, fired};
  ASSERT_TRUE(r.pc().schedule(ticker));
  ASSERT_TRUE(r.pc().deadlines().arm(ticker, 500));
  ASSERT_EQ(r.pc().run(100).elapsed, 100u);
  ASSERT_TRUE(fired.empty());

  r.pc().reset();

  EXPECT_EQ(r.pc().time(), 0u);
  EXPECT_EQ(r.pc().deadlines().next_deadline(), never);

  // Still registered, though: who is wired to the scheduler is how the
  // machine was built, and survives the line.
  EXPECT_EQ(r.pc().deadlines().registered(), 1u);
  EXPECT_TRUE(r.pc().deadlines().arm(ticker, 4));
}

TEST(machine_reset, leaves_the_speed_governor_where_it_was_set) {
  const rig r;
  r.pc().set_speed(speed_preset::at);

  r.pc().reset();

  // A setting, not something the machine arrived at — the same reasoning
  // that keeps attached devices attached across a reset.
  EXPECT_EQ(r.pc().step_cost_subticks(), subticks_per_tick);
}

}  // namespace
}  // namespace amberfolio::machine
