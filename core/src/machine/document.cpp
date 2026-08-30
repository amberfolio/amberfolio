// SPDX-License-Identifier: AGPL-3.0-only
//
// The document edition table. document.h has the reasoning; the hex
// helper it compares through is edition.cpp's, because a fingerprint has
// to mean the same thing whichever table it is in.

#include "amberfolio/machine/document.h"

#include <array>
#include <span>

#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {
namespace {

/// The documents the currently sold archive release comes with — the copy
/// every fact in this tree was gathered against, so the same release
/// `known_editions()` names for the binary.
///
/// Each name says what a player would call it and which game it belongs
/// to; the title appears nominatively (TRADEMARK.md). Nothing here is a
/// byte of a document: a SHA-256 names a file without carrying any of it,
/// which is exactly what CONTRIBUTING.md permits to be written down.
///
/// **The journal's line arrived with M5-E3b (#214)**, which is the first
/// time anybody sat down with one. Until then this table had a single row
/// and said so at length: a fingerprint is a fact about a file somebody
/// hashed, and nobody had hashed that one. What it took to add it is
/// `docs/journal.md` §3, and the second half of it — where each entry is
/// inside the file — is `hosts/common/src/journal_facts.cpp`. The two are
/// checked against each other in CI, so a document known here and not
/// there fails a test rather than a player's ingestion.
constexpr std::array<document_edition, 2> table{{
    {.fingerprint =
         "0db301aeac4d2ec1e63b409ca6d3c9d39c63381d6712ff2ce7edc0528c6586fd",
     .name = "Pool of Radiance code wheel, archive release (PDF)",
     .kind = document_kind::code_wheel},
    {.fingerprint =
         "67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c",
     .name = "Pool of Radiance Adventurer's Journal, archive release (PDF)",
     .kind = document_kind::journal},
}};

}  // namespace

const char* document_kind_name(document_kind kind) noexcept {
  switch (kind) {
    case document_kind::none:
      return "no document";
    case document_kind::code_wheel:
      return "code wheel";
    case document_kind::journal:
      return "journal";
  }
  return "unknown";
}

std::span<const document_edition> known_documents() { return table; }

const document_edition* find_document(const sha256_digest& digest) noexcept {
  for (const document_edition& known : table) {
    if (digest_is(digest, known.fingerprint)) {
      return &known;
    }
  }
  return nullptr;
}

}  // namespace amberfolio::machine
