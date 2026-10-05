// SPDX-License-Identifier: AGPL-3.0-only
//
// The speaker's side of the desktop host (#472, split from main.cpp): the
// state the audio callback shares with the main thread, the callback
// itself, and the listening level F11 and F12 move.
//
// Audio, and the thread that is allowed to touch it
// -------------------------------------------------
//
// `audio_timeline::render()` is the only function in the core that may be
// called off the machine thread, and by exactly one thread — not one at a
// time (platform.h states this contract). SDL's audio stream callback is
// that thread and the only place this file calls it. Everything else —
// `run()`, key posting, frame reads — happens on the main thread.
//
// An underrun is the host's problem: `render()` fills what it can and the
// rest is silence. Nothing back-pressures into machine state, because a
// machine that ran slower when the speaker was starved would no longer be
// deterministic, which is the whole point of the edge list being the
// canonical state rather than the samples.
//
// Volume and mute are this host's too, and only this host's (M4-A1
// remainder, #148). `--volume PERCENT` and `--mute` set where the level
// starts; F11 toggles the mute and F12 steps the volume while the run is
// going. `audio_gain.h` argues at length why none of it is in core; the
// short version is that a gain inside `render()` would stop the samples
// being the exact integral of the edge list, which is the same objection
// platform.h already makes to a high-pass there.
//
// Two consequences of that placement are worth stating where somebody
// reading a run's output will meet them:
//
//   * **`--dump`'s WAV is written before the gain**, so it is what the
//     machine made rather than what this host chose to play. A muted run
//     still dumps its tone, which is the answer docs/hosts.md §3 wants
//     when the question is "is the fault in the machine or in the host".
//     The `.edges` file was never anywhere near it.
//   * **`--verify`'s `sounded` count is taken after the gain**, because
//     that number's whole job is to say what reached SDL's stream. A
//     muted run therefore reports `sounded 0` truthfully, and
//     `sdl-host-mutes-the-tone` is exactly that claim.
//
// F11 and F12 are host keys and cost the emulated program nothing: an
// 83-key XT keyboard has ten function keys, so `sdl::xt_scancode()`
// answers 0 for both and there is no scan code for them to have been
// taken from. `keymap_test.cpp` pins that, because it is the assumption
// the binding rests on.
//

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "audio_gain.h"

namespace amberfolio::sdl {

constexpr unsigned audio_sample_rate = 48000;

/// How much of a run `--dump` keeps sound for, in seconds of virtual
/// time. Long enough to hear a title sequence through; short enough that
/// the buffer it reserves is measured in megabytes rather than
/// gigabytes, which matters because the audio thread appends to it and
/// so it can never be grown.
constexpr unsigned dump_audio_seconds = 60;

/// The rungs F12 steps the volume between (#148). Four of them, and a
/// wrap from the top back to the bottom: one key has to cover a whole
/// axis — F11 and F12 are the only two keys an XT keyboard has no scan
/// code for, so they are the only two this host may take without
/// stealing one from the program — and if a wrap has to surprise
/// somebody it should surprise them quietly rather than loudly.
inline constexpr std::array<float, 4> volume_rungs{0.25F, 0.50F, 0.75F, 1.00F};

/// Where the listening level is *now* (#148). The command line says where it
/// starts and stays that way; these two are what F11 and F12 move while the
/// run is going. Neither is machine state, neither is recorded, and nothing
/// in the machine can observe either -- a run at 25% is the same run as one
/// at 100%, down to the last edge.
struct listening_level {
  float volume{1.0F};
  bool muted{false};

  /// F11.
  void toggle_mute() noexcept { muted = !muted; }

  /// F12: one rung louder, wrapping from the top back to the bottom, and
  /// while muted the first press only lifts the latch.
  void louder() noexcept;

  /// What the audio thread is told: `muted ? 0 : volume`.
  [[nodiscard]] float gain() const noexcept { return muted ? 0.0F : volume; }

  /// The line a change of level prints.
  void say() const;
};

/// The audio callback's shared state. `box` is only ever read for its
/// `audio()`, and `render()` is the one core call the contract allows off
/// the machine thread.
///
/// The three counters are the only things the main thread reads back out,
/// and they are atomic for that reason alone: the audio thread writes
/// them, `--verify`'s report reads them once the stream is destroyed and
/// the callback can no longer be running. Relaxed ordering, because they
/// order nothing — they are a tally, not a handshake.
///
/// `sounded` is the one that says something the other two cannot. A
/// callback that ran and a buffer that was filled prove the plumbing;
/// they do not distinguish a speaker from a silence, because `render()`
/// answering silence is a correct answer to most of any run. Counting
/// the samples that were not zero is what tells a tone that reached
/// SDL's stream from a tone that was only ever in the edge list.
/// `capture` is `--dump`'s: a buffer sized once, before the stream is
/// opened, and filled by whichever thread does the pulling — the audio
/// callback when there is a device, the machine thread when there is
/// not. It is never grown while a callback might be running, which is
/// what makes appending to it from the audio thread legitimate; when it
/// is full it stops taking samples and `truncated` says so, rather than
/// allocating on the one thread that must not.
///
/// `gain` is the volume control (#148), and it is the only thing in this
/// struct the main thread *writes* while the callback may be running. Its
/// own header says why it is a `std::atomic<float>` and not a lock: the
/// callback may not wait, and a level is a value rather than a handshake.
struct audio_bridge {
  machine::machine* box{};
  std::vector<float> scratch;
  sdl::audio_gain gain{audio_sample_rate};
  std::atomic<std::uint64_t> callbacks{0};
  std::atomic<std::uint64_t> samples{0};
  std::atomic<std::uint64_t> sounded{0};
  std::vector<float> capture;
  std::atomic<std::size_t> captured{0};
  std::atomic<bool> truncated{false};
};

/// Append what was just pulled to the capture buffer, if there is one.
///
/// Called from the audio thread when a device is open and from the
/// machine thread when one is not; in both cases it is the *only* writer,
/// which is the whole of what the counters' relaxed ordering rests on
/// (the main thread reads them after the stream has been destroyed).
void capture_samples(audio_bridge& bridge, std::span<const float> pulled);

/// SDL's audio-stream callback: the one place this host calls
/// `audio_timeline::render()`, and the one that runs off the machine thread.
void SDLCALL feed_audio(void* userdata, SDL_AudioStream* stream, int additional,
                        int /*total*/);

}  // namespace amberfolio::sdl
