# SPDX-License-Identifier: AGPL-3.0-only
#
# amberfolio-core — the emulator library.
#
# Freestanding in the sense PLAN.md §4 means it: no host dependencies. It
# links nothing but our own compile-flag interface targets, and that rule is
# the point of this file. Everything the machine needs from the outside
# world arrives through the narrow platform interface the hosts implement.
#
# M0 content was a placeholder: a version symbol and the C ABI shim that
# exposes it to a non-C++ host. M1 adds the first real component, the 8086
# interpreter, under src/cpu with its headers in include/amberfolio/cpu.
# M2 adds the machine around it, under src/machine — the memory map, the
# port map, the device contract and the wiring that holds them together.

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/include/amberfolio/version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/include/amberfolio/version.h"
  @ONLY)

add_library(amberfolio-core STATIC
  src/abi.cpp
  src/cpu/alu.cpp
  src/cpu/decoder.cpp
  src/cpu/dispatch.cpp
  src/cpu/instructions/add.cpp
  src/cpu/instructions/address.cpp
  src/cpu/instructions/bcd.cpp
  src/cpu/instructions/call.cpp
  src/cpu/instructions/div.cpp
  src/cpu/instructions/incdec.cpp
  src/cpu/instructions/interrupt.cpp
  src/cpu/instructions/jump.cpp
  src/cpu/instructions/logic.cpp
  src/cpu/instructions/misc.cpp
  src/cpu/instructions/mov.cpp
  src/cpu/instructions/mul.cpp
  src/cpu/instructions/shift.cpp
  src/cpu/instructions/stack.cpp
  src/cpu/instructions/string.cpp
  src/cpu/instructions/sub.cpp
  src/cpu/interrupts.cpp
  src/cpu/processor.cpp
  src/machine/device.cpp
  src/machine/dos.cpp
  src/machine/ega.cpp
  src/machine/fingerprint.cpp
  src/machine/int10.cpp
  src/machine/keyboard.cpp
  src/machine/loader.cpp
  src/machine/machine.cpp
  src/machine/memory_map.cpp
  src/machine/memory_vfs.cpp
  src/machine/pic.cpp
  src/machine/pit.cpp
  src/machine/platform.cpp
  src/machine/port_map.cpp
  src/machine/renderer.cpp
  src/machine/report.cpp
  src/machine/scheduler.cpp
  src/machine/seam.cpp
  src/machine/seam_code_wheel.cpp
  src/machine/service_floor.cpp
  src/machine/speaker.cpp
  src/machine/trace.cpp
  src/machine/vfs.cpp
  src/sha256.cpp
  src/version.cpp)
add_library(amberfolio::core ALIAS amberfolio-core)

target_compile_features(amberfolio-core
  PUBLIC cxx_std_${AMBERFOLIO_CXX_STANDARD_EFFECTIVE})

target_include_directories(amberfolio-core PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>")

target_link_libraries(amberfolio-core PRIVATE amberfolio::warnings)
