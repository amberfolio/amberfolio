// SPDX-License-Identifier: AGPL-3.0-only
//
// A machine just big enough to run a program that needs no machine:
// a flat megabyte of RAM, no devices, and a loop that steps the
// interpreter until it halts.
//
// This is deliberately not the machine, and it is not the conformance
// harness either. The conformance suite proves each instruction from a
// fresh state, one at a time; nothing there proves that thousands of them
// compose — that IP threads through a backward branch a million times,
// that flags survive from one instruction to the next, that a write lands
// where the next read looks for it. That is what running a whole program
// proves, and the smallest thing that can run one is this.
//
// It is also the only piece of test apparatus that builds under
// Emscripten, which is why it lives outside the GoogleTest rig: the
// benchmark it feeds is what exercises the interpreter on the target a
// browser gets.
//
// Since M2-F1 there is a second runner beside it, `run_on_machine()`,
// which puts the same programs through the real machine with a flat RAM
// map. The bare bus below stays, and stays first, because the point of
// the machine runner is that the two agree.

#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::programs {

/// Where run() puts a program and what it points the segment registers at.
/// Nothing overlaps and nothing is near the interrupt vector table, so a
/// program that runs off its own rails writes somewhere harmless and the
/// failure stays readable.
struct layout {
  static constexpr std::uint16_t code_segment = 0x1000;
  static constexpr std::uint16_t data_segment = 0x2000;
  static constexpr std::uint16_t stack_segment = 0xE000;
  static constexpr std::uint16_t stack_pointer = 0xFFFE;
};

/// A megabyte of RAM and nothing else. Ports read as FF, which is what an
/// unclaimed ISA port floats to; none of these programs touch one, and a
/// program that started to would get the same answer a bare machine gives
/// rather than a crash that hides the change.
class flat_bus final : public cpu::bus {
 public:
  flat_bus();

  [[nodiscard]] std::uint8_t read_memory(std::uint32_t address) override;
  void write_memory(std::uint32_t address, std::uint8_t value) override;
  [[nodiscard]] std::uint8_t read_port8(std::uint16_t port) override;
  void write_port8(std::uint16_t port, std::uint8_t value) override;

 private:
  std::vector<std::uint8_t> memory_;
};

/// A diagnostics sink that keeps the stop rather than printing it: the
/// caller decides how a stop should read, and a benchmark and a test
/// disagree about that.
class recorded_stop final : public cpu::diagnostics {
 public:
  void report(const cpu::stop_record& stop) override { record = stop; }

  cpu::stop_record record{};
};

/// What running a program did.
struct outcome {
  /// Steps, in the sense processor::step() means: one instruction, or one
  /// iteration of a repeated string instruction. HLT is counted — it is an
  /// instruction that runs; the step after it is the one that reports the
  /// halt and is not.
  std::uint64_t steps{};

  /// Wall time for the stepping loop alone. Setting the machine up and
  /// tearing it down are outside it, so a program's number is about the
  /// interpreter rather than about allocating a megabyte.
  double seconds{};

  cpu::registers regs{};
  cpu::stop_record stop{};

  /// How many times something was asked of an address or a port that
  /// nothing answers for. Always zero from `run()`, whose bus has RAM
  /// everywhere and answers every port itself; from `run_on_machine()` it
  /// is a real assertion, because a flat machine has no unmapped memory
  /// but does have an empty port map.
  std::uint64_t notices{};

  /// The program reached HLT.
  bool halted{false};

  /// The step cap ran out first, which means it did not.
  bool capped{false};

  /// It ran to a halt, on its own, without the interpreter refusing an
  /// instruction along the way.
  [[nodiscard]] bool clean() const noexcept {
    return halted && !capped && stop.reason == cpu::stop_reason::none;
  }
};

/// Load `code` at the layout's code segment, reset, and step until HLT.
///
/// Gives up after `step_cap` steps rather than hanging: a program that
/// loops forever is a bug being reported, not a test suite that never
/// finishes.
[[nodiscard]] outcome run(std::span<const std::uint8_t> code,
                          std::uint64_t step_cap);

/// The same program, the same way, on the M2 machine
/// (`amberfolio::machine::machine`) with a flat RAM map.
///
/// This is M2-F1's exit criterion made runnable. The machine puts a
/// memory map, a port map and a device list between the interpreter and
/// its bytes; the claim is that none of that is visible from inside the
/// program. Two runners rather than one replacement, because the claim is
/// a *comparison* — the answers and the step counts have to match the
/// harness above, and a harness that had been replaced could not be
/// compared with.
[[nodiscard]] outcome run_on_machine(std::span<const std::uint8_t> code,
                                     std::uint64_t step_cap);

}  // namespace amberfolio::programs
