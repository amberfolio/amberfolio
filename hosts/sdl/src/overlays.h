// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's furniture: the on-screen keyboard (#377) and the toggle
// panel with its key card (#383, #427), painted over the window and driven by
// the pointer at it (#472, split from main.cpp).
//
//
// The on-screen keyboard
// ----------------------
//
// What it is — the keys, the legends, the make codes, the geometry, what
// moves the focus and what a commit produces — is core's
// (`machine/screen_keyboard.h`), read here rather than restated; the
// page draws the same three layouts from the same numbers.
//
// A committed key goes out through the host's `post_key`, so it is counted,
// recorded and let go of at a focus loss exactly like a key struck at
// the window. Nothing else about it reaches the machine, and nothing at
// all reaches it while a replay is running: a keyboard the recording
// never had is the same input a window keystroke would be.
//
// **The middle mouse button steps it on**: hidden, then each layout in
// turn, then hidden again. One button, because this machine has no
// mouse at all and a mouse button is a control the game can never want
// back — unlike a key, which is the argument the F11/F12/Pause keys in
// main.cpp had to make three times.
//
//
// The toggle panel
// ----------------
//
// The five facts about every seam, painted over the window, and the
// place a player turns one on. `seam_panel.h` argues the columns and
// says why `fired` is a number; what is here is the run loop's half —
// which button opens it, which keys drive it, and where a choice goes.
//
// **The right mouse button opens and closes it**, on the middle
// button's own recorded argument (#377): this machine has no mouse, so
// no mouse button is a control the game can ever want back. A second
// press turns it into the key card (#427), a third closes it.
//
// **The on-screen keyboard has priority for the keys.** It already
// claims all four arrows and Return, and two overlays fighting over
// one key is worse for a player than one of them being unreachable for
// as long as the other is up. So the panel takes up, down and Return
// only while the keyboard is *not* shown; a player with both up steps
// the keyboard off and has them back.
//
// The rows are rebuilt from the engine at every paint and at every
// click rather than kept, which is what makes `fired` a live number
// instead of one taken once before the machine had run a step.

#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/screen_keyboard.h"
#include "options.h"
#include "screen_keyboard_view.h"
#include "seam_panel.h"

namespace amberfolio::sdl {

/// How many keys each overlay takes for itself while it is up.
inline constexpr std::size_t keyboard_control_count = 6;
inline constexpr std::size_t panel_control_count = 4;

class overlays {
 public:
  /// Where a key the overlays commit goes: the host's `post_key`, so that it
  /// is counted, recorded and let go of at a focus loss exactly like a key
  /// struck at the window.
  using key_sink = std::function<void(std::uint8_t, machine::key_action)>;

  overlays(machine::machine& box, const options& opts, bool replaying,
           key_sink post_key, std::vector<std::string> document_notice);

  /// The window lost the keyboard. The breaks for whatever the on-screen
  /// keyboard had latched have just gone out with the rest of the held keys,
  /// so the mask has to stop claiming they are down (#377).
  void focus_lost();

  /// A document was presented, and these are the lines the player is told
  /// (#384). The panel is opened on the answer, because a drop that printed
  /// to a terminal nobody is looking at is a control that did nothing.
  void document_presented(std::vector<std::string> notice);

  /// A mouse button went down. The middle one steps the on-screen keyboard
  /// on (#377), the right one opens and closes the toggle panel (#383), and
  /// the left one presses whatever is under the pointer.
  void mouse_button_down(const SDL_Event& event, SDL_Renderer* renderer);

  /// A key went down or up. True when an overlay took it, in which case the
  /// machine never sees it.
  [[nodiscard]] bool take_key(const SDL_Event& event);

  /// Whether something changed that the game did not draw. The present is
  /// gated on the display's generation, so a focus that moved over a picture
  /// the game is not redrawing would otherwise not appear until the game
  /// moved (#377); and the panel is a live readout while it is up (#383).
  [[nodiscard]] bool wants_present() const noexcept {
    return keyboard_repaint_ || panel_shown_ || panel_repaint_;
  }

  /// Paint both over whatever is already in the render target. Called after
  /// `--verify`'s readback, deliberately: that is a claim about the machine's
  /// own pixels, and an overlay in the target would make it a claim about
  /// this host's furniture instead.
  void draw(SDL_Renderer* renderer);

 private:
  /// Let go of every latched modifier, and post the breaks for it: a
  /// shift latched on one layout has no business surviving into the next,
  /// or outliving the keyboard that latched it.
  void keyboard_unlatch();

  /// Hidden, then each layout in turn, then hidden again.
  void keyboard_step();

  void keyboard_commit(std::size_t at);

  /// Turn the seam on row `at` on or off, through the same
  /// `enable()`/`disable()` a `--seam` flag takes and with the refusal
  /// reported.
  ///
  /// **A choice that did not take is never written down.** A panel that
  /// recorded a toggle core had refused would be the one failure a
  /// fail-closed toggle surface cannot have: the next launch would turn
  /// on a seam this one could not.
  void panel_toggle(std::size_t at);

  /// The focus one row up or down, wrapping. A build with no seams has
  /// no focus at all rather than a focus on row zero of nothing.
  void panel_move(int step);

  machine::machine& box_;
  const options& opts_;
  bool replaying_;
  key_sink post_key_;

  sdl::keyboard_painter keyboard_paint_;
  std::span<const machine::screen_keyboard::layout> keyboard_layouts_;
  std::size_t keyboard_index_{0};
  bool keyboard_shown_{false};
  std::size_t keyboard_focus_{machine::screen_keyboard::no_key};
  std::uint8_t keyboard_latched_{0};
  /// The overlay changed without the machine drawing anything: the
  /// present below is gated on the display's generation, and a focus that
  /// moved over a still picture would otherwise not be drawn until the
  /// game moved.
  bool keyboard_repaint_ = false;
  std::array<bool, keyboard_control_count> keyboard_taken_{};

  sdl::panel_painter panel_paint_;
  bool panel_shown_;
  // The panel's second view, the key card (#427): `panel_shown` with this
  // set is the card, a page of it at `card_page`.
  bool card_shown_{false};
  std::size_t card_page_{0};
  std::size_t panel_focus_;
  /// The overlay changed without the machine drawing anything —
  /// `keyboard_repaint`'s reason, and the same fix.
  bool panel_repaint_{false};
  std::array<bool, panel_control_count> panel_taken_{};
  /// What each document turned out to be, for the panel to say (#384).
  std::vector<std::string> document_notice_;
};

}  // namespace amberfolio::sdl
