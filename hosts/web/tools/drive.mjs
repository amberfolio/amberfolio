// SPDX-License-Identifier: AGPL-3.0-only
//
// The headless web driver — the wasm host's counterpart to the SDL host's
// `--press` / `--until` / `--dump`, run under node against the same wasm
// module a browser fetches.
//
//     node drive.mjs <dir> <PROGRAM.EXE> [options]
//
// It exists because the two hosts have to be checkable against each other
// and only one of them could be scripted. M3's exit criterion and M4's
// legs are both stated as a comparison — the same program reaching the
// same stop line at the same step on the desktop and in the browser
// (#84, docs/hosts.md §4) — and until now the desktop half was one
// command and the web half was a person clicking through a page. A run
// nobody can spell cannot be put in a script, diffed, or repeated a week
// later, which is most of what "parity" is worth.
//
// This is the dev page's own run loop with the browser taken out: the
// same `host.mjs` `Machine`, the same per-frame cadence abi.h documents,
// the same audio pull, the same log drain. What it does not have is
// `requestAnimationFrame`, so it runs the loop as fast as the module
// will go — which is what makes the throughput number at the bottom
// mean something (see "Throughput" below).
//
//
// It takes a directory, and reads nothing else
// --------------------------------------------
//
// Exactly as the SDL host does. The caller names a directory; this reads
// what is in it — and, since #146, what is in the directories below it —
// hands each file to the machine's own filesystem under its path
// relative to that directory, and keeps nothing. **No game content is in
// this file, and none may ever be** — not bytes, not file names, not
// screen text (CONTRIBUTING.md's clean-content rule). Every name this
// program prints it learned from the caller's disk a moment earlier, and
// prints to the caller's own terminal.
//
// The consequence worth stating: nothing in this repository runs it
// against a game. `hosts/web/tests/smoke.mjs` drives it against
// `tests/sessions/spin/`, which is the repository's own 34-byte disk, and
// the real thing is a procedure a person carries out against their own
// copy (docs/hosts.md).
//
//
// Frames are the unit
// -------------------
//
// `--press KEY@FRAME` counts in 60 Hz frames of *virtual* time, and so
// does `--dump-every`, for the same reason the SDL host does: a keystroke
// and the still that shows what it did should be named in the same units,
// and virtual frames are the only units both hosts agree on. One frame is
// `af_ticks_per_second() / 60` ticks, advanced by exactly one slice per
// iteration — abi.h's own run-loop snippet, which is also what app.mjs
// does. A key is posted at the top of its frame, before that frame's
// slice runs.
//
// One budget is coarser here than on the desktop, and the difference is
// the ABI's rather than this program's: `af_machine_run_until` takes a
// tick and there is no step-bounded run call, so `--steps N` is checked
// between slices and ends on the first frame boundary at or past N. The
// SDL host clamps a step budget into the slice and ends on step N
// exactly. `--until` and `--frames` are exact to the tick the machine's
// own step granularity allows, and the stop report always says which
// step it actually ended on — so a comparison is still made on a number
// both hosts state rather than on one either of them rounded.
//
//
// Throughput
// ----------
//
// The one thing this tool measures rather than reports. #116 was closed
// on the strength of an out-of-tree number — a Release wasm module is
// about nine times a Debug one — and there was no instrument in the tree
// that produced it. This is that instrument: wall-clock nanoseconds
// around the run loop against the virtual seconds the loop covered,
// printed as a real-time factor.
//
// Wall time appears here and nowhere near `core/` (platform.h,
// `scripts/check-host-time.sh`): pacing and measurement are a host's
// business, and this host does not pace at all. So the factor is a
// ceiling — how much faster than real time this module *can* run this
// program on this machine — and not what a browser will do, where the
// page holds virtual time to the wall clock (app.mjs's run loop, #157).
// A factor comfortably above 1 is the headroom #107's default speed
// preset needs; a factor below 1 means a browser cannot keep up and the
// number says by how much.

import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';

import {
  loadAmberfolio,
  Machine,
  scancodeFor,
  decodeConsoleBytes,
  SPEED_PRESETS,
  AF_OK,
  describeSkip,
  anySkipLostAFile,
  AF_SEAM_OFF,
  AF_SEAM_ON,
  AF_SEAM_UNAVAILABLE,
  formatSeamFired,
  AF_RUN_END_STOPPED,
  AF_RUN_END_STEP_BUDGET,
  AF_RUN_END_TICK_BUDGET,
} from './host.mjs';

/// `AF_SEAM_*` as core spells the states (`machine::seam_state_name`), so
/// the seam table below reads exactly like the SDL host's `--seams`.
const SEAM_STATE_NAMES = Object.freeze({
  [AF_SEAM_OFF]: 'off',
  [AF_SEAM_ON]: 'on',
  [AF_SEAM_UNAVAILABLE]: 'unavailable',
});

