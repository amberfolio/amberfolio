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
// Four checks now, not one (M2-F4 #45, M2-H2 #55):
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
//      audio, input, clock and console all cross into wasm, and
//      load_program is still the reserved stub it says it is.
//   4. **The actual thing the page runs.** Create a machine, attach the
//      reference device set, load the embedded demo program
//      (hosts/web/src/demo_program.cpp) through host.mjs's own
//      `Machine`/`loadDemoProgram` — the same code hosts/web/page/app.mjs
//      calls — run it, post a key, and assert a framebuffer hash, the
//      console bytes the key echo produced, and that the key was
//      observed. This is the wasm quarter of the M2 exit criterion
//      (PLAN.md §7): the machine runs correctly on this target, not
//      merely that it compiles for it.

import {
  loadAmberfolio,
  formatVersion,
  Machine,
  loadDemoProgram,
  decodeConsoleBytes,
  AF_OK,
  AF_KEY_DOWN,
  AF_KEY_UP,
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
  '_af_machine_stopped',
  '_af_machine_stop_reason',
  '_af_machine_set_speed',
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
  '_af_machine_load_program',
  '_af_machine_write_memory',
  '_af_machine_read_memory',
  '_af_machine_set_entry',
  // Web-host-specific (hosts/web/src/main.cpp), not part of
  // core/include/amberfolio/abi.h: the M2-H2 (#55) embedded demo
  // program. Listed here for the same reason as everything above it —
  // an export hosts/web/CMakeLists.txt forgets is silently absent.
  '_af_web_demo_program_bytes',
  '_af_web_demo_program_size',
];

// The status codes from abi.h. AF_OK is imported from host.mjs, which
// already has to restate it (a JS host has no headers); AF_UNIMPLEMENTED
// is not something host.mjs has any use for, so it stays local.
const AF_UNIMPLEMENTED = 4;

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
  const expectIndex = argv.indexOf('--expect');
  if (expectIndex === -1) return { expected: null };

  const expected = argv[expectIndex + 1];
  if (!expected) {
    console.error('smoke: --expect needs a version argument');
    process.exit(2);
  }
  return { expected };
}

const { expected } = parseArgs(process.argv.slice(2));

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

    check(
      module._af_machine_load_program(box, 1, 1) === AF_UNIMPLEMENTED,
      'af_machine_load_program() should answer AF_UNIMPLEMENTED until #51',
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

if (problems.length > 0) {
  for (const problem of problems) console.error(`smoke: FAIL: ${problem}`);
  process.exit(1);
}

console.log('smoke: OK');
