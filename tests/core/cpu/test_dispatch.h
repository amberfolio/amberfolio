// SPDX-License-Identifier: AGPL-3.0-only
//
// Dispatch tables for the decoder's tests.
//
// The decoder has to be testable before there are any instructions to
// decode into, and it has to stay testable afterwards: a test that says
// "opcode 0x0F stops the machine" is true today and false the week
// somebody implements 0x0F. Both problems go away by handing the
// processor a table of the test's own — `nothing()` when the point is
// that an opcode is unimplemented, `everything()` when the point is what
// the decoder worked out on the way to the handler.
//
// Handlers are plain function pointers and cannot capture, so the ones
// that need to be told apart are template instantiations with a shared
// global log. Test binaries run a case at a time, so a global is honest
// here; `test::ran.clear()` at the top of a case is the whole protocol.

#pragma once

#include <cstddef>
#include <vector>

#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/processor.h"

namespace amberfolio::cpu::test {

/// The ids of the handlers that have run, in order.
inline std::vector<int> ran;

/// A handler that does nothing but say it was the one that ran. Distinct
/// `Id`s give distinct function pointers, which is what lets a test tell
/// one table entry from another.
template <int Id>
void mark(processor& /*cpu*/) {
  ran.push_back(Id);
}

/// The handler used where a test only needs *some* handler.
inline constexpr handler present = &mark<0>;

/// A table with nothing in it: every opcode is unimplemented, whatever
/// the wide phase has got round to.
[[nodiscard]] inline dispatch_table nothing() { return {}; }

/// A table where every opcode and every group entry is implemented, so a
/// step always reaches a handler and `processor::current()` holds what
/// the decoder made of the instruction.
[[nodiscard]] inline dispatch_table everything() {
  dispatch_table t{};
  for (handler& h : t.primary) {
    h = present;
  }
  for (auto& group : t.group) {
    for (handler& h : group) {
      h = present;
    }
  }
  return t;
}

}  // namespace amberfolio::cpu::test
