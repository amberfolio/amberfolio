# SPDX-License-Identifier: AGPL-3.0-only
#
# The SDL3 desktop host — one host for Windows, macOS and Linux (PLAN.md §4).
# Since M2-H1 (#54) it runs a real machine: window, audio, keyboard, and a
# directory-backed VFS. `--headless` drops the window and the audio device
# so the smoke test below still means something on a CI runner with
# neither.

include(AmberfolioSDL3)

add_executable(amberfolio-sdl
  src/audio_gain.cpp
  src/directory_vfs.cpp
  src/dump.cpp
  src/keymap.cpp
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

# The smoke tests. The first three are headless, so they run on a CI box
# with no display and no audio device; the fourth is not, and that is the
# point of it (#80).
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

# --- The boot driver's report (#83) ------------------------------------
#
# The same disk, and the other program on it: one that asks for a service
# nothing backs and lets the machine refuse. What this asserts is the
# *shape* of what comes back — the fingerprint at load, the fixed stop
# line, the service named alongside it, the worklist line, a bounded hang,
# and a dumped frame. tests/core/machine/report_test.cpp checks the
# formatting field by field; this checks that the shape survives a real
# host, a real directory and a real MZ load, which is the only way the
# browser's copy of the same line (#84) can be compared against anything.
add_test(NAME sdl-host-reports-a-stop
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/smoke-disk"
    "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/smoke-dump"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-stop-report.cmake")
set_tests_properties(sdl-host-reports-a-stop PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_smoke_disk)

# --- The windowed check (#80) ------------------------------------------
#
# Everything above this line runs with `--headless`, which is what makes
# it meaningful on a runner. What that leaves untouched is everything at
# the window: the texture upload, the integer scaling, SDL's audio stream
# and the callback that calls `audio_timeline::render()` off the machine
# thread, and the step from an SDL key event to a posted XT scan code.
# All of it had been compiled on three desktop targets and run on none.
#
# So: a second disk, holding M2-T1's composite program and a demo program
# for a person to look at and listen to, and one more case that runs the
# composite with a real window and a real audio device — under SDL's
# `dummy` video and audio drivers, which is what a runner has.
# cmake/run-verify-program.cmake says what it asserts and why.
#
# The generator links amberfolio::programs, which is how it gets the
# composite byte for byte rather than building a second one that would
# drift. That library fetches nothing and links nothing but the core
# (tests/programs/CMakeLists.txt), and the web host already links it for
# the same reason, so this is a dependency the tree already has.
add_executable(amberfolio-sdl-demo-disk tests/make_demo_disk.cpp)
target_link_libraries(amberfolio-sdl-demo-disk PRIVATE
  amberfolio::programs
  amberfolio::warnings)

add_test(NAME sdl-host-demo-disk
  COMMAND amberfolio-sdl-demo-disk
    "${CMAKE_CURRENT_BINARY_DIR}/demo-disk")
set_tests_properties(sdl-host-demo-disk PROPERTIES
  LABELS smoke
  FIXTURES_SETUP sdl_demo_disk)

# Two runs of the same script over the same disk, because the two
# programs answer different halves of the question. The composite draws a
# picture derived pixel by pixel in #56, echoes the keystroke this host
# posts and exits with a code it chose — the video, keyboard and exit
# paths. The demo plays continuously, so it is the one that can be asked
# whether what reached the audio device was a tone rather than a
# well-plumbed silence.
#
# Since M4-A1 (#106) the demo's run is also asked the *other* audio
# question, and it is a different one: `--dump` now writes the edge list
# the machine published, and every gap in it must be the 1356 ticks that
# divisor 2712 in mode 3 means. "What reached the device" and "what the
# machine produced" have different owners in the code, and this is the
# first check in the tree that asks the second of them through a host.
add_test(NAME sdl-host-presents-a-frame
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/demo-disk"
    "-DPROGRAM=COMPOSIT.EXE"
    "-DPRESS=A@60"
    "-DEXPECT_CODE=90"
    "-DEXPECT_CONSOLE=aDONE"
    "-DSTILLS=${CMAKE_CURRENT_BINARY_DIR}/verify-stills"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-verify-program.cmake")
set_tests_properties(sdl-host-presents-a-frame PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_demo_disk)

add_test(NAME sdl-host-sounds-a-tone
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/demo-disk"
    "-DPROGRAM=DEMO.EXE"
    "-DPRESS=Escape@60"
    "-DEXPECT_CODE=0"
    "-DEXPECT_SOUND=ON"
    "-DEDGES=${CMAKE_CURRENT_BINARY_DIR}/verify-edges"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-verify-program.cmake")
set_tests_properties(sdl-host-sounds-a-tone PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_demo_disk)

# --- And the same program with the volume down (#148) ------------------
#
# The case above is the only one in the tree where silence would be wrong,
# which makes it the only one where *silence on purpose* can be asked for
# and told apart from a broken audio path. So: the same program, the same
# window, the same device, one more word on the command line, and the
# opposite expectation.
#
# What it settles is the whole of volume and mute on this host, end to
# end: an option parsed, a level handed to `audio_gain` before the device
# opens, an atomic read on SDL's own audio thread, and a device that got
# nothing but zeros out of a program that was playing a 440 Hz tone
# throughout. `sounded 0` is only meaningful because the run beside it
# proves the same program sounds; a mute that "worked" because nothing
# was playing would be no check at all.
#
# It does not check the two runtime keys. F11 and F12 change the same two
# variables this reaches through `--volume`/`--mute`, and what is
# genuinely separate about them — that they cost the emulated program
# nothing — is `keymap_test.cpp`'s, where the claim is that the board has
# no scan code for either.
add_test(NAME sdl-host-mutes-the-tone
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/demo-disk"
    "-DPROGRAM=DEMO.EXE"
    "-DPRESS=Escape@60"
    "-DEXPECT_CODE=0"
    "-DEXPECT_SILENCE=ON"
    "-DEXTRA=--mute"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-verify-program.cmake")
set_tests_properties(sdl-host-mutes-the-tone PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_demo_disk)

# --- The round trip (#100) ---------------------------------------------
#
# M4-R1's claim, as one case: a run is keys, ticks and hashes, and a
# second machine handed those three reproduces the first exactly. Over
# the same disk and the same composite as the case above, because a
# program that draws, sounds, reads a key and exits with a code of its
# own exercises every part of the state a checkpoint hashes.
# cmake/run-record-replay.cmake says what it asserts and why, including
# the tampered recording it finishes with — a check that cannot fail is
# not one.
#
# It records the run twice: once at a checkpoint a frame, and once at one
# every eight (#101). Eight does not divide sixty, which is the frame the
# scripted press lands on, so the checkpoint beside that key in the sparse
# recording is there because a key was posted and for no other reason.
add_test(NAME sdl-host-records-and-replays
  COMMAND "${CMAKE_COMMAND}"
    "-DHOST=$<TARGET_FILE:amberfolio-sdl>"
    "-DDISK=${CMAKE_CURRENT_BINARY_DIR}/demo-disk"
    "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/replay-work"
    "-DPROGRAM=COMPOSIT.EXE"
    "-DPRESS=A@60"
    "-DEXPECT_CODE=90"
    "-DRECORD_EVERY=8"
    "-DRECORD_UNTIL=400000"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-record-replay.cmake")
set_tests_properties(sdl-host-records-and-replays PROPERTIES
  LABELS smoke
  FIXTURES_REQUIRED sdl_demo_disk)

# --- The committed session, on this host (#100) ------------------------
#
# The third leg of the cross-target proof. `tests/core/machine/
# session_test.cpp` verifies tests/sessions/spin.rec through the ABI on a
# native build and `hosts/web/tests/smoke.mjs` verifies the same file on
# wasm; this is the desktop host doing it through its own `--replay`,
# over the disk the session was recorded against.
#
# It is worth being a third and not a repetition. This host builds its
# device set with its own code rather than through
# `af_machine_attach_reference_devices()`, and the canonical state hashes
# attached devices in attach order — so a wiring that drifted apart from
# the ABI's would pass both of the other two and fail here. That is not
# hypothetical: it is what #100 found, in the other direction.
#
# Headless, because nothing about a replay wants a window: the machine is
# checked against the recording, not the picture against the eye.
add_test(NAME sdl-host-verifies-a-session
  COMMAND amberfolio-sdl
    "${PROJECT_SOURCE_DIR}/tests/sessions/spin"
    SPIN.EXE
    --headless
    --replay "${PROJECT_SOURCE_DIR}/tests/sessions/spin.rec")
set_tests_properties(sdl-host-verifies-a-session PROPERTIES
  LABELS smoke
  PASS_REGULAR_EXPRESSION "replay verified checkpoints=[1-9][0-9]*")

# --- The mapping table, on its own (#80) -------------------------------
#
# The check above posts one key. The table has eighty-odd rows, and a
# table like that is exactly the kind of thing that is wrong in one row
# and right in every other — which is a bug a player finds, in a game,
# on the one key nobody pressed here.
#
# So src/keymap.cpp is its own translation unit and this asserts it
# against core's `xt_keyboard::xt_table`, which was written from the same
# hardware facts by different hands on a different day. tests/keymap_test.cpp
# says what the three claims are.
if(AMBERFOLIO_BUILD_TESTS)
  include(AmberfolioGoogleTest)
  include(GoogleTest)

  add_executable(amberfolio-sdl-tests
    src/audio_gain.cpp
    src/keymap.cpp
    tests/audio_gain_test.cpp
    tests/keymap_test.cpp)
  target_include_directories(amberfolio-sdl-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(amberfolio-sdl-tests PRIVATE
    amberfolio::core
    amberfolio::warnings
    SDL3::SDL3
    GTest::gmock_main)

  gtest_discover_tests(amberfolio-sdl-tests
    DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS unit)
endif()
