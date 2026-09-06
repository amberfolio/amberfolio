// SPDX-License-Identifier: AGPL-3.0-only
//
// The `tsv` filter. tsv_words.h has the reasoning and the format.

#include "tsv_words.h"

#include <cstddef>
#include <string>
#include <string_view>

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

  /// Same paragraph: a new block is a new paragraph too, which is what
  /// Tesseract's own text output says (tsv_words.h).
  [[nodiscard]] bool same_paragraph(const place& other) const noexcept {
    return page == other.page && block == other.block &&
           paragraph == other.paragraph;
  }
};

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
  std::string& out = reading.text;
  double total = 0.0;
  place current;
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
    if (filtered && (cx < left || cx >= right || cy < top || cy >= bottom)) {
      continue;
    }

    // What separates this word from the one before it, in the engine's
    // own terms (#331): a **blank line** where the paragraph changed, one
    // newline where only the printed line did, and a space otherwise. The
    // break is only ever emitted *between* two words that were kept, so a
    // rectangle that clips a paragraph gains no break at the crop and a
    // reading never opens or ends on one.
    const place here{.page = number(field(line, 1), -1),
                     .block = number(field(line, 2), -1),
                     .paragraph = number(field(line, 3), -1),
                     .line = number(field(line, 4), -1)};
    if (any) {
      if (!here.same_paragraph(current)) {
        out.append("\n\n");
      } else if (!here.same_line(current)) {
        out.push_back('\n');
      } else {
        out.push_back(' ');
      }
    }
    current = here;
    any = true;
    out.append(text);

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
