// SPDX-License-Identifier: AGPL-3.0-only
//
// `--press KEY@FRAME[:down|:up]`: a keystroke the host gives itself.
//
// Its own translation unit for the reason keymap.h gives for the scan
// code table: a grammar nobody can test is a grammar that is wrong in one
// corner. It was a private parser in main.cpp until the suffix arrived
// (#313), and a suffix is exactly the corner -- a key name may itself end
// in punctuation (`Keypad :`), so where the split happens matters, and
// tests/press_spec_test.cpp pins it.
//
// The default is a tap: the make and the break, on the same frame, which
// is every driven leg in docs/playable.md. `:down` posts only the make
// and `:up` only the break, and they exist for one kind of question --
// what a program does while a modifier is *held*, which is the question
// a latched Alt asked at the code wheel. A held key has no other
// spelling on this command line, and the page's driver has none at all.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace amberfolio::sdl {

/// Which edges of the key a press posts.
enum class press_edges : std::uint8_t {
  /// The make and then the break, on the same frame: a tap.
  both,
  /// The make only; the key stays held until a `:up` or a focus loss.
  down,
  /// The break only.
  up,
};

/// A keystroke the host gives itself: which key, which frame of the loop
/// to push it on, and which edges.
///
/// The key is kept as SDL's own name until SDL is up, because
/// `SDL_GetScancodeFromName` is a question about SDL's tables and asking
/// it before SDL_Init is asking it early. Frame numbers count iterations
/// of the host loop, which are virtual frame periods — the same unit
/// machine_harness.h's `scripted_key` counts in, one layer further out.
struct scripted_press {
  std::string key;
  std::uint64_t frame{};
  press_edges edges{press_edges::both};
  SDL_Scancode code{SDL_SCANCODE_UNKNOWN};
  bool done{false};
};

/// `KEY@FRAME`, `KEY@FRAME:down` or `KEY@FRAME:up`, into a press. False
/// on anything that is not that, and `out` untouched.
///
/// Split on the *last* `@`, because SDL names a key by the legend printed
/// on it and some legends are punctuation. There is no `@` key on a US
/// board, but splitting on the last one costs nothing and stops that from
/// being a fact this parser quietly depends on. The suffix is looked for
/// after the `@` only, for the same reason: `Keypad :` is a key.
[[nodiscard]] bool parse_press(std::string_view spec, scripted_press& out);

}  // namespace amberfolio::sdl
