// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal text store: what OCR read, what the player fixed, and which
// of the two the reader shows (M5-E3, #174).
//
// This is the product of the whole ingestion. #175's in-game reader has
// one input and this is it: entry number in, text out.
//
//
// It lives on the player's machine, and only there
// -----------------------------------------------
//
// The text in a store is the player's own document, read off the player's
// own copy, on the player's own machine. It is the one thing in this
// project that *is* content, and the rule about it is therefore the
// strongest one there is: no store, no fragment of one, and no fixture
// resembling one ever enters this repository, an issue, or a commit
// message. What may be written down about a store is what may be written
// down about any artifact — how many entries it has, and its SHA-256
// (`fingerprint()`, which exists so that a maintainer can report an
// ingestion on #174 without reporting a word of it).
//
// Where it goes is a host's business, because files are (PLAN.md §4):
//
//   * the desktop host writes one file, beside where M6's configuration
//     will live (`--journal-store` overrides it, and the SDL host's
//     `--help` says where the default is);
//   * the browser serializes one into its own key-value storage and reads
//     it back when the page next loads (M5-E3f), because an ingestion of
//     a real edition is minutes of OCR and asking for it on every visit
//     is not a thing to ask. It is `localStorage` and not IndexedDB, and
//     that is not a downpayment on M6: what M6 owes a browser is the
//     player's *disk*, which is megabytes of binary; this is one small
//     string wanted synchronously the moment the module comes up.
//
// So this object holds text and serializes it, and never opens anything.
//
//
// Two texts per entry, and only one of them is ever overwritten
// -----------------------------------------------------------
//
// #174 asks for a store a player can correct, whose corrections survive
// re-ingestion. That is one sentence and it decides the whole shape: each
// entry carries what the engine read (`scanned`) and, if a person has
// been in there, what they wrote (`corrected`). Ingestion replaces the
// first and never touches the second; the reader asks for `text()`, which
// is the correction where there is one and the scan otherwise.
//
// The alternative — one text, corrected in place — cannot tell "the
// player fixed this" from "the engine happened to get it right", so a
// re-ingestion with a better engine either destroys every correction or
// keeps every mistake. Two fields is the whole fix, and it costs a line.
//
//
// The format is text, on purpose
// ------------------------------
//
// A store is a file a player may want to edit, hand to somebody who is
// re-transcribing an entry properly, or diff after re-ingesting with a
// newer engine. So it is UTF-8 lines, with each text length-prefixed so
// that a transcription containing the word `entry` at the start of a line
// cannot be mistaken for a header. Strict on the way in: a file that is
// not exactly this is `not_a_store`, never a file half-read.
//
//   amberfolio-journal 3
//   edition <64 hex>
//   engine <one line>
//   scanned <kind> <number> <bytes>
//   <bytes bytes><newline>
//   corrected <kind> <number> <bytes>
//   <bytes bytes><newline>
//   seen <kind> <number> <month> <day> <hour> <minute> <read>
//
// The `seen` lines are the journal's own log (M5-E4b, #222) — what the
// game has told this player to read, newest first, with the moment it
// said so and whether they have opened it since. They carry no text, so
// they have no length and no body.
//
// **It is in this file rather than beside a save**, which is a decision
// and not an oversight. The automap keeps a snapshot per save slot because
// a map of explored squares genuinely differs between two parties and
// showing the wrong one misleads. A journal log does not: the journal is
// the *player's* book, and what they have read, they have read.
//
// The version is the first token of the first line so that a store from a
// later format is refused by a build that would misread it, which is the
// same courtesy `automap_store`'s header pays.
//
// **Every version this project has written is still read.** A version 2
// store has no `seen` lines, which is a player who has been cited nothing
// yet — a true statement about an old store, not an error.
//
// **Version 1 is still read** (M5-E3d, #218). It had no `<kind>` because
// there was one section, so every record in one is a journal entry and
// reading it as such loses nothing. Refusing it instead would have thrown
// away a player's corrections to make a point, and a version field exists
// so that a build can tell what it is holding — not so that it can
// decline to. A store read from version 1 is written back as version 2.
//
// `<kind>` is one lower-case word (`machine::journal_kind_name`) rather
// than a number, because a person is expected to open this file and edit
// it, and `scanned tale 4` says what `scanned 1 4` does not.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {

