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
      "60",
      "61",
      "62",
      "63",
      "64",
      "65",
      "66",
      "67",
      "68",
      "69",
      "6A",
      "6B",
      "6C",
      "6D",
      "6E",
      "6F",
      "70",
      "71",
      "72",
      "73",
      "74",
      "75",
      "76",
      "77",
      "78",
      "79",
      "7A",
      "7B",
      "7C",
      "7D",
      "7E",
      "7F",
      "E0",
      "E1",
      "E2",
      "E3",
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
