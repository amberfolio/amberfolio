// SPDX-License-Identifier: AGPL-3.0-only
//
// The `tsv` filter. tsv_words.h has the reasoning and the format.

#include "tsv_words.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_ocr.h"

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

/// The `conf` field, which is a decimal on a 0-100 scale — `96`, `96.55`,
/// and `-1` on the layout lines a word filter never reaches.
///
/// Parsed here rather than with `std::from_chars` or `strtod` for the
/// reason the integer one above is: this file is the part of the desktop
/// engine that runs with no engine installed, and a locale-sensitive
/// conversion would make a `tsv` read differently on a machine whose
/// decimal separator is a comma. Negative is refused as "the engine did
/// not say", which is what -1 means.
[[nodiscard]] double confidence(std::string_view text, double fallback) {
  if (text.empty() || text.front() == '-') {
    return fallback;
  }
  double value = 0.0;
  double scale = 0.0;
  for (const char c : text) {
    if (c == '.' && scale == 0.0) {
      scale = 1.0;
      continue;
    }
    if (c < '0' || c > '9') {
      return fallback;
    }
    if (scale == 0.0) {
      value = (value * 10.0) + static_cast<double>(c - '0');
    } else {
      scale *= 10.0;
      value += static_cast<double>(c - '0') / scale;
    }
  }
  return value;
}

/// Where in the engine's own layout tree one word sat.
///
/// All four counts **restart inside their parent** — `par_num` is 1 again
/// in the next block, `line_num` 1 again in the next paragraph — so what
/// identifies a line is the whole tuple and never its last number. Two
/// one-line paragraphs are `line_num` 1 twice, and the code that compared
/// only that ran them together into one line (#331).
struct place {
  long page{-1};
  long block{-1};
  long paragraph{-1};
  long line{-1};

  /// Same printed line: everything equal.
  [[nodiscard]] bool same_line(const place& other) const noexcept {
    return page == other.page && block == other.block &&
           paragraph == other.paragraph && line == other.line;
  }

  /// Same block: the coarse grouping, and the only one of the engine's
  /// own that this trusts (tsv_words.h).
  [[nodiscard]] bool same_block(const place& other) const noexcept {
    return page == other.page && block == other.block;
  }
};

/// One printed line of the reading: where it sat and what it said.
struct reading_line {
  place where;
  long left{0};
  long right{0};
  long height{0};
  std::string text;
};

/// How far past the column's margin a line has to start before it counts
/// as indented, as a fraction of a line's own height.
///
/// A fraction of the type rather than a number of pixels, because the
/// only scale this file can see is the type's: the same edition read at
/// one and at two comes through here with every measurement doubled, and
/// a threshold in pixels would mean two different things. The printed
/// indent of this edition is a little over a line's height; six tenths of
/// one is comfortably under that and comfortably over the ragged left
/// edge a scan of handwriting-style type has.
constexpr double indent_of_line_height = 0.6;

/// How short of the column's own width a line has to fall before it can
/// be the *last* line of a paragraph, when it does not end in a stop.
///
/// The column is ragged right — this face is not justified — so "ends
/// early" is a weak signal and the threshold is set where only a line
/// that plainly stops short passes it. A heading does; a line of prose
/// that happened to break a word early does not.
constexpr double short_line_of_column = 0.75;

/// Whether `text` ends a sentence, allowing for a closing quote after the
/// stop. A colon counts: the journal's headings end in one.
[[nodiscard]] bool ends_a_sentence(std::string_view text) noexcept {
  while (!text.empty() && (text.back() == '"' || text.back() == '\'' ||
                           text.back() == ')' || text.back() == ']')) {
    text.remove_suffix(1);
  }
  if (text.empty()) {
    return false;
  }
  const char last = text.back();
  return last == '.' || last == '!' || last == '?' || last == ':';
}

/// Whether the line at `here` opens a new paragraph after `before`.
///
/// **Two things have to be true**, and neither alone is enough — which is
/// the whole of what changed in #345, and tsv_words.h has the numbers.
///
/// The line before it has to have **ended**: with a stop, or well short
/// of the column. And this line has to **start** something: a new block,
/// which is a different region of the page, or an indent.
///
/// `margin` is the column's own left edge and `edge` its right, both
/// measured off the lines that were kept rather than off the rectangle: a
/// rectangle is measured to the column and the ink inside it starts where
/// it starts.
[[nodiscard]] bool opens_paragraph(const reading_line& before,
                                   const reading_line& here, long margin,
                                   long edge, long unit) noexcept {
  const auto column = static_cast<double>(edge - margin);
  const bool ended =
      ends_a_sentence(before.text) ||
      (column > 0.0 && static_cast<double>(before.right - margin) <
                           column * short_line_of_column);
  if (!ended) {
    return false;
  }
  return !here.where.same_block(before.where) ||
         static_cast<double>(here.left - margin) >
             static_cast<double>(unit) * indent_of_line_height;
}

}  // namespace

