// SPDX-License-Identifier: AGPL-3.0-only
//
// Virtual time: the currency everything machine-visible is counted in,
// and the governor that says what one scheduling step of it costs.
//
// PLAN.md §4 states the rule this header exists to make possible: "all
// machine-visible time derives from the virtual clock — the PIT,
// interrupt delivery, and audio synthesis run on virtual time, never host
// time. Host wall time only throttles presentation, outside machine
// state." That is what makes a run recordable and replayable, and it is
// why nothing under core/ may include the standard library's clocks or
// call anything that asks the host what time it is — a rule a script
// enforces now (scripts/check-host-time.sh, #78). There is no clock
// here to read the host with; the machine owns the counter and it
// moves only when the machine steps.
//
// **The unit is one tick of the PIT input clock**, 1,193,182 Hz. Not
// microseconds and not CPU clocks: the PIT counts in exactly this unit,
// its channel 2 output is what the speaker plays, and audio synthesis
// integrates that square wave — so counting in anything else would mean
// rounding at the one place in the machine where the arithmetic has to
// be exact. Everything else in the machine can afford the conversion;
// the timer and the speaker cannot.
//
// The counter is 64-bit and unsigned. At 1,193,182 ticks a second, 2^64
// ticks is about 1.55e13 seconds — roughly 490,000 years of emulated
// runtime — so it will not wrap, and no code in the tree needs to handle
// the case. A 32-bit counter would have wrapped in an hour, which is
// well inside one session of the game this emulator is for; that is the
// whole argument for the width.
//
// What is deliberately not here: any notion of a *rate* the machine runs
// at in real seconds. Matching virtual time to wall time is the host's
// job and is done outside machine state (M2-H*). The core does not know
// and must not care how fast the wall is turning.

#pragma once

#include <cstdint>

