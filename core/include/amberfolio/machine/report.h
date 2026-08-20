// SPDX-License-Identifier: AGPL-3.0-only
//
// The stop report: what the machine says when a run ends, in one fixed
// format (M3-F1, #83).
//
// M3's method is a loop — boot the player's copy, read the line, widen
// the one thing it names, boot again (#94) — and the loop is only as good
// as the line. #81 promised the shape of it: the reason, the service or
// the register alongside it, the caller's CS:IP, and the step and the
// tick it happened at. This is that promise, rendered.
//
//
// Why the format lives in core and not in each host
// --------------------------------------------------
//
// Because M3's exit criterion is "verified locally on desktop **and**
// web" (PLAN.md §7), and M3-F2 (#84) states the test of it plainly: a
// player's directory boots in the browser "to the same stop line the
// desktop host reports, at the same step". Two hosts formatting their own
// lines would produce two claims that *look* the same and could quietly
// differ — a hex width here, a missing field there — and the one thing
// that comparison rests on is that the lines are byte for byte the same
// sentence. So the sentence is written once, below both of them, and each
// host's job is to put the characters somewhere a person can read.
//
// It is also what gives the wasm host a report at all. The diagnostics
// sink is a C++ interface (diagnostics.h) and does not cross the C ABI;
// the page therefore hears nothing about a stop beyond a number. A
// formatted block it can ask for by calling one function is how the same
// account reaches a browser (`af_machine_stop_report`, abi.h).
//
//
// The format
// ----------
//
// Lines, each beginning `amberfolio: stop `, each a sequence of
// `key=value` pairs, values in hexadecimal where the machine thinks in
// hexadecimal and decimal where it counts. The prefix is part of the
// format and not the host's decoration, for the reason above; it is also
// what makes the whole report one `grep` away in a log that has other
// things in it.
//
// A real report, from the first boot this was written for. **Each entry
// is one line**; the first two are shown wrapped because they do not fit
// in eighty columns. (Not wrapped with a trailing `\`, which inside a
// `//` comment continues the comment rather than the example — GCC warns
// about it and this file earned that warning once already.)
//
//     amberfolio: stop reason=unimplemented_service steps=99172
//         ticks=396688 frames=20 cs=F000 ip=0121 at=0B5D2
//     amberfolio: stop call=INT21 ah=35 al=00 ax=3500
//         from=0B58:0052 outcome=handled
//     amberfolio: stop next=INT 21h AH=35h AL=00h
//
// `outcome=handled` beside a service-shaped stop is not a contradiction:
// the vector *has* a handler and it ran, and this is that handler's own
// refusal of one AH. `outcome=unimplemented` is the other case — a
// vector nothing backs at all.
//
// The last line is the worklist entry #83 asks for: the one thing to
// widen next, named by the machine rather than inferred by a reader. It
// is absent when there is nothing to widen — a program that exited has
// not asked for anything.
//
// The trace report is the same idea over `trace_ring`'s contents, and is
// empty unless tracing was on.
//
//
// Writing without an allocator
// -----------------------------
//
// Every function here writes into a caller-supplied `std::span<char>`,
// NUL-terminates, and answers how many characters it wrote. It never
// allocates and never throws (PLAN.md §4). A buffer too small is
// truncated rather than refused — a short report is still a report, and a
// stop that produced no output at all because the buffer was a byte
// under would be the worst possible failure mode for the one facility
// whose job is to say what happened. `stop_report_capacity` and
// `trace_report_capacity` below are sized so that never happens to a
// caller that uses them.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/diagnostics.h"

namespace amberfolio::machine {

class machine;

/// Why a host's run loop came back, when the machine itself had not
/// stopped.
///
/// A stop is the machine's own word and `stop_reason` is where it is
/// recorded. This is the *host's*: a boot driver that gave up on a hang
/// after `--steps N`, or a player who closed the window, has ended the
/// run without the machine having refused anything. The report needs to
/// be able to say which, because a run that was cut short at a budget and
/// a run that stopped on a service it does not have are two entirely
/// different findings — and because a hang is exactly what M3-F1 exists
/// to turn into a line with a CS:IP on it.
enum class run_end : std::uint8_t {
  /// The machine stopped, and `machine::stop()` says why. The ordinary
  /// case, and the only one in which the reason printed is the machine's.
  stopped,
  /// The host's step budget ran out with the machine still running.
  step_budget,
  /// The host's virtual-time budget ran out with the machine still
  /// running.
  tick_budget,
  /// The host was asked to quit — a closed window, an interrupted page.
  host_quit,
};

/// Characters `format_stop_report()` can produce, the terminator
/// included. Every line it writes is bounded — the fields are fixed-width
/// hexadecimal or counters that cannot exceed twenty digits — so this is
/// a real ceiling rather than a guess, and a caller with a buffer this
/// size never sees a truncated report.
inline constexpr std::size_t stop_report_capacity = 512;

/// The same, for `format_trace_report()`.
///
/// `trace_ring::step_capacity` step lines at 62 characters apiece (the
/// prefix, a step number of up to twenty digits, and a far address) plus
/// `trace_ring::call_capacity` call lines at 79 plus the header is a
/// little over twenty-one thousand; this is the next round number above
/// it. Both counts are compile-time constants of `trace_ring`, so the
/// arithmetic is a fact rather than an estimate.
inline constexpr std::size_t trace_report_capacity = 24576;

/// The printable name of a stop reason — `unimplemented_service`,
/// `unimplemented_device`, and so on: the enumerator's own spelling, so
/// that a line in a log and a line in diagnostics.h are searchable with
/// the same string. Never null.
[[nodiscard]] const char* stop_reason_name(stop_reason reason) noexcept;

/// The printable name of a `run_end`. Never null.
[[nodiscard]] const char* run_end_name(run_end how) noexcept;

/// The printable name of a notice kind — for a host rendering the
/// diagnostics stream, which is the other half of what a boot log is.
/// Never null.
[[nodiscard]] const char* notice_kind_name(notice_kind what) noexcept;

/// The printable name of the processor's own stop reason
/// (cpu::stop_reason). Never null.
[[nodiscard]] const char* cpu_stop_reason_name(
    cpu::stop_reason reason) noexcept;

/// Render the report for a run that ended `how`, into `out`.
///
/// Answers the number of characters written, the terminator not counted;
/// `out` is always NUL-terminated when it has room for even that.
std::size_t format_stop_report(const machine& box, run_end how,
                               std::span<char> out);

/// Render the trace ring, into `out`. Writes a single line saying so when
/// tracing was never enabled, rather than nothing at all — "there is no
/// trace" is an answer a reader needs, and an empty buffer looks like a
/// facility that failed.
std::size_t format_trace_report(const machine& box, std::span<char> out);

}  // namespace amberfolio::machine
