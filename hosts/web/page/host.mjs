// SPDX-License-Identifier: AGPL-3.0-only
//
// The hand-written JS host — deliberately not SDL-through-Emscripten
// (PLAN.md §4). It instantiates the wasm module, wraps the C ABI
// (core/include/amberfolio/abi.h) in a small `Machine` class, and carries
// the browser-code -> XT-scancode table the core's keyboard translation
// expects (keyboard.h).
//
// It is loaded both by index.html (through app.mjs) in a browser and by
// tests/smoke.mjs under node, so **it must not touch the DOM, `window`,
// `document`, `requestAnimationFrame`, or the Web Audio API**. Anything
// that needs those lives in app.mjs (the page's own glue) and
// audio-worklet.mjs (the AudioWorkletProcessor); this file is the part
// that has to run identically in both places, so a green
// `ctest --preset wasm` genuinely means the machine ran, not just that
// the module instantiated. Emscripten's glue is generated beside this
// file at build time, hence the relative import.

import createModule from './amberfolio.mjs';

// --- Status codes and constants -----------------------------------------
//
// Restated from abi.h rather than imported: a JS host has no headers, and
// a browser learns these the same way it learns everything else that
// crosses the boundary — by being told, once, in the one file allowed to
// know the numbers.

export const AF_OK = 0;
export const AF_NO_MACHINE = 1;
export const AF_STOPPED = 2;
export const AF_INVALID = 3;
/// Nothing answers this today — see abi.h, which keeps the number
/// rather than reclaiming it.
export const AF_UNIMPLEMENTED = 4;
export const AF_NO_FILESYSTEM = 5;

export const AF_KEY_UP = 0;
export const AF_KEY_DOWN = 1;

/// Where a seam stands (abi.h's AF_SEAM_*): off, on, unavailable — and
/// the answer for an index that names no seam.
export const AF_SEAM_OFF = 0;
export const AF_SEAM_ON = 1;
export const AF_SEAM_UNAVAILABLE = 2;
export const AF_SEAM_NONE = 3;

/// What one enabled seam did, as the desktop host says it at the end of a
/// run (hosts/sdl/src/main.cpp): `armed fired=N`, or `inert fired=N`, and
/// the sentence that names the failure when an armed seam fired nothing.
///
/// Here rather than in either caller because both hosts have to say it
/// identically — the same argument `machine/report.h` makes for the stop
/// line. `tools/drive.mjs` prints it after every run and the dev page
/// puts it in its console and beside each seam's checkbox; a reader
/// comparing a browser run against a desktop one should be comparing two
/// runs, not two spellings.
///
/// Takes a row of `Machine.seamList()`. `armed` says an address was
/// computed out of the seam's fact table; `fired` says a handler ran
/// there, and #131's lesson is that the first cannot stand in for the
/// second.
export function formatSeamFired(seam) {
  return (
    `${seam.armed ? 'armed' : 'inert'} fired=${Math.round(seam.fired)}` +
    (seam.armed && seam.fired === 0
      ? ' - armed and never reached; its point may not be where its facts say'
      : '')
  );
}

/// The speed governor's presets (abi.h's `AF_SPEED_*`, the values of
/// `machine::speed_preset`): 4, 2, 1 or 51/256 ticks a step. `xt` is the
/// default and the machine the game was written for.
export const AF_SPEED_PC_XT = 0;
export const AF_SPEED_TURBO_XT = 1;
export const AF_SPEED_AT = 2;
export const AF_SPEED_PC_386 = 3;

/// Those four by the names the SDL host's `--speed` takes, so a preset is
/// spelled the same on both hosts — the dev page's control, the web
/// driver's `--speed` and the desktop host's all say `turbo`. Here rather
/// than in either caller for the reason the status codes are here: one
/// place is allowed to know the numbers.
export const SPEED_PRESETS = Object.freeze({
  xt: AF_SPEED_PC_XT,
  turbo: AF_SPEED_TURBO_XT,
  at: AF_SPEED_AT,
  386: AF_SPEED_PC_386,
});

/// How a run ended, for `Machine.stopReport()`. `STOPPED` means the
/// machine stopped of its own accord and the report prints its reason;
/// the others are the *host's* reason, for a run this side cut short.
export const AF_RUN_END_STOPPED = 0;
export const AF_RUN_END_STEP_BUDGET = 1;
export const AF_RUN_END_TICK_BUDGET = 2;
export const AF_RUN_END_HOST_QUIT = 3;

/// Buffer sizes the two report calls will never overflow — the values of
/// `stop_report_capacity` and `trace_report_capacity`
/// (core/include/amberfolio/machine/report.h). Restated here for the
/// reason the status codes are: a JS host has no headers.
export const AF_STOP_REPORT_CAPACITY = 512;
export const AF_TRACE_REPORT_CAPACITY = 32768;

/// Unpack af_version()'s 0x00MMmmpp. The one place JS knows the packing;
/// keep it in step with AF_VERSION_* in core/include/amberfolio/abi.h.
export function unpackVersion(packed) {
  return {
    major: (packed >>> 16) & 0xff,
    minor: (packed >>> 8) & 0xff,
    patch: packed & 0xff,
  };
}

