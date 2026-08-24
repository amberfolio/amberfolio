// SPDX-License-Identifier: AGPL-3.0-only
//
// Headless check that the wasm module loads and its ABI answers — the
// same thing index.html shows a human, in a form CI can run (issue #3
// wires it into the build matrix). Registered with CTest by
// hosts/web/CMakeLists.txt and run from the build tree, beside the glue
// this imports.
//
//     node smoke.mjs --expect <major.minor.patch>
//
// Usable by hand too: pass no --expect and it just reports what it found.
//
// Seven checks now (M2-F4 #45, M2-H2 #55, M3-F2 #84, M4-W1 #108, #157):
//
//   1. The version the module reports is the version CMake built.
//   2. **Every name in the export list is actually exported.** This is
//      the guard for the ABI's one quiet failure mode: a function added
//      to core/include/amberfolio/abi.h and not added to
//      -sEXPORTED_FUNCTIONS in hosts/web/CMakeLists.txt is simply missing
//      from the module, and nothing in the build says so. The list below
//      is the third reader of that guest list, and the point is that it
//      is a separate one — two lists that must agree, checked.
//   3. The ABI boundary answers correctly in isolation: a bare machine
//      (no reference devices attached) refuses on its own — memory,
//      audio, input, clock and console all cross into wasm, and a
//      filesystem call says there is no filesystem rather than pretending.
//   4. **The actual thing the page runs.** Create a machine, attach the
//      reference device set, load the embedded demo program
//      (hosts/web/src/demo_program.cpp) through host.mjs's own
//      `Machine`/`loadDemoProgram` — the same code hosts/web/page/app.mjs
//      calls — run it, post a key, and assert a framebuffer hash, the
//      console bytes the key echo produced, and that the key was
//      observed. This is the wasm quarter of the M2 exit criterion
//      (PLAN.md §7): the machine runs correctly on this target, not
//      merely that it compiles for it.
//   5. **The VFS path M3-F2 (#84) added**, which is how a player's
//      directory gets into a browser at all: put a self-written program
//      into the machine's filesystem, list it back, fingerprint it, load
//      it *from there*, run it, and read the stop report. That is the
//      whole of what the dev page's picker does, minus the DOM — and the
//      stop report is the thing the desktop host has to agree with at the
//      same step, so a check that it is produced at all belongs in CI
//      even though the comparison itself is a local procedure (#92).
//   6. **The headless web driver** (`tools/drive.mjs`, M4-W1 #108) — the
//      thing a person actually points at their own copy, run here against
//      the repository's own 34-byte disk. It is spawned as a process, not
//      imported and called, because what is being checked is the tool as
//      a person invokes it: the command line, the report lines, the
//      exit code, and the files it writes.
//   7. **The dev page's pacing** (#157), on a clock made of numbers. The
//      page's run loop cannot be run here — there is no rAF and no
//      display — so its one decision, where elapsed wall time says
//      virtual time should be, is a pure function in host.mjs and this
//      drives it. Before #157 the display's refresh rate decided virtual
//      time, and a 240 Hz monitor ran the machine at 4x.

import { spawnSync } from 'node:child_process';
import {
  copyFileSync,
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  writeFileSync,
} from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  loadAmberfolio,
  formatVersion,
  Machine,
  loadDemoProgram,
  decodeConsoleBytes,
  scancodeFor,
  AF_OK,
  AF_INVALID,
  AF_NO_FILESYSTEM,
  AF_NO_ROOM,
  describeSkip,
  AF_KEY_DOWN,
  AF_KEY_UP,
  AF_RUN_END_STOPPED,
  AF_SEAM_OFF,
  AF_SEAM_ON,
  AF_SEAM_UNAVAILABLE,
  formatSeamFired,
  pacedAdvance,
  MAX_CATCH_UP_SECONDS,
} from './host.mjs';
import {
  encodePpm,
  keyNameToScancode,
  parseArgs as parseDriveArgs,
} from './drive.mjs';

/// The ABI's guest list, as hosts/web/CMakeLists.txt sets it. Keep the
/// two in step; that is the whole job of this array.
const EXPECTED_EXPORTS = [
  '_malloc',
  '_free',
  '_af_version',
  '_af_ticks_per_second',
  '_af_frame_width',
  '_af_frame_height',
  '_af_palette_entries',
  '_af_machine_create',
  '_af_machine_destroy',
  '_af_machine_attach_reference_devices',
  '_af_machine_reset',
  '_af_machine_run_until',
  '_af_machine_time',
  '_af_machine_steps',
  '_af_machine_stopped',
  '_af_machine_stop_reason',
  '_af_machine_set_speed',
  '_af_machine_set_trace',
  '_af_machine_stop_report',
  '_af_machine_trace_report',
  '_af_machine_framebuffer',
  '_af_machine_palette',
  '_af_machine_frame_generation',
  '_af_machine_render_audio',
  '_af_machine_audio_underruns',
  '_af_machine_audio_resyncs',
  '_af_machine_audio_log_edges',
  '_af_machine_audio_logging_edges',
  '_af_machine_audio_read_edges',
  '_af_machine_audio_edges_dropped',
  '_af_machine_audio_edges_pending',
  '_af_machine_post_key',
  '_af_machine_set_wall_clock',
  '_af_machine_read_log',
  '_af_machine_log_pending',
  '_af_machine_log_dropped',
  '_af_machine_clear_log',
  '_af_machine_read_console',
  '_af_machine_console_pending',
  '_af_machine_console_dropped',
  '_af_machine_vfs_clear',
  '_af_machine_vfs_put',
  '_af_machine_vfs_count',
  '_af_machine_vfs_name_at',
  '_af_machine_vfs_size_at',
  '_af_machine_vfs_bytes_used',
  '_af_machine_vfs_fingerprint',
  '_af_machine_load_from_vfs',
  '_af_machine_load_error',
  '_af_machine_edition',
  '_af_machine_program_fingerprint',
  '_af_machine_seam_count',
  '_af_machine_seam_id',
  '_af_machine_seam_about',
  '_af_machine_seam_state',
  '_af_machine_seam_reason',
  '_af_machine_seam_reading',
  '_af_machine_seam_armed',
  '_af_machine_seam_fired',
  '_af_machine_seam_enable',
  '_af_machine_seam_disable',
  '_af_machine_write_memory',
  '_af_machine_read_memory',
  '_af_machine_set_entry',
  // Web-host-specific (hosts/web/src/main.cpp), not part of
  // core/include/amberfolio/abi.h: the M2-H2 (#55) embedded demo
  // program and the M4-F4 (#98) seam probe. Listed here for the same
  // reason as everything above it — an export hosts/web/CMakeLists.txt
  // forgets is silently absent.
  '_af_web_demo_program_bytes',
  '_af_web_demo_program_size',
  '_af_machine_state_hash',
  '_af_machine_verify_recording',
  '_af_web_probe_program_bytes',
  '_af_web_probe_program_size',
  '_af_web_probe_seam_register',
];

/// FNV-1a, 32-bit. No third-party dependency (PLAN.md §4's house style,
/// applied to the one leaf test script that runs under plain node rather
/// than a bundler) is worth pulling in to hash 64,000 bytes once per CI
/// run; this is a dozen lines and every implementation of FNV-1a agrees
/// with every other one, which is the property a pinned test hash needs.
/// A self-written MZ executable around `image`: a two-paragraph header,
/// no relocations, so that CS:IP lands on the image's first byte and
/// SS:SP a quarter of a kilobyte above it. Every byte of it is ours
/// (CONTRIBUTING.md) — nothing in this repository ships a program.
function makeMzImage(image) {
  const header = new Uint8Array(32);
  const put16 = (at, value) => {
    header[at] = value & 0xff;
    header[at + 1] = (value >> 8) & 0xff;
  };
  header[0] = 0x4d; // 'M'
  header[1] = 0x5a; // 'Z'
  put16(2, header.length + image.length); // bytes in the last page
  put16(4, 1); // pages
  put16(6, 0); // relocations
  put16(8, 2); // header paragraphs
  put16(10, 0x0010); // MINALLOC
  put16(12, 0xffff); // MAXALLOC
  put16(16, 0x0100); // initial SP
  put16(24, 0x001c); // relocation table offset
  const exe = new Uint8Array(header.length + image.length);
  exe.set(header, 0);
  exe.set(image, header.length);
  return exe;
}

function fnv1a32(bytes) {
  let hash = 0x811c9dc5;
  for (const byte of bytes) {
    hash ^= byte;
    hash = Math.imul(hash, 0x01000193) >>> 0;
  }
  return hash >>> 0;
}

/// The demo program's framebuffer hash after `settleTicks` of virtual
/// time — see demo_program.cpp for exactly what it draws and why the
/// result is deterministic. Captured from a real run of this module and
/// pinned here the same way the M1 conformance suite pins its vectors: a
/// changed hash means the drawn pattern changed, which means either the
/// demo program or something underneath it (the EGA write pipeline, the
/// renderer, int10.h's mode-0Dh table) moved, and either is worth a human
/// looking at rather than the test quietly re-pinning itself.
const EXPECTED_FRAMEBUFFER_HASH = 0x24846c85;

function parseArgs(argv) {
  const sessionsIndex = argv.indexOf('--sessions');
  const sessions = sessionsIndex === -1 ? null : argv[sessionsIndex + 1];
  if (sessionsIndex !== -1 && !sessions) {
    console.error('smoke: --sessions needs a directory argument');
    process.exit(2);
  }

  const expectIndex = argv.indexOf('--expect');
  if (expectIndex === -1) return { expected: null, sessions };

  const expected = argv[expectIndex + 1];
  if (!expected) {
    console.error('smoke: --expect needs a version argument');
    process.exit(2);
  }
  return { expected, sessions };
}

const { expected, sessions } = parseArgs(process.argv.slice(2));

// Keep the module's own stdout/stderr distinguishable from ours: a version
// that matches for the wrong reason is exactly what this is meant to catch.
const { module, version, output } = await loadAmberfolio({
  print: (text) => console.log(`  [module] ${text}`),
  printErr: (text) => console.error(`  [module] ${text}`),
});

const found = formatVersion(version);
console.log(`smoke: af_version() reports ${found}`);

const problems = [];
if (expected && found !== expected) {
  problems.push(`expected af_version() to report ${expected}, got ${found}`);
}
if (output.length === 0) {
  problems.push('the module produced no output, so main() did not run');
}

// --- The export list --------------------------------------------------

const missing = EXPECTED_EXPORTS.filter(
  (name) => typeof module[name] !== 'function',
);
if (missing.length > 0) {
  problems.push(
    `these ABI functions are not exported by the module: ${missing.join(', ')}` +
      ' — add them to -sEXPORTED_FUNCTIONS in hosts/web/CMakeLists.txt',
  );
}
if (typeof module.HEAPU8 === 'undefined' || typeof module.HEAPF32 === 'undefined') {
  problems.push('HEAPU8/HEAPF32 are not on the module; a host cannot read the framebuffer');
}
console.log(`smoke: ${EXPECTED_EXPORTS.length - missing.length}/${EXPECTED_EXPORTS.length} ABI exports present`);

// --- The machine ------------------------------------------------------
//
// Everything past here needs the exports, so it is skipped if any are
// missing rather than adding a second failure that says the same thing.

