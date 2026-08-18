// SPDX-License-Identifier: AGPL-3.0-only
//
// Interrupt delivery (M1-F7).
//
// This file carries more of the milestone's weight than its size suggests.
// The single-step vectors are the authority on everything else the CPU
// does, and they say nothing at all here: the suite never sets IF or TF
// and never raises a line, so not one of the behaviours below is checked
// by a single one of its 323 files. What is written down here is the only
// thing standing between the timing windows and a plausible guess.
//
// So the tests are written to pin *when*, not just *what*. "An interrupt
// is delivered" is easy and half the story; "it is delivered after
// exactly one more instruction, and the address pushed is the last prefix
// byte rather than the first" is the story.
//
// The instructions are stand-ins. STI, CLI, HLT, MOV Sreg and the string
// family all belong to issues that have not landed, and a test that
// waited for them would be a test that never got written — so each one is
// a one-line handler in a dispatch table of this file's own, doing the
// one thing about that instruction this machinery cares about. When the
// real ones arrive they call exactly these entry points.

#include "amberfolio/cpu/interrupts.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "cpu/test_bus.h"

namespace amberfolio::cpu {
namespace {

using test::test_bus;

// --- The stand-in instruction set -------------------------------------
//
// 0x90-0x97 is the XCHG AX,r16 row: no ModRM byte, no immediate, one byte
// each. What they mean on a real 8086 is irrelevant — the table below is
// this file's own, and nothing in the shared one is consulted.

constexpr std::uint8_t op_nothing = 0x90;
constexpr std::uint8_t op_sti = 0x91;
constexpr std::uint8_t op_cli = 0x92;
constexpr std::uint8_t op_load_segment = 0x93;
constexpr std::uint8_t op_halt = 0x94;
constexpr std::uint8_t op_repeat = 0x95;
constexpr std::uint8_t op_software_interrupt = 0x96;
constexpr std::uint8_t op_load_flags = 0x97;

/// How many more iterations `op_repeat` has in it. Handlers are plain
/// function pointers and cannot capture; a test binary runs one case at a
/// time, so a file-scope knob the fixture resets is honest here.
int repeats_left = 0;

/// The vector `op_software_interrupt` delivers.
std::uint8_t software_vector = 0;

/// The flag word `op_load_flags` gives the program.
std::uint16_t flags_to_load = 0;

void do_nothing(processor& /*cpu*/) {}

/// STI: sets IF, and holds recognition off for one instruction.
void enable_interrupts(processor& cpu) {
  cpu.regs().set_flag(flag::if_, true);
  cpu.inhibit_interrupts();
}

/// CLI: clears IF, and opens no window at all.
void disable_interrupts(processor& cpu) {
  cpu.regs().set_flag(flag::if_, false);
}

/// MOV Sreg,r/m or POP Sreg. Which segment register it loaded does not
/// matter to this machinery — see interrupts.h on why the 8086 does not
/// check either.
void load_segment(processor& cpu) { cpu.inhibit_interrupts(); }

void halt(processor& cpu) { cpu.halt(); }

/// A repeated string instruction, rewinding IP to the whole instruction
/// the way the real one does (M1-I13 owns that; processor.h's
/// keep_repeating() is the contract).
void repeat(processor& cpu) {
  if (repeats_left > 0) {
    --repeats_left;
    cpu.regs().ip = cpu.current().start_ip;
    cpu.keep_repeating();
  }
}

/// INT n. By the time a real one calls delivery it has fetched its own
/// immediate, so IP is already past the instruction; this one has no
/// immediate to fetch and is past it for the same reason.
void software_interrupt(processor& cpu) {
  cpu.deliver_interrupt(software_vector);
}

/// POPF or IRET: a flag word that came from the program.
void load_flag_word(processor& cpu) { cpu.regs().load_flags(flags_to_load); }

[[nodiscard]] dispatch_table stand_ins() {
  dispatch_table t{};
  t.primary[op_nothing] = &do_nothing;
  t.primary[op_sti] = &enable_interrupts;
  t.primary[op_cli] = &disable_interrupts;
  t.primary[op_load_segment] = &load_segment;
  t.primary[op_halt] = &halt;
  t.primary[op_repeat] = &repeat;
  t.primary[op_software_interrupt] = &software_interrupt;
  t.primary[op_load_flags] = &load_flag_word;
  return t;
}

// --- The fixture ------------------------------------------------------

constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t code_offset = 0x0100;
constexpr std::uint16_t stack_segment = 0x3000;
constexpr std::uint16_t stack_top = 0x0100;

/// Where every vector under test points, one segment per vector so that a
/// test that lands in the wrong handler says which.
constexpr std::uint16_t handler_segment = 0x4000;
constexpr std::uint16_t handler_offset = 0x0050;

[[nodiscard]] constexpr std::uint8_t low(std::uint16_t value) {
  return static_cast<std::uint8_t>(value);
}

[[nodiscard]] constexpr std::uint8_t high(std::uint16_t value) {
  return static_cast<std::uint8_t>(value >> 8u);
}

class Interrupts : public ::testing::Test {
 protected:
  Interrupts() : cpu_(bus_, nullptr, table_) {
    repeats_left = 0;
    software_vector = 0;
    flags_to_load = 0;

    cpu_.regs()[sreg::cs] = code_segment;
    cpu_.regs().ip = code_offset;
    cpu_.regs()[sreg::ss] = stack_segment;
    cpu_.regs()[reg16::sp] = stack_top;
  }

