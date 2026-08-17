// SPDX-License-Identifier: AGPL-3.0-only
//
// M0 stub for the SDL3 desktop host. It opens no window and runs no machine:
// it initialises SDL with no subsystems, reports what it was built against
// and what it linked to, and quits. Asking for no subsystems is deliberate —
// it keeps this runnable as a CI smoke test on a box with no display and no
// audio device.

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>

#include "amberfolio/version.h"

// <cstdio> rather than std::format/std::print, and not only for the wasm
// host's reason (bundle size). libc++ gates std::format's floating-point
// path behind macOS 13.3 availability, so *any* std::format call fails to
// compile against a deployment target of 11.0 — which is what the macos
// preset asks for, and which is worth more here than nicer formatting.
// Revisit if that floor ever rises.

int main() {
  if (!SDL_Init(0)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  const amberfolio::version core = amberfolio::linked_version();
  const int sdl = SDL_GetVersion();

  std::printf("amberfolio %d.%d.%d\n", core.major, core.minor, core.patch);
  std::printf("  SDL %d.%d.%d (built against %d.%d.%d)\n",
              SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
              SDL_VERSIONNUM_MICRO(sdl), SDL_MAJOR_VERSION, SDL_MINOR_VERSION,
              SDL_MICRO_VERSION);
  // ASCII only: this goes to consoles and CI logs whose code page we do
  // not control.
  std::printf("  no machine yet - this is the M0 skeleton.\n");

  SDL_Quit();
  return EXIT_SUCCESS;
}
