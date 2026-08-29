// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal edition fact table. journal_facts.h has the reasoning; the
// hex comparison is `machine::digest_is`, because a fingerprint has to
// mean the same thing whichever of this project's three tables it is in.

#include "amberfolio/host/journal_facts.h"

#include <span>

#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

/// No edition, and the header says why at length: these are facts about
/// a document somebody has to sit down with, and nobody has. The
/// mechanism is proven against `journal_probe.h`'s synthetic document
/// instead, which is the same arrangement `tests/programs` has with the
/// game — a self-written artifact that can be committed, standing in for
/// one that cannot.
///
/// `std::span` over nothing rather than an array of one placeholder:
/// there is no such thing as a placeholder fact.
constexpr std::span<const journal_edition> table{};

}  // namespace

const char* journal_filter_name(journal_filter which) noexcept {
  switch (which) {
    case journal_filter::none:
      return "none";
    case journal_filter::flate:
      return "FlateDecode";
    case journal_filter::dct:
      return "DCTDecode";
    case journal_filter::ccitt:
      return "CCITTFaxDecode";
    case journal_filter::jbig2:
      return "JBIG2Decode";
  }
  return "unknown";
}

bool journal_filter_supported(journal_filter which) noexcept {
  return which == journal_filter::none || which == journal_filter::flate;
}

std::span<const journal_edition> known_journals() { return table; }

const journal_edition* find_journal(std::span<const journal_edition> editions,
                                    const sha256_digest& digest) noexcept {
  for (const journal_edition& known : editions) {
    if (machine::digest_is(digest, known.fingerprint)) {
      return &known;
    }
  }
  return nullptr;
}

const journal_edition* find_journal(const sha256_digest& digest) noexcept {
  return find_journal(known_journals(), digest);
}

}  // namespace amberfolio::host