tsv_reading tsv_read(std::string_view table,
                     const host::journal_region* region) {
  // A null region keeps every word, which is the decoded path: the image
  // Tesseract was handed *is* the entry, so there is nothing to filter
  // and the bounds below would be a rectangle nobody measured (#315).
  const bool filtered = region != nullptr;
  const long left = filtered ? static_cast<long>(region->left) : 0;
  const long top = filtered ? static_cast<long>(region->top) : 0;
  const long right = filtered ? left + static_cast<long>(region->width) : 0;
  const long bottom = filtered ? top + static_cast<long>(region->height) : 0;

  tsv_reading reading;
  double total = 0.0;
  // Gathered before anything is written, because the rule that puts the
  // blank lines in is about the column and not about one word: the
  // margin, the far edge and the size of the type are facts about every
  // line that was kept, and none of them is known until the last one is.
  std::vector<reading_line> lines;
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
    if (filtered && (cx < left || cx >= right || cy < top || cy >= bottom)) {
      continue;
    }

    // Which printed line this word is on. A word joins the line before it
    // when the engine's whole tuple matches and starts a new one when it
    // does not; where the *paragraphs* go is decided afterwards, over the
    // lines, by `opens_paragraph()`.
    const place here{.page = number(field(line, 1), -1),
                     .block = number(field(line, 2), -1),
                     .paragraph = number(field(line, 3), -1),
                     .line = number(field(line, 4), -1)};
    if (lines.empty() || !here.same_line(lines.back().where)) {
      lines.push_back({.where = here,
                       .left = x,
                       .right = x + w,
                       .height = h,
                       .text = std::string(text)});
    } else {
      reading_line& row = lines.back();
      row.right = std::max(row.right, x + w);
      row.height = std::max(row.height, h);
      row.text.push_back(' ');
      row.text.append(text);
    }

    // The engine's own opinion of the word it just gave, kept for the
    // entry it was kept for (#315). A word whose `conf` this cannot read
    // counts towards neither the mean nor the doubtful tally and is not
    // in `words` either: it is a word this build has no opinion about,
    // and averaging it in as a zero would say the engine was sure it was
    // wrong.
    if (const double conf = confidence(field(line, 10), -1.0); conf >= 0.0) {
      ++reading.quality.words;
      total += conf;
      if (conf < host::journal_doubtful_confidence) {
        ++reading.quality.doubtful;
      }
    }
  }
  // The column, off the lines that were kept, and then the reading. A
  // break falls only *between* two lines that were kept, so a rectangle
  // that clips a paragraph gains none at the crop and a reading never
  // opens or ends on one (tsv_words.h).
  std::string& out = reading.text;
  if (!lines.empty()) {
    long margin = lines.front().left;
    long edge = lines.front().right;
    std::vector<long> heights;
    heights.reserve(lines.size());
    for (const reading_line& row : lines) {
      margin = std::min(margin, row.left);
      edge = std::max(edge, row.right);
      heights.push_back(row.height);
    }
    // The median, because one line of a column can be a heading in a
    // larger face, or a box the engine drew around a speck, and a mean
    // would take either seriously.
    const auto middle =
        heights.begin() + (static_cast<std::ptrdiff_t>(heights.size()) / 2);
    std::nth_element(heights.begin(), middle, heights.end());
    const long unit = *middle;

    out.append(lines.front().text);
    for (std::size_t i = 1; i < lines.size(); ++i) {
      out.append(opens_paragraph(lines[i - 1U], lines[i], margin, edge, unit)
                     ? "\n\n"
                     : "\n");
      out.append(lines[i].text);
    }
  }

  if (reading.quality.words != 0) {
    reading.quality.known = true;
    reading.quality.confidence =
        total / static_cast<double>(reading.quality.words);
  }
  return reading;
}

std::string tsv_words_within(std::string_view table,
                             const host::journal_region& region) {
  return tsv_read(table, &region).text;
}

}  // namespace amberfolio::sdl
