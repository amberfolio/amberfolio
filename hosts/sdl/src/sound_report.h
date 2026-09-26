// SPDX-License-Identifier: AGPL-3.0-only
//
// The two lines this host says about the Tandy chip (#404), as text a
// test can read rather than as `fprintf`s inside a main().
//
//   amberfolio: POOL.CFG sound P, started as T (Tandy sound)
//   amberfolio: tandy writes=1907 dropped=0
//
// The first before the load, when the copy's own file names a sound
// device other than the one every copy is started with
// (machine/launcher_view.h): the player's file says one thing and the
// program will read another, and a log that did not say so would describe
// the wrong run. The second at the end of any run that wrote the chip:
// how many writes the program made, and how many the ring had no room for
// (`docs/hosts.md` §4a).

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace amberfolio::sdl {

/// The first line, newline included, or empty when there is nothing to
/// say: no line 2, or one that already names the Tandy chip.
[[nodiscard]] std::string started_sound_line(
    std::optional<std::uint8_t> configured);

/// The second line, newline included, or empty for a run that never
/// wrote the chip and dropped nothing.
[[nodiscard]] std::string chip_traffic_line(std::uint64_t writes,
                                            std::uint64_t dropped);

}  // namespace amberfolio::sdl
