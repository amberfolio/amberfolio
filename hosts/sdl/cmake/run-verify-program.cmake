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
execute_process(
  COMMAND "${HOST}" "${DISK}" "${PROGRAM}" --scale 2 --verify --press "${PRESS}"
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
