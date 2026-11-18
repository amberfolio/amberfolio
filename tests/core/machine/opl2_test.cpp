// SPDX-License-Identifier: AGPL-3.0-only
//
// opl2.h: the AdLib card's chip (#616). The two tables against their
// formulas, the register map, and the waveform it draws: the pitch a
// channel is given, the levels the envelope and the total level put it
// at, the four waveforms, modulation, the rhythm section, the clip, and
// that silence is exact.

#include "amberfolio/machine/opl2.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// The chip's samples in a second.
constexpr std::size_t samples_per_second = 49716;

/// The register offsets of channel `ch`'s two operators.
[[nodiscard]] constexpr std::uint8_t modulator_at(std::uint8_t ch) {
  return static_cast<std::uint8_t>(ch % 3 + 8 * (ch / 3));
}
[[nodiscard]] constexpr std::uint8_t carrier_at(std::uint8_t ch) {
  return static_cast<std::uint8_t>(modulator_at(ch) + 3);
}

[[nodiscard]] constexpr std::uint8_t at(std::uint8_t base,
                                        std::uint8_t offset) {
  return static_cast<std::uint8_t>(base + offset);
}

/// A plain sine on channel `ch`: the carrier at full level, attacking at
/// once and holding while its key is down, releasing at the top rate.
/// The modulator's attack rate is zero, so it never leaves silence.
void sine_voice(opl2_synth& chip, std::uint8_t ch) {
  const std::uint8_t mod = modulator_at(ch);
  const std::uint8_t car = carrier_at(ch);
  chip.write(at(opl2_reg::flags, mod), 0x01);
  chip.write(at(opl2_reg::level, mod), 0x3F);
  chip.write(at(opl2_reg::attack_decay, mod), 0x00);
  chip.write(at(opl2_reg::sustain_release, mod), 0x0F);
  chip.write(at(opl2_reg::flags, car), 0x21);
  chip.write(at(opl2_reg::level, car), 0x00);
  chip.write(at(opl2_reg::attack_decay, car), 0xF0);
  chip.write(at(opl2_reg::sustain_release, car), 0x0F);
  chip.write(at(opl2_reg::feedback, ch), 0x00);
}

void key_on(opl2_synth& chip, std::uint8_t ch, std::uint16_t fnum,
            std::uint8_t block) {
  chip.write(at(opl2_reg::frequency, ch), static_cast<std::uint8_t>(fnum));
  chip.write(at(opl2_reg::key_block, ch),
             static_cast<std::uint8_t>(0x20U | (block << 2U) | (fnum >> 8U)));
}

void key_off(opl2_synth& chip, std::uint8_t ch) {
  chip.write(at(opl2_reg::key_block, ch), 0x00);
}

/// The next `count` samples the chip makes.
[[nodiscard]] std::vector<std::int32_t> take(opl2_synth& chip,
                                             std::size_t count) {
  std::vector<std::int32_t> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    static_cast<void>(chip.run(opl2_ticks_per_sample));
    out.push_back(chip.output());
  }
  return out;
}

[[nodiscard]] std::int32_t highest(const std::vector<std::int32_t>& samples) {
  return *std::ranges::max_element(samples);
}
[[nodiscard]] std::int32_t lowest(const std::vector<std::int32_t>& samples) {
  return *std::ranges::min_element(samples);
}

/// How many times the wave goes from below zero to at or above it.
[[nodiscard]] std::size_t upward_crossings(
    const std::vector<std::int32_t>& samples) {
  std::size_t count = 0;
  for (std::size_t i = 1; i < samples.size(); ++i) {
    if (samples[i - 1] < 0 && samples[i] >= 0) {
      ++count;
    }
  }
  return count;
}

// --- The tables and the map ----------------------------------------------

TEST(Opl2Tables, TheQuarterSineIsItsFormula) {
  for (std::size_t i = 0; i < opl2_log_sine.size(); ++i) {
    const double angle =
        (static_cast<double>(i) + 0.5) * std::numbers::pi / 512.0;
    EXPECT_EQ(opl2_log_sine[i], std::lround(-std::log2(std::sin(angle)) * 256))
        << i;
  }
  // The two ends, by hand: the first is the smallest sine there is, a
  // little over eight octaves down, and the last is the peak.
  EXPECT_EQ(opl2_log_sine.front(), 2137);
  EXPECT_EQ(opl2_log_sine.back(), 0);
}

