# SPDX-License-Identifier: AGPL-3.0-only
#
# amberfolio-core — the emulator library.
#
# Freestanding in the sense PLAN.md §4 means it: no host dependencies. It
# links nothing but our own compile-flag interface targets, and that rule is
# the point of this file. Everything the machine needs from the outside
# world arrives through the narrow platform interface the hosts implement.
#
# M0 content is a placeholder: a version symbol, the C ABI shim that
# exposes it to a non-C++ host, and nothing else.

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/amberfolio/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/amberfolio/version.h"
  @ONLY)

add_library(amberfolio-core STATIC
  src/abi.cpp
  src/version.cpp)
add_library(amberfolio::core ALIAS amberfolio-core)

target_compile_features(amberfolio-core
  PUBLIC cxx_std_${AMBERFOLIO_CXX_STANDARD_EFFECTIVE})

target_include_directories(amberfolio-core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

target_link_libraries(amberfolio-core PRIVATE amberfolio::warnings)
