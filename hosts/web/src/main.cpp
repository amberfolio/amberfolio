// SPDX-License-Identifier: AGPL-3.0-only
//
// The WebAssembly module's entry point. It runs on module instantiation,
// reports the core version through the C ABI, and returns.
//
// Calling af_version() here is not only for the log line: it is what pulls
// the ABI object out of the core's static archive and into the link, which
// is what makes -sEXPORTED_FUNCTIONS able to export it — and since M2-F4
// (#45) that object carries the whole platform interface, so this one call
// is what makes every af_machine_* export reachable.
//
// The module can now run a machine; there is still no *page* that does
// (canvas, WebAudio, keyboard), and that is M2-H2 (#55). The headless
// check in tests/smoke.mjs already drives the ABI end to end.
//
// <cstdio> rather than <iostream>/<format>: this is the one target where
// the standard library we pull in becomes bytes the player downloads
// (PLAN.md §4 — keeping the wasm bundle lean is why there is a hand-written
// JS host at all). Emscripten routes stdout to the console.

#include <cstdio>

#include "amberfolio/abi.h"

int main() {
  const uint32_t v = af_version();

  std::printf("amberfolio %u.%u.%u\n", AF_VERSION_MAJOR(v), AF_VERSION_MINOR(v),
              AF_VERSION_PATCH(v));
  // ASCII only: this goes to a browser console and to CI logs.
  std::printf("  wasm module - the machine ABI is here, the page is not.\n");

  return 0;
}