  /// The bytes the processor is about to execute, at CS:IP.
  void program(std::initializer_list<std::uint8_t> bytes) {
    bus_.poke(code_segment, code_offset, bytes);
  }

  /// Point a vector at a handler, and put an instruction there so a test
  /// can carry on stepping once it has arrived.
  void point_vector(std::uint8_t vector, std::uint16_t segment,
                    std::uint16_t offset) {
    bus_.poke(vector_table_segment, vector_table_offset(vector),
              {low(offset), high(offset), low(segment), high(segment)});
    bus_.poke(segment, offset, {op_nothing, op_nothing});
  }

  /// Point one at the standard handler address, offset by the vector so
  /// that "which handler did it land in" has an answer.
  void point_vector(std::uint8_t vector) {
    point_vector(vector, static_cast<std::uint16_t>(handler_segment + vector),
                 handler_offset);
  }

  [[nodiscard]] std::uint16_t word_at(std::uint16_t segment,
                                      std::uint16_t offset) const {
    const auto next = static_cast<std::uint16_t>(offset + 1u);
    return static_cast<std::uint16_t>(bus_.peek(segment, offset) |
                                      (bus_.peek(segment, next) << 8u));
  }

  /// The interrupt frame, innermost word first: 0 is the return IP, 1 the
  /// return CS, 2 the saved FLAGS.
  [[nodiscard]] std::uint16_t frame(int slot) const {
    const auto at = static_cast<std::uint16_t>(cpu_.regs()[reg16::sp] +
                                               2 * static_cast<unsigned>(slot));
    return word_at(stack_segment, at);
  }

  [[nodiscard]] std::uint16_t handler_cs(std::uint8_t vector) const {
    return static_cast<std::uint16_t>(handler_segment + vector);
  }

  test_bus bus_;
  dispatch_table table_ = stand_ins();
  processor cpu_;
};

// --- The sequence -----------------------------------------------------

TEST_F(Interrupts, DeliveryLoadsCsAndIpFromTheVectorTableEntry) {
  // 0x21 * 4 is 0x84, and the entry is offset first, then segment. Only
  // this one entry is written, so landing anywhere means the arithmetic
  // or the byte order is wrong rather than merely off by an entry.
  software_vector = 0x21;
  point_vector(0x21);
  program({op_software_interrupt});

  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x21));
  EXPECT_EQ(cpu_.regs().ip, handler_offset);
  EXPECT_EQ(word_at(vector_table_segment, 0x0084), handler_offset);
  EXPECT_EQ(word_at(vector_table_segment, 0x0086), handler_cs(0x21));
}

TEST_F(Interrupts, DeliveryPushesFlagsThenCsThenIp) {
  software_vector = 0x40;
  point_vector(0x40);
  program({op_software_interrupt});

  cpu_.regs().set_flag(flag::cf | flag::zf, true);
  const std::uint16_t flags_before = cpu_.regs().flags;

  ASSERT_EQ(cpu_.step(), step_status::ran);

  EXPECT_EQ(cpu_.regs()[reg16::sp], stack_top - 6);
  // The return address is the byte after the instruction: a software
  // interrupt returns *past* itself.
  EXPECT_EQ(frame(0), code_offset + 1);
  EXPECT_EQ(frame(1), code_segment);
  EXPECT_EQ(frame(2), flags_before);
}

