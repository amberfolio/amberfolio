// SPDX-License-Identifier: AGPL-3.0-only
//
// The document edition table: which of the player's *documents* this
// build knows by their SHA-256, and what each one is a document for
// (M5-D3, #171).
//
// `edition.h` next door is the same idea about the game's program image,
// and this is deliberately its sibling rather than a second column in
// it. PLAN.md §2 lists three artifacts a player may supply — the
// binaries, the Adventurer's Journal, the code wheel — and only the
// first is required. A binary is what the machine *runs*; a document is
// something the player *holds*, and the difference is the whole of what
// the gate below means.
//
//
// A possession gate, and nothing more
// -----------------------------------
//
// PLAN.md §5 gates two enhancements on a fingerprint-verified document:
// the code-wheel bypass on the code wheel, the journal on the journal.
// The rule it states is exact — "a possession gate: it demonstrates the
// player holds the document, no more" — and this file is built to that
// sentence and no further.
//
// So: a fingerprint over the bytes a host hands in, compared against the
// table. Nothing is parsed, nothing is rendered, nothing is read
// *inside* the document. The journal's extractor (#174) does read inside
// its own document, and it is a separate piece of work with its own
// issue; a gate is over bytes.
//
// The policy is progressive, which is why this is a gate and not a
// requirement: a missing document leaves its enhancement unavailable and
// changes nothing else. Without the code wheel the wheel challenge
// appears exactly as it did on the real machine, which is the honest
// behaviour rather than a degraded one.
//
//
// The unrecognized path is a first-class answer, again
// ---------------------------------------------------
//
// `find_document()` answering null is not a failure, for the reason
// `find_edition()` answering null is not: it is the machine saying "I do
// not know this file". PLAN.md §9 names edition variance as a real risk
// — players hold re-scanned PDFs and releases nobody here has seen — and
// the mitigation is a friendly message and a process for adding
// editions, never a guess. A gate that armed on an unrecognized document
// would be a gate that armed on anything.
//
//
// What is deliberately not here
// -----------------------------
//
// Anything that reproduces anything. A SHA-256 names a file without
// carrying a byte of it, which is why CONTRIBUTING.md lists fingerprints
// among the things this project may write down — and that rule covers
// documents exactly as it covers the binary. No page image, no text, no
// offsets into one. The journal's fact table (#174) will carry page
// regions and stream offsets, which are facts too, and it will live
// beside the extractor that uses them.

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/sha256.h"

namespace amberfolio::machine {

/// What a document is a document *for* — the gate a seam names.
///
/// `none` is not a document. It is a `seam_definition`'s way of saying
/// it has no gate, which is what every seam in this build says today,
/// and it never appears in the table below. One enumeration rather than
/// a flag beside a kind, because "does this seam need a document" and
/// "which one" are the same question asked once.
enum class document_kind : std::uint8_t {
  none,
  /// The code wheel, for the copy-protection bypass (PLAN.md §5 item 1,
  /// #115).
  code_wheel,
  /// The Adventurer's Journal, for the journal reader (PLAN.md §5 item
  /// 2, #174).
  journal,
};

/// How many kinds there are, `none` included, for an array indexed by
/// one.
inline constexpr std::size_t document_kind_count = 3;

/// The printable name of a `document_kind` — `code wheel`, `journal`.
/// Never null.
///
/// Words a person reads rather than the enumerator's own spelling,
/// because this one lands in the middle of a sentence a host prints:
/// `seam code-wheel inert document_not_presented - the code wheel has
/// not been presented`. Here rather than in a host so that both hosts
/// say it identically (machine/report.h says why that matters).
[[nodiscard]] const char* document_kind_name(document_kind kind) noexcept;

/// One known document: the SHA-256 of the file the player holds, the
/// name a host shows for it, and what it is a document for.
struct document_edition {
  std::string_view fingerprint;
  std::string_view name;
  document_kind kind{document_kind::none};
};

/// Every document edition this build knows.
///
/// One, today: the code wheel of the currently sold archive release —
/// the copy this project's own facts were gathered against, which is the
/// same answer `known_editions()` gives for the binary (PLAN.md §10).
/// The journal has no entry yet and this file will not invent one; a
/// fingerprint is a fact about a file somebody actually hashed, and
/// nobody here has hashed that one.
///
/// **Adding an edition is adding a line**: the SHA-256 of the file, a
/// name a player would recognize, and the kind. It is a fact-table
/// change and not a mechanism change, which is the whole reason the
/// table is a separate thing from the gate.
[[nodiscard]] std::span<const document_edition> known_documents();

/// The document `digest` names, or null for one this build does not
/// recognize — in which case the enhancement it would have gated stays
/// unavailable and the host says so (PLAN.md §9's friendly
/// unrecognized-artifact path).
[[nodiscard]] const document_edition* find_document(
    const sha256_digest& digest) noexcept;

}  // namespace amberfolio::machine
