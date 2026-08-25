// SPDX-License-Identifier: AGPL-3.0-only
//
// The `seam_host_services` both hosts attach: M5-D1's door (#169).
//
// `machine/seam.h` names two services a seam may call out to —
// `journal_open` and `automap_update` — and until this file existed
// nothing implemented them. `seam_context::call_host()` answered false on
// every machine in this tree, which was M4's intended state and is the
// first thing M5 changes.
//
//
// Why one object for both hosts, and why it is C++
// -----------------------------------------------
//
// A callout is handed the machine, and what it reads there is only true
// at the moment of the call. The automap wants the party's position
// *now*; a page that drained a queue of pending callouts on its next
// turn would be reading a machine that has moved on, and could not even
// say by how much. So the implementation runs inside the module, in C++,
// synchronously — on the desktop and in the browser alike — and the JS
// side of the web host learns what happened by *asking* (the polled
// counts in `machine/seam.h`, through `af_machine_seam_host_calls`)
// rather than by being pushed at.
//
// That leaves the two hosts wanting the identical object, so it is
// written once here rather than twice. `hosts/sdl` and `hosts/web` both
// link `amberfolio::host_services`; PLAN.md §4's core/host split is kept
// because this is above the boundary — it *reads* the machine and never
// writes it, which is exactly what a host is allowed (PLAN.md §4, and
// `seam_host_services::serve()`'s own contract).
//
//
// What it does, and what it deliberately does not do yet
// -----------------------------------------------------
//
// M5-D1 is a door, not a consumer. The journal reader (#175) and the
// automap panel (#173, with the explored overlay #179 beside it) are the
// enhancements that will do something with these calls, and each brings
// its own state and its own drawing. What this object does today is the
// part that is the *door*: it takes the call, it reads the machine at
// the moment of the call, and it remembers what it saw, so that a host
// can say afterwards that the callout arrived and when.
//
// The virtual time is the fact worth keeping, and the one only a
// synchronous implementation can have: `machine::time()` at the instant
// the seam called out. A host prints it; a test asserts it against the
// tick the point was reached at. Nothing else here invents a consumer
// that does not exist — an enhancement that needs state adds it in its
// own issue, deriving from this or holding one.
//
// **Counting is not here.** `seam_engine` counts served calls and keeps
// the last argument (`seam.h`'s `host_calls`/`host_argument`), because
// that is the engine's record of what it routed and the ABI's door onto
// it. Keeping a second count here would be two numbers that can
// disagree.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::host {

/// What one service has been asked, as this object saw it.
struct host_service_record {
  /// Whether it has ever been called.
  bool seen{false};
  /// The argument of the most recent call.
  std::uint32_t argument{};
  /// `machine::time()` at the instant of that call — the fact a queue
  /// drained later could not have.
  machine::ticks at{};
};

/// The services, implemented. Held by whoever built it and attached with
/// `seam_engine::set_host()`; never owned by the engine.
class host_services final : public machine::seam_host_services {
 public:
  host_services() = default;

  /// Read the machine, remember what was asked, and return. Writes
  /// nothing: a seam is the only thing that may move this machine
  /// (PLAN.md §4), and a host service that wrote it would be the
  /// fidelity boundary's one rule broken from the far side.
  void serve(machine::machine& box, machine::seam_host_service which,
             std::uint32_t argument) override;

  [[nodiscard]] const host_service_record& record(
      machine::seam_host_service which) const noexcept {
    return records_[static_cast<std::size_t>(which)];
  }

 private:
  std::array<host_service_record, machine::seam_host_service_count> records_{};
};

}  // namespace amberfolio::host
