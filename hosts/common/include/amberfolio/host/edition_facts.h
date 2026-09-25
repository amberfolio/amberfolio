// SPDX-License-Identifier: AGPL-3.0-only
//
// What an edition is *made of*, and what a host has in front of it
// (M6, #207).
//
// `machine/edition.h` answers one question — is this program image one
// this build knows? — and it answers it after a program has been loaded.
// Onboarding (#265) has to answer a different one, and answer it before
// anything is loaded: here are the files a player dropped, so what is
// this, what of it matched, and what is missing. That needs the whole
// requirement list rather than the one fingerprint the seam tables are
// keyed on, which is what this file is.
//
//
// The table is `data/editions.json`, and this is compiled from it
// ----------------------------------------------------------------
//
// A page wants the checklist while the wasm module is still downloading,
// so the table has to be a file a site can fetch on its own — it is
// attached to every release beside `manifest.json` (docs/hosts.md §5).
// A desktop host has no page and no fetch, so it needs the same facts
// linked in. Both are true and only one table may exist, so the JSON is
// the table and `hosts/common/CMakeLists.txt` turns it into the arrays
// behind `edition_requirements_table()` at configure time. Adding an
// edition is editing the JSON; nothing here is written twice, and the
// two cannot drift because there is only one of them.
//
// `tests/edition_facts_test.cpp` holds the other joints this table has
// to keep: every edition's `fingerprint` is one
// `machine::known_editions()` names, every document artifact is one
// `machine::known_documents()` names, and the repack's file list agrees
// with the pristine disk `tests/sessions/party.session` pins.
//
//
// Two rows, one program image
// ---------------------------
//
// A row is a *release*: what one seller ships. The release sold on GOG
// and Steam (`por-store`, the baseline and the first row) and a
// third-party repack of it (`por-archive`, the copy the session library
// was recorded on) boot the same START.EXE, so the machine has one
// edition and this table has two rows with one `fingerprint`. They
// differ in GAME.OVR (two bytes, inside the copy-protection overlay;
// docs/seams.md §5), in the launcher and configurator only the repack
// carries, and in where the copy is installed. `find_requirements()`
// answers the baseline for that fingerprint; `match_edition()` picks
// the row the offered files belong to.
//
//
// Nothing here reproduces anything
// --------------------------------
//
// A name, a size and a SHA-256 per file. That is the same class of fact
// `machine/edition.h` and the session descriptors already carry, and
// CONTRIBUTING.md lists it among the things this project may write down:
// a digest names a file without carrying a byte of it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/machine/document.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {

/// What one row of an edition's requirement list is.
enum class artifact_kind : std::uint8_t {
  /// A file the release ships, with a size and a digest. Matched on the
  /// digest.
  file,
  /// A file the copy is launched with rather than one the release ships
  /// the same bytes of: POOL.CFG, which each seller's launcher writes
  /// for its own install. Required **by name** — no size, no digest —
  /// because a player's copy carries whatever their launcher wrote.
  configuration,
  /// Something the player *holds* rather than something the machine
  /// runs (`machine/document.h`): a digest, no filename, and never
  /// required.
  document,
};

/// One row of an edition's requirement list.
struct edition_artifact {
  /// The name on the machine's filesystem. Empty for a document, which
  /// is whatever the player called their own file.
  std::string_view name;
  /// What a player would call it, when the name does not say. Empty for
  /// every file; the document rows carry the same words
  /// `machine::known_documents()` uses.
  std::string_view about;
  /// 64 lowercase hex characters. Empty for a configuration file.
  std::string_view fingerprint;
  /// Bytes. Zero when the table states no size, which is every row that
  /// is not a file.
  std::uint32_t size{0};
  artifact_kind kind{artifact_kind::file};
  /// What a document row is a document for, and `none` for every other
  /// row.
  machine::document_kind document{machine::document_kind::none};
  /// **Required means the copy is incomplete without it.** Every file
  /// the release ships is required, and so is its configuration file by
  /// name; the documents are not, which is PLAN.md §2's policy exactly —
  /// the binaries are the one artifact nothing runs without, and a
  /// missing document leaves its enhancement unavailable and changes
  /// nothing else. A row lists only what that release ships: a store
  /// copy is never stopped over a file only the repack carries. The
  /// save directory is not a row at all — the program makes it itself
  /// (INT 21h AH=39h) the first time it saves.
  ///
  /// It does not claim the machine *opens* every required file. Which
  /// of them this emulator ever reads is not a fact anybody here has
  /// measured, and a table that guessed would tell a player their copy
  /// was fine when it was not.
  bool required{false};
};

