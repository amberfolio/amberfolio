# SPDX-License-Identifier: AGPL-3.0-only
#
# Presenting a document to the SDL host (M5-D3, #171).
#
# PLAN.md §5 gates two enhancements on a document the player holds, and
# the presenting side is `--document PATH`. What this drives is the whole
# of that path against a real file on a real disk: read, hashed, and
# answered — recognized or not.
#
# **The file is this test's own three bytes.** A check that needed a real
# code wheel would be a check nobody without the document could run, and
# nothing from a player's document may enter this tree in any case
# (PLAN.md §6). So what is asserted is the *mechanism*: that the digest
# comes back, that it is the digest those three bytes have, and that an
# edition nobody fingerprinted is reported rather than guessed at.
#
# "abc" hashes to FIPS 180-4 appendix B.1's digest, which is the same
# answer every other SHA-256 in the world gives.

if(NOT HOST OR NOT DISK OR NOT SCRATCH)
  message(FATAL_ERROR
    "run-document-gate.cmake needs -DHOST=, -DDISK= and -DSCRATCH=")
endif()

set(document "${SCRATCH}/not-a-document.bin")
file(WRITE "${document}" "abc")

execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless --document "${document}"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "the program exits with code 7; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

set(expected
  "document unrecognized sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
if(NOT err MATCHES "${expected}")
  message(FATAL_ERROR
    "the host did not report an unrecognized document with its fingerprint.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

# And a file that is not there is a different sentence: "I could not read
# this" and "I do not recognize this" are two findings, and only one of
# them is about the table.
execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless
    --document "${SCRATCH}/nothing-is-here.bin"
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)
if(NOT err MATCHES "document .* could not be read")
  message(FATAL_ERROR
    "a document that is not there was not reported as unreadable.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

message(STATUS
  "sdl host document: an unrecognized document was reported with its"
  " fingerprint, and an unreadable one apart from it")