TEST(Opl2Tables, TheExponentIsItsFormula) {
  for (std::size_t i = 0; i < opl2_exponent.size(); ++i) {
    const double power = std::pow(2.0, static_cast<double>(i) / 256.0);
    EXPECT_EQ(opl2_exponent[i], std::lround((power - 1.0) * 1024)) << i;
  }
  EXPECT_EQ(opl2_exponent.front(), 0);
  EXPECT_EQ(opl2_exponent.back(), 1018);
}

TEST(Opl2Map, AnOperatorsRegistersAreAtEighteenOfThirtyTwoOffsets) {
  // 00-05, 08-0D and 10-15, in order.
  std::size_t next = 0;
  for (std::uint8_t offset = 0; offset < 0x20; ++offset) {
    const bool named = offset < 0x16 && (offset & 0x07U) < 6;
    if (named) {
      EXPECT_EQ(opl2_operator_at(offset), next) << int{offset};
      ++next;
    } else {
      EXPECT_EQ(opl2_operator_at(offset), opl2_operators) << int{offset};
    }
  }
  EXPECT_EQ(next, opl2_operators);
}

// --- Silence -------------------------------------------------------------

TEST(Opl2Synth, AChipNobodyHasWrittenIsExactSilence) {
  opl2_synth chip;
  EXPECT_TRUE(chip.silent());
  EXPECT_EQ(chip.run(pit_input_hz), 0.0);
  EXPECT_EQ(chip.output(), 0);
}

TEST(Opl2Synth, ANoteThatIsReleasedGoesBackToExactSilence) {
  opl2_synth chip;
  sine_voice(chip, 0);
  key_on(chip, 0, 0x241, 4);
  EXPECT_FALSE(chip.silent());
  EXPECT_GT(highest(take(chip, 1000)), 4000);

  // The top release rate takes the 511 steps four at a time.
  key_off(chip, 0);
  static_cast<void>(take(chip, 200));
  EXPECT_TRUE(chip.silent());
  EXPECT_EQ(chip.run(pit_input_hz), 0.0);
}

// --- Pitch and level -----------------------------------------------------

TEST(Opl2Synth, AChannelSoundsAtItsFNumberAndBlock) {
  // fnum * 49716 / 2^(20 - block): 577 in block 4 is 437.7 Hz, the A
  // above middle C as an AdLib driver's table spells it.
  opl2_synth chip;
  sine_voice(chip, 0);
  key_on(chip, 0, 577, 4);
  const std::vector<std::int32_t> second = take(chip, samples_per_second);
  EXPECT_NEAR(static_cast<double>(upward_crossings(second)), 437.7, 1.0);

  // A full-level sine peaks at 4,084 either way.
  EXPECT_LE(highest(second), 4084);
  EXPECT_GE(highest(second), 4070);
  EXPECT_EQ(lowest(second), -highest(second));
}

TEST(Opl2Synth, ABlockIsAnOctaveAndTheMultipleMultiplies) {
  opl2_synth low;
  sine_voice(low, 0);
  key_on(low, 0, 577, 3);
  EXPECT_NEAR(
      static_cast<double>(upward_crossings(take(low, samples_per_second))),
      218.9, 1.0);

  // A multiple of 2 on the carrier doubles it; a multiple of 0 halves it.
  opl2_synth doubled;
  sine_voice(doubled, 0);
  doubled.write(at(opl2_reg::flags, carrier_at(0)), 0x22);
  key_on(doubled, 0, 577, 4);
  EXPECT_NEAR(
      static_cast<double>(upward_crossings(take(doubled, samples_per_second))),
      875.5, 1.0);

  opl2_synth halved;
  sine_voice(halved, 0);
  halved.write(at(opl2_reg::flags, carrier_at(0)), 0x20);
  key_on(halved, 0, 577, 4);
  EXPECT_NEAR(
      static_cast<double>(upward_crossings(take(halved, samples_per_second))),
      218.9, 1.0);
}

TEST(Opl2Synth, EveryChannelSoundsAndTheyAreTheSame) {
  for (std::uint8_t ch = 0; ch < opl2_channels; ++ch) {
    opl2_synth chip;
    sine_voice(chip, ch);
    key_on(chip, ch, 577, 4);
    EXPECT_NEAR(
        static_cast<double>(upward_crossings(take(chip, samples_per_second))),
        437.7, 1.0)
        << int{ch};
  }
}

TEST(Opl2Synth, TheTotalLevelIsThreeQuartersOfADecibelAStep) {
  // Eight steps are six decibels: half.
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::level, carrier_at(0)), 0x08);
  key_on(chip, 0, 577, 4);
  EXPECT_NEAR(highest(take(chip, samples_per_second)), 2042, 8);
}

