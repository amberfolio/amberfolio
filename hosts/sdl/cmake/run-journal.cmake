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
# The entries that are pictures (#328, #345). The probe has two, on the
# two pages that reach the extractor by different routes: one this build
# inflates itself, one `/DCTDecode`. **Both** are made here, in a default
# build with nothing installed, which is what #345 changed -- before it,
# the second needed a host that had linked an image library for some
# other reason, and a player of a default build got the caption of an
# entry with the map missing under it. Both numbers all the same, so
# "this build cannot decode the pages" could never read as "this journal
# has no drawings".
expect("journal pages decoded by stb_image")
expect("journal pictures=2/2")
expect("journal store .*entries=4 corrections=0 pictures=2 sha256=[0-9a-f][0-9a-f]+")
# With nothing corrected there is nothing to score against, and the host
# says which rather than printing a zero that would read as a perfect
# transcription (#315). The fixture engine reports no confidences either,
# so there is no `journal read` line here at all -- an engine that does
# not say and an engine that was unsure are different things.
expect("journal score nothing to measure against")

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
# Entry two is not in this list on purpose: it carries a paragraph break
# (#331) and so is checked in its own shape below, not as a run of words.
foreach(want "amberfolio-journal 5" "AMBER FOLIO PROBE ENTRY 1"
             "scanned entry 1 " "scanned tale 1 "
             "picture entry 1 0 ")
  string(FIND "${text}" "${want}" at)
  if(at LESS 0)
    message(FATAL_ERROR "the store does not carry '${want}':\n${text}")
  endif()
endforeach()

# And entry two, which carries a **paragraph break** (#331). It is in the
# file the way the engine answered it, blank line and all: the format
# puts each text's length in front of it precisely so that a body may
# hold anything, and a store that normalized whitespace on the way to
# disk would take the shape out of every real entry. The reader draws a
# blank line as a paragraph and a single newline as a space (#316), so
# this is the difference between an entry with paragraphs and one solid
# block of prose.
string(FIND "${text}" "AMBER FOLIO PROBE\n\nENTRY 2" at)
if(at LESS 0)
  message(FATAL_ERROR
    "the store did not keep entry two's paragraph break:\n${text}")
endif()

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
# And now there *is* something to score against, which is the whole
# design of the measurement (#315): the ground truth is the player's own
# correction, so an ingestion becomes measurable the moment somebody
# fixes something. Nineteen characters of correction against the
# twenty-five the fixture read is not a small rate, and the point here is
# only that a real number reaches the player -- the arithmetic is checked
# case by case in `hosts/common/tests/journal_score_test.cpp`.
expect("journal score corrected=1 characters=[0-9]+\\.[0-9]+% words=[0-9]+\\.[0-9]+%")
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

# --- 7. The cheat that cites everything (#301), and where it lands ------
#
# Over the store step 2 left: four rows, and — since #351 — a store file
# that carries no log at all. The flag puts all four on the machine's log,
# Entry 1 first, and the host writes them into `\SAVE\AFSEEN.DAT` beside
# the saves, which is a file it only writes when it was asked to:
# `--save-sidecars`. So this runs over a *copy* of the smoke disk, because
# the point of the flag is that a run does not change a directory nobody
# offered it.
#
# What is checked here is that the file arrives, that it is the sidecar it
# says it is, and that a second run reads it back — four rows and not
# eight. The row order and the fields are held down in C++
# (`JournalCiteAll` in `hosts/common/tests/journal_store_test.cpp`); a
# sidecar is bytes, and this is the place to check that the bytes are
# where a player's next launch will look for them.

set(cite_disk "${SCRATCH}/cite-disk")
file(REMOVE_RECURSE "${cite_disk}")
file(COPY "${DISK}/" DESTINATION "${cite_disk}")
set(sidecar "${cite_disk}/SAVE/AFSEEN.DAT")

function(run_cite_host)
  execute_process(
    COMMAND "${HOST}" "${cite_disk}" HELLO.EXE --headless ${ARGN}
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  set(code "${code}" PARENT_SCOPE)
  set(out "${out}" PARENT_SCOPE)
  set(err "${err}" PARENT_SCOPE)
endfunction()

# Without the flag, nothing is written anywhere: the cheat still cites,
# and a directory nobody asked this build to write into is left alone.
run_cite_host(--journal-store "${store}" --cite-all-journal)
if(NOT code EQUAL 7)
  message(FATAL_ERROR
    "citing everything changed the program's exit code to '${code}'.\n"
    "stdout: ${out}\nstderr: ${err}")
endif()
expect("journal cited all 4 - the Notes log holds every entry \\(log=4\\)")
if(EXISTS "${sidecar}")
  message(FATAL_ERROR
    "a run without --save-sidecars wrote ${sidecar}, which it must never do")
endif()

# And the store's own file never carries the log again.
file(READ "${store}" text)
if(text MATCHES "\nseen ")
  message(FATAL_ERROR
    "the store file still carries the read log:\n${text}")
endif()

# With the flag, the sidecar is there and is one: `AFS`, version 1, four
# rows of eight bytes each behind an eight-byte header.
run_cite_host(--journal-store "${store}" --cite-all-journal --save-sidecars)
expect("journal cited all 4")
expect("save-sidecars writes=[1-9]")
if(NOT EXISTS "${sidecar}")
  message(FATAL_ERROR
    "citing everything with --save-sidecars wrote no ${sidecar}")
endif()
file(SIZE "${sidecar}" sidecar_bytes)
if(NOT sidecar_bytes EQUAL 40)
  message(FATAL_ERROR
    "the read log's sidecar is ${sidecar_bytes} bytes, wanted 8 + 4 * 8")
endif()
file(READ "${sidecar}" head LIMIT 4 HEX)
if(NOT head STREQUAL "41465301")
  message(FATAL_ERROR
    "the read log's sidecar begins '${head}', wanted 'AFS' and version 1")
endif()

# And a launch after it reads the four rows back, before anything has
# cited anything: which is the whole point of a sidecar.
run_cite_host(--journal-store "${store}" --save-sidecars)
expect("journal log seen=4")

run_host(--journal-store "${SCRATCH}/journal-nothing-here.txt" --cite-all-journal)
expect("journal nothing to cite - no journal has been ingested")
if(EXISTS "${SCRATCH}/journal-nothing-here.txt")
  message(FATAL_ERROR
    "citing with no journal wrote a store, which it must never do")
endif()

message(STATUS
  "sdl host journal: a synthetic edition ingested end to end, a"
  " correction kept across a re-ingestion, two unrecognized"
  " documents reported with their fingerprints, and the cheat that"
  " cites everything kept in the sidecar beside the save, which the"
  " next launch read back")
