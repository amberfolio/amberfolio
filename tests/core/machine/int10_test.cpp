// SPDX-License-Identifier: AGPL-3.0-only
//
// INT 10h: what AH=00h AL=0Dh leaves programmed, what a program asking
// for anything else gets, and the mode-discipline notice a write into the
// video window trips before AH=00h has run.
//
// Every program below is written here from the encoding, the same
// discipline service_floor_test.cpp follows (PLAN.md §6).

#include "amberfolio/machine/int10.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t code_segment = 0x3000;
constexpr std::uint16_t stack_top = 0x1000;

constexpr std::uint16_t sequencer_index_port = 0x3C4;
constexpr std::uint16_t sequencer_data_port = 0x3C5;
constexpr std::uint16_t graphics_index_port = 0x3CE;
constexpr std::uint16_t graphics_data_port = 0x3CF;

/// A machine with an EGA attached and the video BIOS installed — every
/// test here wants both.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        video(std::make_unique<ega>()) {
    box->attach(*video);
    install_int10(box->services());
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// INT `service::video_vector` ; HLT — every test's whole program, with
  /// AX and BX set beforehand to name the call.
  void call_int10() const {
    std::uint32_t at = cpu::physical_address(code_segment, 0);
    for (const std::uint8_t byte :
         {std::uint8_t{0xCD}, service::video_vector, std::uint8_t{0xF4}}) {
      box->memory().ram()[at] = byte;
      ++at;
    }

    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = code_segment;
    r[cpu::sreg::ss] = code_segment;
    r[cpu::reg16::sp] = stack_top;
    r.ip = 0;
  }

  /// Step until the program halts or the machine stops.
  void run(unsigned cap = 2000) const {
    unsigned steps = 0;
    while (!box->processor().halted() && !box->stopped() && steps < cap) {
      box->step();
      ++steps;
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
};

// --- AH=00h: mode set ----------------------------------------------------

TEST(int10_mode_set, al_0dh_programs_the_documented_register_state) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_FALSE(r.pc().stopped());
  EXPECT_TRUE(r.pc().video_mode_set());

  // The one load-bearing value: every plane reachable after mode-set.
  r.video->write_port(sequencer_index_port, 0x02);
  EXPECT_EQ(r.video->read_port(sequencer_data_port), 0x0F);

  // The rest of the documented table — inert on this device (ega.h,
  // int10.cpp), but still what mode-set is supposed to leave behind.
  r.video->write_port(graphics_index_port, 0x05);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0x00);
  r.video->write_port(graphics_index_port, 0x06);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0x03);
  r.video->write_port(graphics_index_port, 0x08);
  EXPECT_EQ(r.video->read_port(graphics_data_port), 0xFF);

  // The standard 16-colour default palette (int10.h's top comment).
  constexpr std::array<std::uint8_t, 16> expected{
      0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};
  for (unsigned i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(r.video->palette_register(i), expected[i]) << "register " << i;
  }
  EXPECT_EQ(r.video->overscan_register(), 0x00);
}

TEST(int10_mode_set, any_other_mode_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0013;  // AH=00h AL=13h: a VGA mode.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0013u);
  EXPECT_FALSE(r.pc().video_mode_set());

  ASSERT_EQ(r.log.stops.size(), 1u);
  EXPECT_EQ(r.log.stops[0], r.pc().stop());
}

// --- AH=10h: palette register set -----------------------------------------

TEST(int10_palette, al_00h_sets_one_palette_register) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1000;
  r.regs()[cpu::reg16::bx] = 0x2A05;  // BH = 2Ah colour, BL = register 5.
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.video->palette_register(5), 0x2A);
}

TEST(int10_palette, al_01h_sets_the_overscan_colour) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1001;
  r.regs()[cpu::reg16::bx] = 0x3F00;  // BH = 3Fh colour.
  r.run();

  ASSERT_TRUE(r.pc().processor().halted());
  EXPECT_EQ(r.video->overscan_register(), 0x3F);
}

TEST(int10_palette, any_other_al_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x1099;
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x1099u);
}

// --- Anything but AH=00h/10h -----------------------------------------------

TEST(int10_dispatch, any_other_function_is_a_loud_log_and_a_clean_stop) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x0E41;  // AH=0Eh: teletype output, not ours.
  r.run();

  EXPECT_TRUE(r.pc().stopped());
  EXPECT_EQ(r.pc().stop().reason, stop_reason::unsupported_request);
  EXPECT_EQ(r.pc().stop().at, 0x0E41u);

  // The vector was reached and did have a handler — this is the
  // handler's own refusal, not a missing vector.
  ASSERT_EQ(r.log.calls.size(), 1u);
  EXPECT_EQ(r.log.calls[0].outcome, service_outcome::handled);
}

// --- Mode discipline -------------------------------------------------------

TEST(int10_mode_discipline, a_vram_write_before_mode_set_is_noticed) {
  rig r;

  r.pc().write_memory(0xA0000, 0x11);

  ASSERT_EQ(r.log.notices.size(), 1u);
  EXPECT_EQ(r.log.notices[0].what, notice_kind::video_write_before_mode_set);
  EXPECT_EQ(r.log.notices[0].at, 0xA0000u);
  EXPECT_EQ(r.log.notices[0].value, 0x11);
}

TEST(int10_mode_discipline, no_notice_once_a_mode_has_been_set) {
  rig r;
  r.call_int10();
  r.regs()[cpu::reg16::ax] = 0x000D;
  r.run();
  ASSERT_TRUE(r.pc().video_mode_set());
  r.log.notices.clear();

  r.pc().write_memory(0xA0000, 0x11);

  EXPECT_TRUE(r.log.notices.empty());
}

}  // namespace
}  // namespace amberfolio::machine