if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const box = module._af_machine_create();
  check(box !== 0, 'af_machine_create() answered null');

  if (box !== 0) {
    check(
      module._af_machine_create() === 0,
      'a second af_machine_create() answered a handle; there is one machine',
    );

    check(module._af_frame_width() === 320, 'frame width is not 320');
    check(module._af_frame_height() === 200, 'frame height is not 200');
    check(module._af_palette_entries() === 16, 'palette is not 16 entries');
    check(
      module._af_ticks_per_second() === 1193182,
      'the tick rate is not the PIT input clock',
    );

    // A self-written program: JMP to itself, two bytes, at 1000:0000. It
    // is here to consume virtual time and prove the entry point works,
    // and it is ours — nothing in this repository ever carries bytes from
    // anywhere else (CONTRIBUTING.md).
    const program = new Uint8Array([0xeb, 0xfe]);
    const scratch = module._malloc(program.length);
    module.HEAPU8.set(program, scratch);
    check(
      module._af_machine_write_memory(box, 0x10000, scratch, program.length) ===
        AF_OK,
      'af_machine_write_memory() refused the program',
    );

    // And read it back, which is the check that the two halves agree
    // about where "physical address" is.
    module.HEAPU8.fill(0, scratch, scratch + program.length);
    check(
      module._af_machine_read_memory(box, 0x10000, scratch, program.length) ===
        AF_OK,
      'af_machine_read_memory() refused',
    );
    check(
      module.HEAPU8[scratch] === 0xeb && module.HEAPU8[scratch + 1] === 0xfe,
      'memory did not read back what was written',
    );
    module._free(scratch);

    // A machine exists; a filesystem does not, because the reference
    // device set was never attached. AF_NO_MACHINE would be a lie about
    // the first, which is why AF_NO_FILESYSTEM has its own number.
    check(
      module._af_machine_load_from_vfs(box, 0, 0) === AF_NO_FILESYSTEM,
      'a bare machine should say it has no filesystem, not that it has no machine',
    );
    check(
      module._af_machine_vfs_count(box) === 0,
      'a bare machine listed files it does not have',
    );

    check(
      module._af_machine_set_entry(box, 0x1000, 0, 0x1000, 0xfffe) === AF_OK,
      'af_machine_set_entry() refused',
    );

    // The clock, seeded rather than read from the OS — core never asks
    // the host what time it is (PLAN.md §4).
    check(
      module._af_machine_set_wall_clock(box, 1986, 6, 17, 12, 30, 0, 0) ===
        AF_OK,
      'af_machine_set_wall_clock() refused a real date',
    );
    check(
      module._af_machine_set_wall_clock(box, 1986, 2, 30, 0, 0, 0, 0) !== AF_OK,
      'af_machine_set_wall_clock() accepted 30 February',
    );

    // One frame of virtual time.
    const frame = module._af_ticks_per_second() / 60;
    check(
      module._af_machine_run_until(box, frame) === AF_OK,
      'the machine stopped during the first frame',
    );
    check(
      module._af_machine_time(box) >= frame,
      'the virtual clock did not reach the frame boundary',
    );
    check(module._af_machine_stopped(box) === 0, 'the machine is stopped');

    // Frame out: a pointer into linear memory, and a generation counter.
    const pixels = module._af_machine_framebuffer(box);
    const palette = module._af_machine_palette(box);
    check(pixels !== 0 && palette !== 0, 'the framebuffer pointers are null');

    // Audio pull, into a buffer this side owns and frees.
    const frames = 64;
    const samples = module._malloc(frames * 4);
    const filled = module._af_machine_render_audio(box, samples, frames, 44100);
    check(
      filled === frames,
      `expected ${frames} settled audio frames, got ${filled}`,
    );
    const heard = module.HEAPF32.subarray(samples >> 2, (samples >> 2) + frames);
    check(
      heard.every((sample) => sample === 0),
      'a bare machine (no reference devices attached) produced sound',
    );
    module._free(samples);

    check(
      module._af_machine_post_key(box, 0x1e, 1) === AF_OK,
      'af_machine_post_key() refused a make code',
    );
    check(
      module._af_machine_post_key(box, 0x9e, 1) !== AF_OK,
      'af_machine_post_key() accepted a code with the release bit set',
    );

    // Console: this machine is bare (no reference devices, no DOS layer
    // attached — af_machine_attach_reference_devices() was never
    // called), so there is nothing to drain. The section below attaches
    // the reference set on a machine of its own and checks the console
    // again, once the demo program has actually written to it.
    check(
      module._af_machine_console_pending(box) === 0,
      'something wrote to the console on a machine with no DOS layer attached',
    );

    module._af_machine_destroy(box);
    const again = module._af_machine_create();
    check(again !== 0, 'the machine could not be created again after destroy');
    module._af_machine_destroy(again);
  }

  console.log('smoke: the machine ran a frame and every interface answered');
}

// --- The M2-H2 demo program ---------------------------------------------
//
// Everything above is a boundary check on a bare machine. This is the
// thing hosts/web/page/app.mjs actually runs: a machine with the
// reference device set attached, running the embedded demo program
// (hosts/web/src/demo_program.cpp) through host.mjs's own
// `Machine`/`loadDemoProgram` — not a second, parallel re-implementation
// of what app.mjs does, the same code path. This is the wasm quarter of
// the M2 exit criterion (PLAN.md §7): "self-written real-mode test
// programs run correctly on all targets," demonstrated here on wasm the
// same way tests/programs demonstrates it on every native target.
if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const machine = new Machine(module);
  check(machine.handle !== 0, 'Machine construction answered a null handle');

  const attachStatus = machine.attachReferenceDevices();
  check(
    attachStatus === AF_OK,
    `af_machine_attach_reference_devices() answered ${attachStatus}`,
  );
  // Idempotent, per abi.h — a second call must not disturb anything.
  check(
    machine.attachReferenceDevices() === AF_OK,
    'attaching the reference devices a second time did not answer AF_OK',
  );

  const { writeStatus, entryStatus, size } = loadDemoProgram(machine);
  check(
    writeStatus === AF_OK,
    `loading the demo program failed to write its bytes (status ${writeStatus})`,
  );
  check(
    entryStatus === AF_OK,
    `loading the demo program failed to set its entry point (status ${entryStatus})`,
  );
  check(size > 0, 'the embedded demo program is empty');

  // Enough virtual time for the mode-set, the 8000-byte draw loop and the
  // tone setup to finish and the CPU to reach the echo loop's blocking
  // key read, which halts (keyboard.h's "AH=00h on an empty buffer").
  // The draw loop alone is ~40,000 instructions at 4 ticks/step (the
  // default PC/XT speed, clock.h) — about 160,000 ticks — so this is a
  // generous multiple of that, and comfortably more than one 60 Hz frame
  // period (~19,886 ticks) so the renderer has composed at least once.
  const settleTicks = 400_000;

  // The edge log on before the run, so the tone below can be checked as
  // the machine *published* it and not only as the filter rendered it
  // (#148). Off by default, and asserted so: a browser that paid for
  // this on every run would be paying for a facility almost no run uses
  // (platform.h).
  check(
    !machine.loggingEdges(),
    'the edge log was on before anybody asked for it',
  );
  machine.logEdges(true);
  check(machine.loggingEdges(), 'the edge log would not switch on');

  const settleStatus = machine.runUntil(settleTicks);
  check(
    settleStatus === AF_OK,
    `the machine stopped while drawing (status ${settleStatus}, reason ` +
      `${machine.stopReason()})`,
  );
  check(
    machine.time() >= settleTicks,
    'virtual time did not reach the settle point',
  );

  // --- Video: a hash of the drawn pattern ---------------------------------

  // The renderer is scheduled, not attached (renderer.h), and composes
  // on its own 60 Hz virtual-time deadline — this is the check that it
  // actually fired at least once, so the framebuffer hash below is a
  // hash of the drawn picture and not of an all-zero frame nothing ever
  // composed into.
  check(
    machine.frameGeneration() > 0,
    'the renderer never composed a frame - frameGeneration() is still 0',
  );

  const pixels = machine.framebufferView();
  check(
    pixels.length === machine.frameWidth() * machine.frameHeight(),
    'the framebuffer is not width*height bytes',
  );
  const framebufferHash = fnv1a32(pixels);
  check(
    framebufferHash === EXPECTED_FRAMEBUFFER_HASH,
    `framebuffer hash is 0x${framebufferHash.toString(16)}, expected ` +
      `0x${EXPECTED_FRAMEBUFFER_HASH.toString(16)} - the drawn pattern changed`,
  );

  // --- Sound: the tone should have left non-silent, settled samples ------

  const toneCheck = machine.renderAudio(64, 44100);
  check(
    toneCheck.settled === 64,
    `expected 64 settled audio frames once the tone is programmed, got ${toneCheck.settled}`,
  );
  check(
    Array.from(toneCheck.samples).some((sample) => sample !== 0),
    'the tone PIT channel 2 was programmed for produced no sound',
  );

  // --- And the same tone as the machine published it (#148) --------------
  //
  // The other half of #106's question, which a browser could not ask at
  // all before this door existed: not "does it sound like something" but
  // "did the machine put the right edges at the right ticks". The list
  // is what the SDL host's `--dump PREFIX.edges` writes and what
  // `tools/drive.mjs --dump` now writes here, so the two hosts can be
  // compared as machines rather than as renderings.
  const edges = machine.readEdges(1024);
  check(edges.length > 4, `the edge log holds ${edges.length} edges, expected a tone`);
  check(
    machine.audioEdgesDropped() === 0,
    `the edge log dropped ${machine.audioEdgesDropped()} edges - the list has a hole`,
  );
  for (let i = 1; i < edges.length; i += 1) {
    check(
      edges[i].at > edges[i - 1].at,
      `edge ${i} is at tick ${edges[i].at}, not after ${edges[i - 1].at}`,
    );
    check(
      edges[i].level !== edges[i - 1].level,
      `edge ${i} repeats the level of the one before it`,
    );
  }
  // Drained means gone: what makes a host's file the whole run rather
  // than its last thousand edges.
  check(
    machine.audioEdgesPending() === 0,
    'the edge log still holds edges after a full drain',
  );
  check(machine.readEdges(16).length === 0, 'a drained edge log gave more edges');
  machine.logEdges(false);

  // --- Keyboard: post a key, and prove the machine observed it -----------

  const beforeEcho = machine.time();
  check(
    machine.postKey(0x1e, AF_KEY_DOWN) === AF_OK,
    "posting the 'a' key's make code failed",
  );
  check(
    machine.postKey(0x1e, AF_KEY_UP) === AF_OK,
    "posting the 'a' key's break code failed",
  );

  const echoStatus = machine.runUntil(beforeEcho + 50_000);
  check(
    echoStatus === AF_OK,
    `the machine stopped while echoing the key (status ${echoStatus}, reason ` +
      `${machine.stopReason()})`,
  );

  // --- Console: the DOS AH=02h write the key echo produced ---------------

  const consoleBytes = machine.readConsole();
  check(
    consoleBytes.length === 1 && consoleBytes[0] === 0x61,
    `console output was ${JSON.stringify(Array.from(consoleBytes))}, ` +
      "expected a single 0x61 ('a') - the key event was not observed",
  );
  console.log(
    `smoke: the demo program echoed ${JSON.stringify(decodeConsoleBytes(new Uint8Array([0x61])))} after a posted key`,
  );

  // --- The account: the same lines the desktop host prints (#108) --------
  //
  // The high-volume channels are off until asked for, exactly as the SDL
  // host's `--trace` asks for them, so the check is in two halves: quiet
  // by default, and then the program's own service calls once tracing is
  // on. What is asserted is the *shape core renders*, because that shape
  // is the whole claim - a browser run and a desktop run of one program
  // have to produce the same characters, not merely the same events.
  machine.clearLog();
  check(
    machine.logPending() === 0,
    'the log still had characters pending after clearLog()',
  );

  const quiet = machine.time();
  machine.runUntil(quiet + 50_000);
  check(
    machine.readLog() === '',
    'service calls reached the log with tracing off',
  );

  machine.setTrace(true);
  check(
    machine.postKey(0x1e, AF_KEY_DOWN) === AF_OK,
    "posting the 'a' key's make code a second time failed",
  );
  check(machine.postKey(0x1e, AF_KEY_UP) === AF_OK, 'posting a break code failed');
  machine.runUntil(machine.time() + 50_000);

  const logged = machine.readLog();
  check(
    /^amberfolio: call INT[0-9A-F]{2} ax=[0-9A-F]{4} from=[0-9A-F]{4}:[0-9A-F]{4} (handled|unimplemented)$/m.test(
      logged,
    ),
    `the log carried no service-call line in core's format; got ${JSON.stringify(logged.slice(0, 200))}`,
  );
  check(
    logged.endsWith('\n'),
    'the log did not end on a line break - a line was cut in half',
  );
  // The demo program polls INT 16h in a tight loop, so a traced slice of
  // it produces thousands of call lines - far more than a bounded ring
  // can hold between two drains, and diagnostics.h says as much about
  // this channel. What is asserted is therefore not that nothing was
  // dropped: it is that overflow is *reported* rather than silent, and
  // that every line that did arrive is whole. A traced browser run is a
  // sample plus a count, and the count is what makes it honest.
  const dropped = machine.logDropped();
  check(
    dropped > 0,
    'the tight INT 16h poll did not overflow the log ring - either the ' +
      'ring grew or the trace channel stopped reporting',
  );
  check(
    machine.logDropped() === dropped,
    'the drop count moved across a drain - it is a property of the run',
  );
  console.log(
    `smoke: the diagnostics account crossed the ABI ` +
      `(${logged.split('\n').length - 1} lines, ${dropped} dropped and counted)`,
  );

  machine.destroy();

  console.log(
    'smoke: the demo program drew a frame, played a tone, and echoed a key',
  );
}

// --- The filesystem, and a program loaded off it (M3-F2, #84) -----------
//
// The dev page's picker reads a player's files in the browser and puts
// them in the machine one at a time; everything after that — the names,
// the listing order, the fingerprint, the load — is core's. This drives
// exactly that path with a program of our own, so the ABI surface a
// browser depends on is asserted here rather than only in a browser
// nobody can run in CI.

