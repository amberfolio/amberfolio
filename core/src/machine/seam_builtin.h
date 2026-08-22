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

/// PLAN.md §5 item 6, the two debug cheats (seam_cheats.cpp).
[[nodiscard]] const seam_definition& cheat_invulnerable_seam() noexcept;
[[nodiscard]] const seam_definition& cheat_kill_all_seam() noexcept;

}  // namespace amberfolio::machine
