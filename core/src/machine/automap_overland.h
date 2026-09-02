// SPDX-License-Identifier: AGPL-3.0-only
//
// One recorder for the overland, and two seams that call it. M5-E5b
// (#254) of the explored overlay (#179).
//
// **Why it is here and not in `automap.cpp`.** That file reads nothing
// from the machine on purpose — the seam does the reading and hands the
// answers over, which is what lets the store be tested against nothing at
// all and what makes "the store is wrong" and "the facts are wrong"
// different failures. This does read the machine, so it sits beside the
// seams that do, in `src/` where `seam_builtin.h` is: a translation unit
// of its own, included by both of them.
//
// **Why one function and not two.** A player with only the automap on
// keeps their overland trail; a player with only the explored overlay on
// keeps it too; with both on the trail is recorded once, because setting
// a bit twice is idempotent and the second call finds nothing to change
// and fires no callout. The automap seam **draws** nothing here — its
// panel stays gated on the interior screen — and only its recording
// widens.
//
// The facts this reads are `docs/explored-overlay.md` §2, each with the
// second route that agreed. They are restated here rather than shared
// with `seam_automap.cpp`: a fact table belongs to the file that acts on
// it, which is the house pattern (`0x49F3` is spelled in three seams
// already).

#pragma once

#include <cstdint>

namespace amberfolio::machine {

class machine;
class seam_context;

/// What one look at the overland found. `on_screen` false means the
/// program is not showing the travel view and nothing was touched;
/// `settled` false means it is, but the party's position is not to be
/// believed yet (`automap_state::observe`).
struct overland_look {
  bool on_screen{false};
  bool settled{false};
  /// The view kind, 2..4, one per wilderness area. Only meaningful when
  /// `on_screen`.
  std::uint8_t view_kind{};
  std::uint8_t disk{};
  std::uint8_t area{};
  /// The party's cell: column 0..15, row 0..35.
  std::uint8_t x{};
  std::uint8_t y{};
};

/// Look at the overland, record where the party is standing, and tell a
/// host when that changed something.
///
/// `ds` is the program's data segment, already checked by the caller
/// against the one derived from `image_base()` — this does not re-derive
/// it, because the seam that called it has already refused if DS is not
/// where the facts say.
///
/// It reveals the party's own cell and no other. The overland view has no
/// fog: every one of the twenty-five cells in the window is drawn
/// whatever the party has done, so "explored" here can only mean *walked*
/// — which is what makes the record a trail and what lets the overlay
/// leave the cell under the party unmarked (`docs/explored-overlay.md`
/// §5).
overland_look observe_overland(machine& box, seam_context& ctx,
                               std::uint16_t ds);

}  // namespace amberfolio::machine
