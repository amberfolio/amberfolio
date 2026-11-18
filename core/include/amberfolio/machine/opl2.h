// SPDX-License-Identifier: AGPL-3.0-only
//
// The AdLib card's chip, as registers and as a waveform (#616).
//
// The chip is the Yamaha YM3812, the OPL2: nine channels of two-operator
// FM synthesis, or six and five rhythm instruments, programmed through a
// register address and a register data port. The card clocks it from the
// PC's 14.31818 MHz crystal divided by four, 3,579,545 Hz, and the chip
// makes one sample every 72 of its clocks: 49,716 Hz. The PIT counts at a
// twelfth of the same crystal, so **one sample is exactly 24 ticks** and
// nothing here rounds.
//
// This file is split along the line psg.h splits the Tandy chip:
//
//   * a register write is what the program *did*. The device (adlib.h)
//     keeps the register file on the machine thread, because it is
//     architectural state and goes into a checkpoint, and publishes every
//     write on the audio timeline at the tick it happened.
//   * `opl2_synth` is what the chip *sounds like*: the operators run
//     forward over an interval of virtual time from the same writes, on
//     the consumer's side of `audio_timeline`. It is output and never
//     state, and nothing about it can reach the machine.
//
//
// The registers
// -------------
//
//   01       bit 5 lets E0-F5 choose a waveform; the rest are test bits
//   02 03 04 the two timers and their control (adlib.h; no sound)
//   08       bit 6 chooses which F-number bit splits the keyboard
//   20+op    tremolo, vibrato, sustaining, key-scaled rate, multiple
//   40+op    key-scale level (bits 7-6), total level (bits 5-0)
//   60+op    attack rate, decay rate
//   80+op    sustain level, release rate
//   A0+ch    F-number, low eight bits
//   B0+ch    key on (bit 5), block (bits 4-2), F-number high two bits
//   BD       tremolo depth, vibrato depth, rhythm mode, the five drums
//   C0+ch    feedback (bits 3-1), additive (bit 0)
//   E0+op    waveform (bits 1-0)
//
// An operator's registers are at offsets 00-05, 08-0D and 10-15; channel
// `c`'s modulator is the operator at offset `c % 3 + 8 * (c / 3)` and its
// carrier the one three past it. Every other address is not a register,
// and a write to one is dropped, as the chip drops it.
//
//
// The waveform
// ------------
//
// Everything is the chip's own arithmetic, in integers:
//
//   * **Phase.** An operator's phase advances each sample by
//     `((fnum << block) >> 1) * multiple`, with the multiple in halves,
//     and its top ten bits index one period of the wave. So a channel
//     sounds at `fnum * 49716 / 2^(20 - block)` Hz times the multiple.
//   * **The sine**, as a quarter wave of logarithms: `opl2_log_sine[i]`
//     is `-log2(sin((i + 0.5) * pi / 512)) * 256`, rounded. Attenuation
//     adds to it, and `opl2_exponent` turns the sum back into a level:
//     `2^(i / 256) - 1`, times 1024, rounded. No multiplication, which
//     is the chip's own economy and why its attenuations are in
//     logarithms throughout.
//   * **The envelope**, nine bits of attenuation at 0.1875 dB a step:
//     attack, decay to the sustain level, sustain or a percussive
//     fall, release. A rate is four times the register plus a key
//     scaling of up to fifteen; a rate of `4n + f` moves `(4 + f) *
//     2^n / 32768` steps a sample, four at the most. Decay and release
//     are straight lines in decibels and the attack is the curve the
//     chip draws, an eighth of what is left each step.
//   * **Modulation.** The modulator's output is added to the carrier's
//     phase (or to the output, in additive mode), and the average of
//     the modulator's own last two outputs is fed back into its phase,
//     scaled by the feedback register.
//   * **The two low-frequency oscillators**: a 3.7 Hz triangle on the
//     level (tremolo) and a 6.1 Hz one on the F-number (vibrato), each
//     at one of two depths.
//   * **Rhythm.** With BD bit 5 set, channels 6 to 8 are a bass drum and
//     four instruments whose phases are made from two operators' phase
//     bits and a 23-bit noise register.
//
// A channel at full level is 4,084 either way and the nine are summed
// and clipped to sixteen bits, as the card's converter clips them. **An
// operator at full attenuation is exactly 0**, so a chip nobody has
// written is exact silence and adds nothing to a run without it.
//
// **Where this is not the chip.** The envelope's steps are spread evenly
// by a counter, where the chip spreads them through a small fixed
// pattern; the averages are the same and the sound is. The negative half
// of a wave is the negation of the positive, where the chip's is its
// one's complement, one unit lower: that unit is a standing offset the
// card's output filter removes, and leaving it out is what makes silence
// exact. The composite sine mode (08 bit 7) and the test bits of 01 are
// not modelled, and the device refuses a write that sets one.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

/// One of the chip's samples in ticks (this file's top comment).
inline constexpr std::uint32_t opl2_ticks_per_sample = 24;

inline constexpr std::size_t opl2_channels = 9;
inline constexpr std::size_t opl2_operators = 18;

/// What the chip's sixteen-bit full scale adds to a sample. One channel
/// at full level is an eighth of full scale, so it is about as loud as
/// one Tandy voice (`psg_voice_amplitude`), and all nine with the
/// speaker (`speaker_amplitude`) on top still stay inside the range.
inline constexpr float opl2_amplitude = 0.5F;

/// The chip's full scale, which the sum of the channels is clipped to.
inline constexpr std::int32_t opl2_full_scale = 32767;

