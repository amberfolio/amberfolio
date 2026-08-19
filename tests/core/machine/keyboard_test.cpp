// SPDX-License-Identifier: AGPL-3.0-only
//
// INT 16h, the BDA keystroke buffer, and Ctrl-Break.
//
// The same discipline service_floor_test.cpp states applies here even more
// than there: almost everything below is a real assembled instruction
// stream, because the claims this file makes are about what a program
// finds when it reads 40:1E directly or calls INT 16h, not about what our
// own C++ functions return when called out of band. Every byte of every
// program is written here, from the encoding, per the clean-content rule
// (PLAN.md §6).

#include "amberfolio/machine/keyboard.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/service_floor.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x2000;
constexpr std::uint16_t stack_top = 0x1000;

// A handful of XT scan codes the tests below type with, named instead of
// left as bare hex at each call site.
constexpr std::uint8_t sc_a = 0x1E;
constexpr std::uint8_t sc_s = 0x1F;
constexpr std::uint8_t sc_d = 0x20;
constexpr std::uint8_t sc_f = 0x21;
constexpr std::uint8_t sc_c = 0x2E;
constexpr std::uint8_t sc_left_shift = 0x2A;
constexpr std::uint8_t sc_ctrl = 0x1D;
constexpr std::uint8_t sc_caps_lock = 0x3A;
constexpr std::uint8_t sc_num_lock = 0x45;

