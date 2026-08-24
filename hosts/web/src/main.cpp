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
// M3-F2 (#84) gave the page a second thing to run: a directory of the
// player's own, put into the machine's filesystem one file at a time
// (af_machine_vfs_put) and booted from there (af_machine_load_from_vfs).
// The embedded program below did not change and is not going anywhere —
// it is what proves the boundary works without anybody having a game.
//
// M4-F4 (#98) adds the seam probe: the self-written program tests/programs
// runs with its own seam on and off, staged here so the node smoke check
// can toggle a seam through the ABI and assert the difference — the same
// program, the same seam, the same two results the native suite asserts.
// The seam is registered into the machine's engine through this host's
// own export rather than being part of core's table: it is a test seam
// keyed to a test program, and a player's listing has no business
// carrying it.
//
// <cstdio> rather than <iostream>/<format>: this is the one target where
// the standard library we pull in becomes bytes the player downloads
// (PLAN.md §4 — keeping the wasm bundle lean is why there is a hand-written
// JS host at all). Emscripten routes stdout to the console.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "amberfolio/abi.h"
#include "amberfolio/abi_bridge.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "demo_program.h"
#include "programs/machine_programs.h"

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

// Web-host-specific exports, alongside the core ABI's — see this file's
// own top comment and hosts/web/CMakeLists.txt's export list, which is the
// only place that decides whether a browser can actually reach these. Not
// part of core/include/amberfolio/abi.h: they hand out this *host's*
// embedded programs and register this host's test seam, not anything the
// core itself knows about, so they live beside the code that assembled
// them.

/// A pointer into the module's own linear memory, stable for the
/// module's life — the same "core-owned, read through a typed-array
/// view" shape abi.h's framebuffer/palette pointers already have.
const uint8_t* af_web_demo_program_bytes(void) { return demo_program().data(); }

uint32_t af_web_demo_program_size(void) {
  return static_cast<uint32_t>(demo_program().size());
}

/// The seam probe program (tests/programs/machine_programs.cpp), as the MZ
/// file a page puts on the filesystem and loads — same shape as the demo.
const uint8_t* af_web_probe_program_bytes(void) {
  return amberfolio::programs::seam_probe_file().data();
}

uint32_t af_web_probe_program_size(void) {
  return static_cast<uint32_t>(amberfolio::programs::seam_probe_file().size());
}

/// Register the probe's three seams with `box`'s engine, so
/// `af_machine_seam_*` can list and toggle them. AF_NO_MACHINE for a null
/// handle; AF_INVALID if the registry would not take one (already
/// registered, or full).
///
/// Two, since #147: `probe`, whose points the program runs through, and
/// `probe-unreached`, whose one point sits on an instruction it never
/// executes. Both are keyed to the same file, so both are equally
/// available and both arm — and the only thing that tells them apart is
/// `af_machine_seam_fired`, which is what a browser needed and did not
/// have. Registering them together rather than through two exports keeps
/// the pair inseparable: a smoke check cannot end up asserting the happy
/// one and quietly skipping the other.
///
/// Three, since #161: `probe-trigger` shares `probe`'s point and is
/// **pulled** rather than left on, so a browser can be asked the one
/// question the ABI could not answer before — does a trigger nobody
/// pulled leave the machine alone, and does one somebody pulled act
/// exactly once.
///
/// Four, since #163: `probe-pull` is a trigger whose point has **no
/// address**, so it is offered at every step boundary while the pull is
/// outstanding and decides for itself when acting is safe. A browser
/// gets to ask that one too, because "one bool per step when nobody
/// pulled" is a claim about a hot path and hot paths differ per target.
uint32_t af_web_probe_seam_register(af_machine* box) {
  amberfolio::machine::machine* pc = amberfolio::af_machine_unwrap(box);
  if (pc == nullptr) {
    return AF_NO_MACHINE;
  }
  const bool ok =
      pc->seams().add(amberfolio::programs::seam_probe_definition()) &&
      pc->seams().add(
          amberfolio::programs::seam_probe_unreached_definition()) &&
      pc->seams().add(amberfolio::programs::seam_probe_trigger_definition()) &&
      pc->seams().add(amberfolio::programs::seam_probe_pull_definition());
  return ok ? AF_OK : AF_INVALID;
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
