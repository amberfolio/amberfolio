// SPDX-License-Identifier: AGPL-3.0-only
//
// The narrow platform interface: the five things that cross the boundary
// between the core and a host, each on its own and then all of them
// together under a fake host running a real main loop.
//
// What is worth testing here is not "does the buffer hold what was put in
// it" — it is the four claims platform.h makes that a host has to be able
// to rely on:
//
//   * A frame is pulled, and a host that misses one can tell.
//   * Audio is synthesized on virtual time, so the tone a program asks
//     for has the period it asked for however the host paced itself; and
//     the two clocks are reconciled without the machine ever waiting.
//   * A key event's timestamp is the machine's own position, not the
//     host's, which is what makes a run reproducible.
//   * The date and time DOS reports come from a seed plus virtual time,
//     and never from anything that could differ between two runs.
//
// The threading contract gets one test that actually uses a thread. It
// asserts only counts — anything about *which* samples a racing consumer
// saw would be a test of the scheduler, not of the interface — but it is
// the only thing in the suite that would fail if `render()` were not
// callable off the machine thread at all.

#include "amberfolio/machine/platform.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "machine/test_host.h"

namespace amberfolio::machine {
namespace {

using ::testing::Each;
using ::testing::FloatEq;
using ::testing::FloatNear;

constexpr unsigned cd_rate = 44100;

/// How far ahead the tests settle virtual time before pulling: a tenth of
/// a second. Comfortably more than any buffer here needs, and comfortably
/// inside `audio_timeline::max_lag`, so the overrun rule stays out of the
/// way of the tests that are not about it. (`AHorizonFarAheadMakesPlayback
/// JumpForward` is the one that is, and it deliberately settles a whole
/// second.)
constexpr ticks settled_window = pit_input_hz / 10;

// --- Frame out --------------------------------------------------------

TEST(Framebuffer, StartsBlankAndUnpublished) {
  framebuffer screen;

  EXPECT_EQ(screen.generation(), 0u);
  EXPECT_EQ(screen.pixels().size(), frame_pixels);
  EXPECT_EQ(screen.palette().size(), palette_entries);
  EXPECT_THAT(screen.pixels(), Each(0));
}

TEST(Framebuffer, CompletingAFrameIsTheOnlyThingThatMovesTheGeneration) {
  framebuffer screen;

  screen.writable_pixels()[7] = 0x0A;
  screen.set_palette_entry(3, {.red = 1, .green = 2, .blue = 3});
  EXPECT_EQ(screen.generation(), 0u);

  screen.complete(1234);
  EXPECT_EQ(screen.generation(), 1u);
  EXPECT_EQ(screen.completed_at(), 1234u);
  EXPECT_EQ(screen.pixels()[7], 0x0A);
  EXPECT_EQ(screen.palette()[3], (rgb{.red = 1, .green = 2, .blue = 3}));
}

TEST(Framebuffer, IgnoresAPaletteIndexPastTheSixteenThereAre) {
  framebuffer screen;

  screen.set_palette_entry(palette_entries, {.red = 9, .green = 9, .blue = 9});
  EXPECT_THAT(screen.palette(), Each(rgb{}));
}

// A host compares the generation against what it last presented, so it
// has to keep counting across a reset — a counter that restarted would
// make the blank frame look like one already on screen.
TEST(Framebuffer, ResetBlanksAndPublishesWithoutRestartingTheCount) {
  framebuffer screen;

  screen.writable_pixels()[0] = 0x0F;
  screen.set_palette_entry(0, {.red = 5, .green = 5, .blue = 5});
  screen.complete(10);

  screen.reset();
  EXPECT_EQ(screen.generation(), 2u);
  EXPECT_EQ(screen.completed_at(), 0u);
  EXPECT_THAT(screen.pixels(), Each(0));
  EXPECT_THAT(screen.palette(), Each(rgb{}));
}

// --- Audio pull -------------------------------------------------------

/// Ticks of virtual time one sample covers at `rate`, rounded down. Used
/// by the tests to say how far a horizon has to reach.
constexpr ticks sample_span(unsigned rate) { return pit_input_hz / rate; }

TEST(AudioTimeline, ASilentMachineProducesExactlyZero) {
  audio_timeline audio;
  audio.advance(settled_window);

  std::array<float, 32> out{};
  EXPECT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_THAT(out, Each(FloatEq(0.0F)));
  EXPECT_EQ(audio.underruns(), 0u);
  EXPECT_EQ(audio.resyncs(), 0u);
}

TEST(AudioTimeline, AHeldHighOutputIsFullAmplitude) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish(0, true));
  audio.advance(settled_window);

