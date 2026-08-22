// SPDX-License-Identifier: AGPL-3.0-only
//
// The narrow platform interface: the seam between the freestanding core
// and the two hosts (PLAN.md §4). Frame out, audio pull, input in, the
// wall clock, and the console byte stream — five things, and the whole of
// what a host has to understand to run this machine.
//
// This file is the design document for M2-H1 (#54, the SDL3 host) and
// M2-H2 (#55, the wasm dev page). If a host author has to ask how one of
// these five works, this comment failed.
//
//
// One rule decides the shape of all of it
// ---------------------------------------
//
// **Core → host is pulled. Host → core is pushed. Nothing in core ever
// calls out.**
//
// The core produces three things — frames, audio samples, console bytes —
// and it produces all three of them at moments that are its own, on
// virtual time, inside `machine::run()`. The host consumes all three at
// moments that are the *host's*: a display refresh, an audio device
// callback, a log flush. Those two schedules have nothing to do with each
// other, which is the entire reason PLAN.md §4 insists that "host wall
// time only throttles presentation, outside machine state".
//
// So the core buffers what it produces and the host takes it when it is
// ready. There is no `on_frame` callback, no `on_audio` callback, no
// `on_console_byte` callback. That has four consequences worth stating
// plainly, because each of them was a reason:
//
//   1. **Nothing re-enters the host from inside `run()`.** A callback
//      would put host code — an SDL blit, a JS call through the wasm
//      table — halfway down a stack that starts in `machine::step()`,
//      with the processor mid-scheduling-step and a device's
//      `on_deadline` on the stack above it. Every bug that arrangement
//      produces is a bug about *when* the host ran, which is the hardest
//      kind to reproduce.
//   2. **A run is the same run whether or not anyone is listening.** That
//      is the discipline diagnostics.h and cpu/diagnostics.h already
//      state ("a run with a sink attached and a run without one are the
//      same run"), and a pull keeps it for free: the core fills its
//      buffers identically either way, and the host's attention changes
//      nothing about machine state.
//   3. **The wasm host gets what it actually wants**, which is a stable
//      pointer into linear memory and a counter to poll — not a JS
//      function installed in the wasm table (`addFunction`,
//      `ALLOW_TABLE_GROWTH`, a lifetime to manage), which is exactly the
//      "ownership handed across the boundary" that abi.h forbids.
//   4. **A slow host drops frames instead of slowing the machine.** The
//      framebuffer's generation counter tells a host how many it missed;
//      nothing back-pressures into virtual time.
//
// Input, the wall clock and program loading go the other way — the host
// pushes them — and the same rule explains why: they are things the
// *outside world* produces, and the core has no way to ask for them.
//
// The one interface that will break the rule is the VFS, reserved at the
// bottom of this file: a file read is a request the core originates and
// must have answered before the DOS handler can return, so it is
// genuinely a callout. It is M2-D5's (#50) to design; this file only says
// where it goes and why it is the exception.
//
//
// Frame out — pull, with a generation counter
// -------------------------------------------
//
// `framebuffer` is 320x200 indexed pixels plus sixteen RGB entries, owned
// by the machine, at a fixed address for the machine's whole life. The
// renderer (M2-D3, #48) writes it on a 60 Hz *virtual*-time deadline and
// calls `complete()`; `complete()` bumps `generation()`.
//
// A host presents like this, on its own schedule:
//
//     const std::uint64_t now = pc.display().generation();
//     if (now != last_presented) {
//       blit(pc.display().pixels(), pc.display().palette());
//       last_presented = now;
//     }
//
// and the wasm host does the same thing through three ABI calls, reading
// the pixels straight out of linear memory with no copy on the C side.
//
// **There is one buffer and it is not double-buffered.** It does not need
// to be, because of the threading contract below: the renderer writes it
// only from inside `machine::run()`, the host reads it only outside, and
// those are the same thread. A second buffer would cost 64 KB and a copy
// to protect against a race that the contract already forbids.
//
// If `run()` covers several frame deadlines, the intermediate frames are
// overwritten and never seen. That is correct and deliberate — they were
// never presented, so they never happened as far as the player is
// concerned — and `generation()` is how a host that cares can tell it
// happened.
//
// Rejected: handing the host a frame by callback at the deadline. Beyond
// point 1 above, it gets the timing wrong in principle: a frame completes
// when *virtual* time says so, and the host cannot present at that
// instant even if it wanted to, because its display is not ready then. A
// callback would only ever be a pull with extra steps and a stack frame.
//
//
// Audio pull — the one call that arrives off the machine thread
// -------------------------------------------------------------
//
// This is the subtle one, and it is subtle for a specific reason: **audio
// is synthesized on virtual time and consumed on wall time.** The speaker
// (M2-D4, #49) knows when its output changed in ticks; the host's audio
// device knows only that it needs 512 more samples, now, on a thread that
// must not block.
//
// `audio_timeline` is the seam between those two clocks, and it is a
// single-producer / single-consumer ring of *edges* — "at tick T the
// speaker output became high/low" — plus a published **horizon**, the
// virtual time up to which the timeline is settled.
//
//   * The **producer** is the machine thread. The speaker calls
//     `publish(at, level)` as the machine runs; `machine::run()` calls
//     `advance(now)` when its loop ends, which is the moment that says
//     "everything up to here is final".
//   * The **consumer** is whichever thread pulls audio. `render()` walks
//     the edges, box-filters them into the caller's float buffer (#49's
//     design of record: integrate the edge list over each sample
//     interval), and writes nothing but its own cursor.
//
// The threading contract, exactly:
//
//   * **`audio_timeline::render()` may be called from one other thread**,
//     concurrently with anything the machine thread is doing. It is the
//     only function in the whole platform interface — indeed in the whole
//     core — with that permission.
//   * **Exactly one thread may call `render()`.** Not "one at a time":
//     one. The consumer cursor is plain, non-atomic state that `render()`
//     owns. Two threads calling it, even serialized by the host's own
//     lock, would still be a data race on that cursor. A host that
//     changes audio devices must stop the old one before starting the
//     new one.
//   * **Everything else on the machine is machine-thread only.** `run()`,
//     `step()`, `reset()`, `post_key()`, `set_wall_time()`,
//     `display()`, `console()`, `memory()`, `processor()` — all of it. An
//     audio thread that touched any of them would be reading machine
//     state mid-step.
//   * **No mutex, and none is possible.** A real-time audio callback that
//     blocks on a lock held by a thread emulating an 8086 produces a
//     dropout, and the whole point of the callback is that it cannot
//     afford one. The ring is lock-free by construction: the producer
//     writes `head_`, `horizon_` and `epoch_`; the consumer writes
//     `tail_`; neither ever writes what the other writes. Publication is
//     release/acquire on those indices, which is what makes the edge data
//     written before a `head_` store visible to a consumer that reads
//     that store.
//   * **Nothing `render()` does is machine state.** It cannot stop the
//     machine, cannot advance the clock, cannot enqueue anything. So a
//     replay that pulls audio at different moments — or not at all — is
//     the same run. That is what lets PLAN.md §4 exclude float samples
//     from replay hashes: the samples are output, the edge list is the
//     state.
//
// The two clocks are reconciled by two rules, and both of them are the
// consumer's:
//
//   * **Underrun: hold and do not advance.** If the next sample would
//     reach past the horizon, the machine has not generated that audio
//     yet. `render()` fills the rest of the buffer with the held level
//     and leaves its cursor where it is, so nothing is lost — when the
//     machine catches up, playback resumes at exactly the tick it stopped
//     at. `underruns()` counts the calls this happened on. A host hears a
//     stall, which is the honest sound of a machine that fell behind.
//   * **Overrun: resync forward.** If the horizon has run more than
//     `max_lag` ahead of the cursor — the audio device was stopped, or
//     the host ran a huge slice — playing it all would mean unbounded
//     latency. `render()` jumps its cursor to `horizon - resync_lag`,
//     discards the edges it skipped, and counts a `resync()`. Latency is
//     bounded by construction rather than by hoping.
//
// Both hosts fit this. SDL3 puts `render()` in an `SDL_AudioStream`
// callback on SDL's audio thread. The wasm dev page has no shared memory
// with its AudioWorklet unless it opts into threads, so it calls
// `af_machine_render_audio` on the main thread and posts the samples to
// the worklet — which the contract permits, because "one thread, any
// thread" includes the machine's own.
//
// Rejected: synthesizing samples on the machine thread into a ring of
// floats, leaving the audio thread a memcpy. It sounds simpler and it is
// worse — the producer would have to guess the sample rate before the
// audio device is open, guess how far ahead to run, and re-derive
// everything when the rate changed; and the canonical state would become
// the samples, which PLAN.md §4 explicitly does not want it to be.
//
//
// Input in — stamped at the machine's own position
// ------------------------------------------------
//
// A key event arrives whenever the player presses a key, which is a wall
// time and therefore not a thing this machine is allowed to know. It
// enters the machine at **`machine::time()` as of the moment it is
// posted** — the tick the virtual clock is standing on.
//
// That is reproducible, and the argument is short. The host may only post
// between `run()` calls (machine thread, machine not mid-step), so
// `time()` is a settled value at a scheduling-step boundary. The event
// then sits in the queue until the keyboard service (M2-D8, #53) drains
// it at a step boundary and puts a keystroke in the BDA buffer. Nothing
// downstream of the post can observe *when the host called* — only the
// tick. So a recording is a list of (tick, scancode, up/down), and a
// replay that drives `run(until = tick)` and posts the same event
// produces a bit-identical run, whatever the wall clock did in between.
//
// **The host sends raw XT make/break scancodes and nothing else.** No
// ASCII, no shift state, no modifier mask. This is the host/core split
// #54 asks to have decided: the translation table (scancode + shift state
// → scancode/ASCII pair) is a fact table and lives in core with M2-D8,
// and the shift state is derived from the shift keys' own make/break
// events and kept at 0040:0017 — where era programs read it directly, so
// it has to be there and cannot also live in the host. A host that
// synthesized its own shift state would be a second copy of machine state
// outside the machine.
//
// Rejected: stamping with host time and converting. It needs host time
// inside core, which clock.h forbids in as many words. Rejected: writing
// the BDA buffer straight from the host's event handler — it bypasses the
// recordable seam and puts a host thread inside machine memory.
//
//
// The wall clock — a seed plus virtual time, never a host read
// ------------------------------------------------------------
//
// DOS 2Ah and 2Ch (M2-D7, #52) have to answer with a date and a time, and
// core may not ask the operating system what they are. So the host tells
// it, once: `set_wall_time()` says "at this tick, the wall clock read
// this", and every later read is that instant plus the virtual time
// elapsed since.
//
// This is a better answer than a `now()` callback into the host, which
// was the obvious design:
//
//   * It is **deterministic**. Two runs seeded identically report
//     identical dates at identical ticks, including 2Ch's centiseconds.
//   * It is **recordable in one number**, not in one entry per read. A
//     replay's initial conditions (PLAN.md §4) grow by a single field.
//   * It **advances**. A program that reads 2Ch twice to time something
//     sees the second read later than the first, which a value pushed
//     once per frame would not give it.
//   * It keeps the **no-callout rule** whole, which means the C ABI needs
//     no function pointer from JS.
//
// A host that wants to track the real clock re-seeds whenever it likes;
// each re-seed is one more recordable event with a virtual timestamp,
// exactly like a key event. A host that never seeds gets 1980-01-01
// 00:00:00 — and that is not a fake, it is what a PC with no clock card
// gave you when you pressed Enter at DOS's "Enter new date:" prompt.
//
//
// Console output — bytes, pulled, bounded
// ---------------------------------------
//
// There is no text-mode video in this machine and none is planned (M2
// tracking, #58). DOS console output (INT 21h AH=02h/09h/40h, M2-D7)
// therefore goes into `console_output`, a bounded ring of bytes the host
// drains: the SDL host writes them to stdout, the dev page appends them
// to a `<pre>`, CI reads them out of the smoke test.
//
// Bytes and not characters. What DOS writes is code page 437, and what a
// host does with that — render it, transcode it, print it raw — is the
// host's decision and not one core can make for it.
//
// A full ring **drops the newest byte and counts the drop**, and never
// blocks the machine. An emulated program must not be able to stall
// because nobody is reading its output; that would make machine progress
// depend on the host's attention, which is the same mistake as an audio
// mutex, one layer up.
//
//
// Deliberately not here
// ---------------------
//
//   * **The VFS.** M2-D5 (#50) owns it. It belongs at this boundary — it
//     is the fifth thing PLAN.md §4 lists — and it will be the one
//     host-implemented callout, for the reason given at the top: a DOS
//     read handler cannot return until the bytes are there. Its
//     semantics (case-insensitive 8.3, one drive, pinned enumeration
//     order) live in core precisely so that the backends can be dumb and
//     a replay's initial conditions are well defined.
//   * **Gamepad and virtual-keyboard events.** PLAN.md §4 has them, M6
//     builds them, and they arrive as the same scancode events this file
//     already carries — that is the point of the mapping layer being
//     host-agnostic. Nothing here has to change for them.
//   * **Stereo, resampling, and DC removal.** The PC speaker is one cone
//     and `render()` is mono. A host wanting stereo duplicates the
//     channel; a host wanting a different rate asks for it, because the
//     rate is a parameter of the pull. Removing the tone's DC component
//     is fidelity polish for M4, not correctness.
//   * **Seams.** Nothing in this file is a seam (PLAN.md §5). This is the
//     machine's boundary with the outside world, below the fidelity
//     boundary and always present; a seam is an opt-in enhancement above
//     it, and the seam engine is the only thing that may ever alter
//     machine state (M4).

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

