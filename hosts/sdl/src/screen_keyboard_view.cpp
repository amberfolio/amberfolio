// SPDX-License-Identifier: AGPL-3.0-only
//
// The pixels of the on-screen keyboard. screen_keyboard_view.h says why
// they are here and not in core, and not in main.cpp either.

#include "screen_keyboard_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/machine/font.h"
#include "amberfolio/machine/screen_keyboard.h"

namespace amberfolio::sdl {
namespace {

namespace osk = machine::screen_keyboard;

/// How much of the window the keyboard may take: nearly all of its width,
/// and no more than half its height. The height limit is what stops the
/// `prompt` layout — one row, and forty quarter units wide — from filling
/// a tall window with four enormous keys.
constexpr float width_share = 0.94F;
constexpr float height_share = 0.5F;

/// Glyphs across the atlas, and so the atlas's side in pixels. The whole
/// code page, because a legend is a `std::string_view` and this file has
/// no business deciding which bytes may be in one.
constexpr int atlas_across = 16;
constexpr int atlas_side = atlas_across * machine::font::glyph_height;

/// The colours. Dark enough that the frame underneath is still readable
/// through the panel, and amber for the two states a player has to see at
/// a glance — which key the focus is on, and which modifier is latched.
constexpr SDL_Color panel_colour{.r = 20, .g = 16, .b = 12, .a = 220};
constexpr SDL_Color key_colour{.r = 42, .g = 33, .b = 21, .a = 235};
constexpr SDL_Color edge_colour{.r = 90, .g = 74, .b = 48, .a = 255};
constexpr SDL_Color legend_colour{.r = 232, .g = 217, .b = 189, .a = 255};
constexpr SDL_Color amber{.r = 224, .g = 163, .b = 60, .a = 255};
constexpr SDL_Color latched_legend{.r = 20, .g = 16, .b = 12, .a = 255};

void set_colour(SDL_Renderer* renderer, SDL_Color colour) {
  SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
}

/// How big a legend may be drawn on one key: as many pixels per glyph
/// pixel as the key can carry, across and down.
[[nodiscard]] float legend_scale(const SDL_FRect& key,
                                 std::size_t characters) noexcept {
  if (characters == 0) {
    return 0.0F;
  }
  const auto glyph = static_cast<float>(machine::font::glyph_height);
  const float across =
      (key.w * 0.86F) / (glyph * static_cast<float>(characters));
  const float down = (key.h * 0.7F) / glyph;
  return std::min(across, down);
}

/// One size for the whole keyboard: the largest that every legend on it
/// fits at, and never less than one pixel per glyph pixel.
///
/// Sized per key it would be `Esc` in half the type of the `1` beside it,
/// which reads as a mistake rather than as a keyboard. Real keycaps carry
/// one size of legend and so does this.
[[nodiscard]] float board_scale(const keyboard_box& box,
                                const osk::layout& which) noexcept {
  float scale = 0.0F;
  for (const osk::key& one : which.keys) {
    const float fits = legend_scale(key_rect(box, one), one.label.size());
    if (fits > 0.0F && (scale == 0.0F || fits < scale)) {
      scale = fits;
    }
  }
  return std::max(1.0F, scale);
}

void draw_legend(SDL_Renderer* renderer, SDL_Texture* atlas,
                 const SDL_FRect& key, std::string_view label, float scale,
                 SDL_Color colour) {
  if (atlas == nullptr || label.empty()) {
    return;
  }
  const auto glyph = static_cast<float>(machine::font::glyph_height);
  const float width = scale * glyph * static_cast<float>(label.size());
  float x = key.x + ((key.w - width) / 2.0F);
  const float y = key.y + ((key.h - (scale * glyph)) / 2.0F);

  SDL_SetTextureColorMod(atlas, colour.r, colour.g, colour.b);
  for (const char character : label) {
    const auto code = static_cast<int>(static_cast<unsigned char>(character));
    // Deliberately integer: these are the atlas cell's column and row,
    // not a fraction of one.
    const int cell_x = (code % atlas_across) * machine::font::glyph_height;
    const int cell_y = (code / atlas_across) * machine::font::glyph_height;
    const SDL_FRect from{.x = static_cast<float>(cell_x),
                         .y = static_cast<float>(cell_y),
                         .w = glyph,
                         .h = glyph};
    const SDL_FRect to{.x = x, .y = y, .w = scale * glyph, .h = scale * glyph};
    SDL_RenderTexture(renderer, atlas, &from, &to);
    x += scale * glyph;
  }
}

}  // namespace

keyboard_box fit_keyboard(const osk::layout& which, int window_width,
                          int window_height) noexcept {
  keyboard_box box{};
  if (which.width == 0 || which.rows == 0) {
    return box;
  }
  const auto rows_in_quarters =
      static_cast<float>(which.rows) * static_cast<float>(osk::unit);
  const float across = (static_cast<float>(window_width) * width_share) /
                       static_cast<float>(which.width);
  const float down =
      (static_cast<float>(window_height) * height_share) / rows_in_quarters;
  box.quarter = std::max(1.0F, std::min(across, down));
  box.width = box.quarter * static_cast<float>(which.width);
  box.height = box.quarter * rows_in_quarters;
  box.left = (static_cast<float>(window_width) - box.width) / 2.0F;
  // Sitting near the bottom edge, two quarter units clear of it: one is
  // the panel's own margin around the keys and the other is the gap
  // between the panel and the window.
  box.top =
      static_cast<float>(window_height) - box.height - (2.0F * box.quarter);
  return box;
}

SDL_FRect key_rect(const keyboard_box& box, const osk::key& one) noexcept {
  // The inset is a fraction of a quarter unit rather than a pixel, so
  // that the gap between two keys is the same fraction of a key at every
  // window size.
  const float inset = box.quarter * 0.12F;
  return {
      .x = box.left + (box.quarter * static_cast<float>(one.column)) + inset,
      .y = box.top +
           (box.quarter * static_cast<float>(one.row) *
            static_cast<float>(osk::unit)) +
           inset,
      .w = (box.quarter * static_cast<float>(one.width)) - (2.0F * inset),
      .h = (box.quarter * static_cast<float>(osk::unit)) - (2.0F * inset)};
}

std::size_t key_under(const keyboard_box& box, const osk::layout& which,
                      float x, float y) noexcept {
  if (box.quarter <= 0.0F || x < box.left || y < box.top ||
      x >= box.left + box.width || y >= box.top + box.height) {
    return osk::no_key;
  }
  // Both differences are non-negative — the bounds above are what says
  // so — which is what lets these be unsigned and the row compare be one
  // signedness throughout.
  const auto column = static_cast<unsigned>((x - box.left) / box.quarter);
  const auto row = static_cast<unsigned>(
      (y - box.top) / (box.quarter * static_cast<float>(osk::unit)));
  if (row >= static_cast<unsigned>(which.rows)) {
    return osk::no_key;
  }
  return osk::key_at(which, static_cast<std::uint8_t>(row),
                     static_cast<std::uint16_t>(column));
}

keyboard_painter::~keyboard_painter() {
  if (atlas_ != nullptr) {
    SDL_DestroyTexture(atlas_);
  }
}

void keyboard_painter::ensure_atlas(SDL_Renderer* renderer) {
  if (atlas_tried_) {
    return;
  }
  atlas_tried_ = true;

  // White where the glyph is lit and fully transparent elsewhere, so that
  // one colour mod paints a legend in whatever colour the key wants.
  // On the heap: a whole code page of eight-by-eight cells is sixty-four
  // kilobytes, which is not a thing to put on a stack for the one frame
  // it is needed.
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(atlas_side) *
                                    static_cast<std::size_t>(atlas_side));
  const std::span<const std::uint8_t> glyphs = machine::font::glyphs();
  for (unsigned code = 0; code < machine::font::glyph_count; ++code) {
    const std::size_t at =
        static_cast<std::size_t>(code) * machine::font::glyph_height;
    if (at + machine::font::glyph_height > glyphs.size()) {
      break;
    }
    const auto cell_x = static_cast<std::size_t>(code % atlas_across) *
                        machine::font::glyph_height;
    const auto cell_y = static_cast<std::size_t>(code / atlas_across) *
                        machine::font::glyph_height;
    for (std::size_t line = 0; line < machine::font::glyph_height; ++line) {
      const std::uint8_t bits = glyphs[at + line];
      for (std::size_t bit = 0; bit < machine::font::glyph_height; ++bit) {
        // Bit 7 is the leftmost pixel — the EGA's own order (font.h).
        if ((bits & (0x80U >> bit)) != 0) {
          pixels[((cell_y + line) * atlas_side) + cell_x + bit] = 0xFFFFFFFFU;
        }
      }
    }
  }

  atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, atlas_side, atlas_side);
  if (atlas_ == nullptr) {
    return;
  }
  SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_NEAREST);
  SDL_UpdateTexture(atlas_, nullptr, pixels.data(),
                    atlas_side * static_cast<int>(sizeof(std::uint32_t)));
}

void keyboard_painter::draw(SDL_Renderer* renderer, const osk::layout& which,
                            std::size_t focus, std::uint8_t latched) {
  if (renderer == nullptr) {
    return;
  }
  int window_width = 0;
  int window_height = 0;
  if (!SDL_GetRenderOutputSize(renderer, &window_width, &window_height)) {
    return;
  }
  const keyboard_box box = fit_keyboard(which, window_width, window_height);
  if (box.quarter <= 0.0F) {
    return;
  }
  ensure_atlas(renderer);
  const float scale = board_scale(box, which);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const float margin = box.quarter;
  const SDL_FRect panel{.x = box.left - margin,
                        .y = box.top - margin,
                        .w = box.width + (2.0F * margin),
                        .h = box.height + (2.0F * margin)};
  set_colour(renderer, panel_colour);
  SDL_RenderFillRect(renderer, &panel);

  for (std::size_t i = 0; i < which.keys.size(); ++i) {
    const osk::key& one = which.keys[i];
    const SDL_FRect rect = key_rect(box, one);
    const auto bit = static_cast<std::uint8_t>(osk::latch_of(one.scancode));
    const bool lit = bit != 0 && (latched & bit) != 0;

    set_colour(renderer, lit ? amber : key_colour);
    SDL_RenderFillRect(renderer, &rect);
    set_colour(renderer, i == focus ? amber : edge_colour);
    SDL_RenderRect(renderer, &rect);
    draw_legend(renderer, atlas_, rect, one.label, scale,
                lit ? latched_legend : legend_colour);
  }
}

}  // namespace amberfolio::sdl
