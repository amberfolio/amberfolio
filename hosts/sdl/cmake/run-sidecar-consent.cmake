# SPDX-License-Identifier: AGPL-3.0-only
#
# The one question this host asks a person, end to end (#385).
#
# `sidecar_consent_test.cpp` checks the three decisions as arithmetic:
# whether a launch is one to ask in, what the question says, and what an
# answer to it was. None of those is the claim a player cares about,
# which is that answering `y` once means never being asked again — and
# only a whole host can make that claim, because it is about a file
# written between two launches.
#
# What is driven here is stdin. `execute_process(INPUT_FILE ...)` can
# feed it, and that works *because* nothing in this path asks `isatty`:
# what decides whether a person is asked is the shape of the run — a
# headless one, a driven one, a recording, a dump, a verification — and
# never whether a terminal happens to be attached. A guard on the
# terminal would be untestable here and wrong anyway, since a script that
# inherited one is still not a person.
#
# `--config` points every case at a scratch file, so nothing here reads
# or writes the settings a person's own launches use.

if(NOT HOST OR NOT DISK OR NOT SCRATCH)
  message(FATAL_ERROR
    "run-sidecar-consent.cmake needs -DHOST=, -DDISK= and -DSCRATCH=")
endif()

set(work "${SCRATCH}/sidecar-consent")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")

# The three answers, as files something can read from stdin.
file(WRITE "${work}/yes.txt" "y\n")
file(WRITE "${work}/nothing.txt" "\n")

# A window, a renderer and an audio device that need no display and no
# sound card - run-verify-program.cmake argues the choice of drivers, and
# this needs them for the same reason: what is under test is a launch
# that is *not* headless, because a headless one is never asked.
set(ENV{SDL_VIDEODRIVER} "dummy")
set(ENV{SDL_AUDIODRIVER} "dummy")
set(ENV{SDL_RENDER_DRIVER} "software")

# What the question says, as the two things a player acts on: the name of
# a file they can go and delete, and the promise that nothing appears
# until there is something to put in it.
set(question "may this build keep your progress")

