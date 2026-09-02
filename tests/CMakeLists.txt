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

# The self-written 8086 programs, the machine that runs them, and their
# benchmark. Added before the guard below and not inside it: they fetch
# nothing, they link nothing but the core, and they are the only test
# apparatus that builds under Emscripten. See programs/CMakeLists.txt.
add_subdirectory(programs)

# Everything past here is the GoogleTest rig, which needs GoogleTest.
if(NOT AMBERFOLIO_BUILD_TESTS)
  return()
endif()

include(AmberfolioGoogleTest)
include(GoogleTest)  # gtest_discover_tests, shipped with CMake itself

add_executable(amberfolio-core-tests
  core/abi_test.cpp
  core/cpu/alu_test.cpp
  core/cpu/decoder_test.cpp
  core/cpu/interrupts_test.cpp
  core/cpu/misc_test.cpp
  core/cpu/processor_test.cpp
  core/cpu/program_test.cpp
  core/cpu/registers_test.cpp
  core/machine/clock_test.cpp
  core/machine/dos_test.cpp
  core/machine/document_test.cpp
  core/machine/edition_test.cpp
  core/machine/ega_test.cpp
  core/machine/fingerprint_test.cpp
  core/machine/int10_test.cpp
  core/machine/keyboard_test.cpp
  core/machine/loader_test.cpp
  core/machine/log_test.cpp
  core/machine/machine_program_test.cpp
  core/machine/machine_test.cpp
  core/machine/memory_map_test.cpp
  core/machine/memory_vfs_test.cpp
  core/machine/overlay_test.cpp
  core/machine/pic_test.cpp
  core/machine/pit_test.cpp
  core/machine/platform_test.cpp
  core/machine/renderer_test.cpp
  core/machine/replay_test.cpp
  core/machine/report_test.cpp
  core/machine/scheduler_test.cpp
  core/machine/seam_automap_test.cpp
  core/machine/seam_explored_test.cpp
  core/machine/seam_call_test.cpp
  core/machine/seam_cheats_test.cpp
  core/machine/seam_encamp_test.cpp
  core/machine/seam_journal_test.cpp
  core/machine/seam_test.cpp
  core/machine/service_floor_test.cpp
  core/machine/session_test.cpp
  core/machine/speaker_test.cpp
  core/machine/state_test.cpp
  core/machine/trace_test.cpp
  core/machine/vfs_test.cpp
  core/sha256_test.cpp
  core/version_test.cpp)

