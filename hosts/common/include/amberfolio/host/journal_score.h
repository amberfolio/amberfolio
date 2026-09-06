// SPDX-License-Identifier: AGPL-3.0-only
//
// How well an ingestion went, as a number (#315).
//
// Everything about the OCR pipeline before this file was guesswork, and
// it was guesswork for one reason: there was no number anywhere that said
// how well a run had read. A transcription that "looks mostly right" is
// not a measurement, an engine setting that "should help" is not a
// finding, and a change that improved one entry and ruined two would have
// gone in unnoticed. So this is the first thing #315 asks for and the
// thing every engine-side change in it had to be justified against.
//
//
// The ground truth is the player's own corrections
// -----------------------------------------------
//
// Scoring needs something to score *against*, and the obvious source —
// two hand-typed entries kept in the test tree — is exactly the thing
// this repository may never hold: a journal's text is the player's own
// document (`journal_store.h`, CONTRIBUTING.md). What goes in the
// repository is this comparison harness; what stays on the player's
// machine is both halves of the comparison.
//
// And the store already holds both halves. `journal_text` carries what
// the engine read (`scanned`) and, wherever a person has been in there,
// what they wrote (`corrected`) — and a person who corrected an entry
// has, by doing so, produced ground truth for it. So a score is free for
// exactly the entries somebody cared enough to fix, needs no new file, no
// new format and no new fixture, and gets *better* the more of their
// journal a player has been through.
//
// That is not a trick to dodge the content rule; it is a better ground
// truth than a committed fixture would have been. A committed pair would
// score two entries of one edition forever. This scores whatever the
// player has fixed, of whatever edition they hold, on their own machine —
// so "did this engine change help?" is a question a player can answer
// about their own document by re-ingesting and watching the number move.
//
// The measurement in CI is the other half of the same arrangement, and it
// is the `journal_probe.h` model exactly: a synthetic document this
// project generates, and a fixture engine that misreads it in a way this
// project chose, so the *expected* error rate is arithmetic rather than
// an observation. `journal_probe_noisy_ocr` is that engine. It proves the
// harness counts correctly; it proves nothing about Tesseract, and does
// not pretend to.
//
//
// Character error rate, and why the ratio is of sums
// -------------------------------------------------
//
// The number is the standard one: the Levenshtein distance between the
// truth and what the engine read, over the length of the truth. Zero is
// a perfect reading; 0.05 is one character in twenty wrong, which reads
// as a good transcription with a typo a line; above about 0.15 the prose
// is still followable and every third line has something wrong in it,
// which is where #315 was filed from.
//
// It can exceed 1: an engine that produced twice as much text as the
// entry has is more than 100% wrong, and clamping that would hide the one
// failure mode a whole-page reading has — pulling in the neighbouring
// column.
//
// A store's overall rate is **total distance over total reference
// length**, not the mean of the per-entry rates. The two differ, and the
// difference matters: a mean over entries lets one badly-read caption of
// nine words count as much as a nine-hundred-character entry, so a change
// that helped every long entry and hurt one short one would look like a
// regression. The ratio of sums is the rate a reader actually meets,
// character for character, across the whole book.
//
// The word rate is beside it because the two say different things. A
// character rate answers "how much of this is wrong"; a word rate answers
// "how often do I trip over something", which is closer to what reading
// an entry in the game feels like. Both are reported and neither is the
// headline on its own.
//
//
// What is normalized away, and what deliberately is not
// ----------------------------------------------------
//
// **Whitespace, and nothing else.** Every run of spaces, tabs and
// newlines becomes one space and the ends are trimmed. The reason is that
// the scan's own line breaks are not the entry's — the journal is set in
// two narrow columns and a line ends where the column does — so a
// comparison that counted them would be scoring the typesetting, and a
// player who retyped an entry as one flowing paragraph would score worse
// than the engine did. #316 is the same fact from the other side.
//
// **Case is kept.** It was measured before it was decided: folding case
// moved the rate by four to five parts in a thousand on the two entries
// #315 scored by hand, which is not enough to buy the loss — a `Kobolds`
// where the book prints `kobolds` is a real misreading and the harness
// should say so.
//
// **Punctuation is kept**, for the same reason and with less doubt: an
// apostrophe read as a double quote is the single most common surviving
// error in a good reading, and a metric that could not see it would rank
// an engine that made them all as perfect.
//
//
// The cost, and the one refusal
// -----------------------------
//
// Levenshtein is O(n*m) and a store may legally hold entries of
// `journal_max_entry_bytes`. Real entries are under a kilobyte and the
// quadratic cost there is nothing, but at sixty-four kilobytes it is four
// billion cell updates, which is a hang rather than a diagnostic. So a
// pair longer than `journal_score_limit` is **refused rather than
// approximated**: the score says it could not be taken. A truncated
// comparison reported as a rate would be a number that looked like a
// measurement and was not, which is precisely what this file exists to
// stop.

