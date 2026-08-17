// SPDX-License-Identifier: AGPL-3.0-only
//
// The skeleton: reset state, address formation, the bus round trip, and
// the unimplemented-opcode stop.
//
// The stop tests are the load-bearing ones. "Log, don't fake" is the rule
// the whole emulator is built on (PLAN.md §3), and a rule that is only
// written down in a document is a rule that erodes — so what is checked
// here is not just that an unhandled opcode stops, but that it stops
// *cleanly*: the record names the instruction, IP still points at it, the
// bus is not touched again, and the report happens once rather than on
// every subsequent step.

#include "amberfolio/cpu/processor.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/registers.h"
#include "cpu/test_bus.h"
#include "cpu/test_dispatch.h"

namespace amberfolio::cpu {
namespace {

using test::recording_diagnostics;
using test::test_bus;

// Every opcode on an 8086 does something — even the ones Intel never
// documented — so there is no byte that is permanently a hole in the
// table. These tests hand the processor an empty table of their own
// instead (test_dispatch.h), which keeps them true after the wide phase
// has filled the real one. 0x90 is NOP, chosen because a reader who sees
// it in a memory dump below will not go looking for a meaning it does not
// have here.
constexpr std::uint8_t some_opcode = 0x90;

/// A processor whose instruction set is empty: nothing is implemented.
const dispatch_table& no_instructions() {
  static const dispatch_table table = test::nothing();
  return table;
}

TEST(Processor, ResetIsTheRealPowerOnState) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  // Execution begins at FFFF:0000 — sixteen bytes below the top of the
  // address space, which is why a PC's ROM starts there with a jump.
  EXPECT_EQ(cpu.regs()[sreg::cs], 0xFFFF);
  EXPECT_EQ(cpu.regs().ip, 0);

  EXPECT_EQ(cpu.regs()[sreg::ds], 0);
  EXPECT_EQ(cpu.regs()[sreg::es], 0);
  EXPECT_EQ(cpu.regs()[sreg::ss], 0);
  for (const std::uint16_t w : cpu.regs().word) {
    EXPECT_EQ(w, 0);
  }

  // No flag set — which still reads back as 0xF002 on this part.
  EXPECT_EQ(cpu.regs().flags, flag::reset_value);
  EXPECT_EQ(cpu.regs().flags & flag::defined, 0);

  EXPECT_FALSE(cpu.halted());
  EXPECT_FALSE(cpu.stopped());
  EXPECT_EQ(cpu.stop().reason, stop_reason::none);
}

TEST(Processor, ResetClearsAHaltAndAStop) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  cpu.regs()[sreg::cs] = 0x1234;
  cpu.regs().ip = 0x5678;
  cpu.halt();
  EXPECT_EQ(cpu.step(), step_status::halted);

  cpu.reset();
  EXPECT_FALSE(cpu.halted());
  EXPECT_EQ(cpu.regs()[sreg::cs], 0xFFFF);

  mem.poke(0xFFFF, 0x0000, {some_opcode});
  EXPECT_EQ(cpu.step(), step_status::stopped);
  ASSERT_TRUE(cpu.stopped());

  cpu.reset();
  EXPECT_FALSE(cpu.stopped());
  EXPECT_EQ(cpu.stop().reason, stop_reason::none);
}

TEST(Address, PhysicalAddressIsSegmentTimesSixteenPlusOffset) {
  EXPECT_EQ(physical_address(0x0000, 0x0000), 0x00000u);
  EXPECT_EQ(physical_address(0x0000, 0xFFFF), 0x0FFFFu);
  EXPECT_EQ(physical_address(0x1000, 0x0000), 0x10000u);
  EXPECT_EQ(physical_address(0x1234, 0x5678), 0x179B8u);
  EXPECT_EQ(physical_address(0xB800, 0x0000), 0xB8000u);
}

// The 8086 has twenty address pins and no twenty-first, so the top of the
// address space wraps to the bottom. Real programs relied on it; the A20
// gate exists because the 286 stopped doing it.
TEST(Address, PhysicalAddressWrapsAtOneMegabyte) {
  EXPECT_EQ(physical_address(0xFFFF, 0x0010), 0x00000u);
  EXPECT_EQ(physical_address(0xFFFF, 0x0011), 0x00001u);
  EXPECT_EQ(physical_address(0xFFFF, 0xFFFF), 0x0FFEFu);
  EXPECT_EQ(physical_address(0xF000, 0xFFFF), 0xFFFFFu);

  // Never wider than the address space, for any pair at all.
  for (std::uint32_t segment = 0; segment <= 0xFFFF; segment += 0x111) {
    const auto seg = static_cast<std::uint16_t>(segment);
    EXPECT_LT(physical_address(seg, 0xFFFF), address_space_size);
  }
}

TEST(Processor, ByteAccessGoesThroughTheBusAtThePhysicalAddress) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  mem.poke(0x179B8, {0x5A});
  EXPECT_EQ(cpu.read_byte(0x1234, 0x5678), 0x5A);

  cpu.write_byte(0x1234, 0x5679, 0xA5);
  EXPECT_EQ(mem.peek(0x179B9), 0xA5);
}

