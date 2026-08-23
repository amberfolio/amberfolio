// SPDX-License-Identifier: AGPL-3.0-only

#include "audio_gain.h"

namespace amberfolio::sdl {

audio_gain::audio_gain(unsigned sample_rate) noexcept {
  const float samples = static_cast<float>(sample_rate) * ramp_seconds;
  ramp_length_ = samples > 1.0F ? samples : 1.0F;
}

void audio_gain::set(float level) noexcept {
  const float clamped = level < 0.0F ? 0.0F : (level > 1.0F ? 1.0F : level);
  target_.store(clamped, std::memory_order_relaxed);
}

void audio_gain::apply(std::span<float> samples) noexcept {
  const float target = target_.load(std::memory_order_relaxed);

  if (target != seen_target_) {
    // The producer moved the level. Nothing was signalled and nothing
    // needed to be: a target that is not the one this side was walking
    // to *is* the notification, which is why the atomic can carry a
    // value rather than a handshake.
    //
    // Measured from where the walk has actually reached rather than from
    // where it started, so a change during a change is one walk and not
    // two — and it still takes `ramp_seconds`, because the step is the
    // remaining distance divided by that.
    seen_target_ = target;
    const float distance =
        target > current_ ? target - current_ : current_ - target;
    step_ = distance / ramp_length_;
  }

  if (target == current_) {
    // The settled case, and the one that matters most: at unity the
    // buffer is handed on untouched, so a default run's samples are the
    // bits `render()` produced and the measurements in docs/hosts.md §4
    // are measurements of this host too.
    if (current_ == 1.0F) {
      return;
    }
    for (float& sample : samples) {
      sample *= current_;
    }
    return;
  }

  // Walking. The step is taken before the multiply rather than after, so
  // that the first sample of a change is already on its way: a buffer
  // that began at the old gain exactly would leave a step at the buffer
  // boundary, which is the discontinuity the ramp exists to remove.
  //
  // It lands *on* the target rather than approaching it, which is what
  // makes muted mean 0.0F and not a small number.
  for (float& sample : samples) {
    if (current_ < target) {
      current_ += step_;
      if (current_ > target) {
        current_ = target;
      }
    } else {
      current_ -= step_;
      if (current_ < target) {
        current_ = target;
      }
    }
    sample *= current_;
  }
}

}  // namespace amberfolio::sdl