class state_sink;

// --- Frame out --------------------------------------------------------

/// The framebuffer's width in pixels. The one video mode this machine has
/// is EGA 320x200x16 (mode 0Dh, PLAN.md §3), and the host-visible frame
/// is that mode composed out of the planes — not a general surface whose
/// size a device could change. A second mode would be a second issue and
/// a discussion about scaling; there is not going to be one.
inline constexpr unsigned frame_width = 320;

inline constexpr unsigned frame_height = 200;

inline constexpr std::size_t frame_pixels =
    std::size_t{frame_width} * frame_height;

/// Sixteen palette entries, because mode 0Dh has four planes. The EGA's
/// six-bit colour codes are the *source* of these values (M2-D3 maps them
/// through the wires this machine's display actually has — ega.h); what
/// crosses this boundary is already RGB, so that neither host has to know
/// anything about EGA colour encoding.
inline constexpr unsigned palette_entries = 16;

/// One palette entry, 8 bits per channel, no alpha.
///
/// Three bytes and not a packed 32-bit word: a packed word forces a byte
/// order on both hosts, and the two want opposite ones often enough
/// (SDL's pixel formats are named by memory order, a browser's ImageData
/// is RGBA bytes) that picking one here would just move the shuffle
/// somewhere less obvious. Three named bytes are unambiguous everywhere.
struct rgb {
  std::uint8_t red{};
  std::uint8_t green{};
  std::uint8_t blue{};

