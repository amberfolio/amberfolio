# SPDX-License-Identifier: AGPL-3.0-only
#
# C++ standard selection: C++23 where the toolchain has it, C++20 where it
# lags (PLAN.md §1). Nothing in the codebase may *require* C++23 without a
# C++20 fallback path, or the fallback is a lie — this module only decides
# which standard to ask the compiler for.
#
# Sets AMBERFOLIO_CXX_STANDARD_EFFECTIVE (20 or 23) in the caller's scope.
# Targets consume it as a compile feature; they never set CMAKE_CXX_STANDARD.

set(AMBERFOLIO_CXX_STANDARD "" CACHE STRING
  "C++ standard to build with: 23, 20, or empty to auto-detect")
set_property(CACHE AMBERFOLIO_CXX_STANDARD PROPERTY STRINGS "" 23 20)

if(AMBERFOLIO_CXX_STANDARD STREQUAL "")
  if("cxx_std_23" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(AMBERFOLIO_CXX_STANDARD_EFFECTIVE 23)
  elseif("cxx_std_20" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    set(AMBERFOLIO_CXX_STANDARD_EFFECTIVE 20)
    message(STATUS
      "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} does not offer "
      "C++23; falling back to C++20.")
  else()
    message(FATAL_ERROR
      "Amber Folio needs at least C++20; ${CMAKE_CXX_COMPILER_ID} "
      "${CMAKE_CXX_COMPILER_VERSION} does not offer it.")
  endif()
else()
  if(NOT AMBERFOLIO_CXX_STANDARD MATCHES "^(20|23)$")
    message(FATAL_ERROR
      "AMBERFOLIO_CXX_STANDARD must be 20, 23, or empty (got "
      "'${AMBERFOLIO_CXX_STANDARD}').")
  endif()
  if(NOT "cxx_std_${AMBERFOLIO_CXX_STANDARD}" IN_LIST CMAKE_CXX_COMPILE_FEATURES)
    message(FATAL_ERROR
      "C++${AMBERFOLIO_CXX_STANDARD} was requested but ${CMAKE_CXX_COMPILER_ID} "
      "${CMAKE_CXX_COMPILER_VERSION} does not offer it.")
  endif()
  set(AMBERFOLIO_CXX_STANDARD_EFFECTIVE ${AMBERFOLIO_CXX_STANDARD})
endif()
