// SPDX-License-Identifier: AGPL-3.0-only
//
// The text route. journal_text.h has the reasoning and the six steps.

#include "amberfolio/host/journal_text.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

// --- tokens ------------------------------------------------------------

/// What a content stream (or a CMap, which is the same syntax) is made
/// of, as far as this route needs to tell things apart.
enum class token_kind : std::uint8_t {
  number,
  string,  // a literal or hex string's bytes
  name,    // without its slash
  open_array,
  close_array,
  open_dict,
  close_dict,
  keyword,  // an operator, or `true`, `null`, `begincmap`...
};

struct token {
  token_kind kind{token_kind::keyword};
  double number{};
  std::string bytes{};
};

[[nodiscard]] constexpr bool is_space(std::uint8_t c) noexcept {
  return c == 0x00U || c == 0x09U || c == 0x0AU || c == 0x0CU || c == 0x0DU ||
         c == 0x20U;
}

[[nodiscard]] constexpr bool is_delimiter(std::uint8_t c) noexcept {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '/' || c == '%';
}

[[nodiscard]] constexpr int hex_value(std::uint8_t c) noexcept {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

/// A number the PDF way: an optional sign, digits, an optional point and
/// more digits. No exponent — PDF has none — and no locale, which is why
/// this is not `strtod`.
[[nodiscard]] bool parse_number(std::string_view text, double& out) noexcept {
  std::size_t at = 0;
  bool negative = false;
  if (at < text.size() && (text[at] == '+' || text[at] == '-')) {
    negative = text[at] == '-';
    ++at;
  }
  double whole = 0.0;
  double scale = 1.0;
  bool point = false;
  bool digits = false;
  for (; at < text.size(); ++at) {
    const char c = text[at];
    if (c == '.' && !point) {
      point = true;
      continue;
    }
    if (c < '0' || c > '9') {
      return false;
    }
    digits = true;
    whole = (whole * 10.0) + (c - '0');
    if (point) {
      scale *= 10.0;
    }
  }
  if (!digits) {
    return false;
  }
  out = (negative ? -whole : whole) / scale;
  return true;
}

/// A tokenizer over one stream's decoded bytes.
class tokenizer {
 public:
  explicit tokenizer(std::span<const std::uint8_t> bytes) noexcept
      : bytes_(bytes) {}

  /// The next token into `out`. False at the end, and false with
  /// `broken()` set for bytes that are not the syntax at all.
  [[nodiscard]] bool next(token& out) {
    skip_space();
    if (at_ >= bytes_.size()) {
      return false;
    }
    out.bytes.clear();
    out.number = 0.0;
    const std::uint8_t c = bytes_[at_];
    switch (c) {
      case '(':
        ++at_;
        out.kind = token_kind::string;
        return literal(out.bytes);
      case '<':
        if (peek(1) == '<') {
          at_ += 2;
          out.kind = token_kind::open_dict;
          return true;
        }
        ++at_;
        out.kind = token_kind::string;
        return hex(out.bytes);
      case '>':
        if (peek(1) == '>') {
          at_ += 2;
          out.kind = token_kind::close_dict;
          return true;
        }
        return fail();
      case '[':
        ++at_;
        out.kind = token_kind::open_array;
        return true;
      case ']':
        ++at_;
        out.kind = token_kind::close_array;
        return true;
      case '{':
      case '}':
        // PostScript procedure braces, which a CMap may carry. Nothing
        // here reads inside one, so they are words like any other.
        ++at_;
        out.kind = token_kind::keyword;
        out.bytes.push_back(static_cast<char>(c));
        return true;
      case '/':
        ++at_;
        out.kind = token_kind::name;
        name(out.bytes);
        return true;
      case ')':
        return fail();
      default:
        break;
    }
    const std::size_t start = at_;
    while (at_ < bytes_.size() && !is_space(bytes_[at_]) &&
           !is_delimiter(bytes_[at_])) {
      ++at_;
    }
    const std::string_view word(
        reinterpret_cast<const char*>(bytes_.data()) + start, at_ - start);
    if (parse_number(word, out.number)) {
      out.kind = token_kind::number;
      return true;
    }
    out.kind = token_kind::keyword;
    out.bytes.assign(word);
    return true;
  }

  /// An inline image's data (`BI ... ID <bytes> EI`): step over the bytes
  /// after `ID` to the `EI` that ends them, which is the one place this
  /// syntax is not tokens.
  void skip_inline_image() noexcept {
    // One space after ID, then data until whitespace, `EI`, whitespace.
    if (at_ < bytes_.size()) {
      ++at_;
    }
    while (at_ + 1 < bytes_.size()) {
      if (bytes_[at_] == 'E' && bytes_[at_ + 1] == 'I' &&
          (at_ == 0 || is_space(bytes_[at_ - 1])) &&
          (at_ + 2 >= bytes_.size() || is_space(bytes_[at_ + 2]) ||
           is_delimiter(bytes_[at_ + 2]))) {
        at_ += 2;
        return;
      }
      ++at_;
    }
    at_ = bytes_.size();
  }

  [[nodiscard]] bool broken() const noexcept { return broken_; }

 private:
  [[nodiscard]] std::uint8_t peek(std::size_t ahead) const noexcept {
    return at_ + ahead < bytes_.size() ? bytes_[at_ + ahead] : 0U;
  }

  [[nodiscard]] bool fail() noexcept {
    broken_ = true;
    return false;
  }

  void skip_space() noexcept {
    while (at_ < bytes_.size()) {
      const std::uint8_t c = bytes_[at_];
      if (is_space(c)) {
        ++at_;
      } else if (c == '%') {
        while (at_ < bytes_.size() && bytes_[at_] != 0x0AU &&
               bytes_[at_] != 0x0DU) {
          ++at_;
        }
      } else {
        return;
      }
    }
  }

  /// A literal string, the opening parenthesis already consumed.
  [[nodiscard]] bool literal(std::string& out) {
    int depth = 1;
    while (at_ < bytes_.size()) {
      const std::uint8_t c = bytes_[at_++];
      if (c == '\\') {
        if (at_ >= bytes_.size()) {
          return fail();
        }
        const std::uint8_t e = bytes_[at_++];
        switch (e) {
          case 'n':
            out.push_back('\n');
            break;
          case 'r':
            out.push_back('\r');
            break;
          case 't':
            out.push_back('\t');
            break;
          case 'b':
            out.push_back('\b');
            break;
          case 'f':
            out.push_back('\f');
            break;
          case 0x0DU:
            // A backslash at the end of a line continues the string on
            // the next and stands for nothing.
            if (at_ < bytes_.size() && bytes_[at_] == 0x0AU) {
              ++at_;
            }
            break;
          case 0x0AU:
            break;
          default:
            if (e >= '0' && e <= '7') {
              unsigned value = e - '0';
              for (int more = 0; more < 2 && at_ < bytes_.size() &&
                                 bytes_[at_] >= '0' && bytes_[at_] <= '7';
                   ++more) {
                value = (value * 8U) + (bytes_[at_++] - '0');
              }
              out.push_back(static_cast<char>(value & 0xFFU));
            } else {
              // `\(`, `\)`, `\\` — and any other character, which a
              // backslash in front of does nothing to.
              out.push_back(static_cast<char>(e));
            }
            break;
        }
        continue;
      }
      if (c == '(') {
        ++depth;
      } else if (c == ')') {
        if (--depth == 0) {
          return true;
        }
      }
      out.push_back(static_cast<char>(c));
    }
    return fail();
  }

  /// A hex string, the `<` already consumed. An odd digit out is the
  /// high half of a last byte whose low half is zero.
  [[nodiscard]] bool hex(std::string& out) {
    int high = -1;
    while (at_ < bytes_.size()) {
      const std::uint8_t c = bytes_[at_++];
      if (c == '>') {
        if (high >= 0) {
          out.push_back(static_cast<char>(high << 4));
        }
        return true;
      }
      if (is_space(c)) {
        continue;
      }
      const int value = hex_value(c);
      if (value < 0) {
        return fail();
      }
      if (high < 0) {
        high = value;
      } else {
        out.push_back(static_cast<char>((high << 4) | value));
        high = -1;
      }
    }
    return fail();
  }

  /// A name, the slash already consumed, with `#xx` escapes undone.
  void name(std::string& out) {
    while (at_ < bytes_.size() && !is_space(bytes_[at_]) &&
           !is_delimiter(bytes_[at_])) {
      const std::uint8_t c = bytes_[at_++];
      if (c == '#' && at_ + 1 < bytes_.size() && hex_value(bytes_[at_]) >= 0 &&
          hex_value(bytes_[at_ + 1]) >= 0) {
        out.push_back(static_cast<char>((hex_value(bytes_[at_]) << 4) |
                                        hex_value(bytes_[at_ + 1])));
        at_ += 2;
        continue;
      }
      out.push_back(static_cast<char>(c));
    }
  }

  std::span<const std::uint8_t> bytes_;
  std::size_t at_{0};
  bool broken_{false};
};

// --- code points -------------------------------------------------------

/// A Cyrillic letter that is drawn as a Latin one, and the Latin letter
/// (step 6 of journal_text.h). Only letters with an identical twin: a
/// Cyrillic letter with no Latin double stays what it is.
[[nodiscard]] char32_t fold_look_alike(char32_t c) noexcept {
  struct pair {
    char32_t from;
    char32_t to;
  };
  static constexpr std::array<pair, 22> twins{{
      {.from = U'\u0405', .to = U'S'}, {.from = U'\u0406', .to = U'I'},
      {.from = U'\u0408', .to = U'J'}, {.from = U'\u0410', .to = U'A'},
      {.from = U'\u0412', .to = U'B'}, {.from = U'\u0415', .to = U'E'},
      {.from = U'\u041A', .to = U'K'}, {.from = U'\u041C', .to = U'M'},
      {.from = U'\u041D', .to = U'H'}, {.from = U'\u041E', .to = U'O'},
      {.from = U'\u0420', .to = U'P'}, {.from = U'\u0421', .to = U'C'},
      {.from = U'\u0422', .to = U'T'}, {.from = U'\u0425', .to = U'X'},
      {.from = U'\u0430', .to = U'a'}, {.from = U'\u0435', .to = U'e'},
      {.from = U'\u043E', .to = U'o'}, {.from = U'\u0440', .to = U'p'},
      {.from = U'\u0441', .to = U'c'}, {.from = U'\u0443', .to = U'y'},
      {.from = U'\u0445', .to = U'x'}, {.from = U'\u0455', .to = U's'},
  }};
  for (const pair& twin : twins) {
    if (twin.from == c) {
      return twin.to;
    }
  }
  return c;
}

void append_utf8(std::string& out, char32_t c) {
  const auto value = static_cast<std::uint32_t>(c);
  if (value < 0x80U) {
    out.push_back(static_cast<char>(value));
  } else if (value < 0x800U) {
    out.push_back(static_cast<char>(0xC0U | (value >> 6U)));
    out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  } else if (value < 0x10000U) {
    out.push_back(static_cast<char>(0xE0U | (value >> 12U)));
    out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  } else {
    out.push_back(static_cast<char>(0xF0U | (value >> 18U)));
    out.push_back(static_cast<char>(0x80U | ((value >> 12U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | ((value >> 6U) & 0x3FU)));
    out.push_back(static_cast<char>(0x80U | (value & 0x3FU)));
  }
}

/// UTF-16BE bytes as UTF-8, folded. False for an odd length or a lone
/// surrogate, which is a CMap that is not one.
[[nodiscard]] bool utf16_to_utf8(std::string_view utf16, std::string& out) {
  if (utf16.size() % 2U != 0U) {
    return false;
  }
  for (std::size_t at = 0; at < utf16.size(); at += 2U) {
    const auto unit = static_cast<std::uint32_t>(
        (static_cast<std::uint8_t>(utf16[at]) << 8U) |
        static_cast<std::uint8_t>(utf16[at + 1U]));
    if (unit >= 0xD800U && unit <= 0xDBFFU) {
      if (at + 3U >= utf16.size()) {
        return false;
      }
      const auto low = static_cast<std::uint32_t>(
          (static_cast<std::uint8_t>(utf16[at + 2U]) << 8U) |
          static_cast<std::uint8_t>(utf16[at + 3U]));
      if (low < 0xDC00U || low > 0xDFFFU) {
        return false;
      }
      append_utf8(out,
                  static_cast<char32_t>(0x10000U + ((unit - 0xD800U) << 10U) +
                                        (low - 0xDC00U)));
      at += 2U;
      continue;
    }
    if (unit >= 0xDC00U && unit <= 0xDFFFU) {
      return false;
    }
    append_utf8(out, fold_look_alike(static_cast<char32_t>(unit)));
  }
  return true;
}

/// A source code of a CMap entry as a number.
[[nodiscard]] std::uint32_t code_of(std::string_view bytes) noexcept {
  std::uint32_t value = 0;
  for (const char c : bytes) {
    value = (value << 8U) | static_cast<std::uint8_t>(c);
  }
  return value;
}

/// One code's destination.
[[nodiscard]] bool map_code(journal_font_map& out, std::uint32_t code,
                            std::string_view utf16) {
  if (code > 0xFFU) {
    // A code only a composite font has; nothing here reads one.
    return true;
  }
  std::string text{};
  if (!utf16_to_utf8(utf16, text)) {
    return false;
  }
  out.text.at(code) = std::move(text);
  out.mapped.at(code) = true;
  return true;
}

/// The same destination one code further on: the last UTF-16 unit
/// counted up, which is what a `bfrange` with one destination means.
[[nodiscard]] std::string next_destination(std::string_view utf16,
                                           std::uint32_t step) {
  std::string out(utf16);
  if (out.size() < 2U) {
    return out;
  }
  const std::size_t last = out.size() - 2U;
  const std::uint32_t unit = ((static_cast<std::uint8_t>(out[last]) << 8U) |
                              static_cast<std::uint8_t>(out[last + 1U])) +
                             step;
  out[last] = static_cast<char>((unit >> 8U) & 0xFFU);
  out[last + 1U] = static_cast<char>(unit & 0xFFU);
  return out;
}

// --- the content stream ------------------------------------------------

/// An affine matrix the PDF way: `[a b c d e f]`, a row vector times it.
struct matrix {
  double a{1.0};
  double b{0.0};
  double c{0.0};
  double d{1.0};
  double e{0.0};
  double f{0.0};
};

/// `left` then `right`: what `cm` and the text rendering matrix both do.
[[nodiscard]] matrix times(const matrix& left, const matrix& right) noexcept {
  return {.a = (left.a * right.a) + (left.b * right.c),
          .b = (left.a * right.b) + (left.b * right.d),
          .c = (left.c * right.a) + (left.d * right.c),
          .d = (left.c * right.b) + (left.d * right.d),
          .e = (left.e * right.a) + (left.f * right.c) + right.e,
          .f = (left.e * right.b) + (left.f * right.d) + right.f};
}

[[nodiscard]] matrix translation(double x, double y) noexcept {
  return {.e = x, .f = y};
}

/// The interpreter's state: the graphics state's matrix and its stack,
/// and the text state this route reads.
struct text_state {
  matrix ctm;
  std::vector<matrix> saved{};
  matrix tm;
  matrix tlm;
  double leading{0.0};
  double rise{0.0};
  std::string font{};
};

/// The last `count` operands as numbers, or false.
[[nodiscard]] bool numbers(const std::vector<token>& operands,
                           std::size_t count, std::array<double, 6>& out) {
  if (operands.size() < count) {
    return false;
  }
  const std::size_t first = operands.size() - count;
  for (std::size_t i = 0; i < count; ++i) {
    if (operands[first + i].kind != token_kind::number) {
      return false;
    }
    out.at(i) = operands[first + i].number;
  }
  return true;
}

void next_line(text_state& state) {
  state.tlm = times(translation(0.0, -state.leading), state.tlm);
  state.tm = state.tlm;
}

void show(const text_state& state, std::string_view codes,
          std::vector<journal_text_run>& out) {
  const matrix at =
      times(times(translation(0.0, state.rise), state.tm), state.ctm);
  journal_text_run run;
  run.x = at.e;
  run.y = at.f;
  run.font = state.font;
  run.codes.assign(codes.begin(), codes.end());
  out.push_back(std::move(run));
}

/// One operator, with the operands gathered before it.
void interpret(std::string_view op, const std::vector<token>& operands,
               text_state& state, std::vector<journal_text_run>& out) {
  std::array<double, 6> n{};
  if (op == "q") {
    state.saved.push_back(state.ctm);
  } else if (op == "Q") {
    if (!state.saved.empty()) {
      state.ctm = state.saved.back();
      state.saved.pop_back();
    }
  } else if (op == "cm") {
    if (numbers(operands, 6, n)) {
      state.ctm = times(
          {.a = n[0], .b = n[1], .c = n[2], .d = n[3], .e = n[4], .f = n[5]},
          state.ctm);
    }
  } else if (op == "BT") {
    state.tm = matrix{};
    state.tlm = matrix{};
  } else if (op == "Tm") {
    if (numbers(operands, 6, n)) {
      state.tm = {
          .a = n[0], .b = n[1], .c = n[2], .d = n[3], .e = n[4], .f = n[5]};
      state.tlm = state.tm;
    }
  } else if (op == "Td") {
    if (numbers(operands, 2, n)) {
      state.tlm = times(translation(n[0], n[1]), state.tlm);
      state.tm = state.tlm;
    }
  } else if (op == "TD") {
    if (numbers(operands, 2, n)) {
      state.leading = -n[1];
      state.tlm = times(translation(n[0], n[1]), state.tlm);
      state.tm = state.tlm;
    }
  } else if (op == "T*") {
    next_line(state);
  } else if (op == "TL") {
    if (numbers(operands, 1, n)) {
      state.leading = n[0];
    }
  } else if (op == "Ts") {
    if (numbers(operands, 1, n)) {
      state.rise = n[0];
    }
  } else if (op == "Tf") {
    if (operands.size() >= 2 &&
        operands[operands.size() - 2].kind == token_kind::name) {
      state.font = operands[operands.size() - 2].bytes;
    }
  } else if (op == "Tj" || op == "'" || op == "\"") {
    if (op != "Tj") {
      next_line(state);
    }
    if (!operands.empty() && operands.back().kind == token_kind::string) {
      show(state, operands.back().bytes, out);
    }
  } else if (op == "TJ") {
    // The strings of the array, run together: the numbers between them
    // are kerning, and this edition's widest is a twentieth of an em.
    std::string codes;
    bool any = false;
    for (const token& item : operands) {
      if (item.kind == token_kind::string) {
        codes += item.bytes;
        any = true;
      }
    }
    if (any) {
      show(state, codes, out);
    }
  }
}

// --- from runs to text -------------------------------------------------

struct placed_run {
  double x{};
  double y{};
  std::size_t order{};
  std::string text{};
};

struct line {
  double y{};
  std::vector<placed_run> runs{};
};

[[nodiscard]] bool inside(const journal_text_box& box, double x,
                          double y) noexcept {
  return x >= box.left && x < box.right && y >= box.bottom && y < box.top;
}

/// Runs of spaces as one, and none at either end.
[[nodiscard]] std::string tidy(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == ' ' && (out.empty() || out.back() == ' ')) {
      continue;
    }
    out.push_back(c);
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

[[nodiscard]] bool blank(std::string_view text) noexcept {
  return std::ranges::all_of(text, [](char c) { return c == ' '; });
}

/// One page's content, read, and its fonts' maps.
struct read_page {
  std::uint16_t number{};
  std::vector<journal_text_run> runs{};
  std::vector<std::pair<std::string_view, journal_font_map>> fonts;
};

[[nodiscard]] journal_trouble read_page_facts(
    std::span<const std::uint8_t> document, const journal_text_page& page,
    read_page& out) {
  out.number = page.page;
  std::vector<std::uint8_t> content;
  if (const journal_trouble why =
          decode_stream_at(document, page.offset, page.length, page.filter,
                           page.decoded, content);
      why != journal_trouble::none) {
    return why;
  }
  if (const journal_trouble why = read_text_runs(content, out.runs);
      why != journal_trouble::none) {
    return why;
  }
  for (const journal_text_font& font : page.fonts) {
    std::vector<std::uint8_t> cmap;
    if (const journal_trouble why =
            decode_stream_at(document, font.offset, font.length, font.filter,
                             font.decoded, cmap);
        why != journal_trouble::none) {
      return why;
    }
    journal_font_map map;
    if (const journal_trouble why = read_to_unicode(cmap, map);
        why != journal_trouble::none) {
      return why;
    }
    out.fonts.emplace_back(font.resource, std::move(map));
  }
  return journal_trouble::none;
}

/// A run's codes through its font's map, or false for a font or a code
/// the facts say nothing about.
[[nodiscard]] bool decode_run(const read_page& page,
                              const journal_text_run& run, std::string& out) {
  const auto font = std::ranges::find_if(
      page.fonts, [&](const auto& one) { return one.first == run.font; });
  if (font == page.fonts.end()) {
    return false;
  }
  out.clear();
  for (const std::uint8_t code : run.codes) {
    if (!font->second.mapped.at(code)) {
      return false;
    }
    out += font->second.text.at(code);
  }
  return true;
}

/// Everything inside `box` on `page`, as lines top to bottom.
[[nodiscard]] journal_trouble lines_in(const read_page& page,
                                       const journal_text_box& box,
                                       std::vector<line>& out) {
  out.clear();
  std::vector<placed_run> kept;
  for (std::size_t i = 0; i < page.runs.size(); ++i) {
    const journal_text_run& run = page.runs[i];
    if (!inside(box, run.x, run.y)) {
      continue;
    }
    placed_run placed{.x = run.x, .y = run.y, .order = i};
    if (!decode_run(page, run, placed.text)) {
      return journal_trouble::text_unreadable;
    }
    kept.push_back(std::move(placed));
  }
  // Top to bottom, and stream order among equals.
  std::ranges::stable_sort(
      kept, [](const placed_run& a, const placed_run& b) { return a.y > b.y; });
  constexpr double same_line = 0.5;
  for (placed_run& run : kept) {
    if (out.empty() || out.back().y - run.y > same_line) {
      out.push_back(line{.y = run.y, .runs = {}});
    }
    out.back().runs.push_back(std::move(run));
  }
  for (line& one : out) {
    std::ranges::stable_sort(one.runs,
                             [](const placed_run& a, const placed_run& b) {
                               if (a.x != b.x) {
                                 return a.x < b.x;
                               }
                               return a.order < b.order;
                             });
  }
  return journal_trouble::none;
}

/// The paragraphs being built, and how the last line ended.
struct assembly {
  std::vector<std::string> paragraphs{};
  /// The last line ended in a hyphen this route dropped, or in one the
  /// writer put there: either way the next line joins with no space.
  bool joins{false};

  void add(const line& one, double margin) {
    std::size_t count = one.runs.size();
    bool discretionary = false;
    if (count > 1U && one.runs[count - 1U].text == "-") {
      discretionary = true;
      --count;
    }
    std::string text;
    for (std::size_t i = 0; i < count; ++i) {
      text += one.runs[i].text;
    }
    if (blank(text)) {
      return;
    }
    const bool indented = one.runs.front().x > margin + journal_text_indent;
    if (paragraphs.empty() || indented) {
      paragraphs.push_back(std::move(text));
    } else {
      std::string& last = paragraphs.back();
      if (!joins && !last.empty() && last.back() != ' ') {
        last.push_back(' ');
      }
      last += text;
    }
    const std::string& now = paragraphs.back();
    joins = discretionary || (!now.empty() && now.back() == '-');
  }

  [[nodiscard]] std::string finish() const {
    std::string out;
    for (const std::string& paragraph : paragraphs) {
      std::string tidied = tidy(paragraph);
      if (tidied.empty()) {
        continue;
      }
      if (!out.empty()) {
        out += "\n\n";
      }
      out += tidied;
    }
    return out;
  }
};

}  // namespace

journal_trouble read_to_unicode(std::span<const std::uint8_t> cmap,
                                journal_font_map& out) {
  out = journal_font_map{};
  tokenizer words(cmap);
  token here;
  std::vector<token> pending;
  enum class section : std::uint8_t { none, bfchar, bfrange };
  section in = section::none;
  bool seen_any = false;
  while (words.next(here)) {
    if (here.kind == token_kind::keyword) {
      if (here.bytes == "beginbfchar") {
        in = section::bfchar;
        pending.clear();
        seen_any = true;
        continue;
      }
      if (here.bytes == "beginbfrange") {
        in = section::bfrange;
        pending.clear();
        seen_any = true;
        continue;
      }
      if (here.bytes == "endbfchar" || here.bytes == "endbfrange") {
        in = section::none;
        continue;
      }
    }
    if (in == section::none) {
      continue;
    }
    if (in == section::bfchar) {
      pending.push_back(here);
      if (pending.size() == 2U) {
        if (pending[0].kind != token_kind::string ||
            pending[1].kind != token_kind::string ||
            !map_code(out, code_of(pending[0].bytes), pending[1].bytes)) {
          return journal_trouble::text_unreadable;
        }
        pending.clear();
      }
      continue;
    }
    // A bfrange: <lo> <hi> <dst>, or <lo> <hi> [ <dst> ... ].
    if (pending.size() < 2U) {
      if (here.kind != token_kind::string) {
        return journal_trouble::text_unreadable;
      }
      pending.push_back(here);
      continue;
    }
    const std::uint32_t low = code_of(pending[0].bytes);
    const std::uint32_t high = code_of(pending[1].bytes);
    if (high < low || high - low > 0xFFFFU) {
      return journal_trouble::text_unreadable;
    }
    if (here.kind == token_kind::string) {
      for (std::uint32_t code = low; code <= high; ++code) {
        if (!map_code(out, code, next_destination(here.bytes, code - low))) {
          return journal_trouble::text_unreadable;
        }
      }
    } else if (here.kind == token_kind::open_array) {
      std::uint32_t code = low;
      token item;
      while (words.next(item) && item.kind != token_kind::close_array) {
        if (item.kind != token_kind::string ||
            !map_code(out, code, item.bytes)) {
          return journal_trouble::text_unreadable;
        }
        ++code;
      }
    } else {
      return journal_trouble::text_unreadable;
    }
    pending.clear();
  }
  if (words.broken() || !seen_any) {
    return journal_trouble::text_unreadable;
  }
  return journal_trouble::none;
}

journal_trouble read_text_runs(std::span<const std::uint8_t> content,
                               std::vector<journal_text_run>& out) {
  out.clear();
  tokenizer words(content);
  token here;
  std::vector<token> operands;
  text_state state;
  int array_depth = 0;
  while (words.next(here)) {
    switch (here.kind) {
      case token_kind::open_array:
        ++array_depth;
        break;
      case token_kind::close_array:
        --array_depth;
        break;
      case token_kind::keyword:
        if (array_depth == 0) {
          if (here.bytes == "ID") {
            words.skip_inline_image();
          } else {
            interpret(here.bytes, operands, state, out);
          }
          operands.clear();
          continue;
        }
        break;
      default:
        break;
    }
    operands.push_back(std::move(here));
    here = token{};
  }
  if (words.broken()) {
    out.clear();
    return journal_trouble::text_unreadable;
  }
  return journal_trouble::none;
}

journal_trouble read_text_fragments(
    std::span<const std::uint8_t> document, const journal_edition& edition,
    std::span<const journal_text_fragment> fragments, std::string& out) {
  out.clear();
  if (fragments.empty()) {
    return journal_trouble::no_such_entry;
  }
  assembly built;
  read_page page;
  bool have_page = false;
  std::vector<line> lines;
  for (const journal_text_fragment& fragment : fragments) {
    if (!have_page || page.number != fragment.page) {
      const auto facts = std::ranges::find_if(
          edition.pages, [&](const journal_text_page& one) {
            return one.page == fragment.page;
          });
      if (facts == edition.pages.end()) {
        return journal_trouble::no_such_entry;
      }
      page = read_page{};
      if (const journal_trouble why = read_page_facts(document, *facts, page);
          why != journal_trouble::none) {
        return why;
      }
      have_page = true;
    }
    if (const journal_trouble why = lines_in(page, fragment.box, lines);
        why != journal_trouble::none) {
      return why;
    }
    for (const line& one : lines) {
      built.add(one, fragment.box.left);
    }
  }
  out = built.finish();
  return journal_trouble::none;
}

journal_trouble read_item_text(std::span<const std::uint8_t> document,
                               const journal_edition& edition,
                               const journal_entry_fact& fact,
                               std::string& out) {
  if (const journal_trouble why =
          read_text_fragments(document, edition, fact.text, out);
      why != journal_trouble::none) {
    out.clear();
    return why;
  }
  const sha256_digest digest = sha256(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(out.data()), out.size()));
  if (!machine::digest_is(digest, fact.text_sha256)) {
    out.clear();
    return journal_trouble::text_mismatch;
  }
  return journal_trouble::none;
}

}  // namespace amberfolio::host
