// SPDX-License-Identifier: AGPL-3.0-only

#include "window_checks.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <span>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/renderer.h"
#include "audio_device.h"

namespace amberfolio::sdl {

void push_key_event(SDL_Window* window, SDL_Scancode code, bool down) {
  SDL_Event event{};
  event.key.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.timestamp = SDL_GetTicksNS();
  event.key.windowID = window != nullptr ? SDL_GetWindowID(window) : 0;
  event.key.scancode = code;
  event.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
  event.key.mod = SDL_KMOD_NONE;
  event.key.down = down;
  event.key.repeat = false;
  SDL_PushEvent(&event);
}

void verify_target(SDL_Renderer* renderer, std::span<const std::uint32_t> src,
                   verify_report& report) {
  SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
  if (shot == nullptr) {
    ++report.unreadable;
    return;
  }

  SDL_Surface* rgb = SDL_ConvertSurface(shot, SDL_PIXELFORMAT_XRGB8888);
  SDL_DestroySurface(shot);
  if (rgb == nullptr) {
    ++report.unreadable;
    return;
  }

  // Whole multiples only. A HiDPI backing store makes the target a larger
  // multiple than `--scale` asked for, which is still exact and still
  // checkable; anything that is not a multiple at all is a target this
  // function has no derivation for, and it says so rather than guessing.
  const auto width = static_cast<unsigned>(rgb->w);
  const auto height = static_cast<unsigned>(rgb->h);
  const unsigned scale_x = width / machine::frame_width;
  const unsigned scale_y = height / machine::frame_height;
  if (scale_x == 0 || scale_y == 0 || width != machine::frame_width * scale_x ||
      height != machine::frame_height * scale_y) {
    ++report.odd_size;
    SDL_DestroySurface(rgb);
    return;
  }

  const bool lock = SDL_MUSTLOCK(rgb);
  if (lock && !SDL_LockSurface(rgb)) {
    ++report.unreadable;
    SDL_DestroySurface(rgb);
    return;
  }

  const auto* base = static_cast<const std::uint8_t*>(rgb->pixels);
  const auto pitch = static_cast<std::size_t>(rgb->pitch);
  std::uint64_t wrong = 0;
  for (unsigned y = 0; y < height; ++y) {
    const auto* row = reinterpret_cast<const std::uint32_t*>(base + y * pitch);
    const std::size_t source_row =
        static_cast<std::size_t>(y / scale_y) * machine::frame_width;
    for (unsigned x = 0; x < width; ++x) {
      // The top eight bits are the X of XRGB8888 and belong to nobody.
      if ((row[x] & 0x00FFFFFFU) != src[source_row + (x / scale_x)]) {
        ++wrong;
      }
    }
  }

  if (lock) {
    SDL_UnlockSurface(rgb);
  }
  SDL_DestroySurface(rgb);

  ++report.checked;
  report.mismatched += wrong;
}

bool report_verify(verify_report& report, const machine::machine& box,
                   const audio_bridge& bridge) {
  report.composed = box.display().generation();
  std::fprintf(stderr,
               "amberfolio: verify - composed %llu, presented %llu,"
               " checked %llu, mismatched pixels %llu\n",
               static_cast<unsigned long long>(report.composed),
               static_cast<unsigned long long>(report.presented),
               static_cast<unsigned long long>(report.checked),
               static_cast<unsigned long long>(report.mismatched));
  std::fprintf(stderr,
               "amberfolio: verify - audio callbacks %llu, audio samples"
               " %llu, sounded %llu, keys posted %llu\n",
               static_cast<unsigned long long>(
                   bridge.callbacks.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   bridge.samples.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(
                   bridge.sounded.load(std::memory_order_relaxed)),
               static_cast<unsigned long long>(report.keys));
  if (report.unreadable != 0 || report.odd_size != 0) {
    std::fprintf(stderr,
                 "amberfolio: verify - %llu targets would not read back,"
                 " %llu were not a whole multiple of the frame\n",
                 static_cast<unsigned long long>(report.unreadable),
                 static_cast<unsigned long long>(report.odd_size));
  }

  // What makes this a check rather than a printout. A run that
  // presented nothing proves nothing, and neither does one whose every
  // present was unreadable - so both are failures, in the same breath
  // as a picture that came back wrong.
  const char* wrong = nullptr;
  if (report.presented == 0) {
    wrong = "nothing was ever presented";
  } else if (report.checked == 0) {
    wrong = "no presented frame could be read back and compared";
  } else if (report.mismatched != 0) {
    wrong = "the presented picture is not the one that was uploaded";
  }
  if (wrong != nullptr) {
    std::fprintf(stderr, "amberfolio: verify FAILED - %s\n", wrong);
    std::fflush(stdout);
    return false;
  }
  std::fprintf(stderr, "amberfolio: verify OK\n");
  return true;
}

}  // namespace amberfolio::sdl
