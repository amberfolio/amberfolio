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
  //
  // The one guarded block in the project, and the rule above is why: left
  // to itself clang-format packs a list of short string literals as many
  // to a line as fit, which is precisely the layout the one-line-per-stem
  // rule exists to prevent. A trailing comma does not hold it — this was
  // measured, not assumed. So the formatter is told to keep its hands off
  // the list, and only the list.
  static const std::set<std::string_view, std::less<>> enabled = {
      // --- One line per stem, sorted. ---------------------------------
      "00",
      "01",
      "02",
      "03",
      "04",
      "05",
      "10",
      "11",
      "12",
      "13",
      "14",
      "15",
      "80.0",
      "80.2",
      "81.0",
      "81.2",
      "82.0",
      "82.2",
      "83.0",
      "83.2",
      "86",
      "87",
      "88",
      "89",
      "8A",
      "8B",
      "8C",
      "8E",
      "90",
      "91",
      "92",
      "93",
      "94",
      "95",
      "96",
      "97",
      "A0",
      "A1",
      "A2",
      "A3",
      "A4",
      "A5",
      "A6",
      "A7",
      "AA",
      "AB",
      "AC",
      "AD",
      "AE",
      "AF",
      "B0",
      "B1",
      "B2",
      "B3",
      "B4",
      "B5",
      "B6",
      "B7",
      "B8",
      "B9",
      "BA",
      "BB",
      "BC",
      "BD",
      "BE",
      "BF",
      "C6",
      "C7",
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
