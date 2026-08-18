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
      "D0.0",
      "D0.1",
      "D0.2",
      "D0.3",
      "D0.4",
      "D0.5",
      "D0.6",
      "D0.7",
      "D1.0",
      "D1.1",
      "D1.2",
      "D1.3",
      "D1.4",
      "D1.5",
      "D1.6",
      "D1.7",
      "D2.0",
      "D2.1",
      "D2.2",
      "D2.3",
      "D2.4",
      "D2.5",
      "D2.6",
      "D2.7",
      "D3.0",
      "D3.1",
      "D3.2",
      "D3.3",
      "D3.4",
      "D3.5",
      "D3.6",
      "D3.7",
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
