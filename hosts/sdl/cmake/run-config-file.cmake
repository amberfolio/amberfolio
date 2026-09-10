# SPDX-License-Identifier: AGPL-3.0-only
#
# The desktop config file, end to end (#382).
#
# `desktop_config_test.cpp` checks the format and the precedence rule as
# arithmetic; this checks the thing a player experiences, which is a
# different claim and only a whole host can make it: that a launch with
# nothing to run says what it needs, that `--remember` writes a file, that
# the launch after it needs no arguments at all, that a flag still beats
# the file, and that a file this build cannot read is a loud line and a
# clean start rather than a guess.
#
# `--config` points every one of these at a scratch file, so nothing here
# touches the per-user directory the maintainer's own settings live in —
# and so the case means the same thing on a runner, where there is no
# such directory, as on a desk where there is.

if(NOT HOST OR NOT DISK OR NOT SCRATCH)
  message(FATAL_ERROR
    "run-config-file.cmake needs -DHOST=, -DDISK= and -DSCRATCH=")
endif()

set(config "${SCRATCH}/config-file/config.txt")
file(MAKE_DIRECTORY "${SCRATCH}/config-file")
file(REMOVE "${config}")

# --- 1. A first run says what it needs, and is not an error -----------
#
# The whole of the change to onboarding: no usage block, no failure, and
# a sentence naming the file that would end this.
execute_process(
  COMMAND "${HOST}" --config "${config}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 0)
  message(FATAL_ERROR
    "a first run is not an error; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()
foreach(expected
    "no game directory yet, which is not a problem"
    "point this at a directory"
    "--remember writes ")
  if(NOT err MATCHES "${expected}")
    message(FATAL_ERROR
      "a first run never said '${expected}'.\nstdout: ${out}\nstderr: ${err}")
  endif()
endforeach()
if(EXISTS "${config}")
  message(FATAL_ERROR
    "a first run wrote ${config}; only --remember writes a config")
endif()

# --- 2. A run that is told everything, and asked to remember it -------
execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless
    --config "${config}" --remember
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "the program exits with code 7; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()
if(NOT EXISTS "${config}")
  message(FATAL_ERROR "--remember wrote no config at ${config}")
endif()
file(READ "${config}" kept)
foreach(expected
    "amberfolio-config 1"
    "program HELLO.EXE"
    "speed xt"
    "save-sidecars off")
  if(NOT kept MATCHES "${expected}")
    message(FATAL_ERROR "the config has no '${expected}' line:\n${kept}")
  endif()
endforeach()
# Off for somebody who never chose: a remembered run that named no seam
# must not leave one in the file, or the launch after it would turn on a
# seam nobody asked for (CLAUDE.md's fidelity invariant).
if(kept MATCHES "seam ")
  message(FATAL_ERROR
    "--remember wrote a seam line for a run that named no seam:\n${kept}")
endif()

# --- 3. The second launch needs no arguments --------------------------
execute_process(
  COMMAND "${HOST}" --headless --config "${config}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "a second launch with no arguments did not run the program;"
    " the host returned '${code}'.\nstdout: ${out}\nstderr: ${err}")
endif()
if(NOT out MATCHES "amberfolio host says hello")
  message(FATAL_ERROR
    "a second launch ran something other than the remembered program.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()
if(NOT err MATCHES "config read ")
  message(FATAL_ERROR
    "the host never said which config it read.\nstderr: ${err}")
endif()

# --- 4. flag > config -------------------------------------------------
#
# The same config, and a command line naming the other program on the
# disk. The load line has to be the flag's answer and not the file's.
execute_process(
  COMMAND "${HOST}" "${DISK}" STOPPER.EXE --headless --steps 200
    --config "${config}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT err MATCHES "load STOPPER.EXE")
  message(FATAL_ERROR
    "the config's program beat the command line's.\nstderr: ${err}")
endif()

# --- 5. --no-config ignores it ----------------------------------------
execute_process(
  COMMAND "${HOST}" --headless --no-config
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 0)
  message(FATAL_ERROR
    "--no-config with nothing to run is a first run, not an error;"
    " the host returned '${code}'.\nstderr: ${err}")
endif()
if(NOT err MATCHES "no game directory yet")
  message(FATAL_ERROR
    "--no-config read a config anyway.\nstderr: ${err}")
endif()

# --- 6. A malformed file is a loud line and a clean start -------------
#
# Never a guess (CLAUDE.md's "log, don't fake"), never half-read, and the
# file is left where it is: whatever it is, it is somebody's.
file(WRITE "${config}"
  "amberfolio-config 1\ngame-directory ${DISK}\nprogram HELLO.EXE\nvolume 300\n")
execute_process(
  COMMAND "${HOST}" --headless --config "${config}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 0)
  message(FATAL_ERROR
    "a refused config leaves a first run, not a failure;"
    " the host returned '${code}'.\nstderr: ${err}")
endif()
foreach(expected
    "line 4 - bad-value: volume 300"
    "starting on the defaults"
    "no game directory yet")
  if(NOT err MATCHES "${expected}")
    message(FATAL_ERROR
      "a refused config never said '${expected}'.\nstderr: ${err}")
  endif()
endforeach()
file(READ "${config}" after)
if(NOT after MATCHES "volume 300")
  message(FATAL_ERROR "a refused config was not left where it is:\n${after}")
endif()

# --- 7. --forget-config -----------------------------------------------
execute_process(
  COMMAND "${HOST}" --headless --config "${config}" --forget-config
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

file(READ "${config}" emptied)
if(emptied MATCHES "game-directory")
  message(FATAL_ERROR "--forget-config left a setting behind:\n${emptied}")
endif()
if(NOT err MATCHES "config forgotten ")
  message(FATAL_ERROR "--forget-config said nothing.\nstderr: ${err}")
endif()

message(STATUS
  "sdl host config: a first run, a remembered one, a second launch with"
  " no arguments, a flag that beat the file, and a file that was refused")
