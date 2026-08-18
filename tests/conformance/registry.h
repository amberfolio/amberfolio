// SPDX-License-Identifier: AGPL-3.0-only
//
// Which vector files the pinned suite has — all of which this build is
// expected to pass (issues #14, #34).
//
// One list, and it is not a choice: it is generated at configure time
// from tests/conformance/vector-files.txt, which is every file the pinned
// commit contains. There was a second list here during M1's wide phase,
// the enabled set in registry.cpp, and it grew by one line per file as
// the sixteen family issues landed. M1 is complete, so it is gone and
// every stem runs.

#pragma once

#include <span>
#include <string>
#include <string_view>

namespace amberfolio::conformance {

/// Every vector file of the pinned suite, by stem ("00", "80.0", ...).
[[nodiscard]] std::span<const std::string_view> all_stems() noexcept;

/// The CTest case name for a stem: "80.0" becomes "op_80_0", because a
/// dot in a GoogleTest name is the separator between suite and case.
[[nodiscard]] std::string case_name(std::string_view stem);

}  // namespace amberfolio::conformance
