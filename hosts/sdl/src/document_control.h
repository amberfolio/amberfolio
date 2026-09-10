// SPDX-License-Identifier: AGPL-3.0-only
//
// The document control: any PDF in, what it was recognised as out, and
// the rows it lights (M6, #384).
//
// `--document PATH` has presented a document since M5-D3 (#171) and
// nothing put a face on it: a player who holds a document had to find a
// flag in a usage block to show it, and a player holding an *edition
// this build has never seen* got a line on stderr and no idea that the
// hash on it was the actionable part. M6's exit criterion says a new
// player gets from artifacts in hand to playing without reading source
// code, and a document is one of PLAN.md §2's three artifacts.
//
// So this host takes a document two ways — the flag, and a file dropped
// on the window — and both go through here, which turns a digest into
// the two or three lines a player reads.
//
//
// Two outcomes, and the second one is not a failure
// -------------------------------------------------
//
// A presented document is either an edition this build knows or it is
// not, and `machine/document.h` argues at length why the second is a
// first-class answer rather than an error: players hold re-scanned PDFs
// and releases nobody here has seen (PLAN.md §9). What that player needs
// is not an apology, it is **the SHA-256 of the file they hold**, which
// is what an entry in the table is made of. So the fingerprint is on the
// line either way, and the unrecognised line says plainly that nobody
// here has fingerprinted this one rather than guessing at which edition
// it might be (CLAUDE.md's "log, don't fake").
//
// Neither outcome is a refusal of the *control*: the file was read and
// hashed, and that is all a possession gate ever does with it.
//
//
// And the rows it lights
// ----------------------
//
// A document matters to a player because seams wait on it
// (`seam_definition::gate`), so the report says which. That is a count
// of the seams gated on the kind that just arrived, named — not a count
// of the ones that changed state, because a gated seam a player has not
// turned on yet is still a row that document is for. **Today it is
// always none**: since #290 no seam in this build is gated, the
// code-wheel bypass having become a question a person answers (#291), so
// both hosts say "nothing in this build waits on the code wheel" and
// that is the honest current answer rather than a feature that does not
// work. The mechanism is exercised over a stood-up gated seam in
// `tests/document_control_test.cpp` and by `SeamGate.*` in core's own
// suite.
//
//
// Why the sentences are here and not in `main()`
// ----------------------------------------------
//
// Because the page says them too, word for word
// (`hosts/web/page/documents.mjs`), and because a sentence built inside
// a `main()` is a sentence no test can read. The two hosts spell this
// the way they spell the toggle panel's five columns and the edition
// checklist's lines — once each, deliberately identical, with a test on
// each side holding the words down — so that a player who reads a forum
// post about one is reading about the other. `docs/hosts.md` §9 is where
// the wording is written down for both.

#pragma once

#include <string>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"

namespace amberfolio::sdl {

/// What presenting one document turned out to be.
///
/// Strings rather than the `document_edition` itself for `panel_row`'s
/// reason: the outcome outlives the call that made it, and a test that
/// has to build a `seam_engine` to check a sentence is a test nobody
/// writes.
struct document_outcome {
  /// Whether this build knows the edition. Not "whether it worked": a
  /// document this build has never seen was still read and hashed.
  bool recognized{false};
  /// 64 lowercase hex characters, **whatever the answer**. It is the one
  /// thing an unrecognised outcome gives a player to act on.
  std::string fingerprint;
  /// Core's name for the edition, and what it is a document for
  /// (`code wheel`, `journal`). Empty for an unrecognised document,
  /// because this build has nothing to call it.
  std::string name;
  std::string kind;
  /// The ids of the seams gated on that kind, in registration order —
  /// the rows this document lights. Empty when nothing waits on it,
  /// which is every seam in this build today.
  std::vector<std::string> waiting;
};

/// Present `digest` to `seams` and say what it turned out to be.
///
/// The one place a document reaches the engine on this host. It takes a
/// digest and not a path because the journal's ingestion (#174) has
/// already read and hashed the file it is about to read the insides of,
/// and hashing it a second time would be a second answer that could
/// differ from the first.
[[nodiscard]] document_outcome present_document_to(machine::seam_engine& seams,
                                                   const sha256_digest& digest);

/// The outcome as the lines a player reads, in the words the page uses
/// (`page/documents.mjs`'s `describeDocument`).
///
/// Two lines: what the document is, and what it is *for* here. Neither
/// carries this host's `amberfolio: ` prefix or the page's `[host] ` one
/// — a caller puts the characters somewhere a person can read them, and
/// the panel and stderr do that differently.
[[nodiscard]] std::vector<std::string> document_lines(
    const document_outcome& outcome);

}  // namespace amberfolio::sdl
