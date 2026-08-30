// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal reader's state: what the game cited, what a host handed
// back, and what the reader is doing about it. M5-E4 (#175), PLAN.md §5
// item 2, the in-game half of the journal.
//
// This is the journal seam's memory (`seam_journal.cpp`), and it is here
// for exactly the reason `automap.h` is: it is **observation**, derived
// from the machine by something that watches it, and it is not machine
// state. The same three sentences apply, unchanged:
//
//   * `machine::reset()` drops it. A reset machine has no program, so
//     nothing has cited anything at it.
//   * The state serialization (`state.h`) never sees it. A machine with
//     an entry on its screen hashes as the machine without one, which is
//     what lets the fidelity pair be a test rather than an argument.
//   * A replay **reconstructs** it: the same program draws the same
//     strings, so the same citation is recognized at the same tick.
//
//
// It is also the delivery channel, and that is the one new thing here
// ---------------------------------------------------------------------
//
// The text of a journal entry lives on the player's machine, in a host's
// store (`hosts/common/.../journal_store.h`), because files and OCR are a
// host's business (PLAN.md §4). The reader is a seam, in core. So the
// text has to cross, and `seam_context::call_host()` answers a `bool`:
// it says the callout was served, not what it found.
//
// The buffer below is what it found. A seam calls
// `seam_host_service::journal_open` with the entry number; the host's
// `serve()` — which is synchronous C++ inside the module on both targets,
// and `host_services.h` says why — looks the entry up and calls
// `deliver()` or `refuse()` here; the seam reads it back the instant the
// callout returns. Nothing is queued, nothing is stale, and a host that
// attached nothing leaves `delivery()` at `no_host`, which the reader
// shows rather than showing a blank page.
//
// A host writing here is **not** a host writing machine state. This is
// observation on the same three terms as the automap's store, which
// `automap_update` already drives from the far side of the same door.
// Nothing in this file reaches the bus, the serialization or the hash.
//
//
// What the reader is capped at, and why there is a cap at all
// ----------------------------------------------------------
//
// An entry may be up to `journal_max_entry_bytes` — sixty-four kilobytes
// — in a host's store, because that is what an OCR engine is allowed to
// hand back. Core allocates nothing (PLAN.md §4), so what crosses is a
// fixed buffer, and `journal_page_bytes` is the size of it: four
// kilobytes, which is about thirteen screens of the panel this reader
// draws in and past any entry a page of a printed journal can hold. A
// longer text is delivered truncated and says so (`truncated()`), because
// a reader that silently stopped mid-sentence would be a transcription
// with a hole in it and nothing downstream could tell.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/machine/automap.h"

