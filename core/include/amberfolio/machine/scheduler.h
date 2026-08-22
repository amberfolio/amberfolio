// SPDX-License-Identifier: AGPL-3.0-only
//
// The deadline queue: who wants to be woken at which tick of the virtual
// clock, and in what order the machine wakes them.
//
// device.h predicted this file and its shape — "a device that has to do
// something at a *moment* rather than in answer to a bus cycle posts a
// deadline to the scheduler... it arrives as a second thing a device may
// take part in, not as another virtual on this one" — and that is exactly
// what `scheduled` is. A device that never needs a moment (a keyboard
// port, a latch) implements `device` and nothing else and pays nothing;
// the PIT implements both and is registered twice, once as each.
//
// **One armed deadline per participant.** Every intended user of this
// wants a *next* event, not a list of them: the PIT posts its next output
// edge, the renderer its next frame boundary, audio its next sample
// block. Each of them recomputes that next moment when the one before it
// arrives, so a general priority queue would spend its generality on
// nobody. A device that genuinely needs two independent moments — a PIT
// running channel 0 and channel 2 on different divisors — registers two
// participants, one per channel, which is also how it wants its own code
// organized.
//
// **A deadline never fires mid-instruction.** The machine dispatches at
// step boundaries, which is where the CPU recognizes interrupts, so a
// device that raises a line from `on_deadline` has it seen by the very
// next instruction. Firing inside an instruction would mean a device
// observing the CPU halfway through a memory operand, which no real bus
// arrangement produces and which nothing in the design wants.
//
// **The tie-break is registration order.** Two participants can and will
// land on the same tick — a 100 Hz timer and a 200 Hz one share every
// other edge, exactly — and something has to go first. Registration order
// is the choice: it is fixed by how the machine is wired rather than by
// anything the emulated program does, so the same machine replays the
// same way every time, which is the entire point of a deterministic core
// (PLAN.md §4). Ordering by address, by insertion into a heap, or by
// whatever the last dispatch happened to leave behind would all be
// answers; none of them would be the same answer twice.
//
// **A handler is called with the tick it asked for, not the tick the
// machine reached.** Virtual time moves in whole steps, so a deadline at
// tick 41 is noticed at the boundary on tick 44 — and `on_deadline` is
// still handed 41. A device that schedules the next edge from the one it
// was given therefore never drifts, however coarse the step cost is and
// whatever mix of instructions the program is running. That is what makes
// "deadlines fire at exact virtual times regardless of instruction mix"
// true rather than approximately true.

#pragma once

#include <array>
#include <cstddef>

#include "amberfolio/machine/clock.h"

namespace amberfolio::machine {

class state_sink;

/// Something that asks to be woken at a moment in virtual time.
class scheduled {
 public:
  scheduled() = default;
  scheduled(const scheduled&) = delete;
  scheduled(scheduled&&) = delete;
  scheduled& operator=(const scheduled&) = delete;
  scheduled& operator=(scheduled&&) = delete;

  /// The moment arrived. `due` is the tick this participant armed — not
  /// the tick the machine has reached, which is at or past it (see the
  /// note at the top of this file).
  ///
  /// The deadline is disarmed before the call, so this is a one-shot: a
  /// participant that wants another moment arms one, and the natural way
  /// to do that is `arm(*this, due + period)`, which cannot drift.
  ///
  /// A re-arm must be **strictly after** `due`. The scheduler keeps
  /// dispatching while anything is due, so a participant that re-armed at
  /// or before the deadline it was just handed would be asking to be
  /// woken again, forever, inside one step. The scheduler refuses such a
  /// re-arm rather than hanging (see `scheduler::arm`).
  virtual void on_deadline(ticks due) = 0;

