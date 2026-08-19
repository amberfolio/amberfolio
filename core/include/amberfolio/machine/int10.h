// SPDX-License-Identifier: AGPL-3.0-only
//
// INT 10h: the video BIOS subset PLAN.md §3 scopes to two things, "mode
// set, palette register set." M2-D3 (#48), and the last piece the video
// path needed — ega.h has the device, renderer.h turns its planes into
// pixels, this is how a program tells the device what to be before either
// of those matters.
//
//
// A free function, reaching the hardware through the bus
// ----------------------------------------------------------
//
// `service_handler` (service_floor.h) is a plain function pointer with
// nowhere to keep a device reference — the same reason `cpu::handler` is
// one, and core/ carries no `<functional>` to close over anything with
// (PLAN.md §4). So the handler below does not hold a pointer to "the"
// EGA; it does not need one. AH=00h's mode-set table and AH=10h's palette
// writes go through `machine::write_port8`/`read_port8` at the real
// 3C4h/3C5h, 3CEh/3CFh and 3C0h/3DAh addresses — exactly the ports a
// program poking the hardware directly would use, and exactly what a real
// ROM BIOS's own mode-set microcode does: it is a table of OUT
// instructions, not a pointer into the video card. Machine state that is
// not a bus cycle — "has a mode been programmed yet" — goes through
// `machine::note_video_mode_set()`, for the same reason the timer's
// default handler reaches the BDA through `cpu::processor::read_word`
// rather than a pointer of its own (service_floor.cpp): it is what
// `service_floor::box()` hands out.
//
//
// What is and is not real BIOS behaviour
// -----------------------------------------
//
// The AH=10h calling convention (AL=00h: BL = palette register 0-15,
// BH = colour; AL=01h: BH = overscan colour) matches the documented
// public DOS/BIOS convention — a fact about an interface, not an
// original byte sequence, and explicitly the kind of thing the
// clean-content rule allows (CONTRIBUTING.md). The mode-0Dh register
// *values* AH=00h AL=0Dh writes are this file's own, self-authored
// table, not a transcription of any ROM: most of them (every sequencer
// and graphics-controller register but the map mask) are inert on this
// device by ega.h's own admission, so their exact values do not change
// what the machine does, only what a test asserts. The one exception is
// the sequencer's map mask, set to 0Fh so that a program's writes after
// mode-set actually reach the planes — that value is load-bearing and
// documented as such at its definition. The default palette AH=00h
// installs is the standard 16-colour EGA/CGA-compatible palette (the
// well-known, publicly documented default — its non-consecutive register
// values for the eight "bright" colours are the historical
// CGA-compatibility quirk every EGA/VGA reference describes), used here
// because it is a real, checkable fact rather than an arbitrary choice —
// again, not because any specific program depends on it existing.
//
//
// What refuses, and how
// ------------------------
//
// AH=00h with AL anything but 0Dh, AH=10h with AL anything but 00h/01h,
// and any AH but 00h/10h are all the same shape: the vector *has* a
// handler, the handler ran, and it looked at what was asked and declined.
// That is not `stop_reason::unimplemented_service` (service_floor.h) —
// nothing is missing, something was refused — so it goes through
// `machine::stop_unsupported_request()` instead, PLAN.md §3's rule
// applied at the granularity of one call.

#pragma once

#include "amberfolio/machine/service_floor.h"

namespace amberfolio::machine {

/// Install the INT 10h handler on `floor` (`service::video_vector`).
void install_int10(service_floor& floor);

}  // namespace amberfolio::machine