namespace amberfolio::machine {

/// The most text one entry may cross the host boundary as.
inline constexpr std::size_t journal_page_bytes = 4096;

/// How the last `journal_open` went. Every value but `ready` is a reason
/// the reader has something short to say instead of a page.
enum class journal_delivery : std::uint8_t {
  /// Nothing has been asked for.
  none,
  /// Text was handed over, and `text()` is it.
  ready,
  /// No host service is attached, so nothing could be asked. The honest
  /// answer for a machine driven by something that plugged in no host —
  /// a bare ABI caller, a test — and not a failure.
  no_host,
  /// A host answered and has no journal at all: nobody has ingested one
  /// (`docs/journal.md`).
  no_journal,
  /// A journal, and no entry with that number in it.
  no_entry,
  /// The entry is there and has no text: it was extracted and the engine
  /// read nothing off it. Its own answer rather than `no_entry`, because
  /// the two are fixed by different things — one by ingesting a journal,
  /// the other by ingesting it with an engine that works.
  no_text,
};

/// Which of the journal's numbered sections a citation names (M5-E3d,
/// #218).
///
/// **A number alone does not identify anything.** The document prints
/// three sections the game sends a player to by number, and each numbers
/// from its own base: Tale 4 and Journal Entry 4 are both `4` and are not
/// the same text. So every key that reaches a stored transcription is a
/// pair, from the word the recognizer matched all the way to the row the
/// host looks up.
///
/// It lives here rather than beside the fact table because the recognizer
/// below is what decides it, and the recognizer is core.
enum class journal_kind : std::uint8_t {
  /// "Journal Entry N" — the section the game refers to most.
  entry,
  /// "Tale N" — what a tavern sends a player to read.
  tale,
  /// "Proclamation N" — posted by the city council.
  proclamation,
};

/// How many there are. A loop's bound and an array's size; nothing reads
/// it as a kind.
inline constexpr std::size_t journal_kinds = 3;

/// The one-word name of a kind, lower case. Never null.
///
/// This is a *token*, not a caption: it is what a store file writes and
/// what a host's log line says, so it has to be one word and stable
/// across versions. The reader draws its own words, in its own case, in
/// the game's font.
[[nodiscard]] const char* journal_kind_name(journal_kind which) noexcept;

/// The kind `word` names, or nothing. Case-sensitive and exact — this
/// reads a file somebody may have edited, and a near miss is a mistake
/// worth reporting rather than guessing at.
[[nodiscard]] bool journal_kind_from_name(std::string_view word,
                                          journal_kind& out) noexcept;

/// What the game just told a player to read: which section, and which
/// number of it.
///
/// A number of zero means *nothing was named* — no section numbers from
/// zero, so it needs no separate flag, and a citation that failed to
/// parse and a citation that was never there are the same answer.
struct journal_citation {
  journal_kind kind{journal_kind::entry};
  std::uint16_t number{};

  /// Whether it names anything at all.
  [[nodiscard]] explicit operator bool() const noexcept { return number != 0; }

  /// Written out rather than defaulted: a defaulted comparison is found
  /// by argument-dependent lookup alone, and GoogleTest deliberately
  /// blocks that lookup inside its own comparison helper.
  [[nodiscard]] friend constexpr bool operator==(
      const journal_citation& a, const journal_citation& b) noexcept {
    return a.kind == b.kind && a.number == b.number;
  }
};

/// A citation as the one `std::uint32_t` a host callout carries.
///
/// `seam_host_service::journal_open`'s argument was a number when there
/// was one section, and is a pair now. It is packed rather than widened
/// because the callout's width is ABI (`abi.h`), the top sixteen bits
/// were never used, and a kind is three values: an ABI change would have
/// been a cost paid by every embedder for a bit and a half.
[[nodiscard]] constexpr std::uint32_t journal_open_argument(
    journal_citation what) noexcept {
  return (static_cast<std::uint32_t>(what.kind) << 16U) | what.number;
}

/// The other direction. A kind this build does not know, or a number of
/// zero, comes back as a zero citation — which every caller already has
/// to handle, because it is what an unrecognized citation looks like.
[[nodiscard]] constexpr journal_citation journal_open_citation(
    std::uint32_t argument) noexcept {
  const std::uint32_t kind = argument >> 16U;
  if (kind >= journal_kinds) {
    return {};
  }
  return {.kind = static_cast<journal_kind>(kind),
          .number = static_cast<std::uint16_t>(argument & 0xFFFFU)};
}

/// What the reader is showing.
enum class journal_reader_mode : std::uint8_t {
  /// Nothing. The state at power-on, which is the whole of this seam's
  /// fidelity claim.
  closed,
  /// The entry-number prompt, for a player who wants an entry the game
  /// has not cited.
  asking,
  /// A page of an entry.
  showing,
};

/// How many digits the prompt takes. Entry numbers in a printed journal
/// of this kind run to three; four is one more than anybody needs and
/// still cannot overflow the number it is parsed into.
inline constexpr std::size_t journal_prompt_digits = 4;

/// How much of the program's own drawn text the citation watch keeps.
///
/// A window rather than a string, because a citation is not guaranteed to
/// reach the program's text primitive in one piece: that routine draws a
/// Pascal string at a cell, and a sentence wrapped across two lines of a
/// message panel is two calls. Ninety-six characters is three lines of
/// the widest panel the program has, which is as far apart as the two
/// halves of one sentence can be.
inline constexpr std::size_t journal_citation_window = 96;

/// Everything the journal reader knows, for one machine.
class journal_state {
 public:
  /// Drop everything: nothing cited, nothing delivered, nothing on the
  /// screen. What `machine::reset()` calls.
  void clear() noexcept;

