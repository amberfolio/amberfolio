// SPDX-License-Identifier: AGPL-3.0-only
//
// Running one vector against the interpreter, and saying what went wrong
// (issue #14).
//
// This is the wide phase's primary debugging surface: sixteen instruction
// families are going to be implemented against it, and the difference
// between "test 4211 of 80.0 failed" and a register-by-register diff with
// the flags spelled out is most of a working day per family. So the
// comparison is exact — flags bit for bit, undefined behaviour included,
// no masks anywhere (the M1 decision in issue #35) — and the report is
// written for someone reading it at the point of failure.
//
// Memory is modelled the way the vectors describe it, which is sparsely.
// The suite lists every byte the real part *read*, plus every byte that
// changed; a byte that was only written appears in the final state alone.
// Three facts per address follow from that, and this tracks all three:
// whether the vector mapped it, whether the CPU wrote it, and what it
// should hold at the end. Anything the CPU touches outside that set —
// a read of an unmapped address, a write nothing accounts for — is a
// failure rather than a shrug, because the whole point of an oracle is
// that it knows and we do not (PLAN.md §3: log, don't fake).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"
#include "vectors.h"

namespace amberfolio::conformance {

/// The bus one vector runs against: a megabyte of sparsely-known memory
/// and a scripted port channel.
class vector_bus final : public cpu::bus {
 public:
  vector_bus();

  /// Load a test's initial and expected state. Undoes the previous test
  /// in time proportional to what that test touched, not to a megabyte —
  /// this runs ten thousand times per file.
  void begin(const vector_test& test);

  [[nodiscard]] std::uint8_t read_memory(std::uint32_t address) override;
  void write_memory(std::uint32_t address, std::uint8_t value) override;
  [[nodiscard]] std::uint8_t read_port8(std::uint16_t port) override;
  void write_port8(std::uint16_t port, std::uint8_t value) override;

  /// Append what memory and the ports have to say about this test to
  /// `report`. Empty additions mean everything matched.
  void check(std::string& report) const;

 private:
  static constexpr std::uint8_t mapped = 1U;    ///< the vector gave a value
  static constexpr std::uint8_t written = 2U;   ///< the CPU wrote here
  static constexpr std::uint8_t expected = 4U;  ///< a final value is known

  void mark(std::uint32_t address, std::uint8_t flag);

  std::vector<std::uint8_t> value_;
  std::vector<std::uint8_t> expected_;
  std::vector<std::uint8_t> state_;
  std::vector<std::uint32_t> touched_;

  const vector_test* test_{nullptr};
  std::size_t port_index_{0};
  std::vector<std::string> faults_;
};

/// A processor wired to a vector_bus, reused across every test in a file.
class vector_machine {
 public:
  /// `table` is the instruction set the machine runs. machine_test.cpp
  /// passes one of its own: an oracle that can only ever say "opcode not
  /// implemented" cannot show that the comparison it wraps works, and
  /// during M1's wide phase this harness is trusted long before the
  /// interpreter is.
  explicit vector_machine(
      const cpu::dispatch_table& table = cpu::instruction_set());

  /// Run one vector. The empty string means it passed; anything else is
  /// the failure, formatted to be read.
  [[nodiscard]] std::string run(const vector_test& test);

 private:
  vector_bus bus_;
  cpu::processor cpu_;
};

/// FLAGS as the nine letters that mean something on an 8086, high bit
/// first: ODITSZAPC, with a dot for each one that is clear. Exposed
/// because a family's own unit tests want the same rendering.
[[nodiscard]] std::string flag_letters(std::uint16_t flags);

}  // namespace amberfolio::conformance
