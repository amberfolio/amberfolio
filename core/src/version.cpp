// SPDX-License-Identifier: AGPL-3.0-only
//
// The M0 placeholder translation unit — what makes amberfolio-core a
// compiled library rather than a header-only one. The machine arrives in M1.

#include "amberfolio/version.h"

namespace amberfolio {

version linked_version() noexcept { return core_version; }

}  // namespace amberfolio
