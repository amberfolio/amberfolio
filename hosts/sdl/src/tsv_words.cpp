// SPDX-License-Identifier: AGPL-3.0-only
//
// The `tsv` filter. tsv_words.h has the reasoning and the format.

#include "tsv_words.h"

#include <cstddef>
#include <string>
#include <string_view>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::sdl {
namespace {

/// One tab-separated field of `line`, by index, or empty.
[[nodiscard]] std::string_view field(std::string_view line, std::size_t which) {
  std::size_t at = 0;
  for (std::size_t i = 0; i < which; ++i) {
    const std::size_t tab = line.find('\t', at);
    if (tab == std::string_view::npos) {
      return {};
    }
    at = tab + 1U;
  }
  const std::size_t tab = line.find('\t', at);
  return line.substr(
      at, tab == std::string_view::npos ? std::string_view::npos : tab - at);
}

/// A field as a number, or `fallback` if it is not one.
[[nodiscard]] long number(std::string_view text, long fallback) {
  if (text.empty()) {
    return fallback;
  }
  long value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return fallback;
    }
    value = (value * 10) + (c - '0');
  }
  return value;
}

}  // namespace

std::string tsv_words_within(std::string_view table,
                             const host::journal_region& region) {
  const auto left = static_cast<long>(region.left);
  const auto top = static_cast<long>(region.top);
  const auto right = left + static_cast<long>(region.width);
  const auto bottom = top + static_cast<long>(region.height);

  std::string out;
  long current_line = -1;
  bool any = false;
  std::size_t at = 0;
  while (at <= table.size()) {
    const std::size_t end = table.find('\n', at);
    std::string_view line = table.substr(
        at, end == std::string_view::npos ? std::string_view::npos : end - at);
    at = end == std::string_view::npos ? table.size() + 1U : end + 1U;
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.empty() || number(field(line, 0), -1) != 5) {
      continue;  // the header, or a level that is not a word
    }

    const long x = number(field(line, 6), -1);
    const long y = number(field(line, 7), -1);
    const long w = number(field(line, 8), -1);
    const long h = number(field(line, 9), -1);
    const std::string_view text = field(line, 11);
    if (x < 0 || y < 0 || w < 0 || h < 0 || text.empty()) {
      continue;
    }
    const long cx = x + (w / 2);
    const long cy = y + (h / 2);
    if (cx < left || cx >= right || cy < top || cy >= bottom) {
      continue;
    }

    const long which = number(field(line, 4), -1);
    if (any && which != current_line) {
      out.push_back('\n');
    } else if (any) {
      out.push_back(' ');
    }
    current_line = which;
    any = true;
    out.append(text);
  }
  return out;
}

}  // namespace amberfolio::sdl
