// SPDX-License-Identifier: AGPL-3.0-only
//
// The key card, as lines of text for the panel's painter (#427).
//
// The card is `hosts/common`'s table (`amberfolio/host/key_card.h`); this
// is only this host's way of setting it. One context to a page, because a
// card of every context at once is sixty lines and the window is not
// tall: the same words at a size a person can read, a page at a time.
//
// It is the toggle panel's second view, not a third overlay, and the
// right mouse button steps from one to the next (`docs/hosts.md` §8): the
// panel is the one surface this host already has for things a player
// reads about the enhancements, and a card with its own button would be a
// fourth mouse button's worth of a thing to find. The painter is
// `seam_panel.h`'s: the first two lines are its heading and are drawn in
// amber, the rest in the machine's paper white.
//
// Split from main.cpp for `seam_panel.h`'s reason: the column arithmetic
// is not checkable by eye, and a lambda inside a `main()` is arithmetic no
// test can reach.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "amberfolio/host/key_card.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::sdl {

/// How wide a card line is, which is the toggle panel's width: the two
/// views are one rectangle and do not jump about when the button steps
/// from one to the other.
inline constexpr std::size_t card_columns = 78;

/// Where the second word of a row begins. The first is at most
/// `host::key_card_keys_width` wide, so a space always separates them.
inline constexpr std::size_t card_does_column = 22;

/// The card for this window: the desktop's contexts, with a seam's rows in
/// them only while that seam is on in `seams`.
[[nodiscard]] std::vector<host::key_page> card_pages(
    const machine::seam_engine& seams);

/// One page of the card as lines, `card_columns` wide each, with the same
/// number of lines on every page so that turning one does not resize the
/// panel. `page` is clamped into range, so a card that lost a page (a seam
/// turned off under it) never points past its end.
[[nodiscard]] std::vector<std::string> card_lines(
    const std::vector<host::key_page>& pages, std::size_t& page);

}  // namespace amberfolio::sdl