export function formatVersion({ major, minor, patch }) {
  return `${major}.${minor}.${patch}`;
}

/// Instantiate the module and return it alongside the core's version.
///
/// `print`/`printErr` are routed through the caller's sink so the page and
/// the headless check can each do their own thing with what the module
/// writes.
export async function loadAmberfolio({ print, printErr } = {}) {
  const output = [];
  const capture = (sink) => (text) => {
    output.push(text);
    if (sink) sink(text);
  };

  // The module runs main() during instantiation, so the log lines above
  // land before this resolves.
  const module = await createModule({
    print: capture(print ?? ((text) => console.log(text))),
    printErr: capture(printErr ?? ((text) => console.error(text))),
  });

  return { module, version: unpackVersion(module._af_version()), output };
}

// --- The machine ---------------------------------------------------------

/// A thin wrapper over one `af_machine*` handle. Every method is a direct
/// translation of one (occasionally two, for a buffer round-trip) ABI
/// calls — the JS-side mirror of abi.cpp's own rule that the boundary
/// holds no logic of its own.
///
/// Buffers a call needs (`writeMemory`, `readMemory`, `readConsole`,
/// `renderAudio`) `_malloc`/`_free` their own scratch space rather than
/// caching one: abi.h's own warning is that a typed-array view over
/// linear memory detaches the moment memory grows, so the only safe
/// pattern is to re-derive `module.HEAPU8`/`HEAPF32` at the moment of
/// use, every time — which is what every method below does, rather than
/// holding a `Uint8Array` across a call that might allocate.
export class Machine {
  constructor(module) {
    this.module = module;
    this.handle = module._af_machine_create();
    if (this.handle === 0) {
      throw new Error(
        'af_machine_create() answered null - a machine already exists in this module',
      );
    }
  }

  /// Idempotent (abi.h): safe to call more than once, and safe to skip if
  /// a caller wants the bare CPU-and-RAM machine the ABI defaults to.
  attachReferenceDevices() {
    return this.module._af_machine_attach_reference_devices(this.handle);
  }

  destroy() {
    if (this.handle !== 0) {
      this.module._af_machine_destroy(this.handle);
      this.handle = 0;
    }
  }

  reset() {
    return this.module._af_machine_reset(this.handle);
  }

  ticksPerSecond() {
    return this.module._af_ticks_per_second();
  }

  frameWidth() {
    return this.module._af_frame_width();
  }

  frameHeight() {
    return this.module._af_frame_height();
  }

  paletteEntries() {
    return this.module._af_palette_entries();
  }

  /// Run virtual time up to `tick` (absolute, not a duration — a caller
  /// keeps its own schedule and passes points on it, so the overshoot of
  /// one slice never accumulates into drift). Answers one of the AF_*
  /// status constants above.
  ///
  /// abi.h's snippet spells the next point as `next += ticksPerSecond() /
  /// 60`, and that is right for a caller whose iterations *are* 60 Hz
  /// frames: `tools/drive.mjs` is one, being unpaced and counting frames
  /// of virtual time rather than seconds of anybody's. The dev page is
  /// not — rAF calls it at the display's rate — so it asks
  /// `pacedAdvance()` below for the point elapsed wall time implies
  /// (#157).
  runUntil(tick) {
    return this.module._af_machine_run_until(this.handle, tick);
  }

  time() {
    return this.module._af_machine_time(this.handle);
  }

  /// Scheduling steps since the last reset. The axis M3's exit criterion
  /// is stated on: the desktop host and this one report the same stop
  /// line at the same step (#84).
  steps() {
    return this.module._af_machine_steps(this.handle);
  }

  /// Start or stop the trace ring. A setting: it survives `reset()`.
  setTrace(on) {
    return this.module._af_machine_set_trace(this.handle, on ? 1 : 0);
  }