  // --- what a host was asked, and what it answered ---------------------

  /// About to ask a host for `what`: the citation is remembered and
  /// anything previously delivered is dropped, so a callout that is not
  /// served leaves `no_host` rather than the last entry's text.
  void ask(journal_citation what) noexcept;

  /// A host's answer. `what` longer than `journal_page_bytes` is kept up
  /// to that and `truncated()` becomes true; empty text is `no_text`
  /// rather than `ready`, because a blank page is not an answer.
  void deliver(std::string_view what) noexcept;

  /// A host's other answer.
  void refuse(journal_delivery why) noexcept;

  /// What was asked for. Its number is zero when nothing has been.
  [[nodiscard]] journal_citation entry() const noexcept { return entry_; }
  [[nodiscard]] journal_delivery delivery() const noexcept { return delivery_; }
  [[nodiscard]] bool truncated() const noexcept { return truncated_; }
  [[nodiscard]] std::string_view text() const noexcept {
    return std::string_view{text_.data(), text_length_};
  }

  // --- the citation watch ----------------------------------------------

  /// One string the program has just been asked to draw.
  ///
  /// Answers the citation in it, or a zero one. The text is normalized
  /// into the rolling window first (upper case, runs of anything that is
  /// not a letter or a digit collapsed to one space), so a citation split
  /// across two draws is recognized on the second of them. A match
  /// **clears the window**, so one drawing of a citation opens one entry
  /// however many times the seam looks at it afterwards.
  ///
  /// Nothing of the program's text is kept beyond the window and nothing
  /// of it leaves this object.
  journal_citation note_drawn_text(std::string_view what) noexcept;

  /// The last citation named, or a zero one. Kept so the reader can tell
  /// a fresh citation from the one it is already showing — which is a
  /// comparison of the pair, because the game citing tale 12 while entry
  /// 12 is on the screen is a fresh citation.
  [[nodiscard]] journal_citation cited() const noexcept { return cited_; }

  /// Everything in the window, forgotten. What a match does, and what a
  /// test does between two strings that should not run together.
  void forget_citation() noexcept;

  // --- what the reader is doing -----------------------------------------

  [[nodiscard]] journal_reader_mode reader() const noexcept { return mode_; }
  void set_reader(journal_reader_mode mode) noexcept;

  /// Whether the reader owns the panel's cells at all — the one question
  /// the automap seam asks of this object, because the two panels are the
  /// same pixels and the reader is modal over the map.
  [[nodiscard]] bool reader_open() const noexcept {
    return mode_ != journal_reader_mode::closed;
  }

  /// Which page of the entry is up, and how many there turned out to be.
  /// The count is what the last render worked out, so it is a fact about
  /// what was drawn rather than a promise about what will be.
  [[nodiscard]] std::uint16_t page() const noexcept { return page_; }
  void set_page(std::uint16_t page) noexcept;
  [[nodiscard]] std::uint16_t page_count() const noexcept {
    return page_count_;
  }
  void set_page_count(std::uint16_t count) noexcept { page_count_ = count; }

  /// The digits typed at the prompt.
  [[nodiscard]] std::string_view digits() const noexcept {
    return std::string_view{digits_.data(), digit_count_};
  }
  /// True if there was room for it.
  bool push_digit(char digit) noexcept;
  void pop_digit() noexcept;
  void clear_digits() noexcept;
  /// What the digits say, or zero for none of them.
  [[nodiscard]] std::uint16_t asked_entry() const noexcept;

