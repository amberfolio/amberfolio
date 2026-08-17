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

project(amberfolio
  VERSION 0.0.1
  DESCRIPTION "A purpose-built low-level emulator for the Gold Box machine"
  HOMEPAGE_URL "https://github.com/BussardRamjet/amberfolio"
  LANGUAGES CXX)

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

# The unit-test rig runs on the native targets. Wasm-side test execution is
# out of scope (PLAN.md §6: the conformance suite runs on native CI); what
# covers the wasm target is its build plus the module smoke check.
if(EMSCRIPTEN)
  set(_amberfolio_tests_default OFF)
else()
  set(_amberfolio_tests_default ON)
endif()
set(AMBERFOLIO_BUILD_TESTS ${_amberfolio_tests_default}
  CACHE BOOL "Build the unit tests (fetches GoogleTest)")
unset(_amberfolio_tests_default)

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

if(AMBERFOLIO_BUILD_HOST_SDL)
  add_subdirectory(hosts/sdl)
endif()

if(AMBERFOLIO_BUILD_HOST_WEB)
  add_subdirectory(hosts/web)
endif()

if(AMBERFOLIO_BUILD_TESTS)
  add_subdirectory(tests)
endif()

message(STATUS "amberfolio ${PROJECT_VERSION}: "
  "C++${AMBERFOLIO_CXX_STANDARD_EFFECTIVE}, "
  "warnings-as-errors=${AMBERFOLIO_WERROR}, "
  "sdl-host=${AMBERFOLIO_BUILD_HOST_SDL}, "
  "web-host=${AMBERFOLIO_BUILD_HOST_WEB}, "
  "tests=${AMBERFOLIO_BUILD_TESTS}, "
  "sanitizers=${_amberfolio_sanitize_summary}")
unset(_amberfolio_sanitize_summary)