TEST_F(Interrupts, DeliveryClearsIfAndTfAndTouchesNoOtherFlag) {
  software_vector = 0x40;
  point_vector(0x40);
  program({op_software_interrupt});

  cpu_.regs().set_flag(flag::if_ | flag::tf | flag::cf | flag::df | flag::of,
                       true);
  const std::uint16_t flags_before = cpu_.regs().flags;

  ASSERT_EQ(cpu_.step(), step_status::ran);

  EXPECT_FALSE(cpu_.regs().flag_set(flag::if_));
  EXPECT_FALSE(cpu_.regs().flag_set(flag::tf));
  EXPECT_TRUE(cpu_.regs().flag_set(flag::cf));
  EXPECT_TRUE(cpu_.regs().flag_set(flag::df));
  EXPECT_TRUE(cpu_.regs().flag_set(flag::of));

  // What the handler will hand back is what the program had, IF and TF
  // included. Saving the cleared word instead would silently disable
  // interrupts for good the first time a handler returned.
  EXPECT_EQ(frame(2), flags_before);
  EXPECT_TRUE((frame(2) & flag::if_) != 0);
  EXPECT_TRUE((frame(2) & flag::tf) != 0);
}

TEST_F(Interrupts, DeliveryWrapsTheStackPointerInsideItsSegment) {
  software_vector = 0x40;
  point_vector(0x40);
  program({op_software_interrupt});

  // Three words from 0x0004 puts the last one at 0xFFFE: SP wraps in
  // sixteen bits and never carries into SS.
  cpu_.regs()[reg16::sp] = 0x0004;
  const std::uint16_t flags_before = cpu_.regs().flags;

  ASSERT_EQ(cpu_.step(), step_status::ran);

  EXPECT_EQ(cpu_.regs()[reg16::sp], 0xFFFE);
  EXPECT_EQ(word_at(stack_segment, 0xFFFE), code_offset + 1);
  EXPECT_EQ(word_at(stack_segment, 0x0000), code_segment);
  EXPECT_EQ(word_at(stack_segment, 0x0002), flags_before);
}

// push_word and pop_word are here rather than in the stack family's tests
// (#23) because delivery is what needed them first. What #23 adds is the
// instructions; this is the primitive underneath them.
TEST_F(Interrupts, PushMovesTheStackPointerBeforeItWritesAndPopAfterItReads) {
  cpu_.regs()[reg16::sp] = 0x0000;

  cpu_.push_word(0xBEEF);
  EXPECT_EQ(cpu_.regs()[reg16::sp], 0xFFFE);
  EXPECT_EQ(word_at(stack_segment, 0xFFFE), 0xBEEF);

  EXPECT_EQ(cpu_.pop_word(), 0xBEEF);
  EXPECT_EQ(cpu_.regs()[reg16::sp], 0x0000);
}

// --- The external lines -----------------------------------------------

TEST_F(Interrupts, NmiIsDeliveredAtTheNextBoundaryWhateverIfSays) {
  point_vector(interrupt_vector::nmi);
  program({op_nothing});

  cpu_.regs().set_flag(flag::if_, false);
  cpu_.raise_nmi();
  EXPECT_TRUE(cpu_.nmi_pending());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::nmi));
  EXPECT_EQ(cpu_.regs().ip, handler_offset);
  // The instruction at CS:IP never ran: the boundary came first.
  EXPECT_EQ(frame(0), code_offset);
}

TEST_F(Interrupts, TheNmiLatchIsAnEdgeAndIsConsumedOnce) {
  point_vector(interrupt_vector::nmi);
  program({op_nothing});

  cpu_.raise_nmi();
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_FALSE(cpu_.nmi_pending());

  // The handler runs; nothing re-delivers.
  EXPECT_EQ(cpu_.step(), step_status::ran);
  EXPECT_EQ(cpu_.step(), step_status::ran);
}

TEST_F(Interrupts, IntrWaitsForIf) {
  point_vector(0x08);
  program({op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::if_, false);
  cpu_.raise_intr(0x08);

  // Masked: the instruction runs and the request stays up.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.intr_pending());
  EXPECT_EQ(cpu_.regs()[sreg::cs], code_segment);

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x08));
  EXPECT_EQ(frame(0), code_offset + 1);
}