TEST(Opl2Synth, TheEnvelopeDecaysToItsSustainLevelAndHoldsThere) {
  // Sustain level 4 is twelve decibels down, a quarter, and a sustaining
  // operator stays there for as long as its key is down.
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::attack_decay, carrier_at(0)), 0xFF);
  chip.write(at(opl2_reg::sustain_release, carrier_at(0)), 0x4F);
  key_on(chip, 0, 577, 4);
  static_cast<void>(take(chip, 1000));
  EXPECT_NEAR(highest(take(chip, samples_per_second)), 1021, 8);
  EXPECT_NEAR(highest(take(chip, samples_per_second)), 1021, 8);
}

TEST(Opl2Synth, APercussiveOperatorGoesOnFallingWithItsKeyDown) {
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::flags, carrier_at(0)), 0x01);  // Not sustaining.
  chip.write(at(opl2_reg::attack_decay, carrier_at(0)), 0xFF);
  chip.write(at(opl2_reg::sustain_release, carrier_at(0)), 0x4F);
  key_on(chip, 0, 577, 4);
  static_cast<void>(take(chip, 1000));
  EXPECT_TRUE(chip.silent());
}

TEST(Opl2Synth, ASlowerDecayTakesProportionallyLonger) {
  // Decay rate 8 with no key scaling is rate 32: a step every 32
  // samples. Sustain level 15 is 496 steps away, so 15,872 samples.
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::attack_decay, carrier_at(0)), 0xF8);
  chip.write(at(opl2_reg::sustain_release, carrier_at(0)), 0xF0);
  // The fifteenth multiple, so that a short stretch holds whole periods.
  chip.write(at(opl2_reg::flags, carrier_at(0)), 0x2F);
  key_on(chip, 0, 577, 0);

  // Half way there the level is 248 steps down, 46.5 dB: 4084 / 2^7.75,
  // which is 19.
  static_cast<void>(take(chip, 7936 - 150));
  const std::int32_t half_way = highest(take(chip, 300));
  EXPECT_GT(half_way, 15);
  EXPECT_LT(half_way, 24);
  // And at the end it is inaudible, on a sustaining operator that will
  // hold it there.
  static_cast<void>(take(chip, 8000));
  EXPECT_EQ(highest(take(chip, 2000)), 0);
  EXPECT_FALSE(chip.silent());
}

TEST(Opl2Synth, ASlowAttackClimbsAndArrives) {
  // Attack rate 8 with no key scaling: a step every 32 samples, each an
  // eighth of what is left, so a few dozen steps to full level.
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::attack_decay, carrier_at(0)), 0x80);
  key_on(chip, 0, 577, 0);
  const std::int32_t early = highest(take(chip, 200));
  static_cast<void>(take(chip, 3000));
  const std::int32_t late = highest(take(chip, samples_per_second / 4));
  EXPECT_LT(early, 100);
  EXPECT_GE(late, 4070);
}

TEST(Opl2Synth, AnOperatorWithNoAttackRateNeverSounds) {
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::attack_decay, carrier_at(0)), 0x00);
  key_on(chip, 0, 577, 4);
  EXPECT_EQ(highest(take(chip, 5000)), 0);
  EXPECT_EQ(lowest(take(chip, 5000)), 0);
}

// --- The waveforms -------------------------------------------------------

TEST(Opl2Synth, TheWaveformRegisterNeedsItsEnable) {
  opl2_synth chip;
  sine_voice(chip, 0);
  chip.write(at(opl2_reg::waveform, carrier_at(0)), 0x01);
  key_on(chip, 0, 577, 4);
  // Not enabled: still a sine, both ways.
  EXPECT_LT(lowest(take(chip, 2000)), -4000);

  // Enabled: the half sine has nothing where the sine is negative.
  chip.write(opl2_reg::test, opl2_reg::waveform_enable);
  const std::vector<std::int32_t> half = take(chip, 2000);
  EXPECT_EQ(lowest(half), 0);
  EXPECT_GT(highest(half), 4000);
}

TEST(Opl2Synth, TheRectifiedAndThePulseSineStayPositive) {
  for (const std::uint8_t wave : {std::uint8_t{2}, std::uint8_t{3}}) {
    opl2_synth chip;
    sine_voice(chip, 0);
    chip.write(opl2_reg::test, opl2_reg::waveform_enable);
    chip.write(at(opl2_reg::waveform, carrier_at(0)), wave);
    key_on(chip, 0, 577, 4);
    const std::vector<std::int32_t> out = take(chip, 4000);
    EXPECT_GE(lowest(out), 0) << int{wave};
    EXPECT_GT(highest(out), 4000) << int{wave};
    // The pulse sine is silent for half of the time; the rectified one
    // only where the sine crosses zero.
    const auto zeros = static_cast<double>(std::ranges::count(out, 0));
    if (wave == 3) {
      EXPECT_NEAR(zeros / 4000.0, 0.5, 0.02);
    } else {
      EXPECT_LT(zeros / 4000.0, 0.02);
    }
  }
}

