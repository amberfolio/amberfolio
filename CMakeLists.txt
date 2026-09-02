# SPDX-License-Identifier: AGPL-3.0-only
#
# Amber Folio — top-level build.
#
# Layout (PLAN.md §4): core/ is the freestanding emulator library, hosts/*
# are the platform shells, cmake/ holds the shared build logic. Nothing
# third-party is committed here; dependencies are fetched at configure time.

cmake_minimum_required(VERSION 3.25)

# Must precede project(): that is when the generator turns this into a cache
# entry. Set afterwards it would be shadowed by the empty entry CMake has
# already created. Useful to clang-tidy (issue #5) and to editors; generators
# that do not support it ignore it.
if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# The version is the milestone (PLAN.md §7, CONTRIBUTING.md's "Releases
# and tags"): it is bumped in a closeout pull request and tagged on the
# merge commit, so no commit on `main` ever reports a version that was
# never tagged. `af_version` carries it across the C ABI and the wasm
# smoke test asserts the module reports what CMake built.
project(amberfolio
  VERSION 0.3.0
  DESCRIPTION "A purpose-built low-level emulator for the Gold Box machine"
  HOMEPAGE_URL "https://github.com/amberfolio/amberfolio"
  LANGUAGES CXX)

# The web host serves the program over a network, and AGPL-3.0-only §13
# answers that with an obligation: its users must be offered the
# Corresponding Source. The page carries that offer as a link, and a link
# to "the repository" does not discharge it — the offer has to name the
# version being served, so whatever builds the page has to say where its
# source is. CI passes a URL pinned to the commit it deployed
# (.github/workflows/ci.yml); a local build points at the repository.
#
# Declared here rather than in hosts/web so every preset accepts the
# flag and CI can pass it from one shared configure step; only the web
# host reads it.
set(AMBERFOLIO_SOURCE_URL "${PROJECT_HOMEPAGE_URL}" CACHE STRING
  "URL of the source this build was made from (AGPL-3.0-only §13 offer)")

# SDL3 is a C project and enables C (and Objective-C on Apple) itself when
# it is added as a subdirectory; we deliberately do not enable them here.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Native targets are 64-bit only (PLAN.md §1). wasm32 is the one 4-byte
# pointer target we do want, so Emscripten is exempt.
if(NOT EMSCRIPTEN AND CMAKE_SIZEOF_VOID_P LESS 8)
  message(FATAL_ERROR
    "Amber Folio native targets are 64-bit only; this toolchain produces "
    "${CMAKE_SIZEOF_VOID_P}-byte pointers.")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

option(AMBERFOLIO_WERROR "Treat compiler warnings as errors" ON)

# The desktop host's OCR engine, if this build is to carry one at all
# (M5-E3c, #216). Declared here so `cmake -L` shows it beside the other
# build-shaping options; `cmake/AmberfolioTesseract.cmake` is what it does
# and why it is off.

# One host per target family, and they do not overlap: SDL3 on the desktop,
# the hand-written JS host on wasm (PLAN.md §4 — deliberately not
# SDL-through-Emscripten).
if(EMSCRIPTEN)
  set(_amberfolio_sdl_host_default OFF)
  set(_amberfolio_web_host_default ON)
else()
  set(_amberfolio_sdl_host_default ON)
  set(_amberfolio_web_host_default OFF)
endif()
set(AMBERFOLIO_BUILD_HOST_SDL ${_amberfolio_sdl_host_default}
  CACHE BOOL "Build the SDL3 desktop host")
set(AMBERFOLIO_BUILD_HOST_WEB ${_amberfolio_web_host_default}
  CACHE BOOL "Build the WebAssembly module and its JS host")
unset(_amberfolio_sdl_host_default)
unset(_amberfolio_web_host_default)

if(AMBERFOLIO_BUILD_HOST_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR
    "AMBERFOLIO_BUILD_HOST_WEB needs the Emscripten toolchain; configure "
    "with the 'wasm' preset (see README.md).")
endif()