// The acknowledge cycle this machine has: a controller that still has
// work re-raises, and one that does not, does not.
TEST_F(Interrupts, DeliveringIntrDropsTheLevel) {
  point_vector(0x08);
  program({op_nothing});

  cpu_.regs().set_flag(flag::if_, true);
  cpu_.raise_intr(0x08);

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_FALSE(cpu_.intr_pending());
  EXPECT_EQ(cpu_.step(), step_status::ran);
}

TEST_F(Interrupts, ClearIntrWithdrawsARequestThatWasNeverTaken) {
  program({op_nothing});

  cpu_.regs().set_flag(flag::if_, true);
  cpu_.raise_intr(0x08);
  cpu_.clear_intr();
  EXPECT_FALSE(cpu_.intr_pending());

  EXPECT_EQ(cpu_.step(), step_status::ran);
}

TEST_F(Interrupts, TheLastVectorRaisedIsTheOneDelivered) {
  point_vector(0x0C);
  program({op_nothing});

  cpu_.regs().set_flag(flag::if_, true);
  cpu_.raise_intr(0x08);
  cpu_.raise_intr(0x0C);

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x0C));
}

TEST_F(Interrupts, NmiIsTakenBeforeAnUnmaskedIntr) {
  point_vector(interrupt_vector::nmi);
  point_vector(0x08);
  program({op_nothing});

  cpu_.regs().set_flag(flag::if_, true);
  cpu_.raise_intr(0x08);
  cpu_.raise_nmi();

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::nmi));

  // The INTR level is still up, but delivery cleared IF, so the NMI
  // handler runs undisturbed until it says otherwise.
  ASSERT_TRUE(cpu_.intr_pending());
  EXPECT_EQ(cpu_.step(), step_status::ran);

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x08));
}

// --- The single-step trap ---------------------------------------------

TEST_F(Interrupts, TheTrapFiresAfterTheInstructionThatBeganWithTfSet) {
  point_vector(interrupt_vector::single_step);
  program({op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::tf, true);

  // The flag was set before this instruction, not by it — so the trap is
  // owed at the end of it, not at the boundary in front of it.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.trap_pending());
  EXPECT_EQ(cpu_.regs().ip, code_offset + 1);

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::single_step));
  EXPECT_EQ(frame(0), code_offset + 1);
}

TEST_F(Interrupts, DeliveringTheTrapClearsTfSoItCannotRecurse) {
  point_vector(interrupt_vector::single_step);
  program({op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::tf, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_EQ(cpu_.step(), step_status::serviced);

  EXPECT_FALSE(cpu_.regs().flag_set(flag::tf));
  EXPECT_FALSE(cpu_.trap_pending());

  // The handler is not single-stepped. Without the TF clear inside
  // delivery this would trap forever, one frame deeper each time.
  EXPECT_EQ(cpu_.step(), step_status::ran);
  EXPECT_EQ(cpu_.step(), step_status::ran);
}

TEST_F(Interrupts, AnInstructionThatSetsTfIsNotItselfTrapped) {
  point_vector(interrupt_vector::single_step);
  program({op_load_flags, op_nothing, op_nothing});
  flags_to_load = flag::tf;

  // POPF (or IRET) turning single-stepping back on. It began with TF
  // clear, so it owes nothing — which is the whole reason a debugger can
  // return to the program it is tracing without trapping in its own IRET.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.regs().flag_set(flag::tf));
  EXPECT_FALSE(cpu_.trap_pending());

  // The instruction after it is the one that traps.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.trap_pending());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(frame(0), code_offset + 2);
}

// Tracing over an INT lands in the handler, which is what makes DEBUG's
// T command step into DOS. The INT's own delivery clears TF, but the trap
// was already owed by the time it did.
TEST_F(Interrupts, TracingASoftwareInterruptLandsOnTheHandler) {
  software_vector = 0x21;
  point_vector(0x21);
  point_vector(interrupt_vector::single_step);
  program({op_software_interrupt});

  cpu_.regs().set_flag(flag::tf, true);

  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x21));
  ASSERT_TRUE(cpu_.trap_pending());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::single_step));
  EXPECT_EQ(frame(0), handler_offset);
  EXPECT_EQ(frame(1), handler_cs(0x21));
}

TEST_F(Interrupts, AnOwedTrapIsDroppedWhenSomethingElseTakesTheBoundary) {
  point_vector(interrupt_vector::nmi);
  point_vector(interrupt_vector::single_step);
  program({op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::tf, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.trap_pending());

  cpu_.raise_nmi();
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::nmi));

  // Single-step is the lowest priority, and lowest priority is meant
  // literally: the trap is not queued behind the NMI. Delivery cleared
  // TF, so there is nothing left for it to be about.
  EXPECT_FALSE(cpu_.trap_pending());
  EXPECT_EQ(cpu_.step(), step_status::ran);
}

