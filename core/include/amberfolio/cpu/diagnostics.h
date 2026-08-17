// SPDX-License-Identifier: AGPL-3.0-only
//
// "Log, don't fake", made into a mechanism.
//
// PLAN.md §3 states the rule: an unimplemented instruction, register or
// port is a loud log line and a clean stop, never a silently guessed
// answer. Guessing is how emulators end up subtly wrong for decades, and
// the difference between a project that holds that line and one that says
// it does is whether there is somewhere for the report to go.
//
// So there are two halves, and both are mandatory:
//
//   * The processor records *what* it would not fake in a `stop_record`
//     and returns `step_status::stopped`. This half cannot be switched
//     off or ignored — the caller gets a status it has to look at.
//   * It hands the same record to a `diagnostics` sink, if it has one, so
//     a host can put a line in front of a human. This half is optional
//     only in the sense that a sink is optional; nothing is lost when
//     there is none, because the record is still there to be read.
//
// The core stays free of host dependencies (PLAN.md §4), so the record is
// structured data and the sink renders it. There is no printf here and
// there is not going to be one.

#pragma once

#include <cstdint>

namespace amberfolio::cpu {

/// Why the processor stopped.
///
/// One entry per *kind* of thing we decline to invent. M1-F3 adds the
/// group-opcode case; the device and DOS-service layers will grow their
/// own reasons, or their own record type, when they exist.
enum class stop_reason : std::uint8_t {
  /// Nothing has gone wrong; the processor is running.
  none,
  /// An opcode with no handler in the dispatch table — or, for a group
  /// opcode, no handler for that ModRM `reg` field.
  unimplemented_opcode,
  /// An instruction made of more prefix bytes than any real one has.
  ///
  /// The one place this machine knowingly declines to do what the part
  /// does. A real 8086 fed an unbroken run of prefix bytes fetches them
  /// forever and never executes anything — it is a hang, and reproducing
  /// a hang faithfully means hanging. A bounded refusal is the honest
  /// alternative: nothing is invented, and the caller is told exactly
  /// where the run started. No real instruction, and no conformance
  /// vector, comes anywhere near the limit.
  prefix_chain_too_long,
};

/// `stop_record::extension` when the opcode is not a group opcode, and so
/// has no ModRM `reg` field selecting which instruction it is.
inline constexpr std::uint8_t no_extension = 0xFF;

/// Everything a human needs to identify the instruction we refused.
///
/// `cs`/`ip` point at the *first byte of the instruction* — prefixes
/// included — not at wherever the fetch had got to, and the processor
/// rewinds IP to match before it stops. A stop therefore leaves the
/// machine in a state you can inspect and, having implemented the missing
/// handler, re-enter.
struct stop_record {
  stop_reason reason{stop_reason::none};
  std::uint8_t opcode{};
  /// For a group opcode, the ModRM `reg` field that picked the entry
  /// there was no handler for. `no_extension` otherwise — without it,
  /// "opcode FF is unimplemented" would not say which of five
  /// instructions was actually wanted.
  std::uint8_t extension{no_extension};
  std::uint16_t cs{};
  std::uint16_t ip{};

  friend constexpr bool operator==(const stop_record&,
                                   const stop_record&) = default;
};

/// Where a host or a test receives what the core would not fake.
///
/// Implementations must not throw and must not touch machine state: this
/// is a report, not a hook. (The one sanctioned way to alter the machine
/// is a seam — PLAN.md §5.)
class diagnostics {
 public:
  diagnostics() = default;
  diagnostics(const diagnostics&) = delete;
  diagnostics(diagnostics&&) = delete;
  diagnostics& operator=(const diagnostics&) = delete;
  diagnostics& operator=(diagnostics&&) = delete;

  /// Called once, at the moment the processor gives up on an instruction.
  /// Not called again while it stays stopped — a stop is an event, not a
  /// state to be re-reported on every step. One method rather than one
  /// per reason: the record says which, and the list of reasons is going
  /// to grow.
  virtual void report(const stop_record& stop) = 0;

 protected:
  // See bus.h: held by reference, never deleted through this type.
  ~diagnostics() = default;
};

}  // namespace amberfolio::cpu