  std::array<float, 32> out{};
  EXPECT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_THAT(out, Each(FloatEq(speaker_amplitude)));
}

// The box filter, at its smallest: an edge partway through the first
// sample's interval makes that sample the fraction of the interval the
// output was high. Anything that sampled the level instead of
// integrating it would give a whole amplitude or none.
TEST(AudioTimeline, ASampleIsTheFractionOfItsIntervalSpentHigh) {
  audio_timeline audio;
  const ticks span = sample_span(cd_rate);
  const ticks third = span / 3;

  ASSERT_TRUE(audio.publish(0, true));
  ASSERT_TRUE(audio.publish(third, false));
  audio.advance(settled_window);

  std::array<float, 2> out{};
  ASSERT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_THAT(out[0], FloatNear(speaker_amplitude * static_cast<float>(third) /
                                    static_cast<float>(span),
                                0.01F));
  EXPECT_THAT(out[1], FloatEq(0.0F));
}

// The claim the whole design exists for: the tone's period is the one the
// edges say, in samples, whatever the host's pacing was. 2000 ticks a
// cycle is 596.6 Hz, and a tenth of a second of it is 59.66 cycles.
TEST(AudioTimeline, TheToneHasThePeriodItsEdgesAsked) {
  audio_timeline audio;
  constexpr ticks half_period = 1000;
  constexpr ticks length = pit_input_hz / 10;

  bool level = false;
  for (ticks at = 0; at < length; at += half_period) {
    ASSERT_TRUE(audio.publish(at, level));
    level = !level;
  }
  audio.advance(length);

  std::vector<float> out(cd_rate / 10, 0.0F);
  ASSERT_EQ(audio.render(out, cd_rate), out.size());

  const float threshold = speaker_amplitude / 2;
  unsigned rises = 0;
  bool high = false;
  for (const float sample : out) {
    if (!high && sample > threshold) {
      ++rises;
      high = true;
    } else if (high && sample < threshold) {
      high = false;
    }
  }

  // 59.66 cycles: the tenth of a second contains 59 whole ones and part
  // of a sixtieth, so either count is the right answer depending on where
  // the last sample lands.
  EXPECT_GE(rises, 59u);
  EXPECT_LE(rises, 60u);
}

// The underrun rule: hold the level, do not advance the cursor, and lose
// nothing — when the machine catches up, playback resumes where it
// stopped.
TEST(AudioTimeline, AnUnderrunHoldsTheLevelAndKeepsItsPlace) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish(0, true));

  const ticks settled = sample_span(cd_rate) * 8;
  audio.advance(settled);

  std::array<float, 64> out{};
  const std::size_t first = audio.render(out, cd_rate);
  EXPECT_LT(first, out.size());
  EXPECT_GE(first, 7u);
  EXPECT_EQ(audio.underruns(), 1u);
  EXPECT_LE(audio.playback_position(), settled);
  // Held, not silenced: the output was high when time ran out.
  EXPECT_THAT(out.back(), FloatEq(speaker_amplitude));

  const ticks stopped_at = audio.playback_position();
  audio.advance(settled_window);
  EXPECT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_GT(audio.playback_position(), stopped_at);
  EXPECT_EQ(audio.underruns(), 1u);
}

// The overrun rule: a horizon that has run far ahead — the audio device
// was stopped, or a host ran an enormous slice — is not played through at
// unbounded latency. The cursor jumps.
TEST(AudioTimeline, AHorizonFarAheadMakesPlaybackJumpForward) {
  audio_timeline audio;
  audio.advance(pit_input_hz);

  std::array<float, 8> out{};
  EXPECT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_EQ(audio.resyncs(), 1u);
  EXPECT_GE(audio.playback_position(), pit_input_hz - audio_timeline::max_lag);
}