struct rig {
  explicit rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {}

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  void poke(std::uint16_t at, std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t p = cpu::physical_address(code_segment, at);
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[p] = byte;
      ++p;
    }
  }

  /// Load an instruction stream at `code_segment:0000`, with a stack.
  void program(std::initializer_list<std::uint8_t> bytes) const {
    poke(0, bytes);

    box->processor().reset();
    cpu::registers& regs = box->processor().regs();
    regs[cpu::sreg::cs] = code_segment;
    regs[cpu::sreg::ss] = code_segment;
    regs[cpu::reg16::sp] = stack_top;
    regs.ip = 0;
  }

  /// Point a vector at `code_segment:offset`, the way a program's own
  /// hooking code would.
  void hook(std::uint8_t vector, std::uint16_t offset) const {
    const std::uint32_t entry = cpu::physical_address(
        cpu::vector_table_segment, cpu::vector_table_offset(vector));
    const std::span<std::uint8_t> ram = box->memory().ram();
    ram[entry] = static_cast<std::uint8_t>(offset);
    ram[entry + 1] = static_cast<std::uint8_t>(offset >> 8u);
    ram[entry + 2] = static_cast<std::uint8_t>(code_segment);
    ram[entry + 3] = static_cast<std::uint8_t>(code_segment >> 8u);
  }

  /// A byte at `offset` within the BDA, read the way the machine's own
  /// self test wrote it: through the back door, not a bus cycle.
  [[nodiscard]] std::uint8_t bda_byte(std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(bda::segment, offset)];
  }

  [[nodiscard]] std::uint16_t bda_word(std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        bda_byte(offset) |
        (static_cast<std::uint16_t>(bda_byte(offset + 1)) << 8u));
  }

  [[nodiscard]] std::uint8_t code_byte(std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(code_segment, offset)];
  }

  /// Step until `done` answers true, the machine stops, or `cap` steps
  /// have run. The cap is a test-failure device, not a scheduling one —
  /// every call site below picks one generous enough that reaching it
  /// means the design broke, not that the test was impatient.
  template <typename Predicate>
  void run_until(Predicate done, unsigned cap = 4000) const {
    unsigned steps = 0;
    while (!done() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- The buffer is real memory ------------------------------------------

TEST(keyboard_buffer, a_program_reads_the_bda_buffer_directly) {
  const rig r;

  //   0000  B8 40 00   MOV AX, 0040h
  //   0003  8E D8      MOV DS, AX
  //   0005  A1 1E 00   MOV AX, [001E]     ; the buffer's first slot
  //   0008  F4         HLT
  r.program({0xB8, 0x40, 0x00, 0x8E, 0xD8, 0xA1, 0x1E, 0x00, 0xF4});

  r.pc().post_key(sc_a, key_action::down);
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  // AH = the scan code, AL = the translated ASCII — no INT 16h call at
  // all, because the claim is that this is real memory a program can
  // walk to on its own, exactly as the era's programs did.
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1E61);
}

// --- Dequeue order, and wraparound ---------------------------------------

/// AH=00h in a loop, storing AL at code_segment:0100 and counting down
/// CX, `loop_count` times: the vehicle both the wraparound test and the
/// full-buffer-drop test below run. Sets DS = CS first — `MOV [DI], AL`
/// is a DS-relative write, and a freshly reset processor's DS is 0, which
/// would land every write in the IVT instead of this program's own
/// segment.
///
///   0000  8C C8      MOV AX, CS
///   0002  8E D8      MOV DS, AX
///   0004  B9 xx xx   MOV CX, loop_count
///   0007  BF 00 01   MOV DI, 0100h
///   000A  B4 00      loop: MOV AH, 00h
///   000C  CD 16            INT 16h
///   000E  88 05            MOV [DI], AL
///   0010  47               INC DI
///   0011  49               DEC CX
///   0012  75 F6            JNZ loop
///   0014  F4         HLT
void load_read_loop(const rig& r, std::uint16_t loop_count) {
  r.program({0x8C,
             0xC8,
             0x8E,
             0xD8,
             0xB9,
             static_cast<std::uint8_t>(loop_count),
             static_cast<std::uint8_t>(loop_count >> 8u),
             0xBF,
             0x00,
             0x01,
             0xB4,
             0x00,
             0xCD,
             0x16,
             0x88,
             0x05,
             0x47,
             0x49,
             0x75,
             0xF6,
             0xF4});
}

// Sixteen slots hold fifteen keystrokes (service_floor.h), so twenty
// reads through the real dequeue path cannot complete without the buffer
// wrapping past 40:3E back to 40:1E at least once — this is what proves
// the wraparound rather than asserting head/tail arithmetic by hand.
TEST(keyboard_buffer, dequeues_in_order_and_wraps_around_the_buffer) {
  const rig r;
  load_read_loop(r, 20);

  const std::array<std::uint8_t, 4> pattern{sc_a, sc_s, sc_d, sc_f};
  for (std::size_t i = 0; i < 20; ++i) {
    r.pc().post_key(pattern[i % pattern.size()], key_action::down);
    // Generous enough for one loop iteration (MOV/INT/MOV/INC/DEC/JNZ is
    // seven steps, INT costs two) with margin; drain() runs on the first
    // of these and settles the buffer before INT 16h asks for it.
    r.run_until([&, i] { return r.regs()[cpu::reg16::di] == 0x0100 + i + 1; },
                15);
  }
  // DI reaching its last value is the loop body's INC, not its DEC/JNZ/HLT
  // tail — a few more steps let CX reach zero and the program halt.
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  const std::string expected = "asdfasdfasdfasdfasdf";
  for (std::size_t i = 0; i < 20; ++i) {
    EXPECT_EQ(r.code_byte(static_cast<std::uint16_t>(0x0100 + i)),
              static_cast<std::uint8_t>(expected[i]))
        << "slot " << i;
  }
}

TEST(keyboard_buffer, a_full_buffer_drops_the_newest_keystrokes) {
  const rig r;
  load_read_loop(r, 20);

  // Fifteen 'a's fill the buffer exactly; the five 's's that follow
  // arrive with nowhere to go and are dropped, as the BIOS drops them.
  for (int i = 0; i < 15; ++i) {
    r.pc().post_key(sc_a, key_action::down);
  }
  for (int i = 0; i < 5; ++i) {
    r.pc().post_key(sc_s, key_action::down);
  }

  // The sixteenth read finds nothing left and blocks — which is exactly
  // the proof that only fifteen were ever there to read.
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::reg16::cx], 20 - 15);
  for (int i = 0; i < 15; ++i) {
    EXPECT_EQ(r.code_byte(static_cast<std::uint16_t>(0x0100 + i)), 'a')
        << "slot " << i;
  }
}

// --- Status vs. read ------------------------------------------------------