// --- The one-instruction windows --------------------------------------

TEST_F(Interrupts, StiRecognizesInterruptsOnlyAfterTheFollowingInstruction) {
  point_vector(0x08);
  program({op_sti, op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::if_, false);
  cpu_.raise_intr(0x08);

  // IF was clear at this boundary, so nothing is taken; STI sets it and
  // opens the window.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.regs().flag_set(flag::if_));
  EXPECT_TRUE(cpu_.interrupts_inhibited());

  // The window: IF is set and a request is up, and the instruction runs
  // anyway. This is what lets `STI` / `RET` leave a critical section.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_FALSE(cpu_.interrupts_inhibited());
  EXPECT_TRUE(cpu_.intr_pending());
  EXPECT_EQ(cpu_.regs().ip, code_offset + 2);

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(frame(0), code_offset + 2);
}

TEST_F(Interrupts, CliTakesEffectWithNoWindowAtAll) {
  program({op_cli, op_nothing});

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_FALSE(cpu_.regs().flag_set(flag::if_));
  EXPECT_FALSE(cpu_.interrupts_inhibited());

  // Raised the instant the flag went down. There is no grace period on
  // this side: the next boundary is already closed.
  cpu_.raise_intr(0x08);
  EXPECT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.intr_pending());
}

TEST_F(Interrupts, ASegmentLoadHoldsInterruptsOffForOneInstruction) {
  point_vector(0x08);
  program({op_load_segment, op_nothing, op_nothing});

  cpu_.regs().set_flag(flag::if_, true);

  // The MOV SS half of the pair.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.interrupts_inhibited());

  // The request arrives inside the window — which is the case the window
  // exists for. If it were taken here, six bytes would go on a stack that
  // has a new SS and an old SP.
  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.intr_pending());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(frame(0), code_offset + 2);
}

TEST_F(Interrupts, TheWindowHoldsOffNmiToo) {
  point_vector(interrupt_vector::nmi);
  program({op_load_segment, op_nothing, op_nothing});

  ASSERT_EQ(cpu_.step(), step_status::ran);
  cpu_.raise_nmi();

  // Ungated is not the same as unstoppable: IF has no say over NMI, but
  // the recognition window is not about IF.
  ASSERT_EQ(cpu_.step(), step_status::ran);
  EXPECT_TRUE(cpu_.nmi_pending());

  EXPECT_EQ(cpu_.step(), step_status::serviced);
}

// The 80286 suppresses the trap after a MOV to SS as well, and documents
// it as a fix. The 8086 is the part that made it worth fixing; we are not
// a 286 (registers.h says the same about the top four flag bits), so only
// external recognition is held off here.
TEST_F(Interrupts, TheWindowDoesNotHoldOffTheSingleStepTrap) {
  point_vector(interrupt_vector::single_step);
  program({op_load_segment, op_nothing});

  cpu_.regs().set_flag(flag::tf, true);

  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.interrupts_inhibited());
  ASSERT_TRUE(cpu_.trap_pending());

  EXPECT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::single_step));
}

// --- HLT --------------------------------------------------------------

TEST_F(Interrupts, AHaltEndsOnAnUnmaskedIntrAndResumesPastTheHlt) {
  point_vector(0x08);
  program({op_halt});

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.halted());

  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_EQ(cpu_.step(), step_status::halted);

  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_FALSE(cpu_.halted());
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(0x08));
  // Past the HLT, so an IRET does not halt again.
  EXPECT_EQ(frame(0), code_offset + 1);
}

TEST_F(Interrupts, AMaskedIntrDoesNotEndAHalt) {
  program({op_halt});

  cpu_.regs().set_flag(flag::if_, false);
  ASSERT_EQ(cpu_.step(), step_status::ran);

  cpu_.raise_intr(0x08);
  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_TRUE(cpu_.halted());
  EXPECT_TRUE(cpu_.intr_pending());
}

TEST_F(Interrupts, AHaltEndsOnNmiWhateverIfSays) {
  point_vector(interrupt_vector::nmi);
  program({op_halt});

  cpu_.regs().set_flag(flag::if_, false);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_EQ(cpu_.step(), step_status::halted);

  cpu_.raise_nmi();
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_FALSE(cpu_.halted());
}

