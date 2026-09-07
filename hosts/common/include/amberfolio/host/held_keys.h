// SPDX-License-Identifier: AGPL-3.0-only
//
// The keys a host has posted a make code for and no break code yet
// (#313), so that when the window stops hearing the keyboard the host can
// let go of all of them the way a hand would.
//
// A window loses the keyboard without warning -- Alt-Tab, a screenshot
// hotkey, a click on another window, a tab switch -- and the key-up for
// whatever was held at that moment goes to whoever has the keyboard
// next. The machine never sees it. Its BIOS keeps a modifier's state in
// the BDA shift-flag byte, and that byte has no other way of learning
// that a finger came off a key, so the bit stays set: every later letter
// arrives as a control code (Ctrl), or is refused by the program (Alt),
// until the person happens to tap the same key again. On a Hungarian
// layout AltGr is Ctrl and Alt together, so the person who found this
// found it often.
//
// The fix is a host's, and it is exactly what the hardware would do had
// the person released the keys: post a break code for each one. Never
// the BDA written behind the machine's back -- that is machine state,
// and the fidelity invariant (CLAUDE.md) says a host's input reaches the
// machine as scan codes and nothing else. A break code posted here goes
// through `machine::post_key()` like any other, is stamped with the
// machine's clock and recorded into a session the same way, so a replay
// of a run that lost focus is faithful without anyone having to say so.
//
// Both hosts keep this bookkeeping; the SDL host keeps it here, and the
// web page keeps the same object in JS (`HeldKeys` in host.mjs), because
// a page's keys never pass through C++ on the way to the ABI. The two are
// written to say the same thing, and the release order is the same on
// both: ascending scan code, which is a rule and not a history, so the
// releases a focus loss posts are the same on every run that held the
// same keys.

#pragma once

#include <bitset>
#include <cstdint>
#include <vector>

#include "amberfolio/machine/platform.h"

namespace amberfolio::host {

/// The set of scan codes with a make posted and no break yet.
class held_keys {
 public:
  /// Note a key event the host is posting: a `down` marks the code held,
  /// an `up` clears it. Codes outside the XT wire (0 and the release bit)
  /// are ignored, because the host never posts them.
  void note(std::uint8_t scancode, machine::key_action action) noexcept;

  /// True while `scancode` has a make posted and no break.
  [[nodiscard]] bool held(std::uint8_t scancode) const noexcept;

  /// True when nothing is held.
  [[nodiscard]] bool empty() const noexcept { return held_.none(); }

  /// Every held code in ascending order, and forget them all: the host
  /// posts a break for each, in this order.
  [[nodiscard]] std::vector<std::uint8_t> release_all();

 private:
  /// One bit per XT make code (keyboard.h: 0x01..0x7F carry codes; 0x80
  /// and above are the release bit and never a key of their own).
  static constexpr std::size_t code_count = 0x80;
  std::bitset<code_count> held_{};
};

}  // namespace amberfolio::host
