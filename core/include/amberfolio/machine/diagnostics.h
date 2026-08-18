// SPDX-License-Identifier: AGPL-3.0-only
//
// "Log, don't fake" at the machine layer.
//
// cpu/diagnostics.h states the rule and its two halves — a record the
// caller cannot ignore, and a sink a host can render — and this is the
// same shape one level up. What is different is that the machine has two
// kinds of thing to say, not one:
//
//   * A **stop**. The machine gave up, the way the processor does when it
//     will not invent an instruction. Sticky, inspectable, cleared by
//     reset().
//   * A **notice**. Something was asked of an address or a port that
//     nothing answers for. This is *not* a stop, because there is nothing
//     to invent: an unterminated bus reads FF and swallows writes, and
//     that is the true answer rather than a guess (device.h). PLAN.md §3
//     asks for the log line here — "ignored (logged, not faked)" — not
//     for the machine to halt every time a program probes for a card that
//     is not fitted.
//
// One sink takes all of it, including the processor's own stops, so a
// host wires up one object rather than three. The core stays free of host
// dependencies: these are structured records, and the sink is what turns
// them into something a human reads.

#pragma once

#include <cstdint>

#include "amberfolio/cpu/diagnostics.h"

namespace amberfolio::machine {

/// Why the machine stopped — the machine layer's own reasons. The
/// processor's are in cpu::stop_reason, and `processor` below is how the
/// two meet.
enum class stop_reason : std::uint8_t {
  /// Nothing has gone wrong; the machine is running.
  none,
  /// The processor stopped, so the machine has. What it refused to
  /// invent is in `machine::processor().stop()` — a machine-level record
  /// that restated it would only be able to get it wrong.
  processor,
  /// Two devices claimed the same ports or overlapping memory windows, or
  /// more claims arrived than the machine has room for.
  ///
  /// The one reason here the emulated program cannot cause: it is a
  /// mistake in how the machine was put together, caught at the moment it
  /// is made rather than surfacing later as a device that mysteriously
  /// never answers.
  conflicting_claim,
};

struct stop_record {
  stop_reason reason{stop_reason::none};

  /// The physical address or the port the reason is about; zero when it
  /// is about neither.
  std::uint32_t at{};

  friend constexpr bool operator==(const stop_record&,
                                   const stop_record&) = default;
};

/// What was asked for that nothing answers for.
enum class notice_kind : std::uint8_t {
  /// An address no region and no device claims.
  unmapped_memory_read,
  unmapped_memory_write,
  /// A write to the BIOS region, which is ROM (memory_map.h).
  rom_write,
  /// A port no device claims.
  unclaimed_port_read,
  unclaimed_port_write,
};

struct notice {
  notice_kind what{};

  /// The physical address, or the port number.
  std::uint32_t at{};

  /// The byte a dropped write was carrying. Zero for a read.
  std::uint8_t value{};

  /// Where the program was when it did this: the instruction being
  /// executed, at its first byte, prefixes included. The whole value of a
  /// line about an address nothing answers for is what asked.
  std::uint16_t cs{};
  std::uint16_t ip{};

  friend constexpr bool operator==(const notice&, const notice&) = default;
};

class diagnostics {
 public:
  diagnostics() = default;
  diagnostics(const diagnostics&) = delete;
  diagnostics(diagnostics&&) = delete;
  diagnostics& operator=(const diagnostics&) = delete;
  diagnostics& operator=(diagnostics&&) = delete;

  /// Something was asked of nothing.
  ///
  /// Reported once per 4 KiB page of memory and once per port, until the
  /// next reset. A program that polls an absent card in a loop would
  /// otherwise produce a line per iteration and bury the one line that
  /// told you something — and the first touch is the one that says where
  /// the program was when it started.
  virtual void report(const notice& what) = 0;

  /// The machine layer gave up.
  virtual void report(const stop_record& stop) = 0;

  /// The processor gave up, and so the machine has: `machine::stop()`
  /// reads `stop_reason::processor` from the same moment. Passed through
  /// rather than translated, because this record names the opcode.
  ///
  /// One report per stop, not two: this is the line that says what
  /// happened, and the machine's own record is there to be inspected.
  virtual void report(const cpu::stop_record& stop) = 0;

 protected:
  // See cpu/bus.h: held by reference, never deleted through this type.
  ~diagnostics() = default;
};

}  // namespace amberfolio::machine
