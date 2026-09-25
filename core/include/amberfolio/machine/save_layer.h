// SPDX-License-Identifier: AGPL-3.0-only
//
// The save layer: which files on the machine's filesystem are a
// playthrough's rather than the publisher's, and which of them make up
// save slot `S` (#208).
//
// `edition.h` says which program image this build knows; this says what
// that program *writes*. The two are the same kind of fact about the
// same set of files, which is why this is a sibling of that table rather
// than a second column in it: an edition is one line and a save layer is
// a dozen, and a table of one-line rows stops being a table the moment
// one row is a table.
//
//
// Why a host needs this stated rather than guessed
// ------------------------------------------------
//
// A browser splits its filesystem in two. The game's own files are
// read-mostly and large and stay where they were dropped; what a
// playthrough writes is small, changes constantly, has to survive a
// reload, and is the only part that will ever be synced anywhere. That
// boundary is not a convenience. The publisher's bytes are the
// publisher's and a page has no business copying them about; the
// player's own save is the player's. A host drawing that line from a
// filename heuristic — "anything under `SAVE\`", "anything written since
// the run began" — is guessing at exactly the place where guessing is
// worst, and PLAN.md §3's rule about not faking answers is no narrower
// here than it is over an unimplemented port.
//
// So the line is a fact table, gathered the way every other fact in this
// repository was: by watching what the program actually does with a real
// copy (`docs/hosts.md` §6 has the runs). A name here is a name the
// program builds; nothing is inferred from what a directory happened to
// contain.
//
//
// Patterns, because a slot is a family of files
// --------------------------------------------
//
// One save is not one file. It is a slot file plus a record per party
// member plus, for members who have them, what they carry and what they
// have memorized — and the member records are numbered and the whole
// family is spelled with the slot's letter in the middle of each name.
// A row here is therefore a *pattern*, with three placeholders:
//
//   * `<S>` — one slot letter, from `save_layer::slots`;
//   * `<N>` — one party-member index, `1` to `save_layer::members`;
//   * `<NAME>` — a DOS name the player chose, which this build cannot
//     enumerate and does not try to.
//
// `match_save_file()` is the other direction and the one a host writing
// a directory back actually calls: hand it a path, get the row, the slot
// letter and the member index. **The rows are tried in order and the
// first match wins**, the table running from the most specific name to
// the least, because `<NAME>.ITM` would otherwise swallow a member's
// `CHRDAT<S><N>.ITM` — the general row is last for that reason and not
// by accident.
//
//
// Where the rows are (#397)
// -------------------------
//
// A pattern is a **leaf**, and every row says which directory it is a
// leaf of: the **save directory** or the **current directory**
// (`save_file_anchor`). Neither is a constant, because the program does
// not have one. It reads its save directory out of line 4 of its own
// configuration file, `POOL.CFG`, which it opens relative to the
// directory it was started in (`dos.h`, "The current directory") — and
// the copies on sale spell that line differently:
//
//   * the archive release keeps its files at the root and says `C:\SAVE\`;
//   * one storefront's copy sits at `C:\POOLRAD` and says `C:\POOLRAD\`,
//     so its saves are **beside the game's own files**;
//   * another's sits at `C:\POOLRAD` too and says `C:\POOLRAD\SAVE\`.
//
// So `save_layer_places` carries both directories and
// `read_save_directory()` answers the first one the way the program
// would: `POOL.CFG` read relative to the current directory, line 4
// canonicalized against it. A configuration file that is absent or that
// does not say is **said**, never filled in with a likely directory — a
// host that cannot be told where the saves are should persist nothing
// rather than persist a guess, which is the rule for a program with no
// table at all.
//
// **The first-match rule has a second job when the two directories are
// one.** A copy whose saves are beside its game files puts the
// configuration file in the save directory, and that file must still be
// the program's and not a playthrough's — so the `POOL.CFG` row sits
// ahead of the three `<NAME>` rows, and no pattern in the table can
// equal a file the publisher ships (a test holds that against every
// edition's file list).
//
// What `required` means
// ---------------------
//
// That the slot is incomplete without it: the program writes it for
// every save and reads it back for every load. The optional rows are not
// optional because the program tolerates their absence carelessly — it
// opens them, is told the file is not there, and carries on, which is a
// legitimate DOS answer and the one this project logs rather than fakes
// (`docs/machine.md` §5). They are absent because there was nothing to
// put in them: a member carrying nothing has no items file, and one who
// casts nothing has no spell file. A save that finds an old items file
// where the member now carries nothing *unlinks* it, so the absence is
// written down rather than left to a stale file to contradict.
//
// `required` is false for every row that is not part of a slot, where
// the question does not arise.
//
//
// Two things this table deliberately does say
// -------------------------------------------
//
// **This build's own sidecars are in it** (`host::slot_store`), even
// though the program has never heard of them. A page splitting its
// filesystem has to put them on the playthrough's side — they are what
// the automap and the reader learnt while that party played, and a
// player who loses them loses the map they filled in — and a table that
// listed only the program's files would leave a host to work that out
// alone. Their names live here, beside the format `automap.h` already
// states, so the tree spells `AFMAP.DAT` once. They go in the save
// directory, whichever one that is, because they are about the saves
// beside them.
//
// **The program's configuration file is in it, marked as a file the
// program only reads.** It sits in the directory the program was started
// in — which on one of the copies above is the save directory too — it
// ships with the game, and the configuration program beside it is the
// only thing that writes it — so a host is told, in the one place it is
// looking, to leave it with the game files. A boundary is as much about
// what is on the other side of it.
//
//
// What is deliberately not here
// -----------------------------
//
// Anything inside any of these files. A filename is a fact; a layout is
// a fact this table has no use for and does not carry, and a byte of
// content is what CONTRIBUTING.md forbids outright. Nothing here parses
// a save, and `match_save_file()` never opens one.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

