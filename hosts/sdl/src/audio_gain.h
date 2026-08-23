// SPDX-License-Identifier: AGPL-3.0-only
//
// Volume and mute, on the desktop host's side of the audio boundary
// (M4-A1 remainder, #148).
//
//
// Why this is here and not in `audio_timeline` (#148 item 4)
// ----------------------------------------------------------
//
// `platform.h` already decided it, in the paragraph that refuses a
// high-pass: "removing the tone's DC component ... belongs in a *host's*
// reconstruction if anywhere, because a `render()` that high-passed would
// stop being the exact integral of the edge list, which is the one thing
// it is for." A gain is the same kind of thing said with a simpler
// arithmetic, and it loses the same property: a sample would become the
// integral of the edge list times a number nobody wrote down, and the
// artefacts that rest on that identity — `--dump`'s WAV, `AudioFilter`'s
// mean of 0.125, the duty recovered to 1e-11 — would all be statements
// about the listener's volume knob instead of about the machine.
//
// Three more reasons, each on its own sufficient:
//
//   * **It is not machine state and it is not machine output either.**
//     It is a fact about the room the player is in. Nothing in the
//     machine may observe it, nothing hashes it, no recording carries
//     it, and a run at 40% is the same run as one at 100% down to the
//     last bit of the framebuffer and the last edge in the list.
//   * **The two hosts want it at different places.** SDL renders on the
//     audio thread and hands the result straight to the device, so the
//     last point before the device is here. The browser renders on the
//     main thread and posts chunks to an AudioWorklet that holds and
//     fades a level across a stall (`audio-worklet.mjs`) — so the last
//     point before *its* device is inside the worklet, after the fade,
//     which is the only place where muting during a stall is actually
//     silent. A gain in core would be applied before both and could be
//     neither.
//   * **Core would gain a control it has no way to be given.** Every
//     other setting on the machine is set on the machine thread between
//     slices. This one is read by the audio thread, which may not touch
//     machine state at all, so it would need an atomic of its own inside
//     the machine that is deliberately outside `save_state()` — a piece
//     of not-machine-state living in the machine, which is exactly the
//     shape the edge log's own comment argues against.
//
// So the whole feature is two hosts' code, core is untouched, and the ABI
// is untouched.
//
//
// Crossing the thread boundary without a mutex
// --------------------------------------------
//
// The same constraint `audio_timeline` is built around: the audio
// callback cannot wait for anything. `target_` is a `std::atomic<float>`
// — static_asserted lock-free — written by whoever is deciding the
// volume (the main thread: a command line at startup, a key at the
// window) and read once per callback by the audio thread. Relaxed on
// both sides, because it orders nothing: it publishes no other data and
// guards no other read, so it is a value and not a handshake. Exactly the
// argument the bridge's own counters make in the other direction.
//
// Everything else here — `current_`, the glide — is the consumer's,
// plain and non-atomic, on the same terms as `audio_timeline`'s playback
// cursor: one thread calls `apply()`, not one at a time, one.
//
//
// Unity is a no-op, not a multiply
// ---------------------------------
//
// `apply()` returns without touching a byte when the gain is 1 and is
// staying there, which is every run that does not ask for otherwise. That
// is not a micro-optimization, it is the guarantee: the samples a default
// run hands its device are *the same bits* `render()` produced, so the
// numbers docs/hosts.md §4 pins cannot move because this file exists.
// `AudioGain.UnityIsTheSameBits` in tests/audio_gain_test.cpp measures
// that rather than asserting it.
//
//
// And it glides
// -------------
//
// A gain that stepped would put a discontinuity in the output at the
// moment somebody pressed the key — a click, made by the host, that the
// machine never generated. So a change is walked to over six
// milliseconds, which is the same span and the same reasoning as the
// worklet's fade to silence. It lands *exactly* on the target rather than
// approaching it, so "muted" is arithmetic silence and not a small
// number.

#pragma once

#include <atomic>
#include <cstddef>
#include <span>

namespace amberfolio::sdl {

class audio_gain {
 public:
  /// How long a change takes to walk in, in seconds. Six milliseconds:
  /// long enough that a step is not a click, short enough that a mute is
  /// a mute rather than a fade-out.
  static constexpr float ramp_seconds = 0.006F;

  explicit audio_gain(unsigned sample_rate) noexcept;

  // --- Producer: whichever thread decides the volume ------------------

  /// Set the level the output is walking towards, clamped to [0, 1].
  ///
  /// One means "what the machine made" — this host does not amplify, so
  /// that the loudest thing a player can hear is the thing
  /// `audio_timeline::render()` produced and nothing louder.
  void set(float level) noexcept;

  [[nodiscard]] float target() const noexcept {
    return target_.load(std::memory_order_relaxed);
  }

  // --- Consumer: exactly one thread, which is the one that pulls ------

  /// Scale `samples` in place by the current gain, walking it towards
  /// the target as it goes. A no-op, byte for byte, at unity.
  void apply(std::span<float> samples) noexcept;

  /// Where the walk has actually reached. The consumer's own state, so
  /// this is a consumer-thread read like `apply()` itself; it exists for
  /// the test and for nobody else.
  [[nodiscard]] float level() const noexcept { return current_; }

 private:
  static_assert(std::atomic<float>::is_always_lock_free,
                "the audio callback may not wait for anything, and a "
                "std::atomic<float> that is not lock-free would make it");

  std::atomic<float> target_{1.0F};

  /// Consumer-owned, all three. `seen_target_` is how `apply()` notices
  /// that the value under it changed without the producer having to
  /// signal anything: a new target is a target that is not the one this
  /// side was last walking to.
  float current_{1.0F};
  float seen_target_{1.0F};

  /// How much of the walk is left to do per sample, recomputed when the
  /// target moves so that a whole change takes `ramp_seconds` whatever
  /// its size — which is what makes muting a quiet run feel the same as
  /// muting a loud one, and a small step something other than instant.
  float step_{0.0F};

  /// `ramp_seconds` in samples at the rate this gain was made for. At
  /// least one, so that a nonsense rate arrives at once rather than
  /// never.
  float ramp_length_{1.0F};
};

}  // namespace amberfolio::sdl