  friend constexpr bool operator==(const rgb&, const rgb&) = default;
};

/// The completed frame, and the counter that says it is a new one.
///
/// Written by the renderer (M2-D3) on a virtual-time deadline, read by
/// the host on its own schedule. Both from the machine thread — see the
/// threading contract at the top of this file.
class framebuffer {
 public:
  // --- The host's side ------------------------------------------------

  /// One byte per pixel, row-major, `frame_width * frame_height` of them.
  /// The value is a palette index in 0-15; nothing else can appear here,
  /// because the planes only carry four bits.
  [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept {
    return pixels_;
  }

  [[nodiscard]] std::span<const rgb> palette() const noexcept {
    return palette_;
  }

  /// How many frames have been completed since the machine was built.
  ///
  /// Monotonic, and it does *not* go back to zero on `reset()`: a host
  /// compares it against what it last presented, and a counter that
  /// restarted would make a fresh frame look like an old one. `reset()`
  /// bumps it, because a reset machine has a new (blank) frame to show.
  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }

  /// The virtual time the renderer finished this frame at. Not needed to
  /// present — but it is what makes "the frame deadline landed on exactly
  /// the tick it was armed for" an assertable fact rather than a claim.
  [[nodiscard]] ticks completed_at() const noexcept { return completed_at_; }