/// What one file in the save layer is to a playthrough.
enum class save_file_kind : std::uint8_t {
  /// The saved game itself. One per slot, and the file the program opens
  /// to find out whether a slot exists at all.
  slot,
  /// One party member's record inside a slot.
  member,
  /// The list of characters that are on the disk but in no party.
  roster,
  /// A character kept under a name the player chose, in or out of a
  /// party.
  character,
  /// The program's settings. Read by the game, written by the
  /// configuration program that ships beside it.
  config,
  /// A file of this build's own, beside the program's saves and never
  /// inside one (`host::slot_store`).
  sidecar,
};

/// The printable name of a `save_file_kind` — `slot`, `member`,
/// `roster`, `character`, `config`, `sidecar`. Never null.
///
/// Words rather than a number across the ABI, for the reason
/// `seam_reason_name` is: a host that has to keep its own table of
/// spellings is a host that can disagree with this one.
[[nodiscard]] const char* save_file_kind_name(save_file_kind kind) noexcept;

/// Which directory a row's pattern is a leaf of (see "Where the rows
/// are" above).
enum class save_file_anchor : std::uint8_t {
  /// The directory the program saves into: line 4 of its configuration
  /// file.
  save_directory,
  /// The directory the program was started in (`dos.h`).
  current_directory,
};

/// One row: a leaf pattern, the directory it is a leaf of, what the file
/// is, whether a slot is incomplete without it, and one line a host can
/// show a player.
struct save_file {
  std::string_view pattern;
  save_file_kind kind{save_file_kind::slot};
  bool required{false};
  std::string_view about;
  save_file_anchor anchor{save_file_anchor::save_directory};
};

/// One edition's save layer.
struct save_layer {
  /// The SHA-256 of the program image this is a fact about, as 64
  /// lowercase hex characters. Keyed the way a seam is keyed
  /// (`seam.h`), and for the same reason: what a program writes is a
  /// fact about that binary, so the binary is what names it.
  std::string_view fingerprint;
  /// The slot letters, in the order the program asks the directory about
  /// them.
  std::string_view slots;
  /// The largest party-member index a slot's records are numbered with.
  /// `<N>` runs from 1 to this.
  std::uint8_t members{0};
  /// The rows, most specific pattern first (see the header comment).
  std::span<const save_file> files;
};

/// The save layer of the program `program` is the image of, or null for
/// one this build has no table for — an unrecognized binary, or one it
/// knows the name of and not the files of.
///
/// Null is a real answer and not a failure, exactly as `find_edition()`
/// answering null is: a host that cannot be told which files are the
/// player's should persist nothing rather than persist a guess.
[[nodiscard]] const save_layer* save_layer_for(
    const sha256_digest& program) noexcept;

/// What `match_save_file()` found.
struct save_layer_row {
  /// The row, or null when `path` is not in the save layer at all — in
  /// which case every other field is zero and the file belongs with the
  /// game's own.
  const save_file* file{nullptr};
  /// Its index in `save_layer::files`, for a host that wants to name the
  /// row it matched.
  std::size_t index{0};
  /// The slot letter the path belongs to, or 0 for a row that is not
  /// per-slot.
  char slot{0};
  /// The party-member index, 1 to `save_layer::members`, or 0 for a row
  /// that names no member.
  std::uint8_t member{0};
};