/// The page's own audio cadence, so the loop this measures is the loop a
/// browser runs (app.mjs).
const AUDIO_SAMPLE_RATE = 44100;

/// A minute of it, at most, when `--dump` asks for a WAV — the same bound
/// and the same `(truncated)` the SDL host's capture has.
const AUDIO_CAPTURE_LIMIT = AUDIO_SAMPLE_RATE * 60;

// --- Key names -----------------------------------------------------------
//
// The scancode table lives in host.mjs and stays there: `scancodeFor()`
// is the only thing in this program allowed to decide what a key *is*.
// What is here is spelling, and only spelling.
//
// A `--press` name is tried first as a `KeyboardEvent.code` — the
// browser's own name for a physical key, and the name the dev page works
// in. If that is not a key, it is tried as one of SDL's scancode names,
// because that is how every key in `docs/playable.md` is written down: a
// leg recorded there as `--press Return@9100` should be drivable here by
// typing the same thing, or the two hosts have parity in the machine and
// none in the procedure. The alias below turns SDL's spelling into the
// browser's and hands it to `scancodeFor()`; it decides no scancodes, and
// a key it does not know is refused rather than guessed at.

const SDL_KEY_ALIASES = Object.freeze({
  Return: 'Enter',
  Escape: 'Escape',
  Backspace: 'Backspace',
  Tab: 'Tab',
  Space: 'Space',
  Left: 'ArrowLeft',
  Right: 'ArrowRight',
  Up: 'ArrowUp',
  Down: 'ArrowDown',
  Home: 'Home',
  End: 'End',
  PageUp: 'PageUp',
  PageDown: 'PageDown',
  Insert: 'Insert',
  Delete: 'Delete',
  CapsLock: 'CapsLock',
  Numlock: 'NumLock',
  ScrollLock: 'ScrollLock',
  '-': 'Minus',
  '=': 'Equal',
  '[': 'BracketLeft',
  ']': 'BracketRight',
  '\\': 'Backslash',
  ';': 'Semicolon',
  "'": 'Quote',
  '`': 'Backquote',
  ',': 'Comma',
  '.': 'Period',
  '/': 'Slash',
  'Left Shift': 'ShiftLeft',
  'Right Shift': 'ShiftRight',
  'Left Ctrl': 'ControlLeft',
  'Left Alt': 'AltLeft',
  'Keypad +': 'NumpadAdd',
  'Keypad -': 'NumpadSubtract',
  'Keypad *': 'NumpadMultiply',
  'Keypad .': 'NumpadDecimal',
});

/// A `--press` key name to an XT scancode, or `undefined` for a key the
/// 83-key board never had. Browser spelling first, then SDL's.
export function keyNameToScancode(name) {
  const direct = scancodeFor(name);
  if (direct !== undefined) return direct;

  // The mechanical half of SDL's spelling: a bare letter or digit is its
  // own scancode name there, and `Keypad 5` is the keypad's.
  if (/^[A-Za-z]$/.test(name)) return scancodeFor(`Key${name.toUpperCase()}`);
  if (/^[0-9]$/.test(name)) return scancodeFor(`Digit${name}`);
  const keypad = /^Keypad ([0-9])$/.exec(name);
  if (keypad) return scancodeFor(`Numpad${keypad[1]}`);

  const alias = SDL_KEY_ALIASES[name];
  return alias === undefined ? undefined : scancodeFor(alias);
}

// --- The command line ----------------------------------------------------

const USAGE = `usage: node drive.mjs <dir> <PROGRAM.EXE> [options]

  --frames N            run N frames of virtual time at 60 Hz
  --until TICKS         run until virtual tick TICKS
  --steps N             stop at the first frame boundary at or past step N
  --press KEY@FRAME     post a key at the top of frame FRAME (repeatable)
  --pull ID@FRAME       pull a seam's trigger at the top of frame FRAME
                        (repeatable; the seam has to be on and to be one
                        that takes a trigger)
  --seam ID             turn one seam on before the first step (repeatable)
  --seams               list every seam this build carries, and exit
  --vfs-list            list every file on the disk after the run, at its
                        own path — what the run left behind, including
                        anything the program wrote below the root
  --speed xt|turbo|at|386
  --trace               keep the trace ring and the service-call channel
  --dump PREFIX         write PREFIX.ppm, PREFIX.wav and PREFIX.edges
  --dump-every N        also write PREFIX-NNNNNN.ppm every N frames
  --quiet               only the report lines, no per-frame console output
  -- ARGUMENTS          everything after -- becomes the command tail

With no bound the run ends when the machine does, which for a program
that never stops is never; give it a --frames, --until or --steps.`;