  // --- The renderer's side --------------------------------------------

  /// The pixels, to write into. The renderer composes the planes straight
  /// into this rather than into a scratch buffer of its own: there is one
  /// buffer by design (see the top of this file), and a copy would buy
  /// nothing the threading contract does not already give.
  [[nodiscard]] std::span<std::uint8_t> writable_pixels() noexcept {
    return pixels_;
  }

  /// Set one palette entry. Out-of-range indices are ignored rather than
  /// clamped: an index past 15 is a caller bug, and clamping it would
  /// silently write entry 15 instead of saying nothing happened.
  void set_palette_entry(unsigned index, rgb color) noexcept;

  /// Publish: this frame is finished, at virtual time `at`. The only
  /// thing that moves `generation()`.
  void complete(ticks at) noexcept;

  /// Power-on: a black frame with a black palette, published. Called by
  /// `machine::reset()`.
  ///
  /// Published rather than left as it was, because a host that keeps
  /// showing the previous run's last frame after a reset is showing
  /// something that no longer exists. A real card blanks while it
  /// re-initialises; this is that.
  void reset() noexcept;

  /// The frame, its palette, its generation and when it completed
  /// (state.h): what a host presents is machine output and is pinned
  /// with the rest.
  void save_state(state_sink& out) const;

 private:
  std::array<std::uint8_t, frame_pixels> pixels_{};
  std::array<rgb, palette_entries> palette_{};
  std::uint64_t generation_{};
  ticks completed_at_{};
};