function(run_host)
  cmake_parse_arguments(RUN "" "STDIN" "ARGS" ${ARGN})
  if(RUN_STDIN)
    execute_process(
      COMMAND "${HOST}" "${DISK}" HELLO.EXE ${RUN_ARGS}
      INPUT_FILE "${RUN_STDIN}"
      RESULT_VARIABLE code
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  else()
    execute_process(
      COMMAND "${HOST}" "${DISK}" HELLO.EXE ${RUN_ARGS}
      RESULT_VARIABLE code
      OUTPUT_VARIABLE out
      ERROR_VARIABLE err)
  endif()
  set(code "${code}" PARENT_SCOPE)
  set(out "${out}" PARENT_SCOPE)
  set(err "${err}" PARENT_SCOPE)
endfunction()

# --- 1. A person is asked, and the answer is written down -------------
#
# Without --remember, which is the one exception this host makes to
# #382's rule and the reason it is worth a case of its own: a question
# somebody was asked in so many words is not a driving script's flag, and
# "asked once" cannot be delivered without writing the answer somewhere.
#
# The file is seeded with a volume nobody would pick by accident and the
# command line names another one. What must come back is the *seeded*
# volume beside the new answer: the write-back is what the file already
# said plus one key, and never this run's settings.
set(config "${work}/asked.txt")
file(WRITE "${config}" "amberfolio-config 1\nvolume 33\n")
run_host(ARGS --fast max --config "${config}" --volume 90 STDIN "${work}/yes.txt")

if(NOT err MATCHES "${question}")
  message(FATAL_ERROR
    "a launch with a person in it was never asked.\nstderr: ${err}")
endif()
foreach(expected "AFMAP.DAT" "AFSEEN.DAT" "until there is something to put")
  if(NOT err MATCHES "${expected}")
    message(FATAL_ERROR
      "the question never said '${expected}'.\nstderr: ${err}")
  endif()
endforeach()
file(READ "${config}" answered)
if(NOT answered MATCHES "save-sidecars on")
  message(FATAL_ERROR "a yes was not written down:\n${answered}")
endif()
if(NOT answered MATCHES "volume 33")
  message(FATAL_ERROR
    "the answer was written with this run's settings rather than beside"
    " what the file already said:\n${answered}")
endif()

# --- 2. And that is the "once" in "asked once" ------------------------
#
# The same config, a second launch, and a stdin holding nothing at all:
# if this one asked, it would get silence and the case would still pass
# for the wrong reason. So what is asserted is that the question was not
# printed.
run_host(ARGS --fast max --config "${config}")
if(err MATCHES "${question}")
  message(FATAL_ERROR
    "a config that already answers this was asked again.\nstderr: ${err}")
endif()
if(NOT err MATCHES "save-sidecars writes=")
  message(FATAL_ERROR
    "the remembered answer did not reach the run.\nstderr: ${err}")
endif()

# And with the store on and nothing accumulated, no file of ours appeared
# on the disk: #385's second trap, at the only altitude that can see it.
# A run of a program that explored nothing used to be enough to put
# header-only files in somebody's directory, and the sentence the
# question above prints promises that it does not.
#
# The names rather than the directory, because `SAVE\` on this disk is
# `run-vfs-door.cmake`'s and is nothing to do with this.
file(GLOB ours "${DISK}/SAVE/AF*.DAT")
if(ours)
  message(FATAL_ERROR
    "the store on with nothing to keep still wrote ${ours}")
endif()

# --- 3. Silence is neither a yes nor a no -----------------------------
#
# An empty line, which is also what a closed stdin gives. Nothing is
# written into the player's directory, nothing is written into the
# config, and the next person at this keyboard is asked again - reading
# silence as consent would put a file somewhere on the strength of a
# keypress that never happened, and reading it as a refusal would write
# down an answer nobody gave.
set(quiet "${work}/quiet.txt")
run_host(ARGS --fast max --config "${quiet}" STDIN "${work}/nothing.txt")

if(NOT err MATCHES "${question}")
  message(FATAL_ERROR "the question was not asked.\nstderr: ${err}")
endif()
if(NOT err MATCHES "no answer, so nothing is written")
  message(FATAL_ERROR
    "an unanswered question said nothing about itself.\nstderr: ${err}")
endif()
if(EXISTS "${quiet}")
  message(FATAL_ERROR
    "an unanswered question wrote ${quiet}; silence is not an answer")
endif()
if(err MATCHES "save-sidecars writes=")
  message(FATAL_ERROR
    "an unanswered question turned the sidecars on.\nstderr: ${err}")
endif()

run_host(ARGS --fast max --config "${quiet}" STDIN "${work}/nothing.txt")
if(NOT err MATCHES "${question}")
  message(FATAL_ERROR
    "a question nobody answered was not asked again.\nstderr: ${err}")
endif()

# --- 4. A driven run is never asked anything --------------------------
#
# The case with teeth, and the reason the guard is the shape of the run.
# A prompt in a run nobody is watching never comes back; worse, a sidecar
# written by a verification run makes the disk every recorded session
# pins a different disk, which scripts/sweep.py answers by skipping the
# session and naming it rather than failing. A wrong answer here turns
# the whole session library into skips without anything going red.
#
# Fed a `y` on stdin on purpose: what must happen is that nobody reads
# it.
set(driven "${work}/driven.txt")
run_host(ARGS --headless --config "${driven}" STDIN "${work}/yes.txt")

if(err MATCHES "${question}")
  message(FATAL_ERROR "a headless run was asked a question.\nstderr: ${err}")
endif()
if(EXISTS "${driven}")
  message(FATAL_ERROR "a headless run wrote ${driven}")
endif()
if(NOT out MATCHES "amberfolio host says hello")
  message(FATAL_ERROR
    "the driven run did not run the program.\nstdout: ${out}\n"
    "stderr: ${err}")
endif()

# --- 5. A flag is an answer, for this run, and is never overruled ------
#
# `--no-save-sidecars` against the config from case 1, which says on. The
# run keeps them off, is not asked, and the file is left exactly as it
# was: a flag answers for one launch and does not become what a player
# chose.
run_host(ARGS --fast max --config "${config}" --no-save-sidecars)
if(err MATCHES "${question}")
  message(FATAL_ERROR
    "a launch that named the flag was asked anyway.\nstderr: ${err}")
endif()
file(READ "${config}" after_flag)
if(NOT after_flag MATCHES "save-sidecars on")
  message(FATAL_ERROR
    "--no-save-sidecars rewrote what the player chose:\n${after_flag}")
endif()

message(STATUS
  "sdl host sidecar consent: a person asked once and remembered, silence"
  " that wrote nothing and asked again, a driven run asked nothing, and a"
  " flag that beat the file")
