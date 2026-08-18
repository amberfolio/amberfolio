# SPDX-License-Identifier: AGPL-3.0-only
#
# The self-written 8086 programs, the machine that runs them, and the
# benchmark that times them.
#
# Outside the AMBERFOLIO_BUILD_TESTS guard in the directory above, and
# that is the point of it being its own directory: this fetches nothing
# and links nothing but the core, so it builds under Emscripten, where the
# GoogleTest rig does not. `ctest --preset wasm` running the benchmark is
# what exercises the interpreter on the target a browser gets — until now
# the wasm leg proved only that the core compiles and that the module
# answers with its version.
#
# The library is shared by two readers: amberfolio-bench times the
# programs, and the unit-test rig (tests/core/cpu/program_test.cpp)
# asserts their answers. What "correct" means lives in programs.cpp, once.

add_library(amberfolio-programs STATIC
  assembler.cpp
  harness.cpp
  programs.cpp)
add_library(amberfolio::programs ALIAS amberfolio-programs)

# tests/, not tests/programs/: an include then reads
# "programs/harness.h", which says where the header is from — the same
# way core headers are included by their path under include/. Inside this
# directory too, so there is one spelling and not two.
cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _amberfolio_tests_dir)
target_include_directories(amberfolio-programs PUBLIC "${_amberfolio_tests_dir}")
unset(_amberfolio_tests_dir)

# PUBLIC on the core: harness.h holds a processor and a bus by value, so
# anything that links this needs the core's headers. Our warning baseline
# is PRIVATE as everywhere else — test apparatus is our code, but the
# flags are not part of its interface.
target_link_libraries(amberfolio-programs
  PUBLIC amberfolio::core
  PRIVATE amberfolio::warnings)

add_executable(amberfolio-bench bench_main.cpp)
target_link_libraries(amberfolio-bench PRIVATE
  amberfolio::programs
  amberfolio::warnings)

if(EMSCRIPTEN)
  # ENVIRONMENT=node trims the glue to the one place this ever runs.
  # EXIT_RUNTIME is what carries main's return value out to node's exit
  # code, which is the whole of how CTest learns whether it passed.
  # ALLOW_MEMORY_GROWTH because the harness allocates the machine's
  # megabyte on top of whatever the default heap is spoken for.
  target_link_options(amberfolio-bench PRIVATE
    "-sENVIRONMENT=node"
    "-sEXIT_RUNTIME=1"
    "-sALLOW_MEMORY_GROWTH=1")

  # Already found by hosts/web for the module smoke check when that is
  # being built; find_program caches, so this is a no-op the second time.
  find_program(AMBERFOLIO_NODE NAMES node nodejs
    DOC "node executable used to run the wasm module's headless smoke check")

  if(AMBERFOLIO_NODE)
    add_test(NAME wasm-programs-bench
      COMMAND "${AMBERFOLIO_NODE}" "$<TARGET_FILE:amberfolio-bench>")
    set_tests_properties(wasm-programs-bench PROPERTIES LABELS bench)
  else()
    # Loud rather than quietly skipped, for the reason hosts/web gives:
    # the test presets set noTestsAction=error, so a box without node
    # should fail rather than pass an empty suite.
    message(WARNING
      "node was not found, so the wasm benchmark is not registered. Set "
      "AMBERFOLIO_NODE to point at one (emsdk ships a node under "
      "$ENV{EMSDK}/node).")
  endif()
else()
  # On the native targets the unit suite already asserts every one of
  # these answers, case by case. This case is registered anyway, and not
  # only for the numbers it prints: it is the one thing that runs the
  # programs through a plain main() with no test framework under them,
  # which is exactly how the wasm leg runs them.
  add_test(NAME programs-bench COMMAND amberfolio-bench)
  set_tests_properties(programs-bench PROPERTIES LABELS bench)
endif()
