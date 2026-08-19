// SPDX-License-Identifier: AGPL-3.0-only
//
// The interpreter over whole programs.
//
// Every other CPU test in this directory, and every one of the 323
// conformance files beside them, asks the same shape of question: given
// this state, does this one instruction leave that state? None of them
// asks whether a thousand instructions in a row still add up — whether IP
// threads through a backward branch a million times, whether a flag set by
// one instruction is the flag the next one reads, whether a byte written
// early is the byte found late. A program is the only thing that asks
// that, and the answer to each of these is a fact about arithmetic rather
// than about this emulator, so there is nothing to get wrong in the
// expectation.
//
// One case per program, named after it. The programs and their answers
// are in tests/programs; this file only runs them — twice since M2-F1,
// once against the bare bus and once against the machine, because
// "the machine's bus is transparent to the CPU" is a claim only the
// comparison can make.

#include <cstdint>
#include <ostream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "programs/harness.h"
#include "programs/programs.h"

namespace amberfolio::programs {
namespace {

class self_written_program : public ::testing::TestWithParam<program> {};

TEST_P(self_written_program, runs_to_a_halt_with_the_right_answer) {
  const program& p = GetParam();
  const outcome r = run(p.code, p.step_cap);

  // Reported before the answer is: a stop tells you which opcode the
  // interpreter refused, and that is a better first line of a failure than
  // "expected 9592, got 0".
  EXPECT_EQ(r.stop.reason, cpu::stop_reason::none)
      << "stopped on opcode " << std::hex << unsigned{r.stop.opcode} << " at "
      << r.stop.cs << ":" << r.stop.ip;
  EXPECT_FALSE(r.capped) << "ran past its step cap of " << p.step_cap
                         << " without halting";
  EXPECT_TRUE(r.halted);

  EXPECT_EQ(r.regs[p.answer], p.expected);

  // The step model, not the answer — see program::steps.
  EXPECT_EQ(r.steps, p.steps);
}

// The same programs, on the machine (M2-F1, #42). Its exit criterion,
// stated as a test: a machine with a flat RAM map runs these to the same
// answers and the same step counts as the M1 harness. Everything is
// compared, not just the answer — the step count catches a machine that
// changed the step model, and the whole register file catches one that
// lost a byte somewhere the answer does not look.
TEST_P(self_written_program, runs_the_same_on_the_machine) {
  const program& p = GetParam();
  const outcome bare = run(p.code, p.step_cap);
  const outcome on_machine = run_on_machine(p.code, p.step_cap);

  EXPECT_EQ(on_machine.stop.reason, cpu::stop_reason::none)
      << "stopped on opcode " << std::hex << unsigned{on_machine.stop.opcode}
      << " at " << on_machine.stop.cs << ":" << on_machine.stop.ip;
  EXPECT_FALSE(on_machine.capped);
  EXPECT_TRUE(on_machine.halted);

  EXPECT_EQ(on_machine.regs[p.answer], p.expected);
  EXPECT_EQ(on_machine.steps, p.steps);

  // Against the other harness rather than only against the expectations:
  // the expectations are three numbers, and this is every register and
  // every flag.
  EXPECT_EQ(on_machine.regs, bare.regs);
  EXPECT_EQ(on_machine.steps, bare.steps);
  EXPECT_EQ(on_machine.halted, bare.halted);

  // A flat map has no unmapped memory at all, so this is a statement
  // about ports: these programs touch none, and the machine would have
  // said so if they had.
  EXPECT_EQ(on_machine.notices, 0u);
}

// `entry`, not the `info` this parameter is conventionally called:
// INSTANTIATE_TEST_SUITE_P expands to a function with a parameter of that
// name, and the lambda would shadow it. GCC says so under -Wshadow, which
// is in the warning baseline; Clang and MSVC do not, so this reads as a
// pointless rename until you build it with GCC.
INSTANTIATE_TEST_SUITE_P(programs, self_written_program,
                         ::testing::ValuesIn(all_programs()),
                         [](const ::testing::TestParamInfo<program>& entry) {
                           return std::string(entry.param.name);
                         });

}  // namespace
}  // namespace amberfolio::programs
