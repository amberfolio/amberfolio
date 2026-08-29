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
#include <string_view>

namespace amberfolio::machine {
namespace {

/// The two words the citation's shape is made of, and how far apart the
/// first of them and the number may be.
///
/// The second is optional: a citation may name the entry in as many words
/// as it likes, or in none, and what the recognizer insists on is the
/// first word and a number within reach of it. Twelve characters is the
/// reach — enough for the second word and the punctuation around it, and
/// short enough that a number in the *next* sentence is not a citation.
constexpr std::string_view citation_word = "JOURNAL";
constexpr unsigned citation_reach = 12;

/// The largest entry number a citation may name. Four digits, which is
/// what the prompt takes (journal.h), and a run of more than that is not
/// an entry number — it is a year, a coin count, or a number that has run
/// into the one before it.
constexpr unsigned citation_max_digits = 4;

[[nodiscard]] constexpr bool is_digit(char ch) noexcept {
  return ch >= '0' && ch <= '9';
}

[[nodiscard]] constexpr bool is_letter(char ch) noexcept {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

/// One character, as the window keeps it: upper case for a letter, itself
/// for a digit, and a space for everything else.
[[nodiscard]] constexpr char normalized(char ch) noexcept {
  if (ch >= 'a' && ch <= 'z') {
    return static_cast<char>(ch - ('a' - 'A'));
  }
  if (is_letter(ch) || is_digit(ch)) {
    return ch;
  }
  return ' ';
}

}  // namespace

std::uint16_t journal_citation_in(std::string_view text) noexcept {
  for (std::size_t at = 0; at + citation_word.size() <= text.size(); ++at) {
    if (text.substr(at, citation_word.size()) != citation_word) {
      continue;
    }
    // A word and not a fragment of a longer one: the character before it
    // and the one after it must not be letters. Without that a word this
    // one happens to be inside is a citation.
    if (at > 0 && is_letter(text[at - 1])) {
      continue;
    }
    std::size_t p = at + citation_word.size();
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

    unsigned digits = 0;
    unsigned value = 0;
    while (p < text.size() && is_digit(text[p]) &&
           digits <= citation_max_digits) {
      value = (value * 10U) + static_cast<unsigned>(text[p] - '0');
      ++digits;
      ++p;
    }
    if (digits == 0 || digits > citation_max_digits || value == 0) {
      // Zero is not an entry, and a longer run of digits is not an entry
      // number. Both fall through to the next occurrence of the word
      // rather than answering, because a second citation later in the
      // same window is still a citation.
      continue;
    }
    return static_cast<std::uint16_t>(value);
  }
  return 0;
}

void journal_state::clear() noexcept {
  entry_ = 0;
  delivery_ = journal_delivery::none;
  truncated_ = false;
  text_length_ = 0;
  cited_ = 0;
  window_length_ = 0;
  mode_ = journal_reader_mode::closed;
  page_ = 0;
  page_count_ = 0;
  digit_count_ = 0;
  on_screen_ = false;
  covered_ = false;
  drawn_signature_ = 0;
}

void journal_state::ask(std::uint16_t entry) noexcept {
  entry_ = entry;
  delivery_ = journal_delivery::no_host;
  truncated_ = false;
  text_length_ = 0;
}

void journal_state::deliver(std::string_view what) noexcept {
  const std::size_t take = std::min(what.size(), journal_page_bytes);
  for (std::size_t i = 0; i < take; ++i) {
    text_[i] = what[i];
  }
  text_length_ = take;
  truncated_ = what.size() > take;
  delivery_ = take == 0 ? journal_delivery::no_text : journal_delivery::ready;
}

void journal_state::refuse(journal_delivery why) noexcept {
  text_length_ = 0;
  truncated_ = false;
  delivery_ = why;
}

std::uint16_t journal_state::note_drawn_text(std::string_view what) noexcept {
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
    return 0;
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

  const std::uint16_t found =
      journal_citation_in(std::string_view{window_.data(), window_length_});
  if (found == 0) {
    return 0;
  }
  // One drawing of a citation opens one entry: the window is emptied so
  // the same characters cannot match again on the next string the program
  // draws beside them.
  cited_ = found;
  window_length_ = 0;
  return found;
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
