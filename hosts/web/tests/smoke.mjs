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
// Three checks now, not one (M2-F4, #45):
//
//   1. The version the module reports is the version CMake built.
//   2. **Every name in the export list is actually exported.** This is
//      the guard for the ABI's one quiet failure mode: a function added
//      to core/include/amberfolio/abi.h and not added to
//      -sEXPORTED_FUNCTIONS in hosts/web/CMakeLists.txt is simply missing
//      from the module, and nothing in the build says so. The list below
//      is the third reader of that guest list, and the point is that it
//      is a separate one — two lists that must agree, checked.
//   3. The machine runs. Create, place a self-written program, set the
//      entry point, run a frame of virtual time, and pull on all five
//      parts of the platform interface. It is a boundary check, not a
//      machine check — the machine's own tests are in tests/core — but it
//      is the one that proves frame, audio, input, clock and console all
//      cross into wasm, which is what M2-H2 (#55) then builds a page on.

import { loadAmberfolio, formatVersion } from './host.mjs';

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
];

// The status codes from abi.h. Restated rather than imported, for the
// same reason as the export list: a JS host has no headers, so the
// numbers crossing the boundary are worth asserting from the other side.
const AF_OK = 0;
const AF_UNIMPLEMENTED = 4;

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
      'a machine with no speaker attached produced sound',
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

    // Console: no DOS layer yet (M2-D7, #52), so there is nothing to
    // drain — which is the honest answer and the one to assert.
    check(
      module._af_machine_console_pending(box) === 0,
      'something wrote to the console before there was a DOS layer',
    );

    module._af_machine_destroy(box);
    const again = module._af_machine_create();
    check(again !== 0, 'the machine could not be created again after destroy');
    module._af_machine_destroy(again);
  }

  console.log('smoke: the machine ran a frame and every interface answered');
}

if (problems.length > 0) {
  for (const problem of problems) console.error(`smoke: FAIL: ${problem}`);
  process.exit(1);
}

console.log('smoke: OK');