/// The registers this file's top comment lists, by address.
namespace opl2_reg {
inline constexpr std::uint8_t test = 0x01;
inline constexpr std::uint8_t timer_1 = 0x02;
inline constexpr std::uint8_t timer_2 = 0x03;
inline constexpr std::uint8_t timer_control = 0x04;
inline constexpr std::uint8_t keyboard_split = 0x08;
inline constexpr std::uint8_t flags = 0x20;
inline constexpr std::uint8_t level = 0x40;
inline constexpr std::uint8_t attack_decay = 0x60;
inline constexpr std::uint8_t sustain_release = 0x80;
inline constexpr std::uint8_t frequency = 0xA0;
inline constexpr std::uint8_t key_block = 0xB0;
inline constexpr std::uint8_t rhythm = 0xBD;
inline constexpr std::uint8_t feedback = 0xC0;
inline constexpr std::uint8_t waveform = 0xE0;

/// 01 bit 5: E0-F5 choose a waveform. The other bits test the chip.
inline constexpr std::uint8_t waveform_enable = 0x20;
/// 08 bit 7: the composite sine mode, which is not modelled.
inline constexpr std::uint8_t composite_sine = 0x80;
/// 08 bit 6: the keyboard split takes F-number bit 8, not bit 9.
inline constexpr std::uint8_t note_select = 0x40;
}  // namespace opl2_reg

/// The quarter sine in logarithms and the exponent that undoes them
/// (this file's top comment). Named at namespace scope so the unit suite
/// can hold every entry to its formula.
extern const std::array<std::uint16_t, 256> opl2_log_sine;
extern const std::array<std::uint16_t, 256> opl2_exponent;

/// The operator at register offset `offset` (00-15), or `opl2_operators`
/// for an offset that names none.
[[nodiscard]] constexpr std::size_t opl2_operator_at(
    std::uint8_t offset) noexcept {
  const std::size_t column = offset & 0x07U;
  const std::size_t row = offset >> 3U;
  if (row > 2 || column > 5) {
    return opl2_operators;
  }
  return row * 6 + column;
}

/// The chip run forward in virtual time: the consumer's half.
class opl2_synth {
 public:
  opl2_synth() noexcept { reset(); }

  /// Power-on, and the start of a new run.
  void reset() noexcept;

  /// A byte written to register `reg`, at the current position.
  void write(std::uint8_t reg, std::uint8_t value) noexcept;

  /// Run `span` ticks forward and answer the output summed over them, a
  /// sample's sixteen-bit value for each tick it was held: dividing by
  /// `span` gives the mean level across the interval, which is the box
  /// filter, in the chip's own units.
  [[nodiscard]] double run(ticks span) noexcept;

  /// The output as it stands, in those units, for a hold.
  [[nodiscard]] std::int32_t output() const noexcept { return output_; }

  /// Whether every operator is at full attenuation and none is about to
  /// leave it: the chip is making exact silence and will until something
  /// is written.
  [[nodiscard]] bool silent() const noexcept;

 private:
  enum class stage : std::uint8_t { attack, decay, sustain, release };

  struct oper {
    // What the registers say.
    bool tremolo{};
    bool vibrato{};
    bool sustaining{};
    bool scaled_rate{};
    std::uint8_t multiple{};
    std::uint8_t scale_level{};
    std::uint8_t total_level{};
    std::uint8_t attack_rate{};
    std::uint8_t decay_rate{};
    std::uint8_t sustain_level{};
    std::uint8_t release_rate{};
    std::uint8_t wave{};

    // Where it is.
    std::uint32_t phase{};
    std::uint16_t envelope{0x1FF};
    stage at{stage::release};
    bool key{};
    std::int32_t out{};
    std::int32_t before{};
  };

  struct channel {
    std::uint16_t fnum{};
    std::uint8_t block{};
    bool key{};
    std::uint8_t feedback{};
    bool additive{};
  };

  /// One sample: the oscillators, every envelope, every channel.
  void sample() noexcept;

  void set_key(std::size_t op, bool down) noexcept;
  void refresh_keys() noexcept;

  [[nodiscard]] std::uint8_t rate_of(const oper& o, std::uint8_t reg_rate,
                                     const channel& c) const noexcept;
  [[nodiscard]] std::uint32_t envelope_steps(std::uint8_t rate) const noexcept;
  void advance_envelope(oper& o, const channel& c) noexcept;
  [[nodiscard]] std::uint32_t attenuation_of(const oper& o,
                                             const channel& c) const noexcept;
  [[nodiscard]] std::uint32_t phase_step(const oper& o,
                                         const channel& c) const noexcept;
  [[nodiscard]] std::int32_t wave_of(const oper& o, std::uint32_t phase,
                                     std::uint32_t attenuation) const noexcept;
  [[nodiscard]] std::int32_t melodic(std::size_t ch) noexcept;
  [[nodiscard]] std::int32_t rhythm() noexcept;

  std::array<oper, opl2_operators> ops_{};
  std::array<channel, opl2_channels> channels_{};

  bool waveforms_{};
  bool note_select_{};
  bool deep_tremolo_{};
  bool deep_vibrato_{};
  bool rhythm_{};
  std::uint8_t drums_{};

  /// Samples since power-on: the envelope's clock and both oscillators'.
  std::uint64_t counter_{};
  std::uint32_t noise_{1};

  /// This sample's tremolo, in envelope steps, and vibrato, as the
  /// position in its eight-step cycle.
  std::uint32_t tremolo_{};
  std::uint32_t vibrato_{};

  std::int32_t output_{};

  /// Ticks until the next sample.
  std::uint32_t until_sample_{opl2_ticks_per_sample};
};

}  // namespace amberfolio::machine
