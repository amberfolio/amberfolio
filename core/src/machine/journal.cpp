// SPDX-License-Identifier: AGPL-3.0-only
//
// `journal_state` and the citation recognizer (journal.h, M5-E4 #175).
//
// Nothing here reads the machine: this object is handed things — by a
// host, through `deliver()`, and by the seam, through `note_drawn_text()`
// — and remembers them. That is what makes the recognizer testable
// against strings a test writes rather than only against a program.

#include "amberfolio/machine/journal.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace amberfolio::machine {
namespace {

/// How a section numbers what it holds, which is the notation a citation
/// of it is written in.
///
/// The one printed journal this build has a fact table for numbers its
/// entries and its tales in decimal and its proclamations in Roman
/// numerals, and the program cites each the way the booklet prints it.
/// That is the fact the first driven citation established (#232): at the
/// city hall the program names four proclamations in one sentence, every
/// one of them a Roman numeral, and a recognizer that wanted digits saw
/// nothing at all.
enum class citation_notation : std::uint8_t { decimal, roman };

/// One word a citation is built on.
struct citation_word {
  std::string_view word;
  journal_kind kind;
  citation_notation notation;
  /// A plural names several at once, and a list may follow it: "entries
  /// 23 and 14", "proclamations LXIV, LXXVIII, CIX, and LIX". A singular
  /// names one, and the number after it is the whole citation — anything
  /// after that number is the next sentence's business.
  bool plural;
};

/// The words each of the journal's numbered sections is cited by.
///
/// **The section's own word and not the book's.** The word this
/// enhancement is named after appears in a citation as often as not
/// ("in your journal you note...") and is no part of the citation's
/// shape: what names the section is `ENTRY`, `TALE` or `PROCLAMATION`,
/// each with its plural, and the number belongs to whichever of those it
/// follows. `TALE` rather than `TAVERN TALE` because it is the word that
/// is certainly there, and a recognizer that insisted on both would miss
/// a program that printed only one of them.
///
/// **A plural before its singular**, because the scan takes the first
/// word in this table that matches at a position and a plural is its
/// singular with a letter on the end. Nothing else about the order is
/// load-bearing.
constexpr std::array<citation_word, 6> citation_words{{
    {.word = "ENTRIES",
     .kind = journal_kind::entry,
     .notation = citation_notation::decimal,
     .plural = true},
    {.word = "ENTRY",
     .kind = journal_kind::entry,
     .notation = citation_notation::decimal,
     .plural = false},
    {.word = "TALES",
     .kind = journal_kind::tale,
     .notation = citation_notation::decimal,
     .plural = true},
    {.word = "TALE",
     .kind = journal_kind::tale,
     .notation = citation_notation::decimal,
     .plural = false},
    {.word = "PROCLAMATIONS",
     .kind = journal_kind::proclamation,
     .notation = citation_notation::roman,
     .plural = true},
    {.word = "PROCLAMATION",
     .kind = journal_kind::proclamation,
     .notation = citation_notation::roman,
     .plural = false},
}};

/// How far past a decimal word its number may be. Twelve characters is
/// enough for a second word and the punctuation around it, and short
/// enough that a number in the *next* sentence is not a citation. A Roman
/// numeral has no reach: it must be the next word, because any word
/// spelled out of the seven Roman letters would otherwise be one.
constexpr unsigned citation_reach = 12;

/// The largest entry number a decimal citation may name. Four digits,
/// which is what the prompt takes (journal.h), and a run of more than
/// that is not an entry number — it is a year, a coin count, or a number
/// that has run into the one before it. A Roman numeral's own grammar
/// caps it lower than this.
constexpr unsigned citation_max_digits = 4;

[[nodiscard]] constexpr bool is_digit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool is_letter(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

/// One character, as the window keeps it: upper case for a letter,
/// itself for a digit or a comma, and a space for everything else.
///
/// The comma stays because it is how a list says it is not finished. A
/// sentence wrapped across two lines of the message panel reaches the
/// watch as two draws, and where the wrap falls inside a list of
/// proclamations the first draw ends in a comma or an "and": the watch
/// holds its answer until the rest arrives rather than opening the first
/// three of four (`journal_citations_in()`).
[[nodiscard]] constexpr char normalized(char ch) noexcept {
  if (ch >= 'a' && ch <= 'z') {
    return static_cast<char>(ch - ('a' - 'A'));
  }
  if (is_letter(ch) || is_digit(ch) || ch == ',') {
    return ch;
  }
  return ' ';
}

/// The value of a canonical Roman numeral, or zero when `run` is not one.
///
/// Canonical means the form a printed booklet sets — up to three of a
/// symbol in a row, the six subtractive pairs and nothing else — checked
/// against the whole run: a word that merely happens to be spelled out of
/// the seven letters (`CIVIL`, `DID`, `MILD`) is not a number, and a run
/// that parses only as far as its first mistake is not one either. The
/// grammar caps what it can say at 3999.
[[nodiscard]] constexpr unsigned roman_value(std::string_view run) noexcept {
  std::size_t at = 0;
  unsigned value = 0;
  const auto take = [&run, &at](std::string_view symbol) noexcept {
    if (run.substr(at, symbol.size()) != symbol) {
      return false;
    }
    at += symbol.size();
    return true;
  };
  for (unsigned n = 0; n < 3 && take("M"); ++n) {
    value += 1000;
  }
  if (take("CM")) {
    value += 900;
  } else if (take("CD")) {
    value += 400;
  } else {
    if (take("D")) {
      value += 500;
    }
    for (unsigned n = 0; n < 3 && take("C"); ++n) {
      value += 100;
    }
  }
  if (take("XC")) {
    value += 90;
  } else if (take("XL")) {
    value += 40;
  } else {
    if (take("L")) {
      value += 50;
    }
    for (unsigned n = 0; n < 3 && take("X"); ++n) {
      value += 10;
    }
  }
  if (take("IX")) {
    value += 9;
  } else if (take("IV")) {
    value += 4;
  } else {
    if (take("V")) {
      value += 5;
    }
    for (unsigned n = 0; n < 3 && take("I"); ++n) {
      value += 1;
    }
  }
  return at == run.size() ? value : 0;
}

/// A number read at `at`: its value, or zero for none there, and where
/// it ended.
struct number_read {
  unsigned value{};
  std::size_t end{};
};

/// A decimal number starting exactly at `at`.
[[nodiscard]] constexpr number_read read_decimal(std::string_view text,
                                                 std::size_t at) noexcept {
  unsigned digits = 0;
  unsigned value = 0;
  std::size_t p = at;
  while (p < text.size() && is_digit(text[p]) &&
         digits <= citation_max_digits) {
    value = (value * 10U) + static_cast<unsigned>(text[p] - '0');
    ++digits;
    ++p;
  }
  if (digits == 0 || digits > citation_max_digits || value == 0) {
    // Zero is not an entry, and a longer run of digits is not an entry
    // number.
    return {};
  }
  return {.value = value, .end = p};
}

/// A Roman numeral starting exactly at `at`: the run of letters there,
/// whole, and only if all of it is a canonical numeral.
[[nodiscard]] constexpr number_read read_roman(std::string_view text,
                                               std::size_t at) noexcept {
  std::size_t p = at;
  while (p < text.size() && is_letter(text[p])) {
    ++p;
  }
  const unsigned value = roman_value(text.substr(at, p - at));
  if (value == 0) {
    return {};
  }
  return {.value = value, .end = p};
}

[[nodiscard]] constexpr number_read read_number(
    std::string_view text, std::size_t at,
    citation_notation notation) noexcept {
  return notation == citation_notation::roman ? read_roman(text, at)
                                              : read_decimal(text, at);
}

/// Whether `text` has the whole word `AND` at `at`.
[[nodiscard]] constexpr bool and_at(std::string_view text,
                                    std::size_t at) noexcept {
  return text.substr(at, 3) == "AND" &&
         (at + 3 == text.size() || !is_letter(text[at + 3]));
}

/// What one code point is drawn as. Empty for nothing this build knows,
/// which the caller turns into the substitute.
///
/// The pairs are what an OCR engine actually produces, measured on a real
/// ingestion rather than guessed: the four quotation marks are ninety-seven
/// in a hundred of it, the dashes are most of the rest. The neighbours of
/// each are here too, because a table that handled the left quote and not
/// the right one would be a table waiting to be surprised by a different
/// page.
[[nodiscard]] std::string_view drawn_as(std::uint32_t code) noexcept {
  switch (code) {
    case 0x2018:  // ' and its family
    case 0x2019:
    case 0x201A:
    case 0x201B:
    case 0x2032:
    case 0x00B4:
      return "'";
    case 0x201C:  // " and its family
    case 0x201D:
    case 0x201E:
    case 0x201F:
    case 0x2033:
    case 0x00AB:
    case 0x00BB:
      return "\"";
    case 0x2010:  // the dashes, which the program's font has one of
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2212:
      return "-";
    case 0x2026:  // an ellipsis is three stops, and reads as three
      return "...";
    case 0x00A0:  // a space that is not one
    case 0x2007:
    case 0x2009:
    case 0x202F:
      return " ";
    default:
      return {};
  }
}

/// The substitute: what a code point with no glyph looks like.
constexpr char no_glyph = '?';

}  // namespace

journal_drawn journal_drawable(std::string_view text,
                               std::span<char> into) noexcept {
  journal_drawn out;
  std::size_t at = 0;
  while (at < text.size()) {
    const auto lead = static_cast<std::uint8_t>(text[at]);

    // How many bytes this code point claims, and what it is worth. An
    // ill-formed lead, a sequence that runs off the end, or a continuation
    // byte that is not one, all fall back to a single substituted byte -
    // and always consume exactly one, so nothing here can fail to advance.
    std::size_t width = 1;
    std::uint32_t code = lead;
    if (lead >= 0xC2 && lead <= 0xDF) {
      width = 2;
      code = lead & 0x1FU;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
      width = 3;
      code = lead & 0x0FU;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
      width = 4;
      code = lead & 0x07U;
    } else if (lead >= 0x80) {
      width = 1;
      code = 0xFFFFFFFFU;  // a stray continuation or an ill-formed lead
    }
    if (width > 1) {
      if (at + width > text.size()) {
        width = 1;
        code = 0xFFFFFFFFU;
      } else {
        for (std::size_t i = 1; i < width; ++i) {
          const auto next = static_cast<std::uint8_t>(text[at + i]);
          if ((next & 0xC0U) != 0x80U) {
            width = 1;
            code = 0xFFFFFFFFU;
            break;
          }
          code = (code << 6U) | (next & 0x3FU);
        }
      }
    }

    // What it draws as. Printable ASCII is itself; a newline is kept
    // because the layout above reads it; every other control character is
    // a space, which is what a stray one in a transcription means.
    std::array<char, 3> one{};
    std::string_view piece;
    if (code == '\n') {
      one[0] = '\n';
      piece = std::string_view{one.data(), 1};
    } else if (code >= 0x20 && code < 0x7F) {
      one[0] = static_cast<char>(code);
      piece = std::string_view{one.data(), 1};
    } else if (code < 0x20 || code == 0x7F) {
      one[0] = ' ';
      piece = std::string_view{one.data(), 1};
    } else if (const std::string_view known = drawn_as(code); !known.empty()) {
      piece = known;
    } else {
      one[0] = no_glyph;
      piece = std::string_view{one.data(), 1};
    }

    if (out.written + piece.size() > into.size()) {
      out.complete = false;
      return out;
    }
    for (const char ch : piece) {
      into[out.written++] = ch;
    }
    at += width;
  }
  return out;
}

std::size_t journal_citations_in(std::string_view text,
                                 std::span<journal_citation> out) noexcept {
  std::size_t count = 0;
  const auto emit = [&out, &count](journal_kind kind, unsigned value) noexcept {
    const journal_citation one{.kind = kind,
                               .number = static_cast<std::uint16_t>(value)};
    // Once per drawing: a sentence that names the same number twice has
    // named it once.
    for (std::size_t i = 0; i < count; ++i) {
      if (out[i] == one) {
        return;
      }
    }
    if (count < out.size()) {
      out[count++] = one;
    }
  };

  // Position outermost, word innermost: the answer is in the order the
  // text names things, whichever section each names, which is what keeps
  // a later sentence from outranking the one being drawn.
  for (std::size_t at = 0; at < text.size(); ++at) {
    for (const citation_word& w : citation_words) {
      if (text.substr(at, w.word.size()) != w.word) {
        continue;
      }
      // A word and not the tail of a longer one.
      if (at > 0 && is_letter(text[at - 1])) {
        continue;
      }
      std::size_t p = at + w.word.size();

      number_read first{};
      if (w.notation == citation_notation::decimal) {
        // A word and not the head of a longer one either: an entryway is
        // not an entry and a talent is not a tale.
        if (p < text.size() && is_letter(text[p])) {
          continue;
        }
        const std::size_t limit = std::min(text.size(), p + citation_reach);
        while (p < limit && !is_digit(text[p])) {
          ++p;
        }
        if (p >= limit) {
          continue;
        }
        first = read_decimal(text, p);
      } else {
        // The numeral is the next word — or, when the program printed
        // the word and the numeral as two operands with nothing between
        // them, the rest of this one. Either way the letters after the
        // word have to *be* a numeral, which is also what makes the
        // plural-before-singular order in the table safe: `PROCLAMATIONS`
        // is not `PROCLAMATION` followed by the numeral `S`.
        while (p < text.size() && !is_letter(text[p]) && !is_digit(text[p])) {
          ++p;
        }
        first = read_roman(text, p);
      }
      if (first.value == 0) {
        // Fall through to the next occurrence of a word rather than
        // answering, because a second citation later in the same window
        // is still a citation.
        continue;
      }
      emit(w.kind, first.value);
      p = first.end;

      // After a plural, a list: numbers joined by commas and the word
      // "and", in the same notation. The list ends at the first thing
      // that is neither.
      if (w.plural) {
        for (;;) {
          std::size_t r = p;
          bool joined = false;
          for (;;) {
            while (r < text.size() && !is_letter(text[r]) &&
                   !is_digit(text[r])) {
              joined = joined || text[r] == ',';
              ++r;
            }
            if (!and_at(text, r)) {
              break;
            }
            joined = true;
            r += 3;
          }
          if (r >= text.size()) {
            if (joined) {
              // The list runs off the end of what has been drawn: a
              // comma or an "and" with nothing after it. The rest is on
              // its way, and an answer now would be three proclamations
              // of four. Nothing, until it arrives — and that is the
              // whole window's answer, because the window is emptied on
              // a match and the tail of this list would go with it.
              return 0;
            }
            break;
          }
          const number_read next = read_number(text, r, w.notation);
          if (next.value == 0) {
            break;
          }
          emit(w.kind, next.value);
          p = next.end;
        }
      }
      // Carry on after the citation, not inside it.
      at = p - 1;
      break;
    }
  }
  return count;
}

journal_citation journal_citation_in(std::string_view text) noexcept {
  std::array<journal_citation, journal_citations_at_once> found{};
  return journal_citations_in(text, found) == 0 ? journal_citation{} : found[0];
}

const char* journal_kind_name(journal_kind which) noexcept {
  switch (which) {
    case journal_kind::entry:
      return "entry";
    case journal_kind::tale:
      return "tale";
    case journal_kind::proclamation:
      return "proclamation";
  }
  return "entry";
}

bool journal_kind_from_name(std::string_view word, journal_kind& out) noexcept {
  for (std::size_t i = 0; i < journal_kinds; ++i) {
    const auto kind = static_cast<journal_kind>(i);
    if (word == journal_kind_name(kind)) {
      out = kind;
      return true;
    }
  }
  return false;
}

void journal_state::clear() noexcept {
  entry_ = {};
  delivery_ = journal_delivery::none;
  truncated_ = false;
  text_length_ = 0;
  cited_ = {};
  cited_count_ = 0;
  window_length_ = 0;
  mode_ = journal_reader_mode::closed;
  page_ = 0;
  page_count_ = 0;
  digit_count_ = 0;
  asked_kind_ = journal_kind::entry;
  seen_count_ = 0;
  seen_changed_ = false;
  list_cursor_ = 0;
  list_drawn_ = 0;
  on_screen_ = false;
  covered_ = false;
  drawn_signature_ = 0;
}

void journal_state::ask(journal_citation what) noexcept {
  entry_ = what;
  delivery_ = journal_delivery::no_host;
  truncated_ = false;
  text_length_ = 0;
}

void journal_state::deliver(std::string_view what) noexcept {
  // Made drawable on the way in (#219), so what this buffer holds is what
  // the panel can put on the screen: one glyph per code point, wrapping
  // that counts the right bytes, and no page cut in half through the
  // middle of a character.
  const journal_drawn drawn = journal_drawable(what, text_);
  text_length_ = drawn.written;
  truncated_ = !drawn.complete;
  delivery_ =
      drawn.written == 0 ? journal_delivery::no_text : journal_delivery::ready;
}

void journal_state::refuse(journal_delivery why) noexcept {
  text_length_ = 0;
  truncated_ = false;
  delivery_ = why;
}

journal_citation journal_state::note_drawn_text(
    std::string_view what) noexcept {
  // Into the window, normalized, with a space in front of it when there
  // is already something there: two strings the program drew are two
  // words, never one.
  std::array<char, journal_citation_window> incoming{};
  std::size_t length = 0;
  const auto append = [&incoming, &length](char ch) noexcept {
    if (ch == ' ' && (length == 0 || incoming[length - 1] == ' ')) {
      return;
    }
    if (length == journal_citation_window) {
      // Longer than the whole window on its own: keep the tail, which is
      // where a number after the word would be.
      for (std::size_t i = 1; i < journal_citation_window; ++i) {
        incoming[i - 1] = incoming[i];
      }
      --length;
    }
    incoming[length++] = ch;
  };
  for (const char ch : what) {
    append(normalized(ch));
  }
  if (length == 0) {
    return {};
  }
  // Two strings the program drew are two words, and the separator goes on
  // here rather than in front of the normalizer above — which would have
  // dropped it, since a leading space is exactly what that collapses.
  if (window_length_ != 0 && length < journal_citation_window) {
    for (std::size_t i = length; i > 0; --i) {
      incoming[i] = incoming[i - 1];
    }
    incoming[0] = ' ';
    ++length;
  }

  if (length >= journal_citation_window) {
    for (std::size_t i = 0; i < journal_citation_window; ++i) {
      window_[i] = incoming[i];
    }
    window_length_ = journal_citation_window;
  } else {
    const std::size_t room = journal_citation_window - length;
    if (window_length_ > room) {
      const std::size_t drop = window_length_ - room;
      for (std::size_t i = drop; i < window_length_; ++i) {
        window_[i - drop] = window_[i];
      }
      window_length_ = room;
    }
    for (std::size_t i = 0; i < length; ++i) {
      window_[window_length_ + i] = incoming[i];
    }
    window_length_ += length;
  }

  const std::size_t found = journal_citations_in(
      std::string_view{window_.data(), window_length_}, cited_all_);
  if (found == 0) {
    return {};
  }
  // One drawing of a citation opens one entry: the window is emptied so
  // the same characters cannot match again on the next string the program
  // draws beside them. Everything the drawing named is kept beside the
  // first, in the order it was named, for the log.
  cited_count_ = found;
  cited_ = cited_all_[0];
  window_length_ = 0;
  return cited_;
}

void journal_state::forget_citation() noexcept { window_length_ = 0; }

void journal_state::set_reader(journal_reader_mode mode) noexcept {
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  // Whatever is on the planes is not what this mode wants there.
  on_screen_ = false;
  drawn_signature_ = 0;
}

void journal_state::set_page(std::uint16_t page) noexcept {
  if (page_ == page) {
    return;
  }
  page_ = page;
  drawn_signature_ = 0;
}

bool journal_state::push_digit(char digit) noexcept {
  if (digit_count_ == journal_prompt_digits || !is_digit(digit)) {
    return false;
  }
  digits_[digit_count_++] = digit;
  drawn_signature_ = 0;
  return true;
}

void journal_state::pop_digit() noexcept {
  if (digit_count_ != 0) {
    --digit_count_;
    drawn_signature_ = 0;
  }
}

void journal_state::clear_digits() noexcept {
  if (digit_count_ != 0) {
    digit_count_ = 0;
    drawn_signature_ = 0;
  }
}

std::uint16_t journal_state::asked_entry() const noexcept {
  unsigned value = 0;
  for (std::size_t i = 0; i < digit_count_; ++i) {
    value = (value * 10U) + static_cast<unsigned>(digits_[i] - '0');
  }
  return static_cast<std::uint16_t>(value);
}

void journal_state::note_seen(journal_citation what, std::uint8_t month,
                              std::uint8_t day, std::uint8_t hour,
                              std::uint8_t minute) noexcept {
  if (!what) {
    return;
  }
  // Already there? Take it out, keeping what the player has done with it,
  // and let it go back on the front re-dated.
  bool was_read = false;
  for (std::size_t i = 0; i < seen_count_; ++i) {
    if (seen_[i].what == what) {
      was_read = seen_[i].read;
      for (std::size_t j = i; j + 1 < seen_count_; ++j) {
        seen_[j] = seen_[j + 1];
      }
      --seen_count_;
      break;
    }
  }
  if (seen_count_ == journal_log_rows) {
    --seen_count_;  // the oldest falls off the end
  }
  for (std::size_t i = seen_count_; i > 0; --i) {
    seen_[i] = seen_[i - 1];
  }
  seen_[0] = journal_seen_row{.what = what,
                              .month = month,
                              .day = day,
                              .hour = hour,
                              .minute = minute,
                              .read = was_read};
  ++seen_count_;
  seen_changed_ = true;
}

bool journal_state::mark_seen_read(journal_citation what) noexcept {
  for (std::size_t i = 0; i < seen_count_; ++i) {
    if (seen_[i].what == what) {
      if (!seen_[i].read) {
        seen_[i].read = true;
        seen_changed_ = true;
      }
      return true;
    }
  }
  return false;
}

void journal_state::clear_seen() noexcept {
  seen_count_ = 0;
  seen_changed_ = false;
  list_cursor_ = 0;
  list_drawn_ = 0;
}

void journal_state::move_list_cursor(int by) noexcept {
  if (seen_count_ == 0) {
    return;
  }
  // Stops at the ends rather than wrapping. A log is a list with a top
  // and a bottom, and a cursor that jumped from one to the other would
  // lose a player who was holding a key down.
  //
  // All of it in one type: the index is a `size_t` and the step is the
  // only signed thing here, so the step is what gets taken apart rather
  // than the index being carried through a signed round trip.
  const std::size_t last = seen_count_ - 1;
  std::size_t where = list_cursor();
  if (by < 0) {
    const auto back = static_cast<std::size_t>(-by);
    where = back > where ? 0 : where - back;
  } else {
    where += static_cast<std::size_t>(by);
    where = where > last ? last : where;
  }
  if (where != list_cursor_) {
    list_cursor_ = where;
    list_drawn_ = 0;
    drawn_signature_ = 0;
  }
}

void journal_state::cycle_asked_kind() noexcept {
  const auto next = static_cast<std::size_t>(asked_kind_) + 1U;
  asked_kind_ = static_cast<journal_kind>(next % journal_kinds);
  // The panel says which section it is pointed at, so it has to be drawn
  // again — the digits did not change and nothing else would notice.
  drawn_signature_ = 0;
}

void journal_state::set_covered(bool covered) noexcept {
  if (covered_ == covered) {
    return;
  }
  covered_ = covered;
  // Whichever way it went, the program has just painted on these cells: a
  // clear took them, or the roster came back over them. Either way what
  // this seam put there is gone, and the next arrival has to draw again
  // rather than compare a signature and decide it need not. The same rule
  // `automap_state::set_panel_covered()` follows, for the same reason.
  on_screen_ = false;
  drawn_signature_ = 0;
}

}  // namespace amberfolio::machine
