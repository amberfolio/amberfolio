// SPDX-License-Identifier: AGPL-3.0-only
//
// The PC speaker: port 61h's two live bits and the two static ones, the
// AND that gates channel 2's tone (and the direct-drive case that
// bypasses it by holding the gate low), silence as an exact resting
// level, tone changes surviving a reprogram out from under an already-
// armed deadline, and — this issue's exit criterion — a programmed
// divisor turning into a tone whose zero-crossing period, measured back
// out of the pulled sample stream, is that divisor exactly.
//
// pit_test.cpp already exercises channel 2's counting and gate arithmetic
// on its own; nothing here re-tests that a divisor reloads correctly,
// only that this device turns the resulting OUT line into the right
// edges on `machine::audio()`.

#include "amberfolio/machine/speaker.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;
using ::testing::Each;
using ::testing::FloatEq;
using ::testing::FloatNear;

constexpr unsigned cd_rate = 44100;

/// Port 61h's two live bits, named the way the tests read best.
constexpr std::uint8_t gate_on = 0x01;
constexpr std::uint8_t data_on = 0x02;

/// A machine with a PIT, the PIC the PIT's constructor needs (nothing
/// here raises IRQ0), and a speaker — wired up exactly the way
/// `speaker.h`'s own constructor doc comment describes, then reset so
/// the speaker's power-on correction of channel 2's gate (speaker.h,
/// `reset()`) has actually run, the way a real host's startup sequence
/// would leave it.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        irq(*box),
        timer(*box, irq),
        spk(*box, timer) {
    box->attach(irq);
    box->attach(timer);
    box->attach(spk);
    box->schedule(timer.channel0_deadline());
    box->schedule(timer.channel2_deadline());
    box->schedule(spk);
    box->reset();

    // One tick per step, exactly like pit_test.cpp's own rig and for the
    // identical reason: this file's tests place edges at exact ticks and
    // check exact sample boundaries, and the default speed governor's
    // 4-ticks-per-step (clock.h) would let `advance(n)` overshoot `n` by
    // up to three ticks — invisible to PIT divisor-vs-period tests at the
    // scale pit_test.cpp uses, but not to a box filter checking which
    // side of one tick an edge landed on.
    box->set_step_cost(1);

    // `reset()` bumps `audio_timeline`'s epoch (platform.h,
    // `machine::reset()`'s own doc comment), and the *first* `render()`
    // call after an epoch bump discards whatever the ring holds at that
    // moment, catch-up rather than corruption — but every test below
    // calls `render()` only once, at the very end, after several edges
    // it wants counted. Priming the epoch here, on an empty ring, before
    // any of that runs, is what a host's own audio thread would do
    // anyway by simply having started pulling before anything
    // interesting happened; it just has to happen once, on purpose,
    // for a rig that renders only at the end of each test.
    std::array<float, 1> priming{};
    box->audio().render(priming, audio_timeline::min_sample_rate);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }

  void advance(ticks n) const { box->run(box->time() + n); }

  /// 43h then one or two data-port writes: a control word selecting
  /// channel 2 with `access` and `mode`, followed by `value`'s bytes in
  /// the order the access mode calls for. Restated from pit_test.cpp's
  /// own `program_channel`, narrowed to the one channel this file ever
  /// touches, for the same reason clock_test.cpp restates its own rig.
  void program_tone(pit_access access, pit_mode mode,
                    std::uint16_t value) const {
    const auto mode_bits =
        static_cast<std::uint8_t>(mode == pit_mode::mode0   ? 0
                                  : mode == pit_mode::mode2 ? 2
                                                            : 3);
    const auto access_bits = static_cast<std::uint8_t>(access);
    box->write_port8(pit_control_port,
                     static_cast<std::uint8_t>((2u << 6) | (access_bits << 4) |
                                               (mode_bits << 1)));
    if (access == pit_access::lsb || access == pit_access::both) {
      box->write_port8(pit_channel2_port, static_cast<std::uint8_t>(value));
    }
    if (access == pit_access::msb || access == pit_access::both) {
      box->write_port8(pit_channel2_port,
                       static_cast<std::uint8_t>(value >> 8u));
    }
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  pic::controller irq;
  pit timer;
  speaker spk;
};

