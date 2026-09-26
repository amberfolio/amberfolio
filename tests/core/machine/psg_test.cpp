// SPDX-License-Identifier: AGPL-3.0-only
//
// The Tandy sound chip (psg.h, tandy_sound.h, #404): what a written byte
// means, what the chip sounds like, and that the device puts its writes
// on the timeline the consumer hears them from.

#include "amberfolio/machine/psg.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/tandy_sound.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

using ::testing::Each;
using ::testing::FloatEq;

constexpr unsigned rate = 48000;

/// The voice's full level, as the double the chip's sums are in.
constexpr double full = static_cast<double>(psg_voice_amplitude);

/// Latch bytes, spelled the way the chip's protocol builds them.
constexpr std::uint8_t tone_low(unsigned voice, unsigned low) {
  return static_cast<std::uint8_t>(0x80 | (voice << 5) | (low & 0x0F));
}
constexpr std::uint8_t tone_high(unsigned high) {
  return static_cast<std::uint8_t>(high & 0x3F);
}
constexpr std::uint8_t volume(unsigned voice, unsigned attenuation) {
  return static_cast<std::uint8_t>(0x90 | (voice << 5) | (attenuation & 0x0F));
}
constexpr std::uint8_t noise(unsigned control) {
  return static_cast<std::uint8_t>(0xE0 | (control & 0x07));
}

// --- What a byte means -------------------------------------------------

TEST(PsgRegisters, PowerOnIsEveryVoiceSilent) {
  const psg_registers regs = psg_registers::powered_on();
  EXPECT_THAT(regs.attenuation, Each(psg_silent));
}

TEST(PsgRegisters, ALatchAndADataByteMakeATenBitPeriod) {
  psg_registers regs = psg_registers::powered_on();
  EXPECT_FALSE(regs.write(tone_low(1, 0x5)));
  EXPECT_FALSE(regs.write(tone_high(0x2A)));
  EXPECT_EQ(regs.period[1], 0x2A5);

  // A fresh latch changes the low four bits and keeps the high six.
  EXPECT_FALSE(regs.write(tone_low(1, 0xC)));
  EXPECT_EQ(regs.period[1], 0x2AC);
}

TEST(PsgRegisters, AnAttenuationLatchSetsOnlyThatVoice) {
  psg_registers regs = psg_registers::powered_on();
  EXPECT_FALSE(regs.write(volume(2, 3)));
  EXPECT_EQ(regs.attenuation[2], 3);
  EXPECT_EQ(regs.attenuation[0], psg_silent);

  // The NCR part ignores a data byte after an attenuation latch (#407).
  EXPECT_FALSE(regs.write(0x07));
  EXPECT_EQ(regs.attenuation[2], 3);
}

TEST(PsgRegisters, OnlyAChangeOfNoiseModeReseeds) {
  psg_registers regs = psg_registers::powered_on();
  // Power-on is periodic, so a periodic latch changes nothing.
  EXPECT_FALSE(regs.write(noise(1)));
  EXPECT_EQ(regs.noise, 1);
  EXPECT_TRUE(regs.write(noise(6)));
  EXPECT_EQ(regs.noise, 6);
  // The same mode again, even at another rate: the register runs on.
  EXPECT_FALSE(regs.write(noise(6)));
  EXPECT_FALSE(regs.write(noise(5)));
  EXPECT_EQ(regs.noise, 5);
  EXPECT_TRUE(regs.write(noise(2)));
}

TEST(PsgRegisters, ADataByteAfterANoiseLatchIsIgnored) {
  psg_registers regs = psg_registers::powered_on();
  EXPECT_TRUE(regs.write(noise(6)));
  EXPECT_FALSE(regs.write(0x03));
  EXPECT_EQ(regs.noise, 6);
}

// --- What the chip sounds like ------------------------------------------

TEST(PsgSynth, ASilentChipIsExactlyZero) {
  psg_synth chip;
  EXPECT_EQ(chip.run(100000), 0.0);
  EXPECT_EQ(chip.level(), 0.0F);
}

