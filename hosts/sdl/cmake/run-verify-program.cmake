# SPDX-License-Identifier: AGPL-3.0-only
#
# Runs the SDL host with a window, an audio device and a keystroke — the
# paths `--headless` is defined as not taking — and checks what came back.
#
# The point of #80 is that everything here had been compiled on three
# desktop targets and run on none: the texture upload, the integer
# scaling, the audio callback on its own thread, and the step from an SDL
# key event to a posted XT scan code. A CI runner has no display and no
# sound card, so this points SDL at the drivers it ships for exactly that
# situation. They are not stubs of ours and they are not a different code
# path — SDL's window, renderer, texture, audio stream and event queue are
# all the real ones; `dummy` gives them a surface nobody sees and a device
# nobody hears.
#
# `dummy` and not `offscreen`, which is the other headless video driver and
# was the first choice here. It is the better-sounding one and it does not
# work: offscreen creates its windows through EGL, and a macOS runner has
# no EGL, so `SDL_CreateWindowAndRenderer` fails there with "Could not
# initialize OpenGL / GLES library" before any of this can be asked.
# `dummy` allocates a plain framebuffer and wants no graphics library at
# all, which is exactly what a software renderer reading its own target
# back needs. Learned from a red macOS leg, and written down so the
# better-sounding name does not get chosen again.
#
# `software` for the renderer is not about availability but about being
# able to say what happened: the host's `--verify` reads the render target
# back and compares every pixel of it against the bytes it uploaded, and a
# software rasterizer answers that question identically on Windows, macOS
# and Linux. An accelerated backend would answer it too, and that is worth
# doing on a machine that has one — see docs/hosts.md — but it is not what
# a comparison across three runners should rest on.
#
# What is deliberately still not checked here: that a photon left a
# display and a pressure wave left a speaker. Nothing on a runner can
# check those. docs/hosts.md is how a person does, and #80 stays open for
# that half until they have.

if(NOT HOST OR NOT DISK OR NOT PROGRAM OR NOT DEFINED EXPECT_CODE)
  message(FATAL_ERROR
    "run-verify-program.cmake needs -DHOST=, -DDISK=, -DPROGRAM= and"
    " -DEXPECT_CODE=")
endif()

set(ENV{SDL_VIDEODRIVER} "dummy")
set(ENV{SDL_AUDIODRIVER} "dummy")
set(ENV{SDL_RENDER_DRIVER} "software")

# The key at frame 60, which is a full second of virtual frames after the
# composite has drawn and stopped its tone. Not because the timing is
# delicate — a key posted early would simply sit in the BDA buffer and be
# read the moment INT 16h asks — but because the second it spends halted
# is the interesting part: the machine is idle in HLT while this host goes
# on presenting frames and SDL's audio thread goes on calling
# `audio_timeline::render()`. That is the one core function callable off
# the machine thread, and a second of it under a real device's callback is
# the closest a runner gets to the threading mistake #80 worried about.
# `--dump-every` when the caller gave a prefix (M4-G1, #102). This is the
# only check in the tree where a run lasts long enough to have a *series*
# of frames, which is the whole of what the option is: everything a
# player-supplied copy does past the title happens over tens of virtual
# seconds, and one final frame cannot say what the screen did on the way.
set(stills)
if(STILLS)
  file(GLOB _stale "${STILLS}-*.ppm")
  if(_stale)
    file(REMOVE ${_stale})
  endif()
  set(stills --dump "${STILLS}" --dump-every 15)
elseif(EDGES)
  # `--dump` without the cadence, for the sake of its third file: the
  # edge list the machine published, which is the canonical audio state
  # and the only artefact of a run that can be checked against a
  # programmed divisor rather than listened to (M4-A1, #106). Only one of
  # the two, because they share a prefix and the caller who wants stills
  # is not the caller who wants this.
  file(REMOVE "${EDGES}.edges")
  set(stills --dump "${EDGES}")
endif()

