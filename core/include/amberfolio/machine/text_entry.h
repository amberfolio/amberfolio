// SPDX-License-Identifier: AGPL-3.0-only
//
// Whether the program is reading a line of text right now (#504).
//
// The program reads a line of text through one resident routine, its line
// editor: a new character's name, a script's free-text answer, a script's
// number prompt and the code word at the copy-protection challenge
// (docs/seams.md, "The edit keys"). A host that paints a keyboard of its own
// wants to put it up the moment the program asks for a line and take it down
// when the line is accepted, and must not guess either from what is on the
// screen: that would be a second opinion on what the program is doing.
//
// The answer comes from the `text-entry` piece of `modern-controls`
// (seam_text_entry.cpp): two points, at the editor's entry and at its one
// return, that note the crossing in one of the seam's own words and write
// nothing of the machine's. So it is **observation**: not machine state, not
// serialized, in no hash, dropped with the seam's enable and rebuilt by a
// replay, which reaches the same two instructions.
//
// **With `modern-controls` off nothing watches the editor**, and the answer
// is `unknown` rather than `not_reading`: a host told "no" would be told a
// guess. Every site build turns the seam on, so for it the answer is always
// one of the other two.

#pragma once

#include <cstdint>

namespace amberfolio::machine {

class machine;

enum class text_entry : std::uint8_t {
  /// The program is not inside its line editor.
  not_reading,
  /// The program is inside its line editor: from the editor's first
  /// instruction until its return, the line accepted or abandoned with Esc.
  reading,
  /// Nothing is watching: `modern-controls` is off, or not available for
  /// this program, or no program is loaded.
  unknown,
};

/// Where the program is, as of the last step boundary.
[[nodiscard]] text_entry text_entry_now(const machine& box) noexcept;

}  // namespace amberfolio::machine