// --- Audio pull -------------------------------------------------------

/// What a fully-driven speaker output writes into the sample buffer.
///
/// Samples run 0.0 (the cone at rest) to this value, not -a..+a: a
/// silent speaker then produces exactly 0.0, which is what #49 means by
/// "silence is exact". The tone therefore carries a DC component, which
/// is also what the real thing does — the cone really is displaced while
/// the output is high — and which every practical DAC path removes. A
/// quarter of full scale leaves headroom and keeps a square wave from
/// being painful; it is a comfort choice, not a fidelity one.
inline constexpr float speaker_amplitude = 0.25F;

/// The virtual-time seam between the machine thread and the audio thread:
/// a lock-free SPSC ring of output edges plus the horizon they are
/// settled to.
///
/// Read the threading contract at the top of this file before using any
/// of it. In one line: everything under "producer" is machine-thread
/// only, `render()` belongs to exactly one other thread, and there is no
/// mutex anywhere because an audio callback cannot afford to wait for
/// one.
class audio_timeline {
 public:
  /// How many unconsumed edges the ring holds. A 1 kHz tone at 60 pulls a
  /// second is about 33 edges between pulls; the direct-drive path era
  /// games use for sampled effects can go faster, but it is bounded by
  /// how often the program can write port 61h, which is one write per
  /// scheduling step at the very most. 2048 is far above anything real
  /// and costs 32 KB.
  ///
  /// A power of two, so the index-to-slot mapping is a mask.
  static constexpr std::size_t edge_capacity = 2048;

  /// The sample rates `render()` will accept. The upper bound is well
  /// under `pit_input_hz`, which is what guarantees a sample interval is
  /// at least six ticks long and so never degenerates to zero width. The
  /// lower bound is a sanity floor; nothing sane asks for less.
  static constexpr unsigned min_sample_rate = 4000;
  static constexpr unsigned max_sample_rate = 192000;

  /// How far the horizon may run ahead of the playback cursor before
  /// `render()` gives up on the backlog and jumps forward — 200 ms of
  /// virtual time — and where it jumps to, 20 ms behind the horizon.
  ///
  /// These are host-pacing tolerances, not machine behaviour: they bound
  /// audio latency when the host's audio device has been stopped or its
  /// run slices are much bigger than its pulls. Nothing in the machine
  /// can observe them.
  static constexpr ticks max_lag = pit_input_hz / 5;
  static constexpr ticks resync_lag = pit_input_hz / 50;

  // --- Producer: the machine thread only ------------------------------

  /// The speaker output became `level` at tick `at`.
  ///
  /// False, and nothing recorded, if `at` is not strictly after the last
  /// published edge (the consumer walks the list in order and a
  /// backwards edge would corrupt that walk), or if the ring is full
  /// because the consumer has not run — in which case `dropped_edges()`
  /// counts it. Dropping rather than blocking, for the reason the console
  /// ring drops: machine progress must never depend on the host's
  /// attention.
  bool publish(ticks at, bool level) noexcept;

  /// Everything up to `now` is settled — the horizon. `machine::run()`
  /// calls this when its loop ends.
  ///
  /// Not called from `machine::step()`. A publication point is not a
  /// machine action: `step()` is the debugger's and the test's entry, it
  /// runs one instruction, and paying an atomic store per instruction to
  /// serve an audio thread that a single-stepping caller does not have
  /// would be a cost on the hot path for nobody. A caller that steps by
  /// hand and wants audio calls this itself.
  void advance(ticks now) noexcept;

  /// The RESET line: the timeline starts again at tick 0.
  ///
  /// Implemented as an epoch bump rather than as clearing the ring,
  /// because clearing would move the producer's index backwards under a
  /// consumer that may be reading it. The consumer notices the new epoch,
  /// throws away everything it had not consumed, and starts again at tick
  /// 0 — which is where the virtual clock now is.
  void restart() noexcept;

  [[nodiscard]] ticks horizon() const noexcept {
    return horizon_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::uint64_t dropped_edges() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
  }

