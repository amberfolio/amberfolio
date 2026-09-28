// SPDX-License-Identifier: AGPL-3.0-only
//
// `--dump`'s text beside its pictures: what `machine/screen_text.h` reads
// off the screen, as a file a person or a script can read.
//
//   screen-text none
//   text  |  MAIN MENU                             |
//   ink   |  FFFFFFFFF                             |
//   paper |  000000000                             |
//
// One block of three lines per row of the grid. `text` is the character,
// `#` for a cell that is not text and `%` for one whose bitmap is more
// than one character's; `ink` and `paper` are the palette index in hex,
// and `.` where there is none. A run with no text says why on the first
// line and has no rows.

#pragma once

#include <filesystem>
#include <string>

namespace amberfolio::machine {
class machine;
}

namespace amberfolio::sdl {

[[nodiscard]] std::string screen_text_dump(const machine::machine& box);

/// `screen_text_dump()` into `where`. Silent on failure, like a still.
void write_screen_text(const std::filesystem::path& where,
                       const machine::machine& box);

}  // namespace amberfolio::sdl