if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const machine = new Machine(module);
  check(
    machine.attachReferenceDevices() === AF_OK,
    'attaching the reference devices failed',
  );
  // The RESET line, as app.mjs pulls it and as the SDL host's own wiring
  // does: it is part of powering the machine on, and skipping it leaves
  // the frame generation one behind a desktop run of the same program
  // (see app.mjs for the whole argument).
  machine.reset();

  // A self-written MZ program: a two-paragraph header, no relocations,
  // and an image that writes 'H' to the console through INT 21h AH=02h
  // and exits with code 11. Every byte is ours (CONTRIBUTING.md); the
  // listing beside it is what makes that checkable by eye.
  const image = new Uint8Array([
    0xb2, 0x48, // MOV DL, 'H'
    0xb4, 0x02, // MOV AH, 02h    ; console output
    0xcd, 0x21, // INT 21h
    0xb8, 0x0b, 0x4c, // MOV AX, 4C0Bh  ; exit, code 11
    0xcd, 0x21, // INT 21h
  ]);
  const exe = makeMzImage(image);

  check(machine.vfsPut('HELLO.EXE', exe) === AF_OK, 'putting HELLO.EXE failed');

  // Lower case in, upper case out: the page does no name logic and core
  // canonicalizes (abi.h). And a name no DOS short name can equal is
  // refused rather than mangled, which is how a page gets its "skipped"
  // list for the files a boxed copy carries.
  check(machine.vfsPut('notes.txt', new Uint8Array([1])) === AF_OK,
        'putting a lower-case name failed');
  check(
    machine.vfsPut('code wheel.pdf', new Uint8Array([1])) === AF_INVALID,
    'a name with a space in it was accepted as a DOS short name',
  );

  const listing = machine.vfsList();
  check(listing.length === 2, `expected 2 files, got ${listing.length}`);
  // Pinned name order (machine/vfs.h), so a listing is the same on every
  // host and in every run.
  check(
    listing[0]?.name === 'HELLO.EXE' && listing[1]?.name === 'NOTES.TXT',
    `listing is ${JSON.stringify(listing.map((e) => e.name))}, expected ` +
      "['HELLO.EXE', 'NOTES.TXT'] in pinned name order",
  );
  check(
    listing[0]?.size === exe.length,
    `HELLO.EXE is ${listing[0]?.size} bytes, expected ${exe.length}`,
  );
  check(
    machine.vfsBytesUsed() === exe.length + 1,
    `the filesystem holds ${machine.vfsBytesUsed()} bytes, expected ${exe.length + 1}`,
  );

  // The identity of the file — a fact about it, and the same digest the
  // desktop host prints at load.
  const digest = machine.vfsFingerprint('HELLO.EXE');
  check(
    typeof digest === 'string' && /^[0-9a-f]{64}$/.test(digest),
    `the fingerprint of HELLO.EXE is ${JSON.stringify(digest)}`,
  );
  check(
    machine.vfsFingerprint('GONE.DAT') === null,
    'a file that is not there was fingerprinted anyway',
  );

  const loadStatus = machine.loadFromVfs('HELLO.EXE', ' ARG');
  check(
    loadStatus === AF_OK,
    `loading HELLO.EXE off the filesystem answered ${loadStatus} ` +
      `(loader error ${machine.loadError()})`,
  );

  // Run it to its own exit, and read what it said.
  let text = '';
  for (let frame = 0; frame < 60 && !machine.stopped(); ++frame) {
    machine.runUntil(machine.ticksPerSecond() * ((frame + 1) / 60));
    const bytes = machine.readConsole();
    if (bytes.length > 0) text += decodeConsoleBytes(bytes);
  }
  check(text === 'H', `the program printed ${JSON.stringify(text)}, expected "H"`);
  check(machine.stopped(), 'the program never exited');
  check(machine.steps() > 0, 'the machine took no steps');

  // And the report: the same fixed lines the desktop host prints,
  // formatted in core so the two cannot drift (machine/report.h).
  const report = machine.stopReport(AF_RUN_END_STOPPED);
  check(
    report.includes('amberfolio: stop reason=program_exited '),
    `the stop report does not say the program exited:\n${report}`,
  );
  check(
    report.includes('amberfolio: stop exit=11'),
    `the stop report does not carry the exit code the program chose:\n${report}`,
  );
  check(
    !report.includes('next='),
    `a program that exited was given a worklist line:\n${report}`,
  );

  console.log(
    `smoke: a program loaded off the filesystem printed "H", exited 11, and ` +
      'reported its stop',
  );

  machine.destroy();
}

// --- A file below the root, opened by the program (M4, #146) -------------
//
// Until #146 the ABI's door reached the root and no further, so a host
// handing over a real installation dropped every subdirectory — and
// `\SAVE\` is where a shipped save slot lives, which is why a browser
// could start a game and never resume one (docs/playable.md).
//
// This is the widened door, end to end and on the machine a browser runs:
// a page hands over `SAVE/SAVE1.DAT` in the browser's own spelling, core
// makes `\SAVE` and puts the file in it, and a program of our own opens
// `\SAVE\SAVE1.DAT` in DOS's spelling and prints the byte it read. That
// the two spellings are one file is the part core decides and neither
// host may (abi.h); that the byte comes back is the part the whole issue
// was about.

if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const machine = new Machine(module);
  check(
    machine.attachReferenceDevices() === AF_OK,
    'attaching the reference devices failed',
  );
  machine.reset();

  // The path the *program* asks DOS for. The page hands the same file
  // over as `SAVE/SAVE1.DAT` below.
  const dosPath = '\\SAVE\\SAVE1.DAT';

  // Laid out by hand, because the image carries its own data: the code
  // first, then the ASCIZ path, then a one-byte buffer. DS is set from CS
  // so that both are addressed at their offset in the image, which is
  // where the loader puts CS:0000.
  const codeLength = 44;
  const pathAt = codeLength;
  const bufferAt = pathAt + dosPath.length + 1;
  const code = [
    0x8c, 0xc8, //             MOV AX, CS
    0x8e, 0xd8, //             MOV DS, AX
    0xba, pathAt, 0x00, //     MOV DX, path
    0xb8, 0x00, 0x3d, //       MOV AX, 3D00h   ; open, read-only
    0xcd, 0x21, //             INT 21h
    0x72, 0x19, //             JC  refused
    0x89, 0xc3, //             MOV BX, AX      ; the handle DOS gave us
    0xba, bufferAt, 0x00, //   MOV DX, buffer
    0xb9, 0x01, 0x00, //       MOV CX, 1
    0xb4, 0x3f, //             MOV AH, 3Fh     ; read
    0xcd, 0x21, //             INT 21h
    0x8a, 0x16, bufferAt, 0x00, // MOV DL, [buffer]
    0xb4, 0x02, //             MOV AH, 02h     ; console output
    0xcd, 0x21, //             INT 21h
    0xb8, 0x00, 0x4c, //       MOV AX, 4C00h   ; exit 0
    0xcd, 0x21, //             INT 21h
    // refused:
    0xb8, 0x01, 0x4c, //       MOV AX, 4C01h   ; exit 1
    0xcd, 0x21, //             INT 21h
  ];
  check(
    code.length === codeLength,
    `the reader program is ${code.length} bytes, not the ${codeLength} its ` +
      'own offsets were computed from',
  );

  const image = new Uint8Array(bufferAt + 1);
  image.set(code, 0);
  for (let i = 0; i < dosPath.length; ++i) image[pathAt + i] = dosPath.charCodeAt(i);
  const exe = makeMzImage(image);

  check(machine.vfsPut('READSAVE.EXE', exe) === AF_OK, 'putting READSAVE.EXE failed');

  // The put a browser makes: the browser's separator, the player's own
  // capitalization, and a directory nothing has made yet.
  const slot = new Uint8Array([0x53]); // 'S'
  check(
    machine.vfsPut('save/Save1.Dat', slot) === AF_OK,
    'putting a file into a subdirectory was refused',
  );
  // A component no DOS short name can equal is still the useful refusal,
  // anywhere in the path rather than only at its end.
  check(
    machine.vfsPut('SAVE/code wheel.pdf', slot) === AF_INVALID,
    'a path with a space in a component was accepted',
  );

  // The root listing is the root's (abi.h): the program, and the
  // directory core made on the way to the file. Pinned name order, so
  // `READSAVE.EXE` comes before `SAVE`.
  const listing = machine.vfsList();
  check(
    listing.length === 2 &&
      listing[0]?.name === 'READSAVE.EXE' &&
      listing[1]?.name === 'SAVE',
    `the root holds ${JSON.stringify(listing.map((e) => e.name))}, expected ` +
      "['READSAVE.EXE', 'SAVE']",
  );
  check(
    listing[1]?.size === 0,
    `the directory core made reports ${listing[1]?.size} bytes, expected 0`,
  );

  // One file, either spelling — the identity of a player's file is taken
  // in core and does not depend on how a host spelled the way to it.
  const digest = machine.vfsFingerprint('save/Save1.Dat');
  check(
    typeof digest === 'string' && /^[0-9a-f]{64}$/.test(digest),
    `the fingerprint of the saved file is ${JSON.stringify(digest)}`,
  );
  check(
    machine.vfsFingerprint(dosPath) === digest,
    "the browser's spelling and DOS's spelling fingerprint differently",
  );

  const loadStatus = machine.loadFromVfs('READSAVE.EXE', '');
  check(
    loadStatus === AF_OK,
    `loading READSAVE.EXE answered ${loadStatus} (loader error ${machine.loadError()})`,
  );

  let text = '';
  for (let frame = 0; frame < 60 && !machine.stopped(); ++frame) {
    machine.runUntil(machine.ticksPerSecond() * ((frame + 1) / 60));
    const bytes = machine.readConsole();
    if (bytes.length > 0) text += decodeConsoleBytes(bytes);
  }
  // 'S' is the byte the page put in the file. Anything else — including
  // nothing — means DOS did not open the file the page wrote, which is
  // the failure #146 was filed about.
  check(
    text === 'S',
    `the program printed ${JSON.stringify(text)}, expected "S" — the byte ` +
      'the page put in the file below the root',
  );
  check(machine.stopped(), 'the reader program never exited');
  check(
    machine.stopReport(AF_RUN_END_STOPPED).includes('amberfolio: stop exit=0'),
    'the reader program did not exit 0, so it could not open the file',
  );

  console.log(
    'smoke: a file put below the root was opened by the program at its own ' +
      'DOS path',
  );

  machine.destroy();
}

// --- A disk of the real shape, and the two ways one is refused (#158) ----
//
// A shipped installation is a flat root of about a hundred and twenty
// files with a `SAVE\` under it already holding the slot files an archive
// release ships — 195 entries before a player has saved anything. The
// backend held 192, so the table filled part-way through the disk: seven
// of the game's own data files were refused, the game booted and ran
// without them, and the symptom loud enough to notice was that no save
// could be written at all.
//
// Two things are checked here, and the second is the one that survives
// the next disk. First, that shape goes in whole and a save can still be
// made after it. Second, when a filesystem *is* full, the refusal says
// so — `AF_NO_ROOM`, distinct from the `AF_INVALID` that a path DOS
// could never have named gets. Both were `AF_INVALID` before, and a
// skipped list of eight things read as eight of the same thing.
//
// No game content is here (CONTRIBUTING.md): the entry counts are the
// fact, and every name below is this file's own.

if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const machine = new Machine(module);
  check(
    machine.attachReferenceDevices() === AF_OK,
    'attaching the reference devices failed',
  );
  machine.reset();

  const byte = new Uint8Array([0x2a]);
  const rootFiles = 122;
  const shippedSlots = 72;

  let refused = 0;
  for (let i = 0; i < rootFiles; ++i) {
    if (machine.vfsPut(`DATA${i}.DAT`, byte) !== AF_OK) refused += 1;
  }
  for (let i = 0; i < shippedSlots; ++i) {
    if (machine.vfsPut(`SAVE/SLOT${i}.DAT`, byte) !== AF_OK) refused += 1;
  }
  check(
    refused === 0,
    `${refused} of a ${rootFiles + shippedSlots}-file installation were ` +
      'refused; the disk the browser boots would have holes in it',
  );
  // The root holds its files plus the one directory core made on the way
  // — 190 of 195 taken was the bug.
  const rootListing = machine.vfsList();
  check(
    rootListing.length === rootFiles + 1,
    `the root holds ${rootListing.length} entries, expected ${rootFiles + 1}`,
  );
  check(
    rootListing.some((entry) => entry.name === 'SAVE'),
    'the SAVE directory core made on the way is not in the root listing',
  );

  // What the bug's symptom was: a save still has somewhere to go, and it
  // is seven files — the slot, and a character file for each of a party
  // of six.
  let saved = 0;
  if (machine.vfsPut('SAVE/SAVGAMA.DAT', byte) === AF_OK) saved += 1;
  for (let i = 0; i < 6; ++i) {
    if (machine.vfsPut(`SAVE/CHAR${i}.CHA`, byte) === AF_OK) saved += 1;
  }
  check(saved === 7, `only ${saved} of a 7-file save could be written`);

  // Now fill it the rest of the way and read the refusal. The bound is
  // core's and this side does not restate it — it puts files until one
  // does not fit, which is exactly what a page does.
  let held = 0;
  let full = AF_OK;
  for (let i = 0; i < 4096; ++i) {
    const status = machine.vfsPut(`FILL${i}.DAT`, byte);
    if (status !== AF_OK) {
      full = status;
      break;
    }
    held += 1;
  }
  check(
    full === AF_NO_ROOM,
    `a full filesystem answered ${full}, expected AF_NO_ROOM (${AF_NO_ROOM})`,
  );
  check(
    describeSkip(full) === 'no room left on the disk',
    `a full filesystem is described as ${JSON.stringify(describeSkip(full))}`,
  );
  check(
    held > 0,
    'the filesystem was already full before this test put anything in it',
  );

  // And the other refusal, from that same full filesystem. These two
  // being one status is what made a browser report seven missing game
  // data files in the same sentence as a PDF it was right to ignore.
  const unnameable = machine.vfsPut('code wheel.pdf', byte);
  check(
    unnameable === AF_INVALID,
    `a name no DOS short name can equal answered ${unnameable}`,
  );
  check(
    describeSkip(unnameable) === 'not a DOS-nameable path',
    `an unnameable path is described as ${JSON.stringify(describeSkip(unnameable))}`,
  );
  check(
    describeSkip(full) !== describeSkip(unnameable),
    'a full disk and an illegal name are still reported the same way',
  );

  // The move abi.h tells a host that ran out of room to make.
  check(machine.vfsClear() === AF_OK, 'clearing a full filesystem failed');
  check(
    machine.vfsPut('WALLDEF3.DAX', byte) === AF_OK,
    'a cleared filesystem still had no room',
  );

  console.log(
    'smoke: a 195-entry installation went in whole, a save was written ' +
      'after it, and a full disk says so rather than saying "bad name"',
  );

  machine.destroy();
}

