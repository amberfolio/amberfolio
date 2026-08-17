# SPDX-License-Identifier: AGPL-3.0-only
#
# The SDL3 desktop host — one host for Windows, macOS and Linux (PLAN.md §4).
# In M0 it is a stub: it proves the toolchain, the link line and the presets,
# and nothing more.

include(AmberfolioSDL3)

add_executable(amberfolio-sdl src/main.cpp)

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

# The stub asks SDL for no subsystems, so this runs on a headless CI box.
# It is a smoke test — that the host starts, links and exits cleanly — and
# stays separate from the unit tests in tests/, which never link a host.
add_test(NAME sdl-host-smoke COMMAND amberfolio-sdl)
set_tests_properties(sdl-host-smoke PROPERTIES LABELS smoke)
