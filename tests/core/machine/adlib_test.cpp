// SPDX-License-Identifier: AGPL-3.0-only
//
// adlib.h: the AdLib card at 388h and 389h (#616). The address latch and
// the register file, the status byte and its mark, the two timers
// computed from virtual time (which is how a program finds the card),
// what is refused, what is published, and what a reset undoes.

#include "amberfolio/machine/adlib.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/opl2.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

/// A machine with the card in it, and a processor turning in a two-byte
/// loop so that virtual time can be made to pass. Everything goes
/// through the bus, as a program's `in` and `out` do.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)), card(*box) {
    box->attach(card);
    box->reset();
    spin();
  }

  /// `jmp $` at 2000:0000, and the processor pointed at it.
  void spin() const {
    constexpr std::uint16_t segment = 0x2000;
    const std::uint32_t at = cpu::physical_address(segment, 0);
    box->memory().ram()[at] = 0xEB;
    box->memory().ram()[at + 1] = 0xFE;
    cpu::registers& regs = box->processor().regs();
    regs[cpu::sreg::cs] = segment;
    regs.ip = 0;
  }

  void wait(ticks span) const { box->run(box->time() + span); }

  void write(std::uint8_t reg, std::uint8_t value) const {
    box->write_port8(adlib_address_port, reg);
    box->write_port8(adlib_data_port, value);
  }

  [[nodiscard]] std::uint8_t status() const {
    return box->read_port8(adlib_address_port);
  }

  /// The three bits a program looks at.
  [[nodiscard]] std::uint8_t flags() const {
    return static_cast<std::uint8_t>(status() & 0xE0U);
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  adlib_card card;
};

constexpr std::uint8_t timer_1_flags = adlib_status_any | adlib_status_timer_1;
constexpr std::uint8_t timer_2_flags = adlib_status_any | adlib_status_timer_2;

// --- The status, and finding the card --------------------------------------

TEST(AdlibCard, TheStatusCarriesTheChipsMarkAndNothingElseAtPowerOn) {
  const rig r;
  EXPECT_EQ(r.status(), adlib_status_mark);
  // The data port reads as nothing: the chip has no register to give back.
  EXPECT_EQ(r.box->read_port8(adlib_data_port), 0xFF);
  EXPECT_FALSE(r.box->stopped());
}

TEST(AdlibCard, TheDetectionEveryProgramMakesFindsIt) {
  const rig r;
  // Mask both timers and clear the flags: nothing set.
  r.write(opl2_reg::timer_control, 0x60);
  r.write(opl2_reg::timer_control, 0x80);
  EXPECT_EQ(r.flags(), 0x00);

  // Timer 1 from FFh, started with timer 2 masked: one count, 80
  // microseconds, 96 ticks.
  r.write(opl2_reg::timer_1, 0xFF);
  r.write(opl2_reg::timer_control, 0x21);
  r.wait(adlib_timer_1_period - 8);
  EXPECT_EQ(r.flags(), 0x00);
  r.wait(16);
  EXPECT_EQ(r.flags(), timer_1_flags);

  // And the program puts it back.
  r.write(opl2_reg::timer_control, 0x60);
  r.write(opl2_reg::timer_control, 0x80);
  EXPECT_EQ(r.flags(), 0x00);
  r.wait(10 * adlib_timer_1_period);
  EXPECT_EQ(r.flags(), 0x00);
}

// --- The timers ------------------------------------------------------------

TEST(AdlibCard, ATimerCountsUpFromItsValueAtItsOwnPace) {
  const rig r;
  // Timer 1 from 200: 56 counts of 96 ticks.
  r.write(opl2_reg::timer_1, 200);
  r.write(opl2_reg::timer_control, adlib_start_timer_1);
  r.wait(56 * adlib_timer_1_period - 8);
  EXPECT_EQ(r.flags(), 0x00);
  r.wait(16);
  EXPECT_EQ(r.flags(), timer_1_flags);
}

