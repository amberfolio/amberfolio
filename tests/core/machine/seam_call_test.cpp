// SPDX-License-Identifier: AGPL-3.0-only
//
// Calling the program (M5-D4, #188): a seam asks the program to run one
// of its own routines, and comes back.
//
// The routine here is **this file's own 8086**, thirteen instructions
// written for the test — the real consumers call the game's text and
// frame drawers, and what that has in common with this is only the frame
// the engine builds. So the test writes a routine that reports what it
// was handed: two words off its stack, one byte through a far pointer,
// and a call counter. If the engine builds the frame the program's own
// callers build, that routine reads the right things; if it does not, the
// numbers are wrong in a way that says which end is wrong.
//
// Everything about a batch that is not "did the call happen" is here too,
// because they are the properties that make it safe to use: the machine
// is put back exactly as it was, the calls run in the order they were
// queued, a routine that never returns is abandoned rather than waited
// for, and a seam that queues nothing costs the machine nothing.

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

/// The binary this file's seams claim, and the digest a test "loads".
constexpr std::string_view claimed_hex =
    "3333333333333333333333333333333333333333333333333333333333333333";
constexpr std::array<std::string_view, 1> claimed_binaries{claimed_hex};

[[nodiscard]] sha256_digest claimed_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(claimed_hex, digest));
  return digest;
}

/// Where the test puts its own machine. The routine lives in a segment of
/// its own, so a call that landed anywhere else lands somewhere blank.
constexpr std::uint16_t routine_segment = 0x2000;
constexpr std::uint16_t routine_offset = 0x0000;
constexpr std::uint16_t spinning_offset = 0x0100;
constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x4000;
constexpr std::uint16_t stack_pointer = 0x0800;

/// Where the routine writes what it was handed, in `data_segment`.
constexpr std::uint16_t saw_first_word = 0x1000;
constexpr std::uint16_t saw_second_word = 0x1002;
constexpr std::uint16_t saw_byte = 0x1004;
constexpr std::uint16_t call_count = 0x1006;

/// The two words the handler passes, and the byte it places.
constexpr std::uint16_t first_word = 0x1111;
constexpr std::uint16_t second_word = 0x2222;
constexpr std::uint8_t placed_byte = 0x5A;

/// This file's own routine, assembled by hand.
///
///     push bp / mov bp, sp
///     mov ax, [bp+0Ch] / mov [saw_first_word], ax    ; the deepest word
///     mov ax, [bp+0Ah] / mov [saw_second_word], ax
///     les di, [bp+6]   / mov al, es:[di] / xor ah, ah
///     mov [saw_byte], ax                             ; through the far ptr
///     inc word [call_count]
///     mov sp, bp / pop bp / retf 8
///
/// Four words of arguments, so `retf 8` — the routine cleans them up, the
/// way every one of the program's own does.
constexpr std::array<std::uint8_t, 33> routine{
    0x55, 0x89, 0xE5,                    // push bp / mov bp, sp
    0x8B, 0x46, 0x0C, 0xA3, 0x00, 0x10,  // [bp+0Ch] -> saw_first_word
    0x8B, 0x46, 0x0A, 0xA3, 0x02, 0x10,  // [bp+0Ah] -> saw_second_word
    0xC4, 0x7E, 0x06,                    // les di, [bp+6]
    0x26, 0x8A, 0x05, 0x30, 0xE4,        // mov al, es:[di] / xor ah, ah
    0xA3, 0x04, 0x10,                    // -> saw_byte
    0xFF, 0x06, 0x06, 0x10,              // inc word [call_count]
    0x89, 0xEC, 0x5D};                   // mov sp, bp / pop bp
constexpr std::array<std::uint8_t, 3> routine_return{0xCA, 0x08, 0x00};

/// And one that never comes back: `jmp $`.
constexpr std::array<std::uint8_t, 2> spinning{0xEB, 0xFE};

// --- What the handlers do --------------------------------------------------
//
// Handler-local counters, because a `seam_handler` is a plain function
// pointer with nowhere to keep anything — the same reason `seam_test.cpp`
// keeps its own at namespace scope.

unsigned handler_runs = 0;
unsigned calls_queued = 0;
bool place_succeeded = false;
bool call_succeeded = false;
unsigned queue_how_many = 1;
bool spin_instead = false;

/// The handler under test: place a byte, queue `queue_how_many` calls
/// that name it, and return. It acts once — a second arrival would queue
/// a second batch for ever, since the machine comes back to the same
/// instruction.
void ask_the_program(machine& box, seam_context& ctx) {
  ++handler_runs;
  if (handler_runs > 1) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  static_cast<void>(box);

  std::uint16_t segment = 0;
  std::uint16_t offset = 0;
  const std::array<std::uint8_t, 1> bytes{placed_byte};
  place_succeeded = ctx.place_bytes(bytes, segment, offset);

  // Segment then offset, so the offset is the last word pushed and a
  // `les` at the routine's own frame finds a far pointer.
  const std::array<std::uint16_t, 4> words{first_word, second_word, segment,
                                           offset};
  calls_queued = 0;
  call_succeeded = true;
  for (unsigned nth = 0; nth < queue_how_many; ++nth) {
    call_succeeded =
        ctx.call_program(routine_segment,
                         spin_instead ? spinning_offset : routine_offset,
                         words) &&
        call_succeeded;
    if (call_succeeded) {
      ++calls_queued;
    }
  }
}