/// One release, as a host has to render it before anything is loaded.
struct edition_requirements {
  /// A stable key for whoever generates a roster from this. Never a
  /// display string.
  std::string_view id;
  /// The release's own name, for a player. Not the machine edition's
  /// name: two releases of one program image are one
  /// `machine::known_editions()` row and two of these.
  std::string_view name;
  /// One sentence on what this release is.
  std::string_view about;
  /// The file that boots it — what a host offers as the program to run,
  /// and the artifact whose digest is the `fingerprint` below.
  std::string_view boot;
  /// The boot file's SHA-256: the key `machine::find_edition()` looks
  /// this edition up by. Several rows may share one.
  std::string_view fingerprint;
  /// The directory of the machine's filesystem the copy's files live in,
  /// which is also the DOS current directory when the program is loaded
  /// — DOS spelling, from the root: `\POOLRAD` for the store release,
  /// where its own launcher mounts the copy and changes into it, and `\`
  /// for the repack. The default is the root.
  std::string_view install{"\\"};
  std::span<const edition_artifact> artifacts;
};

/// Every edition this build states requirements for, in the table's own
/// order.
[[nodiscard]] std::span<const edition_requirements>
edition_requirements_table();

/// The first row whose boot file has this fingerprint, or null. The
/// 64-hex spelling, so a caller holding a `machine::edition` can look up
/// a row without hashing anything.
///
/// **One program image can be several releases**, and this answers the
/// first of them in the table's order, which is the baseline: the table
/// puts the release sold today first. Which release a particular copy
/// *is* is a question about its other files, and `match_edition()` is
/// the answer to it.
[[nodiscard]] const edition_requirements* find_requirements(
    std::string_view fingerprint) noexcept;

/// One file a host has fingerprinted, in whatever order it found them.
struct offered_file {
  /// As the host has it. Only reported back; the matching is on bytes.
  std::string_view name;
  sha256_digest digest;
};

/// What a set of fingerprinted files turned out to be.
///
/// **Files are matched on the digest, never on the name.** A file that
/// was renamed still matches, and a file that carries a required
/// artifact's name and different bytes matches nothing — so it lands in
/// `unclaimed` while the artifact it is not lands in `missing`. Those two
/// lines together are the fact a player can act on ("START.EXE is here
/// and is a different build"), and no single-bucket answer says it.
///
/// **A configuration row is matched on the name**, case-insensitively
/// and on the last path component, because its bytes are whatever the
/// player's launcher wrote. It is matched only once a row has been
/// chosen by digest: a directory holding a POOL.CFG and nothing else is
/// not a copy of anything.
struct edition_match {
  /// The closest edition — the one the most of these files belong to by
  /// digest, the earlier row on a tie — or null when not one file
  /// belonged to any of them, which is the honest answer for a directory
  /// that holds something else entirely. Two releases of one program
  /// share most of their files, so it is the files one release has and
  /// the other does not that decide between them.
  const edition_requirements* edition{nullptr};
  /// Indices into `edition->artifacts`.
  std::vector<std::size_t> matched;
  /// Indices into `edition->artifacts`: required rows nothing offered.
  std::vector<std::size_t> missing;
  /// Indices into the offered files: the ones this edition does not
  /// name. A host prints them with their hashes, because that list plus
  /// those digests is what an edition nobody has fingerprinted yet
  /// looks like, and it is what a report against this build's tables
  /// would have to carry.
  std::vector<std::size_t> unclaimed;

  /// Whether every required artifact was offered. Not the same claim as
  /// `machine::find_edition()` recognising the boot file: a copy can be
  /// recognised and incomplete.
  [[nodiscard]] bool complete() const noexcept {
    return edition != nullptr && missing.empty();
  }
};

/// Match a fingerprinted set against every edition this build knows.
/// Allocates; a host calls it once when files arrive, not per frame.
[[nodiscard]] edition_match match_edition(
    std::span<const offered_file> offered);

}  // namespace amberfolio::host