TEST(PsgSynth, AToneAtFullLevelSpendsHalfItsTimeHigh) {
  psg_synth chip;
  chip.write(tone_low(0, 0x0));
  chip.write(tone_high(0x10));  // period 256
  chip.write(volume(0, 0));

  // A whole number of periods: 32 * 256 chip clocks each.
  const ticks span = 32 * 256 * 10 / psg_clocks_per_tick;
  const double mean = chip.run(span) / static_cast<double>(span * 3);
  EXPECT_NEAR(mean, full / 2.0, full / 50.0);
}

TEST(PsgSynth, AToneFlipsAtThePeriodItWasGiven) {
  psg_synth chip;
  chip.write(tone_low(0, 0x0));
  chip.write(tone_high(0x01));  // period 16: a flip every 256 chip clocks
  chip.write(volume(0, 0));

  // Walk one chip step at a time and count the flips.
  float last = chip.level();
  unsigned flips = 0;
  const unsigned steps = 16 * 40;
  for (unsigned i = 0; i < steps; ++i) {
    // Sixteen chip clocks is 16/3 ticks; three steps are sixteen ticks.
    if (i % 3 == 2) {
      static_cast<void>(chip.run(6));
    } else {
      static_cast<void>(chip.run(5));
    }
    if (chip.level() != last) {
      ++flips;
      last = chip.level();
    }
  }
  EXPECT_NEAR(static_cast<double>(flips), 40.0, 1.0);
}

TEST(PsgSynth, AttenuationIsTwoDecibelsAStep) {
  psg_synth loud;
  psg_synth quiet;
  for (psg_synth* chip : {&loud, &quiet}) {
    chip->write(tone_low(0, 0x0));
    chip->write(tone_high(0x10));
  }
  loud.write(volume(0, 0));
  quiet.write(volume(0, 5));  // -10 dB
  const ticks span = 32 * 256 * 10 / psg_clocks_per_tick;
  const double ratio = quiet.run(span) / loud.run(span);
  EXPECT_NEAR(ratio, 0.316228, 0.01);
}

TEST(PsgSynth, WhiteNoiseSounds) {
  psg_synth chip;
  chip.write(noise(6));
  chip.write(volume(3, 0));
  const ticks span = pit_input_hz / 10;
  const double mean = chip.run(span) / static_cast<double>(span * 3);
  EXPECT_GT(mean, full * 0.25);
  EXPECT_LT(mean, full * 0.75);
}

TEST(PsgSynth, PeriodicNoiseIsHighOneShiftInFifteen) {
  psg_synth chip;
  chip.write(noise(0));  // periodic, a shift every 512 chip clocks
  chip.write(volume(3, 0));
  const ticks span = 512 * 15 * 40 / psg_clocks_per_tick;
  const double mean = chip.run(span) / static_cast<double>(span * 3);
  EXPECT_NEAR(mean, full / 15.0, full / 150.0);
}

TEST(PsgSynth, RewritingTheSameNoiseModeLeavesItRunning) {
  // The footstep's shape (#407): the noise control rewritten every
  // 3.9 ms. It must sound exactly like noise never rewritten, not like
  // the first few shifts after a seed played over and over.
  psg_synth rewritten;
  psg_synth left;
  for (psg_synth* chip : {&rewritten, &left}) {
    chip->write(noise(6));
    chip->write(volume(3, 0));
  }
  const ticks every = 4653;  // 3.9 ms
  for (int i = 0; i < 50; ++i) {
    rewritten.write(noise(6));
    ASSERT_EQ(rewritten.run(every), left.run(every)) << i;
  }
}

TEST(PsgSynth, ResetSilencesIt) {
  psg_synth chip;
  chip.write(volume(3, 0));
  chip.write(noise(4));
  static_cast<void>(chip.run(10000));
  chip.reset();
  EXPECT_EQ(chip.run(10000), 0.0);
}

// --- The timeline ----------------------------------------------------------

