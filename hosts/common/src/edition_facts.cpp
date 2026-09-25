// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition requirement table and the match over it. edition_facts.h
// has the reasoning; the table itself is data/editions.json, compiled
// into `edition_table.h` by this directory's CMakeLists.txt, so the only
// code here is the lookup and the matching.

#include "amberfolio/host/edition_facts.h"

#include <algorithm>
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
      continue;  // a configuration file: matched by name, and only later
    }
    for (const offered_file& file : offered) {
      if (machine::digest_is(file.digest, artifact.fingerprint)) {
        matched.push_back(a);
        break;
      }
    }
  }
}

/// Whether `path`'s last component is `name`, ignoring ASCII case. A
/// host may offer a bare name or a path under the copy's install
/// directory, with either slash; DOS names are case-blind.
bool names(std::string_view path, std::string_view name) noexcept {
  const std::size_t cut = path.find_last_of("/\\");
  const std::string_view last =
      cut == std::string_view::npos ? path : path.substr(cut + 1);
  const auto lower = [](char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
  };
  return std::ranges::equal(last, name, {}, lower, lower);
}

/// Whether offered file `file` is claimed by artifact `artifact`: by the
/// digest for everything that has one, by the name for a configuration
/// file.
bool claims(const edition_artifact& artifact, const offered_file& file) {
  if (artifact.kind == artifact_kind::configuration) {
    return names(file.name, artifact.name);
  }
  return !artifact.fingerprint.empty() &&
         machine::digest_is(file.digest, artifact.fingerprint);
}

}  // namespace

std::span<const edition_requirements> edition_requirements_table() {
  return generated::all_editions;
}

const edition_requirements* find_requirements(
    std::string_view fingerprint) noexcept {
  // The first row, in the table's order: the baseline (edition_facts.h).
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
  const std::span<const edition_artifact> artifacts = best.edition->artifacts;

  // The configuration rows, now that a row is chosen: by name, because
  // the bytes are whatever the player's launcher wrote. Kept in index
  // order, which the pass below relies on.
  for (std::size_t a = 0; a < artifacts.size(); ++a) {
    if (artifacts[a].kind != artifact_kind::configuration) {
      continue;
    }
    for (const offered_file& file : offered) {
      if (claims(artifacts[a], file)) {
        best.matched.insert(std::ranges::upper_bound(best.matched, a), a);
        break;
      }
    }
  }

  // What is owed: a required artifact nothing offered.
  std::size_t next = 0;
  for (std::size_t a = 0; a < artifacts.size(); ++a) {
    const bool was_matched =
        next < best.matched.size() && best.matched[next] == a;
    if (was_matched) {
      ++next;
      continue;
    }
    if (artifacts[a].required) {
      best.missing.push_back(a);
    }
  }

  // And what this edition does not name: every offered file no artifact
  // of it claims. A host prints these with their hashes — that list is
  // what an edition nobody has fingerprinted looks like.
  for (std::size_t f = 0; f < offered.size(); ++f) {
    const bool claimed = std::ranges::any_of(
        best.matched,
        [&](const std::size_t a) { return claims(artifacts[a], offered[f]); });
    if (!claimed) {
      best.unclaimed.push_back(f);
    }
  }
  return best;
}

}  // namespace amberfolio::host