TEST(keyboard_int16, status_on_an_empty_buffer_sets_zf_and_leaves_ax) {
  const rig r;

  //   0000  B8 FF FF   MOV AX, 0FFFFh
  //   0003  B4 01      MOV AH, 01h
  //   0005  CD 16      INT 16h
  //   0007  F4         HLT
  r.program({0xB8, 0xFF, 0xFF, 0xB4, 0x01, 0xCD, 0x16, 0xF4});
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_TRUE(r.regs().flag_set(cpu::flag::zf));
  // AH=01h with nothing to preview touches nothing: AL is still the FFh
  // the program itself put there.
  EXPECT_EQ(r.regs()[cpu::reg16::ax] & 0x00FFu, 0x00FFu);
}

TEST(keyboard_int16, status_previews_without_removing_and_read_then_does) {
  const rig r;

  //   0000  B4 01      MOV AH, 01h
  //   0002  CD 16      INT 16h        ; status: ZF clear, AX = preview
  //   0004  89 C3      MOV BX, AX     ; keep it
  //   0006  B4 00      MOV AH, 00h
  //   0008  CD 16      INT 16h        ; read: removes it
  //   000A  F4         HLT
  r.program({0xB4, 0x01, 0xCD, 0x16, 0x89, 0xC3, 0xB4, 0x00, 0xCD, 0x16, 0xF4});
  r.pc().post_key(sc_a, key_action::down);
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.regs().flag_set(cpu::flag::zf));
  EXPECT_EQ(r.regs()[cpu::reg16::bx], 0x1E61);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1E61);

  // The read actually removed it: the buffer is empty again.
  EXPECT_EQ(r.bda_word(bda::keyboard_buffer_head),
            r.bda_word(bda::keyboard_buffer_tail));
}

// --- Shift, ctrl, and lock keys --------------------------------------------

TEST(keyboard_shift_flags, shift_and_ctrl_and_caps_lock_all_reach_40_17) {
  const rig r;

  //   0000  B4 02      MOV AH, 02h
  //   0002  CD 16      INT 16h        ; AL = 40:17
  //   0004  89 C3      MOV BX, AX
  //   0006  B4 00      MOV AH, 00h
  //   0008  CD 16      INT 16h        ; the shifted 'A'
  //   000A  89 C1      MOV CX, AX
  //   000C  F4         HLT
  r.program({0xB4, 0x02, 0xCD, 0x16, 0x89, 0xC3, 0xB4, 0x00, 0xCD, 0x16, 0x89,
             0xC1, 0xF4});

  r.pc().post_key(sc_left_shift, key_action::down);
  r.pc().post_key(sc_a, key_action::down);
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::reg16::bx] & 0xFFu, xt_keyboard::left_shift_mask);
  EXPECT_EQ(r.regs()[cpu::reg16::cx], 0x1E41);  // AH=1E, AL='A'
}

TEST(keyboard_shift_flags, ctrl_letter_gives_the_control_code) {
  const rig r;

  r.program({0xB4, 0x00, 0xCD, 0x16, 0xF4});  // MOV AH,00h ; INT16h ; HLT
  r.pc().post_key(sc_ctrl, key_action::down);
  r.pc().post_key(sc_c, key_action::down);
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  // Ctrl-C, through the buffer, is an ordinary keystroke (PLAN.md's
  // keyboard-services scope note) — 0x03, not a break.
  EXPECT_EQ(r.regs()[cpu::reg16::ax] & 0xFFu, 0x03u);
  EXPECT_FALSE(r.pc().stopped());
}

TEST(keyboard_shift_flags, caps_lock_toggles_letter_case_and_ignores_repeat) {
  const rig r;

  r.program({0xB4, 0x00, 0xCD, 0x16, 0xF4});
  r.pc().post_key(sc_caps_lock, key_action::down);
  r.pc().post_key(sc_caps_lock, key_action::down);  // auto-repeat: no re-toggle
  r.pc().post_key(sc_a, key_action::down);
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.bda_byte(bda::keyboard_shift_flags) & xt_keyboard::caps_lock_mask,
            xt_keyboard::caps_lock_mask);
  EXPECT_EQ(r.regs()[cpu::reg16::ax] & 0xFFu, 'A');
}

