// SPDX-License-Identifier: AGPL-3.0-only
//
// The unvectored quarter of the flags/I/O/ESC/misc family (issue #33): no
// SingleStepTests file exists for 0F POP CS, 9B WAIT or F4 HLT, so what
// pins their behaviour is here instead. The other twenty-four opcodes of
// the family are conformance-tested and need nothing of their own.
//
// These run against the real dispatch table (instruction_set()) rather
// than a stand-in: unlike interrupts_test.cpp, which predates every
// instruction it exercises, 0F/9B/F4 are this family's own handlers and
// are already wired into it.

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>

#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "cpu/test_bus.h"

namespace amberfolio::cpu {
namespace {

using test::test_bus;

constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t code_offset = 0x0100;
constexpr std::uint16_t stack_segment = 0x3000;

class Misc : public ::testing::Test {
 protected:
  Misc() : cpu_(bus_, nullptr, instruction_set()) {
    cpu_.regs()[sreg::cs] = code_segment;
    cpu_.regs().ip = code_offset;
    cpu_.regs()[sreg::ss] = stack_segment;
  }

  /// The bytes the processor is about to execute, at CS:IP.
  void program(std::initializer_list<std::uint8_t> bytes) {
    bus_.poke(code_segment, code_offset, bytes);
  }

  test_bus bus_;
  processor cpu_;
};

// --- 0F POP CS ----------------------------------------------------------

TEST_F(Misc, PopCsPopsIntoCsMovesSpAndInhibitsInterrupts) {
  cpu_.regs()[reg16::sp] = 0x0004;
  // Low byte first: the stack, like every word in this machine, is
  // little-endian.
  bus_.poke(stack_segment, 0x0004, {0x34, 0x12});
  program({0x0F});

  ASSERT_FALSE(cpu_.interrupts_inhibited());
  ASSERT_EQ(cpu_.step(), step_status::ran);

  EXPECT_EQ(cpu_.regs()[sreg::cs], 0x1234);
  EXPECT_EQ(cpu_.regs()[reg16::sp], 0x0006);
  // A segment load holds interrupt recognition off for one instruction,
  // the same window STI opens (interrupts.h) — POP CS is a segment load
  // like any other.
  EXPECT_TRUE(cpu_.interrupts_inhibited());
}

// --- 9B WAIT --------------------------------------------------------------

TEST_F(Misc, WaitAdvancesIpByOneAndChangesNothingElse) {
  program({0x9B});
  const registers before = cpu_.regs();
  const std::size_t accesses_before = bus_.accesses.size();

  ASSERT_EQ(cpu_.step(), step_status::ran);

  registers after = cpu_.regs();
  EXPECT_EQ(after.ip, before.ip + 1);
  after.ip = before.ip;
  // Nothing else moved: no TEST pin means no coprocessor to wait for, so
  // WAIT is a NOP that happens to fetch its own opcode byte and nothing
  // more.
  EXPECT_EQ(after, before);
  EXPECT_EQ(bus_.accesses.size(), accesses_before + 1);
}

// --- F4 HLT ---------------------------------------------------------------

TEST_F(Misc, HltEntersTheHaltedStateAndThenConsumesNothing) {
  program({0xF4});

  // The instruction itself runs to completion — it is what *puts* the
  // processor in the halted state, not itself a no-op step.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.halted());

  const std::size_t accesses_before = bus_.accesses.size();
  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_EQ(cpu_.step(), step_status::halted);
  // step() reports halted and consumed nothing meanwhile — wake-on-
  // interrupt is #17's machinery and is tested there.
  EXPECT_EQ(bus_.accesses.size(), accesses_before);
}

}  // namespace
}  // namespace amberfolio::cpu
