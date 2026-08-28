// SPDX-License-Identifier: AGPL-3.0-only
//
// The build's seam table: `all_seams()`, assembled from the one accessor
// each built-in seam provides (seam_builtin.h). Every engine registers
// this table at construction (seam.h); a host or a test adds its own on
// top.

#include <array>
#include <span>

#include "amberfolio/machine/seam.h"
#include "seam_builtin.h"

namespace amberfolio::machine {

std::span<const seam_definition> all_seams() {
  // Copies of the definitions, which are spans and views over each file's
  // own static arrays — there is nothing in a `seam_definition` that is
  // not a reference to storage with the program's whole life, so a copy
  // dangles nothing. Built on first use rather than at static-init time,
  // so the order the accessors' own statics are constructed in is decided
  // here and not by link order.
  static const std::array<seam_definition, 6> table{
      code_wheel_seam(),     encamp_fix_seam(),
      automap_seam(),        cheat_invulnerable_seam(),
      cheat_kill_all_seam(), cheat_wound_party_seam()};
  return table;
}

}  // namespace amberfolio::machine