  /// How many edges the producer has published since the last restart,
  /// and a running digest of every one of them (tick and level, in
  /// order). Producer-side only, so machine-thread state like everything
  /// else in the serialization: the edge list *is* the canonical audio
  /// state (this file's audio section), and a count plus a digest is how
  /// a ring that may already have been consumed can still be pinned.
  [[nodiscard]] std::uint64_t published() const noexcept { return published_; }
  [[nodiscard]] std::uint64_t edge_digest() const noexcept {
    return edge_digest_;
  }

  /// The producer's side of this timeline (state.h): the published count
  /// and digest, and the last edge. Never the consumer's cursor, the
  /// horizon, or a sample — those are output.
  void save_state(state_sink& out) const;

  // --- Consumer: exactly one thread, which may be any thread ----------

  /// Fill `out` with mono samples at `sample_rate`, box-filtered from the
  /// edge list: each sample is the fraction of its interval the speaker
  /// output spent high, times `speaker_amplitude`.
  ///
  /// `out` is **always written in full**. The return value is how many of
  /// those frames came from settled virtual time; the remainder, if any,
  /// is the held level repeated because the machine has not got there yet
  /// (see the underrun rule at the top of this file), and `underruns()`
  /// counts the call.
  ///
  /// Zero, and `out` untouched, if `sample_rate` is outside
  /// [`min_sample_rate`, `max_sample_rate`]: filling a buffer with
  /// silence would hide a host that asked for something impossible, and
  /// this is the one place in the audio path where a wrong answer is
  /// cheap to notice.
  std::size_t render(std::span<float> out, unsigned sample_rate) noexcept;

  /// Calls that ran out of settled time, and calls that had to jump
  /// forward. Both are host-pacing symptoms and neither is machine state;
  /// they are here so a host can show a number instead of guessing why it
  /// sounds wrong.
  [[nodiscard]] std::uint64_t underruns() const noexcept {
    return underruns_.load(std::memory_order_relaxed);
  }

  [[nodiscard]] std::uint64_t resyncs() const noexcept {
    return resyncs_.load(std::memory_order_relaxed);
  }

  /// Where playback has reached, in virtual time. The consumer's own
  /// cursor, so this is a consumer-thread read like `render()` itself.
  [[nodiscard]] ticks playback_position() const noexcept { return cursor_; }

 private:
  struct edge {
    ticks at{};
    bool level{};
  };

  /// Consume every edge inside `[from, to)`, leaving `level_` at what the
  /// output is by `to`, and answer how many ticks of that interval the
  /// output spent high. The whole of the box filter.
  [[nodiscard]] std::uint64_t integrate(ticks from, ticks to,
                                        std::uint64_t head) noexcept;

  /// Jump the cursor forward to `at`, consuming — and so discarding the
  /// audio of — every edge up to it. The overrun rule.
  void skip_to(ticks at, std::uint64_t head) noexcept;

  /// A new epoch: throw away every unconsumed edge whatever its tick, and
  /// start again silent at tick 0. The producer's `restart()` seen from
  /// the other side.
  void restart_playback(std::uint64_t head) noexcept;

  std::array<edge, edge_capacity> edges_{};

  /// Written by the producer, read by the consumer. `head_` is published
  /// with release *after* the slot it names is written, which is what
  /// makes the edge visible to a consumer that acquires it.
  std::atomic<std::uint64_t> head_{};
  std::atomic<ticks> horizon_{};
  std::atomic<std::uint64_t> epoch_{};
  std::atomic<std::uint64_t> dropped_{};

  /// Written by the consumer, read by the producer to know whether the
  /// ring is full. The only value that travels that direction.
  std::atomic<std::uint64_t> tail_{};

  std::atomic<std::uint64_t> underruns_{};
  std::atomic<std::uint64_t> resyncs_{};

  /// The last tick the producer published, so it can refuse a backwards
  /// edge. Producer-only; never read by the consumer.
  ticks last_published_{};
  bool have_published_{};

  /// Every edge published since the last restart, counted and folded
  /// into a running FNV-1a over (tick, level) — see `published()`.
  std::uint64_t published_{};
  std::uint64_t edge_digest_{1469598103934665603ULL};

  /// Consumer-only state. Plain, not atomic, and that is exactly why only
  /// one thread may ever call `render()`.
  std::uint64_t taken_{};
  std::uint64_t seen_epoch_{};
  ticks cursor_{};
  /// The sub-tick remainder of the sample cursor, in units of
  /// 1/sample_rate of a tick. Kept so that a sample interval that is not
  /// a whole number of ticks never rounds, and playback cannot drift over
  /// a long run.
  std::uint64_t cursor_remainder_{};
  unsigned remainder_scale_{};
  bool level_{};
};

