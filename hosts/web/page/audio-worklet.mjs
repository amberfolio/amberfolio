// SPDX-License-Identifier: AGPL-3.0-only
//
// The AudioWorkletProcessor half of the M2-H2 (#55) audio path.
//
// platform.h's threading contract says the wasm module has no shared
// memory with an AudioWorklet unless it opts into threads (this build
// does not — PLAN.md §4 keeps the wasm bundle lean, and threads mean
// SharedArrayBuffer and cross-origin-isolation headers this bare dev page
// has no reason to ask for). So this file never touches the module at
// all: app.mjs calls `machine.renderAudio()` on the **main thread** —
// which the contract explicitly permits, "one thread, any thread"
// includes the machine's own — and posts each resulting `Float32Array`
// across with `postMessage()`. This processor's whole job is to play
// those chunks back in order, on the audio thread real-time playback
// needs, with no wasm calls of its own.
//
// Registered by name ("amberfolio-speaker") because
// `audioContext.audioWorklet.addModule()` loads this file into a
// separate global scope (`AudioWorkletGlobalScope`) that has no `window`,
// no `document`, and nothing in common with host.mjs or app.mjs except
// what crosses `port.postMessage()` — which is exactly the boundary
// platform.h's own audio section draws, one layer further out.
//
//
// The underrun policy, and why it is not core's word for word (#108)
// ------------------------------------------------------------------
//
// `audio_timeline::render()` holds the last level when it runs out of
// settled virtual time, and `platform_test.cpp`'s
// `AnUnderrunHoldsTheLevelAndKeepsItsPlace` pins that. This file used to
// fill silence instead, which meant two hosts disagreeing about a
// documented policy — worth reconciling, and worth reconciling by
// understanding *why* core holds rather than by copying the line.
//
// Core holds because its underrun is not a gap in the waveform at all.
// The cursor does not advance, so the machine's own audio for that span
// is not lost, it is not yet made: the level being held is the level the
// speaker's cone is genuinely at, the pull resumes at exactly the tick it
// stopped on, and the wave continues from where it left off. A hold there
// is the physically true answer, and it lasts as long as one pull runs
// past the horizon — microseconds.
//
// The underrun on *this* side is a different event with the same name.
// It means the main thread did not post a chunk in time, and how long it
// will go on is a question about the browser's scheduler, not about
// virtual time: a backgrounded tab is seconds, and the backlog cap above
// means a long stall does not even replay what it missed. Holding a
// non-zero level for that long is not a continuation of anything. It is
// a DC offset on the output — inaudible in itself, since a constant
// pressure makes no sound, but a deflected cone, a bias in whatever the
// destination mixes this with, and a step at both ends of the gap.
//
// So: **hold across the seam, then fade to silence.** The first few
// milliseconds of an underrun output the last sample, which is what core
// does and what makes the common case — one late chunk — continuous. A
// stall that outlasts that is ramped down to zero over a few more and
// left there. Nothing is faked in either half: a held sample is the real
// last sample, and silence after a stall says truthfully that this host
// has nothing to play. The count of underruns is posted to the page, so
// "why did it sound wrong" has a number attached on this side of the
// boundary as well as core's (`af_machine_audio_underruns`).
//
//
// Volume and mute, and why they are in *this* file (#148)
// -------------------------------------------------------
//
// `hosts/sdl/src/audio_gain.h` makes the general argument — a gain does
// not belong in `audio_timeline::render()`, because a sample there is the
// exact integral of the edge list and every measurement in
// docs/hosts.md §4 rests on its staying that. This host has a second,
// sharper reason for the gain being *here* rather than one step earlier
// in app.mjs, where the samples are pulled:
//
// **The held level and the fade are made on this side of the boundary.**
// Scaling the chunks as they were posted would leave up to eight of them
// already queued at the old level, and — worse — would not touch
// `starvedSample()` at all. A player who muted a stalled tab would go on
// hearing the held sample for three milliseconds and then a ramp of it,
// which is a mute that does not mute. Applying the gain where the sample
// is written to the output covers the real samples and the invented ones
// with one multiply and no special case.
//
// It glides for the same reason the underrun fades: a gain that stepped
// would put a discontinuity in the output at the moment somebody moved
// the slider — a click this host made, which the machine never
// generated. Six milliseconds, the same span as the fade; and it lands
// *on* the target rather than approaching it, so muted is arithmetic
// silence and not a small number. `hosts/web/tests/smoke.mjs` measures
// all of it, unity leaving every sample untouched included.
//
// The control crosses the same `port.postMessage()` the chunks do — a
// `{ gain }` record rather than a `Float32Array` — which is this host's
// answer to the constraint platform.h states for the other side: no
// mutex, because an audio thread may not wait. A message queue drained
// between quanta is the boundary the browser already gives, and a level
// is a value rather than a handshake.