/// Ticks of virtual time one sample covers at `rate`, rounded down —
/// platform_test.cpp's own helper, restated for the identical reason.
constexpr ticks sample_span(unsigned rate) { return pit_input_hz / rate; }

/// Comfortably more than the box filter needs to settle a horizon —
/// platform_test.cpp's own margin, restated.
constexpr ticks settled_window = pit_input_hz / 10;

// --- Port 61h itself ----------------------------------------------------

TEST(SpeakerPort, PowerOnIsBothBitsLowEvenThoughChannel2sGateDefaultsHigh) {
  rig r;
  EXPECT_EQ(r.pc().read_port8(speaker_port), 0);
}

TEST(SpeakerPort, ReadsBackExactlyWhatWasWrittenToBitsZeroAndOne) {
  rig r;

  r.pc().write_port8(speaker_port, gate_on | data_on);
  EXPECT_EQ(r.pc().read_port8(speaker_port), gate_on | data_on);

  r.pc().write_port8(speaker_port, gate_on);
  EXPECT_EQ(r.pc().read_port8(speaker_port), gate_on);

  r.pc().write_port8(speaker_port, 0);
  EXPECT_EQ(r.pc().read_port8(speaker_port), 0);
}

TEST(SpeakerPort, TheRefreshDetectAndParityBitsAreAcceptedButAlwaysReadZero) {
  rig r;

  // Bits 4 and 7, alongside both live bits: accepted (no fault), and the
  // two static ones never come back.
  r.pc().write_port8(
      speaker_port, static_cast<std::uint8_t>(gate_on | data_on | 0x10 | 0x80));

  EXPECT_EQ(r.pc().read_port8(speaker_port), gate_on | data_on);
  EXPECT_TRUE(r.log.device_stops.empty());
  EXPECT_FALSE(r.pc().stopped());
}

TEST(SpeakerPort, AWriteThatSetsAnUndocumentedBitFaultsOnFirstTouch) {
  rig r;

  // Bit 2 (0x04): the parity-check enable this subset does not model.
  r.pc().write_port8(speaker_port, static_cast<std::uint8_t>(gate_on | 0x04));

  EXPECT_TRUE(r.pc().stopped());
  ASSERT_EQ(r.log.device_stops.size(), 1u);
  EXPECT_EQ(r.log.device_stops.front().at, speaker_port);
  EXPECT_EQ(r.log.device_stops.front().detail, gate_on | 0x04);

  // The fault won: neither bit was applied, matching pit.h's and pic.h's
  // own "refuse, do not half-apply" convention for a bad configuration.
  EXPECT_EQ(r.pc().read_port8(speaker_port), 0);
}

// --- The AND: gate, data enable, and channel 2's tone --------------------

