// SPDX-License-Identifier: AGPL-3.0-only
//
// M0 stub for the SDL3 desktop host. It opens no window and runs no machine:
// it initialises SDL with no subsystems, reports what it was built against
// and what it linked to, and quits. Asking for no subsystems is deliberate —
// it keeps this runnable as a CI smoke test on a box with no display and no
// audio device.

#include <SDL3/SDL.h>

#include <cstdlib>
#include <format>
#include <iostream>

#include "amberfolio/version.h"

// std::print is the C++23 spelling of what follows, but <print> is missing
// from every standard library old enough to need the C++20 language fallback
// the build still allows; <format> is not. Collapse this into std::println
// the day that fallback goes.

int main() {
  if (!SDL_Init(0)) {
    std::cerr << std::format("SDL_Init failed: {}\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  const auto core = amberfolio::linked_version();
  const int sdl = SDL_GetVersion();

  std::cout << std::format("amberfolio {}.{}.{}\n",
                           core.major, core.minor, core.patch)
            << std::format("  SDL {}.{}.{} (built against {}.{}.{})\n",
                           SDL_VERSIONNUM_MAJOR(sdl), SDL_VERSIONNUM_MINOR(sdl),
                           SDL_VERSIONNUM_MICRO(sdl), SDL_MAJOR_VERSION,
                           SDL_MINOR_VERSION, SDL_MICRO_VERSION)
            // ASCII only: this goes to consoles and CI logs whose code page
            // we do not control.
            << "  no machine yet - this is the M0 skeleton.\n";

  SDL_Quit();
  return EXIT_SUCCESS;
}
