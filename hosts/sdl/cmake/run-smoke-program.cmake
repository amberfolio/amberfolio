# SPDX-License-Identifier: AGPL-3.0-only
#
# Runs the SDL host headless over the disk make_smoke_disk built, and
# checks both halves of what a successful run means: the string the
# program printed through INT 21h, and the exit code the program chose.
#
# A script rather than a bare add_test because the program exits with 7 on
# purpose. CTest can assert "zero" or "not zero" and nothing in between,
# and "the program's own exit code reached the process" is exactly the
# claim worth making here — it is the last link in the chain from AH=4Ch
# through machine::exit_program to main()'s return.

if(NOT HOST OR NOT DISK)
  message(FATAL_ERROR "run-smoke-program.cmake needs -DHOST= and -DDISK=")
endif()

execute_process(
  COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless
  RESULT_VARIABLE code
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err)

if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "the program exits with code 7; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

if(NOT out MATCHES "amberfolio host says hello")
  message(FATAL_ERROR
    "the program's console output did not reach stdout.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()

message(STATUS "sdl host smoke: program printed and exited 7")
