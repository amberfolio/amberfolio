# SPDX-License-Identifier: AGPL-3.0-only
#
# Ingesting a journal on the SDL host (M5-E3, #174).
#
# What this drives is the whole of `--journal` against real files on a
# real disk: read, hashed, looked up, followed to each entry's stream,
# inflated, cropped, read by an engine, and written into a text store
# that is then read back and added to.
#
# **The document is one this project generates.** `known_journals()` is
# empty and no real edition may ever be in this tree (PLAN.md §6), so the
# artifact is `host/journal_probe.h`'s synthetic PDF, written out by
# amberfolio-sdl-journal-probe, and the host is asked for it with
# `--journal-probe`. The fixture engine that comes with it answers for
# exactly the probe's pixels and refuses anything else, so a store with
# the probe's words in it is evidence that the offset, the filter, the
# predictor and the crop were all right — which is the claim worth
# making, and the only one available without a real document.
#
# The unrecognized path is checked twice over, because it is the path
# every real journal takes today: a file that is not a journal at all,
# and the probe itself against the *shipped* table, which must not know
# it.

if(NOT HOST OR NOT DISK OR NOT SCRATCH OR NOT PROBE)
  message(FATAL_ERROR
    "run-journal.cmake needs -DHOST=, -DDISK=, -DSCRATCH= and -DPROBE=")
endif()

set(document "${SCRATCH}/journal-probe.pdf")
set(store "${SCRATCH}/journal-store.txt")
file(REMOVE "${store}")

execute_process(COMMAND "${PROBE}" "${document}" RESULT_VARIABLE code)
if(NOT code EQUAL 0)
  message(FATAL_ERROR "the probe document was not written (${code})")
endif()

function(run_host)
  execute_process(
    COMMAND "${HOST}" "${DISK}" HELLO.EXE --headless ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  set(code "${code}" PARENT_SCOPE)
  set(out "${out}" PARENT_SCOPE)
  set(err "${err}" PARENT_SCOPE)
endfunction()

function(expect what)
  if(NOT err MATCHES "${what}")
    message(FATAL_ERROR
      "the host did not say '${what}'.\nstdout: ${out}\nstderr: ${err}")
  endif()
endfunction()

# --- 1. The whole pipeline, on a document this project made ------------

run_host(--journal "${document}" --journal-probe --journal-store "${store}")
if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "the program exits with code 7; the host returned '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()
expect("journal Amber Folio journal probe \\(synthetic\\) entries=4")
expect("journal engine amberfolio journal probe fixture")
expect("journal entries=4 extracted=4 recognized=4")
expect("journal store .*entries=4 corrections=0 sha256=[0-9a-f][0-9a-f]+")

# The store is a file, and it is the file this says it is. Its words are
# the probe's own, which is what makes them printable here at all.
if(NOT EXISTS "${store}")
  message(FATAL_ERROR "no store was written to ${store}")
endif()
file(READ "${store}" text)
# The kind on every record, and the two rows that share a number
# (#218): the probe carries a tale numbered one over entry one's own
# rectangle, so a build that keyed on the number alone would write
# three records here instead of four and this would say so.
foreach(want "amberfolio-journal 3" "AMBER FOLIO PROBE ENTRY 1"
             "AMBER FOLIO PROBE ENTRY 2"
             "scanned entry 1 " "scanned tale 1 ")
  string(FIND "${text}" "${want}" at)
  if(at LESS 0)
    message(FATAL_ERROR "the store does not carry '${want}':\n${text}")
  endif()
endforeach()

# --- 2. A correction survives a second ingestion -----------------------
#
# Written by hand, which is also the check that the format is one a
# person can edit — `host/journal_store.h` says that is why it is text
# with each record's length in front of it.
set(correction "A PERSON WROTE THIS")
string(LENGTH "${correction}" correction_length)
file(APPEND "${store}" "corrected entry 1 ${correction_length}\n${correction}\n")

run_host(--journal "${document}" --journal-probe --journal-store "${store}")
expect("journal store .*entries=4 corrections=1")
file(READ "${store}" text)
string(FIND "${text}" "${correction}" at)
if(at LESS 0)
  message(FATAL_ERROR "the correction did not survive re-ingestion:\n${text}")
endif()
string(FIND "${text}" "AMBER FOLIO PROBE ENTRY 1" at)
if(at LESS 0)
  message(FATAL_ERROR
    "re-ingestion did not replace the scan underneath the correction:\n${text}")
endif()

# --- 3. No engine is a sentence, not a silence -------------------------

run_host(--journal "${document}" --journal-probe --journal-ocr none
         --journal-store "${SCRATCH}/journal-none.txt")
expect("journal no engine asked for")
expect("journal entries=4 extracted=4 recognized=0")

# --- 4. Unrecognized, twice --------------------------------------------
#
# A file that is not a journal — "abc", whose digest is FIPS 180-4
# appendix B.1's — and the probe against the shipped table, which knows
# nothing about it and must not.
set(stranger "${SCRATCH}/not-a-journal.bin")
file(WRITE "${stranger}" "abc")
run_host(--journal "${stranger}" --journal-probe
         --journal-store "${SCRATCH}/journal-stranger.txt")
expect("journal unrecognized sha256=ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
if(EXISTS "${SCRATCH}/journal-stranger.txt")
  message(FATAL_ERROR
    "an unrecognized document wrote a store, which it must never do")
endif()

run_host(--journal "${document}" --journal-store "${SCRATCH}/journal-shipped.txt")
expect("journal unrecognized sha256=")
expect("this is not a journal edition this build knows")

# --- 5. A file that is not there is a different sentence ---------------

run_host(--journal "${SCRATCH}/nothing-is-here.pdf" --journal-probe)
expect("journal .* could not be read")

# --- 6. The companions refuse to stand alone ---------------------------

run_host(--journal-probe)
if(code EQUAL 0)
  message(FATAL_ERROR "--journal-probe without --journal was accepted")
endif()
expect("need --journal")

message(STATUS
  "sdl host journal: a synthetic edition ingested end to end, a"
  " correction kept across a re-ingestion, and two unrecognized"
  " documents reported with their fingerprints")
