// SPDX-License-Identifier: AGPL-3.0-only
//
// The stem list, and the CTest case names built from it (issues #14,
// #34).
//
// This file used to carry a second list: one line per *enabled* stem,
// sorted, the one thing all sixteen parallel family branches touched. A
// stem outside it still registered as a case and reported SKIPPED, which
// is what made M1's remaining work visible in every run.
//
// That list is gone with M1's closeout. Every file of the pin is
// expected to pass, so there is nothing left to be in or out of — which
// is the point: an allowlist that happens to hold all 323 stems and one
// that is required to are the same suite today and different suites the
// moment somebody deletes a line. The count is checked against the pin
// at configure time (tests/CMakeLists.txt), so shrinking the suite now
// means failing the build rather than passing a smaller one.

#include "registry.h"

#include <span>
#include <string>
#include <string_view>

#include "vector_stems.h"

namespace amberfolio::conformance {

std::span<const std::string_view> all_stems() noexcept { return vector_stems; }

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