TEST(SpeakerTone, AllFourGateAndDataCombinationsGateTheOutputAsWired) {
  rig r;
  constexpr ticks divisor = 2000;
  constexpr ticks step = 2000;
  r.program_tone(pit_access::both, pit_mode::mode3,
                 static_cast<std::uint16_t>(divisor));

  // All four writes and their advances first, and one render at the end
  // covering the whole span — not one render per state. A render pulls
  // only as many ticks as its own buffer is wide, and four small renders
  // interleaved with `advance()` calls that each move virtual time
  // further than that would leave the render cursor permanently behind
  // "now", so a later state's render would still be looking at an
  // earlier one's ticks. One render, indexed by where each write actually
  // landed, has no such lag to account for.
  r.pc().write_port8(speaker_port, 0);  // 00: silence
  r.advance(step);
  const ticks at_10 = r.pc().time();
  r.pc().write_port8(speaker_port, gate_on);  // 10: gated, still silent
  r.advance(step);
  const ticks at_01 = r.pc().time();
  r.pc().write_port8(speaker_port, data_on);  // 01: direct-drive high
  r.advance(step);
  const ticks at_11 = r.pc().time();
  r.pc().write_port8(speaker_port, gate_on | data_on);  // 11: the tone
  r.advance(step * 4);  // a few `divisor`-length cycles to settle

  const ticks span = sample_span(cd_rate);
  std::vector<float> out(500, 0.0F);
  ASSERT_EQ(r.pc().audio().render(out, cd_rate), out.size());

  // The sample at each window's tick midpoint — comfortably clear of the
  // one sample straddling the write tick itself, which is a legitimate
  // fractional box-filter value and not what this test is checking.
  const auto mid_sample = [](ticks from, ticks to) {
    return static_cast<std::size_t>((from + (to - from) / 2) / span);
  };

  const std::size_t silent_sample = mid_sample(0, at_10);
  const std::size_t gated_sample = mid_sample(at_10, at_01);
  const std::size_t direct_drive_sample = mid_sample(at_01, at_11);

  EXPECT_THAT(out[silent_sample], FloatEq(0.0F));
  EXPECT_THAT(out[gated_sample], FloatEq(0.0F));
  EXPECT_THAT(out[direct_drive_sample], FloatEq(speaker_amplitude));

  // 11: the tone plays — not a steady level either way, which is enough
  // to tell it apart from the other three states.
  const std::size_t tone_from = static_cast<std::size_t>(at_11 / span) + 2;
  ASSERT_LT(tone_from, out.size());
  const auto tone = std::span(out).subspan(tone_from);
  const bool varies =
      std::ranges::any_of(tone,
                          [](float s) { return s > speaker_amplitude / 2; }) &&
      std::ranges::any_of(tone,
                          [](float s) { return s < speaker_amplitude / 2; });
  EXPECT_TRUE(varies);
}

// A program driving digitised sound the era way: one control word (never
// touching channel 2's data port again), then port 61h bit 1 toggled by
// software with the gate held low. Three `sample_span`-long, sample-
// aligned windows — high, low, high — so each pulled sample is either
// wholly one level or the other, with nothing fractional to tolerance
// against.
TEST(SpeakerTone, DirectDrivingBit1BySoftwareTogglesTheConeExactlyOnTheWrite) {
  rig r;
  // A mode 3 control word and a divisor, gate left low throughout (the
  // rig's own power-on state) — GATE low pins channel 2's OUT high
  // regardless of what the divisor is doing underneath it (pit.h's own
  // doc comment on `output()`), so the AND with bit 1 plays bit 1 itself.
  r.program_tone(pit_access::lsb, pit_mode::mode3, 0);
  const ticks span = sample_span(cd_rate);

  r.pc().write_port8(speaker_port, data_on);  // high, from tick 0
  r.advance(span);

  r.pc().write_port8(speaker_port, 0);  // low, from tick `span`
  r.advance(span);

  r.pc().write_port8(speaker_port, data_on);  // high again, from `2 * span`
  r.advance(span + settled_window);

  std::array<float, 3> out{};
  ASSERT_EQ(r.pc().audio().render(out, cd_rate), out.size());
  EXPECT_THAT(out[0], FloatEq(speaker_amplitude));
  EXPECT_THAT(out[1], FloatEq(0.0F));
  EXPECT_THAT(out[2], FloatEq(speaker_amplitude));
}

// --- Silence is exact -----------------------------------------------------

