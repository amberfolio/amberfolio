// SPDX-License-Identifier: AGPL-3.0-only

#include "sound_report.h"

#include <cstdint>
#include <optional>
#include <string>

#include "amberfolio/machine/launcher_view.h"

namespace amberfolio::sdl {

std::string started_sound_line(std::optional<std::uint8_t> configured) {
  if (!configured || *configured == machine::sound_for_this_machine) {
    return {};
  }
  std::string line = "amberfolio: POOL.CFG sound ";
  line.push_back(static_cast<char>(*configured));
  line += ", started as ";
  line.push_back(static_cast<char>(machine::sound_for_this_machine));
  line += " (Tandy sound)\n";
  return line;
}

std::string chip_traffic_line(std::uint64_t writes, std::uint64_t dropped) {
  if (writes == 0 && dropped == 0) {
    return {};
  }
  return "amberfolio: tandy writes=" + std::to_string(writes) +
         " dropped=" + std::to_string(dropped) + "\n";
}

}  // namespace amberfolio::sdl
