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
// What it does
// ------------
//
// M5-D1 built the door and left it with no consumer. What it does in
// every case is the part that is the *door*: it takes the call, it reads
// the machine at the moment of the call, and it remembers what it saw, so
// that a host can say afterwards that the callout arrived and when.
//
// **And since M5-E2c it has consumers.** `automap_update` drives the
// exploration sidecar (`automap_store.h`), which reads what the panel has
// explored out of the machine and writes it into a file beside the save.
// That is a host doing host work — files are a host's, by PLAN.md §4 —
// and it is off unless a host has been asked for it.
//
// `journal_open` is the other (M5-E4, #175), and it is the one that has to
// hand something *back*. `serve()` answers `void` and `call_host()`
// answers a `bool`: between them they can say a call was served and not
// what it found. So what it found goes into `machine::journal()`, which is
// core's own observation buffer for exactly this — not machine state, on
// the same three terms the automap's store is not (`machine/journal.h`),
// and read by the seam the instant the callout returns. The store it is
// answered out of is a host's (`journal_store.h`); a host that has not set
// one leaves every entry unanswerable, which is the honest state for a
// player who has not ingested a journal and is what the reader shows.
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

#include "amberfolio/host/automap_store.h"
#include "amberfolio/host/journal_store.h"
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

  /// Read the machine, remember what was asked, and return.
  ///
  /// **It never writes machine state.** A seam is the only thing that
  /// may move this machine (PLAN.md §4), and a host service that wrote
  /// it would be the fidelity boundary's one rule broken from the far
  /// side. What it may touch is observation and files: the exploration
  /// store since M5-E2c, which is not machine state on
  /// `machine/automap.h`'s own three terms; the journal delivery buffer
  /// since M5-E4, which is not machine state on `machine/journal.h`'s
  /// identical three; and files, which are a host's own business.
  void serve(machine::machine& box, machine::seam_host_service which,
             std::uint32_t argument) override;

  [[nodiscard]] const host_service_record& record(
      machine::seam_host_service which) const noexcept {
    return records_[static_cast<std::size_t>(which)];
  }

  /// Where `journal_open` looks an entry up (M5-E4, #175).
  ///
  /// A pointer and not a member, unlike the exploration sidecar next door,
  /// because the store is already somewhere in both hosts by the time this
  /// object exists: the desktop host reads one off the player's disk and
  /// the browser keeps one for the life of the tab (`journal_store.h`).
  /// Null — which is the default — is "no journal has been read", and the
  /// reader says so rather than showing a blank page.
  ///
  /// Held, never owned, and it must outlive the run: the same contract
  /// `seam_engine::set_host()` has for this object.
  /// **Not const since M5-E4b (#222)**: `journal_open` only reads, but
  /// `journal_seen` writes the log back into the store, which is where it
  /// outlives the machine. A host that hands over a store is handing over
  /// somewhere to put what the game says.
  void set_journal_store(journal_store* store) noexcept { journal_ = store; }
  [[nodiscard]] const journal_store* journal() const noexcept {
    return journal_;
  }
  [[nodiscard]] journal_store* journal() noexcept { return journal_; }

  /// The exploration sidecar `automap_update` drives (M5-E2c, #173).
  ///
  /// It lives here because this is the object both hosts already attach,
  /// so a browser gets the persistence with no wiring of its own beyond
  /// the flag that turns it on and the file events that tell it which
  /// save slot the program touched. It is off until a host enables it,
  /// and while it is off `serve()` below still does everything it did.
  [[nodiscard]] automap_store& automap() noexcept { return automap_; }
  [[nodiscard]] const automap_store& automap() const noexcept {
    return automap_;
  }

 private:
  std::array<host_service_record, machine::seam_host_service_count> records_{};
  automap_store automap_{};
  journal_store* journal_{nullptr};
};

}  // namespace amberfolio::host
