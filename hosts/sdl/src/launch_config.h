// SPDX-License-Identifier: AGPL-3.0-only
//
// The config file, and the one place **flag > config > default** happens
// (#382, #385, #472): what a launch settled on once the command line and the
// settings file had both been read, and the writes back into that file.
// `desktop_config.h` has the format and the argument for every part of it;
// `options.h` has what each flag is for.

#pragma once

#include <string>

#include "amberfolio/machine/seam.h"
#include "desktop_config.h"
#include "options.h"

namespace amberfolio::sdl {

/// A gain as the percentage `--volume` and the config file both speak
/// in. One helper, so the rounding is the same on the way in and on the
/// way out and a remembered 75 comes back as 75.
[[nodiscard]] unsigned volume_percent(float gain);

/// What this run settled on, as a config file.
///
/// Written only by `--remember`, and that is the load-bearing part of
/// #382 rather than a convenience. A host that wrote its settings down
/// at the end of every run would make the next run's seams whatever the
/// last run's command line happened to say -- so a driving script's
/// `--seam automap` would leave the seam on for a player who never chose
/// it, and the off side of `scripts/visual-legs.py`'s next pair would
/// quietly be the on side. Every seam is off for somebody who never
/// chose one (CLAUDE.md), and asking is how that stays true.
[[nodiscard]] sdl::desktop_config config_of(const options& opts);

/// The seams that are on right now, written into the config file and
/// **nothing else about this run** (#383).
///
/// This is the one place the desktop host writes a setting without
/// `--remember`, and the argument for it is narrow on purpose. #382's
/// rule is about a *flag*: a driving script's `--seam automap` is not
/// somebody choosing an enhancement, and a host that wrote it down would
/// leave the seam on for the next person who ran the script — which is
/// the fidelity invariant lost to a convenience. A click or a Return
/// **in a panel** is nobody but a player, and it is the only gesture
/// this host has that cannot be anything else. So the panel remembers
/// and the flag still does not.
///
/// Only `.seams`, over whatever the file already holds. `config_of()`
/// would also write down this run's `--speed`, `--scale` and `--volume`,
/// and a driving script's speed becoming a player's setting is the very
/// thing `--remember` exists to prevent.
///
/// **Absence of a stored choice is off**, never "unset means inherit": a
/// player who has turned every seam back off gets no `seam` line at all,
/// which is exactly the file a player who never opened the panel has.
///
/// Kept as one function with one caller and a name that says what it is,
/// so that it can be taken out on its own if this rule is the wrong one.
void remember_panel_seams(const options& opts,
                          const machine::seam_engine& seams);

/// Whether this launch is one nobody is watching (#385).
///
/// The shape of the run rather than `isatty`, for two reasons. A script
/// that inherited a terminal would pass an `isatty` check and is still
/// not a person; and a shape is something a test can state, where a
/// terminal is not. `--replay` is not in the list because `settle_config`
/// returns before the ask for one.
///
/// `sidecar_consent.h` argues each of these; the short version is that a
/// prompt in a run nobody is watching either hangs it or is answered by
/// something that did not know it was answering.
[[nodiscard]] bool nobody_is_at_the_keyboard(const options& opts);

/// The config file, and the one place **flag > config > default**
/// happens (#382, `desktop_config.h`).
///
/// False when this launch has nothing to run: `opts.first_run` then says
/// whether that is a first run, which is not an error, or a command line
/// this host could not make sense of, which is.
[[nodiscard]] bool settle_config(options& opts);

}  // namespace amberfolio::sdl
