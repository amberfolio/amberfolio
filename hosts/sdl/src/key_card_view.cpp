// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card's lines. key_card_view.h says why they are here.

#include "key_card_view.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/key_card.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::sdl {
namespace {

/// `text` in a line of `card_columns` spaces, from `at`, cut at the edge.
void place(std::string& line, std::size_t at, std::string_view text) {
  for (std::size_t i = 0; i < text.size() && at + i < line.size(); ++i) {
    line[at + i] = text[i];
  }
}

[[nodiscard]] std::string blank_line() {
  std::string line;
  line.resize(card_columns, ' ');
  return line;
}

/// `text` broken into lines of at most `card_columns` characters, on
/// spaces. The legend is the only thing wrapped, and its words are short.
[[nodiscard]] std::vector<std::string> wrapped(std::string_view text) {
  std::vector<std::string> out;
  std::string line;
  while (!text.empty()) {
    const std::size_t space = text.find(' ');
    const std::string_view word = text.substr(0, space);
    if (!line.empty() && line.size() + 1 + word.size() > card_columns) {
      out.push_back(line);
      line.clear();
    }
    if (!line.empty()) {
      line += ' ';
    }
    line += word;
    if (space == std::string_view::npos) {
      break;
    }
    text.remove_prefix(space + 1);
  }
  if (!line.empty()) {
    out.push_back(line);
  }
  return out;
}

}  // namespace

std::vector<host::key_page> card_pages(const machine::seam_engine& seams) {
  return host::key_card_for(
      host::card_shell::desktop, [&seams](std::string_view id) {
        return seams.status(id).state == machine::seam_state::on;
      });
}

std::vector<std::string> card_lines(const std::vector<host::key_page>& pages,
                                    std::size_t& page) {
  std::vector<std::string> lines;
  std::string title = blank_line();
  place(title, 0,
        "keys - up/down or a click turns the page, right button closes");
  lines.push_back(title);
  if (pages.empty()) {
    std::string none = blank_line();
    place(none, 0, "no keys to show");
    lines.push_back(none);
    return lines;
  }
  page = std::min(page, pages.size() - 1);

  std::size_t tallest = 0;
  for (const host::key_page& each : pages) {
    tallest = std::max(tallest, each.rows.size());
  }

  const host::key_page& shown = pages[page];
  std::string heading = blank_line();
  place(heading, 0, shown.context->title);
  const std::string count =
      std::to_string(page + 1) + "/" + std::to_string(pages.size());
  place(heading, card_columns - count.size(), count);
  lines.push_back(heading);

  for (const host::key_row* row : shown.rows) {
    std::string line = blank_line();
    place(line, 0, row->keys);
    place(line, card_does_column, row->does);
    lines.push_back(line);
  }
  lines.push_back(blank_line());
  for (const std::string& piece : wrapped(host::key_card_legend())) {
    std::string line = blank_line();
    place(line, 0, piece);
    lines.push_back(line);
  }
  // The same height on every page, so the panel keeps its size: the
  // padding goes under the legend, where a short page's gap is least in
  // the way.
  const std::size_t height =
      lines.size() + (tallest - std::min(tallest, shown.rows.size()));
  while (lines.size() < height) {
    lines.push_back(blank_line());
  }
  return lines;
}

}  // namespace amberfolio::sdl