TEST(AudioTimeline, RefusesAnEdgeThatGoesBackwards) {
  audio_timeline audio;

  EXPECT_TRUE(audio.publish(100, true));
  EXPECT_FALSE(audio.publish(100, false));
  EXPECT_FALSE(audio.publish(99, false));
  EXPECT_TRUE(audio.publish(101, false));
}

TEST(AudioTimeline, DropsEdgesRatherThanBlockingWhenNobodyIsPulling) {
  audio_timeline audio;

  for (std::size_t i = 0; i < audio_timeline::edge_capacity; ++i) {
    ASSERT_TRUE(audio.publish(static_cast<ticks>(i + 1), (i % 2) == 0));
  }
  EXPECT_EQ(audio.dropped_edges(), 0u);

  EXPECT_FALSE(audio.publish(audio_timeline::edge_capacity + 1, true));
  EXPECT_EQ(audio.dropped_edges(), 1u);
}

TEST(AudioTimeline, RefusesASampleRateItCannotHonour) {
  audio_timeline audio;
  audio.advance(settled_window);

  std::array<float, 4> out{};
  out.fill(1.0F);
  EXPECT_EQ(audio.render(out, audio_timeline::min_sample_rate - 1), 0u);
  EXPECT_EQ(audio.render(out, audio_timeline::max_sample_rate + 1), 0u);
  EXPECT_THAT(out, Each(FloatEq(1.0F)));
}

TEST(AudioTimeline, RestartThrowsAwayThePreviousRunsAudio) {
  audio_timeline audio;
  ASSERT_TRUE(audio.publish(0, true));
  audio.advance(settled_window);

  std::array<float, 8> out{};
  ASSERT_EQ(audio.render(out, cd_rate), out.size());
  ASSERT_THAT(out, Each(FloatEq(speaker_amplitude)));

  audio.restart();
  audio.advance(settled_window);
  EXPECT_EQ(audio.render(out, cd_rate), out.size());
  EXPECT_THAT(out, Each(FloatEq(0.0F)));
  EXPECT_LT(audio.playback_position(), settled_window);
}

// --- Input in ---------------------------------------------------------

TEST(InputQueue, KeepsEventsInTheOrderTheyWerePosted) {
  input_queue keys;

  EXPECT_TRUE(keys.post(0x1E, key_action::down, 100));
  EXPECT_TRUE(keys.post(0x1E, key_action::up, 200));
  EXPECT_EQ(keys.size(), 2u);

  key_event event{};
  ASSERT_TRUE(keys.take(event));
  EXPECT_EQ(
      event,
      (key_event{.at = 100, .scancode = 0x1E, .action = key_action::down}));
  ASSERT_TRUE(keys.take(event));
  EXPECT_EQ(event.at, 200u);
  EXPECT_EQ(event.action, key_action::up);

  EXPECT_TRUE(keys.empty());
  EXPECT_FALSE(keys.take(event));
}

TEST(InputQueue, PeekLooksWithoutTaking) {
  input_queue keys;
  EXPECT_EQ(keys.peek(), nullptr);

  keys.post(0x11, key_action::down, 7);
  ASSERT_NE(keys.peek(), nullptr);
  EXPECT_EQ(keys.peek()->scancode, 0x11);
  EXPECT_EQ(keys.size(), 1u);
}

// A full queue drops the newest, as the BIOS buffer does: the events
// already in it are older input the machine has not seen, and dropping
// those would reorder the player's typing.
TEST(InputQueue, AFullQueueDropsTheNewestAndSaysSo) {
  input_queue keys;

  for (std::size_t i = 0; i < input_queue::capacity; ++i) {
    ASSERT_TRUE(keys.post(0x01, key_action::down, static_cast<ticks>(i)));
  }
  EXPECT_FALSE(keys.post(0x02, key_action::down, 999));
  EXPECT_EQ(keys.dropped(), 1u);

  key_event event{};
  ASSERT_TRUE(keys.take(event));
  EXPECT_EQ(event.at, 0u);
}

