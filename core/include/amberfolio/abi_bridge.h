// SPDX-License-Identifier: AGPL-3.0-only
//
// The one door from an `af_machine*` back to the `machine&` behind it, for
// a C++ host that is built on the C ABI and has something of its own to
// wire in — the web host's own exports (hosts/web/src/main.cpp), which
// register a test seam for the node smoke check (M4-F4, #98).
//
// Deliberately not in abi.h, which is plain C and promises that nothing
// about the machine's C++ shape leaks through it; this is a C++ header
// for a C++ caller, and it hands back a reference a caller must not keep
// past `af_machine_destroy`. A JS host cannot include it, and that is the
// point: everything a page needs is in abi.h, and this exists only so a
// host-side C++ file need not duplicate the machine it is already linked
// against to reach it.

#pragma once

#include "amberfolio/abi.h"

namespace amberfolio::machine {
class machine;
class diagnostic_log;
}  // namespace amberfolio::machine

namespace amberfolio {

/// The machine behind `box`, or null for a null handle.
[[nodiscard]] machine::machine* af_machine_unwrap(af_machine* box) noexcept;

/// The diagnostic log behind `box`, or null for a null handle — the sink
/// that machine was built with, so that a C++ host inside the module can
/// hang a second consumer of the records off it (`machine/log.h`'s
/// `set_relay`). The web host's exploration sidecar is what wants it.
[[nodiscard]] machine::diagnostic_log* af_machine_log_unwrap(
    af_machine* box) noexcept;

}  // namespace amberfolio
