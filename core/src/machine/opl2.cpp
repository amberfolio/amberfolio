// SPDX-License-Identifier: AGPL-3.0-only
//
// opl2.h has the chip; this is its two tables and its arithmetic.

#include "amberfolio/machine/opl2.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

// `-log2(sin((i + 0.5) * pi / 512)) * 256`, rounded: a quarter of a sine
// as attenuations, 256 to the octave. The unit suite recomputes every
// entry.
const std::array<std::uint16_t, 256> opl2_log_sine{{
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979, 949,
    920,  894,  869,  846,  825,  804,  785,  767,  749,  732,  717,  701, 687,
    672,  659,  646,  633,  621,  609,  598,  587,  576,  566,  556,  546, 536,
    527,  518,  509,  501,  492,  484,  476,  468,  461,  453,  446,  439, 432,
    425,  418,  411,  405,  399,  392,  386,  380,  375,  369,  363,  358, 352,
    347,  341,  336,  331,  326,  321,  316,  311,  307,  302,  297,  293, 289,
    284,  280,  276,  271,  267,  263,  259,  255,  251,  248,  244,  240, 236,
    233,  229,  226,  222,  219,  215,  212,  209,  205,  202,  199,  196, 193,
    190,  187,  184,  181,  178,  175,  172,  169,  167,  164,  161,  159, 156,
    153,  151,  148,  146,  143,  141,  138,  136,  134,  131,  129,  127, 125,
    122,  120,  118,  116,  114,  112,  110,  108,  106,  104,  102,  100, 98,
    96,   94,   92,   91,   89,   87,   85,   83,   82,   80,   78,   77,  75,
    74,   72,   70,   69,   67,   66,   64,   63,   62,   60,   59,   57,  56,
    55,   53,   52,   51,   49,   48,   47,   46,   45,   43,   42,   41,  40,
    39,   38,   37,   36,   35,   34,   33,   32,   31,   30,   29,   28,  27,
    26,   25,   24,   23,   23,   22,   21,   20,   20,   19,   18,   17,  17,
    16,   15,   15,   14,   13,   13,   12,   12,   11,   10,   10,   9,   9,
    8,    8,    7,    7,    7,    6,    6,    5,    5,    5,    4,    4,   4,
    3,    3,    3,    2,    2,    2,    2,    1,    1,    1,    1,    1,   1,
    1,    0,    0,    0,    0,    0,    0,    0,    0,
}};

// `(2^(i / 256) - 1) * 1024`, rounded: the fraction of an octave back
// into a level. The unit suite recomputes every entry.
const std::array<std::uint16_t, 256> opl2_exponent{{
    0,    3,    6,    8,    11,  14,  17,  20,  22,  25,  28,  31,  34,  37,
    40,   42,   45,   48,   51,  54,  57,  60,  63,  66,  69,  72,  75,  78,
    81,   84,   87,   90,   93,  96,  99,  102, 105, 108, 111, 114, 117, 120,
    123,  126,  130,  133,  136, 139, 142, 145, 148, 152, 155, 158, 161, 164,
    168,  171,  174,  177,  181, 184, 187, 190, 194, 197, 200, 204, 207, 210,
    214,  217,  220,  224,  227, 231, 234, 237, 241, 244, 248, 251, 255, 258,
    262,  265,  268,  272,  276, 279, 283, 286, 290, 293, 297, 300, 304, 308,
    311,  315,  318,  322,  326, 329, 333, 337, 340, 344, 348, 352, 355, 359,
    363,  367,  370,  374,  378, 382, 385, 389, 393, 397, 401, 405, 409, 412,
    416,  420,  424,  428,  432, 436, 440, 444, 448, 452, 456, 460, 464, 468,
    472,  476,  480,  484,  488, 492, 496, 501, 505, 509, 513, 517, 521, 526,
    530,  534,  538,  542,  547, 551, 555, 560, 564, 568, 572, 577, 581, 585,
    590,  594,  599,  603,  607, 612, 616, 621, 625, 630, 634, 639, 643, 648,
    652,  657,  661,  666,  670, 675, 680, 684, 689, 693, 698, 703, 708, 712,
    717,  722,  726,  731,  736, 741, 745, 750, 755, 760, 765, 770, 774, 779,
    784,  789,  794,  799,  804, 809, 814, 819, 824, 829, 834, 839, 844, 849,
    854,  859,  864,  869,  874, 880, 885, 890, 895, 900, 906, 911, 916, 921,
    927,  932,  937,  942,  948, 953, 959, 964, 969, 975, 980, 986, 991, 996,
    1002, 1007, 1013, 1018,
}};