TEST(Processor, ByteAccessWrapsWithThePhysicalAddress) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  mem.poke(0x00000, {0x77});
  EXPECT_EQ(cpu.read_byte(0xFFFF, 0x0010), 0x77);

  cpu.write_byte(0xFFFF, 0x0011, 0x88);
  EXPECT_EQ(mem.peek(0x00001), 0x88);
}

TEST(Processor, AnOpcodeWithNoHandlerStopsAndSaysWhich) {
  test_bus mem;
  recording_diagnostics log;
  processor cpu(mem, &log, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0x0100;
  mem.poke(0x2000, 0x0100, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_TRUE(cpu.stopped());
  EXPECT_EQ(cpu.stop().reason, stop_reason::unimplemented_opcode);
  EXPECT_EQ(cpu.stop().opcode, some_opcode);
  EXPECT_EQ(cpu.stop().cs, 0x2000);
  EXPECT_EQ(cpu.stop().ip, 0x0100);
  // 0x90 is not a group opcode, so there is no ModRM reg field to name.
  EXPECT_EQ(cpu.stop().extension, no_extension);

  ASSERT_EQ(log.reports.size(), 1u);
  EXPECT_EQ(log.reports.front(), cpu.stop());
}

// A clean stop leaves the machine where the instruction found it, so the
// state is inspectable and — once the handler exists — re-enterable. IP
// must not be left pointing past a byte we did not execute.
TEST(Processor, AStopRewindsIpToTheInstructionItRefused) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0x0100;
  mem.poke(0x2000, 0x0100, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.regs().ip, 0x0100);
}

TEST(Processor, StoppedIsStickyAndReportedOnce) {
  test_bus mem;
  recording_diagnostics log;
  processor cpu(mem, &log, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0x0100;
  mem.poke(0x2000, 0x0100, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  const stop_record first = cpu.stop();

  // Put a different byte there. A stopped processor must not fetch it.
  mem.poke(0x2000, 0x0100, {0xEB});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.stop(), first);
  EXPECT_EQ(log.reports.size(), 1u);
}

// The sink is optional; the stop is not. Without one, nothing is lost —
// the caller still gets `stopped` and the record is still there to read.
TEST(Processor, StopsWithoutASinkJustAsLoudly) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  mem.poke(0xFFFF, 0x0000, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.stop().reason, stop_reason::unimplemented_opcode);
  EXPECT_EQ(cpu.stop().opcode, some_opcode);
}

TEST(Processor, AHaltedProcessorConsumesNothingUntilItResumes) {
  test_bus mem;
  recording_diagnostics log;
  processor cpu(mem, &log, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0x0100;
  mem.poke(0x2000, 0x0100, {some_opcode});

  cpu.halt();
  EXPECT_TRUE(cpu.halted());
  EXPECT_EQ(cpu.step(), step_status::halted);
  EXPECT_EQ(cpu.step(), step_status::halted);
  EXPECT_EQ(cpu.regs().ip, 0x0100);
  EXPECT_TRUE(log.reports.empty());

  // What M1-F7's interrupt delivery will do.
  cpu.resume();
  EXPECT_FALSE(cpu.halted());
  EXPECT_EQ(cpu.step(), step_status::stopped);
}

// Fetch is a bus read at CS:IP like any other read — no prefetch queue,
// no read-ahead. M1 is architectural correctness only (PLAN.md §3), and a
// step that stopped on its first byte must have taken exactly one byte off
// the bus.
TEST(Processor, FetchIsOneBusReadAtCsIp) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0x0100;
  mem.poke(0x20100, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);

  ASSERT_EQ(mem.accesses.size(), 1u);
  EXPECT_EQ(mem.accesses.front(),
            (test::memory_access{.what = test::memory_access::kind::read,
                                 .address = 0x20100u,
                                 .value = some_opcode}));
}

// A code segment's offsets wrap in sixteen bits like any other: an
// instruction stream that runs off the end continues at the bottom of the
// same segment rather than rolling into the next one. Nothing here
// executes past one byte yet, so what this pins is the fetch address at
// the very top of a segment; M1-F3, where instructions have operands, is
// where the IP advance across that boundary gets exercised.
TEST(Processor, FetchAtTheTopOfASegmentStaysInIt) {
  test_bus mem;
  processor cpu(mem, nullptr, no_instructions());

  cpu.regs()[sreg::cs] = 0x2000;
  cpu.regs().ip = 0xFFFF;
  mem.poke(0x2FFFF, {some_opcode});

  EXPECT_EQ(cpu.step(), step_status::stopped);
  EXPECT_EQ(cpu.stop().opcode, some_opcode);
  EXPECT_EQ(cpu.stop().ip, 0xFFFF);

  ASSERT_EQ(mem.accesses.size(), 1u);
  EXPECT_EQ(mem.accesses.front().address, 0x2FFFFu);
}

}  // namespace
}  // namespace amberfolio::cpu