// --- Modulation ----------------------------------------------------------

/// The same note as `sine_voice`, with the modulator sounding at full
/// level too.
void two_operator_voice(opl2_synth& chip, std::uint8_t ch) {
  sine_voice(chip, ch);
  chip.write(at(opl2_reg::level, modulator_at(ch)), 0x00);
  chip.write(at(opl2_reg::attack_decay, modulator_at(ch)), 0xF0);
  chip.write(at(opl2_reg::flags, modulator_at(ch)), 0x21);
}

TEST(Opl2Synth, TheModulatorBendsTheCarriersPhaseAndNotItsLevel) {
  opl2_synth plain;
  sine_voice(plain, 0);
  key_on(plain, 0, 577, 4);
  opl2_synth bent;
  two_operator_voice(bent, 0);
  key_on(bent, 0, 577, 4);

  const std::vector<std::int32_t> a = take(plain, 4000);
  const std::vector<std::int32_t> b = take(bent, 4000);
  EXPECT_NE(a, b);
  // Still one operator's worth of level, and many more crossings.
  EXPECT_LE(highest(b), 4084);
  EXPECT_GT(upward_crossings(b), upward_crossings(a) * 2);
}

TEST(Opl2Synth, InAdditiveModeTheTwoOperatorsAreSummed) {
  opl2_synth chip;
  two_operator_voice(chip, 0);
  chip.write(at(opl2_reg::feedback, 0), 0x01);
  key_on(chip, 0, 577, 4);
  // Two sines in step: twice one.
  const std::vector<std::int32_t> out = take(chip, 4000);
  EXPECT_GE(highest(out), 2 * 4070);
  EXPECT_LE(highest(out), 2 * 4084);
}

TEST(Opl2Synth, FeedbackChangesTheModulatorsOwnWave) {
  opl2_synth without;
  two_operator_voice(without, 0);
  without.write(at(opl2_reg::feedback, 0), 0x01);
  key_on(without, 0, 577, 4);
  opl2_synth with;
  two_operator_voice(with, 0);
  with.write(at(opl2_reg::feedback, 0), 0x0F);  // Feedback 7, additive.
  key_on(with, 0, 577, 4);
  EXPECT_NE(take(without, 4000), take(with, 4000));
}

TEST(Opl2Synth, TremoloAndVibratoMoveTheLevelAndThePitch) {
  opl2_synth steady;
  sine_voice(steady, 0);
  key_on(steady, 0, 577, 4);
  const std::vector<std::int32_t> flat = take(steady, samples_per_second);

  // Deep tremolo: 4.8 dB at the bottom of its swing, 3.7 times a second.
  opl2_synth shaking;
  sine_voice(shaking, 0);
  shaking.write(at(opl2_reg::flags, carrier_at(0)), 0xA1);
  shaking.write(opl2_reg::rhythm, 0x80);
  key_on(shaking, 0, 577, 4);
  const std::vector<std::int32_t> shaken = take(shaking, samples_per_second);
  EXPECT_NE(flat, shaken);
  EXPECT_NEAR(highest(flat), highest(shaken), 20);
  // The swing is 210 steps of 64 samples, so its bottom is 6,720 samples
  // in, and the stretch around it is quieter by that much.
  const std::vector<std::int32_t> trough(shaken.begin() + 6656,
                                         shaken.begin() + 6784);
  EXPECT_NEAR(highest(trough), 4084 * std::pow(10.0, -4.875 / 20.0), 30);

  // Vibrato leaves the level alone and moves the crossings.
  opl2_synth wavering;
  sine_voice(wavering, 0);
  wavering.write(at(opl2_reg::flags, carrier_at(0)), 0x61);
  wavering.write(opl2_reg::rhythm, 0x40);
  key_on(wavering, 0, 577, 4);
  const std::vector<std::int32_t> wavered = take(wavering, samples_per_second);
  EXPECT_NE(flat, wavered);
  EXPECT_GE(highest(wavered), 4070);
}

