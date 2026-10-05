// SPDX-License-Identifier: AGPL-3.0-only

#include "desktop_window.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/renderer.h"
#include "audio_device.h"
#include "options.h"
#include "overlays.h"
#include "window_checks.h"

namespace amberfolio::sdl {

bool desktop_window::open(const options& opts,
                          std::vector<scripted_press>& presses,
                          audio_bridge& bridge) {
  const int w = static_cast<int>(machine::frame_width * opts.scale);
  const int h = static_cast<int>(machine::frame_height * opts.scale);
  if (!SDL_CreateWindowAndRenderer("amberfolio", w, h, 0, &window_,
                                   &renderer_)) {
    std::fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n",
                 SDL_GetError());
    SDL_Quit();
    return false;
  }
  // Now, and not at parse time: what SDL calls a key is a question
  // about SDL's own tables, and asking it before SDL_Init is asking it
  // early.
  for (scripted_press& press : presses) {
    press.code = SDL_GetScancodeFromName(press.key.c_str());
    if (press.code == SDL_SCANCODE_UNKNOWN) {
      std::fprintf(stderr, "amberfolio: SDL has no key called '%s'\n",
                   press.key.c_str());
      SDL_Quit();
      return false;
    }
  }

  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888,
                               SDL_TEXTUREACCESS_STREAMING,
                               static_cast<int>(machine::frame_width),
                               static_cast<int>(machine::frame_height));
  SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_NEAREST);

  const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                           .channels = 1,
                           .freq = static_cast<int>(audio_sample_rate)};
  audio_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                     feed_audio, &bridge);
  if (audio_ != nullptr) {
    SDL_ResumeAudioStreamDevice(audio_);
  }
  return true;
}

void desktop_window::present(const machine::machine& box, overlays& furniture,
                             bool verify, verify_report& report) {
  const std::span<const std::uint8_t> pixels = box.display().pixels();
  const std::span<const machine::rgb> palette = box.display().palette();
  for (std::size_t i = 0; i < argb_.size(); ++i) {
    const machine::rgb color = palette[pixels[i] & 0x0FU];
    argb_[i] = (static_cast<std::uint32_t>(color.red) << 16) |
               (static_cast<std::uint32_t>(color.green) << 8) |
               static_cast<std::uint32_t>(color.blue);
  }
  SDL_UpdateTexture(
      texture_, nullptr, argb_.data(),
      static_cast<int>(machine::frame_width * sizeof(std::uint32_t)));
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  if (verify) {
    verify_target(renderer_, argb_, report);
  }
  // After the read-back, and deliberately: `--verify` is a claim
  // about the machine's own pixels, and an overlay in the target
  // would make it a claim about this host's furniture instead.
  furniture.draw(renderer_);
  SDL_RenderPresent(renderer_);
  ++report.presented;
}

void desktop_window::close() {
  if (audio_ != nullptr) {
    SDL_DestroyAudioStream(audio_);
  }
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
  }
  if (renderer_ != nullptr) {
    SDL_DestroyRenderer(renderer_);
  }
  if (window_ != nullptr) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

}  // namespace amberfolio::sdl
