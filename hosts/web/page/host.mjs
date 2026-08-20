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
export const AF_UNIMPLEMENTED = 4;

export const AF_KEY_UP = 0;
export const AF_KEY_DOWN = 1;

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

  /// Run virtual time up to `tick` (absolute, not a duration — abi.h's
  /// own run-loop snippet is `next += ticksPerSecond() / 60;
  /// runUntil(next)`, and that is exactly what app.mjs's frame callback
  /// does). Answers one of the AF_* status constants above.
  runUntil(tick) {
    return this.module._af_machine_run_until(this.handle, tick);
  }

  time() {
    return this.module._af_machine_time(this.handle);
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