// --- A seam, toggled through the ABI (M4-F4, #98) -------------------------
//
// The probe program tests/programs runs with its seam on and off, staged
// here through the web host's own export and loaded off the filesystem
// like a player's program. What this asserts is the toggle surface a page
// sits on: the program is identified as it loads (unrecognized edition,
// which is the honest answer for a test program), the seam is listed as
// off and available, the code-wheel seam is listed as unavailable with
// its reason, and turning the probe on changes the result block in
// exactly the way the native suite asserts it does.
//
// And, since #147, what the seams *did*. Two seams are registered, not
// one: `probe`, whose points the program runs through, and
// `probe-unreached`, armed on an instruction past the exit that the
// program never executes. Both are keyed to the same file, so both are
// available and both arm - `af_machine_seam_armed` cannot tell them
// apart. `af_machine_seam_fired` can, and the assertion that
// `probe-unreached` finishes a whole run at zero is the one this issue
// was filed for: "armed and fired nothing" is the failure that reads
// exactly like success, and a browser had no way to say it.

if (missing.length === 0) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const runProbe = (seamOn) => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    machine.reset();
    check(
      module._af_web_probe_seam_register(machine.handle) === AF_OK,
      'the probe seams could not be registered',
    );

    const ptr = module._af_web_probe_program_bytes();
    const size = module._af_web_probe_program_size();
    const exe = module.HEAPU8.slice(ptr, ptr + size);
    check(size > 0, 'the probe program is empty');
    check(machine.vfsPut('PROBE.EXE', exe) === AF_OK, 'putting PROBE.EXE failed');

    // Before the load: no program is known, so every seam is unavailable
    // for want of one.
    check(machine.edition() === null, 'an edition was reported before anything loaded');
    check(
      machine.seamList().every((s) => s.state === AF_SEAM_UNAVAILABLE && s.reason === 'no_program'),
      'seams were available before a program was loaded',
    );

    const loadStatus = machine.loadFromVfs('PROBE.EXE', '');
    check(loadStatus === AF_OK, `loading PROBE.EXE answered ${loadStatus}`);

    // The load identified the program: its fingerprint is known, it is no
    // edition this build recognizes, and the listing says which seams
    // apply to it.
    const fingerprint = machine.programFingerprint();
    check(
      typeof fingerprint === 'string' && /^[0-9a-f]{64}$/.test(fingerprint),
      `the program's fingerprint is ${JSON.stringify(fingerprint)}`,
    );
    check(fingerprint === machine.vfsFingerprint('PROBE.EXE'), 'the machine and the filesystem disagree about the fingerprint');
    check(machine.edition() === null, 'a test program was recognized as a known edition');

    const before = machine.seamList();
    const probe = before.find((s) => s.id === 'probe');
    const wheel = before.find((s) => s.id === 'code-wheel');
    check(probe !== undefined, 'the probe seam is not listed');
    check(probe?.state === AF_SEAM_OFF && probe?.reason === 'none', `the probe seam starts ${JSON.stringify(probe)}`);
    check(probe?.fired === 0, `a seam that is off has fired ${JSON.stringify(probe?.fired)} times`);
    check(wheel !== undefined, 'the code-wheel seam is not listed');
    check(
      wheel?.state === AF_SEAM_UNAVAILABLE && wheel?.reason === 'wrong_binary',
      `the code-wheel seam should be unavailable for a test program: ${JSON.stringify(wheel)}`,
    );

    if (seamOn) {
      check(machine.seamEnable('probe') === AF_OK, 'enabling the probe seam was refused');
      const on = machine.seamList().find((s) => s.id === 'probe');
      check(on?.state === AF_SEAM_ON && on?.armed === true, `the probe seam did not arm: ${JSON.stringify(on)}`);
      check(on?.fired === 0, `the probe seam fired ${on?.fired} times before a step was taken`);

      // The pair (#147). This one arms exactly as the working seam does
      // - same program, same fingerprint, `armed === true` - and its
      // point is on an instruction past the program's exit. Nothing
      // visible before the run distinguishes the two.
      check(
        machine.seamEnable('probe-unreached') === AF_OK,
        'enabling the unreached probe seam was refused',
      );
      const never = machine.seamList().find((s) => s.id === 'probe-unreached');
      check(
        never?.state === AF_SEAM_ON && never?.armed === true,
        `the unreached probe seam did not arm: ${JSON.stringify(never)}`,
      );

      check(machine.seamEnable('code-wheel') === AF_INVALID, 'an unavailable seam was enabled');
      check(machine.seamEnable('no-such-seam') === AF_INVALID, 'a seam that does not exist was enabled');
    }

    for (let frame = 0; frame < 120 && !machine.stopped(); ++frame) {
      machine.runUntil(machine.ticksPerSecond() * ((frame + 1) / 60));
    }
    check(machine.stopped(), `the probe program never exited (seam ${seamOn ? 'on' : 'off'})`);
    check(
      machine.stopReport(AF_RUN_END_STOPPED).includes('amberfolio: stop exit=136'),
      `the probe program did not exit 88h (seam ${seamOn ? 'on' : 'off'})`,
    );

    // The result block: two words at image segment 0060h, offset 0800h
    // (tests/programs/machine_harness.h) — physical 0x600 + 0x800. Read
    // through host.mjs's own wrapper, not the raw export: this check is
    // meant to be the page's code path, and a `_af_machine_read_memory`
    // called by hand here was a wrapper that did not exist saying it did
    // (#108).
    const block = machine.readMemory(0x600 + 0x800, 4);
    check(block !== null, 'reading the result block failed');
    check(
      machine.readMemory(0x0f_ff_ff, 4) === null,
      'a read that runs off the end of the megabyte was allowed',
    );
    const words = block === null
      ? [0, 0]
      : [block[0] | (block[1] << 8), block[2] | (block[3] << 8)];

    // What the seams did, read off the run that just ended - the browser
    // half of the line the desktop host prints at the end of every run
    // (#131, #147).
    const after = machine.seamList();
    const fired = Object.fromEntries(after.map((s) => [s.id, s.fired]));
    const states = Object.fromEntries(after.map((s) => [s.id, s.state]));
    const lines = Object.fromEntries(after.map((s) => [s.id, formatSeamFired(s)]));
    machine.destroy();
    return { words, fired, states, lines };
  };

  const off = runProbe(false);
  check(off.words[0] === 0x1111 && off.words[1] === 0x0000, `seam off: result block is ${JSON.stringify(off.words.map((w) => w.toString(16)))}, expected 1111, 0`);
  const on = runProbe(true);
  check(on.words[0] === 0x2222 && on.words[1] === 0x256b, `seam on: result block is ${JSON.stringify(on.words.map((w) => w.toString(16)))}, expected 2222, 256b`);

  // --- What the seams did, not what they were armed at (#147) -----------
  //
  // Three claims, and the middle one is the issue:
  //
  //   * a seam that ran reports a count a browser can see;
  //   * a seam that armed and was never reached reports **zero** - the
  //     failure #131 spent a milestone not seeing, because `armed` says
  //     the same thing either way;
  //   * a seam left off reports zero, so the count belongs to the enable
  //     it was made under and not to the machine's whole life.
  check(on.fired.probe > 0, `the probe seam ran but reports fired=${on.fired.probe}`);
  check(
    on.fired['probe-unreached'] === 0,
    'a seam armed where the program never goes reports ' +
      `fired=${on.fired['probe-unreached']}, and the whole point of the count is that it is 0`,
  );
  check(
    on.states['probe-unreached'] === AF_SEAM_ON,
    'the unreached probe seam was not still on at the end of the run',
  );
  check(
    off.fired.probe === 0 && off.fired['probe-unreached'] === 0,
    `a seam that was never enabled reports ${JSON.stringify(off.fired)}`,
  );

  // The line both JS surfaces print, checked as the pure function it is
  // (host.mjs). The driver and the dev page share it precisely so a
  // browser run and a desktop run can be compared as two runs rather than
  // as two spellings.
  //
  // What it does **not** decide any more is what the row means (#163):
  // `reading` is the finished sentence, worked out once in core and
  // carried over by `af_machine_seam_reading`, and all this checks is
  // that the numbers are laid out right and the sentence goes on the
  // end. The sentences themselves are asserted below against rows from
  // runs that really happened, which is a stronger claim than a string
  // pinned by hand here could be — a hand-built row can be made to say
  // anything, including something the C++ never says.
  check(
    formatSeamFired({ armed: true, fired: 9 }) === 'armed fired=9',
    `a fired seam formats as "${formatSeamFired({ armed: true, fired: 9 })}"`,
  );
  check(
    formatSeamFired({ armed: false, fired: 0 }) === 'inert fired=0',
    `an inert seam formats as "${formatSeamFired({ armed: false, fired: 0 })}"`,
  );
  check(
    formatSeamFired({ armed: true, fired: 0, reading: ' - a sentence core decided' }) ===
      'armed fired=0 - a sentence core decided',
    'the reading is not appended to the row',
  );
  check(
    formatSeamFired({ armed: true, fired: 1, trigger: true, reached: 12, waited: 1868720 }) ===
      'armed fired=1 reached=12 waited=1868720',
    'a served pull does not report what it waited',
  );
  check(
    formatSeamFired({ armed: true, fired: 0, trigger: true, reached: 12, waiting: true }) ===
      'armed fired=0 reached=12 waiting',
    'a pending pull does not show as waiting',
  );

  // And the two rows that must never contradict themselves, from runs
  // that happened. `probe` acted; `probe-unreached` armed at an address
  // the program never executes, which is #131's failure built on
  // purpose, and is the one row that has earned the warning.
  check(
    on.lines.probe === `armed fired=${Math.round(on.fired.probe)}`,
    `a seam that acted carries a sentence: "${on.lines.probe}"`,
  );
  check(
    on.lines['probe-unreached'] ===
      'armed fired=0 - armed and never reached; its point may not be where its facts say',
    `the unreached seam's row reads "${on.lines['probe-unreached']}"`,
  );

  // --- The trigger (#161) -----------------------------------------------
  //
  // The host -> seam direction, asked of a browser. `probe-trigger` is
  // the same program, the same point and the same handler as `probe`'s
  // register edit, declared as a trigger — so the only thing that
  // decides whether the result block carries 1111h or 2222h is whether
  // anybody pulled it. On and never asked has to be the plain machine's
  // run, which is #96's rule with #161's latch in it.
  const runTrigger = (pull) => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    machine.reset();
    check(
      module._af_web_probe_seam_register(machine.handle) === AF_OK,
      'the probe seams could not be registered',
    );
    const ptr = module._af_web_probe_program_bytes();
    const size = module._af_web_probe_program_size();
    check(machine.vfsPut('PROBE.EXE', module.HEAPU8.slice(ptr, ptr + size)) === AF_OK, 'putting PROBE.EXE failed');
    check(machine.loadFromVfs('PROBE.EXE', '') === AF_OK, 'loading PROBE.EXE failed');

    const listed = machine.seamList().find((s) => s.id === 'probe-trigger');
    check(listed?.trigger === true, `probe-trigger is not listed as a trigger: ${JSON.stringify(listed)}`);
    check(
      machine.seamPull('probe-trigger') === AF_INVALID,
      'a trigger on a seam that is off was accepted',
    );
    check(machine.seamEnable('probe-trigger') === AF_OK, 'enabling probe-trigger was refused');
    check(
      machine.seamPull('probe') === AF_INVALID,
      'a seam that takes no trigger accepted one',
    );
    if (pull) {
      check(machine.seamPull('probe-trigger') === AF_OK, 'pulling probe-trigger was refused');
      const waiting = machine.seamList().find((s) => s.id === 'probe-trigger');
      check(waiting?.waiting === true, `a pull did not show as waiting: ${JSON.stringify(waiting)}`);
    }

    for (let frame = 0; frame < 120 && !machine.stopped(); ++frame) {
      machine.runUntil(machine.ticksPerSecond() * ((frame + 1) / 60));
    }
    check(machine.stopped(), `the probe program never exited (trigger ${pull ? 'pulled' : 'idle'})`);
    const block = machine.readMemory(0x600 + 0x800, 4);
    const words = block === null ? [0, 0] : [block[0] | (block[1] << 8), block[2] | (block[3] << 8)];
    const row = machine.seamList().find((s) => s.id === 'probe-trigger');
    machine.destroy();
    return { words, row };
  };

  const idle = runTrigger(false);
  check(
    idle.words[0] === 0x1111,
    `a trigger nobody pulled changed the run: ${idle.words[0].toString(16)}`,
  );
  check(idle.row?.fired === 0, `a trigger nobody pulled fired ${idle.row?.fired} times`);
  check(
    idle.row?.reached > 0,
    'a trigger nobody pulled was never reached either, so the equality above says nothing',
  );
  check(idle.row?.waiting === false, 'a trigger nobody pulled reports a pull outstanding');

  const pulled = runTrigger(true);
  check(
    pulled.words[0] === 0x2222,
    `a pulled trigger did not act: ${pulled.words[0].toString(16)}`,
  );
  check(pulled.row?.fired === 1, `a pulled trigger fired ${pulled.row?.fired} times, expected 1`);
  check(pulled.row?.waiting === false, 'a served pull is still outstanding');
  check(
    pulled.row?.reached === idle.row?.reached,
    `the point was reached ${pulled.row?.reached} times pulled and ${idle.row?.reached} times not; ` +
      'the arrival is the same either way and only what happens there differs',
  );

  // --- The point with no address (#163) ---------------------------------
  //
  // `probe-pull`'s one point has no address at all: it is offered at
  // every step boundary while the pull is outstanding, declines until
  // the program has stored its own first answer, and marks a third
  // result word at the first step after that. Asked of a browser because
  // "one bool per step when nobody pulled" is a claim about a hot path,
  // and hot paths differ per target.
  const runPullPoint = (pull) => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    machine.reset();
    check(
      module._af_web_probe_seam_register(machine.handle) === AF_OK,
      'the probe seams could not be registered',
    );
    const ptr = module._af_web_probe_program_bytes();
    const size = module._af_web_probe_program_size();
    check(machine.vfsPut('PROBE.EXE', module.HEAPU8.slice(ptr, ptr + size)) === AF_OK, 'putting PROBE.EXE failed');
    check(machine.loadFromVfs('PROBE.EXE', '') === AF_OK, 'loading PROBE.EXE failed');
    check(machine.seamEnable('probe-pull') === AF_OK, 'enabling probe-pull was refused');
    if (pull) {
      check(machine.seamPull('probe-pull') === AF_OK, 'pulling probe-pull was refused');
    }
    for (let frame = 0; frame < 120 && !machine.stopped(); ++frame) {
      machine.runUntil(machine.ticksPerSecond() * ((frame + 1) / 60));
    }
    check(machine.stopped(), `the probe program never exited (pull point ${pull ? 'pulled' : 'idle'})`);
    const block = machine.readMemory(0x600 + 0x800, 6);
    const mark = block === null ? -1 : block[4] | (block[5] << 8);
    const row = machine.seamList().find((s) => s.id === 'probe-pull');
    machine.destroy();
    return { mark, row };
  };

  const notPulled = runPullPoint(false);
  check(
    notPulled.mark === 0,
    `a point with no address acted without being pulled: ${notPulled.mark.toString(16)}`,
  );
  check(notPulled.row?.fired === 0, `an unpulled point with no address fired ${notPulled.row?.fired} times`);
  check(
    notPulled.row?.reached === 0,
    'a point with no address counted an arrival, and it has no address to arrive at',
  );

  const pulledPoint = runPullPoint(true);
  check(
    pulledPoint.mark === 0x3333,
    `a pulled point with no address left no mark: ${pulledPoint.mark.toString(16)}`,
  );
  check(
    pulledPoint.row?.fired === 1,
    `a pulled point with no address fired ${pulledPoint.row?.fired} times, expected 1 — ` +
      'the offers it declined are not firings',
  );
  check(pulledPoint.row?.waiting === false, 'a served pull is still outstanding');

  // --- What each row *means*, from four runs that happened (#163) -------
  //
  // The sentence is core's (`machine::seam_reading_of`), the same one the
  // desktop host prints, and these are the states a person is actually
  // looking at. The last pair is the defect this all exists for: a seam
  // served by a point with no address reports `fired=1 reached=0`, and
  // every earlier reading keyed on `reached === 0` called that a broken
  // address table — `fired=1` and "never reached" cannot both be true.
  check(
    idle.row?.reading === ' - reached, and never pulled; this seam acts only when asked',
    `an unpulled trigger's row reads "${idle.row?.reading}"`,
  );
  check(
    pulled.row?.reading === ' - pulled, and served',
    `a served trigger's row reads "${pulled.row?.reading}"`,
  );
  check(
    notPulled.row?.reading === '',
    'a point with no address that nobody pulled says something, and there is ' +
      `nothing to say: "${notPulled.row?.reading}"`,
  );
  check(
    pulledPoint.row?.reading === ' - pulled, and served',
    `a served point with no address reads "${pulledPoint.row?.reading}"`,
  );
  check(
    !notPulled.row?.reading.includes('never reached') &&
      !pulledPoint.row?.reading.includes('never reached'),
    'a seam with no addressed point was told its address may be wrong',
  );
  check(
    formatSeamFired(pulledPoint.row) ===
      `armed fired=1 reached=0 waited=${Math.round(pulledPoint.row?.waited)} - pulled, and served`,
    `a served address-free point's whole line reads "${formatSeamFired(pulledPoint.row)}"`,
  );

  console.log(
    'smoke: the probe seam listed, toggled, edited a register, posted a key ' +
      `and reported fired=${on.fired.probe}; the unreached one reported fired=0; ` +
      `the trigger acted only when pulled (reached=${pulled.row?.reached} either way); ` +
      `the address-free point read "${formatSeamFired(pulledPoint.row)}"`,
  );
}

