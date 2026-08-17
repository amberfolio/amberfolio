// SPDX-License-Identifier: AGPL-3.0-only
//
// M0 stub for the WebAssembly module. It runs on module instantiation,
// reports the core version through the C ABI, and returns — the same
// "prove the toolchain, nothing more" job the SDL host's stub does.
//
// Calling af_version() here is not only for the log line: it is what pulls
// the ABI object out of the core's static archive and into the link, which
// is what makes -sEXPORTED_FUNCTIONS able to export it.
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
  std::printf("  wasm module - no machine yet, this is the M0 skeleton.\n");

  return 0;
}
