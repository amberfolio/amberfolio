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

# The SDL host is a desktop thing; the wasm target (issue #2) brings its own
# hand-written JS host instead.
if(EMSCRIPTEN)
  set(_amberfolio_sdl_host_default OFF)
else()
  set(_amberfolio_sdl_host_default ON)
endif()
set(AMBERFOLIO_BUILD_HOST_SDL ${_amberfolio_sdl_host_default}
  CACHE BOOL "Build the SDL3 desktop host")
unset(_amberfolio_sdl_host_default)

include(AmberfolioCxxStandard)
include(AmberfolioWarnings)

enable_testing()

add_subdirectory(core)

if(AMBERFOLIO_BUILD_HOST_SDL)
  add_subdirectory(hosts/sdl)
endif()

message(STATUS "amberfolio ${PROJECT_VERSION}: "
  "C++${AMBERFOLIO_CXX_STANDARD_EFFECTIVE}, "
  "warnings-as-errors=${AMBERFOLIO_WERROR}, "
  "sdl-host=${AMBERFOLIO_BUILD_HOST_SDL}")