/// The format version this build writes.
inline constexpr std::uint32_t journal_store_version = 3;

/// The oldest it reads. See the format above: a version 1 store is a
/// store of journal entries and nothing is lost by saying so.
inline constexpr std::uint32_t journal_store_oldest_version = 1;

/// The first line's keyword — the thing that says a file is one of ours
/// before anything else is believed about it.
inline constexpr std::string_view journal_store_magic = "amberfolio-journal";

/// The file the desktop host writes when it was not told otherwise, under
/// the per-user data directory M6's configuration will share
/// (`journal_store_default_path()` in the SDL host).
inline constexpr std::string_view journal_store_filename = "journal.txt";

/// One item's text.
struct journal_text {
  /// Which section it is in. Without it `number` names three things —
  /// `journal_facts.h`'s `journal_kind` has the argument.
  journal_kind kind{journal_kind::entry};
  std::uint16_t number{};
  /// What the engine read, replaced on every ingestion.
  std::string scanned;
  /// What a person wrote, never touched by an ingestion. Empty when
  /// nobody has been in there.
  std::string corrected;

  /// What a reader shows: the correction if there is one.
  [[nodiscard]] std::string_view text() const noexcept {
    return corrected.empty() ? std::string_view(scanned)
                             : std::string_view(corrected);
  }
};

/// Entry number to text, for one edition.
class journal_store {
 public:
  journal_store() = default;

  /// Which edition this store is of, as 64 lowercase hex characters, and
  /// what engine last read it.
  ///
  /// Recorded rather than checked: a store is a store *of* a document,
  /// and the ingester refuses to write a store whose edition is not the
  /// document it was handed (`journal_ingest.h`). Keeping the fingerprint
  /// here is what makes that check possible at all.
  [[nodiscard]] std::string_view edition() const noexcept { return edition_; }
  void set_edition(std::string_view fingerprint) {
    edition_ = fingerprint;
    changed_ = true;
  }