TEST(InputQueue, WrapsAroundItsStorage) {
  input_queue keys;
  key_event event{};

  for (std::size_t i = 0; i < input_queue::capacity * 3; ++i) {
    ASSERT_TRUE(keys.post(static_cast<std::uint8_t>((i % 0x50) + 1),
                          key_action::down, static_cast<ticks>(i)));
    ASSERT_TRUE(keys.take(event));
    EXPECT_EQ(event.at, static_cast<ticks>(i));
  }
  EXPECT_TRUE(keys.empty());
}

// --- The wall clock ---------------------------------------------------

TEST(WallClock, AnUnseededMachineIsAPcWithNoClockCard) {
  const wall_clock clock;

  EXPECT_FALSE(clock.seeded());
  EXPECT_EQ(clock.at(0), (wall_time{.year = 1980,
                                    .month = 1,
                                    .day = 1,
                                    .weekday = 2,
                                    .hour = 0,
                                    .minute = 0,
                                    .second = 0,
                                    .centisecond = 0}));
}

TEST(WallClock, SeedsAtATickAndComputesTheWeekday) {
  wall_clock clock;
  const wall_time when{.year = 1986,
                       .month = 6,
                       .day = 17,
                       .weekday = 0,
                       .hour = 12,
                       .minute = 30,
                       .second = 45,
                       .centisecond = 25};

  ASSERT_TRUE(clock.set(when, 5000));
  EXPECT_TRUE(clock.seeded());

  const wall_time read = clock.at(5000);
  EXPECT_EQ(read.year, 1986);
  EXPECT_EQ(read.month, 6);
  EXPECT_EQ(read.day, 17);
  EXPECT_EQ(read.hour, 12);
  EXPECT_EQ(read.minute, 30);
  EXPECT_EQ(read.second, 45);
  EXPECT_EQ(read.centisecond, 25);
  // 1986-06-17 was a Tuesday, whatever the caller put in the field.
  EXPECT_EQ(read.weekday, 2);
}

// The whole reason this is a seed and not a callback: the clock advances,
// and it advances by virtual time, so DOS 2Ch's centiseconds are worth
// reading twice.
TEST(WallClock, AdvancesWithVirtualTimeAndNothingElse) {
  wall_clock clock;
  ASSERT_TRUE(clock.set({.year = 1990,
                         .month = 12,
                         .day = 31,
                         .weekday = 0,
                         .hour = 23,
                         .minute = 59,
                         .second = 59,
                         .centisecond = 99},
                        0));

  // One centisecond is 11931.82 ticks, so 11931 of them is not one yet
  // and the clock says so rather than rounding up — which is the whole
  // reason the conversion keeps its remainder.
  EXPECT_EQ(clock.at(pit_input_hz / 100).centisecond, 99);
  EXPECT_EQ(clock.at(pit_input_hz / 100).day, 31);

  const wall_time rolled = clock.at((pit_input_hz / 100) + 1);
  EXPECT_EQ(rolled.day, 1);
  EXPECT_EQ(rolled.month, 1);
  EXPECT_EQ(rolled.year, 1991);
  EXPECT_EQ(rolled.hour, 0);
  EXPECT_EQ(rolled.centisecond, 0);
  // 1991-01-01 was a Tuesday.
  EXPECT_EQ(rolled.weekday, 2);

  // One second later, one second later. Nothing rounds.
  const wall_time after = clock.at(pit_input_hz);
  EXPECT_EQ(after.second, 0);
  EXPECT_EQ(after.minute, 0);
  EXPECT_EQ(after.centisecond, 99);
}

