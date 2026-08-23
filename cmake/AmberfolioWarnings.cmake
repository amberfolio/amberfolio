# SPDX-License-Identifier: AGPL-3.0-only
#
# amberfolio::warnings — an INTERFACE target carrying the warning baseline.
#
# It is linked by *our* targets only — including the tests, which are our
# code. Third-party code (SDL3, GoogleTest, and later Tesseract) keeps its
# own settings and can never trip our -Werror; its headers are pulled in as
# SYSTEM includes for the same reason.

add_library(amberfolio-warnings INTERFACE)
add_library(amberfolio::warnings ALIAS amberfolio-warnings)

# MSVC is true for clang-cl too, which wants the MSVC-style spellings.
if(MSVC)
  set(_amberfolio_warnings
    /W4
    /permissive-   # standard conformance, not the Microsoft dialect
    /utf-8)        # sources and execution charset are both UTF-8
  if(AMBERFOLIO_WERROR)
    list(APPEND _amberfolio_warnings /WX)
  endif()
else()
  set(_amberfolio_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Woverloaded-virtual
    -Wformat=2
    -Wimplicit-fallthrough
    -Wnull-dereference
    -Wdouble-promotion)

  # Partial designated initializers (#79): `{.a = 1}` on an aggregate
  # whose `b` has no default member initializer. Clang diagnoses that as
  # -Wmissing-designated-field-initializers, and -Wextra above already
  # pulls it in, so naming it here changes nothing today.
  #
  # It is named anyway because the compiler that enforces -Wextra is not
  # pinned. .llvm-version pins clang-format and clang-tidy; the
  # linux-clang preset asks for plain `clang++` and gets whatever the
  # runner image ships, and .clang-tidy's check list excludes
  # clang-diagnostic-*, so the tidy gate never sees a compiler warning
  # either. That leaves one unpinned leg deciding this for every
  # toolchain — GCC has no equivalent diagnostic and MSVC none — and
  # leaves it decided by which warnings -Wextra happens to contain in
  # that build of that compiler. Spelling the flag out makes it this
  # baseline's decision instead of a side effect, which is the drift the
  # issue was filed about.
  #
  # Probed rather than assumed: an unrecognised positive -W option is a
  # hard error on GCC, and under -Werror it is one on any Clang too old
  # to know this flag. The probe needs -Werror itself for a subtler
  # reason — Clang *accepts* an unknown -W option and merely warns, so a
  # check without it succeeds for a flag that would do nothing.
  include(CheckCXXCompilerFlag)
  set(_amberfolio_saved_required_flags "${CMAKE_REQUIRED_FLAGS}")
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Werror")
  check_cxx_compiler_flag(-Wmissing-designated-field-initializers
    AMBERFOLIO_HAVE_WMISSING_DESIGNATED_FIELD_INITIALIZERS)
  set(CMAKE_REQUIRED_FLAGS "${_amberfolio_saved_required_flags}")
  unset(_amberfolio_saved_required_flags)

  if(AMBERFOLIO_HAVE_WMISSING_DESIGNATED_FIELD_INITIALIZERS)
    list(APPEND _amberfolio_warnings
      -Wmissing-designated-field-initializers)
  else()
    # Not fatal: GCC and older Clangs still build the project, they just
    # cannot check this. Said out loud so it is a known gap rather than a
    # silent one.
    message(STATUS
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} has no "
      "-Wmissing-designated-field-initializers; partial designated "
      "initializers go unchecked on this toolchain.")
  endif()

  if(AMBERFOLIO_WERROR)
    list(APPEND _amberfolio_warnings -Werror)
  endif()
endif()

# A note for whoever hits this next. -isystem genuinely silences a
# third-party header on GCC and Clang; MSVC's equivalent does not always,
# even with the /external:I and /external:W0 that CMake passes for a
# SYSTEM include directory — a warning raised inside a header-only
# template is still blamed on the code that instantiated it, and
# /external:templates- does not rescue every case. Where that bites, the
# suppression belongs on the target that includes the header (see
# tests/CMakeLists.txt), not on this baseline.

# Deliberately absent: -Wconversion / -Wsign-conversion. The 8086 core is
# built out of 8- and 16-bit arithmetic that is *meant* to wrap and narrow,
# and blanket conversion warnings there produce casts that hide real bugs
# rather than expose them. Revisit per-target if that judgement changes.

target_compile_options(amberfolio-warnings INTERFACE
  "$<$<COMPILE_LANGUAGE:CXX>:${_amberfolio_warnings}>")

unset(_amberfolio_warnings)
