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

namespace amberfolio::machine {

/// The most text one entry may cross the host boundary as.
inline constexpr std::size_t journal_page_bytes = 4096;

// ---------------------------------------------------------------------------
// The entries that are pictures (#328)
// ---------------------------------------------------------------------------
//
// Several of a journal's entries are drawings — maps, mazes, a diagram,
// a row of scratched runes — and an OCR engine reads every word that is
// on such a page, which is the entry's heading and its one-line
// caption. What the reader used to show for one was those two lines and
// nineteen blank rows.
//
// A picture is reduced to the numbers below **at ingestion**, on the
// player's own machine, out of the player's own document
// (`hosts/common/.../journal_picture.h`), and what crosses into the
// machine is levels rather than colours. The box, the level count and
// the pixel aspect are here rather than beside the reducer because they
// are facts about *this screen*: `seam_journal.cpp` holds each of them
// against the frame the program actually draws, in a `static_assert`, so
// a host cannot reduce to a shape the reader has no room for.
//
// **Levels, not indices.** A journal's drawings are one hue of ink on
// paper, so tone is the whole of what a reduction has to keep; which
// palette index each tone becomes is the reader's business, and the
// reader is the only thing that knows what palette the program has
// installed. Keeping the two apart is also what lets the ramp be
// re-chosen without re-ingesting anybody's document — the same
// arrangement `explored_reveal_radius` has with the automap's sidecar.

/// The reader's full-screen page, in pixels: the interior of the box the
/// program's own frame drawer leaves, which is thirty-eight character
/// cells across and twenty rows deep on the 320x200 screen.
inline constexpr unsigned journal_art_width = 304;
inline constexpr unsigned journal_art_height = 160;

/// How many tones a stored picture has. Four, measured: a plain
/// threshold and a four-level quantization both read as a map at this
/// size and every dithered candidate speckled the paper, so the choice
/// was between two levels and four, and four costs one more bit
/// (`docs/journal.md` §11).
inline constexpr unsigned journal_art_levels = 4;

/// Level 0 is ink and the last is paper, which is the way round a
/// greymap runs and the *opposite* of what reaches the screen: the
/// reader draws on a black ground, so the most ink becomes the brightest
/// index.
inline constexpr std::uint8_t journal_art_ink = 0;
inline constexpr std::uint8_t journal_art_paper = journal_art_levels - 1;

/// Two bits a pixel, four pixels a byte, each row padded to a whole
/// byte. A row's stride and the whole picture's size follow.
inline constexpr unsigned journal_art_pixels_per_byte = 4;

[[nodiscard]] constexpr std::size_t journal_art_stride(
    unsigned width) noexcept {
  return (static_cast<std::size_t>(width) + journal_art_pixels_per_byte - 1U) /
         journal_art_pixels_per_byte;
}

/// The most bytes one picture can be: the whole box, packed.
inline constexpr std::size_t journal_art_bytes =
    journal_art_stride(journal_art_width) * journal_art_height;

/// The display shape of one screen pixel, as height over width. The
/// 320x200 mode fills a 4:3 display, so a pixel is a fifth taller than
/// it is wide and a picture reduced without allowing for it comes out
/// stretched by that much. The reducer fits a picture so that it looks
/// like the printed one *on the glass* rather than in the framebuffer.
inline constexpr unsigned journal_art_aspect_tall = 6;
inline constexpr unsigned journal_art_aspect_wide = 5;

/// The most pictures one entry may have.
///
/// A sanity limit on a fact table somebody edits by hand rather than a
/// belief about journals, and well above what any of them wants: the
/// most in the one edition anybody has tabled is **three**, an atlas
/// printed as three maps across a two-page spread.
inline constexpr std::size_t journal_art_per_entry = 8;

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

/// A picture as the callout that fetches one names it: the citation, and
/// which of the entry's pictures (#328).
///
/// The same one word `journal_open` gets, with the number of the picture
/// in the byte above the pair — which is free, because a kind is three
/// values and a section number is sixteen bits. An entry has at most
/// `journal_art_per_entry` pictures, so a byte is more room than the
/// table can use, and the callout's width stays the ABI's own
/// (`journal_open_argument`, above, is why that matters).
[[nodiscard]] constexpr std::uint32_t journal_art_argument(
    journal_citation what, std::uint8_t nth) noexcept {
  return journal_open_argument(what) | (static_cast<std::uint32_t>(nth) << 24U);
}

/// The citation out of one. The picture's number is masked off first, so
/// this is `journal_open_citation` on the pair underneath and gives the
/// same zero citation for a kind this build does not know.
[[nodiscard]] constexpr journal_citation journal_art_citation(
    std::uint32_t argument) noexcept {
  return journal_open_citation(argument & 0x00FFFFFFU);
}

/// And which picture of it.
[[nodiscard]] constexpr std::uint8_t journal_art_which(
    std::uint32_t argument) noexcept {
  return static_cast<std::uint8_t>(argument >> 24U);
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
///
/// **Two hundred and fifty-six, up from sixty-four** (#301). The number
/// was set for play, where sixty-four is more than a game cites in an
/// evening. It is set now for the debug cheat that puts *everything* a
/// player's ingested journal holds onto this log so a person can
/// proof-read the text off the game's own screen — and the one edition
/// anybody has ingested is ninety-nine sections (fifty-eight entries,
/// twenty-three tales, eighteen proclamations), so a cap of sixty-four
/// dropped the last thirty-five off the end before the listing could
/// show them. The listing itself is untouched: it scrolls a ten-row
/// window over whatever the log holds (`seam_journal.cpp`), so a longer
/// log is more to scroll and not a different screen. What it costs is a
/// couple of kilobytes of observation per machine, and nothing in the
/// state hash, because none of this is machine state.
inline constexpr std::size_t journal_log_rows = 256;

/// What the reader is showing.
enum class journal_reader_mode : std::uint8_t {
  /// Nothing. The state at power-on, which is the whole of this seam's
  /// fidelity claim.
  closed,
  /// The journal's own screen: what the game has cited, newest first
  /// (M5-E4b, #222). What `Notes` opens, and the only way in there is.
  listing,
  /// A page of an entry, opened from a row of the listing.
  showing,
};

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

  // --- and the same for a picture (#328) --------------------------------
  //
  // A picture crosses the same way the text does and for the same
  // reasons: `serve()` answers `void`, so what it found goes into a
  // buffer here and the seam reads it back the instant the callout
  // returns. One at a time, because the buffer is the whole of the
  // reader's box packed — twelve kilobytes — and a page shows one.

  /// About to ask a host for picture `nth` of `what`. Anything held is
  /// dropped, so a callout nothing serves leaves the buffer empty rather
  /// than the last picture's pixels.
  void ask_art(journal_citation what, std::uint8_t nth) noexcept;

  /// A host's answer: the picture's own shape, its packed levels, and
  /// **how many pictures the entry has in all** — which is the number the
  /// reader pages by and is why a refusal carries it too.
  ///
  /// Ignored, leaving the buffer empty, for a shape bigger than the
  /// reader's box or a run of levels that is not the size that shape
  /// says: a host may hold a store written by another build, and a
  /// picture that does not describe itself is not one this can draw.
  void deliver_art(std::uint16_t width, std::uint16_t height,
                   std::span<const std::uint8_t> levels,
                   std::uint8_t of) noexcept;

  /// A host's other answer: the entry has `of` pictures and this is not
  /// one it could hand over. `of` may still be more than zero — a store
  /// that knows the count and lost the record.
  void refuse_art(std::uint8_t of) noexcept;

  /// How many pictures the entry the reader is showing has, as the last
  /// callout said. Zero until one has been made, which is every entry
  /// that is prose.
  [[nodiscard]] std::uint8_t art_count() const noexcept { return art_count_; }

  /// Whether the buffer holds a picture, and which of the entry's it is.
  [[nodiscard]] bool art_ready() const noexcept { return art_ready_; }

  /// Whether a callout for the picture named by `art_of()` and
  /// `art_nth()` has come back at all — with the picture or without it.
  ///
  /// **This is what stops a refused picture being asked for for ever.**
  /// The reader fetches when what is held is not what the page wants,
  /// and a host that answers "I have the count and not the record"
  /// leaves nothing held; without this the next arrival would ask again,
  /// at the program's own polling rate, for as long as the page was up.
  /// False after `ask_art()` and true after either answer.
  [[nodiscard]] bool art_answered() const noexcept { return art_answered_; }
  [[nodiscard]] std::uint8_t art_nth() const noexcept { return art_nth_; }
  /// Which entry the buffer's picture belongs to — the pair, because
  /// tale 4 and entry 4 are different documents and both may have art.
  [[nodiscard]] journal_citation art_of() const noexcept { return art_of_; }
  [[nodiscard]] std::uint16_t art_width() const noexcept { return art_width_; }
  [[nodiscard]] std::uint16_t art_height() const noexcept {
    return art_height_;
  }

  /// The level at `(x, y)`, and `journal_art_paper` outside the picture —
  /// which is what a reader wants for the margin around one anyway, since
  /// paper is the level it draws nothing for.
  [[nodiscard]] std::uint8_t art_level_at(unsigned x,
                                          unsigned y) const noexcept;

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

  /// Whether the reader is on the screen at all — the one question the
  /// automap seam asks of this object, because what the reader draws
  /// covers the map's own cells and the reader is modal over it.
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

  /// Which line of the log the list is pointed at, and the key that moves
  /// it. Clamped to what the log holds, so a list that shrank under a
  /// cursor does not leave it past the end.
  [[nodiscard]] std::size_t list_cursor() const noexcept {
    return seen_count_ == 0
               ? 0
               : (list_cursor_ < seen_count_ ? list_cursor_ : seen_count_ - 1);
  }
  void move_list_cursor(int by) noexcept;

  /// How many rows of whatever the program is drawing full-screen are on
  /// it so far — the log's listing, or a page of an entry (#305). Never
  /// both: the two are the same screen and one is always the way out of
  /// the other.
  ///
  /// A batch may queue twelve calls and place 256 bytes (`seam.h`), and a
  /// screen of ten rows is more than that — twenty is further past it —
  /// so it is painted over successive arrivals, a few rows at a time, and
  /// this is how far it has got. Zero means "start again", which is what
  /// a moved cursor, a new line in the log, a turned page and a changed
  /// mode all mean.
  [[nodiscard]] std::size_t screen_drawn() const noexcept {
    return screen_drawn_;
  }
  void set_screen_drawn(std::size_t rows) noexcept { screen_drawn_ = rows; }

  /// Whether the listing is underneath the page that is up (#305).
  ///
  /// The way out rather than the way it is drawn: a page opened from a
  /// row of the listing goes **back to the listing**, on the screen it is
  /// already holding, and one opened from the prompt goes out through the
  /// program's own composer. Observation, and not machine state.
  [[nodiscard]] bool page_from_list() const noexcept { return from_list_; }
  void set_page_from_list(bool from_list) noexcept { from_list_ = from_list; }

  /// Whether the party's own command-bar routine is sitting in its key
  /// loop right now (#305).
  ///
  /// **The precondition for opening the reader at all**, and the reason
  /// it is a flag rather than a memory of which key was pressed. A page
  /// is a full screen and nothing else (#346), the program's screen
  /// composer is what puts one back, and the composer is a safe give-back
  /// exactly when there cannot be a vendor's screen under it — which is
  /// exactly while the *party's* bar is the live one. That is a place in
  /// the program rather than a key, which is why `Notes` and F1 are both
  /// answered against it: opening on a screen this does not hold is
  /// M5-E2d again.
  ///
  /// Set where the adventuring loop calls that routine and cleared where
  /// it returns, both of which are points this seam already has for the
  /// `Notes` splice.
  [[nodiscard]] bool bar_live() const noexcept { return bar_live_; }
  void set_bar_live(bool live) noexcept { bar_live_ = live; }

  /// Which command the program's own bar highlight was sitting on when
  /// the bar routine was entered (#330), and whether anything recorded
  /// it.
  ///
  /// **The one thing the `Notes` splice has to give back besides the
  /// string.** The highlight is a single byte in the data segment that
  /// every bar in the program shares and numbers against its own groups
  /// (M5-E1g, #304), the routine sets it to the group of whatever command
  /// it matched, and `Notes` is a group only because this seam put one
  /// there — so a player who opened the journal came back to a bar with
  /// `Notes` drawn in the highlight's white end to end while every word
  /// beside it was the initial-white-and-green tail every bar in this game
  /// wears.
  ///
  /// What the program would have left is **measurable and is this**: on
  /// the party's own bar the highlight moves only when the routine matches
  /// a command, and `N` matches none of the program's, so a run with the
  /// seam off leaves the byte exactly as the routine found it. Recorded at
  /// the point the splice goes in and put back at the point it comes out,
  /// so it is never carried past one call of the program's own routine.
  ///
  /// Observation and not machine state, on `automap.h`'s three terms: it
  /// is read out of the machine, it is written back only where this seam
  /// had already written, and a run that never opens the journal never
  /// touches it.
  [[nodiscard]] bool bar_highlight_known() const noexcept {
    return bar_highlight_known_;
  }
  [[nodiscard]] std::uint8_t bar_highlight() const noexcept {
    return bar_highlight_;
  }
  void note_bar_highlight(std::uint8_t which) noexcept {
    bar_highlight_ = which;
    bar_highlight_known_ = true;
  }
  void forget_bar_highlight() noexcept { bar_highlight_known_ = false; }

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

 private:
  journal_citation entry_{};
  journal_delivery delivery_{journal_delivery::none};
  bool truncated_{false};
  std::size_t text_length_{};
  std::array<char, journal_page_bytes> text_{};

  std::uint16_t art_width_{};
  std::uint16_t art_height_{};
  std::uint8_t art_nth_{};
  std::uint8_t art_count_{};
  bool art_ready_{false};
  bool art_answered_{false};
  journal_citation art_of_{};
  std::array<std::uint8_t, journal_art_bytes> art_{};

  journal_citation cited_{};
  std::size_t cited_count_{};
  std::array<journal_citation, journal_citations_at_once> cited_all_{};
  std::size_t window_length_{};
  std::array<char, journal_citation_window> window_{};

  journal_reader_mode mode_{journal_reader_mode::closed};
  std::uint16_t page_{};
  std::uint16_t page_count_{};
  bool from_list_{false};
  bool bar_live_{false};
  std::uint8_t bar_highlight_{};
  bool bar_highlight_known_{false};

  std::size_t seen_count_{};
  std::size_t list_cursor_{};
  std::size_t screen_drawn_{};
  bool seen_changed_{false};
  std::array<journal_seen_row, journal_log_rows> seen_{};

  bool on_screen_{false};
  bool covered_{false};
  std::uint32_t drawn_signature_{};
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
