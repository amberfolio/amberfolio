# SPDX-License-Identifier: AGPL-3.0-only
#
# The boot driver's report, on a program that stops on purpose (M3-F1,
# #83).
#
# M3's method is to read the stop line and widen the one thing it names,
# and M3's exit criterion is that the browser reports the same line at the
# same step (#84). Both rest on the line being a *fixed shape*. The
# formatting itself is asserted field by field in
# tests/core/machine/report_test.cpp; this is the other half — that the
# shape survives the trip through a real host, a real directory, a real
# MZ load, and out of the process on stderr where a person or a script
# reads it.
#
# Three runs, because the three things #83 adds are independent of each
# other and a single run would only prove whichever happened first:
#
#   1. the report on a refusal, including the fingerprint printed at load
#      and the `next=` worklist line;
#   2. the report on a step budget, which is the shape a *hang* takes —
#      the failure this host previously could not report at all;
#   3. the dump, which is the file a person looks at when "the title
#      renders" is the claim being made;
#   4. and `--dump-every`'s refusal when `--dump` gave it no prefix
#      (M4-G1, #102).

if(NOT HOST OR NOT DISK OR NOT OUT)
  message(FATAL_ERROR
    "run-stop-report.cmake needs -DHOST=, -DDISK= and -DOUT=")
endif()

# --- 1. The refusal ----------------------------------------------------

execute_process(
  COMMAND "${HOST}" "${DISK}" STOPPER.EXE --headless -- TAIL
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

set(context "exit: ${code}\nstdout: ${out}\nstderr: ${err}")

# A run that ended without the program choosing an exit code is a failed
# run, whatever ended it. That is the host's own rule and it is what makes
# `ctest` able to tell a boot that refused something from a boot that
# finished.
if(code EQUAL 0)
  message(FATAL_ERROR
    "a program that stopped on an unbacked vector exited 0.\n${context}")
endif()

# The identity of the file, printed before anything ran. Sixty-four
# lowercase hex characters, matched as such rather than pinned to a value:
# the generator next door may legitimately change what it writes, and what
# is being checked here is that a fingerprint was taken and printed at
# all.
if(NOT err MATCHES "amberfolio: load STOPPER\\.EXE sha256=[0-9a-f][0-9a-f]+")
  message(FATAL_ERROR
    "the program's SHA-256 was not printed at load.\n${context}")
endif()

# The command tail reached the loader. Five characters: the separator
# space DOS leaves in front of a tail, then `TAIL`. Spelled out rather
# than computed, so that a change to what this test passes has to be
# noticed here too.
if(NOT err MATCHES "amberfolio: load psp=[0-9A-F]+ .*tail=5")
  message(FATAL_ERROR
    "the command tail did not reach the loader.\n${context}")
endif()

# The headline, whole. Every field #81 asked for, in the order the format
# fixes them in.
if(NOT err MATCHES
   "amberfolio: stop reason=unimplemented_service steps=[0-9]+ ticks=[0-9]+ frames=[0-9]+ cs=[0-9A-F][0-9A-F][0-9A-F][0-9A-F] ip=[0-9A-F][0-9A-F][0-9A-F][0-9A-F] at=[0-9A-F]+")
  message(FATAL_ERROR
    "the stop report's headline is not the fixed shape.\n${context}")
endif()

# The service alongside it, with the caller read off its own stack.
if(NOT err MATCHES
   "amberfolio: stop call=INT63 ah=77 al=00 ax=7700 from=[0-9A-F]+:[0-9A-F]+ outcome=unimplemented")
  message(FATAL_ERROR
    "the stop report did not name the service that was refused.\n${context}")
endif()

# And the worklist line: the one thing to widen next, named by the machine
# rather than inferred by whoever is reading.
if(NOT err MATCHES "amberfolio: stop next=INT 63h AH=77h AL=00h")
  message(FATAL_ERROR
    "the stop report did not name what to widen next.\n${context}")
endif()

# --- 2. The hang, bounded ----------------------------------------------
#
# HELLO.EXE is six steps long from entry to its AH=4Ch, so three is
# comfortably inside it: the machine is still running, nothing has refused
# anything, and the run ends because this host said so. That is the shape
# a real hang takes, produced deterministically and in six steps.

execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless --steps 3 --trace
  RESULT_VARIABLE budget_code
  OUTPUT_VARIABLE budget_out
  ERROR_VARIABLE budget_err)

