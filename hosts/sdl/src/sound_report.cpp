// SPDX-License-Identifier: AGPL-3.0-only

#include "sound_report.h"

#include <cstdint>
#include <optional>
#include <string>

#include "amberfolio/machine/launcher_view.h"

namespace amberfolio::sdl {

std::string started_sound_line(
    std::optional<machine::configured_sound> configured) {
  if (!configured || configured->letter == configured->started) {
    return {};
  }
  std::string line = "amberfolio: ";
  line += configured->file;
  line += " sound ";
  line.push_back(static_cast<char>(configured->letter));
  line += ", started as ";
  line.push_back(static_cast<char>(configured->started));
  switch (configured->started) {
    case machine::configured_sound_adlib:
      line += " (AdLib)\n";
      break;
    case machine::configured_sound_speaker:
      line += " (PC speaker)\n";
      break;
    case machine::configured_sound_silent:
      line += " (silent)\n";
      break;
    default:
      line += " (Tandy sound)\n";
      break;
  }
  return line;
}

std::string chip_traffic_line(std::uint64_t writes, std::uint64_t dropped) {
  if (writes == 0 && dropped == 0) {
    return {};
  }
  return "amberfolio: tandy writes=" + std::to_string(writes) +
         " dropped=" + std::to_string(dropped) + "\n";
}

std::string card_traffic_line(std::uint64_t writes, std::uint64_t dropped) {
  if (writes == 0 && dropped == 0) {
    return {};
  }
  return "amberfolio: adlib writes=" + std::to_string(writes) +
         " dropped=" + std::to_string(dropped) + "\n";
}

}  // namespace amberfolio::sdl
