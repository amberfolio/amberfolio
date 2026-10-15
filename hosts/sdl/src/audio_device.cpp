// SPDX-License-Identifier: AGPL-3.0-only

#include "audio_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "amberfolio/machine/machine.h"

namespace amberfolio::sdl {

void capture_samples(audio_bridge& bridge, std::span<const float> pulled) {
  if (bridge.capture.empty()) {
    return;
  }
  const std::size_t at = bridge.captured.load(std::memory_order_relaxed);
  const std::size_t room = bridge.capture.size() - at;
  const std::size_t count = pulled.size() < room ? pulled.size() : room;
  for (std::size_t i = 0; i < count; ++i) {
    bridge.capture[at + i] = pulled[i];
  }
  bridge.captured.store(at + count, std::memory_order_relaxed);
  if (count < pulled.size()) {
    bridge.truncated.store(true, std::memory_order_relaxed);
  }
}

void SDLCALL feed_audio(void* userdata, SDL_AudioStream* stream, int additional,
                        int /*total*/) {
  auto* bridge = static_cast<audio_bridge*>(userdata);
  if (bridge == nullptr || bridge->box == nullptr || additional <= 0) {
    return;
  }

  const auto wanted = static_cast<std::size_t>(additional) / sizeof(float);
  if (bridge->scratch.size() < wanted) {
    // Grown on the audio thread, which is not ideal, but it happens once
    // per device-buffer size rather than per callback and the alternative
    // is guessing SDL's buffer size before it tells us.
    bridge->scratch.resize(wanted);
  }

  const std::span<float> out(bridge->scratch.data(), wanted);
  bridge->box->audio().render(out, audio_sample_rate);

  // Captured before the gain and played after it. `--dump`'s WAV is a
  // rendering of what the *machine* made — the artefact docs/hosts.md §3
  // sends a person to when they are trying to tell a machine fault from a
  // host fault — and a listening level is no part of that. What goes to
  // the device is the other thing, and `sounded` below counts that one.
  capture_samples(*bridge, out);

  bridge->gain.apply(out);
  SDL_PutAudioStreamData(stream, out.data(),
                         static_cast<int>(wanted * sizeof(float)));

  std::uint64_t sounded = 0;
  for (const float sample : out) {
    if (sample != 0.0F) {
      ++sounded;
    }
  }

  bridge->callbacks.fetch_add(1, std::memory_order_relaxed);
  bridge->samples.fetch_add(wanted, std::memory_order_relaxed);
  bridge->sounded.fetch_add(sounded, std::memory_order_relaxed);
}

void listening_level::louder() noexcept {
  if (muted) {
    // Louder, while latched to silence, plainly means "let me
    // hear it" — so the latch lifts and the level it lifts to is
    // the one that was already there. One press, one audible
    // change.
    muted = false;
  } else {
    const auto next = std::ranges::find_if(
        volume_rungs, [this](float rung) { return rung > volume; });
    volume = next != volume_rungs.end() ? *next : volume_rungs.front();
  }
}

void listening_level::say() const {
  if (muted) {
    std::fprintf(stderr, "amberfolio: audio muted\n");
  } else {
    std::fprintf(stderr, "amberfolio: audio volume %ld%%\n",
                 std::lround(volume * 100.0F));
  }
}

}  // namespace amberfolio::sdl