class AmberfolioSpeakerProcessor extends AudioWorkletProcessor {
  constructor() {
    super();

    /// Chunks waiting to be played, oldest first, plus how far into the
    /// oldest one playback has reached. A plain array of `Float32Array`s
    /// rather than one big ring: chunks arrive in whole pulls from
    /// `machine.renderAudio()` and are consumed sample by sample, so
    /// there is no reason to flatten them into one buffer first.
    this.queue = [];
    this.queueOffset = 0;

    // A cap on backlog, not a cap on chunk size: if the main thread falls
    // behind (a backgrounded tab, a slow rAF) and then catches up all at
    // once, playing every queued chunk back to back would mean audio
    // trailing seconds behind the picture. Dropping the *oldest* excess
    // chunks keeps latency bounded the same way
    // `audio_timeline::render()`'s own overrun rule does on the core
    // side (platform.h) — jump forward rather than let a backlog grow
    // without bound.
    this.maxQueuedChunks = 8;

    /// The last sample that came out of a chunk, and how many samples
    /// have been filled since the queue ran dry. Together they are the
    /// underrun policy in the top comment: hold `holdSamples` of the
    /// former, ramp it away over `rampSamples` more, then silence.
    ///
    /// In milliseconds rather than in samples, because what the two
    /// bounds are about is time — a chunk that is one quantum late, and
    /// a cone that should not be left deflected — and `sampleRate` is
    /// whatever the AudioContext was opened at.
    this.lastSample = 0;
    this.starvedFor = 0;
    this.holdSamples = Math.round(sampleRate * 0.003);
    this.rampSamples = Math.round(sampleRate * 0.006);

    /// Runs of starvation, not starved samples: one late chunk is one
    /// underrun however many samples it cost. Posted when it changes, so
    /// the page can show it beside core's own counters and the message
    /// rate is bounded by the thing being counted.
    this.underruns = 0;

    /// The listening level (#148): where it is, where it is going, and
    /// how far it may move in one sample. One is "what the machine
    /// made"; this host does not amplify, for the reason the desktop one
    /// does not.
    ///
    /// `gainStep` is recomputed whenever the target moves, as the
    /// remaining distance over the ramp — so a whole change takes
    /// `gainRampSamples` whatever its size, and muting a quiet page
    /// feels like muting a loud one.
    this.gain = 1;
    this.targetGain = 1;
    this.gainRampSamples = Math.max(1, Math.round(sampleRate * 0.006));
    this.gainStep = 0;

    this.port.onmessage = (event) => {
      const data = event.data;
      // The control record. Checked before the chunk, because a
      // Float32Array is not a plain object and a plain object is not
      // audio; neither shape can be mistaken for the other.
      if (data && !(data instanceof Float32Array) && typeof data.gain === 'number') {
        const wanted = Math.min(1, Math.max(0, data.gain));
        if (wanted !== this.targetGain) {
          this.targetGain = wanted;
          this.gainStep = Math.abs(wanted - this.gain) / this.gainRampSamples;
        }
        return;
      }
      const samples = data;
      if (!(samples instanceof Float32Array) || samples.length === 0) {
        return;
      }
      this.queue.push(samples);
      while (this.queue.length > this.maxQueuedChunks) {
        this.queue.shift();
        this.queueOffset = 0;
      }
    };
  }

  /// The gain to multiply the next output sample by, walked one step
  /// towards the target. Lands exactly on it rather than approaching it,
  /// which is what makes a mute silence.
  nextGain() {
    if (this.gain === this.targetGain) return this.gain;
    if (this.gain < this.targetGain) {
      this.gain = Math.min(this.targetGain, this.gain + this.gainStep);
    } else {
      this.gain = Math.max(this.targetGain, this.gain - this.gainStep);
    }
    return this.gain;
  }

  /// The value to fill an underrun sample with: the last real sample for
  /// the first few milliseconds, ramped to zero over the next few, then
  /// silence for as long as the stall lasts. See the top comment for why
  /// this is core's rule rather than a departure from it.
  starvedSample() {
    const since = this.starvedFor;
    this.starvedFor += 1;
    if (since < this.holdSamples) {
      return this.lastSample;
    }
    const into = since - this.holdSamples;
    if (into >= this.rampSamples) {
      return 0;
    }
    return this.lastSample * (1 - into / this.rampSamples);
  }

  process(_inputs, outputs) {
    const output = outputs[0];
    const channel = output[0];
    if (!channel) {
      // No output channel configured; nothing to do, but keep the
      // processor alive rather than returning false and tearing it down.
      return true;
    }

    // Unity is a no-op and not a multiply, which is the guarantee rather
    // than the optimization: with the volume where it starts, what
    // reaches the destination is bit for bit what `render()` produced,
    // so the numbers docs/hosts.md §4 pins are numbers about this host
    // too.
    const gained = this.gain !== 1 || this.targetGain !== 1;

    for (let i = 0; i < channel.length; ++i) {
      if (this.queue.length === 0) {
        // An underrun on this side of the boundary: the main thread has
        // not posted enough audio to keep up with real-time playback.
        // One run of starvation is one underrun, counted where it starts.
        if (this.starvedFor === 0) {
          this.underruns += 1;
          this.port.postMessage({ underruns: this.underruns });
        }
        // Through the gain as well, which is the whole reason it is in
        // this file: a mute during a stall has to silence the held level
        // too, and the held level exists only here.
        const held = this.starvedSample();
        channel[i] = gained ? held * this.nextGain() : held;
        continue;
      }
      const chunk = this.queue[0];
      const sample = chunk[this.queueOffset];
      // `lastSample` is the sample the machine made, not the sample the
      // listener heard: it is what an underrun holds, so moving the
      // volume must not move it or a stall would freeze the old level.
      channel[i] = gained ? sample * this.nextGain() : sample;
      this.lastSample = sample;
      this.starvedFor = 0;
      this.queueOffset += 1;
      if (this.queueOffset >= chunk.length) {
        this.queue.shift();
        this.queueOffset = 0;
      }
    }

    // The speaker is mono (platform.h: "the PC speaker is one cone and
    // render() is mono"); duplicate it across every output channel the
    // destination actually has, rather than assuming exactly one.
    for (let ch = 1; ch < output.length; ++ch) {
      output[ch].set(channel);
    }

    return true;
  }
}

registerProcessor('amberfolio-speaker', AmberfolioSpeakerProcessor);
