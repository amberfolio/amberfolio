// SPDX-License-Identifier: AGPL-3.0-only
//
// Tests for the oracle itself (issue #14).
//
// Sixteen instruction families are about to be written against this
// harness, and every one of them will believe what it says. A comparison
// that quietly passes a wrong answer would not fail anything — it would
// certify sixteen wrong implementations, and there is nothing downstream
// to catch that. So each thing the harness claims to detect is asserted
// here against a synthetic vector and a hand-written "instruction":
// a wrong register, a wrong flag, a write nobody accounts for, a write
// that never happened, a read outside the mapped bytes, a port
// transaction that does not match the script, a repeat that never
// retires, and an opcode that is not implemented at all.
//
// These need no vector files and are the same in every build, so they run
// wherever the conformance binary runs — including the CI jobs whose
// vector subset is capped to a few hundred tests.

#include "machine.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "vectors.h"

namespace amberfolio::conformance {
namespace {

using ::testing::HasSubstr;

/// The one opcode these tests use. 0x90 on real silicon is NOP; here it
/// is whatever the case under test made it, which is the point of
/// handing the machine a table of our own.
constexpr std::uint8_t opcode = 0x90;

constexpr std::uint16_t test_cs = 0x1000;
constexpr std::uint16_t test_ip = 0x0100;
constexpr std::uint32_t opcode_address = 0x10100;
constexpr std::uint32_t scratch_address = 0x20000;

/// A vector that says "this one-byte instruction changes nothing but IP".
/// Each case then edits the `after` state into whatever it is asserting.
vector_test one_byte_vector() {
  vector_test test;
  test.name = "the instruction under test";
  test.idx = 7;
  test.bytes = {opcode};

  test.before.load_flags(0);
  test.before[cpu::sreg::cs] = test_cs;
  test.before[cpu::sreg::ds] = 0x2000;
  test.before.ip = test_ip;
  test.ram_before.push_back({.address = opcode_address, .value = opcode});

  test.after = test.before;
  test.after.ip = test_ip + 1;
  return test;
}

cpu::dispatch_table table_of(cpu::handler run) {
  cpu::dispatch_table table{};
  table.primary[opcode] = run;
  return table;
}

// --- The hand-written "instructions" ----------------------------------

void do_nothing(cpu::processor&) {}

void set_ax(cpu::processor& cpu) { cpu.regs()[cpu::reg16::ax] = 0xBEEF; }

void set_carry(cpu::processor& cpu) {
  cpu.regs().set_flag(cpu::flag::cf, true);
}

void write_scratch(cpu::processor& cpu) {
  cpu.write_byte(0x2000, 0x0000, 0x5A);
}

void read_scratch(cpu::processor& cpu) {
  const std::uint8_t value = cpu.read_byte(0x2000, 0x0000);
  cpu.regs()[cpu::reg16::ax] = value;
}

void read_a_port(cpu::processor& cpu) {
  const std::uint8_t value = cpu.machine_bus().read_port8(0x00C0);
  cpu.regs()[cpu::reg16::ax] = value;
}

int iterations_left = 0;

void repeat_twice(cpu::processor& cpu) {
  if (iterations_left > 0) {
    --iterations_left;
    cpu.regs()[cpu::reg16::cx] = static_cast<std::uint16_t>(iterations_left);
    cpu.regs().ip = cpu.current().start_ip;
    cpu.keep_repeating();
  }
}

void repeat_forever(cpu::processor& cpu) {
  cpu.regs().ip = cpu.current().start_ip;
  cpu.keep_repeating();
}

// --- The cases --------------------------------------------------------

TEST(ConformanceHarness, AnInstructionThatMatchesReportsNothing) {
  const cpu::dispatch_table table = table_of(&do_nothing);
  vector_machine machine(table);
  EXPECT_EQ(machine.run(one_byte_vector()), "");
}

TEST(ConformanceHarness, AWrongRegisterIsNamedWithBothValues) {
  const cpu::dispatch_table table = table_of(&set_ax);
  vector_machine machine(table);

  const std::string report = machine.run(one_byte_vector());
  EXPECT_THAT(report, HasSubstr("AX"));
  EXPECT_THAT(report, HasSubstr("expected 0000"));
  EXPECT_THAT(report, HasSubstr("got BEEF"));
  // The header identifies which vector, by the index the suite calls it.
  EXPECT_THAT(report, HasSubstr("test 7"));
  EXPECT_THAT(report, HasSubstr("bytes 90"));
}

TEST(ConformanceHarness, AWrongFlagIsNamedByItsLetters) {
  const cpu::dispatch_table table = table_of(&set_carry);
  vector_machine machine(table);

  const std::string report = machine.run(one_byte_vector());
  EXPECT_THAT(report, HasSubstr("FLAGS"));
  EXPECT_THAT(report, HasSubstr("differ: CF"));
}

// The flags are the whole reason this suite exists (issue #35: bit for
// bit, undefined behaviour included, no masks), so an instruction that
// gets every register right and one flag wrong must still fail.
TEST(ConformanceHarness, AFlagSetTheVectorExpectsIsNotAFailure) {
  const cpu::dispatch_table table = table_of(&set_carry);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.after.load_flags(cpu::flag::cf);
  EXPECT_EQ(machine.run(test), "");
}

TEST(ConformanceHarness, AWriteNothingAccountsForIsAFailure) {
  const cpu::dispatch_table table = table_of(&write_scratch);
  vector_machine machine(table);

  const std::string report = machine.run(one_byte_vector());
  EXPECT_THAT(report, HasSubstr("20000"));
  EXPECT_THAT(report, HasSubstr("does not account for it"));
}

TEST(ConformanceHarness, AWriteTheVectorRecordsIsAccepted) {
  const cpu::dispatch_table table = table_of(&write_scratch);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ram_after.push_back({.address = scratch_address, .value = 0x5A});
  EXPECT_EQ(machine.run(test), "");
}

TEST(ConformanceHarness, AWriteOfTheWrongValueIsAFailure) {
  const cpu::dispatch_table table = table_of(&write_scratch);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ram_after.push_back({.address = scratch_address, .value = 0x5B});
  EXPECT_THAT(machine.run(test), HasSubstr("expected 5B  got 5A"));
}

TEST(ConformanceHarness, AWriteThatNeverHappenedIsAFailure) {
  const cpu::dispatch_table table = table_of(&do_nothing);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ram_after.push_back({.address = scratch_address, .value = 0x5A});
  EXPECT_THAT(machine.run(test), HasSubstr("nothing was written there"));
}

// The vectors list every byte the real part read. Reading one they do not
// list means the address was computed wrongly, and answering it with an
// invented byte is exactly the failure mode PLAN.md §3 forbids.
TEST(ConformanceHarness, AReadOutsideTheMappedBytesIsAFailure) {
  const cpu::dispatch_table table = table_of(&read_scratch);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.after[cpu::reg16::ax] = 0xFF;  // what the bus answers, so only the
                                      // unmapped read itself can fail
  EXPECT_THAT(machine.run(test), HasSubstr("does not map"));
}

TEST(ConformanceHarness, AReadOfAMappedByteIsFedFromTheVector) {
  const cpu::dispatch_table table = table_of(&read_scratch);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ram_before.push_back({.address = scratch_address, .value = 0x42});
  test.after[cpu::reg16::ax] = 0x42;
  EXPECT_EQ(machine.run(test), "");
}

// Memory has to be forgotten between vectors, or a write in test 4211
// answers a read in test 4212 and the suite quietly stops being a suite.
TEST(ConformanceHarness, OneVectorDoesNotLeakMemoryIntoTheNext) {
  const cpu::dispatch_table table = table_of(&read_scratch);
  vector_machine machine(table);

  vector_test mapped = one_byte_vector();
  mapped.ram_before.push_back({.address = scratch_address, .value = 0x42});
  mapped.after[cpu::reg16::ax] = 0x42;
  ASSERT_EQ(machine.run(mapped), "");

  // Same machine, same address, a vector that does not map it: the byte
  // the previous test put there must be gone rather than answered.
  vector_test unmapped = one_byte_vector();
  unmapped.after[cpu::reg16::ax] = 0xFF;
  EXPECT_THAT(machine.run(unmapped), HasSubstr("does not map"));
}

TEST(ConformanceHarness, APortReadIsAnsweredFromTheScript) {
  const cpu::dispatch_table table = table_of(&read_a_port);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ports.push_back(
      {.what = port_op::kind::read, .port = 0x00C0, .value = 0x77});
  test.after[cpu::reg16::ax] = 0x77;
  EXPECT_EQ(machine.run(test), "");
}

TEST(ConformanceHarness, APortReadOfTheWrongPortIsAFailure) {
  const cpu::dispatch_table table = table_of(&read_a_port);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ports.push_back(
      {.what = port_op::kind::read, .port = 0x00C1, .value = 0x77});
  test.after[cpu::reg16::ax] = 0x77;
  EXPECT_THAT(machine.run(test), HasSubstr("expected a read of port 00C1"));
}

TEST(ConformanceHarness, APortTransactionThatNeverHappensIsAFailure) {
  const cpu::dispatch_table table = table_of(&do_nothing);
  vector_machine machine(table);

  vector_test test = one_byte_vector();
  test.ports.push_back(
      {.what = port_op::kind::read, .port = 0x00C0, .value = 0x77});
  EXPECT_THAT(machine.run(test), HasSubstr("never made"));
}

// One step is one instruction *or one iteration* (PLAN.md §3), so the
// harness has to keep stepping until the instruction retires — and has to
// know when that is, which is what step_status::repeating is for.
TEST(ConformanceHarness, ARepeatedInstructionIsSteppedUntilItRetires) {
  const cpu::dispatch_table table = table_of(&repeat_twice);
  vector_machine machine(table);

  iterations_left = 2;
  vector_test test = one_byte_vector();
  test.before[cpu::reg16::cx] = 2;
  test.after = test.before;
  test.after.ip = test_ip + 1;
  test.after[cpu::reg16::cx] = 0;
  EXPECT_EQ(machine.run(test), "");
}

TEST(ConformanceHarness, ARepeatThatNeverRetiresIsReportedRatherThanHung) {
  const cpu::dispatch_table table = table_of(&repeat_forever);
  vector_machine machine(table);

  EXPECT_THAT(machine.run(one_byte_vector()), HasSubstr("had not retired"));
}

// The state of every vector file until its family lands. It must read as
// "not implemented", not as a wall of register differences against a
// machine that never ran the instruction.
TEST(ConformanceHarness, AnUnimplementedOpcodeIsReportedOnItsOwn) {
  const cpu::dispatch_table nothing{};
  vector_machine machine(nothing);

  const std::string report = machine.run(one_byte_vector());
  EXPECT_THAT(report, HasSubstr("stopped at 1000:0100"));
  EXPECT_THAT(report, HasSubstr("opcode 90"));
  EXPECT_THAT(report, HasSubstr("no handler"));
  EXPECT_THAT(report, ::testing::Not(HasSubstr("IP    expected")));
}

TEST(ConformanceHarness, FlagsRenderHighBitFirstWithADotForClear) {
  EXPECT_EQ(flag_letters(cpu::flag::normalize(0)), ".........");
  EXPECT_EQ(flag_letters(cpu::flag::normalize(cpu::flag::cf)), "........C");
  EXPECT_EQ(flag_letters(cpu::flag::normalize(cpu::flag::of)), "O........");
  EXPECT_EQ(flag_letters(cpu::flag::normalize(cpu::flag::defined)),
            "ODITSZAPC");
}

}  // namespace
}  // namespace amberfolio::conformance
