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

/// The baseline, and the only entry there is: the code wheel that comes
/// with the currently sold archive release — the copy every fact in this
/// tree was gathered against, so the same release `known_editions()`
/// names for the binary.
///
/// The name says what a player would call it and which game it belongs
/// to; the title appears nominatively (TRADEMARK.md). Nothing here is a
/// byte of the document: a SHA-256 names a file without carrying any of
/// it, which is exactly what CONTRIBUTING.md permits to be written down.
///
/// The journal is absent, deliberately. A fingerprint is a fact about a
/// file somebody hashed, and nobody has hashed that one — so the journal
/// reader's gate (#174) refuses every document until this table gains
/// its line. That is the fail-closed direction, and it is what an empty
/// half of a fact table is supposed to do.
constexpr std::array<document_edition, 1> table{{
    {.fingerprint =
         "0db301aeac4d2ec1e63b409ca6d3c9d39c63381d6712ff2ce7edc0528c6586fd",
     .name = "Pool of Radiance code wheel, archive release (PDF)",
     .kind = document_kind::code_wheel},
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