// --- The keys a keyboard-driven game needs (#84) -------------------------
//
// The table itself is a pure function and needs no module, so this runs
// whatever else did. It is here because the arrows and the Page keys were
// simply absent until #84 — a gap that made a keyboard-driven game
// unplayable in a browser while the desktop host had had them all along,
// and exactly the kind of thing that is wrong in one row and right in
// every other.
{
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  // On an 83-key board the cursor keys *are* the keypad, so these share
  // their scancodes — which is what a real XT keyboard does, not an
  // approximation.
  const expected = {
    ArrowUp: 0x48,
    ArrowDown: 0x50,
    ArrowLeft: 0x4b,
    ArrowRight: 0x4d,
    Home: 0x47,
    End: 0x4f,
    PageUp: 0x49,
    PageDown: 0x51,
    Insert: 0x52,
    Delete: 0x53,
    Escape: 0x01,
    Enter: 0x1c,
    F1: 0x3b,
    F10: 0x44,
  };
  for (const [code, scancode] of Object.entries(expected)) {
    check(
      scancodeFor(code) === scancode,
      `${code} maps to ${scancodeFor(code)}, expected 0x${scancode.toString(16)}`,
    );
  }
  // And keys the 83-key board never had stay absent, so the browser keeps
  // its own shortcuts for them (app.mjs only calls preventDefault on keys
  // the machine claims).
  for (const code of ['F11', 'F12', 'MetaLeft', 'ContextMenu']) {
    check(
      scancodeFor(code) === undefined,
      `${code} was mapped to a scancode the 83-key board does not have`,
    );
  }
  console.log(`smoke: ${Object.keys(expected).length} keyboard rows check out`);
}


// `JMP $` behind a two-paragraph MZ header: the program
// `tests/sessions/spin.rec` was recorded of. It never stops, so every
// checkpoint taken of it is of a running machine and a recording of it
// ends on a tick its recorder chose.
//
// Read off the session's own disk rather than assembled here. A session
// is a recording *plus* the disk it was recorded against — one copy of
// the bytes, pinned by the recording's own manifest, and the same pair
// `scripts/sweep.py` hands to the desktop host.
const spinner = sessions === null
  ? new Uint8Array(0)
  : new Uint8Array(readFileSync(`${sessions}/spin/SPIN.EXE`));

// --- Replay, on wasm (#100) ----------------------------------------------
//
// The claim M4-R1 exists to make is that a run is *keys, ticks and
// hashes* and that a machine handed those three reproduces it — on every
// target, from the same core. The native suite makes it in
// `AbiReplay.ARecordingBuiltFromAbiAnswersVerifiesThroughTheAbi`; this is
// the same check on the target that shares none of the native toolchain,
// which is the only place it could plausibly come apart. If SHA-256, the
// state layout, the scheduler's tie-break or the EGA's arithmetic differed
// by one byte under Emscripten, a recording made here would not verify
// here — and one made on a desktop would not verify here either, which is
// what the committed session library (#101) will go on to assert.
//
// The recording is built out of nothing but ABI answers, so this also
// pins `stateHash()` and `verifyRecording()` against each other: two
// functions that disagreed about what the machine is would fail this and
// nothing else.

if (missing.length === 0 && sessions !== null) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  check(spinner.length === 34, `tests/sessions/spin/SPIN.EXE is ${spinner.length} bytes, expected 34`);

  // One 60 Hz frame of virtual time — the boundary the verifier runs to,
  // and so the boundary a recording it will read must checkpoint on.
  const frameTicks = 1193182 / 60;

  const equipped = () => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    // The RESET line, which programs the PIT and the 8259 through real
    // bus cycles. A machine that skipped it has different device state
    // from one that powered on, and a recording is about device state.
    machine.reset();
    check(machine.vfsPut('SPIN.EXE', spinner) === AF_OK, 'putting SPIN.EXE failed');
    check(machine.loadFromVfs('SPIN.EXE', '') === AF_OK, 'loading SPIN.EXE failed');
    return machine;
  };

  const recorder = equipped();
  const fingerprint = recorder.programFingerprint();
  check(
    typeof fingerprint === 'string' && fingerprint.length === 64,
    `the program's fingerprint is ${JSON.stringify(fingerprint)}`,
  );

  const lines = [
    'amberfolio-recording 1 state=1',
    `program SPIN.EXE ${fingerprint}`,
    'tail',
    `file SPIN.EXE 34 ${recorder.vfsFingerprint('SPIN.EXE')}`,
  ];

  const first = recorder.stateHash();
  check(
    typeof first === 'string' && first.length === 64 && /^[0-9a-f]+$/.test(first),
    `the state hash is ${JSON.stringify(first)}`,
  );

  for (let frame = 1; frame <= 4; ++frame) {
    check(recorder.runUntil(frameTicks * frame) === AF_OK, `the machine stopped in frame ${frame}`);
    lines.push(`checkpoint ${Math.round(recorder.time())} ${Math.round(recorder.steps())} ${recorder.stateHash()}`);
  }
  check(recorder.stateHash() !== first, 'the state hash did not move as the machine ran');
  lines.push(`end ${Math.round(recorder.time())} ${Math.round(recorder.steps())}`);
  recorder.destroy();

  const text = `${lines.join('\n')}\n`;

  const player = equipped();
  const verdict = player.verifyRecording(text);
  check(verdict.ok, `the recording did not verify: ${verdict.report}`);
  check(
    verdict.report.includes('replay verified checkpoints=4'),
    `the report is ${JSON.stringify(verdict.report)}`,
  );
  player.destroy();

  // And it can fail. One wrong checkpoint hash, and the same machine
  // refuses the same recording — otherwise everything above is a test of
  // a function that always says yes.
  const tampered = text.replace(/(checkpoint \d+ \d+ )[0-9a-f]{64}/, `$1${'a'.repeat(64)}`);
  check(tampered !== text, 'the recording could not be tampered with');
  const skeptic = equipped();
  const refused = skeptic.verifyRecording(tampered);
  check(!refused.ok, 'a recording with a wrong checkpoint hash was accepted');
  check(
    refused.report.includes('replay diverged'),
    `a tampered recording was refused, but as ${JSON.stringify(refused.report)}`,
  );
  skeptic.destroy();

  console.log('smoke: a recording built from ABI answers verified on wasm, and a tampered one did not');
}

