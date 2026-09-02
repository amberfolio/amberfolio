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
#include <span>
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
  /// "Entry N" — the section the game refers to most, cited in decimal.
  entry,
  /// "Tavern Tale N" — what a tavern sends a player to read, in decimal.
  tale,
  /// "Proclamation N" — posted by the city council, and cited the way the
  /// booklet numbers them: in Roman numerals, often several at once.
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

/// One line of the journal's own log: something the game told the player to
/// read, and when (M5-E4b, #222).
///
/// **A log, not an index.** It fills as a game is played and starts empty,
/// which is the difference between "here is what you have been told" and
/// "here is everything the book contains". The second would be a list of
/// every entry in the journal, which is precisely what that journal's own
/// introduction tells a player not to read.
struct journal_seen_row {
  journal_citation what;
  /// When the game cited it, off the machine's own seeded wall clock.
  /// Month and day rather than a full date because the panel has room for
  /// what a player needs to tell one evening's play from another's, and
  /// no more.
  std::uint8_t month{};
  std::uint8_t day{};
  std::uint8_t hour{};
  std::uint8_t minute{};
  /// Whether the player has opened it since it was cited. The `*` in the
  /// list, and the only thing here they change by reading rather than by
  /// playing.
  bool read{false};
};

/// How many the log keeps. A cap rather than a promise: the oldest falls
/// off the end, because a list nobody can page to the bottom of is not a
/// list, and what a player wants from this is the last few things the game
/// said rather than a complete history.
inline constexpr std::size_t journal_log_rows = 64;

/// What the reader is showing.
enum class journal_reader_mode : std::uint8_t {
  /// Nothing. The state at power-on, which is the whole of this seam's
  /// fidelity claim.
  closed,
  /// The journal's own screen: what the game has cited, newest first
  /// (M5-E4b, #222). What `Notes` opens.
  listing,
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

/// The most citations one drawing can name. Four is what the city hall
/// names in one sentence (#232); eight is room for a longer list without
/// making the state that holds it wide.
inline constexpr std::size_t journal_citations_at_once = 8;

/// How much of the program's own narration the citation watch keeps.
///
/// A window rather than a string, because a citation is not guaranteed to
/// reach the program's message box in one piece: the script prints a
/// sentence as one operand and the number it cites as the next, so the
/// box is called twice and the two halves have to meet somewhere. The
/// window is emptied when the box is told a *new* message has begun,
/// which is the program's own boundary and not a guess at one, so what
/// it holds is one message and never two.
///
/// Two hundred and fifty-six characters, because that is the longest a
/// Pascal string can be and the box takes one of those: a message that
/// fills the box entirely still fits, and the longest the real program
/// was seen to send was a hundred and ninety-seven (#232). A message
/// longer than the window keeps its tail, which is the end a number
/// arrives at in every split form there is.
inline constexpr std::size_t journal_citation_window = 256;

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

  /// One piece of narration the program has just been asked to print.
  ///
  /// Answers the first citation in it, or a zero one. The text is
  /// normalized into the rolling window first (upper case, runs of
  /// anything that is not a letter, a digit or a comma collapsed to one
  /// space), so a citation whose number arrives as the next piece is
  /// recognized on that one. A match **clears the window**, so one
  /// message citing something opens one entry however many times the seam
  /// looks at it afterwards — and a message that names several
  /// (`cited_all()`) is one message.
  ///
  /// Nothing of the program's text is kept beyond the window and nothing
  /// of it leaves this object.
  journal_citation note_drawn_text(std::string_view what) noexcept;

  /// The last citation named, or a zero one. Kept so the reader can tell
  /// a fresh citation from the one it is already showing — which is a
  /// comparison of the pair, because the game citing tale 12 while entry
  /// 12 is on the screen is a fresh citation.
  [[nodiscard]] journal_citation cited() const noexcept { return cited_; }

  /// Everything the last matching drawing named, in the order it named
  /// them; `cited()` is the first. Empty until something has matched.
  [[nodiscard]] std::span<const journal_citation> cited_all() const noexcept {
    return {cited_all_.data(), cited_count_};
  }

  // --- the log of what the game has said ---------------------------------

  /// Remember that the game cited `what` at `when`.
  ///
  /// Newest first. Citing something already in the log **moves it up and
  /// re-dates it** rather than adding a second line, and leaves its read
  /// flag alone: the game repeating itself is the game repeating itself,
  /// and a player who has read that entry has still read it.
  ///
  /// This is observation on `machine/automap.h`'s three terms - dropped
  /// by `reset()`, absent from the state hash, and rebuilt by a host from
  /// what it stored. It is not machine state and a host may write it.
  void note_seen(journal_citation what, std::uint8_t month, std::uint8_t day,
                 std::uint8_t hour, std::uint8_t minute) noexcept;

  /// Mark one read, if it is in the log. False when it is not, which is
  /// the ordinary case for an entry the player asked for at the prompt:
  /// nothing cited it, so there is no line to mark.
  bool mark_seen_read(journal_citation what) noexcept;

  [[nodiscard]] std::span<const journal_seen_row> seen() const noexcept {
    return {seen_.data(), seen_count_};
  }

  /// Everything the log has, gone. What a host calls before handing over
  /// a stored one, so a store that is read twice does not double.
  void clear_seen() noexcept;

  /// Whether the log has changed since a host last said it had written it
  /// down. The same "has it changed" the automap keeps, and for the same
  /// reason: a host that wrote the file on every citation would write it
  /// far more often than anything changed.
  [[nodiscard]] bool seen_changed() const noexcept { return seen_changed_; }
  void set_seen_changed(bool changed) noexcept { seen_changed_ = changed; }