  /// The stop report, as text — the same characters the desktop host
  /// prints, formatted in core so that the two cannot drift
  /// (machine/report.h). `how` is one of the AF_RUN_END_* values above.
  stopReport(how = AF_RUN_END_STOPPED) {
    return this.#report(
      (out, max) => this.module._af_machine_stop_report(this.handle, how, out, max),
      AF_STOP_REPORT_CAPACITY,
    );
  }

  /// The trace ring, as text. One line saying so when tracing was never
  /// asked for.
  traceReport() {
    return this.#report(
      (out, max) => this.module._af_machine_trace_report(this.handle, out, max),
      AF_TRACE_REPORT_CAPACITY,
    );
  }

  stopped() {
    return this.module._af_machine_stopped(this.handle) !== 0;
  }

  stopReason() {
    return this.module._af_machine_stop_reason(this.handle);
  }

  setSpeed(preset) {
    return this.module._af_machine_set_speed(this.handle, preset);
  }

  /// How many frames have completed. A caller presents only when this
  /// differs from what it last presented (platform.h's own pull
  /// contract).
  frameGeneration() {
    return this.module._af_machine_frame_generation(this.handle);
  }

  /// A live view into the module's own framebuffer — `width * height`
  /// palette-index bytes. Not a copy: read it (or copy out of it)
  /// immediately, before anything that might grow linear memory.
  framebufferView() {
    const width = this.frameWidth();
    const height = this.frameHeight();
    const ptr = this.module._af_machine_framebuffer(this.handle);
    return this.module.HEAPU8.subarray(ptr, ptr + width * height);
  }

  /// A live view into the palette — `paletteEntries() * 3` bytes, RGB per
  /// entry. Same "read it now" rule as `framebufferView()`.
  paletteView() {
    const entries = this.paletteEntries();
    const ptr = this.module._af_machine_palette(this.handle);
    return this.module.HEAPU8.subarray(ptr, ptr + entries * 3);
  }

  /// Copy `bytes` into the machine's RAM at physical `address`. Answers
  /// an AF_* status.
  writeMemory(address, bytes) {
    const scratch = this.module._malloc(bytes.length);
    if (scratch === 0) {
      throw new Error('out of wasm heap while writing machine memory');
    }
    try {
      this.module.HEAPU8.set(bytes, scratch);
      return this.module._af_machine_write_memory(
        this.handle,
        address,
        scratch,
        bytes.length,
      );
    } finally {
      this.module._free(scratch);
    }
  }

  /// Copy `length` bytes of the machine's RAM at physical `address` back
  /// out, as a fresh `Uint8Array`, or null when the ABI refused — a span
  /// that runs off the end of the megabyte, or no machine at all. The
  /// other half of `writeMemory()`, and the way a caller looks at a
  /// result block a program left behind.
  ///
  /// A copy rather than a view, unlike `framebufferView()`: that one is
  /// a window onto storage the machine keeps for as long as it lives, and
  /// this one is a snapshot of memory the machine goes on changing. Handing
  /// back a subarray of the scratch buffer would hand back a view of heap
  /// this method frees on its way out.
  readMemory(address, length) {
    if (length <= 0) return new Uint8Array(0);
    const scratch = this.module._malloc(length);
    if (scratch === 0) {
      throw new Error('out of wasm heap while reading machine memory');
    }
    try {
      const status = this.module._af_machine_read_memory(
        this.handle,
        address,
        scratch,
        length,
      );
      if (status !== AF_OK) return null;
      return this.module.HEAPU8.slice(scratch, scratch + length);
    } finally {
      this.module._free(scratch);
    }
  }

  /// Set the four entry-point registers a placed program needs
  /// (abi.h — everything else is already at its power-on value).
  setEntry(cs, ip, ss, sp) {
    return this.module._af_machine_set_entry(this.handle, cs, ip, ss, sp);
  }

  /// Post an XT make/break scancode (0x01-0x53, keyboard.h), stamped at
  /// the machine's current virtual time.
  postKey(scancode, down) {
    return this.module._af_machine_post_key(
      this.handle,
      scancode,
      down ? AF_KEY_DOWN : AF_KEY_UP,
    );
  }

  setWallClock(year, month, day, hour, minute, second, centisecond = 0) {
    return this.module._af_machine_set_wall_clock(
      this.handle,
      year,
      month,
      day,
      hour,
      minute,
      second,
      centisecond,
    );
  }

  /// Drain up to `max` bytes of DOS console output, as a fresh
  /// `Uint8Array` copy — code page 437 bytes, not text (platform.h);
  /// transcoding is app.mjs's job, not this one's.
  readConsole(max = 4096) {
    const scratch = this.module._malloc(max);
    if (scratch === 0) {
      throw new Error('out of wasm heap while draining the console');
    }
    try {
      const count = this.module._af_machine_read_console(
        this.handle,
        scratch,
        max,
      );
      return this.module.HEAPU8.slice(scratch, scratch + count);
    } finally {
      this.module._free(scratch);
    }
  }

  consolePending() {
    return this.module._af_machine_console_pending(this.handle);
  }

  consoleDropped() {
    return this.module._af_machine_console_dropped(this.handle);
  }

  /// Drain up to `max` characters of the running diagnostic log, as a
  /// string.
  ///
  /// What a C++ host is handed as records (machine/diagnostics.h) reaches
  /// this one as the lines core renders from them - the same characters
  /// the SDL host prints for the same record, which is the whole point of
  /// the channel (machine/log.h, #108). Notices and seam transitions
  /// always; service calls and file events after `setTrace(true)`.
  ///
  /// ASCII, not code page 437: unlike the console, these lines are
  /// written by core rather than by the program, so there is nothing here
  /// for the app to transcode. A drain can end mid-line, so a caller that
  /// wants whole lines appends what it gets and splits on the newline
  /// (app.mjs does).
  readLog(max = 8192) {
    const scratch = this.module._malloc(max);
    if (scratch === 0) {
      throw new Error('out of wasm heap while draining the log');
    }
    try {
      const count = this.module._af_machine_read_log(this.handle, scratch, max);
      const bytes = this.module.HEAPU8.subarray(scratch, scratch + count);
      let text = '';
      for (const byte of bytes) {
        text += String.fromCharCode(byte);
      }
      return text;
    } finally {
      this.module._free(scratch);
    }
  }

  logPending() {
    return this.module._af_machine_log_pending(this.handle);
  }

  /// Lines the ring had no room for, over the whole run - see abi.h. A
  /// count, not a length: a line that will not fit is dropped whole.
  logDropped() {
    return this.module._af_machine_log_dropped(this.handle);
  }

  /// Throw away what has not been drained. A host's call and not
  /// `reset()`'s: the log is not machine state (machine/log.h).
  clearLog() {
    return this.module._af_machine_clear_log(this.handle);
  }

  /// Pull `frames` mono samples at `sampleRate`. Answers `{ settled,
  /// samples }`: `samples` is always `frames` long (platform.h's "out is
  /// always written in full"), `settled` is how many of those came from
  /// settled virtual time.
  ///
  /// This is the one ABI call platform.h permits off the machine thread —
  /// see audio-worklet.mjs's own top comment for why this host still
  /// calls it on the main thread and posts the result across, which the
  /// contract explicitly allows ("one thread, any thread" includes the
  /// machine's own).
  renderAudio(frames, sampleRate) {
    const byteLength = frames * 4;
    const scratch = this.module._malloc(byteLength);
    if (scratch === 0) {
      throw new Error('out of wasm heap while rendering audio');
    }
    try {
      const settled = this.module._af_machine_render_audio(
        this.handle,
        scratch,
        frames,
        sampleRate,
      );
      const samples = new Float32Array(frames);
      samples.set(
        this.module.HEAPF32.subarray(scratch >> 2, (scratch >> 2) + frames),
      );
      return { settled, samples };
    } finally {
      this.module._free(scratch);
    }
  }

  audioUnderruns() {
    return this.module._af_machine_audio_underruns(this.handle);
  }

  audioResyncs() {
    return this.module._af_machine_audio_resyncs(this.handle);
  }

  // --- The filesystem (M3-F2, #84) --------------------------------------
  //
  // The wasm counterpart of the directory the SDL host is pointed at. A
  // browser cannot hand the core a directory, so it hands it one file at
  // a time.
  //
  // **Nothing here does name logic.** Paths go across as raw text and
  // core canonicalizes them (abi.h): a page that decided for itself what
  // `Save1.Dat` meant would be a second implementation of the rule that
  // says whether two programs are looking at the same file. Since #146
  // that includes the separator — `SAVE/SAVE1.DAT` is handed over with
  // its `/` intact and core decides what it means.

  /// Empty the filesystem. What to call before taking a second directory
  /// from the player.
  vfsClear() {
    return this.module._af_machine_vfs_clear(this.handle);
  }

  /// Put `bytes` (a Uint8Array) at `path` — `START.EXE` at the root,
  /// `SAVE/SAVE1.DAT` a directory down, either separator (#146). The
  /// directories on the way are made in core.
  ///
  /// `AF_INVALID` for a path no DOS path can equal — which is the useful
  /// answer, not a failure: a real game directory has files in it DOS
  /// could never have named, and this is where a caller gets its
  /// "skipped" list.
  vfsPut(path, bytes) {
    return this.#withCString(path, (pathPtr) => {
      const size = bytes ? bytes.length : 0;
      if (size === 0) {
        return this.module._af_machine_vfs_put(this.handle, pathPtr, 0, 0);
      }
      const scratch = this.module._malloc(size);
      if (scratch === 0) {
        throw new Error('out of wasm heap while putting a file');
      }
      try {
        this.module.HEAPU8.set(bytes, scratch);
        return this.module._af_machine_vfs_put(this.handle, pathPtr, scratch, size);
      } finally {
        this.module._free(scratch);
      }
    });
  }

  /// Everything in the root directory, in the VFS's own pinned name
  /// order, as `{ name, size }`. The root and only it, which is what the
  /// ABI offers and why (abi.h) — an entry of size 0 may be a directory
  /// the machine or a `vfsPut` made.
  vfsList() {
    const count = this.module._af_machine_vfs_count(this.handle);
    const entries = [];
    // 13 is `FILENAME.EXT` and its terminator; the ABI refuses a smaller
    // buffer rather than truncating a name.
    const scratch = this.module._malloc(16);
    if (scratch === 0) {
      throw new Error('out of wasm heap while listing the filesystem');
    }
    try {
      for (let i = 0; i < count; ++i) {
        const length = this.module._af_machine_vfs_name_at(this.handle, i, scratch, 16);
        if (length === 0) continue;
        const bytes = this.module.HEAPU8.subarray(scratch, scratch + length);
        entries.push({
          name: String.fromCharCode(...bytes),
          size: this.module._af_machine_vfs_size_at(this.handle, i),
        });
      }
    } finally {
      this.module._free(scratch);
    }
    return entries;
  }

  vfsBytesUsed() {
    return this.module._af_machine_vfs_bytes_used(this.handle);
  }

  /// The SHA-256 of `name` as 64 lowercase hex characters, or null if the
  /// file could not be read. The identity of a player's file (PLAN.md
  /// §2), and the same digest the desktop host prints at load.
  vfsFingerprint(name) {
    return this.#withCString(name, (namePtr) => {
      const scratch = this.module._malloc(72);
      if (scratch === 0) {
        throw new Error('out of wasm heap while fingerprinting');
      }
      try {
        const length = this.module._af_machine_vfs_fingerprint(
          this.handle,
          namePtr,
          scratch,
          72,
        );
        if (length === 0) return null;
        const bytes = this.module.HEAPU8.subarray(scratch, scratch + length);
        return String.fromCharCode(...bytes);
      } finally {
        this.module._free(scratch);
      }
    });
  }

  /// Load an MZ program off the filesystem: relocations, PSP, entry
  /// state. `AF_OK`, or `AF_INVALID` with `loadError()` saying why.
  loadFromVfs(name, commandTail = '') {
    return this.#withCString(name, (namePtr) =>
      this.#withCString(commandTail, (tailPtr) =>
        this.module._af_machine_load_from_vfs(this.handle, namePtr, tailPtr),
      ),
    );
  }

  /// Why the last `loadFromVfs()` failed — the value of
  /// `machine::loader_error`, 0 when the last one succeeded.
  loadError() {
    return this.module._af_machine_load_error(this.handle);
  }

  // --- Identity and seams (M4-F1 #95, M4-F4 #98) -------------------------
  //
  // `loadFromVfs()` identifies the program as it loads it (abi.h), and
  // these read the answer: which known edition it is, or null for one
  // this build does not recognize — in which case no seam is available,
  // which is PLAN.md §5's rule and not a failure — and the seam list, to
  // show and to toggle. Toggling is configuration: a page does it between
  // `runUntil()` calls, never from inside one.

  /// The edition the loaded program is, as a name, or null when it is
  /// unrecognized or nothing is loaded.
  edition() {
    return this.#text((out, max) => this.module._af_machine_edition(this.handle, out, max), 128);
  }

  /// The SHA-256 of the loaded program, or null when nothing is loaded.
  programFingerprint() {
    return this.#text(
      (out, max) => this.module._af_machine_program_fingerprint(this.handle, out, max),
      72,
    );
  }

  /// The whole-state hash right now, as 64 lowercase hex characters, or
  /// null if it could not be taken. The same digest a recording's
  /// checkpoint carries (docs/replay.md §2), so a page can take one at
  /// any moment and compare it against a golden by eye.
  stateHash() {
    return this.#text(
      (out, max) => this.module._af_machine_state_hash(this.handle, out, max),
      72,
    );
  }

  /// Run this machine through `text`, a recording, and answer
  /// `{ ok, report }` — whether it was that run, and the one line saying
  /// what was verified or what differed first and where.
  ///
  /// The machine must be freshly reset with the program loaded and
  /// nothing else done to it; this drives it to the recording's end. The
  /// recording's own speed and seams are applied before they are checked,
  /// because a replay is the run the recording names (docs/replay.md §4).
  ///
  /// The text goes over as bytes rather than as a C string: a recording
  /// is ASCII by construction but it is also long, and passing a length
  /// beats hunting for a terminator in a megabyte.
  verifyRecording(text) {
    const source = String(text ?? '');
    const scratch = this.module._malloc(source.length + 1);
    if (scratch === 0) {
      throw new Error('out of wasm heap while passing a recording');
    }
    const report = this.module._malloc(512);
    if (report === 0) {
      this.module._free(scratch);
      throw new Error('out of wasm heap while passing a recording');
    }
    try {
      const heap = this.module.HEAPU8;
      for (let i = 0; i < source.length; ++i) {
        const code = source.charCodeAt(i);
        heap[scratch + i] = code < 0x100 ? code : 0x3f; // '?'
      }
      heap[scratch + source.length] = 0;
      const status = this.module._af_machine_verify_recording(
        this.handle,
        scratch,
        source.length,
        report,
        512,
      );
      let end = report;
      while (this.module.HEAPU8[end] !== 0 && end < report + 512) ++end;
      const line = String.fromCharCode(
        ...this.module.HEAPU8.subarray(report, end),
      );
      return { ok: status === AF_OK, status, report: line };
    } finally {
      this.module._free(report);
      this.module._free(scratch);
    }
  }

  /// Every seam this build's registry holds, as `{ id, about, state,
  /// reason, armed, fired }`, in registry order. `state` is one of the
  /// AF_SEAM_* values above; `reason` is the spelling core gives it
  /// (`none`, `wrong_binary`, `module_not_resident`, ...).
  ///
  /// `armed` and `fired` are different claims and both are wanted
  /// (#131): `armed` says an address was computed from the seam's fact
  /// table, `fired` says a handler actually ran there. A seam that is on
  /// and armed and has fired nothing is the failure that reads exactly
  /// like success, and until #147 a browser could not say so.
  seamList() {
    const count = this.module._af_machine_seam_count(this.handle);
    const seams = [];
    for (let i = 0; i < count; ++i) {
      seams.push({
        id: this.#text((out, max) => this.module._af_machine_seam_id(this.handle, i, out, max), 64) ?? '',
        about: this.#text((out, max) => this.module._af_machine_seam_about(this.handle, i, out, max), 256) ?? '',
        state: this.module._af_machine_seam_state(this.handle, i),
        reason: this.#text((out, max) => this.module._af_machine_seam_reason(this.handle, i, out, max), 64) ?? '',
        armed: this.module._af_machine_seam_armed(this.handle, i) !== 0,
        fired: this.module._af_machine_seam_fired(this.handle, i),
      });
    }
    return seams;
  }

  /// Turn a seam on or off by id. `AF_OK` if it took, `AF_INVALID` if
  /// there is no such seam or it is unavailable — `seamList()` says why.
  seamEnable(id) {
    return this.#withCString(id, (ptr) => this.module._af_machine_seam_enable(this.handle, ptr));
  }

  seamDisable(id) {
    return this.#withCString(id, (ptr) => this.module._af_machine_seam_disable(this.handle, ptr));
  }

  // --- Marshalling ------------------------------------------------------

  /// Run one of the "write a NUL-terminated string into my buffer" calls
  /// and bring the characters back, or null when it answered zero. The
  /// strings are ASCII by construction (ids, names, reasons).
  #text(call, capacity) {
    const scratch = this.module._malloc(capacity);
    if (scratch === 0) {
      throw new Error('out of wasm heap while reading a string');
    }
    try {
      const length = call(scratch, capacity);
      if (length === 0) return null;
      const bytes = this.module.HEAPU8.subarray(scratch, scratch + length);
      return String.fromCharCode(...bytes);
    } finally {
      this.module._free(scratch);
    }
  }

  /// Call `use` with `text` in linear memory as a NUL-terminated C
  /// string, and free it afterwards however `use` ends.
  ///
  /// Latin-1 rather than UTF-8, deliberately: what is on the other side
  /// is a DOS short name, whose legal character set is a subset of ASCII
  /// (machine/vfs.h), and a multi-byte encoding of something outside it
  /// would arrive as several bytes core would then reject one at a time.
  /// A character past 0xFF is replaced with one core will refuse, so an
  /// illegal name is refused as an illegal name rather than silently
  /// becoming a different legal one.
  #withCString(text, use) {
    const source = String(text ?? '');
    const scratch = this.module._malloc(source.length + 1);
    if (scratch === 0) {
      throw new Error('out of wasm heap while passing a string');
    }
    try {
      const heap = this.module.HEAPU8;
      for (let i = 0; i < source.length; ++i) {
        const code = source.charCodeAt(i);
        heap[scratch + i] = code < 0x100 ? code : 0x3f; // '?'
      }
      heap[scratch + source.length] = 0;
      return use(scratch);
    } finally {
      this.module._free(scratch);
    }
  }

  /// Run one of the two report calls into a scratch buffer and bring the
  /// characters back as a string. The reports are plain ASCII by
  /// construction (machine/report.h), so byte per character is exact.
  #report(call, capacity) {
    const scratch = this.module._malloc(capacity);
    if (scratch === 0) {
      throw new Error('out of wasm heap while reading a report');
    }
    try {
      const length = call(scratch, capacity);
      const bytes = this.module.HEAPU8.subarray(scratch, scratch + length);
      let out = '';
      // In chunks: `String.fromCharCode(...bytes)` on a full trace report
      // is twenty thousand arguments, which is past what some engines
      // accept in a spread call.
      for (let i = 0; i < bytes.length; i += 4096) {
        out += String.fromCharCode(...bytes.subarray(i, i + 4096));
      }
      return out;
    } finally {
      this.module._free(scratch);
    }
  }
}

