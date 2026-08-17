// SPDX-License-Identifier: AGPL-3.0-only
//
// Which vector files exist, and which of them this build is expected to
// pass (issue #14).
//
// Two lists, and they are deliberately different things. The first is
// every file the pinned suite has, generated from
// tests/conformance/vector-files.txt — it never changes while the pin
// holds, and it is what makes an unimplemented instruction family show up
// in CTest as a skipped case instead of as nothing at all. The second is
// the enabled set in registry.cpp, which starts empty and grows by one
// line per file as the wide phase lands.

#pragma once

#include <span>
#include <string>
#include <string_view>

namespace amberfolio::conformance {

/// Every vector file of the pinned suite, by stem ("00", "80.0", ...).
[[nodiscard]] std::span<const std::string_view> all_stems() noexcept;

/// Whether this build claims to pass `stem`. A stem that is not enabled
/// still registers as a CTest case; it skips rather than running.
[[nodiscard]] bool stem_is_enabled(std::string_view stem);

/// The CTest case name for a stem: "80.0" becomes "op_80_0", because a
/// dot in a GoogleTest name is the separator between suite and case.
[[nodiscard]] std::string case_name(std::string_view stem);

}  // namespace amberfolio::conformance