TEST(WallClock, RefusesADateThatDoesNotExist) {
  wall_clock clock;
  const auto seed = [&clock](std::uint16_t year, std::uint8_t month,
                             std::uint8_t day, std::uint8_t hour,
                             std::uint8_t centisecond) {
    return clock.set({.year = year,
                      .month = month,
                      .day = day,
                      .weekday = 0,
                      .hour = hour,
                      .minute = 0,
                      .second = 0,
                      .centisecond = centisecond},
                     0);
  };

  EXPECT_FALSE(seed(1979, 12, 31, 0, 0));
  EXPECT_FALSE(seed(2100, 1, 1, 0, 0));
  EXPECT_FALSE(seed(1990, 13, 1, 0, 0));
  EXPECT_FALSE(seed(1990, 0, 1, 0, 0));
  EXPECT_FALSE(seed(1990, 4, 31, 0, 0));
  EXPECT_FALSE(seed(1990, 2, 29, 0, 0));
  EXPECT_FALSE(seed(1990, 1, 1, 24, 0));
  EXPECT_FALSE(seed(1990, 1, 1, 0, 100));
  EXPECT_FALSE(clock.seeded());

  // The leap years the naive rule gets wrong, both ways.
  EXPECT_TRUE(seed(2000, 2, 29, 0, 0));
  EXPECT_FALSE(seed(2100, 2, 29, 0, 0));
  EXPECT_TRUE(seed(1988, 2, 29, 0, 0));
}

TEST(WallClock, RebaseCarriesTheInstantAcrossAClockThatRestarts) {
  wall_clock clock;
  ASSERT_TRUE(clock.set({.year = 1991,
                         .month = 3,
                         .day = 4,
                         .weekday = 0,
                         .hour = 8,
                         .minute = 0,
                         .second = 0,
                         .centisecond = 0},
                        0));

  const wall_time before = clock.at(pit_input_hz * 60);
  clock.rebase(pit_input_hz * 60);
  EXPECT_EQ(clock.at(0), before);
  EXPECT_EQ(clock.at(0).minute, 1);
}

// --- Console output ---------------------------------------------------

TEST(ConsoleOutput, DrainsWhatWasWrittenInOrder) {
  console_output console;
  const std::array<std::uint8_t, 5> hello{'h', 'e', 'l', 'l', 'o'};
  console.write(hello);

  EXPECT_EQ(console.pending(), 5u);

  std::array<std::uint8_t, 3> out{};
  EXPECT_EQ(console.read(out), 3u);
  EXPECT_THAT(out, ::testing::ElementsAre('h', 'e', 'l'));
  EXPECT_EQ(console.read(out), 2u);
  EXPECT_EQ(out[0], 'l');
  EXPECT_EQ(out[1], 'o');
  EXPECT_EQ(console.read(out), 0u);
}

TEST(ConsoleOutput, DropsRatherThanStallingTheMachine) {
  console_output console;
  for (std::size_t i = 0; i < console_output::capacity + 10; ++i) {
    console.put('x');
  }

  EXPECT_EQ(console.pending(), console_output::capacity);
  EXPECT_EQ(console.dropped(), 10u);
}

TEST(ConsoleOutput, WrapsAroundItsStorage) {
  console_output console;
  std::array<std::uint8_t, 1> out{};

  for (std::size_t i = 0; i < console_output::capacity * 3; ++i) {
    console.put(static_cast<std::uint8_t>(i));
    ASSERT_EQ(console.read(out), 1u);
    EXPECT_EQ(out[0], static_cast<std::uint8_t>(i));
  }
  EXPECT_EQ(console.dropped(), 0u);
}

// --- The machine's side of it -----------------------------------------

// The rig for these is `test::fake_host` with neither stand-in device
// attached: a machine on the heap, reset, with a HLT at a known entry
// point. A machine waiting for an interrupt is the state a host's run
// loop spends most of its time driving, and it is the shortest program
// that advances the clock without running off into memory nobody wrote.