/// Parse `argv` (everything after the script name) into the run this
/// program is about, or `{ error }` saying what was wrong with it.
/// Exported so `hosts/web/tests/smoke.mjs` can check the parsing without
/// standing a machine up.
export function parseArgs(argv) {
  const opts = {
    dir: null,
    program: null,
    frames: 0,
    until: 0,
    steps: 0,
    presses: [],
    pulls: [],
    seams: [],
    listSeams: false,
    listVfs: false,
    speed: null,
    trace: false,
    dumpPrefix: null,
    dumpEvery: 0,
    quiet: false,
    tail: '',
  };
  const positional = [];

  const count = (text, what) => {
    const value = Number(text);
    if (!Number.isFinite(value) || value <= 0) {
      return { error: `${what} wants a positive number, got ${text}` };
    }
    return { value };
  };

  for (let i = 0; i < argv.length; ++i) {
    const arg = argv[i];
    const next = () => argv[++i];
    if (arg === '--') {
      // The command tail, with the single leading space DOS's own
      // command-line parsing leaves in front of one — the same shape the
      // SDL host passes to the loader.
      const rest = argv.slice(i + 1).join(' ');
      opts.tail = rest === '' ? '' : ` ${rest}`;
      break;
    } else if (arg === '--frames' && i + 1 < argv.length) {
      const parsed = count(next(), '--frames');
      if (parsed.error) return parsed;
      opts.frames = parsed.value;
    } else if (arg === '--until' && i + 1 < argv.length) {
      const parsed = count(next(), '--until');
      if (parsed.error) return parsed;
      opts.until = parsed.value;
    } else if (arg === '--steps' && i + 1 < argv.length) {
      const parsed = count(next(), '--steps');
      if (parsed.error) return parsed;
      opts.steps = parsed.value;
    } else if (arg === '--press' && i + 1 < argv.length) {
      const text = next();
      const at = text.lastIndexOf('@');
      if (at <= 0) return { error: `--press wants KEY@FRAME, as in A@60; got ${text}` };
      const name = text.slice(0, at);
      const frame = Number(text.slice(at + 1));
      if (!Number.isInteger(frame) || frame < 0) {
        return { error: `--press wants KEY@FRAME with a frame number; got ${text}` };
      }
      const scancode = keyNameToScancode(name);
      if (scancode === undefined) {
        return {
          error:
            `--press does not know the key ${JSON.stringify(name)}. Names are ` +
            "the browser's (KeyA, Enter, ArrowUp) or SDL's (A, Return, Up); " +
            'keys the 83-key XT board never had have neither.',
        };
      }
      opts.presses.push({ name, frame, scancode });
    } else if (arg === '--pull' && i + 1 < argv.length) {
      // The SDL host's `--pull ID@FRAME` (#161), spelled identically so
      // a leg written down in docs/playable.md can be typed at either
      // host unchanged. Split on the last `@`, as `--press` is.
      const text = next();
      const at = text.lastIndexOf('@');
      if (at <= 0) return { error: `--pull wants ID@FRAME, as in cheat-kill-all@600; got ${text}` };
      const id = text.slice(0, at);
      const frame = Number(text.slice(at + 1));
      if (!Number.isInteger(frame) || frame < 0) {
        return { error: `--pull wants ID@FRAME with a frame number; got ${text}` };
      }
      opts.pulls.push({ id, frame });
    } else if (arg === '--seam' && i + 1 < argv.length) {
      opts.seams.push(next());
    } else if (arg === '--vfs-list') {
      opts.listVfs = true;
    } else if (arg === '--seams') {
      opts.listSeams = true;
    } else if (arg === '--speed' && i + 1 < argv.length) {
      const name = next();
      if (!Object.hasOwn(SPEED_PRESETS, name)) {
        return { error: `--speed wants xt, turbo, at or 386; got ${name}` };
      }
      opts.speed = name;
    } else if (arg === '--trace') {
      opts.trace = true;
    } else if (arg === '--dump' && i + 1 < argv.length) {
      opts.dumpPrefix = next();
    } else if (arg === '--dump-every' && i + 1 < argv.length) {
      const parsed = count(next(), '--dump-every');
      if (parsed.error) return parsed;
      opts.dumpEvery = parsed.value;
    } else if (arg === '--quiet') {
      opts.quiet = true;
    } else if (arg.startsWith('--')) {
      return { error: `unknown option ${arg}` };
    } else {
      positional.push(arg);
    }
  }

  if (positional.length !== 2) {
    return { error: 'a directory and a program name are both required' };
  }
  if (opts.dumpEvery !== 0 && opts.dumpPrefix === null) {
    return { error: '--dump-every needs --dump, whose prefix it shares' };
  }
  opts.dir = positional[0];
  opts.program = positional[1];
  return opts;
}

