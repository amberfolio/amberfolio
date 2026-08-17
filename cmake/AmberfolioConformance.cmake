# SPDX-License-Identifier: AGPL-3.0-only
#
# The two libraries the CPU conformance harness needs (issue #14), fetched
# at configure time like SDL3 and GoogleTest and, like them, never
# committed here.
#
# The harness reads 323 gzipped JSON files — around 200 MB compressed and
# 1 GB of JSON once inflated — on every full run, so both of these are
# chosen for throughput rather than for being the obvious names:
#
#   * libdeflate (MIT) inflates gzip several times faster than zlib and
#     has a one-call API that takes a whole member at once, which is
#     exactly the shape of this problem. Decompression only: nothing here
#     ever writes a .gz.
#   * simdjson (Apache-2.0) parses at gigabytes per second. The
#     alternative considered was nlohmann/json, which is header-only and
#     pleasant and would have made the parse the dominant cost of the
#     suite by an order of magnitude.
#
# Both licenses are compatible with our AGPL-3.0-only outbound license,
# and neither ships in the emulator: this file is included only when the
# tests are built, and only the conformance test binary links them.

include_guard(GLOBAL)
include(FetchContent)

# Pinned, not floating — the same rule the other two fetched dependencies
# follow: a build of a given commit of this repository fetches the same
# libraries every time.
set(AMBERFOLIO_LIBDEFLATE_TAG "v1.25" CACHE STRING
  "libdeflate git tag to fetch for the conformance harness")
set(AMBERFOLIO_SIMDJSON_TAG "v4.6.7" CACHE STRING
  "simdjson git tag to fetch for the conformance harness")

# Read as cache entries by libdeflate's own CMakeLists, and forced for the
# same reason SDL3's are: a subproject that resets policies cannot be
# relied on to see a normal-variable override.
set(LIBDEFLATE_BUILD_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
# The libdeflate-gzip command-line program, which nothing here runs.
set(LIBDEFLATE_BUILD_GZIP       OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
# We inflate and never deflate, and we read gzip members rather than bare
# zlib streams. Leaving the other halves in would be dead code linked into
# a test binary.
set(LIBDEFLATE_COMPRESSION_SUPPORT OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_ZLIB_SUPPORT        OFF CACHE BOOL "" FORCE)

FetchContent_Declare(libdeflate
  GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
  GIT_TAG        "${AMBERFOLIO_LIBDEFLATE_TAG}"
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  # SYSTEM: third-party headers are not held to our warning baseline.
  SYSTEM
  EXCLUDE_FROM_ALL)

# simdjson only defines its developer targets when it is the top-level
# project, so there is nothing to switch off here; SIMDJSON_INSTALL
# already defaults to off for a subproject.
FetchContent_Declare(simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG        "${AMBERFOLIO_SIMDJSON_TAG}"
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  SYSTEM
  EXCLUDE_FROM_ALL)

message(STATUS
  "conformance: fetching libdeflate ${AMBERFOLIO_LIBDEFLATE_TAG}, "
  "simdjson ${AMBERFOLIO_SIMDJSON_TAG}")
FetchContent_MakeAvailable(libdeflate simdjson)