# The GoogleTest rig runs on the native targets; the conformance suite
# runs on native CI (PLAN.md §6). What the wasm target gets is its build,
# the module smoke check, and — since it needs no framework — the
# self-written programs in tests/programs, which is the one thing that
# runs the interpreter itself there rather than merely compiling it.
if(EMSCRIPTEN)
  set(_amberfolio_tests_default OFF)
else()
  set(_amberfolio_tests_default ON)
endif()
set(AMBERFOLIO_BUILD_TESTS ${_amberfolio_tests_default}
  CACHE BOOL "Build the unit tests (fetches GoogleTest)")
unset(_amberfolio_tests_default)

# The CPU conformance suite (issue #14) rides on the same rig but costs
# two more fetched dependencies at configure time and, on a full run, a
# 200 MB vector cache. On by default because it is M1's exit criterion;
# off is for someone who wants only the unit tests.
set(AMBERFOLIO_BUILD_CONFORMANCE ${AMBERFOLIO_BUILD_TESTS}
  CACHE BOOL "Build the CPU conformance suite (fetches simdjson, libdeflate)")

if(AMBERFOLIO_BUILD_CONFORMANCE AND NOT AMBERFOLIO_BUILD_TESTS)
  message(FATAL_ERROR
    "AMBERFOLIO_BUILD_CONFORMANCE needs AMBERFOLIO_BUILD_TESTS: the "
    "conformance suite is part of the test rig.")
endif()

if(AMBERFOLIO_BUILD_TESTS AND EMSCRIPTEN)
  message(FATAL_ERROR
    "AMBERFOLIO_BUILD_TESTS is not wired up for the Emscripten toolchain: "
    "the test binaries would need node to run them and CTest does not know "
    "that. The wasm target's check is the module smoke test.")
endif()

include(AmberfolioCxxStandard)
include(AmberfolioWarnings)
# Before the first add_subdirectory(): the flags it sets are inherited by
# targets created after it, and instrumenting the third-party code too is
# the point (see the module).
include(AmberfolioSanitizers)

if(AMBERFOLIO_SANITIZE STREQUAL "")
  set(_amberfolio_sanitize_summary "none")
else()
  set(_amberfolio_sanitize_summary "${AMBERFOLIO_SANITIZE}")
endif()

enable_testing()

add_subdirectory(core)

# Before the hosts, and not only alphabetically: tests/programs (inside
# this) builds amberfolio::programs on every target including wasm — it
# fetches nothing and links nothing but the core (tests/programs/
# CMakeLists.txt says why) — and hosts/web links against it to assemble
# the M2-H2 (#55) dev-page demo program at build time. A target has to
# exist before something can `target_link_libraries()` it. The guard for
# the GoogleTest rig underneath this is inside tests/CMakeLists.txt, not
# here, so AMBERFOLIO_BUILD_TESTS is unaffected by the reorder.
add_subdirectory(tests)

# What the two hosts share (M5-D1, #169): the `seam_host_services` both
# of them attach. Before either, and only when there is a host to want
# it — a core-only or a test-only build has no use for a host object.
if(AMBERFOLIO_BUILD_HOST_SDL OR AMBERFOLIO_BUILD_HOST_WEB)
  add_subdirectory(hosts/common)
endif()

if(AMBERFOLIO_BUILD_HOST_SDL)
  add_subdirectory(hosts/sdl)
endif()

if(AMBERFOLIO_BUILD_HOST_WEB)
  add_subdirectory(hosts/web)
endif()

message(STATUS "amberfolio ${PROJECT_VERSION}: "
  "C++${AMBERFOLIO_CXX_STANDARD_EFFECTIVE}, "
  "warnings-as-errors=${AMBERFOLIO_WERROR}, "
  "sdl-host=${AMBERFOLIO_BUILD_HOST_SDL}, "
  "web-host=${AMBERFOLIO_BUILD_HOST_WEB}, "
  "tests=${AMBERFOLIO_BUILD_TESTS}, "
  "conformance=${AMBERFOLIO_BUILD_CONFORMANCE}, "
  "sanitizers=${_amberfolio_sanitize_summary}")
unset(_amberfolio_sanitize_summary)