namespace {

/// The frequency multiple the register's four bits choose, in halves.
constexpr std::array<std::uint8_t, 16> multiple_halves{
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

/// Key-scale level at 6 dB an octave, by the F-number's top four bits,
/// in steps of 0.75 dB, before the block's share comes off: the chip's
/// own sixteen numbers, which are close to `32 + 8 * log2(i)` and not
/// equal to it.
constexpr std::array<std::uint8_t, 16> scale_level_by_fnum{
    0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};

/// How far the register's two bits shift that down: off, 3 dB an octave,
/// 1.5 dB, 6 dB.
constexpr std::array<std::uint8_t, 4> scale_level_shift{8, 1, 2, 0};

/// The vibrato's eight-step triangle, in halves of its depth.
constexpr std::array<std::int8_t, 8> vibrato_shape{0, 1, 2, 1, 0, -1, -2, -1};

/// Full attenuation: nine bits, 0.1875 dB a step.
constexpr std::uint16_t silent_envelope = 0x1FF;

/// The fastest rate a decay has, and the rate from which an attack is
/// immediate.
constexpr std::uint8_t top_rate = 60;

/// The tremolo's triangle: 210 steps of 64 samples, 3.7 Hz.
constexpr std::uint64_t tremolo_step_samples = 64;
constexpr std::uint64_t tremolo_steps = 210;

/// The vibrato's: eight steps of 1,024 samples, 6.1 Hz.
constexpr std::uint64_t vibrato_step_samples = 1024;

/// The rhythm instruments' operators, and the bit of BD that keys each.
constexpr std::size_t bass_modulator = 12;
constexpr std::size_t hi_hat = 13;
constexpr std::size_t tom_tom = 14;
constexpr std::size_t bass_carrier = 15;
constexpr std::size_t snare = 16;
constexpr std::size_t cymbal = 17;

constexpr std::uint8_t hi_hat_bit = 0x01;
constexpr std::uint8_t cymbal_bit = 0x02;
constexpr std::uint8_t tom_tom_bit = 0x04;
constexpr std::uint8_t snare_bit = 0x08;
constexpr std::uint8_t bass_bit = 0x10;

constexpr std::size_t first_rhythm_channel = 6;

/// The channel an operator belongs to, and its modulator.
[[nodiscard]] constexpr std::size_t channel_of(std::size_t op) noexcept {
  return (op / 6) * 3 + (op % 3);
}
[[nodiscard]] constexpr std::size_t modulator_of(std::size_t ch) noexcept {
  return (ch / 3) * 6 + (ch % 3);
}

[[nodiscard]] constexpr std::uint32_t bit_of(std::uint32_t value,
                                             unsigned bit) noexcept {
  return (value >> bit) & 1U;
}

/// The drums a rhythm-mode operator answers to.
[[nodiscard]] constexpr std::uint8_t drum_of(std::size_t op) noexcept {
  switch (op) {
    case bass_modulator:
    case bass_carrier:
      return bass_bit;
    case hi_hat:
      return hi_hat_bit;
    case tom_tom:
      return tom_tom_bit;
    case snare:
      return snare_bit;
    case cymbal:
      return cymbal_bit;
    default:
      return 0;
  }
}

}  // namespace

void opl2_synth::reset() noexcept {
  ops_ = {};
  channels_ = {};
  waveforms_ = false;
  note_select_ = false;
  deep_tremolo_ = false;
  deep_vibrato_ = false;
  rhythm_ = false;
  drums_ = 0;
  counter_ = 0;
  noise_ = 1;
  tremolo_ = 0;
  vibrato_ = 0;
  output_ = 0;
  until_sample_ = opl2_ticks_per_sample;
}

void opl2_synth::write(std::uint8_t reg, std::uint8_t value) noexcept {
  if (reg == opl2_reg::test) {
    waveforms_ = (value & opl2_reg::waveform_enable) != 0;
    return;
  }
  if (reg == opl2_reg::keyboard_split) {
    note_select_ = (value & opl2_reg::note_select) != 0;
    return;
  }
  if (reg == opl2_reg::rhythm) {
    deep_tremolo_ = (value & 0x80U) != 0;
    deep_vibrato_ = (value & 0x40U) != 0;
    rhythm_ = (value & 0x20U) != 0;
    drums_ = static_cast<std::uint8_t>(value & 0x1FU);
    refresh_keys();
    return;
  }

  const std::uint8_t group = reg & 0xF0U;
  if (group == opl2_reg::frequency || group == opl2_reg::key_block ||
      group == opl2_reg::feedback) {
    const std::size_t ch = reg & 0x0FU;
    if (ch >= opl2_channels) {
      return;
    }
    channel& c = channels_[ch];
    if (group == opl2_reg::frequency) {
      c.fnum = static_cast<std::uint16_t>((c.fnum & 0x300U) | value);
    } else if (group == opl2_reg::key_block) {
      c.fnum = static_cast<std::uint16_t>((c.fnum & 0x0FFU) |
                                          ((value & 0x03U) << 8U));
      c.block = static_cast<std::uint8_t>((value >> 2U) & 0x07U);
      c.key = (value & 0x20U) != 0;
      refresh_keys();
    } else {
      c.feedback = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
      c.additive = (value & 0x01U) != 0;
    }
    return;
  }

  const std::size_t op = opl2_operator_at(reg & 0x1FU);
  if (op >= opl2_operators) {
    return;
  }
  oper& o = ops_[op];
  switch (reg & 0xE0U) {
    case opl2_reg::flags:
      o.tremolo = (value & 0x80U) != 0;
      o.vibrato = (value & 0x40U) != 0;
      o.sustaining = (value & 0x20U) != 0;
      o.scaled_rate = (value & 0x10U) != 0;
      o.multiple = static_cast<std::uint8_t>(value & 0x0FU);
      break;
    case opl2_reg::level:
      o.scale_level = static_cast<std::uint8_t>(value >> 6U);
      o.total_level = static_cast<std::uint8_t>(value & 0x3FU);
      break;
    case opl2_reg::attack_decay:
      o.attack_rate = static_cast<std::uint8_t>(value >> 4U);
      o.decay_rate = static_cast<std::uint8_t>(value & 0x0FU);
      break;
    case opl2_reg::sustain_release:
      o.sustain_level = static_cast<std::uint8_t>(value >> 4U);
      o.release_rate = static_cast<std::uint8_t>(value & 0x0FU);
      break;
    case opl2_reg::waveform:
      o.wave = static_cast<std::uint8_t>(value & 0x03U);
      break;
    default:
      break;
  }
}

std::uint8_t opl2_synth::rate_of(const oper& o, std::uint8_t reg_rate,
                                 const channel& c) const noexcept {
  if (reg_rate == 0) {
    return 0;
  }
  // The key code: the block and one bit of the F-number, which the
  // keyboard-split register chooses. A quarter of it unless the operator
  // asks for the whole.
  const unsigned split = note_select_ ? 8U : 9U;
  const unsigned key_code =
      (static_cast<unsigned>(c.block) << 1U) | bit_of(c.fnum, split);
  const unsigned scaling = o.scaled_rate ? key_code : key_code >> 2U;
  const unsigned rate = static_cast<unsigned>(reg_rate) * 4U + scaling;
  return static_cast<std::uint8_t>(rate > top_rate ? top_rate : rate);
}

std::uint32_t opl2_synth::envelope_steps(std::uint8_t rate) const noexcept {
  if (rate == 0) {
    return 0;
  }
  // `(4 + f) * 2^n / 32768` steps a sample, taken off the sample counter
  // so that the whole steps fall evenly and every operator at one rate
  // steps on the same samples.
  const std::uint64_t speed = static_cast<std::uint64_t>(4U + (rate & 3U))
                              << (rate >> 2U);
  return static_cast<std::uint32_t>((((counter_ + 1) * speed) >> 15U) -
                                    ((counter_ * speed) >> 15U));
}

void opl2_synth::set_key(std::size_t op, bool down) noexcept {
  oper& o = ops_[op];
  if (down == o.key) {
    return;
  }
  o.key = down;
  if (!down) {
    o.at = stage::release;
    return;
  }
  // A key going down starts the wave over and the envelope climbing; at
  // the top rate there is no climb.
  o.at = stage::attack;
  o.phase = 0;
  if (rate_of(o, o.attack_rate, channels_[channel_of(op)]) >= top_rate) {
    o.envelope = 0;
  }
}

void opl2_synth::refresh_keys() noexcept {
  for (std::size_t op = 0; op < opl2_operators; ++op) {
    const bool drum = rhythm_ && (drums_ & drum_of(op)) != 0;
    set_key(op, channels_[channel_of(op)].key || drum);
  }
}

void opl2_synth::advance_envelope(oper& o, const channel& c) noexcept {
  const auto fall = [&](std::uint8_t reg_rate) {
    const std::uint32_t next =
        o.envelope + envelope_steps(rate_of(o, reg_rate, c));
    o.envelope = static_cast<std::uint16_t>(
        next > silent_envelope ? silent_envelope : next);
  };

  switch (o.at) {
    case stage::attack: {
      if (o.envelope == 0) {
        o.at = stage::decay;
        break;
      }
      // An eighth of what is left, rounded up, for each step.
      const std::uint32_t steps = envelope_steps(rate_of(o, o.attack_rate, c));
      const std::uint32_t climb = ((o.envelope + 1U) * steps + 7U) >> 3U;
      o.envelope = static_cast<std::uint16_t>(
          climb >= o.envelope ? 0U : o.envelope - climb);
      if (o.envelope == 0) {
        o.at = stage::decay;
      }
      break;
    }
    case stage::decay: {
      // Three decibels a step of the register, and its last step is the
      // floor.
      const std::uint32_t level =
          o.sustain_level == 0x0F ? 0x1F0U : o.sustain_level * 16U;
      if (o.envelope >= level) {
        o.at = stage::sustain;
        break;
      }
      fall(o.decay_rate);
      break;
    }
    case stage::sustain:
      // Held while the key is, unless the operator is the percussive
      // kind, which goes on falling at its release rate.
      if (!o.sustaining) {
        fall(o.release_rate);
      }
      break;
    case stage::release:
      fall(o.release_rate);
      break;
  }
}

std::uint32_t opl2_synth::attenuation_of(const oper& o,
                                         const channel& c) const noexcept {
  std::uint32_t total =
      o.envelope + (static_cast<std::uint32_t>(o.total_level) << 2U);
  if (o.scale_level != 0) {
    const std::uint32_t by_fnum =
        static_cast<std::uint32_t>(scale_level_by_fnum[c.fnum >> 6U]) << 2U;
    const std::uint32_t by_block = (8U - c.block) << 5U;
    if (by_fnum > by_block) {
      total += (by_fnum - by_block) >> scale_level_shift[o.scale_level];
    }
  }
  if (o.tremolo) {
    total += tremolo_;
  }
  return total > silent_envelope ? silent_envelope : total;
}

std::uint32_t opl2_synth::phase_step(const oper& o,
                                     const channel& c) const noexcept {
  std::int32_t fnum = c.fnum;
  if (o.vibrato) {
    // The depth is the F-number's top three bits, so it is the same
    // fraction of any note; the shallow setting halves it.
    const std::int32_t depth = (c.fnum >> 7U) & 0x07;
    const std::int32_t swing = depth * vibrato_shape[vibrato_] / 2;
    fnum += deep_vibrato_ ? swing : swing / 2;
  }
  const std::uint32_t base =
      (static_cast<std::uint32_t>(fnum) << c.block) >> 1U;
  return (base * multiple_halves[o.multiple]) >> 1U;
}

std::int32_t opl2_synth::wave_of(const oper& o, std::uint32_t phase,
                                 std::uint32_t attenuation) const noexcept {
  const std::uint32_t at = phase & 0x3FFU;
  const std::uint8_t wave = waveforms_ ? o.wave : std::uint8_t{0};
  bool negative = (at & 0x200U) != 0;
  if (wave == 1 && negative) {
    return 0;  // The half sine: nothing where the sine is negative.
  }
  if (wave == 3 && (at & 0x100U) != 0) {
    return 0;  // The pulse sine: the rising quarter of each half only.
  }
  if (wave >= 2) {
    negative = false;  // Both of those and the rectified sine stay positive.
  }

  // A quarter wave, read backwards for the falling quarter.
  const std::uint32_t index = (at & 0x100U) != 0 ? (~at & 0xFFU) : (at & 0xFFU);
  const std::uint32_t total = opl2_log_sine[index] + (attenuation << 3U);
  const std::uint32_t octaves = total >> 8U;
  if (octaves >= 13) {
    return 0;
  }
  const std::uint32_t level =
      ((opl2_exponent[~total & 0xFFU] + 1024U) << 1U) >> octaves;
  const auto out = static_cast<std::int32_t>(level);
  return negative ? -out : out;
}

std::int32_t opl2_synth::melodic(std::size_t ch) noexcept {
  const channel& c = channels_[ch];
  oper& modulator = ops_[modulator_of(ch)];
  oper& carrier = ops_[modulator_of(ch) + 3];

  // The modulator hears the average of its own last two outputs.
  const std::int32_t fed =
      c.feedback == 0 ? 0
                      : (modulator.out + modulator.before) >> (9 - c.feedback);
  const std::int32_t modulated = wave_of(
      modulator, (modulator.phase >> 9U) + static_cast<std::uint32_t>(fed),
      attenuation_of(modulator, c));
  modulator.before = modulator.out;
  modulator.out = modulated;

  const std::uint32_t carrier_phase = carrier.phase >> 9U;
  const std::int32_t sum =
      c.additive
          ? modulated +
                wave_of(carrier, carrier_phase, attenuation_of(carrier, c))
          : wave_of(carrier,
                    carrier_phase + static_cast<std::uint32_t>(modulated),
                    attenuation_of(carrier, c));

  modulator.phase += phase_step(modulator, c);
  carrier.phase += phase_step(carrier, c);
  return sum;
}

std::int32_t opl2_synth::rhythm() noexcept {
  const channel& bass = channels_[first_rhythm_channel];
  const channel& upper = channels_[first_rhythm_channel + 1];
  const channel& lower = channels_[first_rhythm_channel + 2];

  // The bass drum is the channel it replaces, except that in additive
  // mode only its carrier is heard.
  oper& bm = ops_[bass_modulator];
  oper& bc = ops_[bass_carrier];
  const std::int32_t fed =
      bass.feedback == 0 ? 0 : (bm.out + bm.before) >> (9 - bass.feedback);
  const std::int32_t modulated =
      wave_of(bm, (bm.phase >> 9U) + static_cast<std::uint32_t>(fed),
              attenuation_of(bm, bass));
  bm.before = bm.out;
  bm.out = modulated;
  const std::uint32_t bass_phase =
      (bc.phase >> 9U) +
      (bass.additive ? 0U : static_cast<std::uint32_t>(modulated));
  std::int32_t sum = wave_of(bc, bass_phase, attenuation_of(bc, bass));

  // The hi-hat, the snare and the cymbal have no phase of their own:
  // each is made of bits of the hi-hat's and the cymbal's operators'
  // phases and of the noise.
  oper& hh = ops_[hi_hat];
  oper& sd = ops_[snare];
  oper& tom = ops_[tom_tom];
  oper& cy = ops_[cymbal];
  const std::uint32_t hat = hh.phase >> 9U;
  const std::uint32_t top = cy.phase >> 9U;
  const std::uint32_t noise = noise_ & 1U;
  const std::uint32_t mix = (bit_of(hat, 2) ^ bit_of(hat, 7)) |
                            (bit_of(hat, 3) ^ bit_of(top, 5)) |
                            (bit_of(top, 3) ^ bit_of(top, 5));
  const std::uint32_t hat_phase =
      (mix << 9U) | ((mix ^ noise) != 0 ? 0xD0U : 0x34U);
  const std::uint32_t snare_phase =
      (bit_of(hat, 8) << 9U) | ((bit_of(hat, 8) ^ noise) << 8U);
  const std::uint32_t cymbal_phase = (mix << 9U) | 0x80U;

  sum += wave_of(hh, hat_phase, attenuation_of(hh, upper));
  sum += wave_of(sd, snare_phase, attenuation_of(sd, upper));
  sum += wave_of(tom, tom.phase >> 9U, attenuation_of(tom, lower));
  sum += wave_of(cy, cymbal_phase, attenuation_of(cy, lower));

  bm.phase += phase_step(bm, bass);
  bc.phase += phase_step(bc, bass);
  hh.phase += phase_step(hh, upper);
  sd.phase += phase_step(sd, upper);
  tom.phase += phase_step(tom, lower);
  cy.phase += phase_step(cy, lower);

  // The rhythm section is twice as loud as a melodic channel.
  return sum * 2;
}

bool opl2_synth::silent() const noexcept {
  for (const oper& o : ops_) {
    if (o.envelope != silent_envelope) {
      return false;
    }
    // About to sound, unless its attack rate is zero and it never will.
    if (o.at == stage::attack && o.attack_rate != 0) {
      return false;
    }
  }
  return true;
}

void opl2_synth::sample() noexcept {
  // The noise and the two oscillators run whether or not anything
  // sounds.
  const std::uint32_t fed_back = (noise_ ^ (noise_ >> 14U)) & 1U;
  noise_ = (noise_ >> 1U) | (fed_back << 22U);

  const std::uint64_t swing = (counter_ / tremolo_step_samples) % tremolo_steps;
  const auto triangle = static_cast<std::uint32_t>(
      swing < tremolo_steps / 2 ? swing : tremolo_steps - swing);
  tremolo_ = deep_tremolo_ ? triangle >> 2U : triangle >> 4U;
  vibrato_ = static_cast<std::uint32_t>((counter_ / vibrato_step_samples) % 8);

  if (silent()) {
    // Nothing to compute, and the answer is exact.
    output_ = 0;
    ++counter_;
    return;
  }

  for (std::size_t op = 0; op < opl2_operators; ++op) {
    advance_envelope(ops_[op], channels_[channel_of(op)]);
  }

  std::int32_t sum = 0;
  const std::size_t melodic_channels =
      rhythm_ ? first_rhythm_channel : opl2_channels;
  for (std::size_t ch = 0; ch < melodic_channels; ++ch) {
    sum += melodic(ch);
  }
  if (rhythm_) {
    sum += rhythm();
  }

  if (sum > opl2_full_scale) {
    sum = opl2_full_scale;
  } else if (sum < -opl2_full_scale) {
    sum = -opl2_full_scale;
  }
  output_ = sum;
  ++counter_;
}

double opl2_synth::run(ticks span) noexcept {
  // The output only changes at a sample, so it is held across each run
  // of ticks up to the next one.
  std::uint64_t left = span;
  double total = 0.0;
  while (left > 0) {
    const std::uint64_t held =
        left < until_sample_ ? left : std::uint64_t{until_sample_};
    total += static_cast<double>(output_) * static_cast<double>(held);
    left -= held;
    until_sample_ -= static_cast<std::uint32_t>(held);
    if (until_sample_ == 0) {
      sample();
      until_sample_ = opl2_ticks_per_sample;
    }
  }
  return total;
}

}  // namespace amberfolio::machine