// --- Pacing: virtual time against the wall (#157) --------------------------
//
// PLAN.md §4's rule is that host wall time only throttles presentation,
// and the SDL host honours it by running exactly one frame of virtual
// time per iteration and then sleeping whatever wall time is left over
// (hosts/sdl/src/main.cpp's top comment). Its corollary is the important
// half: a host that falls behind simply does not sleep, and never runs
// the machine faster to compensate.
//
// A browser cannot copy that loop, because it does not own the cadence.
// `requestAnimationFrame` fires at the **display's** refresh rate, so the
// dev page's old "one 60 Hz frame of virtual time per callback" ran the
// machine at `refresh / 60` times real time — 4x on a 240 Hz monitor,
// compounding with whatever speed preset was selected, which is #157.
//
// So the arithmetic is turned round. rAF hands the callback a
// `DOMHighResTimeStamp`; the machine is advanced by the virtual time that
// *elapsed real time* implies, and the display's rate decides only how
// finely that advance is chopped up. A hundred callbacks at 240 Hz and
// twenty-five at 60 Hz advance the same virtual time, because they cover
// the same wall time.
//
// Absolute and not a duration, still: the caller keeps `tick` as a point
// on its own schedule and this returns the next point, so the overshoot
// of a slice — a step is indivisible — never accumulates into drift
// (abi.h's note on `af_machine_run_until`).

