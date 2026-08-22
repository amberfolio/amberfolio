# SPDX-License-Identifier: AGPL-3.0-only
#
# Records a run of the SDL host and then replays it, which is the whole
# of M4-R1's claim reduced to one CTest case: a run is *keys, ticks and
# hashes* (machine/replay.h), and a second machine handed those three
# things reproduces the first exactly.
#
# Two runs of the same program over two copies of the same disk. The
# first is given `--record` and a scripted keystroke; the second is given
# `--replay` and nothing else — no key, no speed, no seam, because the
# recording names all three and the host refuses to be told them twice.
# If the second machine differs anywhere the recording describes, at any
# checkpoint, the host says which line and which section and exits 1.
#
# Two copies of the disk and not one, because the program writes to it:
# a replay is a run of the same *initial* conditions, and the file
# manifest in the preamble is checked against the disk before a step is
# taken. Replaying over the disk the recorded run left behind would fail
# on that check, correctly, and prove nothing about the machine.
#
# Windowed, under the same `dummy` drivers as run-verify-program.cmake —
# see that file for why `dummy` and not `offscreen`. Windowed and not
# `--headless` because the key is the point: a scripted press becomes an
# SDL event, becomes an XT scan code, becomes a `key` line in the
# recording, and comes back out of the player at the same tick. Headless
# has no event queue and so no key to record.

if(NOT HOST OR NOT DISK OR NOT PROGRAM OR NOT WORK OR NOT DEFINED EXPECT_CODE)
  message(FATAL_ERROR
    "run-record-replay.cmake needs -DHOST=, -DDISK=, -DPROGRAM=, -DWORK="
    " and -DEXPECT_CODE=")
endif()

set(ENV{SDL_VIDEODRIVER} "dummy")
set(ENV{SDL_AUDIODRIVER} "dummy")
set(ENV{SDL_RENDER_DRIVER} "software")

set(recording "${WORK}/session.rec")
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
file(COPY "${DISK}/" DESTINATION "${WORK}/record")
file(COPY "${DISK}/" DESTINATION "${WORK}/replay")

# `--fast max` for the same reason `--headless` never sleeps: there is
# nobody watching, and the pacing this would otherwise do is wall time
# spent proving nothing. It changes no tick and no step (see main.cpp's
# note on `--fast`), which is exactly why a recording made under it
# replays under it.
execute_process(
  COMMAND "${HOST}" "${WORK}/record" "${PROGRAM}" --fast max
          --press "${PRESS}" --record "${recording}"
  RESULT_VARIABLE record_code
  OUTPUT_VARIABLE record_out
  ERROR_VARIABLE record_err)

set(context "record exit: ${record_code}\n${record_err}")

if(NOT record_code EQUAL ${EXPECT_CODE})
  message(FATAL_ERROR
    "the recorded run should exit ${EXPECT_CODE}; it returned"
    " '${record_code}'.\n${context}")
endif()

if(NOT EXISTS "${recording}")
  message(FATAL_ERROR "--record wrote no file.\n${context}")
endif()

# What a recording is, asserted on the file itself rather than trusted
# from the fact that a replay of it passed — a player and a recorder that
# agreed on the wrong format would agree with each other all day.
file(READ "${recording}" text)

if(NOT text MATCHES "^amberfolio-recording 1 state=1\n")
  message(FATAL_ERROR "the recording does not begin with its header.\n${context}")
endif()

if(NOT text MATCHES "\nprogram ${PROGRAM} [0-9a-f][0-9a-f]+\n")
  message(FATAL_ERROR
    "the recording does not name the program and its fingerprint.\n${context}")
endif()

# The make and the break of the scripted press, at a tick.
if(NOT text MATCHES "\nkey [0-9]+ [0-9a-f][0-9a-f] down\n")
  message(FATAL_ERROR "the keystroke is not in the recording.\n${context}")
endif()

