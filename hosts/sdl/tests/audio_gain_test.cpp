// SPDX-License-Identifier: AGPL-3.0-only
//
// Volume and mute on the desktop host (#148 item 4), measured rather
// than described — which is the standard M4-A1 set for anything in the
// audio path (`docs/hosts.md` §4).
//
// Three claims, and the third is the reason this file links the core:
//
//   1. **Mute is silence**, arithmetically. Not a small number, not a
//      fade that stops somewhere near zero: every sample exactly 0.0F,
//      which is the value platform.h's representation reserves for
//      silence (#49).
//   2. **A volume scales linearly.** Half the gain is half the mean, to
//      the bit, because a scalar multiply is the only thing that happens.
//   3. **Unity changes nothing at all** — and the way this file says so
//      is by rendering a tone through a real `audio_timeline`, measuring
//      the two numbers `AudioFilter` in tests/core/machine/
//      platform_test.cpp pins (the 0.125 mean and the duty recovered from
//      the samples), and then measuring them again on the far side of
//      `audio_gain::apply()`. They must be *the same bits*, not the same
//      to a tolerance. That is what makes the numbers in docs/hosts.md §4
//      still true of what this host hands its device, and it is the
//      check that would catch somebody deciding a gain should live in
//      `render()` after all.

#include "audio_gain.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {
namespace {

using machine::audio_timeline;
using machine::pit_input_hz;
using machine::speaker_amplitude;
using machine::ticks;

/// The rate the SDL host opens its device at, which is the rate whose
/// samples this gain ever sees.
constexpr unsigned host_rate = 48000;

/// The rate `AudioFilter` measures at, and for its reason: 29,102 divides
/// `pit_input_hz` exactly, so a whole number of tone periods is a whole
/// number of samples and the mean is the duty cycle and nothing else.
constexpr unsigned exact_rate = 29102;
static_assert(pit_input_hz % exact_rate == 0);

/// A 50% square of `half`-tick half-cycles, rendered over `length` ticks
/// — the same shape `AudioFilter::ARenderedTonesMeanIsItsAmplitudeTimes
/// DutyCycle` renders, built here rather than shared because a test that
/// borrowed the other suite's helper would stop being an independent
/// measurement of the same thing.
[[nodiscard]] std::vector<float> render_square(ticks high, ticks period,
                                               ticks length, unsigned rate) {
  audio_timeline timeline;
  for (ticks at = 0; at < length;) {
    EXPECT_TRUE(timeline.publish(at, true));
    at += high;
    if (at >= length) {
      break;
    }
    EXPECT_TRUE(timeline.publish(at, false));
    at += period - high;
  }
  timeline.advance(length);

  std::vector<float> out(length / (pit_input_hz / rate), 0.0F);
  EXPECT_EQ(timeline.render(out, rate), out.size());
  return out;
}

[[nodiscard]] double mean_of(std::span<const float> samples) {
  double total = 0.0;
  for (const float sample : samples) {
    total += static_cast<double>(sample);
  }
  return total / static_cast<double>(samples.size());
}

/// Run a whole buffer through one gain in chunks of `chunk`, the way a
/// device's callbacks arrive. The chunking matters: the glide is
/// per-sample and must not restart, stall or overshoot at a buffer
/// boundary.
void apply_in_chunks(audio_gain& gain, std::span<float> samples,
                     std::size_t chunk) {
  for (std::size_t at = 0; at < samples.size(); at += chunk) {
    const std::size_t take =
        (samples.size() - at) < chunk ? (samples.size() - at) : chunk;
    gain.apply(samples.subspan(at, take));
  }
}

/// How many samples the ramp takes at a rate, so a test can say "once it
/// has arrived" without restating the constant.
[[nodiscard]] std::size_t ramp_samples(unsigned rate) {
  return static_cast<std::size_t>(static_cast<float>(rate) *
                                  audio_gain::ramp_seconds) +
         1;
}

// --- Claim 3, first, because it is the one the rest must not disturb ---

TEST(AudioGain, UnityHandsBackTheSameBits) {
  audio_gain gain(host_rate);
  ASSERT_FLOAT_EQ(gain.target(), 1.0F);

  std::vector<float> tone = render_square(1000, 2000, 82000, exact_rate);
  const std::vector<float> rendered = tone;

  apply_in_chunks(gain, tone, 512);

  // Not EXPECT_NEAR and not FloatEq: identical, sample for sample. A
  // default run's device gets what `render()` produced, so every number
  // in docs/hosts.md §4 is a number about this host too.
  EXPECT_EQ(tone, rendered);
}

TEST(AudioGain, UnityLeavesTheDcOffsetAndTheDutyExactlyWhereTheyWere) {
  // 41 periods of 2,000 ticks at 29,102 Hz: a whole number of both, so
  // the mean is the duty and nothing else. `AudioFilter` measures 0.125
  // to eleven decimal places here; the point of repeating it is what
  // comes after the gain.
  constexpr ticks period = 2000;
  constexpr ticks length = 82000;

  for (const ticks high : {ticks{250}, ticks{500}, ticks{1000}, ticks{1750}}) {
    audio_gain gain(exact_rate);
    std::vector<float> tone = render_square(high, period, length, exact_rate);

    const double before = mean_of(tone);
    const double duty = static_cast<double>(high) / static_cast<double>(period);
    ASSERT_NEAR(before / static_cast<double>(speaker_amplitude), duty, 1e-6)
        << "the rendered tone is not the duty it was asked for; this test "
           "has stopped measuring what it thinks it is";

    apply_in_chunks(gain, tone, 512);

    // The whole claim: the mean is not *close to* what it was, it is the
    // same double, because not one sample was touched.
    EXPECT_EQ(mean_of(tone), before) << "duty " << duty;
  }
}

TEST(AudioGain, UnityIsANoOpEvenAfterTheGainHasMovedAndComeBack) {
  audio_gain gain(host_rate);
  gain.set(0.5F);

  std::vector<float> settling(4 * ramp_samples(host_rate), 0.25F);
  apply_in_chunks(gain, settling, 128);
  ASSERT_FLOAT_EQ(gain.level(), 0.5F);

  gain.set(1.0F);
  apply_in_chunks(gain, settling, 128);
  ASSERT_FLOAT_EQ(gain.level(), 1.0F);

  // Back at unity the early return is live again, which is what makes
  // "unity is a no-op" a property of the gain rather than of never having
  // used it.
  std::vector<float> tone = render_square(1000, 2000, 82000, exact_rate);
  const std::vector<float> rendered = tone;
  apply_in_chunks(gain, tone, 512);
  EXPECT_EQ(tone, rendered);
}

// --- Claim 1: mute is silence -----------------------------------------

TEST(AudioGain, MuteReachesExactlyZeroAndStaysThere) {
  audio_gain gain(host_rate);
  gain.set(0.0F);

  // The ramp first: everything on the way down is between zero and the
  // sample it started from, and it never goes back up. A mute that
  // stepped would be a click this host made and the machine did not.
  std::vector<float> ramp(ramp_samples(host_rate), speaker_amplitude);
  apply_in_chunks(gain, ramp, 64);
  for (std::size_t i = 1; i < ramp.size(); ++i) {
    ASSERT_LE(ramp[i], ramp[i - 1]) << "the fade to silence is not monotonic";
    ASSERT_GE(ramp[i], 0.0F);
  }
  EXPECT_EQ(ramp.back(), 0.0F);
  EXPECT_EQ(gain.level(), 0.0F);

  // And then it is silence, exactly — 0.0F and not 1e-9, whatever the
  // machine is playing.
  std::vector<float> tone = render_square(1000, 2000, 82000, exact_rate);
  apply_in_chunks(gain, tone, 512);
  EXPECT_THAT(tone, ::testing::Each(0.0F));
}

TEST(AudioGain, MuteArrivesInsideTheRampWhateverTheBufferSizeIs) {
  // One buffer or many, the walk takes the same number of samples: it is
  // per sample, not per call, so a device that pulls 4096 at a time and
  // one that pulls 64 mute at the same moment.
  for (const std::size_t chunk :
       {std::size_t{1}, std::size_t{64}, std::size_t{512}, std::size_t{4096}}) {
    audio_gain gain(host_rate);
    gain.set(0.0F);
    std::vector<float> samples(ramp_samples(host_rate), speaker_amplitude);
    apply_in_chunks(gain, samples, chunk);
    EXPECT_EQ(gain.level(), 0.0F) << "chunk " << chunk;
    EXPECT_EQ(samples.back(), 0.0F) << "chunk " << chunk;
  }
}

// --- Claim 2: a volume scales, linearly -------------------------------

TEST(AudioGain, ASettledVolumeScalesEverySampleByIt) {
  for (const float level : {0.25F, 0.5F, 0.75F}) {
    audio_gain gain(exact_rate);
    gain.set(level);

    // Walk the ramp in over silence, so what is measured afterwards is
    // the settled gain and not the glide.
    std::vector<float> settling(ramp_samples(exact_rate), 0.0F);
    apply_in_chunks(gain, settling, 128);
    ASSERT_FLOAT_EQ(gain.level(), level);

    std::vector<float> tone = render_square(1000, 2000, 82000, exact_rate);
    const std::vector<float> rendered = tone;
    apply_in_chunks(gain, tone, 512);

    for (std::size_t i = 0; i < tone.size(); ++i) {
      ASSERT_FLOAT_EQ(tone[i], rendered[i] * level) << "sample " << i;
    }
    // Which is the same claim in one number: the DC offset docs/hosts.md
    // §4 measures scales with the volume and is not otherwise disturbed.
    EXPECT_NEAR(mean_of(tone),
                static_cast<double>(speaker_amplitude) * 0.5 *
                    static_cast<double>(level),
                1e-7)
        << "level " << level;
  }
}

TEST(AudioGain, RefusesToAmplifyAndRefusesToInvert) {
  audio_gain gain(host_rate);

  gain.set(4.0F);
  EXPECT_FLOAT_EQ(gain.target(), 1.0F);

  gain.set(-1.0F);
  EXPECT_FLOAT_EQ(gain.target(), 0.0F);
}

TEST(AudioGain, TheRampTakesTheSameTimeWhateverTheDistance) {
  // Six milliseconds is six milliseconds: a mute from full and a step
  // from 100% to 75% both arrive in `ramp_seconds`, which is what stops
  // a small change from being instant (a click) and a large one from
  // being a fade-out.
  const std::size_t span = ramp_samples(host_rate);

  audio_gain far(host_rate);
  far.set(0.0F);
  std::vector<float> a(span, 1.0F);
  apply_in_chunks(far, a, 128);
  EXPECT_EQ(far.level(), 0.0F);

  audio_gain near(host_rate);
  near.set(0.75F);
  std::vector<float> b(span, 1.0F);
  apply_in_chunks(near, b, 128);
  EXPECT_FLOAT_EQ(near.level(), 0.75F);

  // And the short one is *not* instant, which is the half that matters:
  // a quarter of the way in it has moved a quarter of the way.
  audio_gain partial(host_rate);
  partial.set(0.75F);
  std::vector<float> c(span / 4, 1.0F);
  apply_in_chunks(partial, c, 128);
  EXPECT_GT(partial.level(), 0.75F);
  EXPECT_LT(partial.level(), 1.0F);
}

}  // namespace
}  // namespace amberfolio::sdl
