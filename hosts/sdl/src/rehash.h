// SPDX-License-Identifier: AGPL-3.0-only
//
// `--rehash FILE` (#404): a recording written back with this build's
// checkpoint hashes.
//
// The player does the run (`replay_player::set_rehash`): every input at
// its tick, every checkpoint at its step count and its stop, the
// machine's own hash taken where the recording's used to be compared.
// This is the text half: the recording as it was, with its header saying
// this build's state version and each `checkpoint` line, in order,
// replaced by the one the run took there. Every other line is the
// recording's byte for byte, so a diff of the two files is the hashes and
// nothing else. `docs/replay.md` §7 says when and why.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/replay.h"

namespace amberfolio::sdl {

/// The host's side of a re-hash run: `player.apply()`, keeping the line
/// each checkpoint becomes, in order. Called wherever a replaying host
/// would call `apply()`; a player that is only verifying keeps nothing.
class rehash_collector {
 public:
  void apply(machine::replay_player& player, machine::machine& box);

  [[nodiscard]] const std::vector<std::string>& lines() const noexcept {
    return lines_;
  }

 private:
  std::vector<std::string> lines_;
};

/// `text` re-hashed, or `std::nullopt` and `why` set when the recording
/// has no header to rewrite, or its checkpoint lines and `checkpoints`
/// do not pair off one for one.
[[nodiscard]] std::optional<std::string> rehash_text(
    std::string_view text, const std::vector<std::string>& checkpoints,
    std::string_view& why);

/// `rehash_text()` written to `path`, with a line on stderr either way.
[[nodiscard]] bool write_rehashed(const std::string& path,
                                  std::string_view text,
                                  const std::vector<std::string>& checkpoints);

}  // namespace amberfolio::sdl
