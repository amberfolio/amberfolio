# SPDX-License-Identifier: AGPL-3.0-only
#
# GoogleTest acquisition. Defines GTest::gtest, GTest::gtest_main,
# GTest::gmock and GTest::gmock_main, either from an installed GoogleTest
# or by fetching and building one at configure time.
#
# GoogleTest is BSD-3-Clause, which is compatible with our AGPL-3.0-only
# outbound license. Nothing from it is committed to this repository — same
# rule as SDL3, and the same reason: fetch, never vendor.
#
# Why GoogleTest at all (issue #4): dynamic registration (RegisterTest) is
# what lets the M1 CPU conformance suite turn each JSON vector file into
# its own named CTest case instead of one opaque pass/fail. GoogleMock
# comes along in the same build; hand-written fakes stay the default for
# the narrow platform interface, and gmock is there for the tests that
# genuinely want matchers or expectations.

include_guard(GLOBAL)
include(FetchContent)

option(AMBERFOLIO_USE_SYSTEM_GTEST
  "Use an installed GoogleTest instead of fetching and building one" OFF)

# Pinned, not floating — same reasoning as the SDL3 pin: a build of a given
# commit of this repository should fetch the same GoogleTest every time.
set(AMBERFOLIO_GTEST_TAG "v1.18.0" CACHE STRING
  "GoogleTest git tag to fetch when not using a system GoogleTest")
# The floor a *system* GoogleTest has to clear. 1.13 is where upstream
# started defining the GTest:: aliases for the fetched build too, so both
# paths through this file hand back the same target names.
set(AMBERFOLIO_GTEST_MIN_VERSION "1.13.0")

if(AMBERFOLIO_USE_SYSTEM_GTEST)
  find_package(GTest ${AMBERFOLIO_GTEST_MIN_VERSION} REQUIRED CONFIG)
  message(STATUS "GoogleTest: system ${GTest_VERSION}")
  return()
endif()

# GoogleTest reads these as cache entries, and like SDL it resets policies
# to its own minimum when added as a subdirectory, so the normal-variable
# override CMP0077 would give us is not reliable here. Force the cache.
#
# gtest_force_shared_crt: GoogleTest defaults to the static MSVC runtime
# (/MT) while CMake defaults everything else to the dynamic one (/MD).
# Left alone, the test binary fails to link with duplicate-symbol errors.
# No effect off MSVC.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        "${AMBERFOLIO_GTEST_TAG}"
  GIT_SHALLOW    TRUE
  GIT_PROGRESS   TRUE
  # SYSTEM: GoogleTest's headers must not be held to our warning baseline.
  SYSTEM
  # EXCLUDE_FROM_ALL: `cmake --build` should not build the test framework
  # for a configuration that never links it.
  EXCLUDE_FROM_ALL)

message(STATUS "GoogleTest: fetching ${AMBERFOLIO_GTEST_TAG}")
FetchContent_MakeAvailable(googletest)
