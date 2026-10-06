// SPDX-License-Identifier: AGPL-3.0-only
//
// The modern-controls seam: the keys and the colours a player of a later
// game expects, in one seam to switch on (#473).
//
// It is seven pieces, each in a source of its own that holds the facts it
// rests on and the handlers that act on them, and each handing this file
// the points it owns:
//
//   * `list-arrows` (seam_list_arrows.cpp): the up and down arrows step the
//     pick-lists and the selected party member, as Home and End do.
//   * `bar-keys` (seam_bar_keys.cpp): Left and Right step a command bar's
//     highlight, Enter takes it, and Esc answers No at a Yes/No question.
//   * `menu-cursor` (seam_menu_cursor.cpp): a cursor on the main menu,
//     moved by Up and Down and taken by Return.
//   * `hero-keys` (seam_hero_keys.cpp): at the main menu, the number row's
//     1 to 8 select a party member, and the party list shows each member's
//     number.
//   * `edit-keys` (seam_edit_keys.cpp): an extended key never types a letter
//     in the line editor.
//   * `select-yellow` (seam_select_yellow.cpp): a selection the player can
//     move is a yellow block, and a command's key letter a white one.
//   * `move-mode` (seam_move_mode.cpp): on the party's own bar, walking is
//     a mode that `Move` starts and `Exit` ends, so Left and Right step the
//     bar; `Area` is gone, the automap being the overhead view (#479).
//
// They were six seams until #473 and are one definition now, because they
// are one thing to a player: a set of controls that is either the game's
// own or this one. What the merge did and did not change:
//
//   * **The points are the union of the six, in the order the six were
//     registered in** (list-arrows, bar-keys, menu-cursor, hero-keys,
//     edit-keys, select-yellow), and `move-mode`'s after them. Five pieces
//     have a point at the same instruction (the menu-bar routine's key
//     read, overlay 25 `0x0572`), and the engine offers an address's points
//     in table order, so the order is behaviour and is kept: `move-mode`
//     rewrites the party bar's keys after the others have passed on them.
//   * **The binary is the one the six already named**, the baseline
//     edition (edition.h), and only it. Every piece's addresses are facts
//     about that program, and a seam is unavailable against any other.
//   * **A piece no longer asks whether another is on.** `menu-cursor` took
//     its colours from `select-yellow` and the journal reader asked about
//     `bar-keys` and `select-yellow`; the first asked a question whose
//     answer is now always yes, and the second asks `modern_controls_on()`.
//   * **Off is the plain machine, and on is all seven.** There is no setting
//     in between, so no piece's handlers are reached with another's off.
//
// The one thing a merged seam cannot be is *idle*: three of the pieces draw
// as soon as what they draw is on the screen (the menu's cursor, the
// roster's numbers, a highlight's block), so a run with the seam on differs
// from the run with it off at the first main menu. The session library's
// pair for it is a `contrast`, as the faces' are (tests/sessions/README.md).

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

/// The baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> modern_controls_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

constexpr std::size_t point_total =
    list_arrows_point_count + bar_keys_point_count + menu_cursor_point_count +
    hero_keys_point_count + edit_keys_point_count + select_yellow_point_count +
    move_mode_point_count;

/// The pieces' points, one after another in the order seam_builtin.h gives.
struct point_table {
  std::array<seam_point, point_total> point{};

  point_table() {
    std::size_t next = 0;
    for (const std::span<const seam_point> piece :
         {list_arrows_points(), bar_keys_points(), menu_cursor_points(),
          hero_keys_points(), edit_keys_points(), select_yellow_points(),
          move_mode_points()}) {
      for (const seam_point& each : piece) {
        point[next++] = each;
      }
    }
  }
};

}  // namespace

const seam_definition& modern_controls_seam() noexcept {
  // Built on first use, as `all_seams()` is: the pieces' arrays are
  // constants of their own files and are not ordered against this one's
  // statics.
  static const point_table points;
  static const seam_definition definition{
      .id = modern_controls_id,
      .about =
          "arrows, Enter and Esc work at the game's lists, bars and menus, "
          "walking is a mode the party's bar starts and ends, the main menu "
          "has a cursor, and a selection is a yellow block",
      .fingerprints = modern_controls_binaries,
      .points = points.point,
      .schema = seam_schema_version};
  return definition;
}

bool modern_controls_on(const machine& box) noexcept {
  return box.seams().status(modern_controls_id).state == seam_state::on;
}

}  // namespace amberfolio::machine
