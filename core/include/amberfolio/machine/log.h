// SPDX-License-Identifier: AGPL-3.0-only
//
// The account, kept for a host that cannot be handed records (M4-W1,
// #108).
//
// diagnostics.h is a C++ interface that hands out structured records held
// by reference. That is the right shape for a host compiled into the same
// binary — the SDL host renders them straight to stderr and keeps nothing
// — and it is the wrong shape for the C ABI, whose rules (abi.h) are the
// opposite of every word in that sentence. So the ABI does not widen: it
// installs one of these, which turns each record into the line
// machine/report.h says it is and keeps the last few kilobytes of them
// for the host to drain as characters.
//
// The consequence is the point of the exercise. Before this, a run in a
// browser said nothing about what the program did beyond the number it
// stopped with, while the same run on the desktop printed its notices,
// its file activity and every seam transition. Driving M4's legs on the
// dev page (docs/playable.md) meant driving them blind by comparison.
//
//
// Not machine state
// -----------------
//
// This is a host facility that sits *beside* a machine, not inside one:
// `console_output` is a member of `machine` and is serialized with it
// (platform.h, state.h), and this deliberately is not. Nothing here is
// hashed, saved, replayed or compared, and a host that drains at a
// different cadence than another host is still running the same machine.
// That is what lets it exist at all without moving a replay hash (#100),
// and it is why `clear()` is the host's call rather than `reset()`'s.
//
//
// Overflow
// --------
//
// The same policy as `console_output`, for consistency and for one reason
// of its own: a full ring drops the *newest* line and counts it. What
// that loses is the tail of a run — and the most valuable line in the
// tail, the stop, is separately recoverable from `format_stop_report()`,
// which does not go through here at all. What dropping the oldest would
// lose is the first-touch notices, which fire once per run and are gone
// for good (diagnostics.h). So the newest is the cheaper loss, and the
// count says how much of it there was.
//
// Lines are whole or absent: a line that will not fit is dropped
// entirely rather than written in part, because half a line in a log is
// worse than a missing one — it reads as a fact.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/report.h"

namespace amberfolio::machine {

/// A `diagnostics` sink that renders to text and keeps it for draining.
class diagnostic_log final : public diagnostics {
 public:
  /// Eight kilobytes of backlog — twice `console_output`'s, because a
  /// line is two orders of magnitude wider than a byte and a host draining
  /// once per frame should never come near either end of it.
  static constexpr std::size_t capacity = 8192;

  /// Whether to keep the service-call and file-event streams.
  ///
  /// Off by default, for the reason diagnostics.h gives: a call is
  /// something the program *did*, not a symptom of anything, and a boot
  /// makes tens of thousands of them. The SDL host's `--trace` turns the
  /// same two streams on, so the live stream and the ring dumped at the
  /// end stay one facility asked for once.
  void set_tracing(bool on) noexcept { tracing_ = on; }
  [[nodiscard]] bool tracing() const noexcept { return tracing_; }

  /// Consumer: the host. Copies out up to `out.size()` characters and
  /// removes them, answering how many. Not NUL-terminated — this is a
  /// stream of lines, and the count is the length.
  ///
  /// A drain can land mid-line when the host's buffer is smaller than
  /// what is waiting; the remainder is the next drain's first characters,
  /// so a host that concatenates what it reads sees whole lines. Nothing
  /// is lost by a short read.
  [[nodiscard]] std::size_t read(std::span<char> out) noexcept;

  [[nodiscard]] std::size_t pending() const noexcept { return count_; }

  /// Lines the ring had no room for. Not reset by `read()`: it is a
  /// property of the run, and a host that wants to say "the log was
  /// truncated" needs the total.
  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

  /// Throw away what has not been drained, and the drop count with it.
  /// The host's call, not the machine's — see the note above.
  void clear() noexcept;

  /// A second sink, handed every record this one is handed, before it is
  /// rendered.
  ///
  /// This exists because of where this object sits. A machine holds one
  /// sink for its whole life, and in the wasm module that one is *this* —
  /// it is a member of the ABI's own handle, so that a JS host can read
  /// as text the account a C++ host is handed as records. A C++ consumer
  /// inside the same module that also wants those records has nowhere
  /// else to stand. The automap's exploration sidecar is the first
  /// (M5-E2c, #173): it learns which save slot the program touched from
  /// the DOS layer's file events and from nothing else.
  ///
  /// Held by reference and never owned, like the sink itself. Null is the
  /// ordinary state and costs one branch per record. It changes nothing
  /// about what is kept or dropped here.
  void set_relay(diagnostics* also) noexcept { relay_ = also; }
  [[nodiscard]] diagnostics* relay() const noexcept { return relay_; }

  void report(const notice& what) override;
  void report(const service_call& call) override;
  void report(const file_event& event) override;
  void report(const stop_record& stop) override;
  void report(const cpu::stop_record& stop) override;
  void report(const device_stop& stop) override;
  void report(const seam_event& event) override;

 private:
  /// Render `record` with `format_diagnostic` and keep the line. The one
  /// place every `report()` above goes through, so the whole-or-absent
  /// rule and the drop count are written once.
  template <typename T>
  void keep(const T& record) noexcept;

  /// Hand `record` to `relay_`, if there is one. Every `report()` above
  /// goes through it, so "the second sink sees everything the first
  /// does" is written once.
  template <typename T>
  void relay_to(const T& record) noexcept;

  std::array<char, capacity> chars_{};
  std::size_t first_{};
  std::size_t count_{};
  std::uint64_t dropped_{};
  bool tracing_{false};
  diagnostics* relay_{nullptr};
};

}  // namespace amberfolio::machine
