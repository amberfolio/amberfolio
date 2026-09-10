# SPDX-License-Identifier: AGPL-3.0-only
#
# A recording replays the same on any machine, whatever settings file is
# on it (#382).
#
# The claim is narrow and it is the one that matters: **a `--replay` run
# reads no config at all.** A recording carries its own seams, its own
# speed and its own keys (`docs/replay.md`), and the session library and
# `scripts/sweep.py` verify recordings by exact comparison — so a host
# that let a config on the replaying machine reach the run would make a
# recording's answer a property of the desk it ran at.
#
# What it points the host at is a config naming every setting a config
# can carry, each of them wrong for this recording: a different game
# directory, a different program, three seams the recording never had,
# another speed, another volume, a window scale, and permission to write
# sidecars into a disk that is committed to this repository. The run
# verifies anyway, because none of it is read.

if(NOT HOST OR NOT DISK OR NOT SESSION OR NOT SCRATCH)
  message(FATAL_ERROR
    "run-replay-past-config.cmake needs -DHOST=, -DDISK=, -DSESSION= and"
    " -DSCRATCH=")
endif()

set(config "${SCRATCH}/replay-config/config.txt")
file(MAKE_DIRECTORY "${SCRATCH}/replay-config")
file(WRITE "${config}"
  "amberfolio-config 1\n"
  "game-directory ${SCRATCH}/nowhere\n"
  "program NOTHERE.EXE\n"
  "seam automap\n"
  "seam journal\n"
  "seam encamp-fix\n"
  "journal-ocr none\n"
  "volume 25\n"
  "mute on\n"
  "speed 386\n"
  "scale 5\n"
  "save-sidecars on\n")

execute_process(
  COMMAND "${HOST}" "${DISK}" SPIN.EXE --headless
    --replay "${SESSION}" --config "${config}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 0)
  message(FATAL_ERROR
    "the replay failed with a config on the machine; the host returned"
    " '${code}'.\nstdout: ${out}\nstderr: ${err}")
endif()
if(NOT err MATCHES "replay verified checkpoints=[1-9][0-9]*")
  message(FATAL_ERROR
    "the recording did not verify past a config.\nstderr: ${err}")
endif()

# The reason, kept. A player whose seams did not come on during a replay
# is owed the sentence that says why, and a silent rule is one somebody
# rediscovers by debugging.
if(NOT err MATCHES "config not read for --replay")
  message(FATAL_ERROR
    "the host ignored the config without saying so.\nstderr: ${err}")
endif()

# And nothing the config asked for happened: no seam line, no speed line,
# and the sidecar the config asked for was never written beside the
# committed disk.
if(err MATCHES "amberfolio: seam ")
  message(FATAL_ERROR
    "a config turned a seam on during a replay.\nstderr: ${err}")
endif()
if(err MATCHES "amberfolio: speed ")
  message(FATAL_ERROR
    "a config changed the speed during a replay.\nstderr: ${err}")
endif()
if(EXISTS "${DISK}/SAVE/AFMAP.DAT")
  message(FATAL_ERROR
    "a config's save-sidecars wrote into ${DISK}, which is committed")
endif()

message(STATUS
  "sdl host replay: a recording verified with every setting a config can"
  " carry pointed the other way")