// --- Output --------------------------------------------------------------
//
// Everything goes to stdout, including the lines the SDL host writes to
// stderr. That is a deliberate difference and not an oversight: this
// program has no program output to keep separate — what a DOS program
// writes to its console comes through `readConsole()` and is printed
// here, prefixed, like the dev page's `<pre>` does it. To diff a run
// against a desktop one, redirect the desktop host's stderr:
//
//     amberfolio <dir> P.EXE --headless 2>desktop.txt
//     node drive.mjs <dir> P.EXE --frames N >web.txt
//     diff <(grep '^amberfolio: stop' desktop.txt) \
//          <(grep '^amberfolio: stop' web.txt)

function say(text) {
  process.stdout.write(`${text}\n`);
}

/// A PPM of one frame: palette indices through the palette, as the same
/// P6 the SDL host's `--dump` writes, so two hosts' pictures compare byte
/// for byte.
export function encodePpm(width, height, pixels, palette) {
  const body = Buffer.alloc(width * height * 3);
  for (let i = 0; i < width * height; ++i) {
    const entry = pixels[i] * 3;
    body[i * 3] = palette[entry];
    body[i * 3 + 1] = palette[entry + 1];
    body[i * 3 + 2] = palette[entry + 2];
  }
  return Buffer.concat([Buffer.from(`P6\n${width} ${height}\n255\n`), body]);
}

/// A 16-bit mono WAV of `samples` at `rate`. The speaker is one cone
/// (platform.h), so one channel; 16-bit because that is what the SDL
/// host's capture writes and the point of writing one at all is that the
/// two can be compared.
export function encodeWav(samples, rate) {
  const header = Buffer.alloc(44);
  const bytes = samples.length * 2;
  header.write('RIFF', 0, 'ascii');
  header.writeUInt32LE(36 + bytes, 4);
  header.write('WAVEfmt ', 8, 'ascii');
  header.writeUInt32LE(16, 16); // PCM chunk size
  header.writeUInt16LE(1, 20); // PCM
  header.writeUInt16LE(1, 22); // mono
  header.writeUInt32LE(rate, 24);
  header.writeUInt32LE(rate * 2, 28); // byte rate
  header.writeUInt16LE(2, 32); // block align
  header.writeUInt16LE(16, 34); // bits per sample
  header.write('data', 36, 'ascii');
  header.writeUInt32LE(bytes, 40);

  const body = Buffer.alloc(bytes);
  for (let i = 0; i < samples.length; ++i) {
    const clamped = Math.max(-1, Math.min(1, samples[i]));
    body.writeInt16LE(Math.round(clamped * 32767), i * 2);
  }
  return Buffer.concat([header, body]);
}

// --- The run -------------------------------------------------------------

/// Put every file under `dir` into `machine`'s filesystem, one at a time
/// — which is what a browser has to do, and so is what this does even
/// though node could hand over a directory. Answers what went in and what
/// the machine refused.
///
/// **Subdirectories are walked** (#146). Until the ABI's door took a path
/// this reported `disk skipped SAVE (not a file)` and went on without it,
/// which is where every shipped save slot lives — so a browser could
/// start a game and never resume one. What goes across now is the path
/// relative to `dir`, and core makes the directories it names.
///
/// The separator handed over is always `/`, never `join()`'s: this runs
/// on three operating systems and the path a run is recorded against
/// must not depend on which. Core takes either spelling and canonicalizes
/// (abi.h), so `/` here and `\` in a `docs/playable.md` line are one
/// path.
///
/// A refusal is the useful answer and not a failure: a real installation
/// has files in it DOS could never have named, and core's own
/// canonicalizer is the one thing entitled to say which (abi.h). A
/// directory with nothing in it simply never comes up — a VFS holds what
/// a run reads, and nothing reads an empty directory.
///
/// **Except when the refusal is "no room" (#158).** That one is not the
/// machine working; it is a file the program will ask for later that is
/// not on the disk it was handed. This used to print `(status 3)` for
/// both, and the run in which it was found reported seven of a game's
/// data files in the same words as a PDF it was right to ignore. So each
/// skipped line names its reason, and `incomplete` says whether any of
/// them is a hole rather than a filter.
function putDirectory(machine, dir) {
  const skipped = [];
  const statuses = [];
  let taken = 0;

  const walk = (at, prefix) => {
    for (const entry of readdirSync(at, { withFileTypes: true })) {
      const path = prefix === '' ? entry.name : `${prefix}/${entry.name}`;
      if (entry.isDirectory()) {
        walk(join(at, entry.name), path);
        continue;
      }
      if (!entry.isFile()) {
        skipped.push(`${path} (not a file)`);
        continue;
      }
      const bytes = new Uint8Array(readFileSync(join(at, entry.name)));
      const status = machine.vfsPut(path, bytes);
      if (status === AF_OK) {
        taken += 1;
        continue;
      }
      statuses.push(status);
      skipped.push(`${path} (${describeSkip(status)})`);
    }
  };

  walk(dir, '');
  return { taken, skipped, incomplete: anySkipLostAFile(statuses) };
}

