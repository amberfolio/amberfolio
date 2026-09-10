// SPDX-License-Identifier: AGPL-3.0-only
//
// The list of places an OCR engine could be. `ocr_discovery.h` has the
// order and the argument for it.

#include "ocr_discovery.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace amberfolio::sdl {
namespace {

/// `directory/executable`, spelled the way this platform spells a path —
/// `std::filesystem` rather than a separator of our own, because the
/// string this returns is one a player will read in a report and one
/// this host will hand to a shell.
void add(std::vector<std::string>& into, std::string_view directory,
         std::string_view executable) {
  if (directory.empty()) {
    return;
  }
  std::string where = (std::filesystem::path(directory) / executable)
                          .lexically_normal()
                          .string();
  if (std::ranges::find(into, where) == into.end()) {
    into.push_back(std::move(where));
  }
}

}  // namespace

std::vector<std::string> ocr_candidates(std::string_view beside,
                                        std::string_view path_variable,
                                        char separator,
                                        std::string_view executable) {
  std::vector<std::string> places;
  if (executable.empty()) {
    return places;
  }
  add(places, beside, executable);
  std::size_t at = 0;
  while (at <= path_variable.size()) {
    const std::size_t end = path_variable.find(separator, at);
    const std::string_view entry = path_variable.substr(
        at, end == std::string_view::npos ? std::string_view::npos : end - at);
    add(places, entry, executable);
    if (end == std::string_view::npos) {
      break;
    }
    at = end + 1;
  }
  return places;
}

}  // namespace amberfolio::sdl
