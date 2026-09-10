// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's configuration file (#382): the answers a player
// gave once, kept beside the journal's text, so a second launch needs no
// arguments at all.
//
// `main.cpp` had grown twelve flags for M6's material and nowhere to put
// an answer to any of them, and the browser path was already the
// reference — a person drops a directory and is told what it was, and
// the page remembers. This is the desktop's half.
//
//
// What it holds, and what it deliberately does not
// -----------------------------------------------
//
// The **settings a player chooses**: where their game is, which program
// to run, which seams they want on, which OCR engine to read a journal
// with, how loud, how fast, how big, and whether this build may write
// its own files beside their saves.
//
// Not the diagnostics. `--trace`, `--watch`, `--dump`, `--steps`,
// `--press`, `--pull`, `--record` and `--replay` are instruments a person
// points at a problem on the day they have one; a config that remembered
// `--trace` would be a config that made every launch after it slower for
// a reason nobody could see. Not `--journal PATH` either: an ingestion
// happens once, and a config that remembered it would re-read a document
// every launch.
//
// **Seam state is configuration, not machine state** (CLAUDE.md), so
// writing it down here is allowed — it never reaches the serialization,
// a checkpoint hashes none of it, and `reset()` keeps it. What it must
// never do is make a seam *on* for somebody who never chose one, which
// is why this file is written only when a player asks for it
// (`--remember`) and never as a side effect of a run that happened to
// name a seam. A driving script's `--seam` is not a player's choice.
//
//
// The format
// ----------
//
// `code_wheel_store.h`'s shape, and its reasons — the version is the
// first token of the first line, so a file from a later build is
// *refused* rather than half-read:
//
//     amberfolio-config 1
//     game-directory C:\Games\POR
//     program START.EXE
//     seam automap
//     seam journal
//     journal-ocr none
//     volume 75
//     mute off
//     speed at
//     scale 3
//     save-sidecars on
//
// One `key value` a line, the value being the whole rest of the line so
// that a path with spaces in it needs no quoting. `seam` is the one
// repeatable key. Every key is optional: a file says what somebody
// chose, and a key that is not there is not a choice.
//
// **Unforgiving on purpose**, for `code_wheel_store.h`'s reason: no
// comments and no blank lines, because a parser that skipped what it did
// not understand could not tell a config from a shopping list. A line
// this build cannot read refuses the whole file, and the reading names
// the line — a refusal that does not say which line is a refusal a
// player cannot act on.
//
// A refused file is **left where it is** and the run starts on the
// defaults. Never half-read, never repaired, never guessed at: this is
// the project's "log, don't fake" rule applied to a file somebody may
// have typed into by hand.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace amberfolio::sdl {

/// The format version this build writes, and the oldest it reads. One,
/// both: this is the first.
inline constexpr std::uint32_t desktop_config_version = 1;
inline constexpr std::uint32_t desktop_config_oldest_version = 1;

/// The first line's keyword — what says a file is one of ours before
/// anything else about it is believed.
inline constexpr std::string_view desktop_config_magic = "amberfolio-config";

/// The file the desktop host reads when it was not told otherwise, in
/// the per-user data directory it keeps the journal's text and the
/// answered code wheels in.
inline constexpr std::string_view desktop_config_filename = "config.txt";

/// Why a config could not be read. `none` is a config; everything else
/// leaves the object untouched, because half a config is worse than none
/// (`code_wheel_trouble` next door makes the same choice).
enum class config_trouble : std::uint8_t {
  none,
  /// The first line is not this project's, or the version is not a
  /// number: whatever this file is, it is not a config.
  not_a_config,
  /// A config from a build newer than this one. Refused rather than
  /// guessed at, and the file is left where it is.
  later_version,
  /// A line whose key this build does not know.
  unknown_key,
  /// A key this build knows, with a value it does not: `volume 300`,
  /// `speed pdp11`, `mute perhaps`.
  bad_value,
};

