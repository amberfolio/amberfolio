// SPDX-License-Identifier: AGPL-3.0-only
//
// The renderer: turns the EGA's four planes and its palette into the
// 320x200 indexed framebuffer platform.h promises a host, on a 60 Hz
// virtual-time deadline. M2-D3 (#48), and mostly composition over what
// #47 and #45 already built — this file's own logic is small; the
// contracts it has to honour are the point.
//
//
// A scheduled participant, not a device
// --------------------------------------
//
// scheduler.h predicted this exactly: "the renderer... will be scheduled
// and answer no bus cycles at all." There is no memory window or port to
// claim — a program never talks to a renderer — so this implements
// `scheduled` alone and is registered with `machine::schedule()`, not
// `machine::attach()`.
//
// Because it is not a `device`, `machine::reset()` does not know to
// re-arm it — that loop only walks attached devices (machine.h). So
// `reset()` here is this class's own counterpart to what a device's
// `reset()` does, and whoever owns a renderer (today, a test; later,
// whatever M3 wires the whole machine together) must call it once after
// construction and again after every `machine::reset()`, the same way a
// device's own `reset()` is what would re-arm a deadline "from power-on"
// (scheduler.h's phrase for exactly this).
//
//
// The frame period, and its rounding
// ------------------------------------
//
// 60 Hz in *virtual* time (PLAN.md §4's "all machine-visible time derives
// from the virtual clock") means a period in ticks of the PIT input
// clock, not a period in wall-clock milliseconds — clock.h forbids the
// core from knowing what a millisecond of the host's clock is. The exact
// value is `pit_input_hz / 60 = 1,193,182 / 60 = 19,886.3667`, not a
// whole number of ticks, and the unit of account has to be a whole
// number (clock.h again). Truncating to 19,886 loses about 0.37 ticks of
// period every frame, which is 0.0018% of a frame — over a whole hour of
// continuous rendering (216,000 frames) that is roughly 79,000 ticks,
// under 70 milliseconds of drift against a real 60.000 Hz refresh.
// PLAN.md §4 is explicit that matching wall time is the host's problem,
// not the core's, and this is nowhere near a number this project has
// promised to get right — `clock.h`'s own rounding of `pit_input_hz`
// itself argues the identical case at a finer grain. What *is* promised,
// and what truncation gives for free, is that every rearmed deadline is
// exactly `frame_period` ticks after the one before it, forever: `due +
// frame_period` from a fixed `due` never drifts against itself, which is
// scheduler.h's whole reason a handler is handed the tick it armed rather
// than the tick the machine reached. "Frame deadlines land at exact
// virtual times" is a claim about that self-consistency, not about
// matching a real display's crystal, and truncation is the simplest
// value that keeps it.
//
//
// The pull contract
// -------------------
//
// platform.h's framebuffer is filled and published from inside
// `machine::run()` and read only outside it (the design essay at the top
// of that file). `on_deadline` runs from `scheduler::dispatch_due`, which
// `machine::step()` calls at a step boundary — squarely inside `run()` —
// so composing straight into `display().writable_pixels()` and then
// calling `complete()` is already on the right side of the line; there is
// nothing this file has to do to honour the contract beyond not doing
// anything else. No scratch buffer, no callback out to a host: the design
// essay's "one buffer, not double-buffered" argument is why.

#pragma once

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/scheduler.h"

namespace amberfolio::machine {

class machine;

/// Composes `video`'s planes and palette into `box`'s framebuffer on a
/// 60 Hz virtual-time deadline.
class renderer final : public scheduled {
 public:
  /// See this file's top comment, "The frame period, and its rounding."
  /// The number itself is the card's vertical rate and lives with the
  /// card (`ega::frame_period`), because since #88 the EGA computes its
  /// own raster position against it and the two must be the same period
  /// or 3DAh would report a beam the renderer does not follow.
  static constexpr ticks frame_period = ega::frame_period;

  renderer(machine& box, const ega& video) noexcept
      : box_(&box), video_(&video) {}

  /// Arm the first frame deadline. Call once after construction and again
  /// after every `machine::reset()` — see this file's top comment for why
  /// a renderer, unlike an attached device, needs to be told.
  void reset();

  void on_deadline(ticks due) override;

 private:
  void compose() const;

  machine* box_;
  const ega* video_;
};

}  // namespace amberfolio::machine