TEST(AdlibCard, TimerTwoIsFourTimesSlower) {
  const rig r;
  r.write(opl2_reg::timer_2, 0xFF);
  r.write(opl2_reg::timer_control, adlib_start_timer_2);
  r.wait(adlib_timer_1_period * 2);
  EXPECT_EQ(r.flags(), 0x00);
  r.wait(adlib_timer_2_period);
  EXPECT_EQ(r.flags(), timer_2_flags);
}

TEST(AdlibCard, AFlagStaysUntilItIsClearedAndTheTimerRunsOn) {
  const rig r;
  r.write(opl2_reg::timer_1, 0xFF);
  r.write(opl2_reg::timer_control, adlib_start_timer_1);
  r.wait(2 * adlib_timer_1_period);
  EXPECT_EQ(r.flags(), timer_1_flags);
  EXPECT_EQ(r.flags(), timer_1_flags);  // Reading it clears nothing.

  // Cleared, and the next count sets it again: the clear did not stop
  // the timer, and did nothing else with the byte it came in.
  r.write(opl2_reg::timer_control, 0x80 | adlib_mask_timer_1);
  EXPECT_EQ(r.flags(), 0x00);
  r.wait(2 * adlib_timer_1_period);
  EXPECT_EQ(r.flags(), timer_1_flags);
}

TEST(AdlibCard, AMaskedTimerRunsAndRaisesNothing) {
  const rig r;
  r.write(opl2_reg::timer_1, 0xFF);
  r.write(opl2_reg::timer_2, 0xFF);
  r.write(opl2_reg::timer_control,
          adlib_start_timer_1 | adlib_start_timer_2 | adlib_mask_timer_1);
  r.wait(4 * adlib_timer_2_period);
  EXPECT_EQ(r.flags(), timer_2_flags);
}

TEST(AdlibCard, AStoppedTimerRaisesNothing) {
  const rig r;
  r.write(opl2_reg::timer_1, 0xFF);
  r.write(opl2_reg::timer_control, adlib_start_timer_1);
  r.write(opl2_reg::timer_control, 0x00);
  r.wait(10 * adlib_timer_1_period);
  EXPECT_EQ(r.flags(), 0x00);
}

TEST(AdlibCard, ALongWaitIsSettledAtOnce) {
  // Ten seconds and a hundred thousand counts later the flag is up and
  // the timer is still on its own beat: the next count is at the next
  // multiple of its period.
  const rig r;
  r.write(opl2_reg::timer_1, 0xFF);
  r.write(opl2_reg::timer_control, adlib_start_timer_1);
  const ticks started = r.box->time();
  r.wait(ticks{10} * pit_input_hz);
  EXPECT_EQ(r.flags(), timer_1_flags);
  r.write(opl2_reg::timer_control, 0x80);

  const ticks since = r.box->time() - started;
  const ticks to_next = adlib_timer_1_period - since % adlib_timer_1_period;
  if (to_next > 16) {
    r.wait(to_next - 8);
    EXPECT_EQ(r.flags(), 0x00);
  }
  r.wait(16);
  EXPECT_EQ(r.flags(), timer_1_flags);
}

// --- The registers -----------------------------------------------------------

TEST(AdlibCard, ADataWriteGoesToTheLatchedAddressAndOntoTheTimeline) {
  const rig r;
  r.write(0x20, 0x21);
  r.write(0xB0, 0x31);
  EXPECT_EQ(r.card.reg(0x20), 0x21);
  EXPECT_EQ(r.card.reg(0xB0), 0x31);
  EXPECT_EQ(r.card.address(), 0xB0);
  EXPECT_EQ(r.box->audio().fm_published(), 2u);

  // A second data write with no new address goes to the same register.
  r.box->write_port8(adlib_data_port, 0x11);
  EXPECT_EQ(r.card.reg(0xB0), 0x11);
  EXPECT_EQ(r.box->audio().fm_published(), 3u);
}

TEST(AdlibCard, TheTimersAreNotTheSynthesizers) {
  const rig r;
  r.write(opl2_reg::timer_1, 0x80);
  r.write(opl2_reg::timer_2, 0x80);
  r.write(opl2_reg::timer_control, 0x03);
  EXPECT_EQ(r.box->audio().fm_published(), 0u);
  EXPECT_EQ(r.card.reg(opl2_reg::timer_1), 0x80);
}