/// The most virtual time one callback may advance, in seconds, however
/// much wall time went by.
///
/// It has to exist. A backgrounded tab, a breakpoint, a sleeping laptop
/// or a slow first paint hands the next callback an arbitrarily large
/// delta, and running the machine forward by all of it is exactly the
/// burst of emulated instructions the SDL host's loop is written to
/// forbid. It is also what makes a host that *cannot* keep up settle
/// rather than diverge: without a cap, each callback that overran would
/// ask the next one for more virtual time than the last, and the page
/// would freeze for longer and longer.
///
/// A tenth of a second — six 60 Hz frames. Long enough that ordinary
/// jitter is absorbed rather than lost (a dropped frame or two, a GC
/// pause, an rAF throttled to 30 Hz on battery), so virtual time really
/// does track the wall on any display a person is looking at; short
/// enough that anything past it is not jitter but a stall, and a stall is
/// dropped. What is dropped is *not* remembered: virtual time falls
/// behind the wall and stays behind, which is precisely what the desktop
/// host does when it declines to sleep.
export const MAX_CATCH_UP_SECONDS = 0.1;

/// Where to run virtual time to, given where it is and how much wall time
/// has passed since the last callback.
///
/// `since` is the previous callback's `DOMHighResTimeStamp` and `now` is
/// this one's, both in milliseconds; `since` is null on the first
/// callback, which anchors the clock and advances nothing. A delta that
/// is not a positive finite number — a first frame, a clock that went
/// backwards — advances nothing rather than guessing at one.
///
/// Answers the next absolute tick, the advance it represents, and whether
/// the clamp was reached, which is a fact about the host worth showing
/// rather than swallowing.
export function pacedAdvance({
  tick,
  since,
  now,
  ticksPerSecond,
  maxCatchUpSeconds = MAX_CATCH_UP_SECONDS,
}) {
  const limit =
    Number.isFinite(maxCatchUpSeconds) && maxCatchUpSeconds > 0
      ? maxCatchUpSeconds * ticksPerSecond
      : MAX_CATCH_UP_SECONDS * ticksPerSecond;
  const elapsedMs =
    Number.isFinite(since) && Number.isFinite(now) && now > since ? now - since : 0;
  const wanted = (elapsedMs / 1000) * ticksPerSecond;
  const advance = Math.min(wanted, limit);
  return { tick: tick + advance, advance, clamped: wanted > limit };
}

