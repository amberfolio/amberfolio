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
#     ever writes a .gz. Its declaration is AmberfolioLibdeflate.cmake's
#     since M5-E3 (#174) gave it a second consumer that ships; this file
#     includes that one rather than keeping a second set of options.
#   * simdjson (Apache-2.0) parses at gigabytes per second. The
#     alternative considered was nlohmann/json, which is header-only and
#     pleasant and would have made the parse the dominant cost of the
#     suite by an order of magnitude.
#
# Both licenses are compatible with our AGPL-3.0-only outbound license.
# simdjson does not ship in the emulator: this file is included only when
# the tests are built, and only the conformance test binary links it.
# libdeflate does, now — the journal's extractor is a host, not a test —
# and AmberfolioLibdeflate.cmake is where that is written down.

include_guard(GLOBAL)
include(FetchContent)

# Pinned, not floating — the same rule the other fetched dependencies
# follow: a build of a given commit of this repository fetches the same
# libraries every time.
set(AMBERFOLIO_SIMDJSON_TAG "v4.6.7" CACHE STRING
  "simdjson git tag to fetch for the conformance harness")

# libdeflate is shared with the journal's extractor (hosts/common), so its
# declaration and its options are in one place.
include(AmberfolioLibdeflate)

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

message(STATUS "conformance: fetching simdjson ${AMBERFOLIO_SIMDJSON_TAG}")
FetchContent_MakeAvailable(simdjson)