TEST(SpeakerSilence, ALongPullWhileSilentIsAllRestingLevel) {
  rig r;
  r.program_tone(pit_access::both, pit_mode::mode3, 2000);
  r.pc().write_port8(speaker_port, gate_on);  // data stays off: silent

  r.advance(pit_input_hz);  // a full virtual second of nothing happening

  // The return value is not asserted here: a pull this size, this long
  // after the only edge activity the machine ever had (none), is exactly
  // the overrun case platform_test.cpp's own `AHorizonFarAheadMakesPlayback
  // JumpForward` covers, and this test is not about that mechanism — it
  // is about the held level, silent or not, being exactly 0.0 regardless
  // of how `render()` got there.
  std::vector<float> out(cd_rate, 1.0F);
  r.pc().audio().render(out, cd_rate);
  EXPECT_THAT(out, Each(FloatEq(0.0F)));
  EXPECT_EQ(r.pc().audio().dropped_edges(), 0u);
}

TEST(SpeakerSilence, MutingMidBufferLandsExactlyInTheRightSample) {
  rig r;
  r.program_tone(pit_access::lsb, pit_mode::mode3, 0);  // gate stays low
  r.pc().write_port8(speaker_port, data_on);            // direct-drive high

  const ticks span = sample_span(cd_rate);
  const ticks third = span / 3;
  r.advance(third);
  r.pc().write_port8(speaker_port, 0);  // mute, a third of the way through
  r.advance(settled_window);

  std::array<float, 2> out{};
  ASSERT_EQ(r.pc().audio().render(out, cd_rate), out.size());
  EXPECT_THAT(out[0], FloatNear(speaker_amplitude * static_cast<float>(third) /
                                    static_cast<float>(span),
                                0.01F));
  EXPECT_THAT(out[1], FloatEq(0.0F));
}

// --- A tone change survives a stale deadline -------------------------------

// The whole reason `pit_channel_observer` exists: channel 2 is
// reprogrammed through the PIT's own ports, a tick this device was not
// watching for, and its already-armed deadline (from the *old* divisor)
// would otherwise fire late, or not at all before the pull happens.
TEST(SpeakerTone, PullingAcrossAToneChangeHearsTheNewDivisorNotTheOld) {
  rig r;
  constexpr ticks first_divisor = 5000;
  constexpr ticks second_divisor = 500;
  constexpr unsigned cycles_after_change = 20;

  r.program_tone(pit_access::both, pit_mode::mode3,
                 static_cast<std::uint16_t>(first_divisor));
  r.pc().write_port8(speaker_port, gate_on | data_on);
  r.advance(first_divisor * 2);  // a couple of cycles of the old tone

  // A fresh control word, then a fresh divisor: a new note, the way era
  // sound code actually changes pitch (this file's `DirectDriving...`
  // test has the same pattern without port 61h in the way). Port 61h is
  // untouched — this is exactly the write `pit_channel_observer` exists
  // to catch, since this device's own deadline is still counting down to
  // the *old* divisor's boundary when it happens.
  r.program_tone(pit_access::both, pit_mode::mode3,
                 static_cast<std::uint16_t>(second_divisor));
  const ticks changed_at = r.pc().time();
  r.advance(second_divisor * cycles_after_change);

  // A buffer comfortably inside what was actually settled (`first_divisor
  // * 2 + second_divisor * cycles_after_change` ticks): no underrun, and
  // no overrun either, so every sample answers from real edges.
  std::vector<float> out(600, 0.0F);
  ASSERT_EQ(r.pc().audio().render(out, cd_rate), out.size());

  // Count rising edges after the change: at the new, ten-times-faster
  // divisor there should be close to `cycles_after_change` of them in the
  // ticks remaining after it; a schedule still stuck on the old divisor
  // could produce at most a tenth as many in the same span.
  const std::size_t tail_from =
      static_cast<std::size_t>(changed_at / sample_span(cd_rate)) + 2;
  ASSERT_LT(tail_from, out.size());

  const float threshold = speaker_amplitude / 2;
  bool high = out[tail_from] > threshold;
  unsigned rises = high ? 1 : 0;
  for (std::size_t i = tail_from + 1; i < out.size(); ++i) {
    if (!high && out[i] > threshold) {
      ++rises;
      high = true;
    } else if (high && out[i] < threshold) {
      high = false;
    }
  }

  EXPECT_GT(rises, 5u);
}