// --- The committed session library, on wasm (#100) -----------------------
//
// The other half of `tests/core/machine/session_test.cpp`, and the reason
// either exists: the same file, verified by two builds that share no
// compiler, no standard library and no SHA-256 implementation. A pair of
// builds that agreed about a program's answer and disagreed about the
// machine underneath it would fail here and nowhere else.
//
// tests/sessions/README.md says what each session pins and the short list
// of changes that may legitimately re-record one. A red result here is
// not on that list.

if (missing.length === 0 && sessions !== null) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const text = readFileSync(`${sessions}/spin.rec`, 'latin1');
  check(text.length > 0, 'tests/sessions/spin.rec is empty');

  const loaded = () => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    machine.reset();
    check(machine.vfsPut('SPIN.EXE', spinner) === AF_OK, 'putting SPIN.EXE failed');
    check(machine.loadFromVfs('SPIN.EXE', '') === AF_OK, 'loading SPIN.EXE failed');
    return machine;
  };

  const machine = loaded();
  const verdict = machine.verifyRecording(text);
  check(
    verdict.ok,
    `tests/sessions/spin.rec did not verify on wasm: ${verdict.report}\n` +
      '        tests/sessions/README.md says when a session may legitimately be\n' +
      '        re-recorded. If none of those changed, this is a finding about the\n' +
      '        machine and not about the golden.',
  );
  check(
    verdict.report.includes('replay verified checkpoints=4'),
    `the report is ${JSON.stringify(verdict.report)}`,
  );
  machine.destroy();

  // And it can fail here too.
  const tampered = text.replace(/(checkpoint \d+ \d+ )[0-9a-f]{64}/, `$1${'a'.repeat(64)}`);
  check(tampered !== text, 'the committed session could not be tampered with');
  const skeptic = loaded();
  const refused = skeptic.verifyRecording(tampered);
  check(!refused.ok, 'a committed session with a wrong checkpoint hash was accepted');
  skeptic.destroy();

  console.log('smoke: the committed session tests/sessions/spin.rec verified on wasm');
}

// --- The manifest reaches the saved game (#155) ---------------------------
//
// A recording's manifest is the statement of what disk the run started
// from. Until #155 it named the root and nothing below it, so a `.rec`
// verified **on its own** — which is what `af_machine_verify_recording`
// is, and the browser's only path to one — could begin from a different
// saved party and not say so, diverging thousands of frames later at a
// checkpoint hash. That reads as a finding about the machine and is
// really a finding about a directory.
//
// Here on wasm rather than only natively because this is the target where
// the ABI *is* the surface: a page hands over a whole installation, saves
// and all (#146), and the recording has to speak for every byte of it.

if (missing.length === 0 && sessions !== null) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const frameTicks = 1193182 / 60;
  const saved = new Uint8Array([0x53]);

  // A machine holding the spinner at the root and one saved-game file
  // below it, the shape a player's installation arrives in.
  const equipped = (save) => {
    const machine = new Machine(module);
    check(machine.attachReferenceDevices() === AF_OK, 'attaching the reference devices failed');
    machine.reset();
    check(machine.vfsPut('SPIN.EXE', spinner) === AF_OK, 'putting SPIN.EXE failed');
    check(machine.vfsPut('SAVE/SAVE1.DAT', save) === AF_OK, 'putting SAVE/SAVE1.DAT failed');
    check(machine.loadFromVfs('SPIN.EXE', '') === AF_OK, 'loading SPIN.EXE failed');
    return machine;
  };

  const recorder = equipped(saved);
  // Depth first, each directory's entries in the VFS's pinned name order,
  // a directory's own line before its contents — so `SAVE` and everything
  // under it come before `SPIN.EXE`, and the directory itself carries
  // neither a size nor a digest because it has neither.
  const lines = [
    'amberfolio-recording 2 state=1',
    `program SPIN.EXE ${recorder.programFingerprint()}`,
    'tail',
    'dir SAVE',
    `file SAVE\\SAVE1.DAT ${saved.length} ${recorder.vfsFingerprint('SAVE/SAVE1.DAT')}`,
    `file SPIN.EXE ${spinner.length} ${recorder.vfsFingerprint('SPIN.EXE')}`,
  ];
  for (let frame = 1; frame <= 4; ++frame) {
    check(recorder.runUntil(frameTicks * frame) === AF_OK, `the machine stopped in frame ${frame}`);
    lines.push(`checkpoint ${Math.round(recorder.time())} ${Math.round(recorder.steps())} ${recorder.stateHash()}`);
  }
  lines.push(`end ${Math.round(recorder.time())} ${Math.round(recorder.steps())}`);
  recorder.destroy();
  const text = `${lines.join('\n')}\n`;

  const player = equipped(saved);
  const verdict = player.verifyRecording(text);
  check(verdict.ok, `a recording naming a nested file did not verify: ${verdict.report}`);
  player.destroy();

  // One byte of the saved game, one run further along. Refused at the
  // manifest, naming the file — not at a checkpoint, and not as a
  // divergence.
  const skeptic = equipped(new Uint8Array([0x54]));
  const refused = skeptic.verifyRecording(text);
  check(!refused.ok, 'a disk whose saved game differs was accepted');
  check(
    refused.report.includes('replay refused') &&
      refused.report.includes('path=SAVE\\SAVE1.DAT'),
    `the refusal does not name the nested file: ${JSON.stringify(refused.report)}`,
  );
  check(
    !refused.report.includes('diverged'),
    `the wrong disk was reported as a divergence: ${JSON.stringify(refused.report)}`,
  );
  skeptic.destroy();

  // And the sensitivity that makes the two checks above mean something:
  // with the nested lines taken out, the recording is not a description
  // of this disk at all and is refused for that instead.
  const rootOnly = text
    .split('\n')
    .filter((line) => !line.startsWith('dir ') && !line.startsWith('file SAVE\\'))
    .join('\n');
  const partial = equipped(saved);
  check(!partial.verifyRecording(rootOnly).ok, 'a manifest missing the saved game was accepted');
  partial.destroy();

  console.log('smoke: a recording pins the saved game below the root, and a disk that differs there is refused by name');
}

// --- The headless web driver (M4-W1, #108) -------------------------------
//
// `tools/drive.mjs` is the web half of what the SDL host's `--press` /
// `--until` / `--dump` do: a directory, a program, keys at frames, a
// picture at the end, and the same report lines core formats for both
// hosts. Before it, the desktop half of every comparison in
// `docs/hosts.md` was one command and the web half was a person clicking
// through a page, which is not a thing a script can repeat.
//
// It is **spawned**, not imported and called. What is under test is the
// tool as a person invokes it — the command line, the exit code, the
// lines on stdout, the files on disk — and a function called in-process
// would check none of those four. The disk is the repository's own
// `tests/sessions/spin/`: nothing here runs a game, here or anywhere
// else, and the driver's own top comment says why it never can.

if (missing.length === 0 && sessions !== null) {
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const driver = fileURLToPath(new URL('./drive.mjs', import.meta.url));
  const disk = `${sessions}/spin`;
  const scratch = mkdtempSync(join(tmpdir(), 'amberfolio-drive-'));

  /// The driver, run the way a person runs it, with its output captured.
  /// `process.execPath` rather than a bare `node`: the runner may have
  /// been started by emsdk's own node, and this should use whichever one
  /// is running it rather than whichever one is first on PATH.
  const run = (...args) =>
    spawnSync(process.execPath, [driver, disk, 'SPIN.EXE', ...args], {
      encoding: 'utf8',
    });

  const dump = join(scratch, 'spin');
  const first = run('--frames', '4', '--press', 'A@1', '--press', 'Up@2', '--dump', dump);
  check(first.status === 0, `the driver exited ${first.status}: ${first.stderr}`);
  const said = first.stdout ?? '';

  // The identity of the file, before anything ran — the same line the
  // SDL host prints, and the reason the two can be compared at all.
  check(
    /^amberfolio: load SPIN\.EXE sha256=[0-9a-f]{64}$/m.test(said),
    `the driver printed no load line:\n${said}`,
  );
  // The stop report, formatted in core (machine/report.h). A frame
  // budget is the host's reason for ending a run and not the machine's,
  // so it reports as a tick budget: the machine is still running.
  check(
    /^amberfolio: stop reason=tick_budget steps=\d+ ticks=\d+ frames=\d+ /m.test(said),
    `the driver printed no stop report:\n${said}`,
  );
  check(
    said.includes('amberfolio: press A frame=1') &&
      said.includes('amberfolio: press Up frame=2'),
    `the driver did not say which keys it posted:\n${said}`,
  );
  // Every seam this build carries, in the SDL host's own `--seams`
  // shape. `SPIN.EXE` is no known edition, so all of them are
  // unavailable and each says why — PLAN.md §5's rule reported rather
  // than a silence.
  check(
    /^amberfolio: seams \S+ unavailable wrong_binary - /m.test(said),
    `the driver printed no seam table:\n${said}`,
  );
  check(
    /^amberfolio: audio underruns=\d+ resyncs=\d+$/m.test(said),
    `the driver printed no audio counters:\n${said}`,
  );

  // The throughput measurement, which #116 was closed without having an
  // instrument for. What is asserted is that it is a real quotient of
  // two real quantities — virtual time covered over wall time spent —
  // and not that it is any particular number: the number is a property
  // of the machine it ran on and of nothing in this repository.
  const throughput =
    /^amberfolio: throughput virtual=([\d.]+)s wall=([\d.]+)s factor=([\d.]+)x steps=(\d+) steps\/s=(\d+)$/m.exec(
      said,
    );
  check(throughput !== null, `the driver printed no throughput line:\n${said}`);
  if (throughput !== null) {
    const [, virtualSeconds, wallSeconds, factor, steps] = throughput;
    check(Number(virtualSeconds) > 0, 'the driver covered no virtual time');
    check(Number(wallSeconds) > 0, 'the driver spent no wall time');
    check(Number(steps) > 0, 'the driver took no steps');
    check(
      Math.abs(
        Number(factor) / (Number(virtualSeconds) / Number(wallSeconds)) - 1,
      ) < 0.01,
      `factor=${factor} is not virtual/wall (${virtualSeconds}/${wallSeconds})`,
    );
  }

  // The state hash: the same digest a recording's checkpoint carries
  // (docs/replay.md §2), and the one line two hosts' runs can be
  // compared on. Two runs of one script must end in the same machine, or
  // nothing above this is worth reading.
  const hashOf = (text) => /^amberfolio: state hash=([0-9a-f]{64})$/m.exec(text)?.[1];
  const hash = hashOf(said);
  check(hash !== undefined, `the driver printed no state hash:\n${said}`);
  const again = run('--frames', '4', '--press', 'A@1', '--press', 'Up@2');
  check(again.status === 0, `the second run exited ${again.status}: ${again.stderr}`);
  check(
    hashOf(again.stdout ?? '') === hash,
    'two runs of one script disagreed about the machine they ended in',
  );

  // The picture. A PPM the SDL host's `--dump` could have written, with
  // the machine's own dimensions in its header — parsed rather than
  // hashed, because what is asserted is that a frame was written at all
  // and in the format both hosts agree on.
  const ppm = readFileSync(`${dump}.ppm`);
  const ppmHeader = 'P6\n320 200\n255\n';
  check(
    ppm.subarray(0, ppmHeader.length).toString('latin1') === ppmHeader,
    `the dumped frame's header is ${JSON.stringify(ppm.subarray(0, 16).toString('latin1'))}`,
  );
  check(
    ppm.length === ppmHeader.length + 320 * 200 * 3,
    `the dumped frame is ${ppm.length} bytes, expected ${ppmHeader.length + 320 * 200 * 3}`,
  );

  // And the edge list, `--dump`'s third file (#148). `SPIN.EXE` is ten
  // bytes of `JMP $` and publishes nothing, so what is checked here is
  // the shape and the *statement*: a program that made no sound says so
  // with a count of zero, because a missing file is not an answer and a
  // silent run and a broken dump must not look alike. The tone this
  // format carries is checked against a machine that makes one, above,
  // and byte for byte against the SDL host's own file by a person
  // (docs/hosts.md 4).
  const edgeFile = readFileSync(`${dump}.edges`, 'utf8').split('\n');
  check(
    edgeFile[0] === '# amberfolio audio edges' &&
      edgeFile[1] === '# pit-input-hz 1193182' &&
      edgeFile[2] === '# tick level',
    `the dumped edge list has the wrong header: ${JSON.stringify(edgeFile.slice(0, 3))}`,
  );
  check(
    edgeFile[3] === '# edges 0 dropped 0',
    `a machine that published nothing wrote ${JSON.stringify(edgeFile[3])}`,
  );
  check(
    /^amberfolio: dump edges=\S+\.edges count=\d+ dropped=\d+$/m.test(said),
    `the driver did not report its edge list:\n${said}`,
  );

  // And the refusal. A seam whose addresses are facts about another
  // binary must not quietly not happen: a script that asked for the
  // cheats seam and got a plain machine would be the worst outcome this
  // whole apparatus has (PLAN.md §5), so the run ends and says why.
  const refused = run('--frames', '1', '--seam', 'cheat-invulnerable');
  check(refused.status === 1, `a refused seam exited ${refused.status}`);
  check(
    (refused.stdout ?? '').includes(
      'amberfolio: seam cheat-invulnerable refused (wrong_binary)',
    ),
    `a refused seam did not say why:\n${refused.stdout}`,
  );
  const unknown = run('--frames', '1', '--seam', 'no-such-seam');
  check(unknown.status === 1, `a seam that does not exist exited ${unknown.status}`);

  // A key the 83-key board never had is refused at the command line
  // rather than posted as something else, and a listing is a question
  // whose answer is the whole of what was asked for.
  const bad = run('--press', 'F11@1', '--frames', '1');
  check(bad.status === 2, `--press F11 exited ${bad.status}, expected a usage failure`);
  const listed = run('--seams');
  check(listed.status === 0, `--seams exited ${listed.status}`);
  check(
    !(listed.stdout ?? '').includes('amberfolio: stop '),
    '--seams ran the machine; a listing is a question, not a run',
  );

  // --- What is under the directory, too (M4, #146) ----------------------
  //
  // The driver used to read the top of the directory it was given and
  // nothing below it, and reported what it left behind as
  // `disk skipped SAVE (not a file)` — which is where a Gold Box save
  // slot lives, so a player could not arrive in a browser with one. It
  // walks now.
  //
  // The disk here is the repository's own `spin/` copied into a scratch
  // directory with a subdirectory added, because `tests/sessions/spin/`
  // is what `spin.rec` was recorded against and a new entry in it would
  // change that recording's initial conditions (docs/replay.md §1).
  const nested = join(scratch, 'disk');
  mkdirSync(join(nested, 'SAVE'), { recursive: true });
  copyFileSync(join(disk, 'SPIN.EXE'), join(nested, 'SPIN.EXE'));
  writeFileSync(join(nested, 'SAVE', 'SAVE1.DAT'), Buffer.from([0x53]));

  const walked = spawnSync(
    process.execPath,
    [driver, nested, 'SPIN.EXE', '--frames', '1', '--quiet'],
    { encoding: 'utf8' },
  );
  check(walked.status === 0, `the driver exited ${walked.status}: ${walked.stderr}`);
  const walkedSaid = walked.stdout ?? '';
  // Two files taken, nothing skipped: the program at the top and the one
  // in the subdirectory, whose bytes are counted in the total.
  check(
    /^amberfolio: disk files=2 skipped=0 bytes=35$/m.test(walkedSaid),
    `the driver did not walk the subdirectory:\n${walkedSaid}`,
  );
  check(
    !walkedSaid.includes('disk skipped'),
    `the driver skipped something it should have walked into:\n${walkedSaid}`,
  );

  // --- A disk the machine cannot hold, said so (M4, #158) ---------------
  //
  // The line a person reads when a directory overruns the filesystem.
  // Both refusals printed `(status 3)` until #158, so the run that found
  // this reported seven of a game's data files in the same words as a
  // PDF it was right to ignore. They are different sentences now, and a
  // disk with a hole in it says so once more, loudly, on its own line —
  // because everything after it is a run on a disk that is not the one
  // the caller named.
  //
  // The filler count is deliberately not `max_entries`: this side does
  // not restate core's bound, it hands over more files than any bound
  // this backend will plausibly have and checks what comes back.
  const toobig = join(scratch, 'toobig');
  mkdirSync(toobig, { recursive: true });
  copyFileSync(join(disk, 'SPIN.EXE'), join(toobig, 'SPIN.EXE'));
  for (let i = 0; i < 700; ++i) {
    writeFileSync(join(toobig, `F${i}.DAT`), Buffer.from([0x2a]));
  }
  // And one name DOS could never have had, so both refusals are in the
  // same output and can be told apart in it.
  writeFileSync(join(toobig, 'code wheel.pdf'), Buffer.from([0x2a]));

  // Nothing is asserted about the exit code, and that is on purpose:
  // `readdirSync` hands files back in the order the operating system
  // keeps them, so whether `SPIN.EXE` itself is one of the ones that fit
  // differs between the three platforms this runs on. What is under test
  // is the report of the disk, which is printed before anything is
  // loaded — and which is the only thing that told anybody the disk was
  // wrong.
  const overrun = spawnSync(
    process.execPath,
    [driver, toobig, 'SPIN.EXE', '--frames', '1', '--quiet'],
    { encoding: 'utf8' },
  );
  const overrunSaid = overrun.stdout ?? '';

  // Before anything about *what* it said: that all of it arrived. A pipe
  // is an asynchronous stream in node and `process.exit()` drops whatever
  // is still queued on one, so a run that prints a line per skipped file
  // — this one, six hundred-odd times — used to lose its tail, and the
  // tail is where the summary lives. It failed in Release and passed in
  // Debug, on the same code and the same disk, because the only variable
  // was how fast the process reached the exit; the driver flushes before
  // exiting now. Counting the lines against the number the report claims
  // is the assertion that would have caught it: a truncated list is a
  // count that does not add up, whichever line it stopped at.
  const claimed = /^amberfolio: disk files=\d+ skipped=(\d+) /m.exec(overrunSaid);
  check(claimed !== null, `the overrun disk printed no report:\n${overrunSaid.slice(0, 2000)}`);
  if (claimed !== null) {
    const printed = overrunSaid
      .split('\n')
      .filter((line) => line.startsWith('amberfolio: disk skipped ')).length;
    check(
      printed === Number(claimed[1]),
      `the driver reported ${claimed[1]} skipped files and printed ${printed} ` +
        'of them - its output was cut off',
    );
  }

  check(
    overrunSaid.includes('(no room left on the disk)'),
    `an overrun disk did not say what was wrong:\n${overrunSaid.slice(0, 4000)}`,
  );
  check(
    overrunSaid.includes('code wheel.pdf (not a DOS-nameable path)'),
    'a name DOS could never have had was not reported as such:\n' +
      overrunSaid.slice(0, 4000),
  );
  check(
    /^amberfolio: disk INCOMPLETE - /m.test(overrunSaid),
    `an overrun disk was not called incomplete:\n${overrunSaid.slice(0, 4000)}`,
  );
  check(
    !overrunSaid.includes('(status 3)'),
    'a refusal is still printed as a bare status number:\n' +
      overrunSaid.slice(0, 4000),
  );

  rmSync(scratch, { recursive: true, force: true });
  console.log(
    'smoke: the headless web driver ran a disk, walked below it, pressed ' +
      'keys, dumped a frame and measured itself',
  );
}