/// Stand a machine up, run the program, and report. Answers the process
/// exit code.
export async function drive(opts) {
  const { module } = await loadAmberfolio({
    print: (text) => say(`  [module] ${text}`),
    printErr: (text) => say(`  [module] ${text}`),
  });

  const machine = new Machine(module);
  if (machine.attachReferenceDevices() !== AF_OK) {
    say('amberfolio: the reference devices could not be attached');
    machine.destroy();
    return 1;
  }
  // The RESET line, before anything is loaded — the same thing the SDL
  // host's `wired_machine` and the dev page's `ensureMachine()` do. It is
  // not decoration: it blanks and republishes the frame, so a machine
  // that skipped it is one frame behind a desktop run of the same program
  // forever after, and `frames=` in the stop report is where that shows
  // (docs/hosts.md §4).
  machine.reset();

  const { taken, skipped, incomplete } = putDirectory(machine, opts.dir);
  say(
    `amberfolio: disk files=${taken} skipped=${skipped.length} ` +
      `bytes=${machine.vfsBytesUsed()}`,
  );
  for (const name of skipped) say(`amberfolio: disk skipped ${name}`);
  // Loud, and separately from the per-file lines, because this one is
  // not a list of things that were right to leave out: the disk did not
  // fit, and whatever it is missing the program will ask for eventually
  // and get a file-not-found for. The run still goes ahead — it is the
  // caller's disk and the caller's decision — but nobody should read the
  // rest of this output without knowing (#158).
  if (incomplete) {
    say(
      'amberfolio: disk INCOMPLETE - the filesystem ran out of room, so ' +
        'files above are missing from the machine, not merely from DOS',
    );
  }

  // The identity of the file, before anything executes — a fact about it
  // and never anything out of it (PLAN.md §2), and the same line the SDL
  // host prints at load.
  const digest = machine.vfsFingerprint(opts.program);
  say(`amberfolio: load ${opts.program} sha256=${digest ?? 'unreadable'}`);

  // Asked for before the load, so the ring covers the whole run rather
  // than starting a few instructions into it (trace.h).
  machine.setTrace(opts.trace);

  const loadStatus = machine.loadFromVfs(opts.program, opts.tail);
  if (loadStatus !== AF_OK) {
    say(
      `amberfolio: cannot load ${opts.program} (status ${loadStatus}, ` +
        `loader error ${machine.loadError()})`,
    );
    machine.destroy();
    return 1;
  }

  const edition = machine.edition();
  say(
    edition === null
      ? 'amberfolio: edition unrecognized - no seams are available for this program'
      : `amberfolio: edition ${edition}`,
  );

  if (opts.speed !== null) {
    machine.setSpeed(SPEED_PRESETS[opts.speed]);
    say(`amberfolio: speed ${opts.speed}`);
  }

  // The seams the run was asked for, enabled after the load and before
  // the first step: a seam is keyed on the program's fingerprint, so
  // there has to be a program, and a run with a seam on is not the same
  // run as one without it. A refusal ends the run rather than being
  // shrugged at — a script that asked for the cheats seam and silently
  // got a plain machine would be the worst possible outcome of this
  // whole apparatus (PLAN.md §5).
  for (const id of opts.seams) {
    if (machine.seamEnable(id) !== AF_OK) {
      const row = machine.seamList().find((seam) => seam.id === id);
      say(
        `amberfolio: seam ${id} refused (${row?.reason ?? 'no seam by that name'})`,
      );
      reportSeams(machine);
      machine.destroy();
      return 1;
    }
  }

  if (opts.listSeams) {
    // A listing is a question, and the answer is the whole of what was
    // asked for — nothing runs. The SDL host's `--seams`, in the state
    // the run would have started in.
    reportSeams(machine);
    machine.destroy();
    return 0;
  }

  const perFrame = machine.ticksPerSecond() / 60;
  const audioPerFrame = Math.round(AUDIO_SAMPLE_RATE / 60);
  const captured = opts.dumpPrefix === null ? null : [];
  let capturedSamples = 0;
  let stills = 0;

  // --- The edge list, written as the run makes it (#148) ----------------
  //
  // `--dump`'s third file, and the SDL host's third file byte for byte
  // (hosts/sdl/src/main.cpp says why it exists): the PPM is the frame,
  // the WAV is one *rendering* of the sound, and this is the sound in the
  // units the machine works in — "at tick T the output became high".
  //
  // Until the ABI grew an edge-log door this could not be asked here at
  // all, so #106's measurable half stopped at the desktop: a browser
  // could compare renderings and not machines. Now the same run dumped on
  // both hosts produces two files that must be identical, and that is a
  // stronger statement than two WAVs agreeing.
  //
  // Streamed, and drained every frame, because core's log is a bounded
  // ring with no allocator behind it (platform.h) — the loop empties it
  // and this file keeps the whole run.
  const edgeLines = opts.dumpPrefix === null ? null : [];
  let edgesWritten = 0;
  if (edgeLines !== null) {
    machine.logEdges(true);
  }
  const drainEdges = () => {
    if (edgeLines === null) return;
    for (;;) {
      const batch = machine.readEdges(256);
      if (batch.length === 0) return;
      for (const edge of batch) {
        edgeLines.push(`${edge.at} ${edge.level ? '1' : '0'}`);
      }
      edgesWritten += batch.length;
    }
  };

  // The keys, indexed by the frame they are due at, so the loop does no
  // searching. A make and a break at the same virtual instant, which is
  // what a keystroke posted to `af_machine_post_key` is: the machine
  // reads scancodes, and how long a finger stayed down is not something
  // an 83-key board reports.
  const due = new Map();
  for (const press of opts.presses) {
    if (!due.has(press.frame)) due.set(press.frame, []);
    due.get(press.frame).push(press);
  }
  const duePulls = new Map();
  for (const pull of opts.pulls) {
    if (!duePulls.has(pull.frame)) duePulls.set(pull.frame, []);
    duePulls.get(pull.frame).push(pull);
  }

  let partialLine = '';
  const drainLog = () => {
    const text = partialLine + machine.readLog();
    const lastBreak = text.lastIndexOf('\n');
    if (lastBreak < 0) {
      partialLine = text;
      return;
    }
    partialLine = text.slice(lastBreak + 1);
    process.stdout.write(text.slice(0, lastBreak + 1));
  };

  let frame = 0;
  let next = 0;
  let ended = AF_RUN_END_STOPPED;

  // Wall time, around the loop and nothing else: the module's
  // instantiation, the directory read and the files written afterwards
  // are this program's overheads and not the machine's throughput.
  const startedAt = process.hrtime.bigint();

  for (;;) {
    if (opts.steps !== 0 && machine.steps() >= opts.steps) {
      ended = AF_RUN_END_STEP_BUDGET;
      break;
    }
    if (opts.frames !== 0 && frame >= opts.frames) {
      ended = AF_RUN_END_TICK_BUDGET;
      break;
    }
    if (opts.until !== 0 && next >= opts.until) {
      ended = AF_RUN_END_TICK_BUDGET;
      break;
    }

    for (const press of due.get(frame) ?? []) {
      machine.postKey(press.scancode, true);
      machine.postKey(press.scancode, false);
      if (!opts.quiet) {
        say(`amberfolio: press ${press.name} frame=${frame}`);
      }
    }

    // A pull is said whether or not the run is quiet, and a refused one
    // is said loudly: a script that asked a cheat to fire and silently
    // got a plain machine is the worst outcome this driver has, which is
    // the same reasoning `--seam` is refused on.
    for (const pull of duePulls.get(frame) ?? []) {
      const answer = machine.seamPull(pull.id);
      const row = machine.seamList().find((seam) => seam.id === pull.id);
      if (answer !== AF_OK) {
        say(`amberfolio: seam ${pull.id} not pulled frame=${frame}`);
      } else if (!opts.quiet) {
        say(
          `amberfolio: seam ${pull.id} pulled frame=${frame} - ` +
            (row && row.armed
              ? 'acts at the next arrival at its point'
              : 'inert; its module is not resident'),
        );
      }
    }

    // The last slice is clamped into the budget rather than allowed to
    // overshoot it by most of a frame, so `--until T` asks the machine
    // for tick T and no further and the stop can be reproduced. What it
    // then ends on is T or the first tick past it, because a step is
    // atomic and `run_until` finishes the one it is in — the machine's
    // granularity, which the stop report states, and not this loop's.
    next += perFrame;
    if (opts.until !== 0 && next > opts.until) next = opts.until;
    const status = machine.runUntil(next);
    frame += 1;

    const consoleBytes = machine.readConsole();
    if (consoleBytes.length > 0 && !opts.quiet) {
      process.stdout.write(decodeConsoleBytes(consoleBytes));
    }
    drainLog();

    // One frame's worth of audio, pulled exactly as the page pulls it.
    // Not decoration either: the pull is part of the loop being measured,
    // and the underrun and resync counters underneath it only mean
    // something if somebody is pulling (platform.h).
    const { samples } = machine.renderAudio(audioPerFrame, AUDIO_SAMPLE_RATE);
    if (captured !== null && capturedSamples < AUDIO_CAPTURE_LIMIT) {
      captured.push(samples);
      capturedSamples += samples.length;
    }
    drainEdges();

    // Announced only in the count at the end: sixty stills a virtual
    // second is a line of output per still, and the caller asked to watch
    // a film rather than to read about one.
    if (opts.dumpEvery !== 0 && frame % opts.dumpEvery === 0) {
      writeFrame(machine, `${opts.dumpPrefix}-${String(frame).padStart(6, '0')}.ppm`, false);
      stills += 1;
    }

    if (status !== AF_OK) {
      ended = AF_RUN_END_STOPPED;
      break;
    }
  }

  const elapsedNs = process.hrtime.bigint() - startedAt;

  drainLog();
  if (partialLine.length > 0) process.stdout.write(`${partialLine}\n`);
  const lost = machine.logDropped();
  if (lost > 0) {
    say(`amberfolio: ${lost} diagnostic line(s) dropped - the log ring overflowed`);
  }

  // The stop report, formatted in core so the desktop host prints the
  // same sentence (machine/report.h). This is the line the comparison is
  // made on, field for field.
  process.stdout.write(machine.stopReport(ended));
  if (opts.trace) {
    const trace = machine.traceReport();
    if (!trace.startsWith('amberfolio: stop trace=off')) process.stdout.write(trace);
  }

  // The state hash: the same digest a recording's checkpoint carries
  // (docs/replay.md §2), so two hosts' runs can be compared by one line
  // rather than by eye over a picture.
  say(`amberfolio: state hash=${machine.stateHash() ?? 'unavailable'}`);
  say(
    `amberfolio: audio underruns=${machine.audioUnderruns()} ` +
      `resyncs=${machine.audioResyncs()}`,
  );
  reportSeams(machine);
  reportSeamsFired(machine);
  if (opts.listVfs) reportVfs(machine);

  // --- Throughput --------------------------------------------------------
  //
  // Virtual seconds covered against wall seconds spent, and the factor
  // to two decimals: the number is an observation of one machine on one
  // day, and printing it to six figures would be claiming a precision it
  // does not have.
  //
  // Wall time is the exception, printed to the microsecond. Virtual time
  // is a count of ticks and is exact; wall time is the measurement, and a
  // short run of a small program takes single-digit milliseconds, which
  // three decimals round to `0.002` — a number nobody can divide by and
  // get the factor back. A reader who wants to check the arithmetic on
  // this line should be able to.
  //
  // `steps` here and `steps=` in the stop report are the same number, on
  // purpose: this line is the run's two denominators and the report is
  // the run. What is deliberately *not* repeated is `frames=`, which in
  // the stop report counts composed frames — one more than the slices
  // this loop ran, because `reset()` published one before the program
  // started (docs/hosts.md §4).
  const wallSeconds = Number(elapsedNs) / 1e9;
  const virtualSeconds = machine.time() / machine.ticksPerSecond();
  const factor = wallSeconds > 0 ? virtualSeconds / wallSeconds : 0;
  const stepsPerSecond = wallSeconds > 0 ? machine.steps() / wallSeconds : 0;
  say(
    `amberfolio: throughput virtual=${virtualSeconds.toFixed(3)}s ` +
      `wall=${wallSeconds.toFixed(6)}s factor=${factor.toFixed(2)}x ` +
      `steps=${machine.steps()} steps/s=${Math.round(stepsPerSecond)}`,
  );

  if (opts.dumpPrefix !== null) {
    writeFrame(machine, `${opts.dumpPrefix}.ppm`, true);
    if (stills > 0) {
      say(`amberfolio: dump stills=${stills} every=${opts.dumpEvery} frames`);
    }
    const total = capturedSamples;
    if (total === 0) {
      say('amberfolio: dump no audio was captured (nothing pulled the speaker)');
    } else {
      const all = new Float32Array(total);
      let at = 0;
      for (const chunk of captured) {
        all.set(chunk, at);
        at += chunk.length;
      }
      const wav = `${opts.dumpPrefix}.wav`;
      writeFileSync(wav, encodeWav(all, AUDIO_SAMPLE_RATE));
      say(
        `amberfolio: dump audio=${wav} samples=${total}` +
          (total >= AUDIO_CAPTURE_LIMIT ? ' (truncated)' : ''),
      );
    }

    // And the edge list. The count is on the last line as well as in the
    // report so the file answers "is this all of it?" on its own — a
    // truncated dump and a silent run look identical from the top, and
    // only one of them is a finding about the machine.
    drainEdges();
    const dropped = machine.audioEdgesDropped();
    const edges = `${opts.dumpPrefix}.edges`;
    const header = [
      '# amberfolio audio edges',
      `# pit-input-hz ${machine.ticksPerSecond()}`,
      '# tick level',
    ];
    const trailer = `# edges ${edgesWritten} dropped ${dropped}`;
    writeFileSync(
      edges,
      `${header.concat(edgeLines, [trailer]).join('\n')}\n`,
    );
    say(
      `amberfolio: dump edges=${edges} count=${edgesWritten} dropped=${dropped}`,
    );
  }

  machine.destroy();
  return 0;
}

