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
//   * the desktop host writes one file, beside its config
//     (`docs/hosts.md` 2a); `--journal-store` overrides it, and the line
//     the host prints after an ingestion says which file it used;
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
//   amberfolio-journal 5
//   edition <64 hex>
//   engine <one line>
//   scanned <kind> <number> <bytes>
//   <bytes bytes><newline>
//   corrected <kind> <number> <bytes>
//   <bytes bytes><newline>
//   picture <kind> <number> <nth> <width> <height> <bytes>
//   <bytes bytes><newline>
//
// A version 4 store had `seen` lines here too — the journal's own log
// (M5-E4b, #222) — and version 5 does not. Where they went, and why, is
// the section below.
//
//
// The pictures, and why they are here rather than in a sidecar (#328)
// -------------------------------------------------------------------
//
// Several of a journal's entries are drawings, and what an OCR engine
// reads off one is its caption. So a picture is reduced once, at
// ingestion, to four tones in the reader's own box
// (`journal_picture.h`), and kept — because re-deriving it would be a
// decode and a resample per page turn, and because the answer is a fact
// about the player's own document and belongs beside their
// transcription of it.
//
// #328 proposed a sidecar, on the shape the automap's own store has,
// and this is the one place its instructions were not followed. A
// picture record rides **every path a store already has**: both hosts,
// the five `Machine` methods the ABI grew for it (#229), the
// changed-flag rule, `drive.mjs --journal-store`, the
// clear-on-a-different-edition rule, and `fingerprint()`. A second file
// would need every one of those again, on two hosts, to hold something
// that is keyed by the same *(section, number)* and thrown away by the
// same events. What it costs is size, and the arithmetic is small: the
// one tabled edition's fourteen pictures are about a hundred and
// seventy kilobytes packed, and about two hundred and thirty as text,
// beside a browser drawer that holds five megabytes.
//
// A picture's `<bytes>` are the **base64** of the packed levels, and the
// count is of that text rather than of the bytes it stands for, so the
// same length-prefixed reader handles it. Base64 and not raw, because
// this is a file a person opens: a run of arbitrary bytes in it would
// make it a binary file that happens to begin with words.
//
// **A store from before this is read and loses nothing.** A version 3
// store has no pictures, which is a player who ingested with a build
// that could not make one — and re-ingesting is what fixes that, which
// is exactly what re-ingesting is for.
//
// **It is in this file rather than beside a save**, which is a decision
// and not an oversight. It is a picture of the player's own document,
// like the text beside it, and re-deriving it is a re-ingestion.
//
//
// The read log left this file, and the text did not (#351)
// --------------------------------------------------------
//
// A store holds facts about a **document**: what an engine read off the
// player's copy, what they corrected, and what its drawings look like
// reduced. Those are true of the copy however many parties the player
// runs, so the file lives in the per-user data directory and is shared by
// all of them, which is right.
//
// The read log is not one of those. Which entries the game has sent this
// player to, when it said so, and whether they have opened them since is
// a fact about a **playthrough** — and a player running two parties had
// one list between them, so each was told it had already been sent
// somewhere it had never been. It is the same argument the automap's
// exploration answered correctly and this answered wrongly, so it now
// gets the same answer: a sidecar beside the save, per slot, off unless
// the player asked (`slot_store.h`, `\SAVE\AFSEEN<L>.DAT`).
//
// **The rows still live here**, in this object, and travel to the reader
// exactly as they did: `set_seen` puts them in, `restore_journal_log`
// hands them to the machine, `seen()` reads them back. What changed is
// where a host *keeps* them between runs, which is why `serialize()` no
// longer writes them and `changed()` no longer rises for them
// (`log_changed()` does instead). Nothing above the store moved.
//
// **A version 4 store's `seen` lines are still read**, into exactly the
// same rows, and are then this run's working log: it is the one list the
// player accumulated before slots existed, and the first save writes it
// to a slot while the first load replaces it. That is the working
// table's own semantics next door, applied to the one migration there
// will ever be. Written back, the file is version 5 and the lines are
// gone — which is the point, and is not a loss, because by then they are
// in `\SAVE\AFSEEN.DAT` for anyone who asked for one.
//
// The version is the first token of the first line so that a store from a
// later format is refused by a build that would misread it, which is the
// same courtesy `slot_store`'s exploration header pays.
//
// **Every version this project has written is still read.** A version 2
// store has no `seen` lines, which is a player who has been cited nothing
// yet — a true statement about an old store, not an error; a version 3
// store has no `picture` records, on the same reading. A version 4 store
// has `seen` lines and they are read, above.
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_picture.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {
class machine;
}  // namespace amberfolio::machine