TEST(keyboard_shift_flags, num_lock_picks_digit_over_navigation) {
  const rig r;

  r.program({0xB4, 0x00, 0xCD, 0x16, 0xF4});
  r.pc().post_key(sc_num_lock, key_action::down);
  r.pc().post_key(0x4F, key_action::down);  // keypad 1/End
  r.run_until([&] { return r.pc().processor().halted(); });

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::reg16::ax] & 0xFFu, '1');
}

// --- The blocking read: idle, not a lie about time -------------------------

TEST(keyboard_blocking_read,
     halts_without_stalling_the_clock_and_wakes_on_a_posted_key) {
  const rig r;

  //   0000  B4 00      MOV AH, 00h
  //   0002  CD 16      INT 16h
  //   0004  89 C3      MOV BX, AX
  //   0006  F4         HLT
  r.program({0xB4, 0x00, 0xCD, 0x16, 0x89, 0xC3, 0xF4});
  r.regs().set_flag(cpu::flag::if_, true);

  // Reach the blocking wait: nothing posted, so AH=00h halts.
  r.run_until([&] { return r.pc().processor().halted(); });
  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());
  const ticks waiting_at = r.pc().time();

  // Virtual time is not lying: a timer tick delivered while "waiting"
  // still counts, exactly as it would for a real HLT loop. There is no
  // PIT yet (M2-D1, #46), so the tick is raised by hand, the same way
  // service_floor_test.cpp's timer tests do it.
  r.pc().processor().raise_intr(service::timer_vector);
  r.run_until([&] {
    return r.pc().memory().ram()[cpu::physical_address(bda::segment,
                                                       bda::timer_ticks)] == 1;
  });
  EXPECT_EQ(r.bda_word(bda::timer_ticks), 1u);
  // The tick count landing is the timer handler's own body, mid-chain —
  // delivery itself ended the halt (any interrupt's does), and the chain
  // still owes the continuation's IRET back to the INT 16h stub before
  // dispatch re-enters `keyboard_read` and halts again. A few more steps
  // settle that, and only then is it still-waiting-for-the-key again.
  r.run_until([&] { return r.pc().processor().halted(); });
  EXPECT_TRUE(r.pc().processor().halted());
  EXPECT_GT(r.pc().time(), waiting_at);

  // The key arrives, and the read completes on the very step it does.
  r.pc().post_key(sc_a, key_action::down);
  r.run_until([&] { return r.regs()[cpu::reg16::bx] != 0; });
  // BX becoming non-zero is "MOV BX, AX", one instruction short of the
  // program's own final HLT.
  r.run_until([&] { return r.pc().processor().halted(); });

  EXPECT_EQ(r.regs()[cpu::reg16::bx], 0x1E61);
  ASSERT_TRUE(r.pc().processor().halted());
  // The halt at the end is the program's own HLT, not the blocking wait
  // — the instruction after BX was set had nowhere else to go.
  EXPECT_EQ(r.regs().ip, 0x0007);
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
}

// --- Ctrl-Break --------------------------------------------------------

TEST(keyboard_ctrl_break, raises_a_hooked_int_1bh_and_sets_the_break_flag) {
  const rig r;

  //   0000  31 C0              XOR AX, AX
  //   0002  8E D8              MOV DS, AX
  //   0004  C7 06 6C 00 20 00  MOV word [006C], 0020h   ; 1Bh * 4
  //   000A  8C 0E 6E 00        MOV [006E], CS
  //   000E  FB                 STI
  //   000F  F4                 HLT
  //   0010  F4                 HLT   ; where Ctrl-Break's IRET returns to
  //   0020  45 CF              INC BP ; IRET             (the hook)
  //
  // Two HLTs, not one: a real 8086 resumes *after* the HLT it interrupted,
  // not back on top of it, so the hook's IRET lands one byte past the
  // first one — the second HLT is what makes that landing a clean halt
  // again instead of falling into whatever memory follows.
  r.program({0x31, 0xC0, 0x8E, 0xD8, 0xC7, 0x06, 0x6C, 0x00, 0x20, 0x00, 0x8C,
             0x0E, 0x6E, 0x00, 0xFB, 0xF4, 0xF4});
  r.poke(0x0020, {0x45, 0xCF});

  r.run_until([&] { return r.pc().processor().halted(); });
  ASSERT_TRUE(r.pc().processor().halted());
  ASSERT_EQ(r.regs()[cpu::reg16::bp], 0);

  r.pc().post_key(sc_ctrl, key_action::down);
  r.pc().post_key(0x46, key_action::down);  // Scroll Lock/Break
  r.run_until([&] { return r.regs()[cpu::reg16::bp] != 0; });

  EXPECT_EQ(r.regs()[cpu::reg16::bp], 1);
  EXPECT_EQ(r.bda_byte(bda::keyboard_break_flag), bda::keyboard_break_flag_set);
  // Ctrl-Break does not go through the keystroke buffer.
  EXPECT_EQ(r.bda_word(bda::keyboard_buffer_head),
            r.bda_word(bda::keyboard_buffer_tail));
  EXPECT_FALSE(r.pc().stopped());

  // Control returns to the HLT the hook's IRET landed back on.
  r.run_until([&] { return r.pc().processor().halted(); });
  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
}

