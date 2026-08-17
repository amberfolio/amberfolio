# SPDX-License-Identifier: AGPL-3.0-only
#
# The unit-test rig (issue #4). One executable per area of the codebase,
# each discovered into CTest case by case — so `ctest --preset <platform>`
# reports individual test names, not one opaque binary.
#
# M1's CPU conformance suite and M2's device tests drop in here: add
# sources to an existing target, or a new add_executable + the same three
# lines of link/discover boilerplate below.
#
# Fixtures live beside the tests that read them and are self-written or
# public/MIT-licensed vectors only — the content guard applies to test
# data exactly as it does to everything else (PLAN.md §6).

include(AmberfolioGoogleTest)
include(GoogleTest)  # gtest_discover_tests, shipped with CMake itself

add_executable(amberfolio-core-tests
  core/abi_test.cpp
  core/cpu/alu_test.cpp
  core/cpu/decoder_test.cpp
  core/cpu/processor_test.cpp
  core/cpu/registers_test.cpp
  core/version_test.cpp)

# So a test can say #include "cpu/test_bus.h" — shared test apparatus is
# included by its path under tests/core/, the same way core headers are
# included by their path under include/. Not PUBLIC on anything: nothing
# outside this directory has any business seeing it.
target_include_directories(amberfolio-core-tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/core")

# gmock_main, not gtest_main: it brings gtest with it and initialises both,
# so a test that wants a matcher does not need the link line changed. Our
# warning baseline applies — test code is our code.
target_link_libraries(amberfolio-core-tests PRIVATE
  amberfolio::core
  amberfolio::warnings
  GTest::gmock_main)

# PRE_TEST rather than the default POST_BUILD: discovery then happens when
# ctest runs, not by executing the freshly-built binary during the build.
# That keeps the build honest under a cross-compiling or emulated toolchain
# and keeps `cmake --build` from running code. The cost is that a binary
# which cannot start is reported by ctest instead of by the build.
gtest_discover_tests(amberfolio-core-tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS unit)
