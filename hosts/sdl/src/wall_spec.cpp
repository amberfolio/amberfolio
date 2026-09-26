// SPDX-License-Identifier: AGPL-3.0-only

#include "wall_spec.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {

bool parse_wall(std::string_view spec, machine::wall_time& out) {
  const auto number = [spec](std::size_t at, std::size_t width,
                             unsigned& value) -> bool {
    if (at + width > spec.size()) {
      return false;
    }
    value = 0;
    for (std::size_t i = at; i < at + width; ++i) {
      if (spec[i] < '0' || spec[i] > '9') {
        return false;
      }
      value = (value * 10) + static_cast<unsigned>(spec[i] - '0');
    }
    return true;
  };

  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  unsigned centisecond = 0;

  if (spec.size() < 10 || !number(0, 4, year) || spec[4] != '-' ||
      !number(5, 2, month) || spec[7] != '-' || !number(8, 2, day)) {
    return false;
  }
  std::size_t at = 10;
  if (at != spec.size()) {
    // A time, which is `THH:MM` at the least. Midnight is what a bare
    // date means, and that is a real answer rather than a rounding: a
    // person naming a day for a reproducible run is naming its start.
    if (spec[at] != 'T' || !number(at + 1, 2, hour) || at + 3 >= spec.size() ||
        spec[at + 3] != ':' || !number(at + 4, 2, minute)) {
      return false;
    }
    at += 6;
    if (at != spec.size() && spec[at] == ':') {
      if (!number(at + 1, 2, second)) {
        return false;
      }
      at += 3;
      if (at != spec.size() && spec[at] == '.') {
        if (!number(at + 1, 2, centisecond)) {
          return false;
        }
        at += 3;
      }
    }
    if (at != spec.size()) {
      return false;
    }
  }

  out = machine::wall_time{
      .year = static_cast<std::uint16_t>(year),
      .month = static_cast<std::uint8_t>(month),
      .day = static_cast<std::uint8_t>(day),
      .hour = static_cast<std::uint8_t>(hour),
      .minute = static_cast<std::uint8_t>(minute),
      .second = static_cast<std::uint8_t>(second),
      .centisecond = static_cast<std::uint8_t>(centisecond),
  };
  machine::wall_clock probe;
  return probe.set(out, 0);
}

}  // namespace amberfolio::sdl
