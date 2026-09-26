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
// period takes bits 5-0 as its high six bits. After an attenuation or
// noise latch the NCR part ignores a data byte, where the TI parts would
// take its low four bits. A noise latch reseeds the shift register only
// when it changes bit 2, white or periodic; rewriting the same mode
// leaves the register running (#407).
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
// shifts its register on every second reload. The register is sixteen
// bits: each shift moves it right one and puts bit 1 XOR (white AND NOT
// bit 5) in at the top, and its output is the low bit.
//
// Each voice's output is 0 or its level, and a level is 2 dB per
// attenuation step down from `psg_voice_amplitude`, with 15 silent. The
// four are summed. Like the speaker, a voice at rest is exactly 0.0, so a
// silent chip is exact silence and adds nothing to a speaker-only run.
//
// **The noise register is the one choice here that is not the TI
// datasheet's.** The TI and NCR parts differ in its width, its taps and
// when a write reseeds it, and the last matters most: this program
// rewrites the noise control every 3.9 ms through a footstep. Reseeded
// each time, the noise would play only its first seven or so shifts over
// and over, a repeating pattern the ear hears as a ~470 Hz buzz; left
// running, it is a hiss. What is modelled is the NCR 8496, the part the
// Tandy 1000 carries: seeded with 0x8000, the taps above, reseeded only
// when the mode bit changes. The register is output only — the chip has
// no read port.

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
/// The bit a shift feeds, which is also the whole seed.
inline constexpr std::uint16_t psg_noise_feedback = 0x8000;
inline constexpr std::uint16_t psg_noise_seed = psg_noise_feedback;
inline constexpr std::uint16_t psg_noise_tap = 0x0002;
inline constexpr std::uint16_t psg_white_tap = 0x0020;

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

  /// Apply one byte written to the port. True when the write changed the
  /// noise mode, which reseeds the shift register.
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
  std::uint16_t shift_{psg_noise_seed};
  bool noise_flip_{};

  /// Chip clocks until the next counter step.
  std::uint32_t until_step_{psg_clocks_per_step};
};

}  // namespace amberfolio::machine