// --- The driver's own parsing (M4-W1, #108) ------------------------------
//
// Pure functions, so they need no module and no process — and they are
// where the two hosts' *procedures* meet. `docs/playable.md` writes every
// key of every leg in SDL's spelling; a leg drivable there and not here
// would be parity in the machine and none in the practice. So both
// spellings resolve, through host.mjs's one scancode table and no second
// one.
{
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const pairs = {
    KeyA: 0x1e,
    A: 0x1e,
    a: 0x1e,
    Enter: 0x1c,
    Return: 0x1c,
    ArrowUp: 0x48,
    Up: 0x48,
    Numpad5: 0x4c,
    'Keypad 5': 0x4c,
    Digit7: 0x08,
    7: 0x08,
    Escape: 0x01,
    F10: 0x44,
  };
  for (const [name, scancode] of Object.entries(pairs)) {
    check(
      keyNameToScancode(name) === scancode,
      `--press ${name} resolves to ${keyNameToScancode(name)}, expected ` +
        `0x${scancode.toString(16)}`,
    );
  }
  for (const name of ['F11', 'MetaLeft', 'Menu', '']) {
    check(
      keyNameToScancode(name) === undefined,
      `--press ${name} resolved to a scancode the 83-key board does not have`,
    );
  }

  // The command line refuses rather than guesses, and every refusal says
  // what was wrong with what it was given.
  check(parseDriveArgs([]).error !== undefined, 'the driver accepted no arguments at all');
  check(
    parseDriveArgs(['dir']).error !== undefined,
    'the driver accepted a directory with no program',
  );
  check(
    parseDriveArgs(['dir', 'P.EXE', '--press', 'A']).error !== undefined,
    'the driver accepted a --press with no frame',
  );
  check(
    parseDriveArgs(['dir', 'P.EXE', '--speed', 'pentium']).error !== undefined,
    'the driver accepted a speed preset that does not exist',
  );
  check(
    parseDriveArgs(['dir', 'P.EXE', '--dump-every', '10']).error !== undefined,
    'the driver accepted --dump-every with no --dump to share a prefix with',
  );
  check(
    parseDriveArgs(['dir', 'P.EXE', '--frames', '-3']).error !== undefined,
    'the driver accepted a negative frame budget',
  );
  check(
    parseDriveArgs(['dir', 'P.EXE', '--pull', 'cheat-kill-all']).error !== undefined,
    'the driver accepted a --pull with no frame',
  );
  const ok = parseDriveArgs([
    'dir', 'P.EXE', '--frames', '10', '--press', 'Return@4', '--seam', 'a',
    '--seam', 'b', '--pull', 'a@7', '--speed', '386', '--', 'ONE', 'TWO',
  ]);
  check(ok.error === undefined, `a good command line was refused: ${ok.error}`);
  check(
    ok.frames === 10 && ok.seams.length === 2 && ok.speed === '386',
    'the command line did not parse',
  );
  // The single leading space DOS's own command-line parsing leaves in
  // front of a tail — the same shape the SDL host hands the loader.
  check(ok.tail === ' ONE TWO', `the command tail is ${JSON.stringify(ok.tail)}`);
  check(
    ok.presses[0]?.scancode === 0x1c,
    'Return@4 did not resolve to the Enter scancode',
  );
  check(
    ok.pulls[0]?.id === 'a' && ok.pulls[0]?.frame === 7,
    `--pull a@7 parsed as ${JSON.stringify(ok.pulls[0])}`,
  );

  // The PPM encoder, on a picture small enough to write out by hand: two
  // pixels, palette entries 1 and 0, and the P6 header both hosts agree
  // on.
  const encoded = encodePpm(
    2,
    1,
    new Uint8Array([1, 0]),
    new Uint8Array([9, 8, 7, 6, 5, 4]),
  );
  check(
    encoded.toString('latin1') ===
      `P6\n2 1\n255\n${String.fromCharCode(6, 5, 4, 9, 8, 7)}`,
    `the PPM encoder wrote ${JSON.stringify(encoded.toString('latin1'))}`,
  );

  console.log("smoke: the driver reads both hosts' key spellings and refuses the rest");
}