// --- Input in ---------------------------------------------------------

enum class key_action : std::uint8_t {
  /// A make code: the key went down, or the keyboard is repeating it. The
  /// BIOS layer cannot tell those apart and neither can this one — the
  /// hardware sends the same thing.
  down,
  /// A break code: the key came up.
  up,
};

/// One key event, at the virtual time it entered the machine.
struct key_event {
  /// The tick the machine was standing on when the host posted this. See
  /// the input section at the top of this file for why that value, and
  /// why it is reproducible.
  ticks at{};

  /// An XT (scan code set 1) make code, 0x01-0x53, **without** the 0x80
  /// release bit — `action` carries that. A host sends nothing else: no
  /// ASCII, no modifier mask. Translation is M2-D8's fact table and lives
  /// in core.
  std::uint8_t scancode{};

  key_action action{};

  friend constexpr bool operator==(const key_event&,
                                   const key_event&) = default;
};

/// The host's key events, waiting for the keyboard service to drain them.
///
/// A queue rather than a direct write into the BDA buffer because the two
/// are different things: this is the recordable seam PLAN.md §4 wants,
/// and the BDA buffer is emulated hardware state that M2-D8 maintains at
/// 0040:001E where programs read it. Events cross this queue once, at a
/// step boundary, and become keystrokes there.
class input_queue {
 public:
  /// Room for a burst of typing between two drains. The BIOS buffer this
  /// feeds holds sixteen keystrokes; this holds four times that, because
  /// a make and a break are two events and a host may post a whole
  /// frame's worth before the machine next runs.
  static constexpr std::size_t capacity = 64;

  /// Queue an event. False, and `dropped()` incremented, if the queue is
  /// full.
  ///
  /// Drops the **newest**, which is what the BIOS buffer does with a
  /// keystroke that arrives full, and is the right end to drop: the
  /// events already queued are older input the machine has not seen yet,
  /// and throwing those away to make room would reorder the player's
  /// typing.
  bool post(std::uint8_t scancode, key_action action, ticks at) noexcept;

  /// Take the oldest event. False, and `out` untouched, if there is none.
  bool take(key_event& out) noexcept;

  /// The oldest event without taking it, or null. What a keyboard service
  /// uses to ask "is there input due yet" without committing to it.
  [[nodiscard]] const key_event* peek() const noexcept;

  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

  /// Throw the queue away. `machine::reset()` calls it: queued events are
  /// in-flight traffic belonging to the run that just ended, stamped with
  /// ticks from a clock that is about to go back to zero.
  void clear() noexcept;

  /// The events still queued, oldest first (state.h). A key a host posted
  /// that the keyboard service has not yet drained is machine state the
  /// next step will consume.
  void save_state(state_sink& out) const;

 private:
  std::array<key_event, capacity> events_{};
  std::size_t first_{};
  std::size_t count_{};
  std::uint64_t dropped_{};
};

// --- The wall clock ---------------------------------------------------

/// A civil date and time of day, as DOS 2Ah and 2Ch report them.
///
/// Broken-down fields rather than a serial instant, because that is the
/// shape both ends want: the host has a calendar date, and INT 21h hands
/// back exactly these fields in CX/DX. The serial form exists inside
/// `wall_clock` and nowhere else.
struct wall_time {
  /// The full year, 1980-2099 on the way in. DOS reports it in CX.
  std::uint16_t year{1980};
  /// 1-12.
  std::uint8_t month{1};
  /// 1-31.
  std::uint8_t day{1};
  /// 0 = Sunday, as DOS 2Ah reports in AL. **Computed, never supplied**:
  /// `wall_clock::set()` ignores whatever is in this field and derives it
  /// from the date, so a host cannot hand the machine a Tuesday that is
  /// really a Wednesday.
  std::uint8_t weekday{2};
  std::uint8_t hour{};
  std::uint8_t minute{};
  std::uint8_t second{};
  /// Hundredths of a second, 0-99. DOS 2Ch reports it in DL, and it is
  /// the field that makes the difference between a clock that advances
  /// and a value pushed once a frame.
  std::uint8_t centisecond{};

  friend constexpr bool operator==(const wall_time&,
                                   const wall_time&) = default;
};

