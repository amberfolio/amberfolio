// SPDX-License-Identifier: AGPL-3.0-only
//
// `--dump`'s edge list, written as the run makes it (M4-A1, #106, #472).
//
//
// `--dump`'s third file. The PPM is the frame the machine composed and
// the WAV is one *rendering* of the sound it made; this is the sound
// itself, in the units the machine works in — "at tick T the speaker
// output became high" — which platform.h calls the canonical audio
// state and which the WAV's floats explicitly are not. #106 asks the
// two questions separately, and until now only one of them could be:
// whether the machine made the right edges at the right ticks is
// answered by this file, and whether the box filter renders them right
// is answered by the WAV beside it.
//
// Streamed rather than kept and written at the end, because core's log
// is a bounded drain-per-frame ring with no allocator behind it
// (platform.h): the run loop empties it every frame, so this file is
// the whole run's list and the machine never has to hold it. A run that
// ends badly then leaves the edges it had already made, which is more
// than a buffer in a dead process would.
//
// Empty the machine's edge log into that file between slices, on the
// machine thread, where the log's producer side lives: nothing here is
// visible to the audio thread's `render()`, and a reader that perturbed
// what `render()` saw would be measuring itself.

#pragma once

#include <cstdint>
#include <fstream>
#include <string>

#include "amberfolio/machine/machine.h"
#include "options.h"

namespace amberfolio::sdl {

class edge_dump {
 public:
  /// Open `PREFIX.edges` and turn the machine's edge log on, when `--dump`
  /// was given. A file that will not open is a line and a run without one.
  void open(const options& opts, machine::machine& box);

  [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }

  /// Empty the machine's edge log into the file, if there is one.
  void drain(machine::machine& box);

  /// The trailer, and the line saying how many edges there were. The count
  /// is on the last line as well as in the report so that the file answers
  /// "is this all of it?" on its own.
  void finish(const machine::machine& box, const std::string& prefix);

 private:
  std::ofstream file_;
  std::uint64_t written_{0};
};

}  // namespace amberfolio::sdl
