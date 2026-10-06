// SPDX-License-Identifier: AGPL-3.0-only
//
// The seams this build carries, one accessor each, for seam_table.cpp to
// assemble into `all_seams()`.
//
// A source-local header rather than a public one: nothing outside
// core/src/machine has any business naming a built-in seam individually.
// A host sees the table through `all_seams()` and the registry through
// `seam_engine`, and a seam is reached by its id — which is the whole of
// what PLAN.md §5's "individually toggleable" means. Each accessor
// answers a reference to a static with the definition's whole life, so
// the table may hold copies of the definitions (they are spans and views
// over static arrays) without anything dangling.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {

class machine;

/// PLAN.md §5 item 1, in the form M3 needed (seam_code_wheel.cpp).
[[nodiscard]] const seam_definition& code_wheel_seam() noexcept;

/// PLAN.md §5 item 4, the Encamp (F)ix (seam_encamp_fix.cpp).
[[nodiscard]] const seam_definition& encamp_fix_seam() noexcept;

/// PLAN.md §5 item 3, the automap panel (seam_automap.cpp).
[[nodiscard]] const seam_definition& automap_seam() noexcept;

/// PLAN.md §5 item 2's in-game half, the journal reader (seam_journal.cpp).
[[nodiscard]] const seam_definition& journal_seam() noexcept;

/// PLAN.md §5 item 5, the explored overlay (seam_explored.cpp).
[[nodiscard]] const seam_definition& explored_seam() noexcept;

/// The controls, six pieces in one seam (seam_modern_controls.cpp, #473):
/// the up and down arrows in the pick-lists, Left, Right, Return and Esc at
/// a command bar, a cursor on the main menu, the number row's heroes, a
/// selection as a yellow block, and the line editor's extended keys. Each piece
/// keeps its logic and its facts in a source of its own and hands the
/// registration the points it owns.
[[nodiscard]] const seam_definition& modern_controls_seam() noexcept;

/// `modern-controls`' id, and the one question another seam asks about it:
/// is it on. The journal's reader draws and answers bars of its own, which
/// the program's menu-bar routine never sees, and takes the same keys and
/// colours when this is on (seam_journal.cpp, #471).
inline constexpr std::string_view modern_controls_id = "modern-controls";
[[nodiscard]] bool modern_controls_on(const machine& box) noexcept;

/// The pieces' points, in the order the registration offers them at an
/// address two of them share: the pick-lists' arrows, the bars' keys, the
/// main menu's cursor, the heroes' numbers, the line editor, the
/// selection's colours (the order the six seams were registered in), and
/// the party bar's move mode last.
/// The counts are what the registration sizes its table from; each source
/// holds a `static_assert` that its own table is that long.
inline constexpr std::size_t list_arrows_point_count = 3;
inline constexpr std::size_t bar_keys_point_count = 1;
inline constexpr std::size_t menu_cursor_point_count = 2;
inline constexpr std::size_t hero_keys_point_count = 5;
inline constexpr std::size_t edit_keys_point_count = 1;
inline constexpr std::size_t select_yellow_point_count = 9;
inline constexpr std::size_t move_mode_point_count = 5;

/// The up and down arrows in the pick-lists, and the selected party member
/// at the bars where Home and End step it (seam_list_arrows.cpp, #423).
[[nodiscard]] std::span<const seam_point> list_arrows_points() noexcept;

/// Left and Right step a command bar's highlight, Enter takes it, and Esc
/// answers No at a Yes/No question (seam_bar_keys.cpp, #425, #438).
[[nodiscard]] std::span<const seam_point> bar_keys_points() noexcept;

/// Up and Down move a cursor over the main menu, and Return takes it
/// (seam_menu_cursor.cpp, #434).
[[nodiscard]] std::span<const seam_point> menu_cursor_points() noexcept;

/// The number row's 1 to 8 select a party member, and the party list shows
/// each member's number (seam_hero_keys.cpp, #439).
[[nodiscard]] std::span<const seam_point> hero_keys_points() noexcept;

/// An extended key never types a letter in the line editor
/// (seam_edit_keys.cpp, #455).
[[nodiscard]] std::span<const seam_point> edit_keys_points() noexcept;

/// Every selection the player can move as a yellow block, a command's key
/// letter as a white one (seam_select_yellow.cpp, #453, #483).
[[nodiscard]] std::span<const seam_point> select_yellow_points() noexcept;

/// The mark a seam sets in the high half of the colour word it hands the
/// program's string routine when what it draws is a selection: the main
/// menu's cursor row, the journal's listing row and its bar's lit word. The
/// routine reads the low byte, and `select-yellow` draws a marked string
/// as a block (#483).
inline constexpr std::uint16_t selection_mark = 0x8000;

/// On the party's own bar, walking is a mode: `Move` starts it, `Exit` ends
/// it, and between the two Left and Right step the bar (seam_move_mode.cpp,
/// #479). Last at the key read, so every other piece has passed first.
[[nodiscard]] std::span<const seam_point> move_mode_points() noexcept;

/// PLAN.md §5 item 6, the debug cheats (seam_cheats.cpp).
[[nodiscard]] const seam_definition& cheat_invulnerable_seam() noexcept;
[[nodiscard]] const seam_definition& cheat_kill_all_seam() noexcept;
[[nodiscard]] const seam_definition& cheat_wound_party_seam() noexcept;

/// The text faces, alternatives in one group (seam_font.cpp, text_face.h).
[[nodiscard]] const seam_definition& font_sans_seam() noexcept;
[[nodiscard]] const seam_definition& font_chisel_seam() noexcept;

}  // namespace amberfolio::machine
