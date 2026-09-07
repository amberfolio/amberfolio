// SPDX-License-Identifier: AGPL-3.0-only
//
// press_spec.h's parser.

#include "press_spec.h"

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>

namespace amberfolio::sdl {

bool parse_press(std::string_view spec, scripted_press& out) {
  const std::size_t at = spec.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == spec.size()) {
    return false;
  }
  std::string_view digits = spec.substr(at + 1);

  press_edges edges = press_edges::both;
  const std::size_t colon = digits.find(':');
  if (colon != std::string_view::npos) {
    const std::string_view suffix = digits.substr(colon + 1);
    if (suffix == "down") {
      edges = press_edges::down;
    } else if (suffix == "up") {
      edges = press_edges::up;
    } else {
      return false;
    }
    digits = digits.substr(0, colon);
  }

  std::uint64_t frame = 0;
  // Both ends named before the call. `from_chars` is given the length —
  // that is what the second pointer is — but clang-tidy reads a bare
  // `.data()` in an argument list as a string handed over without one,
  // and it is right to, often enough that hoisting is cheaper than an
  // exemption.
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed = std::from_chars(first, last, frame);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.key = std::string(spec.substr(0, at));
  out.frame = frame;
  out.edges = edges;
  return true;
}

}  // namespace amberfolio::sdl