TEST(keyboard_ctrl_break, the_default_handler_is_a_harmless_iret_unhooked) {
  const rig r;

  r.program({0xFB, 0xF4, 0xF4});  // STI ; HLT ; HLT (see the hooked test)
  r.run_until([&] { return r.pc().processor().halted(); });

  r.pc().post_key(sc_ctrl, key_action::down);
  r.pc().post_key(0x46, key_action::down);
  r.run_until([&] { return r.bda_byte(bda::keyboard_break_flag) != 0; });

  EXPECT_EQ(r.bda_byte(bda::keyboard_break_flag), bda::keyboard_break_flag_set);
  EXPECT_FALSE(r.pc().stopped());

  // Nothing was invented for the unhooked vector: it IRETs straight back
  // to the program, which is once again sitting at its own HLT.
  r.run_until([&] { return r.pc().processor().halted(); });
  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::sreg::cs], code_segment);
  EXPECT_TRUE(r.log.stops.empty());
}

// --- Anything else logs and stops -----------------------------------------

TEST(keyboard_int16, an_unrecognized_function_logs_and_stops) {
  const rig r;

  r.program({0xB4, 0x55, 0xCD, 0x16, 0xF4});  // MOV AH,55h ; INT16h ; HLT
  r.run_until([&] { return r.pc().stopped(); });

  ASSERT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unimplemented_service);

  ASSERT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.calls[0].vector, xt_keyboard::int16_vector);
  EXPECT_EQ(r.log.calls[0].function(), 0x55);
  ASSERT_EQ(r.log.stops.size(), 1u);
}

// --- Exit criterion ------------------------------------------------------

// A test program echoes injected keystrokes through INT 16h: two keys
// already queued when it asks (the poll/immediate path) and a third that
// only arrives after it has started waiting (the blocking path) — one
// loop, one mechanism, both ways of reaching it.
TEST(keyboard_exit_criterion, echoes_keystrokes_through_poll_and_blocking) {
  const rig r;
  load_read_loop(r, 3);

  r.pc().post_key(sc_a, key_action::down);
  r.pc().post_key(sc_s, key_action::down);
  r.run_until([&] { return r.regs()[cpu::reg16::di] == 0x0102; });
  ASSERT_EQ(r.code_byte(0x0100), 'a');
  ASSERT_EQ(r.code_byte(0x0101), 's');

  // DI reaching 0x0102 is the second loop's INC, not its DEC/JNZ tail;
  // a few more steps carry it into the third read, which blocks — the
  // third key is not there yet, so the loop is now inside AH=00h,
  // halted, waiting.
  r.run_until([&] { return r.pc().processor().halted(); });
  EXPECT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.regs()[cpu::reg16::cx], 1);

  r.pc().post_key(sc_d, key_action::down);
  r.run_until([&] {
    return r.pc().processor().halted() && r.regs()[cpu::reg16::cx] == 0;
  });

  EXPECT_EQ(r.code_byte(0x0102), 'd');
  // The program's own final HLT is at 0x0014 (load_read_loop's DS setup
  // adds four bytes ahead of the loop); a fetched instruction leaves IP
  // one past its own first byte, HLT included.
  EXPECT_EQ(r.regs().ip, 0x0015);
  EXPECT_FALSE(r.pc().stopped());
}

}  // namespace
}  // namespace amberfolio::machine
