// SPDX-License-Identifier: AGPL-3.0-only
//
// What a key claimed at the program's **blocking read** is answered with.
// Shared by the two seams that claim there — `seam_automap.cpp` and
// `seam_journal.cpp` — because they claim at the same address, in the
// same routine, and what one of them hands that routine back is not a
// thing the other may decide differently.
//
// **Why a read has to be answered and a poll does not.** The poll is
// where a key is meant to be taken: the program asks whether one is
// waiting, a seam takes it off the BIOS ring at 40:1Eh, and the program
// is told no — which is exactly what it would have been told had the key
// never been typed. But a key landing in the step between that question
// and its answer arrives at the *read* instead, and a read is the program
// already committed to being handed one. Take it there and put nothing
// back and the program goes to sleep inside the BIOS, where **no point of
// this engine is reached at all**, so the next key the player types is
// handed straight to it, unseen. That is the whole of "a seam-claimed key
// sometimes needs a second press": the journal reader answered it when it
// was built, and the automap did not until #266.
//
// **Why this character.** `-` is on none of the program's bars — their
// command letters are `0-9A-Z` — it is neither of the two keys that step
// a bar's highlight, and it is not one of the keypad characters the input
// routine remaps. So the menu-bar routine does not even return: it goes
// back to waiting, which is exactly where it was.
//
// It goes back through `seam_context::inject_keystroke()`, which is §3's
// synthetic input and puts it at the tail of the same ring the claim took
// it from. One keystroke, because the program drains its keyboard after
// every key it reads (docs/seams.md §8.4).
//
// **Why this is shared and the addresses are not.** `automap_overland.h`
// says a fact table belongs to the file that acts on it, and that stands:
// `0x49F3` is spelled in three seams. This is not a table. It is one
// answer to one question about one routine, and the argument above is
// longer than the constants — so it is kept once and referred to, and a
// seam that starts claiming at the read gets both by including this.

#pragma once

#include <cstdint>

namespace amberfolio::machine {

/// The keystroke a claim at the blocking read puts back: the scan code
/// and the character of `-`, which the program throws away.
constexpr std::uint8_t key_ignored_scan = 0x0C;
constexpr std::uint8_t key_ignored_ascii = '-';

}  // namespace amberfolio::machine
