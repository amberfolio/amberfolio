// SPDX-License-Identifier: AGPL-3.0-only
//
// Recording a run, replaying one, and the date a run is told it is (#100,
// #320, #472, split from main.cpp): the host's half of machine/replay.h,
// whose header is the argument for all of it.

#pragma once

#include <fstream>
#include <string>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/vfs.h"
#include "options.h"

namespace amberfolio::sdl {

/// `--record`'s file. Every line of the stream goes through `line()`, so that
/// a run without `--record` pays one branch, and so that this host's spelling
/// of a line and the player's stay the one spelling in replay.h.
class recorder {
 public:
  /// Open the file and write the preamble now, before SDL is even up. True
  /// when there is nothing to record or the recording is open; false with a
  /// sentence said when it cannot be made.
  [[nodiscard]] bool open(const options& opts, machine::machine& box,
                          machine::filesystem& files);

  [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }

  /// One line of the stream, written if the file is open.
  void line(const machine::replay_event& event);

  /// Where the recording stops, written before SDL comes down so that a run
  /// whose teardown goes wrong still leaves a recording saying how far it
  /// got. `checkpointed` is the tick of the last checkpoint written.
  void end_run(const machine::machine& box, machine::ticks checkpointed);

 private:
  std::ofstream file_;
};

/// `--replay`: load the recording and become the run it describes. The
/// recording decides the speed, the seams and every key; this applies the
/// first two and checks the initial conditions. False, with a sentence said,
/// when the run cannot be that machine.
[[nodiscard]] bool load_replay(const options& opts, machine::machine& box,
                               machine::filesystem& files,
                               std::string& replay_text,
                               machine::replay_player& player);

/// Seed the machine's date from `--wall`, in the same breath as the line
/// that records it, unless a replay is supplying its own.
void seed_wall_clock(const options& opts, machine::machine& box, bool replaying,
                     recorder& recording);

}  // namespace amberfolio::sdl
