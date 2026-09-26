// SPDX-License-Identifier: AGPL-3.0-only
//
// `--wall`'s date (#320) -- its own unit so a test can read the grammar,
// on press_spec.h's argument.

#pragma once

#include <string_view>

#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {

/// `YYYY-MM-DD`, and optionally `THH:MM`, `:SS` and `.CC` after it, into
/// an instant (`--wall`, #320). False for anything that is not that.
///
/// One `T` and no space, because a date and a time with a space between
/// them is two command-line arguments on most shells and one on a shell
/// somebody remembered to quote it for. The separator DOS itself never
/// had is ISO 8601's, which is the one everybody already types.
///
/// The last word on whether the date is *real* is `wall_clock::set()`
/// itself, called here on a throwaway clock: 31 April and 29 February
/// 2100 are refused by the machine's own rule rather than by a second
/// copy of it living in this parser, which could only ever come to a
/// different conclusion than the machine does.
[[nodiscard]] bool parse_wall(std::string_view spec, machine::wall_time& out);

}  // namespace amberfolio::sdl
