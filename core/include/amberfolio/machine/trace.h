// SPDX-License-Identifier: AGPL-3.0-only
//
// The window into a run: the last N instructions and the last N service
// calls, kept in the machine, dumped with the stop report (M3-F1, #83).
//
// A stop names one address. That is enough when the stop is a service
// this machine does not have — the vector and the AH say what to widen
// (#81) — and it is not nearly enough when a program has wandered
// somewhere it did not mean to go, which is the other half of what a
// first boot produces. The question then is "how did it get here", and
// the only apparatus that answers it is a record of where it just was.
//
//
// Off unless asked for
// ---------------------
//
// Recording is a setting, not state: `enable()` survives `reset()` the
// same way the speed governor does (machine.h), and a machine with it off
// pays one predictable branch per step and nothing else. That matters
// because the cost is on the hot path — `machine::step()` is the loop the
// whole emulator is — and because a ring that filled itself on every run
// would make a 400-million-step boot pay for a facility almost no run
// uses.
//
// It is also why this is a *ring* and not a log. A boot runs for hundreds
// of millions of steps; the interesting ones are the last few hundred
// before it stopped, and everything else is volume. A fixed ring keeps
// exactly the tail, in fixed storage, with no allocator and no decision
// about when to throw anything away.
//
//
// What it is not
// --------------
//
// **Not a replay.** PLAN.md §4's replay harness (M4) records inputs and
// re-runs them; this records an effect and cannot reproduce anything. The
// two do not compete — a trace tells you where the machine was, a replay
// puts it back there.
//
// **Not timestamped.** Every entry costs four bytes (a step) or eight (a
// call) precisely because it carries no tick. The stop report already
// says what step and what tick the run ended on, and the entries are the
// steps immediately before it; a per-entry tick would double the ring for
// a number the reader can already count backwards.
//
// **Not host time, ever.** Same rule as everything else under `core/`
// (machine.h): nothing in this file reads a clock of any kind.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/diagnostics.h"

namespace amberfolio::machine {

/// One recorded step: where the processor stood when it began. The
/// instruction's first byte, prefixes included — the same point
/// `notice`'s `cs`/`ip` name (diagnostics.h), so a notice and a trace
/// entry about the same instruction carry the same address.
struct trace_step {
  std::uint16_t cs{};
  std::uint16_t ip{};

  friend constexpr bool operator==(const trace_step&,
                                   const trace_step&) = default;
};

class trace_ring {
 public:
  /// Steps kept. Two hundred and fifty-six is about a screenful of
  /// disassembly and comfortably more than the longest single-purpose
  /// routine a boot runs through before it refuses something — far enough
  /// back to see the call that led in, near enough that reading the whole
  /// thing is still reading rather than searching.
  static constexpr std::size_t step_capacity = 256;

  /// Service calls kept. Fewer, because they are the rarer and more
  /// informative event: sixty-four is every BIOS and DOS call a program
  /// makes in its whole start-up sequence, several times over.
  static constexpr std::size_t call_capacity = 64;

  constexpr trace_ring() noexcept = default;

  /// Start or stop recording. A setting: `clear()` does not change it and
  /// neither does `machine::reset()`.
  constexpr void enable(bool on) noexcept { recording_ = on; }

  [[nodiscard]] constexpr bool enabled() const noexcept { return recording_; }

  /// Forget everything recorded so far, and leave `enabled()` alone.
  void clear() noexcept;

  /// Record, if recording. The check is inside rather than at every call
  /// site, so that "off unless asked for" is a property of this class
  /// rather than a rule two files have to remember.
  void record(trace_step where) noexcept;
  void record(const service_call& call) noexcept;

  /// How many steps are kept — `steps_seen()` until the ring is full,
  /// `step_capacity` after.
  [[nodiscard]] constexpr std::size_t step_count() const noexcept {
    return (steps_seen_ < step_capacity) ? static_cast<std::size_t>(steps_seen_)
                                         : step_capacity;
  }

  /// Steps recorded since the last `clear()`, including the ones the ring
  /// has since overwritten. What tells a reader whether the oldest entry
  /// is the beginning of the run or merely the beginning of the window.
  [[nodiscard]] constexpr std::uint64_t steps_seen() const noexcept {
    return steps_seen_;
  }

  /// Kept step `index`, oldest first: `step_at(0)` is the furthest back
  /// this ring still remembers and `step_at(step_count() - 1)` is the
  /// last one recorded. `index < step_count()` is a precondition.
  [[nodiscard]] trace_step step_at(std::size_t index) const noexcept;

  [[nodiscard]] constexpr std::size_t call_count() const noexcept {
    return (calls_seen_ < call_capacity) ? static_cast<std::size_t>(calls_seen_)
                                         : call_capacity;
  }

  [[nodiscard]] constexpr std::uint64_t calls_seen() const noexcept {
    return calls_seen_;
  }

  /// Kept call `index`, oldest first. Same rule as `step_at()`.
  [[nodiscard]] service_call call_at(std::size_t index) const noexcept;

 private:
  /// Where the *next* entry goes. Both rings are written at
  /// `seen % capacity`, so the index is derivable and is not kept twice.
  std::array<trace_step, step_capacity> steps_{};
  std::array<service_call, call_capacity> calls_{};
  std::uint64_t steps_seen_{};
  std::uint64_t calls_seen_{};
  bool recording_{false};
};

}  // namespace amberfolio::machine
