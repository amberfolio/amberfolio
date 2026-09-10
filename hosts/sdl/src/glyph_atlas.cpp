// SPDX-License-Identifier: AGPL-3.0-only
//
// The character generator as a texture. glyph_atlas.h says why.

#include "glyph_atlas.h"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/machine/font.h"

namespace amberfolio::sdl {

glyph_atlas::~glyph_atlas() {
  if (atlas_ != nullptr) {
    SDL_DestroyTexture(atlas_);
  }
}

void glyph_atlas::ensure(SDL_Renderer* renderer) {
  if (tried_) {
    return;
  }
  tried_ = true;

  // On the heap: a whole code page of eight-by-eight cells is sixty-four
  // kilobytes, which is not a thing to put on a stack for the one frame
  // it is needed.
  std::vector<std::uint32_t> pixels(static_cast<std::size_t>(glyph_atlas_side) *
                                    static_cast<std::size_t>(glyph_atlas_side));
  const std::span<const std::uint8_t> glyphs = machine::font::glyphs();
  for (unsigned code = 0; code < machine::font::glyph_count; ++code) {
    const std::size_t at =
        static_cast<std::size_t>(code) * machine::font::glyph_height;
    if (at + machine::font::glyph_height > glyphs.size()) {
      break;
    }
    const auto cell_x = static_cast<std::size_t>(code % glyph_atlas_across) *
                        machine::font::glyph_height;
    const auto cell_y = static_cast<std::size_t>(code / glyph_atlas_across) *
                        machine::font::glyph_height;
    for (std::size_t line = 0; line < machine::font::glyph_height; ++line) {
      const std::uint8_t bits = glyphs[at + line];
      for (std::size_t bit = 0; bit < machine::font::glyph_height; ++bit) {
        // Bit 7 is the leftmost pixel — the EGA's own order (font.h).
        if ((bits & (0x80U >> bit)) != 0) {
          pixels[((cell_y + line) * glyph_atlas_side) + cell_x + bit] =
              0xFFFFFFFFU;
        }
      }
    }
  }

  atlas_ = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                             SDL_TEXTUREACCESS_STATIC, glyph_atlas_side,
                             glyph_atlas_side);
  if (atlas_ == nullptr) {
    return;
  }
  SDL_SetTextureBlendMode(atlas_, SDL_BLENDMODE_BLEND);
  SDL_SetTextureScaleMode(atlas_, SDL_SCALEMODE_NEAREST);
  SDL_UpdateTexture(atlas_, nullptr, pixels.data(),
                    glyph_atlas_side * static_cast<int>(sizeof(std::uint32_t)));
}

void glyph_atlas::draw(SDL_Renderer* renderer, std::string_view text, float x,
                       float y, float scale, SDL_Color colour) {
  if (renderer == nullptr || text.empty()) {
    return;
  }
  ensure(renderer);
  if (atlas_ == nullptr) {
    return;
  }
  const auto glyph = static_cast<float>(machine::font::glyph_height);
  SDL_SetTextureColorMod(atlas_, colour.r, colour.g, colour.b);
  SDL_SetTextureAlphaMod(atlas_, colour.a);
  float at = x;
  for (const char character : text) {
    const auto code = static_cast<int>(static_cast<unsigned char>(character));
    // Deliberately integer: these are the atlas cell's column and row,
    // not a fraction of one.
    const int cell_x =
        (code % glyph_atlas_across) * machine::font::glyph_height;
    const int cell_y =
        (code / glyph_atlas_across) * machine::font::glyph_height;
    const SDL_FRect from{.x = static_cast<float>(cell_x),
                         .y = static_cast<float>(cell_y),
                         .w = glyph,
                         .h = glyph};
    const SDL_FRect to{.x = at, .y = y, .w = scale * glyph, .h = scale * glyph};
    SDL_RenderTexture(renderer, atlas_, &from, &to);
    at += scale * glyph;
  }
}

}  // namespace amberfolio::sdl
