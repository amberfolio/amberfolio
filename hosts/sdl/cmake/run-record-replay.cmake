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

if(NOT HOST OR NOT DISK OR NOT PROGRAM OR NOT WORK OR NOT DEFINED EXPECT_CODE
   OR NOT RECORD_EVERY OR NOT RECORD_UNTIL)
  message(FATAL_ERROR
    "run-record-replay.cmake needs -DHOST=, -DDISK=, -DPROGRAM=, -DWORK=,"
    " -DEXPECT_CODE=, -DRECORD_EVERY= and -DRECORD_UNTIL=")
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

if(NOT text MATCHES "^amberfolio-recording 2 state=1\n")
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
# line whose first word is not one of the ten the grammar has.
string(REGEX MATCHALL "[^\n]+" lines "${text}")
foreach(line IN LISTS lines)
  if(NOT line MATCHES
     "^(amberfolio-recording|program|tail|speed|seam|file|dir|wall|key|checkpoint|end)( .*)?$")
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

# --- And the same run, written down less often (#101) ------------------
#
# `--record-every N` is what makes a recording of a game-length run
# affordable to make and small enough to commit: a checkpoint hashes
# every byte of RAM, so one a frame is most of what recording costs at
# both ends. What has to stay true when the cadence is sparse is the
# thing this leg asserts — that it changes *what is written down* and not
# what happened, and that it does not thin out the moments a recording
# exists to pin.
#
# ${RECORD_EVERY} is chosen not to divide the frame the scripted press
# lands on, so the checkpoint beside that key is there because a key was
# posted and for no other reason.

set(sparse "${WORK}/sparse.rec")
file(REMOVE_RECURSE "${WORK}/record")
file(COPY "${DISK}/" DESTINATION "${WORK}/record")

execute_process(
  COMMAND "${HOST}" "${WORK}/record" "${PROGRAM}" --fast max
          --press "${PRESS}" --record "${sparse}"
          --record-every "${RECORD_EVERY}"
  RESULT_VARIABLE sparse_code
  ERROR_VARIABLE sparse_err)

set(context "sparse exit: ${sparse_code}\n${sparse_err}")

if(NOT sparse_code EQUAL ${EXPECT_CODE})
  message(FATAL_ERROR
    "the sparsely-recorded run should exit ${EXPECT_CODE}; it returned"
    " '${sparse_code}'.\n${context}")
endif()

file(READ "${sparse}" sparse_text)

# The same run. A cadence that moved a tick would not be a cadence, it
# would be a different machine — and `end` names the tick and the step
# count the run finished on, so two runs that agree there agree about
# everything the recording is a record of.
string(REGEX MATCH "\nend [0-9]+ [0-9]+" dense_end "${text}")
string(REGEX MATCH "\nend [0-9]+ [0-9]+" sparse_end "${sparse_text}")
if(NOT dense_end STREQUAL sparse_end)
  message(FATAL_ERROR
    "the cadence changed the run: '${dense_end}' against"
    " '${sparse_end}'.\n${context}")
endif()

# Fewer lines, which is the point of the option.
string(REGEX MATCHALL "\ncheckpoint " dense_marks "${text}")
string(REGEX MATCHALL "\ncheckpoint " sparse_marks "${sparse_text}")
list(LENGTH dense_marks dense_count)
list(LENGTH sparse_marks sparse_count)
if(NOT sparse_count LESS dense_count)
  message(FATAL_ERROR
    "--record-every ${RECORD_EVERY} wrote ${sparse_count} checkpoints"
    " where one a frame wrote ${dense_count}.\n${context}")
endif()

# The frame that posted a key is checkpointed whatever the cadence says.
# The two halves of the press are posted and recorded in one frame, so
# the frame's checkpoint is the very next line after the break — and this
# frame is not one the cadence would have taken.
if(NOT sparse_text MATCHES "\nkey [0-9]+ [0-9a-f]+ up\ncheckpoint ")
  message(FATAL_ERROR
    "a sparse cadence dropped the checkpoint beside the"
    " keystroke.\n${context}")
endif()

