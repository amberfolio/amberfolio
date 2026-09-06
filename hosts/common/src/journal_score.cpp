// SPDX-License-Identifier: AGPL-3.0-only
//
// Scoring an ingestion. journal_score.h has the reasoning and the units.

#include "amberfolio/host/journal_score.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_store.h"

namespace amberfolio::host {
namespace {

[[nodiscard]] bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

/// The words of an already-normalized string — which is every run of
/// non-spaces, since normalization has left exactly one space between
/// them and none at either end.
[[nodiscard]] std::vector<std::string_view> words_of(std::string_view text) {
  std::vector<std::string_view> out;
  std::size_t at = 0;
  while (at < text.size()) {
    const std::size_t space = text.find(' ', at);
    if (space == std::string_view::npos) {
      out.push_back(text.substr(at));
      break;
    }
    out.push_back(text.substr(at, space - at));
    at = space + 1U;
  }
  return out;
}

/// Levenshtein over two random-access sequences, in two rows.
///
/// Two rows rather than the full matrix: the distance is all that is
/// wanted here and the path is not, so the O(n*m) time stays and the
/// O(n*m) memory goes. At `journal_score_limit` that is the difference
/// between sixteen kilobytes and sixty-four megabytes.
template <typename Sequence>
[[nodiscard]] std::size_t distance_of(const Sequence& a, const Sequence& b) {
  const std::size_t n = a.size();
  const std::size_t m = b.size();
  if (n == 0) {
    return m;
  }
  if (m == 0) {
    return n;
  }
  std::vector<std::size_t> previous(m + 1U);
  std::vector<std::size_t> current(m + 1U);
  for (std::size_t j = 0; j <= m; ++j) {
    previous[j] = j;
  }
  for (std::size_t i = 1; i <= n; ++i) {
    current[0] = i;
    for (std::size_t j = 1; j <= m; ++j) {
      const std::size_t substitute =
          previous[j - 1U] + (a[i - 1U] == b[j - 1U] ? 0U : 1U);
      current[j] =
          std::min({substitute, previous[j] + 1U, current[j - 1U] + 1U});
    }
    previous.swap(current);
  }
  return previous[m];
}

/// The two normalized sides of one comparison, or nothing when there is
/// no comparison to make — no truth to compare against, or a pair this
/// refuses on length (`journal_score.h`).
struct compared {
  std::string reference;
  std::string candidate;
  bool taken{false};
};

[[nodiscard]] compared prepare(std::string_view reference,
                               std::string_view candidate) {
  compared out{.reference = journal_normalize(reference),
               .candidate = journal_normalize(candidate),
               .taken = false};
  out.taken = !out.reference.empty() &&
              out.reference.size() <= journal_score_limit &&
              out.candidate.size() <= journal_score_limit;
  return out;
}

}  // namespace

std::string journal_normalize(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending = false;
  for (const char c : text) {
    if (is_space(c)) {
      // Held rather than written: a space is only worth a character when
      // something follows it, which is what makes this trim both ends
      // without a second pass.
      pending = !out.empty();
      continue;
    }
    if (pending) {
      out.push_back(' ');
      pending = false;
    }
    out.push_back(c);
  }
  return out;
}

std::size_t journal_edit_distance(std::string_view a, std::string_view b) {
  return distance_of(a, b);
}

std::size_t journal_edit_distance(std::span<const std::string_view> a,
                                  std::span<const std::string_view> b) {
  return distance_of(a, b);
}

journal_score journal_character_score(std::string_view reference,
                                      std::string_view candidate) {
  const compared both = prepare(reference, candidate);
  if (!both.taken) {
    return {};
  }
  return {.distance = journal_edit_distance(both.reference, both.candidate),
          .reference = both.reference.size(),
          .taken = true};
}

journal_score journal_word_score(std::string_view reference,
                                 std::string_view candidate) {
  const compared both = prepare(reference, candidate);
  if (!both.taken) {
    return {};
  }
  const std::vector<std::string_view> left = words_of(both.reference);
  const std::vector<std::string_view> right = words_of(both.candidate);
  return {.distance = journal_edit_distance(left, right),
          .reference = left.size(),
          .taken = true};
}

journal_store_report score_journal_store(const journal_store& store) {
  journal_store_report report;
  for (const journal_text& item : store.entries()) {
    // Both halves, or nothing to measure. `corrected` is the truth and
    // `scanned` is the candidate -- never `text()`, which answers the
    // correction wherever there is one and would therefore score every
    // corrected entry as perfect.
    if (item.corrected.empty() || item.scanned.empty()) {
      continue;
    }
    const journal_item_score scored{
        .what = {.kind = item.kind, .number = item.number},
        .characters = journal_character_score(item.corrected, item.scanned),
        .words = journal_word_score(item.corrected, item.scanned)};
    if (!scored.characters.taken) {
      ++report.refused;
      continue;
    }
    report.characters.distance += scored.characters.distance;
    report.characters.reference += scored.characters.reference;
    report.words.distance += scored.words.distance;
    report.words.reference += scored.words.reference;
    report.items.push_back(scored);
  }
  report.characters.taken = report.characters.reference != 0;
  report.words.taken = report.words.reference != 0;
  return report;
}

}  // namespace amberfolio::host