  /// Everything in the window, forgotten. What a match does, what the
  /// watch does when the program says a new message has begun, and what a
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

  /// Which line of the log the list is pointed at, and the key that moves
  /// it. Clamped to what the log holds, so a list that shrank under a
  /// cursor does not leave it past the end.
  [[nodiscard]] std::size_t list_cursor() const noexcept {
    return seen_count_ == 0
               ? 0
               : (list_cursor_ < seen_count_ ? list_cursor_ : seen_count_ - 1);
  }
  void move_list_cursor(int by) noexcept;

  /// How many rows of the list are on the screen so far.
  ///
  /// A batch may queue twelve calls and place 256 bytes (`seam.h`), and a
  /// screen of ten rows is more than that — so it is painted over
  /// successive arrivals, a few rows at a time, and this is how far it has
  /// got. Zero means "start again", which is what a moved cursor or a new
  /// line in the log means.
  [[nodiscard]] std::size_t list_drawn() const noexcept { return list_drawn_; }
  void set_list_drawn(std::size_t rows) noexcept { list_drawn_ = rows; }

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
  std::size_t cited_count_{};
  std::array<journal_citation, journal_citations_at_once> cited_all_{};
  std::size_t window_length_{};
  std::array<char, journal_citation_window> window_{};

  journal_reader_mode mode_{journal_reader_mode::closed};
  std::uint16_t page_{};
  std::uint16_t page_count_{};
  std::size_t digit_count_{};
  std::array<char, journal_prompt_digits> digits_{};
  journal_kind asked_kind_{journal_kind::entry};

  std::size_t seen_count_{};
  std::size_t list_cursor_{};
  std::size_t list_drawn_{};
  bool seen_changed_{false};
  std::array<journal_seen_row, journal_log_rows> seen_{};

  bool on_screen_{false};
  bool covered_{false};
  std::uint32_t drawn_signature_{};
  std::array<std::uint8_t, automap_panel_pixels> pixels_{};
};

/// What a page came to after being made drawable, and whether all of it
/// fitted.
struct journal_drawn {
  std::size_t written{};
  /// False when the input ran past the buffer. `truncated()` on the state,
  /// and the reader says so rather than quietly ending a sentence.
  bool complete{true};
};

/// Copy `text` into `into` as bytes the program's font can draw, one glyph
/// per code point (M5-E4c, #219).
///
/// **The panel draws a byte as a glyph**, out of a table of sixty-four
/// indexed by the character modulo sixty-four. A store is UTF-8 and an OCR
/// engine emits plenty of it — a real ingestion of one edition carries two
/// hundred and twenty-nine non-ASCII characters, and two hundred and
/// twenty-two of them are quotation marks — so an entry opening with a
/// curly quote opened with three pieces of furniture before this existed.
///
/// It is done here, at the point a host's answer becomes the machine's
/// page, rather than at the point it is drawn. Three things fall out of
/// that and none of them would if it were done later: wrapping counts
/// bytes and is now counting the right ones, a page that runs past the
/// buffer can no longer be cut in half through a multi-byte sequence, and
/// the reader itself needs to know nothing about encodings.
///
/// **The store is not touched.** A player's transcription is theirs, it is
/// UTF-8, and a person editing that file should be able to type a curly
/// quote into it. What changes is only what the panel is handed.
///
/// Every code point produces something. What has an obvious equivalent
/// gets it — the quotation marks, the dashes, an ellipsis — and everything
/// else gets one visible substitute, because a character the panel cannot
/// draw should look like a character the panel cannot draw rather than
/// vanishing. A byte that is not valid UTF-8 is substituted too, and one
/// byte of it is consumed, so no input can make this loop for ever.
[[nodiscard]] journal_drawn journal_drawable(std::string_view text,
                                             std::span<char> into) noexcept;

/// Every citation in `text`, in the order named, into `out`; how many.
///
/// Free, and separate from the window above, so that the pattern can be
/// checked against strings a test writes without a machine anywhere near
/// it — which is what #175 asks for. `text` is expected normalized the
/// way `note_drawn_text()` normalizes: upper case, single spaces, commas
/// kept.
///
/// **What it matches is the citation's shape and not the program's
/// prose.** The word a numbered section of the document is called by —
/// entry, tale, proclamation, each with its plural — and a number after
/// it in the notation that section is numbered in: decimal for entries
/// and tales, a Roman numeral for proclamations. After a plural, a list
/// joined by commas and "and". Nothing is copied out of the program to
/// make it work and nothing of the program's text is written down here
/// (CONTRIBUTING.md).
///
/// **A list that runs off the end is not answered yet.** A sentence
/// wrapped across two lines of the message panel reaches the watch as
/// two draws, and where the wrap falls inside a list the first draw ends
/// in a comma or an "and". That is *nothing* — not the first three of
/// four — until the rest of the list has been drawn, which is why the
/// window keeps its commas.
///
/// That the program cites in these shapes is a measured fact and not a
/// guess: the first real citation anybody drove the reader against was
/// four proclamations in one sentence at the city hall, in Roman numerals,
/// and a recognizer that wanted the book's own word and a decimal number
/// saw nothing (#232).
[[nodiscard]] std::size_t journal_citations_in(
    std::string_view text, std::span<journal_citation> out) noexcept;

/// The first citation in `text`, or a zero one. `journal_citations_in()`
/// with room for one answer.
[[nodiscard]] journal_citation journal_citation_in(
    std::string_view text) noexcept;

}  // namespace amberfolio::machine