/// One that queues nothing at all, for the claim that the mechanism costs
/// a seam that does not use it exactly nothing.
void ask_nothing(machine& /*box*/, seam_context& ctx) {
  ++handler_runs;
  ctx.decline(seam_reason::point_not_recognized);
}

constexpr std::array<seam_point, 1> call_points{
    {{.module = resident_image, .offset = 0x0000, .run = &ask_the_program}}};
constexpr seam_definition call_seam{.id = "test-call",
                                    .about = "calls a routine of its own",
                                    .fingerprints = claimed_binaries,
                                    .points = call_points};

constexpr std::array<seam_point, 1> quiet_points{
    {{.module = resident_image, .offset = 0x0000, .run = &ask_nothing}}};
constexpr seam_definition quiet_seam{.id = "test-no-call",
                                     .about = "never calls anything",
                                     .fingerprints = claimed_binaries,
                                     .points = quiet_points};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    handler_runs = 0;
    calls_queued = 0;
    place_succeeded = false;
    call_succeeded = false;
    queue_how_many = 1;
    spin_instead = false;
    EXPECT_TRUE(box->seams().add(call_seam));
    EXPECT_TRUE(box->seams().add(quiet_seam));
    box->seams().loaded(claimed_digest(), image_load_segment);

    // The routine, and the one that never returns, in their own segment.
    for (std::size_t nth = 0; nth < routine.size(); ++nth) {
      put_byte(routine_segment, static_cast<std::uint16_t>(nth), routine[nth]);
    }
    for (std::size_t nth = 0; nth < routine_return.size(); ++nth) {
      put_byte(routine_segment,
               static_cast<std::uint16_t>(routine.size() + nth),
               routine_return[nth]);
    }
    for (std::size_t nth = 0; nth < spinning.size(); ++nth) {
      put_byte(routine_segment,
               static_cast<std::uint16_t>(spinning_offset + nth),
               spinning[nth]);
    }
    // `jmp $` where the point is, so that "the instruction the handler
    // was called at has still not run" is a thing a test can *look* at:
    // the machine resumes onto it and stays there. A HLT would have done
    // for counting arrivals and would have advanced IP past itself, which
    // is the program running and not the seam.
    put_byte(image_load_segment, 0, 0xEB);
    put_byte(image_load_segment, 1, 0xFE);
  }

  [[nodiscard]] std::uint8_t byte_at(std::uint16_t segment,
                                     std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }
  [[nodiscard]] std::uint16_t word_at(std::uint16_t segment,
                                      std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        byte_at(segment, offset) |
        (byte_at(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
  }
  void put_byte(std::uint16_t segment, std::uint16_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = value;
  }

  /// The machine standing on the point, with a stack and a data segment
  /// of its own.
  void stand_on_the_point() const {
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = 0;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = stack_pointer;
    r[cpu::reg16::bx] = 0xBBBB;  // something to notice being clobbered
  }

  /// Step until the batch is over, or `budget` steps have gone by.
  [[nodiscard]] unsigned run(unsigned budget) const {
    unsigned steps = 0;
    for (; steps < budget; ++steps) {
      box->step();
      if (steps > 0 && !batch_running()) {
        break;
      }
    }
    return steps;
  }

  /// Whether the machine is still somewhere inside a call — which is any
  /// CS but the one the point is in.
  [[nodiscard]] bool batch_running() const {
    return box->processor().regs()[cpu::sreg::cs] != image_load_segment;
  }

  [[nodiscard]] cpu::registers& regs() const { return box->processor().regs(); }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- The call itself -------------------------------------------------------

TEST(SeamCallProgram, RunsTheRoutineWithTheFrameItsOwnCallersBuild) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();

  static_cast<void>(r.run(200));

  EXPECT_TRUE(place_succeeded);
  EXPECT_TRUE(call_succeeded);
  EXPECT_EQ(r.word_at(data_segment, call_count), 1u);
  EXPECT_EQ(r.word_at(data_segment, saw_first_word), first_word)
      << "the first word queued is the deepest on the stack";
  EXPECT_EQ(r.word_at(data_segment, saw_second_word), second_word);
  EXPECT_EQ(r.word_at(data_segment, saw_byte), placed_byte)
      << "and the far pointer named the bytes the seam placed";
}

TEST(SeamCallProgram, PutsTheMachineBackExactlyAsItWas) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();
  const cpu::registers before = r.regs();

  static_cast<void>(r.run(200));

  const cpu::registers& after = r.regs();
  EXPECT_EQ(after[cpu::sreg::cs], before[cpu::sreg::cs]);
  EXPECT_EQ(after.ip, before.ip)
      << "the instruction the handler was called at has still not run";
  EXPECT_EQ(after[cpu::reg16::sp], before[cpu::reg16::sp])
      << "including the stack the bytes were placed on";
  EXPECT_EQ(after[cpu::sreg::ss], before[cpu::sreg::ss]);
  EXPECT_EQ(after[cpu::sreg::ds], before[cpu::sreg::ds]);
  EXPECT_EQ(after[cpu::reg16::bx], before[cpu::reg16::bx])
      << "a routine may clobber what a C routine may clobber";
  EXPECT_EQ(after[cpu::reg16::bp], before[cpu::reg16::bp]);
}