/// The printable name of a `config_trouble` — `ok`, `not-a-config`,
/// `later-version`, `unknown-key`, `bad-value`. Never null.
[[nodiscard]] const char* config_trouble_name(config_trouble why) noexcept;

/// What a reading of a config file turned out to be.
///
/// The line number and the line itself travel with the trouble because a
/// refusal has to be actionable: "line 4, `volume 300`" is something a
/// player can go and fix, where "bad-value" on its own is not. One-based;
/// zero when there is no line to name.
struct config_reading {
  config_trouble why{config_trouble::none};
  std::size_t line{0};
  std::string text;

  [[nodiscard]] bool ok() const noexcept { return why == config_trouble::none; }
};

/// The settings a config file can carry.
///
/// Every field is an `optional` and that is the whole point: a config
/// says what somebody chose, and the difference between "chose 100%" and
/// "never said" is exactly the difference the precedence rule below
/// turns on.
struct desktop_config {
  /// The directory holding the player's own copy, and the program in it
  /// to run — the two arguments this host has always taken positionally,
  /// so that a launch after the first needs neither.
  std::optional<std::string> game_directory;
  std::optional<std::string> program;

  /// The seams the player turned on, in the order the file names them.
  /// Present only when the file named at least one; a file that names
  /// none is a player who chose none, which is also the default.
  std::optional<std::vector<std::string>> seams;

  /// The OCR engine: a path to run, or `none`. Absent means "discover
  /// one" (`ocr_discovery.h`), which is what a player who never said
  /// gets and is the answer this host should reach for first.
  std::optional<std::string> journal_ocr;

  /// 0 to 100, the units `--volume` takes; and the latch beside it,
  /// which is a separate thing for the reason every mixer ever built has
  /// them as two (#148).
  std::optional<unsigned> volume_percent;
  std::optional<bool> muted;

  /// `xt`, `turbo`, `at` or `386` — `machine::speed_preset`'s own names,
  /// kept as text here so this unit needs nothing from core.
  std::optional<std::string> speed;

  /// The integer window scale, at least one.
  std::optional<unsigned> scale;

  /// Whether this build may keep its own files beside the player's
  /// saves (#173, #351). The one setting here that is a permission
  /// rather than a preference, which is why it is asked for rather than
  /// assumed.
  std::optional<bool> save_sidecars;

  /// Whether this config says anything at all.
  [[nodiscard]] bool empty() const noexcept;

  /// The config as its file: the header line and one line per setting
  /// that has one, each ending in a newline. A `desktop_config` with
  /// nothing in it serializes to the header alone, which is what
  /// `--forget-config` writes.
  [[nodiscard]] std::string serialize() const;

  /// The same bytes back in, replacing whatever was here. Anything but
  /// `config_trouble::none` leaves the object exactly as it was, and the
  /// reading says which line stopped it.
  ///
  /// CRLF is normalized first: this is a file a player may have opened
  /// in whatever their platform calls Notepad.
  [[nodiscard]] config_reading parse(std::string_view whole);
};

/// **flag > config > default**, and this is the only place it is written
/// down (#382).
///
/// `value` arrives holding either what the command line said or the
/// default it was born with, and `named_on_the_command_line` is which of
/// those two it is. So: a flag is never overruled; a config replaces a
/// default; and a setting nobody mentioned anywhere keeps the default it
/// already had.
///
/// A template rather than nine copies of an `if`, because nine copies is
/// nine chances for one of them to have the test the other way round —
/// and because a rule stated once is a rule that can be tested once
/// (`desktop_config_test.cpp`).
template <class T>
constexpr void prefer(bool named_on_the_command_line,
                      const std::optional<T>& from_the_config, T& value) {
  if (!named_on_the_command_line && from_the_config.has_value()) {
    value = *from_the_config;
  }
}

}  // namespace amberfolio::sdl