TEST(AdlibCard, AWriteToAnAddressThatIsNotARegisterIsDropped) {
  const rig r;
  for (const std::uint8_t address :
       {std::uint8_t{0x00}, std::uint8_t{0x05}, std::uint8_t{0x26},
        std::uint8_t{0x36}, std::uint8_t{0xA9}, std::uint8_t{0xC9},
        std::uint8_t{0xF6}, std::uint8_t{0xFF}}) {
    r.write(address, 0x5A);
    EXPECT_EQ(r.card.reg(address), 0x00) << int{address};
  }
  EXPECT_EQ(r.box->audio().fm_published(), 0u);
  EXPECT_FALSE(r.box->stopped());
}

// --- What is refused ---------------------------------------------------------

TEST(AdlibCard, ATestBitIsALoudStop) {
  rig r;
  // The waveform enable is the one bit of register 01 that is modelled.
  r.write(opl2_reg::test, opl2_reg::waveform_enable);
  r.wait(8);
  EXPECT_FALSE(r.box->stopped());

  r.write(opl2_reg::test, 0x01);
  r.wait(8);
  ASSERT_TRUE(r.box->stopped());
  EXPECT_EQ(r.box->stop().reason, stop_reason::unimplemented_device);
  ASSERT_EQ(r.log.device_stops.size(), 1u);
  EXPECT_EQ(r.log.device_stops[0].at, adlib_data_port);
  EXPECT_EQ(r.log.device_stops[0].detail, 0x01);
  // Nothing was kept and nothing was published.
  EXPECT_EQ(r.card.reg(opl2_reg::test), opl2_reg::waveform_enable);
  EXPECT_EQ(r.box->audio().fm_published(), 1u);
}

TEST(AdlibCard, TheCompositeSineModeIsALoudStop) {
  rig r;
  r.write(opl2_reg::keyboard_split, opl2_reg::note_select);
  r.wait(8);
  EXPECT_FALSE(r.box->stopped());

  r.write(opl2_reg::keyboard_split, opl2_reg::composite_sine);
  r.wait(8);
  ASSERT_TRUE(r.box->stopped());
  EXPECT_EQ(r.box->stop().reason, stop_reason::unimplemented_device);
}

// --- State, and the reset line -------------------------------------------------

/// The hash of the devices' section, which on this rig is the card's.
[[nodiscard]] sha256_digest devices_of(const machine& box) {
  return hash_state(box)
      .sections[static_cast<std::size_t>(state_section::devices)];
}

TEST(AdlibCard, WhatItWasToldIsInTheStateAndAResetUndoesIt) {
  rig r;
  const sha256_digest fresh = devices_of(*r.box);

  r.write(0x43, 0x10);
  const sha256_digest written = devices_of(*r.box);
  EXPECT_NE(written, fresh);

  // A running timer is state too: where it will run out.
  r.write(opl2_reg::timer_1, 0xF0);
  r.write(opl2_reg::timer_control, adlib_start_timer_1);
  EXPECT_NE(devices_of(*r.box), written);

  // And the write is in the audio section, counted and digested.
  const auto audio_of = [](const machine& box) {
    return hash_state(box)
        .sections[static_cast<std::size_t>(state_section::audio)];
  };
  const sha256_digest heard = audio_of(*r.box);
  r.write(0x43, 0x11);
  EXPECT_NE(audio_of(*r.box), heard);

  r.box->reset();
  r.spin();
  EXPECT_EQ(r.card.reg(0x43), 0x00);
  EXPECT_EQ(r.card.address(), 0x00);
  EXPECT_EQ(r.status(), adlib_status_mark);
  EXPECT_EQ(devices_of(*r.box), fresh);
  r.wait(40 * adlib_timer_1_period);
  EXPECT_EQ(r.flags(), 0x00);
}

}  // namespace
}  // namespace amberfolio::machine