#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/journal.h"

namespace amberfolio::host {

/// The longest normalized text this will compare, in characters. Beyond
/// it a score is refused rather than approximated (see above). Real
/// entries of the one edition in the table run to about a kilobyte, so
/// this is eight times the worst of them and a fraction of what the store
/// would legally accept.
inline constexpr std::size_t journal_score_limit = 8192;

/// One comparison: how many edits away it was, and how long the thing it
/// was compared against is.
///
/// Two numbers and not one rate, because an aggregate has to add the
/// halves separately — a mean of rates is the wrong average
/// (see above) — and because "over what" is half of what a rate means.
struct journal_score {
  /// Edits — insertions, deletions and substitutions — between the truth
  /// and what the engine read.
  std::size_t distance{0};
  /// How much truth there was: characters, or words, depending on which
  /// of the two calls below produced this.
  std::size_t reference{0};
  /// Whether a comparison happened at all. False for an item with no
  /// ground truth, and for a pair `journal_score_limit` refused.
  bool taken{false};

  /// Distance over reference length. Zero when nothing was compared, and
  /// **not clamped**: over one means more was read than was there.
  [[nodiscard]] double rate() const noexcept {
    return reference == 0
               ? 0.0
               : static_cast<double>(distance) / static_cast<double>(reference);
  }
};

/// One item of a store, scored against its own correction.
struct journal_item_score {
  machine::journal_citation what{};
  journal_score characters{};
  journal_score words{};
};

/// A whole store's worth.
struct journal_store_report {
  /// Every item that had both a scan and a correction, in the store's own
  /// order. Empty for a store nobody has corrected, which is not a
  /// failure — it is a player who has not needed to yet, and the honest
  /// thing to print is that there is nothing to measure against.
  std::vector<journal_item_score> items;
  /// The ratio of sums over all of them (see above).
  journal_score characters{};
  journal_score words{};
  /// How many items were refused for length. Reported rather than
  /// silently dropped: an aggregate that quietly left entries out would
  /// be a different measurement than the one it claims to be.
  std::size_t refused{0};
};

/// `text` with every run of whitespace collapsed to one space and the
/// ends trimmed — the form both sides of every comparison are in.
///
/// Exposed because a caller that wants to show a player *what* differed
/// has to show the same strings the rate was taken over, and because the
/// rule is worth being able to test on its own.
[[nodiscard]] std::string journal_normalize(std::string_view text);

/// Levenshtein distance between two already-normalized strings, counting
/// bytes.
///
/// Bytes and not code points, deliberately: an engine that answers a
/// multi-byte character where the truth has an ASCII one is wrong by more
/// than one edit under this, and that is the right sign — it is a
/// substitution the reader cannot draw at all. Both hosts' engines answer
/// UTF-8, so the two sides are in the same units.
[[nodiscard]] std::size_t journal_edit_distance(std::string_view a,
                                                std::string_view b);

/// The same over two sequences of words.
[[nodiscard]] std::size_t journal_edit_distance(
    std::span<const std::string_view> a, std::span<const std::string_view> b);

/// `candidate` against `reference`, by character and by word. Both
/// normalize first; both answer an untaken score when `reference` is
/// empty or either side is longer than `journal_score_limit`.
[[nodiscard]] journal_score journal_character_score(std::string_view reference,
                                                    std::string_view candidate);
[[nodiscard]] journal_score journal_word_score(std::string_view reference,
                                               std::string_view candidate);

/// Every corrected item of `store`, scored against its own correction.
///
/// An item with no correction is not in the result and is not counted:
/// there is nothing to compare it to, and scoring it against itself would
/// put a perfect zero into the aggregate for every entry nobody has
/// checked — which would make the number go *down* as a player found more
/// mistakes. That is the one way this measurement could have been made
/// actively misleading, so it is written down here rather than left to be
/// rediscovered.
[[nodiscard]] journal_store_report score_journal_store(
    const journal_store& store);

}  // namespace amberfolio::host
