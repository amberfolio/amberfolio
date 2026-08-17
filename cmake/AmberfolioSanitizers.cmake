# SPDX-License-Identifier: AGPL-3.0-only
#
# Sanitizer instrumentation, off unless asked for. The linux-asan-ubsan
# preset is the one that asks (PLAN.md §6: "Sanitizer (ASan/UBSan) jobs on
# the native build").
#
# Unlike amberfolio::warnings, this is NOT an interface target our own
# targets opt into — it is a directory-wide flag applied to everything the
# build compiles, third-party included. That is deliberate. ASan's
# container-overflow detection compares an instrumented allocator's
# redzones against uninstrumented std::vector internals and reports
# differences that are not bugs, so a half-instrumented program has to
# turn checks *off* to stay quiet. Instrumenting the whole thing keeps the
# check set intact and needs no suppressions, which is the deal the
# sanitizer job is built on.
#
# Included before the first add_subdirectory() so every target inherits it:
# add_compile_options() reaches targets created after the call, in this
# directory and in subdirectories added after it — including the ones
# FetchContent pulls in.

include_guard(GLOBAL)

set(AMBERFOLIO_SANITIZE "" CACHE STRING
  "Sanitizers to build with, as a -fsanitize= list (e.g. address,undefined)")

if(AMBERFOLIO_SANITIZE STREQUAL "")
  return()
endif()

# clang-cl accepts /fsanitize=address and nothing else here; MSVC has no
# UBSan at all. Rather than quietly building something that is not what
# was asked for, say so.
if(MSVC)
  message(FATAL_ERROR
    "AMBERFOLIO_SANITIZE needs a GCC- or Clang-style driver; MSVC has no "
    "UndefinedBehaviorSanitizer. Use the linux-asan-ubsan preset.")
endif()

add_compile_options(
  "-fsanitize=${AMBERFOLIO_SANITIZE}"
  # Every finding is fatal. Without this UBSan prints and carries on, so a
  # run that found real undefined behaviour still exits 0 and CI stays
  # green — the failure mode this job exists to rule out.
  -fno-sanitize-recover=all
  # ASan can unwind without it, but the frame pointer is what makes the
  # report name the function you were actually in.
  -fno-omit-frame-pointer
  # Regardless of configuration: a sanitizer report without line numbers
  # is a list of addresses. Debug already implies it; Release does not,
  # and a Release sanitizer run is the one that reaches the code the
  # assertions cut short.
  -g)

# The same flag at link time: -fsanitize= is what pulls in the runtime.
add_link_options("-fsanitize=${AMBERFOLIO_SANITIZE}")