// The determinism claim: a key event's timestamp is the machine's own
// position, which is a value the host cannot influence except by choosing
// when to run.
TEST(MachinePlatform, AKeyEventIsStampedWithTheMachinesOwnClock) {
  const test::fake_host it;

  ASSERT_TRUE(it.pc().post_key(0x1E, key_action::down));
  const key_event* first = it.pc().input().peek();
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->at, it.pc().time());
  EXPECT_EQ(first->at, 0u);

  // The down event above does not survive the run: draining it into BDA
  // state at a step boundary is exactly the documented fate platform.h
  // promises the keyboard service (keyboard.h, M2-D8), which is now in
  // the tree. What the determinism claim still says, and what is still
  // checkable here without depending on M2-D8's own internals, is that a
  // *fresh* event posted after the run is stamped with the machine's
  // position now, not with anything the host could have influenced.
  it.pc().run(10'000);
  ASSERT_TRUE(it.pc().post_key(0x1E, key_action::up));

  const key_event* second = it.pc().input().peek();
  ASSERT_NE(second, nullptr);
  EXPECT_EQ(second->at, it.pc().time());
  EXPECT_GE(second->at, 10'000u);
}

TEST(MachinePlatform, RunPublishesTheAudioHorizonAndStepDoesNot) {
  const test::fake_host it;

  EXPECT_EQ(it.pc().audio().horizon(), 0u);
  it.pc().step();
  EXPECT_EQ(it.pc().audio().horizon(), 0u);

  it.pc().run(it.pc().time() + 5'000);
  EXPECT_EQ(it.pc().audio().horizon(), it.pc().time());
}

TEST(MachinePlatform, ResetClearsTheInFlightTrafficAndBlanksTheFrame) {
  const test::fake_host it;

  it.pc().post_key(0x1E, key_action::down);
  it.pc().console().put('x');
  it.pc().display().writable_pixels()[0] = 0x0F;
  it.pc().display().complete(1);
  const std::uint64_t generation = it.pc().display().generation();

  it.pc().reset();

  EXPECT_TRUE(it.pc().input().empty());
  EXPECT_EQ(it.pc().console().pending(), 0u);
  EXPECT_EQ(it.pc().display().pixels()[0], 0);
  EXPECT_GT(it.pc().display().generation(), generation);
  EXPECT_EQ(it.pc().audio().horizon(), 0u);
}

// A wall clock does not restart when a machine warm-boots, but the tick
// it is anchored to has to.
TEST(MachinePlatform, TheWallClockSurvivesTheResetLine) {
  const test::fake_host it;

  ASSERT_TRUE(it.pc().set_wall_time({.year = 1988,
                                     .month = 9,
                                     .day = 1,
                                     .weekday = 0,
                                     .hour = 10,
                                     .minute = 0,
                                     .second = 0,
                                     .centisecond = 0}));
  it.pc().run(pit_input_hz * 2);
  const wall_time before = it.pc().wall().at(it.pc().time());
  EXPECT_EQ(before.second, 2);

  it.pc().reset();
  EXPECT_EQ(it.pc().time(), 0u);
  EXPECT_EQ(it.pc().wall().at(0), before);
  EXPECT_TRUE(it.pc().wall().seeded());
}

// --- The fake host ----------------------------------------------------

TEST(FakeHost, PresentsEveryFrameTheRendererCompletes) {
  test::fake_host host;
  host.add_frame_source();

  // Eleven turns for ten frames. `run(until)` may overshoot `until` by
  // less than one step, so a deadline armed at exactly the turn boundary
  // is dispatched at the first step of the *next* turn — and the host
  // therefore presents it one turn later. That is correct and it is worth
  // a host author knowing: a frame is presented on the turn after the one
  // whose boundary it landed on, and it is still stamped with the exact
  // tick it was armed for.
  host.turns(11);

  ASSERT_EQ(host.presented.size(), 10u);
  const std::uint64_t first = host.presented.front().generation;
  for (std::size_t i = 0; i < host.presented.size(); ++i) {
    // Frame n was completed at exactly the deadline it was armed for —
    // the property M2-F2 exists to give, seen from the host's side.
    EXPECT_EQ(host.presented[i].completed_at,
              test::frame_period * static_cast<ticks>(i + 1));
    EXPECT_EQ(host.presented[i].generation, first + i);
    EXPECT_EQ(host.presented[i].first_palette_entry.red, i + 1);
  }

  // Every frame's pattern differs from the one before it, so "presented"
  // is not the same picture ten times.
  EXPECT_NE(host.presented.front().checksum, host.presented.back().checksum);
}

TEST(FakeHost, DrainsTheConsoleTheDosLayerWouldHaveWritten) {
  test::fake_host host;
  host.add_frame_source();

  host.turns(6);

  EXPECT_EQ(host.console.size(), 5u);
  EXPECT_THAT(host.console, Each('f'));
  EXPECT_EQ(host.pc().console().dropped(), 0u);
}

// The host pulls a frame's worth of samples per turn while the machine
// generates a frame's worth of virtual time per turn, and the two stay in
// step: no underruns after the first turn, and the tone is there.
TEST(FakeHost, PullsAudioInStepWithTheMachine) {
  test::fake_host host(cd_rate);
  host.add_tone_source(1000);

  host.turns(30);

  EXPECT_EQ(host.audio.size(), 30u * (cd_rate / 60));
  EXPECT_GT(host.settled_frames, host.audio.size() / 2);
  EXPECT_EQ(host.pc().audio().dropped_edges(), 0u);

  const bool heard_something =
      std::ranges::any_of(host.audio, [](float s) { return s > 0.0F; });
  EXPECT_TRUE(heard_something);
}

TEST(FakeHost, InjectsKeysAtTheTickTheMachineHasReached) {
  test::fake_host host;
  host.add_frame_source();

  host.turns(3);
  const ticks at = host.pc().time();
  host.press(0x1E);

  EXPECT_EQ(host.pc().input().size(), 2u);
  key_event event{};
  ASSERT_TRUE(host.pc().input().take(event));
  EXPECT_EQ(event.at, at);
  EXPECT_EQ(event.action, key_action::down);
  ASSERT_TRUE(host.pc().input().take(event));
  EXPECT_EQ(event.at, at);
  EXPECT_EQ(event.action, key_action::up);
}

// The point of a pull: a host that presents less often than the machine
// renders misses frames and can tell exactly how many, and nothing about
// the machine's progress depends on its attention.
TEST(FakeHost, ASlowHostMissesFramesWithoutSlowingTheMachine) {
  constexpr unsigned how_many = 12;

  test::fake_host fast;
  fast.add_frame_source();
  fast.turns(how_many);

  // A renderer running four times as fast as the host presents — which is
  // the same situation as a host presenting four times slower than the
  // renderer, and easier to arrange.
  test::fake_host slow;
  slow.add_frame_source(test::frame_period / 4);
  slow.turns(how_many);

  // Neither host changed how far the machine got.
  EXPECT_EQ(slow.pc().time(), fast.pc().time());

  ASSERT_GT(fast.presented.size(), 1u);
  ASSERT_GT(slow.presented.size(), 1u);

  for (std::size_t i = 1; i < fast.presented.size(); ++i) {
    // Every frame seen: consecutive generations.
    EXPECT_EQ(fast.presented[i].generation,
              fast.presented[i - 1].generation + 1);
  }
  for (std::size_t i = 1; i < slow.presented.size(); ++i) {
    // Three of every four missed, and the counter says exactly that —
    // which is the entire argument for a generation counter over a
    // "there is a new frame" flag.
    EXPECT_EQ(slow.presented[i].generation,
              slow.presented[i - 1].generation + 4);
  }
}

// The threading contract, exercised rather than described: a second
// thread pulling audio while the machine runs. It asserts counts only —
// which samples a racing consumer saw is not a property of this
// interface — but nothing else in the suite would notice if `render()`
// stopped being callable from off the machine thread.
TEST(FakeHost, AudioCanBePulledFromAnotherThreadWhileTheMachineRuns) {
  test::fake_host host(cd_rate);
  host.add_tone_source(1000);
  // The pull belongs to the audio thread now. Exactly one thread may call
  // `render()`, so the main loop must stop doing it — see platform.h.
  host.pulls_audio = false;

  std::atomic<bool> running{true};
  std::atomic<std::uint64_t> pulled{0};

  std::thread audio_thread([&host, &running, &pulled] {
    std::vector<float> block(256, 0.0F);
    while (running.load(std::memory_order_relaxed)) {
      host.pc().audio().render(block, cd_rate);
      pulled.fetch_add(block.size(), std::memory_order_relaxed);
    }
  });

  host.turns(60);
  running.store(false, std::memory_order_relaxed);
  audio_thread.join();

  EXPECT_GT(pulled.load(), 0u);
  EXPECT_EQ(host.pc().time(), test::frame_period * 60);
  EXPECT_EQ(host.pc().audio().dropped_edges(), 0u);
}

}  // namespace
}  // namespace amberfolio::machine