namespace amberfolio::machine {

/// A count of PIT input clocks since the machine was reset, or a duration
/// measured in them. Monotonic and never negative, which is why it is
/// unsigned: virtual time only ever moves forward.
using ticks = std::uint64_t;

/// The PIT input clock, 1,193,182 Hz.
///
/// The real number is 14.31818 MHz / 12 = 1,193,181.6..., a third of the
/// PC's 4.77 MHz CPU clock and a twelfth of the colour-burst crystal
/// everything on the board is divided from. It is rounded to the integer
/// here because the unit of account has to be an integer for the clock to
/// be exact, and because 0.6 of a tick in 1.19 million is four orders of
/// magnitude below anything this emulator can be wrong about.
inline constexpr ticks pit_input_hz = 1'193'182;

/// "No deadline." The largest tick there is, used as the answer when the
/// scheduler has nothing armed, so that "the next deadline is far away"
/// and "there is no next deadline" are the same comparison at every call
/// site rather than a null check at each of them.
inline constexpr ticks never = UINT64_MAX;

/// Fractions of a tick one step may cost, and the reason the governor is
/// not simply a count of ticks.
///
/// A tick is 838 nanoseconds. An 8088 cannot retire an instruction in
/// anything like that, so for the machines this emulator started with a
/// step cost of one, two or four whole ticks said everything there was to
/// say. A 386 can: at six million instructions a second it retires five
/// of them per tick, and "one step costs 0.199 ticks" is not a sentence a
/// `ticks` can hold.
///
/// So the cost is kept in 1/256ths of a tick and accumulated, with the
/// whole ticks handed to the clock as they come out. A power of two so
/// the division and the remainder are a shift and a mask, and 256 because
/// it puts the worst rounding error — a 386 asked for 5.99 MIPS gets
/// 5.99 MIPS — four orders of magnitude below anything that could matter,
/// which is the same argument `pit_input_hz` makes for rounding itself.
///
/// **Nothing changes for a whole-tick machine.** A cost that is a
/// multiple of `subticks_per_tick` leaves the accumulator at zero after
/// every step, so the clock advances by exactly the same amount on
/// exactly the same steps it always did.
inline constexpr ticks subticks_per_tick = 256;

/// The speed governor: how much virtual time one scheduling step costs.
///
/// PLAN.md §3 settles the model — "virtual time advances by a fixed cost
/// per scheduling step", with per-opcode cycle counting an explicit
/// non-goal. This game family ran across a decade of PC hardware and does
/// not depend on instruction timing; what it does depend on is that the
/// timer interrupt arrives at a plausible rate relative to how fast the
/// program is getting through its work. One number decides that, and this
/// is the number.
///
/// **These are ratios, not measurements, and M4's playtests are where
/// they get tuned.** PLAN.md §9 lists pacing feel as a known risk for
/// exactly this reason. They are deliberately one knob so that retuning
/// is a changed constant and not a changed design.
enum class speed_preset : std::uint8_t {
  /// The 4.77 MHz 8088 the game was written for: 4 ticks per step, about
  /// 298,000 steps a second.
  ///
  /// Four PIT ticks is exactly sixteen CPU clocks on that machine — the
  /// PIT input and the 8088 clock are the same crystal divided by 12 and
  /// by 3 — and sixteen clocks is a fair average for an 8088 instruction
  /// once its 4-clock bus and its permanently starved prefetch queue are
  /// paid for. The figure it lands on, a third of a MIPS, is the one the
  /// XT is remembered by.
  pc_xt,
  /// The 8-10 MHz "turbo" XT clones: 2 ticks per step, about 597,000
  /// steps a second. Roughly twice an XT, which is roughly what they
  /// were.
  turbo_xt,
  /// An AT-class machine: 1 tick per step, about 1.19 million steps a
  /// second, some four times an XT.
  ///
  /// This is the fastest preset there can be, because the unit of account
  /// is the tick and a step cannot cost less than one of them. Going
  /// faster would mean a finer currency than the PIT's own resolution,
  /// which would buy nothing the timer or the speaker can hear and would
  /// cost the exactness that is the point of the unit. If a later
  /// milestone genuinely wants a 386-speed preset, the honest change is
  /// to count in a fraction of a tick everywhere, deliberately, not to
  /// let this enum grow a zero.
  at,
  /// A 33 MHz 386DX: 51/256 of a tick per step, about 5,990,000 steps a
  /// second.
  ///
  /// Derived the same way `pc_xt` is, and stated so it can be argued
  /// with: six million instructions a second out of 33.3 MHz is five and
  /// a half clocks an instruction, which is what a 386 running 16-bit
  /// code out of memory with a wait state or two actually managed — well
  /// short of the 4.4 the instruction timings promise and well short of
  /// the Dhrystone figure the chip was sold on.
  ///
  /// This is the machine a Gold Box game feels *fast* on, and the one
  /// people ran slowdown utilities to get away from. Whether that is
  /// pleasant to play is exactly the question #107 answers by playtest.
  pc_386,
};

/// The step cost `preset` names, in `subticks_per_tick`ths of a tick.
///
/// Spelled `subticks_per_step` and not `step_cost` because `machine` has
/// a `step_cost_subticks()` accessor for the value it is currently
/// running at, and a member of that name would hide this one inside the
/// very class that needs to call it.
[[nodiscard]] constexpr ticks subticks_per_step(speed_preset preset) noexcept {
  switch (preset) {
    case speed_preset::turbo_xt:
      return 2 * subticks_per_tick;
    case speed_preset::at:
      return subticks_per_tick;
    case speed_preset::pc_386:
      // 1,193,182 * 256 / 51 = 5,989,305 steps a second.
      return 51;
    case speed_preset::pc_xt:
      break;
  }
  return 4 * subticks_per_tick;
}

/// How many steps a second `preset` runs at, for a caller that wants to
/// print it. Integer arithmetic, rounded down, and exact enough that the
/// three whole-tick presets come out on their documented figures.
[[nodiscard]] constexpr std::uint64_t steps_per_second(
    speed_preset preset) noexcept {
  return pit_input_hz * subticks_per_tick / subticks_per_step(preset);
}

/// The preset a machine starts on. The game was written for an XT, so a
/// machine nobody has configured is one.
inline constexpr speed_preset default_speed = speed_preset::pc_xt;

}  // namespace amberfolio::machine
