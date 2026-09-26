// SPDX-License-Identifier: AGPL-3.0-only
//
// The Tandy 1000's sound chip, as registers and as a waveform (#404).
//
// The chip is the TI SN76489 family — on the Tandy 1000, the NCR 8496
// second source — clocked at 3,579,545 Hz: three square-wave tone voices
// and one noise voice, each with a four-bit attenuation, programmed
// through a single write-only port. The PC's crystal is 14.31818 MHz, the
// PIT counts at a twelfth of it and the chip at a quarter, so **one chip
// clock is exactly a third of a tick** and nothing here rounds.
//
// This file is split along the same line platform.h splits the speaker:
//
//   * `psg_registers` is what a write *means* — the latch, the three
//     ten-bit periods, the noise control and the four attenuations. The
//     device (tandy_sound.h) keeps one on the machine thread, because it
//     is architectural state and goes into a checkpoint; the synthesizer
//     keeps its own copy on the audio thread, fed by the same writes.
//   * `psg_synth` is what the chip *sounds like*: the counters and the
//     noise shift register, run forward over an interval of virtual time
//     and box-filtered. It lives on the consumer side of
//     `audio_timeline`, it is output and never state, and nothing about
//     it can reach the machine — exactly the speaker's bargain.
//
//
// The write protocol
// ------------------
//
// A byte with bit 7 set **latches** a register and writes its low four
// bits: bits 6-5 are the voice (0-2 tone, 3 noise), bit 4 says
// attenuation (1) or period/control (0), bits 3-0 are the data. A byte
// with bit 7 clear is a **data** byte for whatever is latched: a tone
// period takes bits 5-0 as its high six bits; anything else takes bits
// 3-0 as its low four, which is what the TI parts do with a data byte
// after an attenuation or noise latch. A write to the noise register, by
// either kind of byte, reseeds the shift register.
//
//
// The waveform
// ------------
//
// Every sixteen chip clocks each voice's counter counts down one. A tone
// voice that reaches zero reloads from its period and flips its output,
// so a period of N sounds at 3,579,545 / (32 N) Hz; a period of 0 counts
// as 1024, the TI behaviour. The noise voice reloads from 16, 32 or 64
// for noise rates 0-2, or from tone voice 2's period for rate 3, and
// shifts its register on every second reload; its output is the
// register's low bit.
//
// Each voice's output is 0 or its level, and a level is 2 dB per
// attenuation step down from `psg_voice_amplitude`, with 15 silent. The
// four are summed. Like the speaker, a voice at rest is exactly 0.0, so a
// silent chip is exact silence and adds nothing to a speaker-only run.
//
// **The noise register is the one choice here that is not the chip's
// arithmetic.** The TI and NCR parts differ in its width and taps, and a
// write to the noise control reseeds it — which matters more than it
// sounds, because a program that rewrites that register every few
// milliseconds (this one does, for its footsteps) hears only the first
// few shifts after a seed, over and over. What is modelled is the
// register DOSBox's Tandy sound runs, after MAME's SN76496 of its day: a
// Galois shift register seeded with 0x0F35, fed back with 0x14002 for
// white noise and 0x08000 for periodic. That is the machine the Steam
// release's own launcher starts, so a store copy sounds here the way it
// sounds there. A different choice changes the texture of a hiss and
// nothing a program can observe — the chip has no read port.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

/// The chip's clock is three times the PIT's (this file's top comment).
inline constexpr std::uint32_t psg_clocks_per_tick = 3;

/// Chip clocks per counter step.
inline constexpr std::uint32_t psg_clocks_per_step = 16;

/// What one voice at attenuation 0 adds to a sample. Four voices at full
/// level reach half of full scale, and the speaker's quarter
/// (`speaker_amplitude`) on top of that still stays inside it.
inline constexpr float psg_voice_amplitude = 0.125F;

/// Voices 0-2 are tone, 3 is noise.
inline constexpr std::size_t psg_voices = 4;
inline constexpr std::size_t psg_noise_voice = 3;

/// The attenuation that silences a voice.
inline constexpr std::uint8_t psg_silent = 15;

/// The shift register's seed and feedback (this file's top comment).
inline constexpr std::uint32_t psg_noise_seed = 0x0F35;
inline constexpr std::uint32_t psg_white_feedback = 0x14002;
inline constexpr std::uint32_t psg_periodic_feedback = 0x08000;

/// The chip's registers, and what a written byte does to them.
struct psg_registers {
  /// Which register the last latch byte named: voice * 2 + (1 for
  /// attenuation).
  std::uint8_t latched{};

  /// Tone voices' ten-bit periods. `period[3]` is unused.
  std::array<std::uint16_t, psg_voices> period{};

  /// The noise voice's three-bit control: bit 2 white (1) or periodic
  /// (0), bits 1-0 the rate.
  std::uint8_t noise{};

  /// Four-bit attenuations, 15 silent.
  std::array<std::uint8_t, psg_voices> attenuation{};

  /// The chip at power-on: every voice silent. The TI parts come up
  /// making noise, and a Tandy's BIOS silences the chip before anything
  /// runs; this is the state a program finds.
  static constexpr psg_registers powered_on() noexcept {
    psg_registers r;
    r.attenuation.fill(psg_silent);
    return r;
  }

  /// Apply one byte written to the port. True when the write touched the
  /// noise register, which reseeds the shift register.
  bool write(std::uint8_t value) noexcept;
};

/// The chip run forward in virtual time: the consumer's half.
class psg_synth {
 public:
  psg_synth() noexcept { reset(); }

  /// Power-on, and the start of a new run.
  void reset() noexcept;

  /// A byte written to the port, at the current position.
  void write(std::uint8_t value) noexcept;

  /// Run `span` ticks forward and answer the output summed over them in
  /// units of a third of a tick — so dividing by `3 * span` gives the
  /// mean level across the interval, which is the box filter.
  [[nodiscard]] double run(ticks span) noexcept;

  /// The output as it stands, for a hold.
  [[nodiscard]] float level() const noexcept;

  [[nodiscard]] const psg_registers& registers() const noexcept {
    return regs_;
  }

 private:
  /// One counter step: count every voice down and reload what expired.
  void step() noexcept;

  [[nodiscard]] std::uint16_t reload_of(std::size_t voice) const noexcept;

  psg_registers regs_{};
  std::array<std::uint16_t, psg_voices> counter_{};
  std::array<bool, psg_voices> high_{};
  std::uint32_t shift_{psg_noise_seed};
  bool noise_flip_{};

  /// Chip clocks until the next counter step.
  std::uint32_t until_step_{psg_clocks_per_step};
};

}  // namespace amberfolio::machine