// --- The AudioWorklet's underrun policy (M4-A1 #106, M4-W1 #108) ---------
//
// `tests/core/machine/platform_test.cpp`'s
// `AnUnderrunHoldsTheLevelAndKeepsItsPlace` pins core's rule. This is its
// counterpart one layer out, and it exists because the two hosts had
// quietly disagreed: the worklet filled silence where core holds. The
// reconciliation (audio-worklet.mjs's own top comment) is hold across the
// seam, then fade — core's rule for as long as core's reasoning holds,
// and silence once it stops holding, because a stalled tab is not a pull
// that ran a few microseconds past the horizon.
//
// The processor is a class in a global scope a browser provides
// (`AudioWorkletGlobalScope`), so the three things that scope has and
// node does not are stubbed here. Stubbing them is the whole trick: the
// file under test is imported and run unmodified, and what is faked is
// the room it runs in, not the thing being checked.
{
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const rate = 48000;
  globalThis.sampleRate = rate;
  globalThis.AudioWorkletProcessor = class {
    constructor() {
      this.port = { postMessage: (data) => posted.push(data), onmessage: null };
    }
  };
  const posted = [];
  let registeredAs = null;
  let Processor = null;
  globalThis.registerProcessor = (name, type) => {
    registeredAs = name;
    Processor = type;
  };

  await import('./audio-worklet.mjs');
  check(
    registeredAs === 'amberfolio-speaker',
    `the processor registered as ${JSON.stringify(registeredAs)}; app.mjs asks for` +
      ' "amberfolio-speaker"',
  );

  if (Processor !== null) {
    const processor = new Processor();
    const quantum = () => {
      const channel = new Float32Array(128);
      processor.process([], [[channel]]);
      return channel;
    };

    // One chunk of a level held high — 64 samples, half a quantum, so
    // the starvation begins inside the same call that plays them.
    processor.port.onmessage({ data: new Float32Array(64).fill(1) });
    const played = quantum();
    check(
      played.slice(0, 64).every((sample) => sample === 1),
      'the processor did not play the chunk it was posted',
    );
    // The seam: held at the level the last real sample was at, which is
    // exactly what `audio_timeline::render()` does when it runs out of
    // settled time. A step to zero here would be an edge in the output
    // that the machine never generated.
    check(
      played.slice(64).every((sample) => sample === 1),
      'the first samples of an underrun were not the held level',
    );
    check(
      posted.length === 1 && posted[0].underruns === 1,
      `the processor reported ${JSON.stringify(posted)}, expected one underrun`,
    );

    // And then it goes away. `holdSamples` is 3 ms and `rampSamples` 6,
    // so by 16 ms of starvation there is nothing left — a cone that is
    // not being driven should not be left deflected, however quiet a
    // constant deflection is.
    let sample = 1;
    let quanta = 0;
    while (sample !== 0 && quanta < 16) {
      const filled = quantum();
      check(
        filled.every((value) => value <= sample + 1e-6 && value >= 0),
        'the fade to silence was not monotonic',
      );
      sample = filled[filled.length - 1];
      quanta += 1;
    }
    check(sample === 0, `after ${quanta} quanta of starvation the output is ${sample}`);
    check(
      quanta * 128 <= Math.round(rate * 0.02),
      `the fade took ${quanta * 128} samples, which is more than 20 ms`,
    );

    // One run of starvation is one underrun, however many quanta it
    // lasted: the count is of events a listener would notice, not of
    // samples nobody generated.
    check(
      posted.length === 1,
      `a single stall reported ${posted.length} underruns`,
    );

    // And it recovers: a chunk arriving after a stall plays at once, and
    // the next stall counts as a second one.
    processor.port.onmessage({ data: new Float32Array(8).fill(-1) });
    const recovered = quantum();
    check(
      recovered[0] === -1,
      `after a stall the next chunk played as ${recovered[0]}, expected -1`,
    );
    check(
      posted.length === 2 && posted[1].underruns === 2,
      'the stall after a recovery was not counted as a second underrun',
    );
  }

  console.log(
    'smoke: the speaker worklet holds the level across a short gap and fades to silence',
  );

  // --- Volume and mute (M4-A1 remainder, #148) -------------------------
  //
  // The same room, a fresh processor, and the three claims
  // `hosts/sdl/tests/audio_gain_test.cpp` makes of the desktop host's
  // gain — asked of this one because the two are separate
  // implementations of one decision, exactly as the underrun policy
  // above is. Nothing is shared between them but the reasoning, so
  // nothing but a test on each side can say they agree.
  //
  //   1. unity does not touch a sample;
  //   2. a volume scales every sample by it, once the glide has landed;
  //   3. mute is silence — arithmetically zero, including the held
  //      level an underrun invents, which is the half only this file can
  //      check because only this side of the boundary invents one.
  if (Processor !== null) {
    const processor = new Processor();
    const quantum = () => {
      const channel = new Float32Array(128);
      processor.process([], [[channel]]);
      return channel;
    };
    /// Feed one quantum's worth of a constant level and answer what came
    /// out. A fresh chunk per call, so the queue never runs dry and what
    /// is measured is the gain and not the underrun policy.
    const playing = (level) => {
      processor.port.onmessage({ data: new Float32Array(128).fill(level) });
      return quantum();
    };
    /// The glide is six milliseconds; run it out and a little past, so
    /// what follows is the settled gain.
    const settle = (level) => {
      const quanta = Math.ceil((rate * 0.006) / 128) + 1;
      for (let i = 0; i < quanta; ++i) playing(level);
    };

    // 1. Unity. Not "close to": the same bits, which is what makes the
    // numbers in docs/hosts.md §4 numbers about this host too.
    const untouched = playing(0.125);
    check(
      untouched.every((sample) => sample === 0.125),
      'the worklet altered a sample with the volume where it starts',
    );

    // 2. A volume, once it has arrived, is a multiply and nothing else.
    processor.port.onmessage({ data: { gain: 0.5 } });
    settle(0.25);
    const halved = playing(0.25);
    check(
      halved.every((sample) => sample === 0.125),
      `at half volume a 0.25 sample came out as ${halved[0]}`,
    );

    // And the walk to it is a walk: the first quantum after a change is
    // between the two levels rather than at either, which is the click
    // this glide exists to remove.
    processor.port.onmessage({ data: { gain: 1 } });
    const walking = playing(0.25);
    check(
      walking[0] > 0.125 && walking[0] < 0.25 && walking[127] > walking[0],
      `a volume change stepped rather than glided: ${walking[0]} then ${walking[127]}`,
    );

    // 3. Mute. Every sample exactly zero, which is the value platform.h
    // reserves for silence — and the monotonic way down, so the mute is
    // not itself a click.
    processor.port.onmessage({ data: { gain: 0 } });
    let previous = 0.25;
    let quanta = 0;
    while (previous !== 0 && quanta < 16) {
      const fading = playing(0.25);
      for (const sample of fading) {
        if (sample > previous + 1e-6 || sample < 0) {
          check(false, `the mute was not a monotonic fade: ${sample} after ${previous}`);
          break;
        }
        previous = sample;
      }
      quanta += 1;
    }
    check(previous === 0, `after ${quanta} quanta of muting the output is ${previous}`);
    const silent = playing(0.25);
    check(
      silent.every((sample) => sample === 0),
      'a muted worklet handed the destination something other than zero',
    );

    // The half of it that only this host has: a stall while muted must
    // be silent too. `starvedSample()` invents the held level and the
    // fade out of it on this side of the boundary, so a gain applied
    // before the postMessage — in app.mjs, where the chunks are pulled —
    // would leave a muted player hearing the held sample. Here it does
    // not, because the multiply is where the sample is written.
    const stalled = quantum();
    check(
      stalled.every((sample) => sample === 0),
      'a muted worklet held a level through an underrun instead of silence',
    );

    // And it comes back. A gain of one restores the samples themselves,
    // not an approximation of them.
    processor.port.onmessage({ data: { gain: 1 } });
    settle(0.25);
    const restored = playing(0.25);
    check(
      restored.every((sample) => sample === 0.25),
      `unmuting gave back ${restored[0]} where the machine made 0.25`,
    );

    // A gain nobody should be able to ask for is clamped rather than
    // honoured: this host does not amplify, so the loudest thing a
    // player hears is the thing `render()` produced.
    processor.port.onmessage({ data: { gain: 4 } });
    const capped = playing(0.25);
    check(
      capped.every((sample) => sample === 0.25),
      `a gain of 4 was honoured: 0.25 came out as ${capped[0]}`,
    );
  }

  console.log('smoke: the speaker worklet mutes to silence and scales linearly');
}

// --- The dev page's pacing, on a synthetic clock (#157) ------------------
//
// The defect this pins was browser-only by construction and stayed
// invisible for exactly that reason: there is no rAF here and no display,
// so the page's run loop could not be driven at all. `pacedAdvance()` is
// the loop's one decision — where elapsed wall time says virtual time
// should be — factored out of the callback so a clock made of numbers can
// ask it.
//
// The old loop advanced one 60 Hz frame per rAF callback, and rAF fires
// at the *display's* refresh rate, so the machine ran at `refresh / 60`
// times real time: 4x on the 240 Hz monitor #157 was found on. The
// assertion that says it is fixed is the equivalence below — a hundred
// callbacks at 240 Hz and twenty-five at 60 Hz cover the same span of
// wall time, so they must advance the same virtual time.
{
  const check = (condition, message) => {
    if (!condition) problems.push(message);
  };

  const ticksPerSecond = 1193182;
  const oneFrame = ticksPerSecond / 60;

  // The machine's own number and not a copy of it that could drift: the
  // synthetic clock below is only meaningful in the units the PIT counts
  // in (abi.h's `af_ticks_per_second`).
  if (missing.length === 0) {
    check(
      module._af_ticks_per_second() === ticksPerSecond,
      `af_ticks_per_second() is ${module._af_ticks_per_second()}, not the ` +
        `${ticksPerSecond} this check paces against`,
    );
  }

  /// `count` callbacks at `hz`, driven exactly as the rAF callback drives
  /// it: the timestamp of one callback becomes the `since` of the next,
  /// and the answer's tick becomes the next call's `tick`.
  ///
  /// The clock is anchored by a callback at t=0 that is not one of the
  /// `count` — the real loop's first callback has nothing to measure
  /// against either — so `count` callbacks at `hz` span exactly
  /// `count / hz` seconds of wall time and the two rates below are
  /// comparing the same span.
  const runAt = (hz, count, options = {}) => {
    let tick = 0;
    let clamped = 0;
    pacedAdvance({ tick, since: null, now: 0, ticksPerSecond, ...options });
    let since = 0;
    for (let i = 0; i < count; ++i) {
      const now = ((i + 1) * 1000) / hz;
      const paced = pacedAdvance({ tick, since, now, ticksPerSecond, ...options });
      tick = paced.tick;
      since = now;
      if (paced.clamped) clamped += 1;
    }
    return { tick, clamped };
  };

  // The assertion #157 asks for. Five twelfths of a second of wall time,
  // chopped four times as finely on one display as on the other: a
  // hundred callbacks at 240 Hz and twenty-five at 60 Hz must advance the
  // same virtual time, because they cover the same wall time. Under the
  // old loop the first advanced four times the second.
  const fast = runAt(240, 100);
  const slow = runAt(60, 25);
  check(
    Math.abs(fast.tick - slow.tick) < oneFrame,
    `100 callbacks at 240 Hz advanced ${fast.tick} ticks and 25 at 60 Hz ` +
      `advanced ${slow.tick}: the display's refresh rate is still deciding ` +
      'virtual time (#157)',
  );
  check(
    Math.abs(fast.tick - ticksPerSecond * (100 / 240)) < 1,
    `100 callbacks at 240 Hz advanced ${fast.tick} ticks, not the ` +
      `${ticksPerSecond * (100 / 240)} of the wall time they spanned`,
  );
  check(
    fast.clamped === 0 && slow.clamped === 0,
    'an ordinary run hit the catch-up clamp',
  );

  // And a second of it, at every rate a person's display or a browser's
  // battery throttling might hand this loop — 30 Hz being what rAF is
  // slowed to on power saving, and the reason the clamp is not one frame.
  for (const hz of [240, 144, 120, 60, 30]) {
    const run = runAt(hz, hz);
    check(
      Math.abs(run.tick - ticksPerSecond) < 1,
      `a second of callbacks at ${hz} Hz advanced ${run.tick} ticks, not ` +
        `${ticksPerSecond}`,
    );
    check(run.clamped === 0, `${hz} Hz callbacks hit the catch-up clamp`);
  }

  // The first callback has nothing to measure against and advances
  // nothing rather than guessing; so does a clock that went backwards.
  const first = pacedAdvance({ tick: 500, since: null, now: 16.7, ticksPerSecond });
  check(
    first.tick === 500 && first.advance === 0 && !first.clamped,
    `the first callback advanced ${first.advance} ticks with nothing to measure`,
  );
  const backwards = pacedAdvance({ tick: 500, since: 100, now: 90, ticksPerSecond });
  check(
    backwards.tick === 500 && backwards.advance === 0,
    'a timestamp that went backwards advanced virtual time',
  );

  // The clamp: a backgrounded tab, a breakpoint, a laptop that slept.
  // The whole point is that the answer is the clamp and not the delta —
  // running the machine forward by a minute in one callback is the burst
  // of emulated instructions hosts/sdl/src/main.cpp's loop is written to
  // forbid.
  const stalled = pacedAdvance({ tick: 0, since: 0, now: 60000, ticksPerSecond });
  check(
    stalled.clamped && stalled.advance === MAX_CATCH_UP_SECONDS * ticksPerSecond,
    `a 60-second delta advanced ${stalled.advance / ticksPerSecond}s of virtual ` +
      `time; the clamp is ${MAX_CATCH_UP_SECONDS}s`,
  );

  // And the excess is dropped, not banked. The callback after a stall
  // advances its own delta and no more — virtual time stays behind the
  // wall, which is what the desktop host does when it declines to sleep.
  const after = pacedAdvance({
    tick: stalled.tick,
    since: 60000,
    now: 60000 + 1000 / 60,
    ticksPerSecond,
  });
  check(
    !after.clamped && Math.abs(after.advance - oneFrame) < 1,
    `the callback after a stall advanced ${after.advance} ticks instead of ` +
      `one frame's ${oneFrame}: the lost time was banked and paid back in a burst`,
  );

  // The clamp is a parameter with a default, and a caller that hands it
  // nonsense gets the default rather than a machine that never advances.
  const explicit = pacedAdvance({
    tick: 0, since: 0, now: 10000, ticksPerSecond, maxCatchUpSeconds: 0.5,
  });
  check(
    explicit.advance === 0.5 * ticksPerSecond,
    `an explicit half-second clamp advanced ${explicit.advance} ticks`,
  );
  for (const bad of [0, -1, Number.NaN, undefined]) {
    const answer = pacedAdvance({
      tick: 0, since: 0, now: 10000, ticksPerSecond, maxCatchUpSeconds: bad,
    });
    check(
      answer.advance === MAX_CATCH_UP_SECONDS * ticksPerSecond,
      `a clamp of ${bad} advanced ${answer.advance} ticks instead of the default`,
    );
  }

  console.log(
    'smoke: the page paces virtual time against the wall, not against the display',
  );
}

if (problems.length > 0) {
  for (const problem of problems) console.error(`smoke: FAIL: ${problem}`);
  process.exit(1);
}

console.log('smoke: OK');
