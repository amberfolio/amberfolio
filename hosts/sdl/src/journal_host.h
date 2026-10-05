// SPDX-License-Identifier: AGPL-3.0-only
//
// A document the player holds, and the journal read out of one (M5-D3 #171,
// M5-E3 #174, #472): the host's half of "present a document", the OCR engine
// it is read with, and the ingestion that follows. Split from main.cpp.
// `docs/journal.md` is the argument for the journal; `document_control.h`
// has the two sentences a player is told about a document.

#pragma once

#include <string>
#include <vector>

#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/sha256.h"
#include "options.h"

namespace amberfolio::sdl {

/// Present `digest` to the engine and say what it turned out to be.
///
/// Split out of `present_document` below because the journal's ingestion
/// (#174) has already read and hashed the file it is about to read the
/// insides of, and hashing it a second time to say the same sentence
/// would be a second answer that could differ from the first.
/// Answers the lines it printed, so that a caller with somewhere else to
/// put them — the panel, when a player dropped the file on the window
/// rather than naming it on the command line — puts the same words there
/// (#384).
std::vector<std::string> present_digest(machine::machine& box,
                                        const sha256_digest& digest);

/// Read `path` off *this* machine's filesystem, hash it, and present it
/// to the engine (M5-D3, #171).
///
/// Streamed through a stack buffer rather than read whole: a document is
/// a PDF and a PDF can be tens of megabytes, and nothing about hashing
/// one needs it all in memory at once — the same argument
/// `machine::fingerprint_file` makes for a file on the emulated disk.
///
/// The bytes are hashed and dropped. This host never keeps a document,
/// never parses one, and never writes one anywhere (PLAN.md §2, §6): a
/// possession gate is over bytes, and that is the whole of it.
///
/// Answers the lines a player reads, `present_digest()`'s, so that the
/// two ways this host takes a document — `--document PATH` and a file
/// dropped on the window (#384) — cannot say different things about the
/// same file. A file that would not open is one line and no outcome:
/// nothing was hashed, so there is no fingerprint to report and nothing
/// to say about a gate.
std::vector<std::string> present_document(machine::machine& box,
                                          const std::string& path);

/// Ingest `opts.journal` (M5-E3, #174): present it, follow its entries,
/// read them, and keep the text.
///
/// The whole file is read into memory, which is what the extractor wants
/// (`host/journal_extract.h` says why) and what a browser has anyway.
/// Every failure here is a sentence and a return: an ingestion that goes
/// wrong leaves the run alone, because a player who asked to read their
/// journal and could not still asked to play.
void ingest_journal(machine::machine& box, const options& opts,
                    host::journal_store& store);

/// The journal, which is a document that is also read inside (#174), and the
/// read log the reader draws from (M5-E4, #175, #351): ingest it, or read the
/// text a previous run wrote, and put the log where the machine reads it.
/// Here rather than earlier than the documents because it presents itself
/// through the same door, and the same sentence about gates applies to it.
void bring_up_journal(machine::machine& box, const options& opts,
                      host::journal_store& journal_text);

/// `--cite-all-journal` (#301): everything the store holds, onto the log, so
/// `Notes` lists all of it.
void cite_everything(machine::machine& box, host::journal_store& journal_text);
}  // namespace amberfolio::sdl
