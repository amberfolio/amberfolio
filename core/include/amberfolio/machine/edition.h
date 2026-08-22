// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition table: which program images this build knows by their
// SHA-256, and what it calls them (M4-F1, #95).
//
// PLAN.md §2 makes the fingerprint the identity of a player's file —
// "facts about the player's files, and the only thing the project ever
// stores about the originals" — and PLAN.md §5 makes it the key a seam
// set is keyed on. This table is the fact side of that: a fingerprint, a
// human name, and nothing else. Which seams apply to an edition is not
// stored here; a seam carries the fingerprints it is about (seam.h), and
// the set that applies to an edition is the set whose fingerprints name
// it. That keeps one fact in one place: a seam's addresses are facts
// about a binary, and the seam is where they live.
//
//
// The unrecognized path is a first-class answer
// ---------------------------------------------
//
// `find_edition()` answering null is not a failure. It is the machine
// saying "I do not know this file", which is the honest thing to say about
// an edition nobody has fingerprinted yet, and the consequence is exactly
// PLAN.md §5's rule: an unrecognized binary runs as a plain machine with
// no seams *available* — not misapplied ones, not guessed ones. Both
// hosts can show the answer, and nothing downstream treats it as an error.
//
// PLAN.md §10 asks which editions to fingerprint at launch. M4's answer
// is the baseline: the currently sold archive release, which is the copy
// this project's own facts were gathered against. The table is built to
// grow — add a line, with the fingerprint and a name — and growing it is
// a fact-table change, not a mechanism change.
//
//
// What is deliberately not here
// -----------------------------
//
// PDF artifacts — the journal and the code wheel — are M5's, on the same
// primitive (PLAN.md §2); the table below is program images only. And
// nothing here reproduces anything: a SHA-256 names a file without
// carrying a byte of it, which is why CONTRIBUTING.md lists fingerprints
// among the things this project may write down.

#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "amberfolio/sha256.h"

namespace amberfolio::machine {

/// One known edition: the SHA-256 of its program image, as 64 lowercase
/// hex characters, and the name a host shows for it.
struct edition {
  std::string_view fingerprint;
  std::string_view name;
};

/// Every edition this build knows. One, today — the baseline (PLAN.md
/// §10, answered for M4).
[[nodiscard]] std::span<const edition> known_editions();

/// The edition `digest` names, or null for a program this build does not
/// recognize — in which case the game runs as a plain machine and no
/// seam is available for it (PLAN.md §5).
[[nodiscard]] const edition* find_edition(const sha256_digest& digest) noexcept;

/// Whether `digest` is what `text` spells.
///
/// False for text of the wrong length or with a character that is not
/// hex: a fingerprint that is mistyped names nothing, which is the
/// failure this project wants over one that names everything. Here
/// rather than in sha256.h because it is the comparison the fact tables
/// make — a digest the machine computed against a fingerprint somebody
/// wrote down — and sha256.h knows nothing about fact tables.
[[nodiscard]] bool digest_is(const sha256_digest& digest,
                             std::string_view text) noexcept;

/// Parse 64 hex characters into a digest. False, and `out` untouched,
/// for anything that is not exactly that.
[[nodiscard]] bool parse_digest(std::string_view text,
                                sha256_digest& out) noexcept;

}  // namespace amberfolio::machine