/// Every file on the disk after the run, at its own path (M5-D2, #170) —
/// the SDL host's `--vfs-list`, spelled identically.
///
/// After the run, because the question it exists to answer is what the
/// run left behind: what is in `\\SAVE\\` once the game has saved, and
/// whether the file the program wrote is the file a page can read back.
/// Files, and only files — an empty directory does not appear, which is
/// what makes every row here something `vfsGet()` and `vfsRemove()` can
/// act on (abi.h).
function reportVfs(machine) {
  const listing = machine.vfsList();
  say(`amberfolio: vfs ${listing.length} file(s)`);
  for (const entry of listing) {
    say(`amberfolio: vfs ${entry.path} ${entry.size}`);
  }
}

/// One line per seam, in the shape the SDL host's `--seams` prints —
/// id, state, whether it armed, the reason if there is one, and what it
/// is for. Printed at the end of every run as well as on `--seams`,
/// because "the seam was on" is a fact about the run and a report that
/// left it out would be describing the wrong machine.
function reportSeams(machine) {
  for (const seam of machine.seamList()) {
    const state = SEAM_STATE_NAMES[seam.state] ?? 'unknown';
    const armed = seam.state === AF_SEAM_ON ? (seam.armed ? ' armed' : ' inert') : '';
    const why = seam.reason === 'none' ? '' : ` ${seam.reason}`;
    say(`amberfolio: seams ${seam.id} ${state}${armed}${why} - ${seam.about}`);
  }
}

