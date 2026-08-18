// SPDX-License-Identifier: AGPL-3.0-only
//
// The self-written 8086 programs, and what each of them is supposed to
// produce (PLAN.md §6 — every fixture in this tree is written here or
// public-licensed; no byte of any of this comes from anywhere else).
//
// One list, two readers, the same rule the conformance manifest follows:
// the unit tests assert what these programs answer, and the benchmark
// times them. Neither has its own copy of what "correct" means.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/registers.h"

namespace amberfolio::programs {

/// One program, its answer, and how long it takes to arrive at it.
struct program {
  /// Identifies the CTest case, so: letters, digits and underscores.
  std::string_view name;

  /// One line, for the benchmark's table.
  std::string_view about;

  std::vector<std::uint8_t> code;

  /// The register the answer is in when the program halts, and the answer.
  cpu::reg16 answer{};
  std::uint16_t expected{};

  /// Exactly how many steps that takes.
  ///
  /// Asserted, not merely reported. The interpreter is deterministic, so
  /// this is a fact about it, and it is the one fact that catches a change
  /// to the *step model* — the thing PLAN.md §3 pins down and the M2
  /// scheduler is going to charge virtual time against. An instruction
  /// that quietly stopped retiring per iteration, or a REP that started
  /// running to completion inside one step, would still produce the right
  /// answer here and would be caught by nothing else in the suite.
  std::uint64_t steps{};

  /// Give up after this many. Comfortably above `steps`, low enough that a
  /// program that has gone into an endless loop says so in a second.
  std::uint64_t step_cap{};
};

/// Every program, in the order the benchmark should run them.
[[nodiscard]] std::vector<program> all_programs();

/// Just the name, and that is load-bearing twice over: GoogleTest would
/// otherwise print a parameter by its bytes, so a failing case would open
/// with a hex dump of the program — and gtest_discover_tests builds the
/// CTest case name out of this, not out of the name generator beside it
/// (see GoogleTestAddTests.cmake, which replaces the generated suffix with
/// the printed value). Anything more here ends up in `ctest -R`.
void PrintTo(const program& p, std::ostream* os);

}  // namespace amberfolio::programs