TEST_F(Interrupts, AnOwedTrapEndsAHaltToo) {
  point_vector(interrupt_vector::single_step);
  program({op_halt});

  cpu_.regs().set_flag(flag::tf, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_TRUE(cpu_.halted());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_FALSE(cpu_.halted());
  EXPECT_EQ(cpu_.regs()[sreg::cs], handler_cs(interrupt_vector::single_step));
}

TEST_F(Interrupts, AHaltedProcessorTouchesTheBusForNothing) {
  program({op_halt});

  ASSERT_EQ(cpu_.step(), step_status::ran);
  const std::size_t accesses = bus_.accesses.size();

  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_EQ(cpu_.step(), step_status::halted);
  EXPECT_EQ(bus_.accesses.size(), accesses);
}

// --- An interrupted REP, and the prefix the 8086 forgets ---------------

TEST_F(Interrupts, AnInterruptBetweenIterationsResumesTheRun) {
  point_vector(0x08);
  program({0xF3, op_repeat});
  repeats_left = 4;

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::repeating);
  ASSERT_EQ(cpu_.regs().ip, code_offset);

  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::serviced);

  // One prefix, so backing up to the last one and backing up to the
  // instruction are the same address — and IRET resumes the run.
  EXPECT_EQ(frame(0), code_offset);
}

// The quirk, in the shape that costs the program its segment override.
TEST_F(Interrupts, AnInterruptedRepWithAnOverrideLosesTheOverride) {
  point_vector(0x08);
  program({0x2E, 0xF3, op_repeat});
  repeats_left = 4;

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::repeating);

  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::serviced);

  // The REP survives; the CS: in front of it does not. The rest of the
  // run reads from DS, which is not what the program wrote.
  EXPECT_EQ(frame(0), code_offset + 1);
  EXPECT_NE(frame(0), code_offset);
}

// The same quirk in the other prefix order, where what is lost is the
// repeat itself: one more iteration runs and CX keeps what was left.
TEST_F(Interrupts, AnInterruptedRepBehindAnOverrideLosesTheRepeat) {
  point_vector(0x08);
  program({0xF3, 0x2E, op_repeat});
  repeats_left = 4;

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::repeating);

  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::serviced);

  EXPECT_EQ(frame(0), code_offset + 1);
}

TEST_F(Interrupts, TheTrapResumesAtThePrefixToo) {
  point_vector(interrupt_vector::single_step);
  program({0x2E, 0xF3, op_repeat});
  repeats_left = 4;

  // Single-stepping a REP traps once per iteration, and each trap resumes
  // the run the same way an external interrupt does.
  cpu_.regs().set_flag(flag::tf, true);
  ASSERT_EQ(cpu_.step(), step_status::repeating);
  ASSERT_TRUE(cpu_.trap_pending());

  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(frame(0), code_offset + 1);
}

TEST_F(Interrupts, ARetiredRepIsNotResumedAtItsPrefix) {
  point_vector(0x08);
  program({0x2E, 0xF3, op_repeat});
  repeats_left = 0;

  cpu_.regs().set_flag(flag::if_, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  ASSERT_EQ(cpu_.regs().ip, code_offset + 3);

  // It retired, so there is nothing to back up into: the return address
  // is the next instruction like anybody else's.
  cpu_.raise_intr(0x08);
  ASSERT_EQ(cpu_.step(), step_status::serviced);
  EXPECT_EQ(frame(0), code_offset + 3);
}

// --- Reset ------------------------------------------------------------

TEST_F(Interrupts, ResetDropsEveryPendingRequest) {
  program({op_load_segment});

  cpu_.regs().set_flag(flag::tf, true);
  ASSERT_EQ(cpu_.step(), step_status::ran);
  cpu_.raise_nmi();
  cpu_.raise_intr(0x08);

  ASSERT_TRUE(cpu_.nmi_pending());
  ASSERT_TRUE(cpu_.intr_pending());
  ASSERT_TRUE(cpu_.trap_pending());
  ASSERT_TRUE(cpu_.interrupts_inhibited());

  cpu_.reset();

  EXPECT_FALSE(cpu_.nmi_pending());
  EXPECT_FALSE(cpu_.intr_pending());
  EXPECT_FALSE(cpu_.trap_pending());
  EXPECT_FALSE(cpu_.interrupts_inhibited());
}

}  // namespace
}  // namespace amberfolio::cpu