// --- The exit criterion -----------------------------------------------------

// A programmed divisor plays a tone; the pulled sample stream's
// zero-crossing period, measured back out in samples and converted to
// ticks, is that divisor exactly.
TEST(SpeakerExitCriterion,
     APlayedTonesZeroCrossingPeriodMatchesTheProgrammedDivisorExactly) {
  rig r;
  constexpr unsigned cycles = 20;

  // `pit_input_hz` (1,193,182 = 2 x 41 x 14,551) divided exactly, rather
  // than `cd_rate`'s usual 44,100: every sample interval is then exactly
  // `ticks_per_sample` ticks wide with nothing carried between samples
  // (`audio_timeline::render()`'s `fraction` term is exactly zero). And
  // `divisor` a multiple of `ticks_per_sample`, so every rising edge
  // (every `divisor` ticks, this file's top comment on `output()`) lands
  // exactly on a sample boundary rather than partway through one. Between
  // them, a sample index converts back to a tick count with nothing to
  // round — which is what lets this test assert equality rather than a
  // tolerance band, unlike platform_test.cpp's own `TheToneHasThePeriod
  // ItsEdgesAsked` (which pulls at `cd_rate` against an arbitrary divisor
  // and allows +-1 cycle for exactly the two roundings this test avoids).
  constexpr unsigned rate = 29102;
  constexpr ticks ticks_per_sample = pit_input_hz / rate;
  static_assert(pit_input_hz % rate == 0);
  static_assert(ticks_per_sample == 41);
  constexpr ticks divisor = ticks_per_sample * 50;  // 2,050

  r.program_tone(pit_access::both, pit_mode::mode3,
                 static_cast<std::uint16_t>(divisor));
  r.pc().write_port8(speaker_port, gate_on | data_on);  // both on at tick 0

  // A few extra cycles of margin past what the pull below actually needs,
  // so every sample in it answers from settled virtual time.
  r.advance(divisor * (cycles + 5));

  // Comfortably inside the settled span above: `1000 * ticks_per_sample`
  // is 41,000 ticks, under the `divisor * (cycles + 5)` = 51,250 just
  // advanced.
  std::vector<float> out(1000, 0.0F);
  ASSERT_EQ(r.pc().audio().render(out, rate), out.size());

  const float threshold = speaker_amplitude / 2;
  bool high = false;
  std::vector<std::size_t> rises;
  for (std::size_t i = 0; i < out.size(); ++i) {
    if (!high && out[i] > threshold) {
      rises.push_back(i);
      high = true;
    } else if (high && out[i] < threshold) {
      high = false;
    }
  }

  // The tone started exactly at tick 0 with OUT already high (this file's
  // top comment on `output()`), so the pull's very first sample is itself
  // the first "rise" — comfortably more than `cycles` of them fall inside
  // the pulled buffer, at roughly one every `divisor` ticks.
  ASSERT_GE(rises.size(), cycles - 5);

  // The measured period: ticks between the first and last detected rise,
  // divided by the number of periods between them — exact, both because
  // `ticks_per_sample` carries no remainder (above) and because
  // `audio_timeline`'s box filter integrates the real edge list rather
  // than sampling a level, so the sample where a rise is detected is the
  // sample whose interval genuinely started the high half of a cycle.
  const std::size_t span_samples = rises.back() - rises.front();
  const std::size_t span_cycles = rises.size() - 1;
  const ticks span_ticks = static_cast<ticks>(span_samples) * ticks_per_sample;

  EXPECT_EQ(span_ticks / span_cycles, divisor);
  EXPECT_EQ(span_ticks % span_cycles, 0u);
}

}  // namespace
}  // namespace amberfolio::machine
