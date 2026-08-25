// SPDX-License-Identifier: AGPL-3.0-only
//
// The two services, served. host_services.h has the reasoning; this is
// the whole of what M5-D1 (#169) asked for above it.

#include "amberfolio/host/host_services.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::host {

void host_services::serve(machine::machine& box,
                          machine::seam_host_service which,
                          std::uint32_t argument) {
  // The one read, and the reason this runs where it runs: the machine's
  // own virtual clock, at the instant the seam called out. Everything
  // else a consumer will want — the party's position, the entry a
  // journal point named — is read the same way and by the enhancement
  // that wants it (#173, #175).
  host_service_record& seen = records_[static_cast<std::size_t>(which)];
  seen.seen = true;
  seen.argument = argument;
  seen.at = box.time();
}

}  // namespace amberfolio::host
