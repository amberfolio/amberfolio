// SPDX-License-Identifier: AGPL-3.0-only
//
// The rows of a journal edition kept in a unit of their own, so that the
// table in journal_facts.cpp can name them: journal_facts.cpp with them
// in it would be over the content guard's 256 KiB cap
// (scripts/check-clean.sh). Private to hosts/common; journal_facts.h is
// the interface.

#pragma once

#include <array>

#include "amberfolio/host/journal_facts.h"

namespace amberfolio::host::journal_rows {

/// Secret of the Silver Blades' GOG journal, its seventy-two entries
/// (#633): journal_facts_secret_gog.cpp has how they were measured.
extern const std::array<journal_entry_fact, 72> secret_gog_entries;

}  // namespace amberfolio::host::journal_rows