execute_process(
  COMMAND "${HOST}" "${DISK}" "${PROGRAM}" --scale 2 --verify --press "${PRESS}"
    ${stills}
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

set(context "exit: ${code}\nstdout: ${out}\nstderr: ${err}")

# The code the program's own INT 21h AH=4Ch passes, and `--verify` returns
# 1 instead if the picture did not survive the trip to the render target.
# So this one number covers both.
if(NOT code EQUAL ${EXPECT_CODE})
  message(FATAL_ERROR
    "${PROGRAM} exits with code ${EXPECT_CODE}, and --verify fails with 1;"
    " the host returned '${code}'.\n${context}")
endif()

if(NOT err MATCHES "verify OK")
  message(FATAL_ERROR "--verify did not pass.\n${context}")
endif()

if(STILLS)
  # The key is at frame 60 and the program exits after answering it, so a
  # cadence of 15 leaves five stills at least: 0, 15, 30, 45, 60.
  file(GLOB _stills "${STILLS}-*.ppm")
  list(LENGTH _stills _still_count)
  if(_still_count LESS 5)
    message(FATAL_ERROR
      "--dump-every 15 wrote ${_still_count} stills over sixty frames."
      "\n${context}")
  endif()

  # Named for the frame `--press KEY@FRAME` counts in, zero-padded so a
  # listing sorts into the order the frames happened in.
  foreach(_frame 000000 000015 000030 000045 000060)
    if(NOT EXISTS "${STILLS}-${_frame}.ppm")
      message(FATAL_ERROR
        "--dump-every skipped frame ${_frame}.\n${context}")
    endif()
  endforeach()

  file(READ "${STILLS}-000060.ppm" _still_header LIMIT 15)
  if(NOT _still_header MATCHES "^P6\n320 200\n255\n")
    message(FATAL_ERROR "a still is not a 320x200 binary PPM.")
  endif()

  # The still of the last frame and `--dump`'s own frame are the same
  # screen under two names — which is what makes a still comparable with
  # the frame a reader already trusts.
  if(NOT EXISTS "${STILLS}.ppm")
    message(FATAL_ERROR
      "--dump-every displaced the final frame.\n${context}")
  endif()
endif()

# Asserted separately from "verify OK" because the host's own failure
# condition is about the picture; these are the numbers underneath it, and
# a run that presented one frame and compared none would be a very quiet
# way to check nothing.
if(NOT err MATCHES "presented ([1-9][0-9]*), checked ([1-9][0-9]*), mismatched pixels 0")
  message(FATAL_ERROR
    "no frame reached the render target intact.\n${context}")
endif()

# The audio thread reached the core at all.
if(NOT err MATCHES "audio callbacks ([1-9][0-9]*), audio samples ([1-9][0-9]*)")
  message(FATAL_ERROR
    "SDL's audio callback never called into the core.\n${context}")
endif()

# And, of the program that plays throughout, that what reached the device
# was a tone rather than a well-plumbed silence. `render()` answering
# silence is a correct answer to most of any run, so this is asked only
# where silence would be wrong. Not of the composite: its tone lasts
# twenty milliseconds and falls inside the first callback, which is the
# one whose window of virtual time the machine has not settled yet — a
# zero there would be a fact about pull timing, not about the speaker.
if(EXPECT_SOUND AND NOT err MATCHES "sounded ([1-9][0-9]*)")
  message(FATAL_ERROR
    "the audio callback ran, and every sample it handed the device was"
    " silence.\n${context}")
endif()

# And that the host can say what the pacing did to it. Both numbers are
# facts about this run and not assertions about their value — an underrun
# at the start of a windowed run is the shape of a healthy one, because
# SDL's device pulls before the machine has settled any virtual time at
# all. What is asserted is that the line exists: `platform.h` states a
# policy for each, and until #106 no host read either counter, so the two
# policies were specified and unreportable (M4-A1).
if(NOT err MATCHES "audio underruns=[0-9]+ resyncs=[0-9]+ dropped edges=0")
  message(FATAL_ERROR
    "the host did not report the timeline's pacing counters, or the edge"
    " ring overflowed.\n${context}")
endif()

# The other half of #106: the edge list itself, as the machine published
# it, which is a claim about the machine and not about the render of it.
#
# The check is exact and it is not a golden. DEMO.EXE programs channel 2
# with divisor 2712 (hosts/sdl/tests/make_demo_disk.cpp), and mode 3
# toggles the output every half count — so every gap between consecutive
# ticks in this file must be exactly 1356, derived from what the program
# asks for the way this file's pixel rule is derived from what integer
# scaling means. A filter change cannot move it and a WAV cannot show it.
#
# Every gap but the *last*. The final edge is the program clearing the
# gate on its way out, which lands wherever the keystroke did and is a
# short gap rather than a half cycle. Checking it would be checking when
# Escape was pressed.
if(EDGES)
  if(NOT EXISTS "${EDGES}.edges")
    message(FATAL_ERROR "--dump wrote no edge list.\n${context}")
  endif()
  file(STRINGS "${EDGES}.edges" _edge_lines)

  set(_previous "")
  set(_pending "")
  set(_edges 0)
  set(_wrong 0)
  foreach(_line IN LISTS _edge_lines)
    if(_line MATCHES "^([0-9]+) ([01])$")
      math(EXPR _edges "${_edges} + 1")
      if(NOT _previous STREQUAL "")
        # Reaching another edge is what makes the gap before it not the
        # last one, so it is checked here rather than where it was
        # measured.
        if(NOT _pending STREQUAL "")
          if(NOT _pending EQUAL 1356)
            math(EXPR _wrong "${_wrong} + 1")
          endif()
        endif()
        math(EXPR _pending "${CMAKE_MATCH_1} - ${_previous}")
      endif()
      set(_previous "${CMAKE_MATCH_1}")
    endif()
  endforeach()

  # A second of virtual time at 440 Hz is 880 transitions; the tone starts
  # a little way into the run, so the floor is what asks "did it play
  # throughout" without pinning where it began.
  if(_edges LESS 800)
    message(FATAL_ERROR
      "the edge list holds ${_edges} edges, which is not a second of a"
      " 440 Hz tone.\n${context}")
  endif()
  if(NOT _wrong EQUAL 0)
    message(FATAL_ERROR
      "${_wrong} gaps in the ${_edges} published edges are not 1356 ticks,"
      " which is what divisor 2712 in mode 3 means.\n${context}")
  endif()

  # The trailer, so that a file cut short is told apart from a quiet run.
  if(NOT _edge_lines MATCHES "# edges ${_edges} dropped 0")
    message(FATAL_ERROR
      "the edge list does not end saying it is all of them.\n${context}")
  endif()
endif()

# Two: the make and the break of one scripted press, each mapped from an
# SDL event by the host's own table.
if(NOT err MATCHES "keys posted 2")
  message(FATAL_ERROR
    "the scripted keystroke did not travel from an SDL event to a posted"
    " scan code.\n${context}")
endif()

# And, where the program answers in print, the machine's own account of
# the same keystroke: the composite echoes what INT 16h gave it and then
# prints its banner, so `aDONE` is the host's scan code 1Eh having become
# the character the program read.
if(EXPECT_CONSOLE AND NOT out MATCHES "${EXPECT_CONSOLE}")
  message(FATAL_ERROR
    "the key the host posted is not the key the program read.\n${context}")
endif()

message(STATUS
  "sdl host verify: ${PROGRAM} drew, sounded and answered a keystroke")