if(NOT text MATCHES "\ncheckpoint [0-9]+ [0-9]+ [0-9a-f]+ .*ram=")
  message(FATAL_ERROR
    "the recording carries no checkpoint with its sections.\n${context}")
endif()

# The last checkpoint of a run that ended by the program exiting is of a
# machine that had stopped, and says so. That marker is what lets a
# player run the machine *past* the checkpoint's tick to reach it —
# stopping happens inside a step and spends neither the step nor its
# ticks — so a recording of an exiting program that lacks it would replay
# against a machine one step short of stopping.
if(NOT text MATCHES "\ncheckpoint [0-9]+ [0-9]+ [0-9a-f]+ stopped")
  message(FATAL_ERROR
    "the run ended in the program's own exit, so its last checkpoint"
    " should be marked stopped.\n${context}")
endif()

if(NOT text MATCHES "\nend [0-9]+ [0-9]+\n$")
  message(FATAL_ERROR "the recording has no end line.\n${context}")
endif()

# And nothing that is not a fact about the run. A recording is committable
# precisely because it holds no byte of a program and no pixel of a screen
# (PLAN.md §6), and the cheapest way to keep that true is to refuse any
# line whose first word is not one of the nine the grammar has.
string(REGEX MATCHALL "[^\n]+" lines "${text}")
foreach(line IN LISTS lines)
  if(NOT line MATCHES
     "^(amberfolio-recording|program|tail|speed|seam|file|wall|key|checkpoint|end)( .*)?$")
    message(FATAL_ERROR "the recording has a line that is not one: '${line}'")
  endif()
endforeach()

# Then the run that has to be the same one. No --press: the recording's
# keys are the run's keys.
execute_process(
  COMMAND "${HOST}" "${WORK}/replay" "${PROGRAM}" --fast max
          --replay "${recording}"
  RESULT_VARIABLE replay_code
  OUTPUT_VARIABLE replay_out
  ERROR_VARIABLE replay_err)

set(context "${context}\nreplay exit: ${replay_code}\n${replay_err}")

if(NOT replay_err MATCHES "replay verified checkpoints=([1-9][0-9]*) keys=2")
  message(FATAL_ERROR
    "the replay did not verify every checkpoint and both halves of the"
    " keystroke.\n${context}")
endif()

# The program's own code, and not the host's: a replay that verified gets
# out of the way and lets the program answer, the same way a passing
# `--verify` does.
if(NOT replay_code EQUAL ${EXPECT_CODE})
  message(FATAL_ERROR
    "a verified replay should exit ${EXPECT_CODE} as the recorded run"
    " did; it returned '${replay_code}'.\n${context}")
endif()

# And the check has to be able to fail. A recording whose last checkpoint
# claims a state the machine will not be in must be refused — otherwise
# every assertion above is a test of a function that always says yes.
file(READ "${recording}" text)
string(REGEX REPLACE "(\ncheckpoint [0-9]+ [0-9]+ )[0-9a-f]+" "\\10000000000000000000000000000000000000000000000000000000000000000"
       tampered "${text}")
file(WRITE "${WORK}/tampered.rec" "${tampered}")
file(REMOVE_RECURSE "${WORK}/replay")
file(COPY "${DISK}/" DESTINATION "${WORK}/replay")

execute_process(
  COMMAND "${HOST}" "${WORK}/replay" "${PROGRAM}" --fast max
          --replay "${WORK}/tampered.rec"
  RESULT_VARIABLE tampered_code
  ERROR_VARIABLE tampered_err)

if(tampered_code EQUAL 0 OR NOT tampered_err MATCHES "replay diverged")
  message(FATAL_ERROR
    "a recording with a wrong checkpoint hash was accepted.\n"
    "exit: ${tampered_code}\n${tampered_err}")
endif()

message(STATUS
  "sdl host replay: ${PROGRAM} recorded and reproduced tick for tick")
