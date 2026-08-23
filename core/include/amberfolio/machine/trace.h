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
// **Not timestamped.** A step entry costs four bytes and a call entry
// eight precisely because neither carries a tick. The stop report already
// says what step and what tick the run ended on, and the entries are the
// steps immediately before it; a per-entry tick would double the ring for
// a number the reader can already count backwards.
//
//
// The third ring: which file (#121)
// ---------------------------------
//
// A call entry says `INT21 ax=3D00` and where it came from. It cannot say
// *which file*, because it is built as the stub is reached and the path
// does not exist as an answer until the handler has resolved it
// (diagnostics.h's `file_event`). That gap cost a directory audit once:
// a program built its paths from a config naming an absolute directory
// this machine's root does not have, every open failed, and a failed open
// is a legitimate DOS answer — so the run's own account of itself had
// nothing in it about the thing that had gone wrong. The live stream a
// host prints under `--trace` shows the file lines, but a stop report is
// what a reader reads *after* the fact, and the tail is the part that
// matters. So the naming calls get a ring of their own here, and the
// trace report renders it.
//
// **A `dos_path` is not variable-length**, which is what lets this ring
// stay a ring. By the time a `file_event` exists the name has been
// through `canonicalize()`, and what comes out is at most
// `dos_path::max_depth` components of at most `dos_name::max_length`
// characters — a fixed array, 105 bytes, a compile-time fact. Nothing is
// truncated to fit, and nothing can be: `canonicalize()` *refuses* a path
// deeper than the type holds rather than shortening it (vfs.h), because a
// truncated path would name a different real file. So an entry here is a
// `file_event` verbatim — no arena, no allocator, no offsets into a
// character pool that the next entry might overwrite — and
// `report.h`'s `trace_report_capacity` bounds the rendering of it the
// same way it bounds the other two.
//
// Fewer of them than calls, because they are rarer still: a whole boot
// names a few dozen files, and a save or a load names a handful. The one
// thing this ring must be able to hold in full is the last file
// transaction before a stop, and thirty-two covers that many times over.
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

  /// Naming DOS calls kept — opens, creates, mkdirs, unlinks and closes
  /// (diagnostics.h's `file_action`). Fewer still than service calls, and
  /// for the reason the header comment gives: a boot names a few dozen
  /// files across its whole run, and the question this ring answers is
  /// about the last few.
  static constexpr std::size_t file_capacity = 32;

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
  void record(const file_event& event) noexcept;

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

  [[nodiscard]] constexpr std::size_t file_count() const noexcept {
    return (files_seen_ < file_capacity) ? static_cast<std::size_t>(files_seen_)
                                         : file_capacity;
  }

  [[nodiscard]] constexpr std::uint64_t files_seen() const noexcept {
    return files_seen_;
  }

  /// Kept file event `index`, oldest first. Same rule as `step_at()`.
  /// Answered by value like the other two, and it is the one entry big
  /// enough for that to be worth saying out loud: a `file_event` carries
  /// a whole `dos_path`, so this is a hundred-odd bytes copied. A caller
  /// is a report writer looking at one entry at a time, which is what
  /// makes that the right trade against handing out a reference into a
  /// ring the next call may overwrite.
  [[nodiscard]] file_event file_at(std::size_t index) const noexcept;

 private:
  /// Where the *next* entry goes. All three rings are written at
  /// `seen % capacity`, so the index is derivable and is not kept twice.
  std::array<trace_step, step_capacity> steps_{};
  std::array<service_call, call_capacity> calls_{};
  std::array<file_event, file_capacity> files_{};
  std::uint64_t steps_seen_{};
  std::uint64_t calls_seen_{};
  std::uint64_t files_seen_{};
  bool recording_{false};
};

}  // namespace amberfolio::machine
