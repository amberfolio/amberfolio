// SPDX-License-Identifier: AGPL-3.0-only

#include "pacing.h"

#include <chrono>
#include <cstdint>
#include <thread>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pit.h"

namespace amberfolio::sdl {

[[nodiscard]] machine::ticks slice_end(const machine::machine& box,
                                       machine::ticks frame_ticks,
                                       std::uint64_t step_budget,
                                       machine::ticks tick_budget,
                                       machine::ticks next_event) {
  machine::ticks target = box.time() + frame_ticks;

  if (tick_budget != 0 && tick_budget < target) {
    target = tick_budget;
  }

  if (next_event < target) {
    target = next_event;
  }

  if (step_budget != 0 && box.steps() < step_budget) {
    // Saturating, so a budget too big for the clock leaves the frame
    // boundary alone — which is right, because a run cannot reach it.
    const machine::ticks by_steps =
        box.time_after_steps(step_budget - box.steps());
    if (by_steps < target) {
      target = by_steps;
    }
  }

  return target;
}

void wait_for_frame(std::chrono::steady_clock::time_point pace_origin,
                    machine::ticks frame_ticks, double fast,
                    std::uint64_t frame_index) {
  // Whatever wall time is left before *this run's schedule* says
  // frame `frame_index` should end, and never a negative one: a host
  // that fell behind simply does not sleep. It does not then run the
  // machine faster to compensate — see main.cpp's top comment and
  // `pace_origin`'s there, for why the schedule is one fixed origin
  // and not "whatever this frame took."
  //
  // `--fast N` divides the budget and nothing else. The machine has
  // already been run to the same tick it would have been run to
  // anyway; all that changes is how long this thread waits before
  // going round again, which is the one place wall time is allowed
  // to appear at all.
  const auto frame_budget = std::chrono::duration<double>(
      static_cast<double>(frame_ticks) / machine::pit_input_hz / fast);
  const auto deadline =
      pace_origin +
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          frame_budget * static_cast<double>(frame_index + 1));
  const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now());
  if (left.count() > 0) {
    std::this_thread::sleep_for(left);
  }
}

}  // namespace amberfolio::sdl
