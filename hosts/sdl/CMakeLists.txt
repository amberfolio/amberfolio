# SPDX-License-Identifier: AGPL-3.0-only
#
# The SDL3 desktop host — one host for Windows, macOS and Linux (PLAN.md §4).
# Since M2-H1 (#54) it runs a real machine: window, audio, keyboard, and a
# directory-backed VFS. `--headless` drops the window and the audio device
# so the smoke test below still means something on a CI runner with
# neither.

include(AmberfolioSDL3)

add_executable(amberfolio-sdl
  src/directory_vfs.cpp
  src/main.cpp)

# The binary players run is just "amberfolio"; the target name distinguishes
# it from the web host's module, which is called amberfolio too.
set_target_properties(amberfolio-sdl PROPERTIES OUTPUT_NAME amberfolio)

target_link_libraries(amberfolio-sdl PRIVATE
  amberfolio::core
  amberfolio::warnings
  SDL3::SDL3)

# A shared SDL3 (system SDL3, or -DAMBERFOLIO_SDL3_SHARED=ON) leaves a DLL
# that Windows resolves from the executable's own directory. The $<IF:...>
# collapses to a no-op when the list is empty, which is the static case.
if(WIN32)
  add_custom_command(TARGET amberfolio-sdl POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E
      "$<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:amberfolio-sdl>>,copy_if_different,true>"
      "$<TARGET_RUNTIME_DLLS:amberfolio-sdl>"
      "$<TARGET_FILE_DIR:amberfolio-sdl>"
    COMMAND_EXPAND_LISTS
    VERBATIM)
endif()

# Two smoke tests, both headless so they run on a CI box with no display
# and no audio device.
#
# The first is the M0 check kept alive: invoked with no arguments the host
# prints usage and fails, which proves it starts, links and can talk. The
# second is the M2 one — it boots a real program out of a real directory
# and checks the exit code the program itself chose. Neither needs a
# window, which is the whole reason `--headless` exists.
add_test(NAME sdl-host-usage COMMAND amberfolio-sdl)
set_tests_properties(sdl-host-usage PROPERTIES
  LABELS smoke
  WILL_FAIL TRUE)

# The M2 check: a self-written program, assembled by the test rig into a
# directory this test makes, run headless through the whole machine. It
# asserts the program's own console output and the exit code the program
# chose, which together mean the loader, the CPU, the DOS layer, the
# console sink and the directory VFS all did their part.
add_executable(amberfolio-sdl-smoke-disk tests/make_smoke_disk.cpp)
# The standard explicitly: this target links no core library, and
# amberfolio::warnings carries flags rather than a language level, so
# without this it compiles as whatever the toolchain defaults to — which
# on MSVC is old enough not to have <filesystem>.
target_compile_features(amberfolio-sdl-smoke-disk
  PRIVATE cxx_std_${AMBERFOLIO_CXX_STANDARD_EFFECTIVE})
target_link_libraries(amberfolio-sdl-smoke-disk PRIVATE amberfolio::warnings)

add_test(NAME sdl-host-smoke-disk
  COMMAND amberfolio-sdl-smoke-disk
    "${CMAKE_CURRENT_BINARY_DIR}/smoke-disk")
set_tests_properties(sdl-host-smoke-disk PROPERTIES
  LABELS smoke
  FIXTURES_SETUP sdl_smoke_disk)

add_test(NAME sdl-host-runs-a-program
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/smoke-disk"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-smoke-program.cmake")
set_tests_properties(sdl-host-runs-a-program PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_smoke_disk)