/// What each enabled seam actually *did*, one line each, in the words the
/// SDL host ends a run with (hosts/sdl/src/main.cpp) — `seam <id> armed
/// fired=N`, singular, and distinct from the `seams` listing above.
///
/// The listing says an address was computed out of a fact table; this
/// says a handler ran there (#131). A seam that is on and armed and
/// fired nothing is the failure that reads exactly like success, so the
/// zero is called out in words rather than left to a reader to spot.
/// Only for a run: `--seams` asks a question before anything has moved,
/// and every count would be zero for the honest reason.
function reportSeamsFired(machine) {
  for (const seam of machine.seamList()) {
    if (seam.state !== AF_SEAM_ON) continue;
    say(`amberfolio: seam ${seam.id} ${formatSeamFired(seam)}`);
  }
}

function writeFrame(machine, path, announce) {
  const width = machine.frameWidth();
  const height = machine.frameHeight();
  writeFileSync(
    path,
    encodePpm(width, height, machine.framebufferView(), machine.paletteView()),
  );
  if (announce) {
    say(`amberfolio: dump frame=${path} generation=${machine.frameGeneration()}`);
  }
}

/// Wait until everything written to a stream has actually left this
/// process, and only then let the caller exit.
///
/// `process.exit()` does not do this. A pipe on Linux is an
/// **asynchronous** stream in node, so `process.stdout.write()` can queue
/// bytes rather than deliver them, and `exit()` drops whatever is still
/// queued. Small runs never notice. A run that says a lot — the overrun
/// disk in `tests/smoke.mjs` prints a line per skipped file, hundreds of
/// them — loses its tail, and *which* line it is cut off at depends on
/// how fast the process reached the exit. A Release module is faster than
/// a Debug one, so this failed in one configuration and passed in the
/// other, on the same code, from the same disk.
///
/// The lost tail is the worst part of the output to lose: the summary
/// lines come last. `disk INCOMPLETE` is printed after the list it
/// summarises, and it is the one line that says the machine is holding
/// less than the caller handed it (#158).
///
/// `write('', cb)` calls back once the buffer has drained, which is the
/// documented way to ask. Not `process.exitCode` and a natural exit: the
/// wasm module may leave handles the event loop would wait on, and a
/// driver that sometimes hangs instead of exiting is a worse tool than
/// one that sometimes truncates.
function flushed(stream) {
  return new Promise((resolve) => {
    stream.write('', () => resolve());
  });
}

// --- Entry point ---------------------------------------------------------
//
// Guarded, so `hosts/web/tests/smoke.mjs` can import the pieces above
// without a run starting underneath it.

const invokedDirectly =
  process.argv[1] !== undefined &&
  import.meta.url === pathToFileURL(process.argv[1]).href;

if (invokedDirectly) {
  const opts = parseArgs(process.argv.slice(2));
  if (opts.error !== undefined) {
    process.stderr.write(`amberfolio: ${opts.error}\n\n${USAGE}\n`);
    await flushed(process.stderr);
    process.exit(2);
  }
  const code = await drive(opts);
  await flushed(process.stdout);
  await flushed(process.stderr);
  process.exit(code);
}
