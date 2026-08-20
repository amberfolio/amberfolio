// SPDX-License-Identifier: AGPL-3.0-only
//
// What a run produced, written where a person can look at it: the frame
// as a PPM, the speaker as a WAV.
//
// `--dump` on the boot driver (M3-F1, #83) is the generalization of the
// tool `tests/programs/dump_main.cpp` already is for self-written
// programs. The reason it has to exist at the *host* is the reason
// `docs/machine.md` §7 gives for distrusting goldens: "the title renders"
// is a claim to look at, and the only run that can produce the title is a
// run of the player's own copy, which no test in this repository will
// ever make. So the host has to be able to hand a frame over.
//
// The two writers are duplicated between here and that tool rather than
// shared, deliberately and at a cost of about fifty lines. Sharing them
// would mean either the host linking the test rig — a dependency pointing
// exactly the wrong way — or a third library existing for two callers of
// a file format that has not changed since 1991. Both formats are a
// header and then the samples; if one of these two copies is ever wrong,
// the file will not open.

#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {

/// Write `pixels` (palette indices, `machine::frame_pixels` of them)
/// through `palette` as a binary PPM. False if the buffers are the wrong
/// size or the file could not be written.
///
/// True colour, palette already applied, so what lands on disk is what
/// the host would have put on the screen — including the EGA's DAC,
/// which is the part of the picture worth being able to check.
[[nodiscard]] bool write_ppm(const std::filesystem::path& where,
                             std::span<const std::uint8_t> pixels,
                             std::span<const machine::rgb> palette);

/// Write `samples` as a 16-bit mono WAV at `sample_rate`. False if there
/// are no samples or the file could not be written.
///
/// The rate is the rate they were pulled at and is not a choice: a header
/// naming any other number leaves a file that plays the machine's tone at
/// the wrong pitch and says nothing about the machine.
[[nodiscard]] bool write_wav(const std::filesystem::path& where,
                             std::span<const float> samples,
                             unsigned sample_rate);

}  // namespace amberfolio::sdl
