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
// The module runs a machine now, and M2-H2 (#55) is the page that does:
// host.mjs drives create -> af_machine_attach_reference_devices() ->
// af_web_demo_program_bytes() -> run loop, over canvas, an AudioWorklet
// and the keyboard. tests/smoke.mjs drives the identical sequence
// headlessly.
//
// <cstdio> rather than <iostream>/<format>: this is the one target where
// the standard library we pull in becomes bytes the player downloads
// (PLAN.md §4 — keeping the wasm bundle lean is why there is a hand-written
// JS host at all). Emscripten routes stdout to the console.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "amberfolio/abi.h"
#include "demo_program.h"

namespace {

/// Assembled once, on first use, and never freed — the module's whole
/// life is one page load, so there is nothing to reclaim it for. A
/// function-local static rather than a namespace-scope one: construction
/// order between translation units is otherwise unspecified, and this
/// way the vector exists exactly when something first asks for it and
/// not a moment before.
const std::vector<std::uint8_t>& demo_program() {
  static const std::vector<std::uint8_t> program =
      amberfolio::web::demo_program_bytes();
  return program;
}

}  // namespace

extern "C" {

// Two web-host-specific exports, alongside the core ABI's — see this
// file's own top comment and hosts/web/CMakeLists.txt's export list,
// which is the only place that decides whether a browser can actually
// reach these. Not part of core/include/amberfolio/abi.h: they hand out
// this *host's* embedded program, not anything the core itself knows
// about, so they live beside the code that assembled it.

/// A pointer into the module's own linear memory, stable for the
/// module's life — the same "core-owned, read through a typed-array
/// view" shape abi.h's framebuffer/palette pointers already have.
const uint8_t* af_web_demo_program_bytes(void) { return demo_program().data(); }

uint32_t af_web_demo_program_size(void) {
  return static_cast<uint32_t>(demo_program().size());
}

}  // extern "C"

int main() {
  const uint32_t v = af_version();

  std::printf("amberfolio %u.%u.%u\n", AF_VERSION_MAJOR(v), AF_VERSION_MINOR(v),
              AF_VERSION_PATCH(v));
  // ASCII only: this goes to a browser console and to CI logs.
  std::printf("  wasm module - the machine ABI is here, the page is not.\n");
  std::printf("  demo program embedded: %u bytes.\n",
              af_web_demo_program_size());

  return 0;
}
