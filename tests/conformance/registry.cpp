// SPDX-License-Identifier: AGPL-3.0-only
//
//
//                     HOW TO ENABLE A VECTOR FILE
//                     ===========================
//
// The same rule as the dispatch tables in core/src/cpu/dispatch.cpp, and
// for the same reason — sixteen instruction families are implemented in
// parallel, and this file is one of the two things all of them touch:
//
//     ONE LINE PER STEM, sorted, with the stem in it.
//
// Not a range, not a loop, not a helper that enables 00 through 03. Two
// families adding adjacent stems then produce a conflict git resolves by
// keeping both lines rather than one a human has to think about.
//
// A stem belongs here once its family's instructions are implemented and
// its file passes in full. Until then, leaving it out is not a gap being
// tolerated: the case still registers, still shows up in CTest, and
// reports SKIPPED — which is what makes the milestone's remaining work
// visible in every run. Adding a stem that does not pass turns the build
// red, which is the point.

#include "registry.h"

#include <set>
#include <span>
#include <string>
#include <string_view>

#include "vector_stems.h"

namespace amberfolio::conformance {

std::span<const std::string_view> all_stems() noexcept { return vector_stems; }

bool stem_is_enabled(std::string_view stem) {
  // clang-format off
  static const std::set<std::string_view, std::less<>> enabled = {
      // --- One line per stem, sorted. ---------------------------------
      "18",
      "19",
      "1A",
      "1B",
      "1C",
      "1D",
      "28",
      "29",
      "2A",
      "2B",
      "2C",
      "2D",
      "38",
      "39",
      "3A",
      "3B",
      "3C",
      "3D",
      "80.3",
      "80.5",
      "80.7",
      "81.3",
      "81.5",
      "81.7",
      "82.3",
      "82.5",
      "82.7",
      "83.3",
      "83.5",
      "83.7",
      "F6.3",
      "F7.3",
  };
  // clang-format on
  return enabled.contains(stem);
}

std::string case_name(std::string_view stem) {
  std::string name = "op_";
  name += stem;
  for (char& c : name) {
    if (c == '.') {
      c = '_';
    }
  }
  return name;
}

}  // namespace amberfolio::conformance
