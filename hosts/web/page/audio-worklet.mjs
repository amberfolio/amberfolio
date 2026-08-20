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
        // Silence, not a stall — the same choice `audio_timeline::render()`
        // makes on the core side, one layer further in.
        channel[i] = 0;
        continue;
      }
      const chunk = this.queue[0];
      channel[i] = chunk[this.queueOffset];
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