// --- The embedded demo program (M2-H2, #55) -------------------------------
//
// hosts/web/src/demo_program.h is the source of truth for these four
// numbers; they are restated here for the same reason abi.h's status
// codes are restated above — a JS host has no headers to include them
// from. Keep them in step if that file's constants ever move.

const DEMO_PROGRAM_ADDRESS = 0x10000;
const DEMO_PROGRAM_CS = 0x1000;
const DEMO_PROGRAM_IP = 0x0000;
const DEMO_PROGRAM_SS = 0x1000;
const DEMO_PROGRAM_SP = 0xfffe;

/// Place the module's embedded demo program (hosts/web/src/demo_program.cpp)
/// into `machine`'s RAM and point CS:IP/SS:SP at it — the same
/// `af_machine_write_memory()` + `af_machine_set_entry()` pattern
/// abi.h documents as the way a self-written program gets placed until
/// the MZ loader is wired up to this ABI. Both the page (app.mjs) and
/// tests/smoke.mjs call this, so there is exactly one place that knows
/// how the demo program gets into a machine.
///
/// Requires `machine.attachReferenceDevices()` to have already been
/// called: the program uses INT 10h, INT 16h and INT 21h, none of which
/// exist on a bare machine.
export function loadDemoProgram(machine) {
  const module = machine.module;
  const ptr = module._af_web_demo_program_bytes();
  const size = module._af_web_demo_program_size();
  const bytes = module.HEAPU8.slice(ptr, ptr + size);

  const writeStatus = machine.writeMemory(DEMO_PROGRAM_ADDRESS, bytes);
  const entryStatus = machine.setEntry(
    DEMO_PROGRAM_CS,
    DEMO_PROGRAM_IP,
    DEMO_PROGRAM_SS,
    DEMO_PROGRAM_SP,
  );
  return { writeStatus, entryStatus, size };
}

