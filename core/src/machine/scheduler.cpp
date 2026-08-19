// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/scheduler.h"

namespace amberfolio::machine {

bool scheduler::add(scheduled& who) {
  if (registered_ == max_participants || index_of(who) != max_participants) {
    return false;
  }

  entries_[registered_] = {.who = &who, .due = 0, .armed = false};
  ++registered_;
  return true;
}

bool scheduler::arm(scheduled& who, ticks when) {
  const std::size_t at = index_of(who);
  if (at == max_participants) {
    return false;
  }
  if (in_handler_ && when <= dispatching_) {
    return false;
  }

  entries_[at].due = when;
  entries_[at].armed = true;
  return true;
}

void scheduler::disarm(scheduled& who) noexcept {
  const std::size_t at = index_of(who);
  if (at != max_participants) {
    entries_[at].armed = false;
  }
}

ticks scheduler::deadline(const scheduled& who) const noexcept {
  const std::size_t at = index_of(who);
  if (at == max_participants || !entries_[at].armed) {
    return never;
  }
  return entries_[at].due;
}

ticks scheduler::next_deadline() const noexcept {
  ticks soonest = never;
  for (std::size_t i = 0; i < registered_; ++i) {
    if (entries_[i].armed && entries_[i].due < soonest) {
      soonest = entries_[i].due;
    }
  }
  return soonest;
}

void scheduler::dispatch_due(ticks now) {
  // The loop re-scans after every handler because a handler may arm
  // anything, including a participant earlier in the list than the one
  // that just fired. What it may not do is arm at or before the deadline
  // being dispatched (arm() refuses that), so every pass takes a deadline
  // strictly later than the last one and the loop cannot run away.
  for (std::size_t at = earliest_due(now); at != max_participants;
       at = earliest_due(now)) {
    const ticks due = entries_[at].due;

    // Disarmed before the call, so on_deadline() is a one-shot and a
    // handler that wants another moment says so rather than having to
    // remember to cancel one it did not.
    entries_[at].armed = false;

    dispatching_ = due;
    in_handler_ = true;
    entries_[at].who->on_deadline(due);
    in_handler_ = false;
  }
}

void scheduler::disarm_all() noexcept {
  for (std::size_t i = 0; i < registered_; ++i) {
    entries_[i].armed = false;
  }
}

std::size_t scheduler::index_of(const scheduled& who) const noexcept {
  for (std::size_t i = 0; i < registered_; ++i) {
    if (entries_[i].who == &who) {
      return i;
    }
  }
  return max_participants;
}

std::size_t scheduler::earliest_due(ticks now) const noexcept {
  std::size_t best = max_participants;
  for (std::size_t i = 0; i < registered_; ++i) {
    if (!entries_[i].armed || entries_[i].due > now) {
      continue;
    }
    // Strictly earlier, so an equal deadline leaves the earlier
    // registration in place: that is the tie-break, and it is one
    // comparison rather than a rule written somewhere else.
    if (best == max_participants || entries_[i].due < entries_[best].due) {
      best = i;
    }
  }
  return best;
}

}  // namespace amberfolio::machine