# So is the frame the run ends on, and that one has to carry `stopped`:
# without it a replaying host cannot run past the tick to arrive at a
# machine that stopped inside a step (machine/replay.h).
if(NOT sparse_text MATCHES "\ncheckpoint [0-9]+ [0-9]+ [0-9a-f]+ stopped")
  message(FATAL_ERROR
    "a sparse cadence dropped the last checkpoint, or its stopped"
    " marker.\n${context}")
endif()

# And it still has to be a recording of the run.
file(REMOVE_RECURSE "${WORK}/replay")
file(COPY "${DISK}/" DESTINATION "${WORK}/replay")

execute_process(
  COMMAND "${HOST}" "${WORK}/replay" "${PROGRAM}" --fast max
          --replay "${sparse}"
  RESULT_VARIABLE sparse_replay_code
  ERROR_VARIABLE sparse_replay_err)

if(NOT sparse_replay_err MATCHES "replay verified checkpoints=([1-9][0-9]*) keys=2")
  message(FATAL_ERROR
    "the sparse recording did not verify.\n${context}\nreplay exit:"
    " ${sparse_replay_code}\n${sparse_replay_err}")
endif()

# --- A run that a budget ends, which is the shape a session has --------
#
# The rule above has a second half that the run before it cannot show:
# there, the frame the run ended on was also the frame that posted the
# key, so the checkpoint was there either way. A run ended by `--until`
# has no key and stops wherever the budget falls — mid-frame, at a tick no
# cadence would have chosen — which is exactly the shape of a recorded
# leg of a game (`docs/playable.md`), and the case where dropping the
# last checkpoint would mean the recording never pinned where the run
# actually got to.

set(budgeted "${WORK}/budgeted.rec")
file(REMOVE_RECURSE "${WORK}/record")
file(COPY "${DISK}/" DESTINATION "${WORK}/record")

execute_process(
  COMMAND "${HOST}" "${WORK}/record" "${PROGRAM}" --fast max
          --record "${budgeted}" --record-every "${RECORD_EVERY}"
          --until "${RECORD_UNTIL}"
  RESULT_VARIABLE budgeted_code
  ERROR_VARIABLE budgeted_err)

file(READ "${budgeted}" budgeted_text)
set(context "budgeted exit: ${budgeted_code}\n${budgeted_err}")

# The last checkpoint is the tick the run reached, and it is not a
# stopped one: the machine was still running when the budget took it
# away. `end` names that tick too, and the two agreeing is the whole
# rule — the frame a run ends on is checkpointed whatever the cadence
# says.
string(REGEX MATCHALL "\ncheckpoint [0-9]+" budgeted_marks "${budgeted_text}")
list(POP_BACK budgeted_marks budgeted_last)
string(REGEX MATCH "[0-9]+" budgeted_last_tick "${budgeted_last}")
string(REGEX MATCH "\nend ([0-9]+) " budgeted_end "${budgeted_text}")
if(NOT budgeted_end MATCHES "\nend ${budgeted_last_tick} ")
  message(FATAL_ERROR
    "the run ended at '${budgeted_end}' and its last checkpoint is at"
    " ${budgeted_last_tick}.\n${context}")
endif()

if(budgeted_text MATCHES "\ncheckpoint [0-9]+ [0-9]+ [0-9a-f]+ stopped")
  message(FATAL_ERROR
    "a run a budget took away has not stopped, and no checkpoint of it"
    " should say it had.\n${context}")
endif()

file(REMOVE_RECURSE "${WORK}/replay")
file(COPY "${DISK}/" DESTINATION "${WORK}/replay")

execute_process(
  COMMAND "${HOST}" "${WORK}/replay" "${PROGRAM}" --fast max
          --replay "${budgeted}"
  RESULT_VARIABLE budgeted_replay_code
  ERROR_VARIABLE budgeted_replay_err)

if(NOT budgeted_replay_err MATCHES "replay verified checkpoints=([1-9][0-9]*) keys=0")
  message(FATAL_ERROR
    "the budget-ended recording did not verify.\n${context}\nreplay"
    " exit: ${budgeted_replay_code}\n${budgeted_replay_err}")
endif()

message(STATUS
  "sdl host replay: ${PROGRAM} recorded and reproduced tick for tick,"
  " at one checkpoint a frame (${dense_count}) and at one every"
  " ${RECORD_EVERY} (${sparse_count})")
