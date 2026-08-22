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
// Five checks now (M2-F4 #45, M2-H2 #55, M3-F2 #84):
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

import { readFileSync } from 'node:fs';

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
  AF_KEY_DOWN,
  AF_KEY_UP,
  AF_RUN_END_STOPPED,
  AF_SEAM_OFF,
  AF_SEAM_ON,
  AF_SEAM_UNAVAILABLE,
} from './host.mjs';

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
  '_af_machine_post_key',
  '_af_machine_set_wall_clock',
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
  '_af_machine_seam_armed',
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
      'the probe seam could not be registered',
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
    check(wheel !== undefined, 'the code-wheel seam is not listed');
    check(
      wheel?.state === AF_SEAM_UNAVAILABLE && wheel?.reason === 'wrong_binary',
      `the code-wheel seam should be unavailable for a test program: ${JSON.stringify(wheel)}`,
    );

    if (seamOn) {
      check(machine.seamEnable('probe') === AF_OK, 'enabling the probe seam was refused');
      const on = machine.seamList().find((s) => s.id === 'probe');
      check(on?.state === AF_SEAM_ON && on?.armed === true, `the probe seam did not arm: ${JSON.stringify(on)}`);
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
    // (tests/programs/machine_harness.h) — physical 0x600 + 0x800.
    const scratch = module._malloc(4);
    check(module._af_machine_read_memory(machine.handle, 0x600 + 0x800, scratch, 4) === AF_OK, 'reading the result block failed');
    const words = [
      module.HEAPU8[scratch] | (module.HEAPU8[scratch + 1] << 8),
      module.HEAPU8[scratch + 2] | (module.HEAPU8[scratch + 3] << 8),
    ];
    module._free(scratch);
    machine.destroy();
    return words;
  };

  const off = runProbe(false);
  check(off[0] === 0x1111 && off[1] === 0x0000, `seam off: result block is ${JSON.stringify(off.map((w) => w.toString(16)))}, expected 1111, 0`);
  const on = runProbe(true);
  check(on[0] === 0x2222 && on[1] === 0x256b, `seam on: result block is ${JSON.stringify(on.map((w) => w.toString(16)))}, expected 2222, 256b`);

  console.log('smoke: the probe seam listed, toggled, edited a register and posted a key');
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

if (problems.length > 0) {
  for (const problem of problems) console.error(`smoke: FAIL: ${problem}`);
  process.exit(1);
}

console.log('smoke: OK');