set(budget_context
  "exit: ${budget_code}\nstdout: ${budget_out}\nstderr: ${budget_err}")

if(budget_code EQUAL 0)
  message(FATAL_ERROR
    "a run cut short by --steps exited 0.\n${budget_context}")
endif()

# Exactly three, not "about three": the budget is clamped into the run
# slice precisely so that a stop can be reproduced to the step, and a
# report that overshot by part of a frame would make that false.
if(NOT budget_err MATCHES
   "amberfolio: stop reason=step_budget steps=3 ticks=[0-9]+ frames=[0-9]+ cs=[0-9A-F]+ ip=[0-9A-F]+")
  message(FATAL_ERROR
    "--steps 3 did not end the run at step 3 with a CS:IP."
    "\n${budget_context}")
endif()

# And the trace ring, which is what makes a bounded hang answerable: not
# only where the program was, but how it got there.
if(NOT budget_err MATCHES "amberfolio: stop trace=on steps_seen=3 kept=3")
  message(FATAL_ERROR "--trace produced no ring.\n${budget_context}")
endif()
if(NOT budget_err MATCHES "amberfolio: stop trace step=0 at=[0-9A-F]+:[0-9A-F]+")
  message(FATAL_ERROR
    "the trace ring listed no steps.\n${budget_context}")
endif()

# --- 3. The dump --------------------------------------------------------

file(REMOVE "${OUT}.ppm")

execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless --dump "${OUT}"
  RESULT_VARIABLE dump_code
  OUTPUT_VARIABLE dump_out
  ERROR_VARIABLE dump_err)

set(dump_context
  "exit: ${dump_code}\nstdout: ${dump_out}\nstderr: ${dump_err}")

# HELLO.EXE exits with 7 of its own accord, so this run also checks that
# `--dump` changes nothing about how a run ends.
if(NOT dump_code EQUAL 7)
  message(FATAL_ERROR
    "--dump changed the exit code the program chose.\n${dump_context}")
endif()

if(NOT EXISTS "${OUT}.ppm")
  message(FATAL_ERROR "--dump wrote no frame.\n${dump_context}")
endif()

# A binary PPM of the one video mode this machine has: the header is the
# only part a script can check without a decoder, and it is the part that
# would be wrong if the writer were.
file(READ "${OUT}.ppm" _header LIMIT 15)
if(NOT _header MATCHES "^P6\n320 200\n255\n")
  message(FATAL_ERROR "the dumped frame is not a 320x200 binary PPM.")
endif()

file(SIZE "${OUT}.ppm" _size)
if(NOT _size EQUAL 192015)
  message(FATAL_ERROR
    "the dumped frame is ${_size} bytes; 320*200*3 plus a 15-byte header "
    "is 192015.")
endif()

# --- 4. --dump-every is refused when there is nowhere to put them -------
#
# The stills themselves are checked where a run lasts long enough to
# have more than one frame — run-verify-program.cmake, whose program
# plays for a virtual second. What belongs here is the argument check,
# because it is the same "say why rather than silently do nothing" rule
# the two above it are made of.

execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless --dump-every 60
  RESULT_VARIABLE lonely_code
  ERROR_VARIABLE lonely_err)

if(lonely_code EQUAL 0)
  message(FATAL_ERROR
    "--dump-every without --dump was accepted.\nstderr: ${lonely_err}")
endif()
if(NOT lonely_err MATCHES "amberfolio: --dump-every needs --dump")
  message(FATAL_ERROR
    "--dump-every without --dump did not say why.\nstderr: ${lonely_err}")
endif()

message(STATUS
  "sdl host stop report: refusal named, budget bounded, frames dumped")
