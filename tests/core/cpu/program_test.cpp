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
// are in tests/programs; this file only runs them.

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

INSTANTIATE_TEST_SUITE_P(programs, self_written_program,
                         ::testing::ValuesIn(all_programs()),
                         [](const ::testing::TestParamInfo<program>& info) {
                           return std::string(info.param.name);
                         });

}  // namespace
}  // namespace amberfolio::programs
