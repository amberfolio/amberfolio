// SPDX-License-Identifier: AGPL-3.0-only
//
// SDL's key events, in the vocabulary the machine has: XT scan code set 1.
//
// Its own translation unit rather than a lump inside main.cpp, and for
// one reason: it is a table of eighty-odd literals that nothing had ever
// checked (#80). A table like that is exactly the kind of thing that is
// wrong in one row and right in every other, and the way to find out is
// to derive it against something written down independently -- which
// core's own `xt_keyboard::xt_table` is. tests/keymap_test.cpp does that
// derivation; it cannot, so long as the table is private to a main().
//
// The direction is one-way on purpose. This maps the host's event
// vocabulary onto the wire the machine has; what a scan code *means* --
// which character, which shift rule, which BDA bit -- is core's business
// and lives in machine/keyboard.h, because programs read the BDA
// directly and the answer has to be the same for every host.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

namespace amberfolio::sdl {

/// SDL scancode → the raw XT make/break code the core's translation table
/// expects. The table itself lives in core (machine/keyboard.h) because
/// programs read the BDA shift flags directly; this is only the mapping
/// from the host's own event vocabulary onto the wire the machine has.
[[nodiscard]] std::uint8_t xt_scancode(SDL_Scancode code) noexcept;

}  // namespace amberfolio::sdl
