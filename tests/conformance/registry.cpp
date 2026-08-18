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
      "08",
      "09",
      "0A",
      "0B",
      "0C",
      "0D",
      "10",
      "11",
      "12",
      "13",
      "14",
      "15",
      "18",
      "19",
      "1A",
      "1B",
      "1C",
      "1D",
      "20",
      "21",
      "22",
      "23",
      "24",
      "25",
      "28",
      "29",
      "2A",
      "2B",
      "2C",
      "2D",
      "30",
      "31",
      "32",
      "33",
      "34",
      "35",
      "38",
      "39",
      "3A",
      "3B",
      "3C",
      "3D",
      "80.0",
      "80.1",
      "80.2",
      "80.3",
      "80.4",
      "80.5",
      "80.6",
      "80.7",
      "81.0",
      "81.1",
      "81.2",
      "81.3",
      "81.4",
      "81.5",
      "81.6",
      "81.7",
      "82.0",
      "82.1",
      "82.2",
      "82.3",
      "82.4",
      "82.5",
      "82.6",
      "82.7",
      "83.0",
      "83.1",
      "83.2",
      "83.3",
      "83.4",
      "83.5",
      "83.6",
      "83.7",
      "84",
      "85",
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
      "9A",
      "A0",
      "A1",
      "A2",
      "A3",
      "A4",
      "A5",
      "A6",
      "A7",
      "A8",
      "A9",
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
      "C0",
      "C1",
      "C2",
      "C3",
      "C6",
      "C7",
      "C8",
      "C9",
      "CA",
      "CB",
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
      "E8",
      "E9",
      "EA",
      "EB",
      "F6.0",
      "F6.1",
      "F6.2",
      "F6.3",
      "F6.6",
      "F6.7",
      "F7.0",
      "F7.1",
      "F7.2",
      "F7.3",
      "F7.6",
      "F7.7",
      "FF.2",
      "FF.3",
      "FF.4",
      "FF.5",
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
