# SPDX-License-Identifier: AGPL-3.0-only
#
# libdeflate (MIT), fetched at configure time like every other dependency
# and never committed here.
#
# It arrived in M1 for the conformance harness, which inflates 323 gzipped
# vector files on every full run, and until M5-E3 (#174) that was its only
# consumer — which is why the declaration lived inside
# AmberfolioConformance.cmake. The journal's extractor is the second, and
# it is a different kind of consumer: it *ships*. So the declaration moved
# here, where both can include it, rather than being written twice with
# two sets of options that could drift.
#
# What that means for the module a player downloads: the decompressor and
# nothing else. Compression is off, so the half of the library that writes
# a stream is not linked at all, and what is left is a few tens of
# kilobytes of table-driven inflate.
#
#
# Why a library rather than an inflate of our own
# ----------------------------------------------
#
# The journal's streams are FlateDecode — a zlib stream, produced by
# whatever scanner or press made the player's PDF. An inflate written here
# would be perhaps two hundred lines of well-understood code, and this
# project has written harder things; the reason not to is that it could
# not be *tested*. Every stream this tree can produce for a test is a
# stream this tree wrote, and a decoder tested only against its own
# encoder is tested against its own misreadings. libdeflate is tested
# against the world's compressors, which is the property that is actually
# wanted here.
#
# MIT, so compatible with our AGPL-3.0-only outbound licence
# (CONTRIBUTING.md), and it is now the one third-party library that
# reaches a shipped host. NOTICE.md says so.

include_guard(GLOBAL)
include(FetchContent)

# Pinned, not floating — the same rule every other fetched dependency
# follows: a build of a given commit of this repository fetches the same
# library every time.
set(AMBERFOLIO_LIBDEFLATE_TAG "v1.25" CACHE STRING
  "libdeflate git tag to fetch")

# Read as cache entries by libdeflate's own CMakeLists, and forced for the
# same reason SDL3's are: a subproject that resets policies cannot be
# relied on to see a normal-variable override.
set(LIBDEFLATE_BUILD_STATIC_LIB ON  CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
# The libdeflate-gzip command-line program, which nothing here runs.
set(LIBDEFLATE_BUILD_GZIP       OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
# We inflate and never deflate: nothing in this tree writes a compressed
# stream, and leaving that half in would be dead code in a player's
# download.
set(LIBDEFLATE_COMPRESSION_SUPPORT OFF CACHE BOOL "" FORCE)
# Both wrappers, and for two different callers: the conformance harness
# reads gzip members, the journal's extractor reads the zlib streams a
# PDF's FlateDecode filter is made of. Before #174 the zlib half was off.
set(LIBDEFLATE_ZLIB_SUPPORT        ON  CACHE BOOL "" FORCE)
set(LIBDEFLATE_GZIP_SUPPORT        ON  CACHE BOOL "" FORCE)

FetchContent_Declare(libdeflate
  GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
  GIT_TAG        "${AMBERFOLIO_LIBDEFLATE_TAG}"
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  # SYSTEM: third-party headers are not held to our warning baseline.
  SYSTEM
  EXCLUDE_FROM_ALL)

message(STATUS "fetching libdeflate ${AMBERFOLIO_LIBDEFLATE_TAG}")
FetchContent_MakeAvailable(libdeflate)
