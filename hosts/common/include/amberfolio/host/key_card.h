// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card (#427): the game's keys, by context, as data.
//
// The barrier to entry for the controls is that they are undiscoverable,
// not that they are missing. A list steps on Home and End, a bar's
// highlight on `,` and `.`, and the 3D view walks on the arrows; nothing
// on the screen says so, and none of it is what a player tries first.
//
// So this is the card both shells show without leaving the game, and it
// is **one table**: the desktop host paints it (`hosts/sdl/src/
// key_card_view.*`), the page renders it from the same words through
// `af_web_key_card_json` (`hosts/web/page/key-card.mjs`). A change to
// the card is a change to `key_card.cpp` and to no shell.
//
//
// What is on it, and what is not
// ------------------------------
//
// Key names and what a key does where it is pressed: facts about the
// program's input routines, each one checked against the running program
// and against the routine that reads it (docs/hosts.md has the list of
// which were driven). Nothing is copied from the game's manual or its
// reference card, and nothing here is the program's own text beyond the
// names of commands its bars already show a player.
//
// Not on it: anything a shell draws *into* the game's screen. The card is
// the shell's furniture, painted over the window or in the page and never
// into the machine's frame (PLAN.md §5).
//
//
// Seam rows follow the toggles
// ----------------------------
//
// A row may name a seam id. It is shown only while that seam is on, so a
// player who has turned nothing on is told only what the unmodified game
// does. **Adding a seam's keys is one line in `key_card.cpp`**, in the
// context where the key is pressed; a context left with no row to show is
// not shown at all. `tests/key_card_test.cpp` holds the table to the seam
// table (`all_seams()`), so a row that names a seam which does not exist
// is a red test and not a card that never shows it.
//
//
// Both shells filter the same way
// -------------------------------
//
// `key_card_for()` is the filter for a host that has a C++ seam engine.
// A page has the same data as JSON and the same rule in JavaScript
// (`visibleKeyCard()` in `hosts/web/page/host.mjs`); each is checked on
// its own side, this one by `key_card_test.cpp` and the page's by
// `tests/smoke.mjs` over the card the module hands out.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace amberfolio::host {

/// How wide a row's two words may be, so that a shell with a fixed grid
/// of characters (the desktop host's is 78 across) can set them in two
/// columns without cutting either. The unit suite holds every row to it.
inline constexpr std::size_t key_card_keys_width = 21;
inline constexpr std::size_t key_card_does_width = 56;

/// Which shell a context is for.
///
/// `both` is the game's own keys, which are the machine's and the same
/// everywhere. `desktop` and `page` are a shell's own controls: the keys
/// and buttons that shell takes for itself, which the other does not
/// have.
enum class card_shell : std::uint8_t {
  both,
  desktop,
  page,
};

/// One key and what it does there.
struct key_row {
  /// The key or keys, in the names a keyboard has for them. Alternatives
  /// are separated by a comma and a space.
  std::string_view keys;
  /// What it does, in a short clause.
  std::string_view does;
  /// The seam this row belongs to, or empty for a row about the program
  /// as it is. Shown only while that seam is on.
  std::string_view seam;

  constexpr key_row(std::string_view keys_in, std::string_view does_in,
                    std::string_view seam_in = {}) noexcept
      : keys(keys_in), does(does_in), seam(seam_in) {}
};

/// A screen or a situation, and the keys that mean something there.
struct key_context {
  /// A stable word for the context: a page keys its DOM on it and a test
  /// names it. Lower-case, no spaces.
  std::string_view id;
  /// What a player calls the place, for a heading.
  std::string_view title;
  std::span<const key_row> rows;
  card_shell shell;

  constexpr key_context(std::string_view id_in, std::string_view title_in,
                        std::span<const key_row> rows_in,
                        card_shell shell_in = card_shell::both) noexcept
      : id(id_in), title(title_in), rows(rows_in), shell(shell_in) {}
};

/// The whole card, in the order a shell shows it.
[[nodiscard]] std::span<const key_context> key_card();

/// The note under the card: what holds for every context at once.
[[nodiscard]] std::string_view key_card_legend();

/// One context as a shell shows it: only the rows that are on, in order.
struct key_page {
  const key_context* context{nullptr};
  std::vector<const key_row*> rows;
};

/// The card as one shell shows it. The contexts for `shell` (and the ones
/// for `both`), each with only the rows whose seam `seam_on` says is on
/// or that name none; a context with no row left is dropped.
[[nodiscard]] std::vector<key_page> key_card_for(
    card_shell shell, const std::function<bool(std::string_view)>& seam_on);

/// The card as JSON for a page, which has no way to call C++:
///
///   {"legend": "...", "contexts": [{"id": "...", "title": "...",
///    "shell": "both|desktop|page", "rows": [{"keys": "...",
///    "does": "...", "seam": "..."}]}]}
///
/// Every context and every row, unfiltered: the page filters, because it
/// is the page that learns a seam has been switched. Plain printable
/// ASCII with nothing in it that needs escaping, which the unit suite
/// checks, so a reader needs no JSON library to trust it. Built once and
/// valid for the life of the program.
[[nodiscard]] std::string_view key_card_json();

}  // namespace amberfolio::host
