# SPDX-License-Identifier: AGPL-3.0-only
#
# SDL3 acquisition. Defines the SDL3::SDL3 target, either from an installed
# SDL3 or by fetching and building one at configure time.
#
# SDL3 is zlib-licensed, which is compatible with our AGPL-3.0-only outbound
# license in both the static and shared cases. Nothing from it is committed
# to this repository — that is the point of fetching rather than vendoring.

include_guard(GLOBAL)
include(FetchContent)

option(AMBERFOLIO_USE_SYSTEM_SDL3
  "Use an installed SDL3 instead of fetching and building one" OFF)
option(AMBERFOLIO_SDL3_SHARED
  "Link SDL3 dynamically instead of statically (fetched SDL3 only)" OFF)

# Pinned, not floating: a build of a given commit of this repository should
# fetch the same SDL every time. Upstream commit for release-3.4.14:
# 147a8ee32dbf9ac02f3794964490687b6bbda1bc
set(AMBERFOLIO_SDL3_TAG "release-3.4.14" CACHE STRING
  "SDL3 git tag to fetch when not using a system SDL3")
# The floor a *system* SDL3 has to clear. Deliberately lower than the pin
# above: the host uses nothing newer than the first stable SDL3, and holding
# distro packages to the pin would make AMBERFOLIO_USE_SYSTEM_SDL3 useless.
# Raise it the day the code needs something newer.
set(AMBERFOLIO_SDL3_MIN_VERSION "3.2.0")

if(AMBERFOLIO_USE_SYSTEM_SDL3)
  find_package(SDL3 ${AMBERFOLIO_SDL3_MIN_VERSION} REQUIRED CONFIG)
  message(STATUS "SDL3: system ${SDL3_VERSION}")
  return()
endif()

# SDL reads these as cache entries, and it resets CMake policies to its own
# (older) minimum when it is added as a subdirectory — so the normal-variable
# override that CMP0077/CMP0126 would give us is not reliable here. Forcing
# the cache is; our own options above are the knobs users are meant to turn.
if(AMBERFOLIO_SDL3_SHARED)
  set(_amberfolio_sdl_linkage "shared")
  set(SDL_SHARED ON  CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
  set(_amberfolio_sdl_linkage "static")
  # One self-contained binary: no DLL to ship beside it, no rpath to get
  # wrong. SDL still loads the platform backends (X11, Wayland, ...) at
  # runtime, so a static SDL is not a static desktop.
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS        OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES     OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL      OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        "${AMBERFOLIO_SDL3_TAG}"
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  # SYSTEM: SDL's headers must not be held to our warning baseline.
  SYSTEM)

message(STATUS
  "SDL3: fetching ${AMBERFOLIO_SDL3_TAG} (${_amberfolio_sdl_linkage})")
FetchContent_MakeAvailable(SDL3)
unset(_amberfolio_sdl_linkage)
