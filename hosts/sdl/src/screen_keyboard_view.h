// SPDX-License-Identifier: AGPL-3.0-only
//
// The on-screen keyboard, painted over the window (#377).
//
// The keyboard itself — which keys, where they sit, what each produces,
// what moves the focus — is core's
// (`machine/screen_keyboard.h`), and this host reads it rather than
// keeping a second copy: the page draws the same three layouts from the
// same numbers, and a layout change is a change to that one file.
//
// What is here is the two things core has no business knowing. **Pixels**:
// how big a key is in this window, where the keyboard sits in it, and
// which key a pointer landed on. And **the paint**: rectangles and a
// legend, in the machine's own character generator (`machine/font.h`) so
// that the keyboard looks like the machine it is attached to rather than
// like whatever font a desktop happened to have.
//
// Split from main.cpp for the reason keymap.h gives for the scan-code
// table: the geometry is arithmetic nobody can check by eye, and a
// private lambda inside a `main()` is arithmetic no test can reach.
// `tests/screen_keyboard_view_test.cpp` reaches this.
//
//
// Nothing here touches the machine
// ---------------------------------
//
// A commit produces scan codes and `main()` posts them through the same
// `post_key` a window keystroke takes — counted, recorded, and released
// on a focus loss like any other. This file never sees the machine, and
// the overlay is drawn after the frame has been presented and verified,
// so `--verify`'s read-back is still comparing the machine's own pixels
// against the machine's own framebuffer and nothing else.

#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/screen_keyboard.h"

namespace amberfolio::sdl {

/// Where a layout goes in a window, in pixels: what one quarter unit is
/// worth, and the rectangle the whole keyboard fills.
///
/// The keyboard is as wide as it can be without touching the window's
/// edges and no taller than half of it, centred, and sitting on the
/// bottom edge — where a thumb is on a tablet, and out of the way of a
/// game that draws its own text across the top.
struct keyboard_box {
  float quarter{};  ///< Pixels in one quarter unit; a key row is four.
  float left{};
  float top{};
  float width{};
  float height{};
};

/// Fit `which` into a window `window_width` by `window_height` pixels.
/// A window too small for a whole key answers a box with a `quarter` of
/// one pixel rather than a degenerate one: a keyboard nobody can read is
/// still better than a division by zero.
[[nodiscard]] keyboard_box fit_keyboard(
    const machine::screen_keyboard::layout& which, int window_width,
    int window_height) noexcept;

/// The rectangle key `one` fills, inset by a hairline so that two
/// neighbouring keys read as two keys.
[[nodiscard]] SDL_FRect key_rect(
    const keyboard_box& box, const machine::screen_keyboard::key& one) noexcept;

/// Which key a window point is on, or
/// `machine::screen_keyboard::no_key` for a point in a gap, in the
/// margin, or outside the keyboard altogether.
///
/// The pixel-to-cell arithmetic is here; *which key covers that cell* is
/// core's `key_at()`, so a gap between two keys means the same thing on
/// this host as it does on the page.
[[nodiscard]] std::size_t key_under(
    const keyboard_box& box, const machine::screen_keyboard::layout& which,
    float x, float y) noexcept;

/// Paints a layout, and owns the one texture it needs to do it: an atlas
/// of the machine's character generator, built once on the renderer it
/// will be drawn with.
///
/// A texture rather than a rectangle per lit pixel: a legend is eight
/// rows of eight, and a full keyboard's worth of them is thousands of
/// draw calls a frame the moment it is spelled the obvious way.
class keyboard_painter {
 public:
  keyboard_painter() = default;
  keyboard_painter(const keyboard_painter&) = delete;
  keyboard_painter& operator=(const keyboard_painter&) = delete;
  keyboard_painter(keyboard_painter&&) = delete;
  keyboard_painter& operator=(keyboard_painter&&) = delete;
  ~keyboard_painter();

  /// Draw `which` over whatever the renderer already holds, with `focus`
  /// outlined and every key whose latch bit is in `latched` lit.
  ///
  /// The atlas is built on the first call and kept. A renderer that
  /// refuses it costs the legends and nothing else — the keys are still
  /// there, still hittable, and the run does not stop over a font.
  void draw(SDL_Renderer* renderer,
            const machine::screen_keyboard::layout& which, std::size_t focus,
            std::uint8_t latched);

 private:
  void ensure_atlas(SDL_Renderer* renderer);

  SDL_Texture* atlas_{nullptr};
  bool atlas_tried_{false};
};

}  // namespace amberfolio::sdl