// --- Keyboard: browser code -> XT scancode --------------------------------
//
// keyboard.h's `xt_keyboard::xt_table` is the authority this mirrors: XT
// scan code set 1, make codes 0x01-0x53, the 83-key IBM PC/XT keyboard.
// Keyed by `KeyboardEvent.code` (the physical key, independent of layout
// or modifier state — exactly what a scancode is) rather than `.key`
// (which reports the *character*, and would require guessing the
// scancode back out of it).
//
// The 83-key XT keyboard has exactly one Ctrl key and one Alt key — no
// left/right distinction exists in hardware for them, unlike Shift, which
// genuinely has two. Both browser variants of Ctrl and Alt therefore map
// to the same single scancode; there is nothing else it could mean on
// this machine. Keys the 83-key board never had (F11/F12, a numpad
// Enter, a right Ctrl/Alt as distinct keys) are simply absent — the same
// "not modelled, not guessed" gap keyboard.h documents.
//
// The arrow, Home/End, Page and Insert/Delete rows are the same keys as
// the numeric keypad, and that is not an approximation: on an 83-key
// board there *is* no separate cursor pad, and a program reads 48h for
// "up" whether the player pressed Numpad8 or the arrow a later keyboard
// added. Mapping them onto the same scancodes is what a real XT keyboard
// does; leaving them out, which is what this table did until M3-F2
// (#84), is what made a keyboard-driven game unplayable in a browser
// while the desktop host had had them since M2-H1.
export const XT_SCANCODES = Object.freeze({
  Escape: 0x01,
  Digit1: 0x02,
  Digit2: 0x03,
  Digit3: 0x04,
  Digit4: 0x05,
  Digit5: 0x06,
  Digit6: 0x07,
  Digit7: 0x08,
  Digit8: 0x09,
  Digit9: 0x0a,
  Digit0: 0x0b,
  Minus: 0x0c,
  Equal: 0x0d,
  Backspace: 0x0e,
  Tab: 0x0f,
  KeyQ: 0x10,
  KeyW: 0x11,
  KeyE: 0x12,
  KeyR: 0x13,
  KeyT: 0x14,
  KeyY: 0x15,
  KeyU: 0x16,
  KeyI: 0x17,
  KeyO: 0x18,
  KeyP: 0x19,
  BracketLeft: 0x1a,
  BracketRight: 0x1b,
  Enter: 0x1c,
  ControlLeft: 0x1d,
  ControlRight: 0x1d,
  KeyA: 0x1e,
  KeyS: 0x1f,
  KeyD: 0x20,
  KeyF: 0x21,
  KeyG: 0x22,
  KeyH: 0x23,
  KeyJ: 0x24,
  KeyK: 0x25,
  KeyL: 0x26,
  Semicolon: 0x27,
  Quote: 0x28,
  Backquote: 0x29,
  ShiftLeft: 0x2a,
  Backslash: 0x2b,
  KeyZ: 0x2c,
  KeyX: 0x2d,
  KeyC: 0x2e,
  KeyV: 0x2f,
  KeyB: 0x30,
  KeyN: 0x31,
  KeyM: 0x32,
  Comma: 0x33,
  Period: 0x34,
  Slash: 0x35,
  ShiftRight: 0x36,
  NumpadMultiply: 0x37,
  AltLeft: 0x38,
  AltRight: 0x38,
  Space: 0x39,
  CapsLock: 0x3a,
  F1: 0x3b,
  F2: 0x3c,
  F3: 0x3d,
  F4: 0x3e,
  F5: 0x3f,
  F6: 0x40,
  F7: 0x41,
  F8: 0x42,
  F9: 0x43,
  F10: 0x44,
  NumLock: 0x45,
  ScrollLock: 0x46,
  Numpad7: 0x47,
  Numpad8: 0x48,
  Numpad9: 0x49,
  NumpadSubtract: 0x4a,
  Numpad4: 0x4b,
  Numpad5: 0x4c,
  Numpad6: 0x4d,
  NumpadAdd: 0x4e,
  Numpad1: 0x4f,
  Numpad2: 0x50,
  Numpad3: 0x51,
  Numpad0: 0x52,
  NumpadDecimal: 0x53,

  // The cursor pad, onto the keypad codes it shares with an 83-key
  // board — see the note above.
  Home: 0x47,
  ArrowUp: 0x48,
  PageUp: 0x49,
  ArrowLeft: 0x4b,
  ArrowRight: 0x4d,
  End: 0x4f,
  ArrowDown: 0x50,
  PageDown: 0x51,
  Insert: 0x52,
  Delete: 0x53,
});

/// `KeyboardEvent.code` -> XT scancode, or `undefined` for a key the
/// 83-key board never had (this file's own top comment on `XT_SCANCODES`).
export function scancodeFor(code) {
  return XT_SCANCODES[code];
}

// --- Console bytes -> text -------------------------------------------------

/// DOS console output is code page 437 bytes, not text (platform.h);
/// transcoding is the host's decision. This one keeps the printable
/// ASCII range as itself and renders anything else — the CP437 graphics
/// range, control codes the demo program never sends — as a visible
/// escape, which is enough for a bare dev page that only ever echoes
/// plain keystrokes back.
export function decodeConsoleBytes(bytes) {
  let out = '';
  for (const byte of bytes) {
    if (byte === 0x0a || byte === 0x0d || (byte >= 0x20 && byte < 0x7f)) {
      out += String.fromCharCode(byte);
    } else {
      out += `\\x${byte.toString(16).padStart(2, '0')}`;
    }
  }
  return out;
}
