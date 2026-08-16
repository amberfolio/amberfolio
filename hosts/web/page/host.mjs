// SPDX-License-Identifier: AGPL-3.0-only
//
// The hand-written JS host — deliberately not SDL-through-Emscripten
// (PLAN.md §4). In M0 it does one thing: instantiate the wasm module and
// read the core version back out through the C ABI. Canvas, WebAudio,
// input and the IndexedDB-backed VFS arrive in M2.
//
// It is loaded both by index.html in a browser and by smoke.mjs under
// node, so it must not touch the DOM. Emscripten's glue is generated
// beside this file at build time, hence the relative import.

import createModule from './amberfolio.mjs';

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
/// writes — a stub of the log path the machine will need for real.
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
