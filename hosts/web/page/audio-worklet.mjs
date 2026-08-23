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

    this.port.onmessage = (event) => {
      const samples = event.data;
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

    for (let i = 0; i < channel.length; ++i) {
      if (this.queue.length === 0) {
        // An underrun on this side of the boundary: the main thread has
        // not posted enough audio to keep up with real-time playback.
        // One run of starvation is one underrun, counted where it starts.
        if (this.starvedFor === 0) {
          this.underruns += 1;
          this.port.postMessage({ underruns: this.underruns });
        }
        channel[i] = this.starvedSample();
        continue;
      }
      const chunk = this.queue[0];
      const sample = chunk[this.queueOffset];
      channel[i] = sample;
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