 protected:
  // See cpu/bus.h: a participant is held by reference and never owned or
  // deleted through this type, so it pays for no vtable slot it does not
  // need.
  ~scheduled() = default;
};

/// The queue itself: a fixed set of registered participants, at most one
/// armed deadline each, dispatched in virtual-time order.
class scheduler {
 public:
  /// How many participants there is room for. M2's list is short — the
  /// PIT's two channels, the renderer's frame boundary, audio's sample
  /// block — and eight leaves the milestone room, the same reasoning and
  /// the same number as `machine::max_devices`.
  static constexpr std::size_t max_participants = 8;

  /// Register `who`, and fix its place in the tie-break: participants
  /// registered earlier fire first when deadlines land on the same tick.
  ///
  /// False, and nothing registered, if there is no room left or `who` is
  /// already registered. Registration is not arming — a participant
  /// starts with no deadline, and a device that wants one from power-on
  /// arms it in `reset()`.
  bool add(scheduled& who);

  /// Arm `who` to be woken at tick `when`, replacing any deadline it
  /// already had. There is only ever one.
  ///
  /// `when` may be in the past: that says "overdue", and the participant
  /// is woken at the next step boundary with the tick it asked for, which
  /// is how a device catches up after a long instruction rather than
  /// silently losing the edge.
  ///
  /// False, and nothing armed, if `who` was never registered, or if this
  /// call comes from inside a handler and `when` is not strictly after
  /// the deadline being dispatched — the one re-arm that would loop
  /// forever (see `scheduled::on_deadline`).
  bool arm(scheduled& who, ticks when);

  /// Take away `who`'s deadline. Harmless if it had none.
  void disarm(scheduled& who) noexcept;

  /// `who`'s armed deadline, or `never` if it has none or was never
  /// registered.
  [[nodiscard]] ticks deadline(const scheduled& who) const noexcept;

  /// The earliest armed deadline, or `never` if nothing is armed. This is
  /// what a host would consult to sleep until something happens, and what
  /// a future "skip ahead while halted" optimization would use.
  [[nodiscard]] ticks next_deadline() const noexcept;

  /// Wake everything due at or before `now`, earliest first, ties broken
  /// by registration order.
  ///
  /// Keeps going until nothing is due, so a participant whose period is
  /// shorter than the step cost gets each of its edges in turn rather
  /// than one merged one — a PIT programmed with a small divisor really
  /// does produce several output edges in the time one instruction takes,
  /// and dropping them would be faking. `arm`'s refusal rule is what
  /// bounds the loop.
  void dispatch_due(ticks now);

  /// Disarm everything, keeping the registrations. What `machine::reset()`
  /// calls: a deadline is something a device posted while running, so it
  /// cannot survive the RESET line, but who is wired to the scheduler is
  /// part of how the machine was built and does survive it — the same
  /// distinction `machine::reset()` draws for attached devices.
  void disarm_all() noexcept;

  /// Every registration's armed deadline, in registration order
  /// (state.h): armed or not, and the tick. The registrations themselves
  /// are wiring and are not written — two machines wired alike agree on
  /// the count, and two wired differently are not the same machine.
  void save_state(state_sink& out) const;

  [[nodiscard]] std::size_t registered() const noexcept { return registered_; }

 private:
  /// Where `who` sits, or `max_participants` if it is not registered.
  [[nodiscard]] std::size_t index_of(const scheduled& who) const noexcept;

  /// The earliest entry due at or before `now`, or `max_participants` if
  /// none is. The scan compares with `<`, never `<=`, which is what makes
  /// the earlier registration win a tie.
  [[nodiscard]] std::size_t earliest_due(ticks now) const noexcept;

  struct entry {
    scheduled* who{};
    ticks due{};
    bool armed{};
  };

  std::array<entry, max_participants> entries_{};
  std::size_t registered_{};

  /// The deadline `dispatch_due` is currently handing to a handler, and
  /// whether it is inside one at all. Only `arm`'s refusal rule reads
  /// them.
  ticks dispatching_{};
  bool in_handler_{};
};

}  // namespace amberfolio::machine
