// SPDX-License-Identifier: AGPL-3.0-only
//
// What this host does and says about the seams (#472, split from main.cpp):
// turning on the ones the run asked for, why one was refused, the `--seams`
// listing, and the closing account of what each enabled seam did and what it
// asked of the host (#131, #169). The sentences are the sweep's and
// docs/hosts.md's; what a row *means* is core's (`machine::seam_reading_of`).

#pragma once

#include "amberfolio/host/host_services.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "options.h"

namespace amberfolio::sdl {

/// Why a `--seam` was refused, in words. Named here rather than printed
/// as a number because the two that a person actually hits — the wrong
/// binary and a name that is not a seam — are the two a number would be
/// useless for.
[[nodiscard]] const char* seam_refusal(machine::seam_error why) noexcept;

/// Enable every seam the run was asked for, now that there is a program to
/// key them on, and say why any was refused. False when the launch should
/// stop: a `--seam` this host cannot honour is a command line to fix.
[[nodiscard]] bool enable_requested_seams(machine::machine& box,
                                          const options& opts);

/// The `--seams` listing (#98): every seam's id, its state and the reason,
/// the document it waits for, and what has been shown to the engine.
void report_seam_listing(const machine::machine& box);

/// What each enabled seam actually did (#131), once, at the end of the run
/// it belongs to.
void report_seam_outcomes(const machine::machine& box);

/// And what the seams asked of the host (M5-D1, #169).
void report_host_services(const machine::machine& box,
                          const host::host_services& services);

/// And what the sidecars did with them (M5-E2c #173, #351).
void report_sidecars(const host::host_services& services);

}  // namespace amberfolio::sdl
