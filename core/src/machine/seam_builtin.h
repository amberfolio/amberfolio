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

#include "amberfolio/machine/seam.h"

namespace amberfolio::machine {

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

/// The up and down arrows in the pick-lists (seam_list_arrows.cpp, #423).
[[nodiscard]] const seam_definition& list_arrows_seam() noexcept;
/// Left and Right step a command bar's highlight, Enter takes it, and Esc
/// answers No at a Yes/No question (seam_bar_keys.cpp, #425, #438).
[[nodiscard]] const seam_definition& bar_keys_seam() noexcept;
/// Up and Down move a cursor over the main menu, and Return takes it
/// (seam_menu_cursor.cpp, #434).
[[nodiscard]] const seam_definition& menu_cursor_seam() noexcept;

/// The number row's 1 to 8 select a party member, and the party list shows
/// each member's number (seam_hero_keys.cpp, #439).
[[nodiscard]] const seam_definition& hero_keys_seam() noexcept;

/// PLAN.md §5 item 6, the debug cheats (seam_cheats.cpp).
[[nodiscard]] const seam_definition& cheat_invulnerable_seam() noexcept;
[[nodiscard]] const seam_definition& cheat_kill_all_seam() noexcept;
[[nodiscard]] const seam_definition& cheat_wound_party_seam() noexcept;

/// The text faces, alternatives in one group (seam_font.cpp, text_face.h).
[[nodiscard]] const seam_definition& font_sans_seam() noexcept;
[[nodiscard]] const seam_definition& font_chisel_seam() noexcept;

}  // namespace amberfolio::machine
