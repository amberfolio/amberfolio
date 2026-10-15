// SPDX-License-Identifier: AGPL-3.0-only
//
// The window, its renderer and the audio device (#472, split from main.cpp):
// opened once before the loop, presented to once a frame, and taken down
// before the run's closing reports so that nothing SDL writes on its way
// out can land in the middle of them.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "audio_device.h"
#include "options.h"
#include "overlays.h"
#include "window_checks.h"

namespace amberfolio::sdl {

class desktop_window {
 public:
  desktop_window() = default;
  desktop_window(const desktop_window&) = delete;
  desktop_window& operator=(const desktop_window&) = delete;

  /// Create the window, the texture the frame is uploaded to and the audio
  /// stream, and resolve each scripted press's key name to an SDL scancode.
  /// False, with a sentence said and SDL shut down, when any of it fails.
  /// A device that will not open is not a failure: the run goes on without
  /// sound, and `has_audio_device()` says so.
  [[nodiscard]] bool open(const options& opts,
                          std::vector<scripted_press>& presses,
                          audio_bridge& bridge);

  /// Upload the machine's frame and present it with the furniture over it.
  /// `--verify`'s readback is taken between the two.
  void present(const machine::machine& box, overlays& furniture, bool verify,
               verify_report& report);

  /// The audio stream first, and before the counters are read: it is what
  /// stops the callback thread, and until it has returned the tallies are
  /// still being written to. Then SDL itself.
  void close();

  [[nodiscard]] bool has_audio_device() const noexcept {
    return audio_ != nullptr;
  }
  [[nodiscard]] SDL_Window* window() const noexcept { return window_; }
  [[nodiscard]] SDL_Renderer* renderer() const noexcept { return renderer_; }

 private:
  SDL_Window* window_{nullptr};
  SDL_Renderer* renderer_{nullptr};
  SDL_Texture* texture_{nullptr};
  SDL_AudioStream* audio_{nullptr};
  std::vector<std::uint32_t> argb_ =
      std::vector<std::uint32_t>(machine::frame_pixels);
};

}  // namespace amberfolio::sdl
