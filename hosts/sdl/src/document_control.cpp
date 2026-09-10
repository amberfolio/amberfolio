// SPDX-License-Identifier: AGPL-3.0-only
//
// The document control's two outcomes, as sentences. document_control.h
// says why they are here and not in main.cpp, and why the page spells
// them the same.

#include "document_control.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "amberfolio/machine/document.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"

namespace amberfolio::sdl {
namespace {

/// `n seams`, or `1 seam`. A count in a sentence is read as a sentence,
/// and `1 seams` reads as a bug in the thing that printed it.
[[nodiscard]] std::string seams_plural(std::size_t n) {
  return std::to_string(n) + (n == 1 ? " seam" : " seams");
}

}  // namespace

document_outcome present_document_to(machine::seam_engine& seams,
                                     const sha256_digest& digest) {
  document_outcome out;
  std::array<char, sha256_digest::text_length + 1> hex{};
  static_cast<void>(format_hex(digest, hex));
  out.fingerprint = hex.data();

  const machine::document_edition* known = seams.present_document(digest);
  if (known == nullptr) {
    // Reported, never guessed (machine/document.h): a gate that armed on
    // an unrecognised document would be a gate that armed on anything,
    // and a *report* that named a likely edition would be the same
    // failure one layer up.
    return out;
  }
  out.recognized = true;
  out.name = std::string(known->name);
  out.kind = machine::document_kind_name(known->kind);
  // The rows this document is for: gated on the kind that just arrived,
  // whether or not the player has turned them on. A seam nobody has
  // enabled yet is still a row this document lights.
  for (std::size_t i = 0; i < seams.count(); ++i) {
    const machine::seam_definition* definition = seams.find(seams.status(i).id);
    if (definition != nullptr && definition->gate == known->kind) {
      out.waiting.emplace_back(definition->id);
    }
  }
  return out;
}

std::vector<std::string> document_lines(const document_outcome& outcome) {
  std::vector<std::string> lines;
  if (!outcome.recognized) {
    lines.push_back("document unrecognized sha256=" + outcome.fingerprint +
                    " - no gate is satisfied by it");
    // The second line is the actionable half, and it is why an
    // unrecognised document is a clean outcome rather than a shrug: the
    // hash above is precisely what a new row in `machine/document.h`'s
    // table is made of.
    lines.emplace_back(
        "nobody here has fingerprinted this one - that sha256 is what an"
        " entry in the table is made of");
    return lines;
  }
  lines.push_back("document " + outcome.name + " (" + outcome.kind +
                  ") sha256=" + outcome.fingerprint);
  if (outcome.waiting.empty()) {
    lines.push_back("nothing in this build waits on the " + outcome.kind);
    return lines;
  }
  std::string lit = "the " + outcome.kind + " lights " +
                    seams_plural(outcome.waiting.size()) + ":";
  for (std::size_t i = 0; i < outcome.waiting.size(); ++i) {
    lit += (i == 0 ? " " : ", ") + outcome.waiting[i];
  }
  lines.push_back(lit);
  return lines;
}

}  // namespace amberfolio::sdl