namespace amberfolio::host {

/// The format version this build writes.
inline constexpr std::uint32_t journal_store_version = 5;

/// The oldest it reads. See the format above: a version 1 store is a
/// store of journal entries and nothing is lost by saying so.
inline constexpr std::uint32_t journal_store_oldest_version = 1;

/// The first line's keyword — the thing that says a file is one of ours
/// before anything else is believed about it.
inline constexpr std::string_view journal_store_magic = "amberfolio-journal";

/// The file the desktop host writes when it was not told otherwise, under
/// the per-user data directory it shares with the config and the
/// answered code wheels (`per_user_path()` in the SDL host).
inline constexpr std::string_view journal_store_filename = "journal.txt";

// --- The read log's sidecar (#351) ------------------------------------
//
// The rows that left this file go beside the save, one file per slot,
// written and read by `slot_store.h`. The *format* is here, beside the
// rows it is a picture of, for the reason the exploration sidecar's is in
// `machine/automap.h` beside its records: a store owns what its own bytes
// mean, and a host is not the place to settle a layout question.
//
// Binary, and fixed-stride, where the store's own file is text on
// purpose. The store is text because a player opens it to correct a
// transcription; nobody hand-edits a list of what the game said and
// when. What a fixed stride buys is a fixed buffer: this is written from
// a seam's host callout, and a host has no business heap-allocating
// there.
//
//     off len  what
//     0   3    'A' 'F' 'S'
//     3   1    the layout version
//     4   2    how many rows follow, little-endian
//     6   2    the bytes in one row, little-endian
//
// and then that many rows of, newest first, which is the order the log
// is in everywhere else:
//
//     0   1    the section (`machine::journal_kind`)
//     1   2    the number in it, little-endian
//     3   1    month
//     4   1    day
//     5   1    hour
//     6   1    minute
//     7   1    non-zero if the player has opened it since
//
// The stride is in the header so that a later version can grow a row and
// a build that met one would refuse it whole rather than read every row
// at the wrong offset — the same courtesy, and the same header, the
// exploration sidecar pays.
inline constexpr std::array<char, 3> journal_log_sidecar_magic{'A', 'F', 'S'};
inline constexpr std::uint8_t journal_log_sidecar_version = 1;
inline constexpr std::size_t journal_log_sidecar_header_bytes = 8;
inline constexpr std::size_t journal_log_sidecar_record_bytes = 8;

/// The largest one can be: every row the machine's log will hand back.
inline constexpr std::size_t journal_log_sidecar_capacity =
    journal_log_sidecar_header_bytes +
    (machine::journal_log_rows * journal_log_sidecar_record_bytes);

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

  /// Every picture of `what`, in printed order (#328). Empty for an
  /// entry that is prose, which is most of them.
  [[nodiscard]] std::span<const journal_picture> pictures(
      machine::journal_citation what) const noexcept;

  /// One of them, or null.
  [[nodiscard]] const journal_picture* picture(machine::journal_citation what,
                                               std::uint8_t nth) const noexcept;

  /// How many this store holds in all — the number a host reports after
  /// an ingestion, beside how many entries it recognized.
  [[nodiscard]] std::size_t picture_count() const noexcept {
    return pictures_.size();
  }

