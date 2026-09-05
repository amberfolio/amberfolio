// SPDX-License-Identifier: AGPL-3.0-only
//
// The copies whose code-wheel challenge has been answered: M6-C1b, #292,
// and the half of #290 that makes "once" mean once.
//
// The seam (`machine/seam_code_wheel.cpp`) watches the program's own
// comparison, and when a person answers the challenge correctly it
// latches that and calls `code_wheel_answered`. The latch is
// configuration on the engine: a `reset()` keeps it, the serialization
// never sees it, and it lives exactly as long as the machine does. That
// is the right answer for fidelity and the wrong one for a player, who
// answered the question on Tuesday and should not be asked again on
// Wednesday. This is the other half — a host reads it out of the callout
// and writes it somewhere that outlives the process, and hands it back to
// the next machine before the first instruction runs.
//
// It is `journal_store.h`'s sibling in shape and its opposite in size:
// **this object holds a list of digests and serializes it, and never
// opens anything.** Where the bytes go is a host's business (PLAN.md §4)
// — a file beside the desktop host's other per-user data, this browser's
// own key-value drawer on the web.
//
//
// What it remembers, and what it deliberately does not
// ---------------------------------------------------
//
// One thing, per copy: **that the challenge was answered.** Not what the
// question was, not what the answer was, not when. A word from the
// program's own table is content and would never be written down here
// (CONTRIBUTING.md); the challenge's own selection is the program's; and
// a timestamp would be a fact about a person that nothing in this project
// needs.
//
// **Keyed by the program's SHA-256**, which is the fingerprint the seam
// is already keyed to (`machine/edition.h`). A player who owns two
// editions has answered for one of them, and an answer that carried over
// to a binary nobody had run would be this store making a claim about a
// copy it had never seen. It is also what makes the file honest to look
// at: a digest is a fact about a file, and it is the only kind of thing
// this project ever writes down about somebody's game.
//
//
// The format
// ----------
//
// Lines, in the shape `journal_store.h` uses and for its reasons — the
// version is the first token of the first line so a store from a later
// build is *refused* rather than half-read, and a file that is not
// exactly this is `not_a_store`:
//
//   amberfolio-code-wheel 1
//   answered <64 hex>
//
// One `answered` line per copy, in the order they were answered. No
// length prefixes, because there is no text in it; no comments, because
// a parser that skipped lines it did not understand would be a parser
// that could not tell a store from a shopping list.
//
// A player may delete the file, or a line of it, and be asked again. That
// is the whole of "forget it", and it is why the format is one a person
// can read.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/sha256.h"

namespace amberfolio::machine {
class machine;
}  // namespace amberfolio::machine

namespace amberfolio::host {

/// The format version this build writes, and the oldest it reads. One,
/// both: this is the first.
inline constexpr std::uint32_t code_wheel_store_version = 1;
inline constexpr std::uint32_t code_wheel_store_oldest_version = 1;

/// The first line's keyword — what says a file is one of ours before
/// anything else about it is believed.
inline constexpr std::string_view code_wheel_store_magic =
    "amberfolio-code-wheel";

/// The file the desktop host writes when it was not told otherwise, in
/// the per-user data directory it keeps the journal's text in.
inline constexpr std::string_view code_wheel_store_filename = "code-wheel.txt";

/// Why a store could not be read. `none` is a store; everything else
/// leaves the object untouched, because half a store is worse than none
/// (`journal_trouble` makes the same choice next door).
enum class code_wheel_trouble : std::uint8_t {
  none,
  /// The first line is not this project's, or the version is not a
  /// number: whatever this file is, it is not a store.
  not_a_store,
  /// A store from a build newer than this one. Refused rather than
  /// guessed at, and the file is left where it is.
  later_version,
  /// A line that is not `answered <64 hex>`.
  bad_line,
};

/// The printable name of a `code_wheel_trouble` — `ok`, `not-a-store`,
/// `later-version`, `bad-line`. Never null.
///
/// Here rather than in a host so that a desktop run and a browser run
/// report an unreadable store in the same words.
[[nodiscard]] const char* code_wheel_trouble_name(
    code_wheel_trouble why) noexcept;

/// The copies this player has answered for.
class code_wheel_store {
 public:
  code_wheel_store() = default;

  /// Whether `program` has answered. False for a store that has never
  /// heard of it, which is what a first launch looks like.
  [[nodiscard]] bool answered(const sha256_digest& program) const noexcept;

  /// Write `program` down. True if that changed anything — a copy
  /// already in the store is not news, and does not raise `changed()`,
  /// so a host does not rewrite a file for a run that told it what it
  /// knew.
  bool remember(const sha256_digest& program);

  /// Everything gone: the player asking to be asked again. Raises
  /// `changed()` if there was anything to forget.
  bool forget() noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return answered_.size(); }
  [[nodiscard]] bool empty() const noexcept { return answered_.empty(); }
  [[nodiscard]] std::span<const sha256_digest> copies() const noexcept {
    return answered_;
  }

  /// The store as its file: the header line and one `answered` line per
  /// copy, each ending in a newline.
  [[nodiscard]] std::string serialize() const;

  /// The same bytes back in, replacing whatever was here. Anything but
  /// `none` leaves the object exactly as it was.
  ///
  /// **A parse does not raise `changed()`**, for `journal_store`'s
  /// reason: what came out of the drawer came from the page, and a flag
  /// raised by reading would have every host write back on startup the
  /// bytes it had just read.
  [[nodiscard]] code_wheel_trouble parse(std::string_view whole);

  /// Whether this store has moved since a host last kept it, and the
  /// host saying it has now.
  ///
  /// The lowering is the caller's, and deliberately so: this object has
  /// no idea whether a file was written or whether `localStorage` took
  /// the bytes, so it cannot clear the flag on its own.
  [[nodiscard]] bool changed() const noexcept { return changed_; }
  void clear_changed() noexcept { changed_ = false; }

 private:
  std::vector<sha256_digest> answered_;
  bool changed_{false};
};

/// Tell `box` what `store` remembers about the program it has loaded, and
/// answer whether it was told anything.
///
/// The one line both hosts would otherwise write twice, and the reason it
/// is here: looking a digest up in a store is the same job on a desktop
/// and in a browser, and a page that did it in JavaScript would need the
/// fingerprint marshalled out of the module to answer a question the
/// module can answer.
///
/// False for a machine with no program loaded — there is nothing to look
/// up yet — and for a copy the store has never heard of, in which case
/// the seam watches and the game asks, which is the whole point.
bool apply_code_wheel_store(machine::machine& box,
                            const code_wheel_store& store);

}  // namespace amberfolio::host