  [[nodiscard]] std::string_view engine() const noexcept { return engine_; }
  void set_engine(std::string_view what) {
    engine_ = what;
    changed_ = true;
  }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] const std::vector<journal_text>& entries() const noexcept {
    return entries_;
  }

  /// The item `what` names, or null.
  [[nodiscard]] const journal_text* find(
      machine::journal_citation what) const noexcept;

  /// What a reader shows for `what` — empty for an item that is not there
  /// or has no text at all.
  [[nodiscard]] std::string_view text(
      machine::journal_citation what) const noexcept;

  /// Ingestion's write: what the engine read for `what`. Leaves any
  /// correction alone, which is the whole point of the pair.
  ///
  /// False if the store is full or the text is longer than
  /// `journal_max_entry_bytes`.
  [[nodiscard]] bool record_scan(machine::journal_citation what,
                                 std::string_view text);

  /// A person's write.
  [[nodiscard]] bool correct(machine::journal_citation what,
                             std::string_view text);

  /// How many entries have any text at all, and how many have a
  /// correction — the two numbers a host reports after an ingestion.
  [[nodiscard]] std::size_t recognized() const noexcept;
  [[nodiscard]] std::size_t corrections() const noexcept;

  /// Everything gone, header included.
  void clear();

  /// The file's bytes.
  [[nodiscard]] std::string serialize() const;

  /// `text` back into this store, replacing everything in it.
  ///
  /// Strict: anything that is not exactly the format above leaves the
  /// store as it was and answers a reason. A partly-read store is the one
  /// outcome worth going out of the way to make impossible — it is a
  /// player's transcription with a hole in it, and nothing downstream
  /// would be able to tell.
  ///
  /// Strict about the format, and not about line endings: CRLF is
  /// normalized to LF first, so a store that has been through an editor
  /// on Windows still reads. Every length in the format counts bytes, so
  /// without that a file Notepad had saved would disagree with its own
  /// counts on every record — a correct refusal, and a useless one.
  [[nodiscard]] journal_trouble parse(std::string_view whole);

  /// The journal's log: what the game has cited, newest first.
  ///
  /// Held here so it outlives the machine, and handed to
  /// `machine::journal_state` for the reader to draw from — which is the
  /// same direction the automap's exploration goes, and for the same
  /// reason: it is observation, not machine state, so a host owns it and
  /// core borrows it.
  [[nodiscard]] std::span<const machine::journal_seen_row> seen()
      const noexcept {
    return seen_;
  }

  /// Replace the log wholesale. What `journal_seen` does with what the
  /// machine's own log holds after a citation.
  ///
  /// Keeps at most `machine::journal_log_rows`, because that is what the
  /// machine will hand back and a store that kept more would grow a tail
  /// no reader could ever show.
  void set_seen(std::span<const machine::journal_seen_row> rows);

  /// Whether this store has moved since a host last wrote it out.
  ///
  /// The same shape the automap's sidecar has, and for the same reason: a
  /// host that wrote the file on every citation would write it far more
  /// often than anything changed.
  ///
  /// **Every write raises it**, which is more than it used to be: it was
  /// `set_seen()` alone (M5-E4b, #222), because the log was the only
  /// thing that moved while a machine was running. #229 made it the
  /// store's flag rather than the log's, because the caller it exists for
  /// now is a host deciding whether to *persist the store* — and a
  /// player's correction that did not raise it is a correction that
  /// quietly does not get saved. So `record_scan`, `correct`,
  /// `set_edition`, `set_engine`, `clear` and `set_seen` all raise it,
  /// and a write that was refused (too long, no room) raises nothing.
  ///
  /// **`parse()` is the exception**, and deliberately: a store read in
  /// from a file or a browser's drawer came *from* a host, which
  /// therefore already holds those bytes. Raising it there would have
  /// every host write back, on startup, exactly what it had just read.
  ///
  /// **The lowering is the caller's.** A store cannot know whether a host
  /// got the bytes to disk, and a flag that cleared itself on read would
  /// lose a correction made between the read and the write.
  [[nodiscard]] bool changed() const noexcept { return changed_; }
  void clear_changed() noexcept { changed_ = false; }

  /// The SHA-256 of `serialize()`.
  ///
  /// Present so a maintainer can say what came out of an ingestion of
  /// their own document without saying any of it: a fingerprint names a
  /// thing without carrying a byte of it, which is what CONTRIBUTING.md
  /// permits to be written down about an artifact and is the only kind of
  /// report #174's exit criterion asks for.
  [[nodiscard]] sha256_digest fingerprint() const;

 private:
  [[nodiscard]] journal_text* entry_for(machine::journal_citation what);

  std::string edition_;
  std::string engine_;
  /// Kept sorted by kind and then by number, so a serialization is a
  /// function of the content and not of the order things were written in
  /// — which is what makes `fingerprint()` worth reporting.
  std::vector<journal_text> entries_;
  /// **Not sorted**, unlike the entries: this is a log and its order is
  /// its content. `fingerprint()` is still a function of the content,
  /// because the order is part of what was stored rather than an artefact
  /// of what was written first.
  std::vector<machine::journal_seen_row> seen_;
  bool changed_{false};
};

/// A store's read log, into the machine the reader draws it from.
///
/// **The one line of a store that does not travel through
/// `set_journal_store()`.** A store holds two things: the text of each
/// entry, which the reader asks for by number through the host-service
/// pointer, and the log of what the game has told this player to read.
/// The log lives in `machine::journal_state`, where it is *observation*
/// (`machine/journal.h`) — so it has to be *put* there, once, when a
/// store is loaded.
///
/// Here rather than in either host because both need it and the ordering
/// is easy to get wrong in a way nothing would notice: the store holds
/// the log newest first and so does the machine, and `note_seen` puts
/// each row on the **front**, so feeding them in stored order hands the
/// reader its own list upside down. It was written twice, and then only
/// one of the two was written at all (#237) — a browser forgot every `*`
/// on reload while a terminal did not.
///
/// Restoring what a store already holds is not the log *moving*, so the
/// changed flag is cleared: a host that wrote the file back afterwards
/// would be writing what it had just read.
void restore_journal_log(machine::journal_state& into,
                         const journal_store& from) noexcept;

}  // namespace amberfolio::host