# So a test can say #include "cpu/test_bus.h" or "machine/test_device.h" —
# shared test apparatus is included by its path under tests/core/, the same
# way core headers are included by their path under include/. Not PUBLIC on
# anything: nothing outside this directory has any business seeing it.
target_include_directories(amberfolio-core-tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/core")

# gmock_main, not gtest_main: it brings gtest with it and initialises both,
# so a test that wants a matcher does not need the link line changed. Our
# warning baseline applies — test code is our code.
# Where the committed recordings live (tests/sessions/README.md).
# A compile definition and not a search: the sessions are source,
# they sit beside the test that reads them, and a test that hunted
# for them could pass by finding nothing.
target_compile_definitions(amberfolio-core-tests PRIVATE
  "AMBERFOLIO_SESSIONS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/sessions\"")

target_link_libraries(amberfolio-core-tests PRIVATE
  amberfolio::core
  amberfolio::programs
  amberfolio::warnings
  GTest::gmock_main)

# PRE_TEST rather than the default POST_BUILD: discovery then happens when
# ctest runs, not by executing the freshly-built binary during the build.
# That keeps the build honest under a cross-compiling or emulated toolchain
# and keeps `cmake --build` from running code. The cost is that a binary
# which cannot start is reported by ctest instead of by the build.
# TIMEOUT: the test presets set 120 seconds for everything
# (CMakePresets.json's `test-base`, whose whole reason is that a hung test
# must fail and soon). A test's own TIMEOUT property beats that default,
# and one case in this binary needs it to: nothing else here is close.
# `AbiVfs.SaysOutOfRoomRatherThanInvalidWhenTheEntryTableIsFull`
# fills the virtual filesystem's whole entry table to prove that "no room"
# and "bad name" are different answers, and that fill is quadratic in
# `max_entries` (memory_vfs.h says why every write is a pass over the
# offsets). It takes 23 s on an idle desktop in a Debug build, and on the
# ASan runner under `--parallel 4` it has come in at 119.9 s and been
# killed — a coin flip, and a gate that fails half the time is worse than
# no gate.
#
# Named here rather than fixed there: the quadratic fill is the
# filesystem's business and this file has no opinion about it. Three
# hundred seconds is two and a half times the worst measured run, which is
# headroom against a loaded runner and still short enough that a genuinely
# hung case is killed rather than waited out.
gtest_discover_tests(amberfolio-core-tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES
    LABELS unit
    TIMEOUT 300)

# --- The CPU conformance suite (issue #14) -----------------------------
#
# Its own binary, not another source file in the one above: it links two
# more third-party libraries, its cases are registered at run time rather
# than by TEST() macros, and it carries the `conformance` label so that
# `ctest -L unit` stays the fast answer and `ctest -L conformance` is the
# slow, thorough one.
#
# The vectors themselves are never here. They are fetched and condensed
# into a cache outside the source tree by
# scripts/fetch-conformance-vectors.py; without them every case reports
# SKIPPED rather than failing, so a contributor who has not run the script
# still gets a green `ctest`.

if(AMBERFOLIO_BUILD_CONFORMANCE)
  include(AmberfolioConformance)

  # One list, two readers. The manifest is what the fetch script
  # downloads; this turns the same file into the harness's registry, so
  # the two cannot disagree about what the pinned suite contains.
  set(_amberfolio_manifest
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/vector-files.txt")

  # How many files the pin has. A fact of commit
  # aea84484abc79d09639d855b7b0ab32bc9e4dbeb, stated a second time here
  # on purpose (#34): since M1's closeout every stem in the manifest runs
  # and is expected to pass, so the manifest is the one place left where
  # the suite could get smaller — and a suite that quietly lost a file
  # reports exactly what a whole one does, "100% tests passed". Deleting
  # a line now fails the configure instead. The number changes when the
  # pin changes, and only then.
  set(_amberfolio_pinned_stem_count 323)
  set_property(DIRECTORY APPEND PROPERTY
    CMAKE_CONFIGURE_DEPENDS "${_amberfolio_manifest}")

  # Matched positively rather than by skipping "#" lines: file(STRINGS)
  # splits on any non-ASCII byte, so an em dash in a comment arrives here
  # as two fragments and the second one has no "#" on it. A stem is two
  # hex digits with an optional ".0"-".7" group suffix, and nothing else
  # in the file looks like that.
  file(STRINGS "${_amberfolio_manifest}" _amberfolio_manifest_lines)
  set(AMBERFOLIO_CONFORMANCE_STEMS "")
  set(AMBERFOLIO_CONFORMANCE_STEM_COUNT 0)
  foreach(_line IN LISTS _amberfolio_manifest_lines)
    if(_line MATCHES "^[0-9A-F][0-9A-F](\\.[0-7])?$")
      string(APPEND AMBERFOLIO_CONFORMANCE_STEMS "        \"${_line}\",\n")
      math(EXPR AMBERFOLIO_CONFORMANCE_STEM_COUNT
        "${AMBERFOLIO_CONFORMANCE_STEM_COUNT} + 1")
    endif()
  endforeach()

  if(NOT AMBERFOLIO_CONFORMANCE_STEM_COUNT EQUAL _amberfolio_pinned_stem_count)
    message(FATAL_ERROR
      "${_amberfolio_manifest} lists ${AMBERFOLIO_CONFORMANCE_STEM_COUNT} "
      "vector stems; the pinned suite has ${_amberfolio_pinned_stem_count}. "
      "Either a line was lost — which would shrink the conformance suite "
      "without turning anything red, and is the whole reason this check "
      "exists — or the pin moved, in which case update "
      "_amberfolio_pinned_stem_count above together with "
      "scripts/fetch-conformance-vectors.py.")
  endif()

  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance/vector_stems.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/vector_stems.h"
    @ONLY)

  add_executable(amberfolio-conformance-tests
    conformance/conformance_test.cpp
    conformance/machine.cpp
    conformance/machine_test.cpp
    conformance/registry.cpp
    conformance/vectors.cpp)

  target_include_directories(amberfolio-conformance-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/conformance"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

  # GTest::gmock rather than gmock_main: this binary registers its cases
  # instead of declaring them, so it has a main() of its own
  # (conformance_test.cpp).
  target_link_libraries(amberfolio-conformance-tests PRIVATE
    amberfolio::core
    amberfolio::warnings
    GTest::gmock
    libdeflate::libdeflate_static
    simdjson::simdjson)

  # std::getenv is how the harness is configured (cache location, test
  # limit), and MSVC deprecates it in favour of a Microsoft-only
  # alternative. Silenced here rather than in the source, because the
  # source is portable and the objection is not.
  target_compile_definitions(amberfolio-conformance-tests PRIVATE
    $<$<BOOL:${MSVC}>:_CRT_SECURE_NO_WARNINGS>)

  # C4706, assignment within a conditional expression: simdjson's
  # iterators are written that way, and MSVC blames the instantiation
  # rather than the definition however hard it is told the header is
  # external (see cmake/AmberfolioWarnings.cmake). Narrowed to the one
  # target that includes simdjson; GCC and Clang need nothing, because
  # -isystem does what it says.
  target_compile_options(amberfolio-conformance-tests PRIVATE
    $<$<BOOL:${MSVC}>:/wd4706>)

  # TIMEOUT: the biggest files hold ten thousand vectors each, and the
  # preset's two-minute default exists to catch a hung unit test, not to
  # bound this.
  #
  # SKIP_REGULAR_EXPRESSION: GoogleTest exits 0 for a skipped test, so
  # without this a suite that opened no vectors at all would report itself
  # as passing. One skip is left since M1's closeout — the vectors are not
  # on this machine — and it has to read as a skip, because that is what
  # lets CI tell "323 files pass" from "323 files were never run".
  #
  # The obvious value here is GoogleTest's own banner, "\[ +SKIPPED +\]".
  # Do not use it. gtest_discover_tests writes each property into a
  # generated CMake script, and some versions wrap the value in [[...]] —
  # which a value *ending in* ] closes early, leaving a stray bracket and
  # a syntax error in a file nobody wrote by hand. (Newer CMake wraps in
  # [==[...]==] and sets this very property itself, which is why the
  # breakage showed up on two runners and not the others.) A bare word
  # matches nothing that a passing or failing test prints, needs no
  # quoting anywhere, and cannot come apart.
  gtest_discover_tests(amberfolio-conformance-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES
      LABELS conformance
      TIMEOUT 900
      SKIP_REGULAR_EXPRESSION SKIPPED)
endif()
