# SPDX-License-Identifier: AGPL-3.0-only
#
# amberfolio::warnings — an INTERFACE target carrying the warning baseline.
#
# It is linked by *our* targets only. Vendored third-party code (SDL3, and
# later GoogleTest and Tesseract) keeps its own settings and can never trip
# our -Werror; its headers are pulled in as SYSTEM includes for the same
# reason.

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
  if(AMBERFOLIO_WERROR)
    list(APPEND _amberfolio_warnings -Werror)
  endif()
endif()

# Deliberately absent: -Wconversion / -Wsign-conversion. The 8086 core is
# built out of 8- and 16-bit arithmetic that is *meant* to wrap and narrow,
# and blanket conversion warnings there produce casts that hide real bugs
# rather than expose them. Revisit per-target if that judgement changes.

target_compile_options(amberfolio-warnings INTERFACE
  "$<$<COMPILE_LANGUAGE:CXX>:${_amberfolio_warnings}>")

unset(_amberfolio_warnings)
