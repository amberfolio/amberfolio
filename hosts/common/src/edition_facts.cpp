// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition requirement table and the match over it. edition_facts.h
// has the reasoning; the table itself is data/editions.json, compiled
// into `edition_table.h` by this directory's CMakeLists.txt, so the only
// code here is the lookup and the matching.

#include "amberfolio/host/edition_facts.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/host/edition_table.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

/// How many of `offered` carry the digest of one of `edition`'s
/// artifacts, and which artifacts those were.
///
/// `machine::digest_is` is the comparison, which is the same one every
/// other fact table in this project compares through — a fingerprint has
/// to mean the same thing wherever it is written down.
void score(const edition_requirements& edition,
           std::span<const offered_file> offered,
           std::vector<std::size_t>& matched) {
  matched.clear();
  for (std::size_t a = 0; a < edition.artifacts.size(); ++a) {
    const edition_artifact& artifact = edition.artifacts[a];
    if (artifact.fingerprint.empty()) {
      continue;  // a directory: nothing to hash, so nothing to match
    }
    for (const offered_file& file : offered) {
      if (machine::digest_is(file.digest, artifact.fingerprint)) {
        matched.push_back(a);
        break;
      }
    }
  }
}

}  // namespace

std::span<const edition_requirements> edition_requirements_table() {
  return generated::all_editions;
}

const edition_requirements* find_requirements(
    std::string_view fingerprint) noexcept {
  for (const edition_requirements& known : generated::all_editions) {
    if (known.fingerprint == fingerprint) {
      return &known;
    }
  }
  return nullptr;
}

edition_match match_edition(std::span<const offered_file> offered) {
  edition_match best;
  std::vector<std::size_t> matched;
  for (const edition_requirements& candidate : generated::all_editions) {
    score(candidate, offered, matched);
    if (matched.empty()) {
      continue;  // nothing of this edition is here; not a candidate
    }
    // Strictly greater, so a tie keeps the earlier row: the table's own
    // order is the only tiebreak this has, and picking the later one
    // would make the answer depend on where somebody appended.
    if (best.edition == nullptr || matched.size() > best.matched.size()) {
      best.edition = &candidate;
      best.matched = matched;
    }
  }
  if (best.edition == nullptr) {
    return best;
  }

  // What is owed. A required artifact whose digest nobody offered is
  // missing — including the directory rows, which have no digest and so
  // can never match; a host that lists files only should say so rather
  // than let a copy that cannot save look complete.
  std::size_t next = 0;
  for (std::size_t a = 0; a < best.edition->artifacts.size(); ++a) {
    const bool was_matched =
        next < best.matched.size() && best.matched[next] == a;
    if (was_matched) {
      ++next;
      continue;
    }
    if (best.edition->artifacts[a].required) {
      best.missing.push_back(a);
    }
  }

  // And what this edition does not name: every offered file no artifact
  // of it claims. A host prints these with their hashes — that list is
  // what an edition nobody has fingerprinted looks like.
  for (std::size_t f = 0; f < offered.size(); ++f) {
    bool claimed = false;
    for (const std::size_t a : best.matched) {
      if (machine::digest_is(offered[f].digest,
                             best.edition->artifacts[a].fingerprint)) {
        claimed = true;
        break;
      }
    }
    if (!claimed) {
      best.unclaimed.push_back(f);
    }
  }
  return best;
}

}  // namespace amberfolio::host
