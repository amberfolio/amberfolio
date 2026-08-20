// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/trace.h"

#include <cstddef>

namespace amberfolio::machine {

void trace_ring::clear() noexcept {
  // The counters are the whole of what "kept" means — `step_count()` and
  // `step_at()` both derive from them — so zeroing them is the clear.
  // The arrays keep whatever they held; nothing can read them, and
  // blanking two kilobytes to prove it would only cost time.
  steps_seen_ = 0;
  calls_seen_ = 0;
}

void trace_ring::record(trace_step where) noexcept {
  if (!recording_) {
    return;
  }
  steps_[static_cast<std::size_t>(steps_seen_ % step_capacity)] = where;
  ++steps_seen_;
}

void trace_ring::record(const service_call& call) noexcept {
  if (!recording_) {
    return;
  }
  calls_[static_cast<std::size_t>(calls_seen_ % call_capacity)] = call;
  ++calls_seen_;
}

trace_step trace_ring::step_at(std::size_t index) const noexcept {
  // The oldest kept entry is `steps_seen_ - step_count()` in absolute
  // terms; the ring position is that, modulo the capacity. Doing the
  // arithmetic in absolute step numbers rather than in ring positions is
  // what keeps the not-yet-wrapped case from needing its own branch.
  const std::uint64_t oldest = steps_seen_ - step_count();
  return steps_[static_cast<std::size_t>((oldest + index) % step_capacity)];
}

service_call trace_ring::call_at(std::size_t index) const noexcept {
  const std::uint64_t oldest = calls_seen_ - call_count();
  return calls_[static_cast<std::size_t>((oldest + index) % call_capacity)];
}

}  // namespace amberfolio::machine