  /// Ingestion's other write.
  ///
  /// **It replaces rather than merges**, one picture at a time, and
  /// there is no `corrected` beside it: the two-texts rule
  /// (`journal_text`) exists because a person edits a transcription, and
  /// nobody is going to hand-edit a base64 bitmap. A better reduction is
  /// a re-ingestion, which is what a version field is for.
  ///
  /// False for a picture bigger than the reader's own box, for one whose
  /// bytes are not the size its shape says, or for a store that is full.
  [[nodiscard]] bool record_picture(journal_picture what);

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

  /// The log gone, and nothing else (#351).
  ///
  /// What a slot's snapshot is read **over**: the party now in the
  /// machine is that slot's party, and what the last one was told has
  /// nothing to do with it. A slot with no sidecar beside it clears the
  /// log and reads nothing, for the same reason `automap_state::
  /// forget_records()` exists — an empty log is the truth about a
  /// playthrough nobody recorded one for.
  ///
  /// Raises `log_changed()` only if there was something to forget.
  void forget_seen();

  // --- the log's sidecar (#351) ----------------------------------------
  //
  // Its layout is above. These three mirror `automap_state`'s own three,
  // deliberately: `slot_store` writes both files through one pair of
  // calls, and two stores that answered a different shape would put the
  // difference in the host.

  /// How many bytes `write_log_sidecar` would fill for the rows in hand.
  [[nodiscard]] std::size_t log_sidecar_bytes() const noexcept;

  /// The log into `out`. Answers how many bytes were written, and zero
  /// for a span too small — which a caller avoids by asking
  /// `log_sidecar_bytes()` first.
  [[nodiscard]] std::size_t write_log_sidecar(
      std::span<std::uint8_t> out) const noexcept;

  /// A sidecar back, **replacing** the whole log. False when the bytes
  /// are not one this build knows how to read, and then the log is left
  /// exactly as it was — a file that is not ours is a reason to say so,
  /// not a reason to forget what a player was told.
  ///
  /// Raises `log_changed()` on success, because the rows in hand are now
  /// a different list from the one a host last wrote out. That is one
  /// redundant write of a file whose bytes were just read, and it is
  /// worth it: the alternative is a load whose log is only on disk under
  /// the slot it came from, and a working table that still holds the
  /// previous party's.
  [[nodiscard]] bool read_log_sidecar(std::span<const std::uint8_t> in);

  /// The same bytes as one line of text, and back.
  ///
  /// For a browser, which has no directory to put a sidecar in and keeps
  /// what it keeps in a key-value drawer of strings (M5-E3f). Base64 of
  /// exactly `write_log_sidecar`'s bytes, so the two hosts keep one
  /// format and a log written by either is a log the other would read —
  /// the same arrangement a picture record already has (§11).
  ///
  /// `parse_log` answers false on anything `read_log_sidecar` would
  /// refuse and on text that is not base64, and leaves the log alone.
  [[nodiscard]] std::string serialize_log() const;
  [[nodiscard]] bool parse_log(std::string_view text);

  /// Whether this store has moved since a host last wrote it out.
  ///
  /// The same shape the automap's sidecar has, and for the same reason: a
  /// host that wrote the file on every citation would write it far more
  /// often than anything changed.
  ///
  /// **Every write to the text raises it.** It was `set_seen()` alone
  /// (M5-E4b, #222), because the log was the only thing that moved while
  /// a machine was running. #229 made it the store's flag rather than the
  /// log's, because the caller it exists for now is a host deciding
  /// whether to *persist the store* — and a player's correction that did
  /// not raise it is a correction that quietly does not get saved. So
  /// `record_scan`, `record_picture`, `correct`, `set_edition`,
  /// `set_engine` and `clear` all raise it, and a write that was refused
  /// (too long, no room) raises nothing.
  ///
  /// **`set_seen()` no longer does** (#351). The log is not in this
  /// file any more, so a citation that raised this flag would have a host
  /// rewrite a player's whole transcription to record something that is
  /// not in it. `log_changed()` below is the log's own flag, and the two
  /// hosts write two different files off the two of them.
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