TEST(ChipTimeline, AWrittenToneIsHeard) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish_chip(0, tone_low(0, 0x0)));
  ASSERT_TRUE(audio.publish_chip(0, tone_high(0x10)));
  ASSERT_TRUE(audio.publish_chip(0, volume(0, 0)));
  audio.advance(pit_input_hz / 10);

  std::array<float, 2400> out{};
  ASSERT_EQ(audio.render(out, rate), out.size());
  float lowest = out[0];
  float highest = out[0];
  double sum = 0;
  for (const float sample : out) {
    lowest = sample < lowest ? sample : lowest;
    highest = sample > highest ? sample : highest;
    sum += static_cast<double>(sample);
  }
  EXPECT_FLOAT_EQ(lowest, 0.0F);
  EXPECT_FLOAT_EQ(highest, psg_voice_amplitude);
  EXPECT_NEAR(sum / static_cast<double>(out.size()), full / 2.0, 0.01);
}

TEST(ChipTimeline, AWriteLandsAtItsOwnTick) {
  audio_timeline audio;
  const ticks later = pit_input_hz / 20;
  ASSERT_TRUE(audio.publish_chip(later, noise(4)));
  ASSERT_TRUE(audio.publish_chip(later, volume(3, 0)));
  audio.advance(pit_input_hz / 10);

  std::array<float, 4800> out{};
  ASSERT_EQ(audio.render(out, rate), out.size());
  // Silent up to the write, sounding after it.
  const std::size_t at = rate / 20;
  for (std::size_t i = 0; i + 1 < at; ++i) {
    ASSERT_EQ(out[i], 0.0F) << i;
  }
  double after = 0;
  for (std::size_t i = at + 1; i < out.size(); ++i) {
    after += static_cast<double>(out[i]);
  }
  EXPECT_GT(after, 0.0);
}

TEST(ChipTimeline, AWriteBeforeTheLastIsRefused) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish_chip(100, 0x9F));
  EXPECT_TRUE(audio.publish_chip(100, 0xBF));
  EXPECT_FALSE(audio.publish_chip(99, 0xDF));
  EXPECT_EQ(audio.chip_published(), 2u);
}

TEST(ChipTimeline, AFullRingDropsAndCounts) {
  audio_timeline audio;
  for (std::size_t i = 0; i < audio_timeline::chip_write_capacity; ++i) {
    ASSERT_TRUE(audio.publish_chip(i, 0x9F));
  }
  EXPECT_FALSE(audio.publish_chip(audio_timeline::chip_write_capacity, 0x9F));
  EXPECT_EQ(audio.dropped_chip_writes(), 1u);
}

TEST(ChipTimeline, TheSpeakerAndTheChipAreSummed) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish(0, true));
  ASSERT_TRUE(audio.publish_chip(0, tone_low(0, 0x0)));  // period 1024
  ASSERT_TRUE(audio.publish_chip(0, volume(0, 0)));
  audio.advance(pit_input_hz / 10);

  // Period 0 counts as 1024. The voice goes high at its first counter
  // step and stays there for 16,384 chip clocks, far longer than a
  // sample, so the second sample is both at full level.
  std::array<float, 4> out{};
  ASSERT_EQ(audio.render(out, rate), out.size());
  EXPECT_FLOAT_EQ(out[1], speaker_amplitude + psg_voice_amplitude);
}

// --- The device --------------------------------------------------------

TEST(TandySound, AWriteIsKeptAndPublishedAtTheMachinesTick) {
  auto owned = std::make_unique<machine>(memory_layout::pc);
  machine& box = *owned;
  tandy_sound chip(box);
  box.attach(chip);
  box.reset();

  chip.write_port(tandy_sound_port, volume(1, 4));
  EXPECT_EQ(chip.registers().attenuation[1], 4);
  EXPECT_EQ(box.audio().chip_published(), 1u);

  // Reading the port is open bus: the chip has no read path.
  EXPECT_EQ(chip.read_port(tandy_sound_port), 0xFF);

  box.reset();
  EXPECT_EQ(chip.registers().attenuation[1], psg_silent);
}

}  // namespace
}  // namespace amberfolio::machine