/// The two directories a layer's rows are leaves of, for one copy.
struct save_layer_places {
  /// Where the program saves: `read_save_directory()`'s answer.
  dos_path save_directory{};
  /// Where it was started: `dos_services::current_directory()`.
  dos_path current_directory{};

  [[nodiscard]] const dos_path& of(save_file_anchor anchor) const noexcept {
    return anchor == save_file_anchor::save_directory ? save_directory
                                                      : current_directory;
  }
};

/// Which row of `layer` the path `path` is, if any, on a copy laid out as
/// `places` says.
///
/// The path is compared as `canonicalize()` left it — upper case, one
/// separator, no leading one — so a host cannot reach a different answer
/// by spelling a name differently (#146). A row matches only a path
/// directly inside its own directory. A `<S>` matches only a letter
/// `layer.slots` lists and a `<N>` only an index inside `layer.members`,
/// so a stale record left by a larger party is not claimed for a slot it
/// is no longer part of.
[[nodiscard]] save_layer_row match_save_file(const save_layer& layer,
                                             const save_layer_places& places,
                                             const dos_path& path) noexcept;

/// The longest `spell_save_pattern()` writes: a full-depth directory and
/// the longest pattern in the table.
inline constexpr std::size_t save_pattern_capacity = max_host_path_text + 32;

/// Row `file`'s pattern with its directory in front, on a copy laid out
/// as `places` says — `SAVE\SAVGAM<S>.DAT`, `POOLRAD\POOL.CFG` — relative
/// to the root with no leading separator, which is how the table spelled
/// every row before its directories moved. NUL-terminated into `out`;
/// answers the length, or zero if it did not fit.
std::size_t spell_save_pattern(const save_file& file,
                               const save_layer_places& places,
                               std::span<char> out) noexcept;

// --- Where the program saves -------------------------------------------

/// The program's configuration file, opened relative to the directory
/// it was started in, and the line of it that names the save directory
/// (one-based). Facts about every edition this build knows, gathered the
/// way the rows were: by watching a real copy open them.
inline constexpr std::string_view save_layer_config_file = "POOL.CFG";
inline constexpr std::size_t save_layer_config_save_line = 4;

/// Why `read_save_directory()` could not say.
enum class save_directory_trouble : std::uint8_t {
  /// It could: the answer is the directory.
  none,
  /// There is no configuration file in the current directory.
  no_config,
  /// There is one and it would not open or read.
  unreadable,
  /// It ends before the line that names the save directory, or that line
  /// is empty.
  too_short,
  /// That line is not a path this machine can name: another drive, or a
  /// component no DOS short name can equal.
  not_a_path,
};

/// The printable name of a `save_directory_trouble` — `none`,
/// `no-config`, `unreadable`, `too-short`, `not-a-path`. Never null.
[[nodiscard]] const char* save_directory_trouble_name(
    save_directory_trouble what) noexcept;

/// What `read_save_directory()` found.
struct save_directory_answer {
  dos_path directory{};
  save_directory_trouble trouble{save_directory_trouble::no_config};

  [[nodiscard]] bool ok() const noexcept {
    return trouble == save_directory_trouble::none;
  }
};

/// The directory the program saves into on `fs`, started in
/// `current_directory`: `save_layer_config_file` opened relative to it,
/// line `save_layer_config_save_line` canonicalized against it — the two
/// resolutions the program's own opens make. Nothing else is consulted
/// and nothing is assumed: an absent or malformed file answers its
/// trouble, and the root beside it is not a claim.
///
/// The directory need not exist yet. A copy that has never been saved in
/// may not have made it, and where the program *will* save is still the
/// answer.
[[nodiscard]] save_directory_answer read_save_directory(
    filesystem& fs, const dos_path& current_directory);

/// `places` for `fs` as a program started in `current_directory` would
/// see it, or false — and `places` untouched — when
/// `read_save_directory()` could not say. The one call a host makes
/// before it matches anything.
[[nodiscard]] bool save_layer_places_of(filesystem& fs,
                                        const dos_path& current_directory,
                                        save_layer_places& places);

/// The two files this build writes into the save directory, as leaves,
/// so that `host::slot_store` and the table above cannot disagree about
/// their names. `automap.h` and `journal_store.h` have their formats.
inline constexpr std::string_view save_layer_automap_working = "AFMAP.DAT";
inline constexpr std::string_view save_layer_journal_working = "AFSEEN.DAT";

}  // namespace amberfolio::machine