TEST(Opl2Synth, KeyScalingQuietensTheHigherNote) {
  // 6 dB an octave on the carrier: the same F-number an octave up is
  // half the level.
  const auto peak_in_block = [](std::uint8_t block) {
    opl2_synth chip;
    sine_voice(chip, 0);
    chip.write(at(opl2_reg::level, carrier_at(0)), 0xC0);
    key_on(chip, 0, 0x3FF, block);
    return highest(take(chip, samples_per_second / 2));
  };
  const std::int32_t low = peak_in_block(3);
  const std::int32_t high = peak_in_block(4);
  EXPECT_GT(low, 0);
  EXPECT_NEAR(static_cast<double>(low) / high, 2.0, 0.05);
}

// --- Rhythm ----------------------------------------------------------------

TEST(Opl2Synth, TheRhythmBitsKeyTheDrumsWithoutTheChannelsKeys) {
  opl2_synth chip;
  for (std::uint8_t ch = 6; ch < opl2_channels; ++ch) {
    two_operator_voice(chip, ch);
    // A pitch, and no key.
    chip.write(at(opl2_reg::frequency, ch), 0x41);
    chip.write(at(opl2_reg::key_block, ch), 0x0A);
  }
  EXPECT_TRUE(chip.silent());

  // Each drum on its own makes a sound, and they are not the same sound.
  std::vector<std::vector<std::int32_t>> drums;
  for (const std::uint8_t drum :
       {std::uint8_t{0x10}, std::uint8_t{0x08}, std::uint8_t{0x04},
        std::uint8_t{0x02}, std::uint8_t{0x01}}) {
    chip.write(opl2_reg::rhythm, static_cast<std::uint8_t>(0x20U | drum));
    drums.push_back(take(chip, 2000));
    EXPECT_GT(highest(drums.back()), 1000) << int{drum};
    chip.write(opl2_reg::rhythm, 0x20);
    static_cast<void>(take(chip, 400));
    EXPECT_TRUE(chip.silent()) << int{drum};
  }
  for (std::size_t i = 0; i < drums.size(); ++i) {
    for (std::size_t j = i + 1; j < drums.size(); ++j) {
      EXPECT_NE(drums[i], drums[j]) << i << " and " << j;
    }
  }

  // Without the rhythm bit the drum bits key nothing.
  chip.write(opl2_reg::rhythm, 0x1F);
  EXPECT_TRUE(chip.silent());
}

// --- The sum ---------------------------------------------------------------

TEST(Opl2Synth, NineChannelsAreSummedAndClippedToSixteenBits) {
  opl2_synth chip;
  for (std::uint8_t ch = 0; ch < opl2_channels; ++ch) {
    two_operator_voice(chip, ch);
    chip.write(at(opl2_reg::feedback, ch), 0x01);
    key_on(chip, ch, 577, 4);
  }
  // Eighteen sines in step would be 73,512.
  const std::vector<std::int32_t> out = take(chip, 2000);
  EXPECT_EQ(highest(out), opl2_full_scale);
  EXPECT_EQ(lowest(out), -opl2_full_scale);
}

TEST(Opl2Synth, TheSameWritesMakeTheSameSamples) {
  const auto played = [] {
    opl2_synth chip;
    two_operator_voice(chip, 2);
    chip.write(at(opl2_reg::feedback, 2), 0x0A);
    key_on(chip, 2, 0x2AE, 3);
    return take(chip, 3000);
  };
  EXPECT_EQ(played(), played());
}

TEST(Opl2Synth, RunIsTheHeldOutputTimesTheTicksItWasHeld) {
  opl2_synth chip;
  sine_voice(chip, 0);
  key_on(chip, 0, 577, 4);
  static_cast<void>(take(chip, 100));

  // Across one whole sample the sum is that sample's value, 24 times.
  const std::int32_t before = chip.output();
  EXPECT_EQ(chip.run(opl2_ticks_per_sample),
            static_cast<double>(before) * opl2_ticks_per_sample);
  // And in two uneven pieces it is the same sum.
  const std::int32_t held = chip.output();
  const double pieces = chip.run(7) + chip.run(opl2_ticks_per_sample - 7);
  EXPECT_EQ(pieces, static_cast<double>(held) * opl2_ticks_per_sample);
}

TEST(Opl2Synth, ResetIsPowerOn) {
  opl2_synth chip;
  two_operator_voice(chip, 0);
  key_on(chip, 0, 577, 4);
  static_cast<void>(take(chip, 100));
  chip.reset();
  EXPECT_TRUE(chip.silent());
  EXPECT_EQ(chip.run(pit_input_hz), 0.0);
}

}  // namespace
}  // namespace amberfolio::machine
