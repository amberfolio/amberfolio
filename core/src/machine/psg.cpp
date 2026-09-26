// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/psg.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

namespace {

constexpr std::uint8_t latch_bit = 0x80;
constexpr std::uint8_t white_noise_bit = 0x04;
constexpr std::uint8_t noise_rate_mask = 0x03;

/// 2 dB a step: 10^(-step / 10), 15 silent.
constexpr std::array<float, 16> levels{
    1.000000F, 0.794328F, 0.630957F, 0.501187F, 0.398107F, 0.316228F,
    0.251189F, 0.199526F, 0.158489F, 0.125893F, 0.100000F, 0.079433F,
    0.063096F, 0.050119F, 0.039811F, 0.0F};

}  // namespace

bool psg_registers::write(std::uint8_t value) noexcept {
  if ((value & latch_bit) != 0) {
    latched = static_cast<std::uint8_t>((value >> 4) & 0x07);
  }
  const auto voice = static_cast<std::size_t>(latched >> 1);
  const bool is_attenuation = (latched & 0x01) != 0;
  const auto low = static_cast<std::uint8_t>(value & 0x0F);

  if (is_attenuation) {
    attenuation[voice] = low;
    return false;
  }
  if (voice == psg_noise_voice) {
    noise = static_cast<std::uint8_t>(low & 0x07);
    return true;
  }
  if ((value & latch_bit) != 0) {
    period[voice] = static_cast<std::uint16_t>((period[voice] & 0x3F0) | low);
  } else {
    period[voice] = static_cast<std::uint16_t>((period[voice] & 0x00F) |
                                               ((value & 0x3F) << 4));
  }
  return false;
}

void psg_synth::reset() noexcept {
  regs_ = psg_registers::powered_on();
  counter_.fill(0);
  high_.fill(false);
  shift_ = psg_noise_seed;
  noise_flip_ = false;
  until_step_ = psg_clocks_per_step;
}

void psg_synth::write(std::uint8_t value) noexcept {
  if (regs_.write(value)) {
    shift_ = psg_noise_seed;
    high_[psg_noise_voice] = (shift_ & 0x01) != 0;
  }
}

std::uint16_t psg_synth::reload_of(std::size_t voice) const noexcept {
  if (voice == psg_noise_voice) {
    const unsigned rate = regs_.noise & noise_rate_mask;
    if (rate == 3) {
      return reload_of(2);
    }
    return static_cast<std::uint16_t>(0x10U << rate);
  }
  const std::uint16_t period = regs_.period[voice];
  return period == 0 ? std::uint16_t{0x400} : period;
}

void psg_synth::step() noexcept {
  for (std::size_t voice = 0; voice < psg_voices; ++voice) {
    if (counter_[voice] > 1) {
      --counter_[voice];
      continue;
    }
    counter_[voice] = reload_of(voice);
    if (voice != psg_noise_voice) {
      high_[voice] = !high_[voice];
      continue;
    }
    noise_flip_ = !noise_flip_;
    if (!noise_flip_) {
      continue;
    }
    const bool white = (regs_.noise & white_noise_bit) != 0;
    if ((shift_ & 0x01) != 0) {
      shift_ ^= white ? psg_white_feedback : psg_periodic_feedback;
    }
    shift_ >>= 1;
    high_[psg_noise_voice] = (shift_ & 0x01) != 0;
  }
}

float psg_synth::level() const noexcept {
  float sum = 0.0F;
  for (std::size_t voice = 0; voice < psg_voices; ++voice) {
    if (high_[voice]) {
      sum += levels[regs_.attenuation[voice] & 0x0F];
    }
  }
  return sum * psg_voice_amplitude;
}

double psg_synth::run(ticks span) noexcept {
  // In chip clocks, a third of a tick each. The level only changes at a
  // step, so it is held across each run of clocks up to the next one.
  std::uint64_t clocks = span * psg_clocks_per_tick;
  double total = 0.0;
  while (clocks > 0) {
    const std::uint64_t held =
        clocks < until_step_ ? clocks : std::uint64_t{until_step_};
    total += static_cast<double>(level()) * static_cast<double>(held);
    clocks -= held;
    until_step_ -= static_cast<std::uint32_t>(held);
    if (until_step_ == 0) {
      step();
      until_step_ = psg_clocks_per_step;
    }
  }
  return total;
}

}  // namespace amberfolio::machine
