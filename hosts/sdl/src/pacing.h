// SPDX-License-Identifier: AGPL-3.0-only
//
// Where a run slice ends and how long the host waits after it (#472, split
// from main.cpp). Virtual time first and wall time second, never the other
// way round: PLAN.md §4, and main.cpp's account of the loop.

#pragma once

#include <chrono>
#include <cstdint>

#include "amberfolio/machine/machine.h"

namespace amberfolio::sdl {

/// Where this run slice has to stop: the next frame boundary, or a
/// budget, whichever comes first.
///
/// Clamping the slice rather than checking after it is what makes
/// `--steps N` end on step N rather than somewhere inside frame N+1. A
/// step budget becomes a tick budget through
/// `machine::time_after_steps()`, which is the machine's own arithmetic
/// because it is the only thing that knows the fraction of a tick
/// carried over from the last step — on a machine faster than one
/// instruction per tick, doing the multiplication out here would land a
/// tick away from the step actually asked for.
///
/// A replay's next event is one more thing that may bring it closer, and
/// the reason it is a clamp and not a check: an event the machine
/// consumes has to arrive on the exact tick it was recorded at, and a
/// loop that noticed the tick after running through it would already
/// have run the wrong machine. The one thing the player does not answer
/// here is a checkpoint of a stopped machine, which this loop has to be
/// allowed to run past in order to arrive at (machine/replay.h).
[[nodiscard]] machine::ticks slice_end(const machine::machine& box,
                                       machine::ticks frame_ticks,
                                       std::uint64_t step_budget,
                                       machine::ticks tick_budget,
                                       machine::ticks next_event);

/// Sleep whatever wall time is left before the run's schedule says frame
/// `frame_index` should end, and never a negative one. `pace_origin` is the
/// one fixed instant the whole run is scheduled against; `fast` is
/// `--fast N`'s N, and never zero here (`--fast max` does not wait at all).
void wait_for_frame(std::chrono::steady_clock::time_point pace_origin,
                    machine::ticks frame_ticks, double fast,
                    std::uint64_t frame_index);

}  // namespace amberfolio::sdl
