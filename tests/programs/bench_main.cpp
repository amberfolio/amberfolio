// SPDX-License-Identifier: AGPL-3.0-only
//
// Run every self-written program, check its answer, and say how long it
// took.
//
// Two jobs in one binary on purpose. The timing is what it is for, and
// timing is not something CI can assert — a shared runner's numbers vary
// by more than any regression worth catching. So what this *fails* on is
// the same thing the unit tests assert: the answers. That makes it a
// check CTest can run and a measurement a human can read, and it means
// the numbers printed always came from a run that was correct.
//
// Two lists since M2-T1 (#56). The first is M1's: the interpreter alone,
// against a flat megabyte with no devices, asserting a register and an
// exact step count. The second is M2's: whole programs through the whole
// machine — loader, devices, services and all — asserting everything the
// machine produced. Printing them one after the other is what puts the
// machine path's cost beside the CPU path's, which is the number this
// milestone did not have before.
//
// On wasm it is the only reader of either list that exists, because the
// GoogleTest rig does not build under Emscripten. `ctest --preset wasm`
// runs this under node, which is how both the interpreter and the machine
// get exercised on the target a browser gets rather than merely compiled
// for it.

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include "amberfolio/cpu/registers.h"
#include "programs/harness.h"
#include "programs/machine_harness.h"
#include "programs/machine_programs.h"
#include "programs/programs.h"

namespace {

/// The register names, indexed the way the encoding numbers them
/// (registers.h). Only used to label a line of output.
constexpr const char* register_name(amberfolio::cpu::reg16 r) {
  switch (r) {
    case amberfolio::cpu::reg16::ax:
      return "AX";
    case amberfolio::cpu::reg16::cx:
      return "CX";
    case amberfolio::cpu::reg16::dx:
      return "DX";
    case amberfolio::cpu::reg16::bx:
      return "BX";
    case amberfolio::cpu::reg16::sp:
      return "SP";
    case amberfolio::cpu::reg16::bp:
      return "BP";
    case amberfolio::cpu::reg16::si:
      return "SI";
    case amberfolio::cpu::reg16::di:
      return "DI";
  }
  return "??";
}

/// Run one program and report on it. True if it was correct.
bool check(const amberfolio::programs::program& p) {
  const amberfolio::programs::outcome r =
      amberfolio::programs::run(p.code, p.step_cap);

  const double rate =
      r.seconds > 0.0 ? static_cast<double>(r.steps) / r.seconds / 1e6 : 0.0;
  // %.*s rather than %s: a string_view is not null-terminated, and making
  // one so would mean a std::string per line to hand printf a pointer into.
  std::printf("%-12.*s %10llu steps  %8.3f s  %8.2f M steps/s   %.*s\n",
              static_cast<int>(p.name.size()), p.name.data(),
              static_cast<unsigned long long>(r.steps), r.seconds, rate,
              static_cast<int>(p.about.size()), p.about.data());

  bool ok = true;
  if (!r.clean()) {
    std::printf(
        "  FAIL  did not run to a halt: halted=%d capped=%d "
        "stop-reason=%u opcode=%02X at %04X:%04X\n",
        static_cast<int>(r.halted), static_cast<int>(r.capped),
        static_cast<unsigned>(r.stop.reason),
        static_cast<unsigned>(r.stop.opcode), r.stop.cs, r.stop.ip);
    ok = false;
  }
  const std::uint16_t got = r.regs[p.answer];
  if (got != p.expected) {
    std::printf("  FAIL  %s = %u (%04X), expected %u (%04X)\n",
                register_name(p.answer), got, got, p.expected, p.expected);
    ok = false;
  }
  if (r.steps != p.steps) {
    std::printf("  FAIL  took %llu steps, expected %llu\n",
                static_cast<unsigned long long>(r.steps),
                static_cast<unsigned long long>(p.steps));
    ok = false;
  }
  return ok;
}

/// Run one machine program and report on it. True if it was correct.
bool check_machine(const amberfolio::programs::machine_program& p) {
  const amberfolio::programs::machine_outcome r =
      amberfolio::programs::run_machine_setup(p.setup);

  const double rate =
      r.seconds > 0.0 ? static_cast<double>(r.steps) / r.seconds / 1e6 : 0.0;
  std::printf("%-12.*s %10llu steps  %8.3f s  %8.2f M steps/s   %.*s\n",
              static_cast<int>(p.name.size()), p.name.data(),
              static_cast<unsigned long long>(r.steps), r.seconds, rate,
              static_cast<int>(p.about.size()), p.about.data());

  const std::vector<std::string> wrong =
      amberfolio::programs::check_machine_program(p, r);
  for (const std::string& line : wrong) {
    std::printf("  FAIL  %s\n", line.c_str());
  }
  return wrong.empty();
}

int run_all() {
  std::printf("amberfolio: the interpreter over whole programs\n\n");
  std::printf("%-12s %16s %10s %17s   %s\n", "program", "steps", "time", "rate",
              "what it does");

  int failures = 0;
  for (const amberfolio::programs::program& p :
       amberfolio::programs::all_programs()) {
    if (!check(p)) {
      ++failures;
    }
  }

  std::printf("\namberfolio: the whole machine over whole programs\n\n");
  std::printf("%-12s %16s %10s %17s   %s\n", "program", "steps", "time", "rate",
              "what it does");

  for (const amberfolio::programs::machine_program& p :
       amberfolio::programs::all_machine_programs()) {
    if (!check_machine(p)) {
      ++failures;
    }
  }

  std::printf("\n");
  if (failures == 0) {
    std::printf("all correct.\n");
    return 0;
  }
  std::printf("%d program(s) wrong.\n", failures);
  return 1;
}

}  // namespace

int main() {
  // The assembler throws on a program that does not assemble, which is a
  // mistake in this repository rather than a run-time condition — but a
  // binary that terminated on it would say nothing useful about which one.
  try {
    return run_all();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "cannot run the programs: %s\n", error.what());
    return 1;
  }
}
