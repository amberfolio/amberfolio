// SPDX-License-Identifier: AGPL-3.0-only
//
// The machine's own character generator, as one texture (#383).
//
// Two things this host paints over the window carry text — the on-screen
// keyboard's legends (#377) and the toggle panel's rows — and both want
// the same letters for the same reason `screen_keyboard_view.h` gives:
// furniture drawn in the machine's own font looks like it belongs to the
// machine it is attached to, rather than like whatever font a desktop
// happened to have. It also costs this host no font dependency at all.
//
// A texture rather than a rectangle per lit pixel: a glyph is eight rows
// of eight, and a panel's worth of them is thousands of draw calls a
// frame the moment it is spelled the obvious way. One atlas rather than
// one per painter, because a whole code page of cells is sixty-four
// kilobytes and there is no reason for two of them.
//
// White where a glyph is lit and fully transparent elsewhere, so one
// colour mod paints a run of text in whatever colour the caller wants.
//
// A renderer that refuses the texture costs the text and nothing else:
// every caller here draws its rectangles first and its letters after, so
// a refused atlas leaves a panel a person can still see and still hit,
// and the run does not stop over a font.

#pragma once

#include <SDL3/SDL.h>

#include <string_view>

#include "amberfolio/machine/font.h"

namespace amberfolio::sdl {

/// Glyphs across the atlas, and so its side in pixels. The whole code
/// page, because a label is a `std::string_view` and this file has no
/// business deciding which bytes may be in one.
inline constexpr int glyph_atlas_across = 16;
inline constexpr int glyph_atlas_side =
    glyph_atlas_across * machine::font::glyph_height;

/// The atlas, built on the first draw and kept for the renderer's life.
class glyph_atlas {
 public:
  glyph_atlas() = default;
  glyph_atlas(const glyph_atlas&) = delete;
  glyph_atlas& operator=(const glyph_atlas&) = delete;
  glyph_atlas(glyph_atlas&&) = delete;
  glyph_atlas& operator=(glyph_atlas&&) = delete;
  ~glyph_atlas();

  /// `text` drawn with its top-left corner at `x`, `y`, each glyph pixel
  /// `scale` window pixels square. Nothing at all when the renderer
  /// would not make the atlas.
  void draw(SDL_Renderer* renderer, std::string_view text, float x, float y,
            float scale, SDL_Color colour);

  /// How wide `characters` glyphs are at `scale`. The one piece of
  /// arithmetic every caller needs and none of them should repeat.
  [[nodiscard]] static constexpr float width_of(std::size_t characters,
                                                float scale) noexcept {
    return scale * static_cast<float>(machine::font::glyph_height) *
           static_cast<float>(characters);
  }

 private:
  void ensure(SDL_Renderer* renderer);

  SDL_Texture* atlas_{nullptr};
  bool tried_{false};
};

}  // namespace amberfolio::sdl
