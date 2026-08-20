// SPDX-License-Identifier: AGPL-3.0-only
//
// M2's exit criterion, one CTest case per program.
//
// PLAN.md §7: "self-written real-mode test programs run correctly on all
// targets." Every other test in this directory asks about one device or
// one service in isolation, driven by a rig built for it. These ask the
// only question the milestone is actually about — does the whole machine
// compose — by running whole programs through it: the loader places one,
// the CPU executes it, the PIT and the PIC deliver its interrupts, the
// EGA takes its writes, the renderer composes them, the speaker plays its
// tones, the BIOS and DOS answer its calls, and the filesystem keeps what
// it wrote.
//
// The programs, the machine they run on and every expectation are in
// tests/programs, which builds with no GoogleTest at all — that is what
// lets `ctest --preset wasm` run this same list under node, which is the
// wasm quarter of the same criterion (tests/programs/CMakeLists.txt).
// This file only runs them and reports.
//
// So there is deliberately almost nothing here: a check that returns the
// list of everything wrong, and a case that fails if the list is not
// empty. A test that re-stated an expectation would be a second copy of
// it, and the two would eventually disagree.

#include <ostream>
#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "programs/machine_harness.h"
#include "programs/machine_programs.h"

namespace amberfolio::programs {
namespace {

class machine_program_case : public ::testing::TestWithParam<machine_program> {
};

TEST_P(machine_program_case, runs_correctly_on_the_whole_machine) {
  const machine_program& p = GetParam();
  const machine_outcome got = run_machine_setup(p.setup);

  const std::vector<std::string> wrong = check_machine_program(p, got);
  for (const std::string& line : wrong) {
    ADD_FAILURE() << line;
  }

  // Not an expectation of the program's, and not asserted anywhere in
  // tests/programs: a program that reached its exit through DOS called
  // the service floor at least once by definition, and a run that
  // reported none of those calls would mean the diagnostics channel had
  // gone quiet without anything else noticing.
  EXPECT_GT(got.service_calls, 0u);
}

// `entry`, not `info` — INSTANTIATE_TEST_SUITE_P expands to a function
// with a parameter of that name and GCC's -Wshadow objects, exactly as
// tests/core/cpu/program_test.cpp explains for its own instantiation.
INSTANTIATE_TEST_SUITE_P(
    machine, machine_program_case, ::testing::ValuesIn(all_machine_programs()),
    [](const ::testing::TestParamInfo<machine_program>& entry) {
      return std::string(entry.param.name);
    });

// The composite is what M2-H2's dev page (#55) embeds, so it has to be
// reachable by name and not only by walking the list. One case, because
// the lookup is the interface and a broken one would strand the dev page
// with no test having noticed.
TEST(machine_program_lookup, the_composite_is_findable_by_name) {
  const machine_program* composite = find_machine_program("composite");
  ASSERT_NE(composite, nullptr);
  EXPECT_EQ(composite->name, "composite");
  EXPECT_FALSE(composite->setup.exe.empty());

  EXPECT_EQ(find_machine_program("no such program"), nullptr);
}

}  // namespace
}  // namespace amberfolio::programs
