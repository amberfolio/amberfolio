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
// states, so the tree spells `SAVE\AFMAP.DAT` once.
//
// **The program's configuration file is in it, marked as a file the
// program only reads.** It sits outside the save directory, it ships
// with the game, and the configuration program beside it is the only
// thing that writes it — so a host is told, in the one place it is
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

/// One row: a path pattern, what the file is, whether a slot is
/// incomplete without it, and one line a host can show a player.
struct save_file {
  std::string_view pattern;
  save_file_kind kind{save_file_kind::slot};
  bool required{false};
  std::string_view about;
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

/// Which row of `layer` the path `path` is, if any.
///
/// The path is compared as `canonicalize()` left it — upper case, one
/// separator, no leading one — so a host cannot reach a different answer
/// by spelling a name differently (#146). A `<S>` matches only a letter
/// `layer.slots` lists and a `<N>` only an index inside
/// `layer.members`, so a stale record left by a larger party is not
/// claimed for a slot it is no longer part of.
[[nodiscard]] save_layer_row match_save_file(const save_layer& layer,
                                             const dos_path& path) noexcept;

/// The two files this build writes beside a playthrough's saves, whole,
/// so that `host::slot_store` and the table above cannot disagree about
/// their names. `automap.h` and `journal_store.h` have their formats.
inline constexpr std::string_view save_layer_directory = "SAVE";
inline constexpr std::string_view save_layer_automap_working =
    "SAVE\\AFMAP.DAT";
inline constexpr std::string_view save_layer_journal_working =
    "SAVE\\AFSEEN.DAT";

}  // namespace amberfolio::machine