  /// Whether the read log has moved since a host last wrote it out
  /// (#351), on `changed()`'s own three terms: raised by `set_seen`,
  /// `forget_seen`, `read_log_sidecar` and `clear`; not raised by
  /// `parse()`, whose rows came from a host in the first place; lowered
  /// only by the caller, which alone knows whether the bytes reached a
  /// disk.
  [[nodiscard]] bool log_changed() const noexcept { return log_changed_; }
  void clear_log_changed() noexcept { log_changed_ = false; }

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
  /// Kept sorted by kind, then number, then `nth`, for `entries_`'s own
  /// reason: a serialization has to be a function of the content.
  std::vector<journal_picture> pictures_;
  /// **Not sorted**, unlike the entries: this is a log and its order is
  /// its content. `fingerprint()` is still a function of the content,
  /// because the order is part of what was stored rather than an artefact
  /// of what was written first.
  std::vector<machine::journal_seen_row> seen_;
  bool changed_{false};
  bool log_changed_{false};
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

/// The debug cheat that puts **everything the store holds** onto the
/// journal's log, so the `Notes` listing shows every entry, tale and
/// proclamation a player's ingestion produced and a person can open
/// each in turn on the game's own screen and read it against the scan
/// (#301). Answers how many it cited: the store's size, or zero for a
/// store with nothing in it, in which case nothing at all is touched —
/// the reader's own "you have not ingested a journal" is the answer a
/// person then gets, and not an empty log dressed up as one.
///
/// **A host action and not a seam**, which is the shape the issue
/// settled on and the reason this is here beside `restore_journal_log`:
/// the log is host-writable by design (`machine/journal.h`'s three
/// terms), the store it reads is the host's, and the core never
/// enumerates a store — it asks for one entry at a time. So nothing
/// under `core/` moves, no host service is added and the ABI is where it
/// was. It contradicts `journal.h`'s "a log, not an index" on purpose,
/// the way `cheat-wound-party` contradicts the game's own damage rules
/// on purpose (PLAN.md §5 item 6): off unless a person asks, and asked
/// for by somebody proof-reading rather than playing.
///
/// **Cited in reverse**, last row of the store first, because
/// `note_seen` puts each new line on the front and the store is sorted
/// by section and then by number: walking it backwards leaves Entry 1 at
/// the top of the listing, then Entry 2, and the tales and proclamations
/// under the entries in their own order — which is the order a person
/// with the book open wants. Every row arrives unread, so the listing's
/// `*` is a to-do list that empties as they go.
///
/// **Clears nothing.** `note_seen`'s own rule — a row already in the log
/// moves up, re-dated, and keeps its read flag — is what makes a second
/// call harmless: no row is doubled, nothing a person has read is unread
/// again, and a row the game cited that is not in the store stays in the
/// log under the cited ones. One `when` for every row, because a bulk
/// cite is one moment and the date column exists to tell one evening
/// from another.
///
/// **And it writes the store's own log**, through `set_seen`, the same
/// call the `journal_seen` host service makes when the seam says the log
/// moved — so the cited rows reach the file or the drawer through the
/// write every host already has, and survive a reload the way a real
/// citation does. Which means they survive *for good*, until the
/// store's `seen` lines are removed or the store is forgotten; both
/// hosts say so where they offer this.
std::size_t cite_all_journal(machine::journal_state& into, journal_store& store,
                             const machine::wall_time& when);

/// The same, stamped off `box`'s own seeded wall clock at its current
/// tick — the instant the seam itself stamps a citation with
/// (`seam_journal.cpp`), derived from virtual time and never read from
/// the host. What both hosts call.
std::size_t cite_all_journal(machine::machine& box, journal_store& store);

}  // namespace amberfolio::host