TEST(SeamCallProgram, RunsEveryQueuedCallInTheOrderItWasQueued) {
  const rig r;
  queue_how_many = 3;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();

  static_cast<void>(r.run(400));

  EXPECT_EQ(calls_queued, 3u);
  EXPECT_EQ(r.word_at(data_segment, call_count), 3u)
      << "one arrival, three calls, and the handler never re-entered";
  EXPECT_EQ(handler_runs, 1u);
}

TEST(SeamCallProgram, RefusesMoreCallsThanABatchHolds) {
  const rig r;
  queue_how_many = seam_engine::max_calls + 1;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();

  static_cast<void>(r.run(2000));

  EXPECT_FALSE(call_succeeded) << "the one that did not fit said so";
  EXPECT_EQ(calls_queued, seam_engine::max_calls);
  EXPECT_EQ(r.word_at(data_segment, call_count), seam_engine::max_calls)
      << "and the ones that did fit still ran";
}

TEST(SeamCallProgram, PlacesBytesWhereAnInterruptCannotReachThem) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();
  const std::uint16_t before = r.regs()[cpu::reg16::sp];

  // One step: the handler runs and the first call is set up. The bytes
  // are below the stack the program was using, and the frame is below
  // them, so anything the machine pushes from here lands lower still.
  r.box->step();

  EXPECT_LT(r.regs()[cpu::reg16::sp], before)
      << "the stack was lowered over the placed bytes and the frame";
}

// --- When it goes wrong ----------------------------------------------------

TEST(SeamCallProgram, AbandonsACallThatDoesNotComeBack) {
  const rig r;
  spin_instead = true;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();
  const cpu::registers before = r.regs();

  // Past the budget, and then some: the engine has to give up on its own.
  for (std::uint32_t step = 0; step < seam_engine::max_call_steps + 16;
       ++step) {
    r.box->step();
  }

  const cpu::registers& after = r.regs();
  EXPECT_EQ(after[cpu::sreg::cs], before[cpu::sreg::cs])
      << "the machine was put back rather than left in the routine";
  EXPECT_EQ(after.ip, before.ip);
  EXPECT_EQ(after[cpu::reg16::sp], before[cpu::reg16::sp]);

  bool said_so = false;
  for (const seam_event& event : r.log.seam_events) {
    if (event.id == "test-call" && event.kind == seam_event_kind::inert &&
        event.reason == seam_reason::call_did_not_return) {
      said_so = true;
    }
  }
  EXPECT_TRUE(said_so) << "and it said why, rather than waiting for ever";
}

TEST(SeamCallProgram, FinishesABatchEvenIfTheSeamIsSwitchedOffDuringIt) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-call"), seam_reason::none);
  r.stand_on_the_point();

  r.box->step();  // the handler ran and the call is under way
  ASSERT_TRUE(r.batch_running());
  ASSERT_EQ(r.box->seams().disable("test-call"), seam_reason::none);

  EXPECT_TRUE(r.box->seams().armed())
      << "you cannot un-call a call: the frame is on the machine's stack";
  static_cast<void>(r.run(200));

  EXPECT_EQ(r.word_at(data_segment, call_count), 1u);
  EXPECT_FALSE(r.box->seams().armed()) << "and then there is nothing left";
}

// --- What it costs a seam that does not use it -----------------------------

TEST(SeamCallProgram, CostsNothingToASeamThatQueuesNothing) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-no-call"), seam_reason::none);
  r.stand_on_the_point();
  const cpu::registers before = r.regs();

  r.box->step();

  EXPECT_EQ(handler_runs, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], before[cpu::reg16::sp])
      << "nothing was placed and no frame was built";
  EXPECT_EQ(r.word_at(data_segment, call_count), 0u);
}

TEST(SeamCallProgram, HasNoBatchWhenNoSeamIsOn) {
  const rig r;
  r.stand_on_the_point();

  r.box->step();

  EXPECT_FALSE(r.box->seams().armed());
  EXPECT_EQ(handler_runs, 0u);
}

}  // namespace
}  // namespace amberfolio::machine