  /// Which section the prompt is pointed at, and the key that moves it.
  ///
  /// The prompt has to have one: a player typing `4` at it has not said
  /// whether they mean the fourth entry or the fourth tale, and a reader
  /// that picked for them would be picking wrong two times in three.
  /// Cycling rather than three keys, because the panel has one line to
  /// say it in and the seam has few keys it may take.
  [[nodiscard]] journal_kind asked_kind() const noexcept { return asked_kind_; }
  void set_asked_kind(journal_kind kind) noexcept { asked_kind_ = kind; }
  void cycle_asked_kind() noexcept;

  /// The prompt as a citation: the kind it is pointed at, and the number
  /// typed into it.
  [[nodiscard]] journal_citation asked() const noexcept {
    return {.kind = asked_kind_, .number = asked_entry()};
  }

  /// Whether the reader's pixels are on the planes because this seam put
  /// them there and nothing has painted over them since, and whether
  /// something other than the party roster owns those cells. The same
  /// pair the automap keeps, told by the same three of the program's own
  /// drawing points.
  [[nodiscard]] bool on_screen() const noexcept { return on_screen_; }
  void set_on_screen(bool up) noexcept { on_screen_ = up; }
  [[nodiscard]] bool covered() const noexcept { return covered_; }
  void set_covered(bool covered) noexcept;

  /// A hash of everything the last render was drawn from; zero is
  /// "nothing has been drawn". The reader redraws when it moves.
  [[nodiscard]] std::uint32_t drawn_signature() const noexcept {
    return drawn_signature_;
  }
  void set_drawn_signature(std::uint32_t signature) noexcept {
    drawn_signature_ = signature;
  }

  /// The panel the reader is rendered into before it goes on the planes,
  /// one byte per pixel. The same rect as the automap's, because it is
  /// the same fact about the program's screen and `automap.h` derives it
  /// once: the interior of the adventuring screen's right-hand frame,
  /// less the program's own status row — the one region a seam can take
  /// and give back, since the program can be asked to redraw the party
  /// roster over it from live state.
  [[nodiscard]] std::array<std::uint8_t, automap_panel_pixels>&
  pixels() noexcept {
    return pixels_;
  }
  [[nodiscard]] const std::array<std::uint8_t, automap_panel_pixels>& pixels()
      const noexcept {
    return pixels_;
  }

 private:
  journal_citation entry_{};
  journal_delivery delivery_{journal_delivery::none};
  bool truncated_{false};
  std::size_t text_length_{};
  std::array<char, journal_page_bytes> text_{};

  journal_citation cited_{};
  std::size_t window_length_{};
  std::array<char, journal_citation_window> window_{};

  journal_reader_mode mode_{journal_reader_mode::closed};
  std::uint16_t page_{};
  std::uint16_t page_count_{};
  std::size_t digit_count_{};
  std::array<char, journal_prompt_digits> digits_{};
  journal_kind asked_kind_{journal_kind::entry};

  bool on_screen_{false};
  bool covered_{false};
  std::uint32_t drawn_signature_{};
  std::array<std::uint8_t, automap_panel_pixels> pixels_{};
};

/// The citation in `text`, or a zero one for none.
///
/// Free, and separate from the window above, so that the pattern can be
/// checked against strings a test writes without a machine anywhere near
/// it — which is what #175 asks for. `text` is expected normalized the
/// way `note_drawn_text()` normalizes: upper case, single spaces.
///
/// **What it matches is the citation's shape and not the program's
/// prose.** One of three ordinary English words — the one this project's
/// own enhancement is named after and the two the document's other
/// numbered sections are called — and a decimal number within a short
/// reach of it. Nothing is copied out of the program to make it work and
/// nothing of the program's text is written down here (CONTRIBUTING.md).
///
/// **The longest word wins**, which matters for exactly one pair: a
/// program that draws "tavern tale" is drawing a tale, and a recognizer
/// that stopped at the first word it found would be right anyway. It is
/// the rule rather than the accident because it is what makes adding a
/// fourth word safe.
[[nodiscard]] journal_citation journal_citation_in(
    std::string_view text) noexcept;

}  // namespace amberfolio::machine