/// The date and time the emulated DOS reports: an instant the host seeded
/// at a known tick, plus the virtual time elapsed since.
///
/// Machine state, not a host callout — see the wall-clock section at the
/// top of this file for the whole argument. Nothing in here reads
/// anything but its own two fields and the tick it is handed.
class wall_clock {
 public:
  /// The year range `set()` accepts. The lower bound is the DOS epoch, so
  /// that the serial form cannot go negative; the upper is where DOS's
  /// own date range stops.
  static constexpr std::uint16_t min_year = 1980;
  static constexpr std::uint16_t max_year = 2099;

  /// Seed: at tick `now`, the wall clock read `when`. `when.weekday` is
  /// ignored and recomputed.
  ///
  /// False, and nothing changed, if any field is out of range or the day
  /// does not exist in that month — 31 April and 29 February 2100 are
  /// both refused. A host that gets this wrong finds out rather than
  /// having the machine quietly report a different day than the one it
  /// was told.
  bool set(const wall_time& when, ticks now) noexcept;

  /// What the clock reads at tick `now`.
  ///
  /// Past 2099 it keeps counting and reports the year it computes.
  /// Clamping would mean a machine left running for a century started
  /// answering the same date forever, which is a lie; reporting 2101 is
  /// merely a date DOS's own range does not cover, and the handler that
  /// packs it into CX is where that gets noticed, not here.
  [[nodiscard]] wall_time at(ticks now) const noexcept;

  /// Whether a host has ever seeded this. False means `at()` is counting
  /// from 1980-01-01 00:00:00 — a real answer (a PC with no clock card
  /// gave you exactly that), not a fake one, but a host writing a save
  /// file may want to know.
  [[nodiscard]] bool seeded() const noexcept { return seeded_; }

  /// The RESET line, called by `machine::reset()` with the tick the clock
  /// is about to leave: the instant is carried across and re-based to
  /// tick 0.
  ///
  /// The wall clock does not restart, because a wall clock does not — it
  /// is a setting the host supplied, like the speed governor and the
  /// attached devices, and a machine that jumped back to 1980 on a warm
  /// boot would be inventing something. What it cannot do is keep
  /// counting from a tick that no longer exists, hence the re-base.
  void rebase(ticks from) noexcept;

  /// The seed (state.h): what DOS's date and time functions answer is
  /// derived from these and the tick, and a replay reseeds them.
  void save_state(state_sink& out) const;

 private:
  /// Hundredths of a second since 1980-01-01 00:00:00, at `base_tick_`.
  /// 64-bit: a century is 3.2e11 of them, which is nowhere near the
  /// width, and 32 bits would have wrapped after 497 days.
  std::uint64_t base_centiseconds_{};
  ticks base_tick_{};
  bool seeded_{};
};

// --- Console output ---------------------------------------------------

/// DOS console output, as a byte stream the host drains.
///
/// There is no text-mode video in this machine and none is planned; this
/// is where INT 21h's console functions write (M2-D7, #52). See the
/// console section at the top of this file.
class console_output {
 public:
  /// Four kilobytes of backlog. A drain-per-frame host never sees a
  /// fraction of it; a host that stopped draining entirely loses the
  /// newest bytes and is told how many, which is better than either
  /// stalling the machine or growing without bound.
  static constexpr std::size_t capacity = 4096;

  /// Producer: the DOS service layer, on the machine thread.
  ///
  /// A full ring drops the byte and counts it. No back-pressure, ever —
  /// see the console section above.
  void put(std::uint8_t byte) noexcept;
  void write(std::span<const std::uint8_t> bytes) noexcept;

  /// Consumer: the host, on the machine thread. Copies up to `out.size()`
  /// bytes out and removes them, answering how many.
  [[nodiscard]] std::size_t read(std::span<std::uint8_t> out) noexcept;

  [[nodiscard]] std::size_t pending() const noexcept { return count_; }

  /// Bytes the ring had no room for. Not reset by `read()`: it is a
  /// property of the run, and a host that wants to say "output was
  /// truncated" needs the total.
  [[nodiscard]] std::uint64_t dropped() const noexcept { return dropped_; }

  /// Throw away what has not been drained. `machine::reset()` calls it,
  /// for the same reason it clears the input queue: this is in-flight
  /// traffic from the run that just ended.
  void clear() noexcept;

  /// The bytes not yet drained, oldest first, and the drop count
  /// (state.h). What the host has already read is the host's; what it
  /// has not is still the machine's.
  void save_state(state_sink& out) const;

 private:
  std::array<std::uint8_t, capacity> bytes_{};
  std::size_t first_{};
  std::size_t count_{};
  std::uint64_t dropped_{};
};

}  // namespace amberfolio::machine
