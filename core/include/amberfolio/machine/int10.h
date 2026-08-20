// SPDX-License-Identifier: AGPL-3.0-only
//
// INT 10h: the video BIOS subset PLAN.md §3 scopes to two things, "mode
// set, palette register set," plus the handful M3's first boot went on
// to ask for on its way to the title screen (#87): mode read-back,
// active page, the character under the cursor, and where the character
// generator is. M2-D3 (#48), and the last piece the video
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
// The modes this machine has, and the one it only records
// -------------------------------------------------------
//
// M3's first boot (#87) opened with five calls in a row, before it drew
// anything: read the mode, set mode 03h, read the mode again, ask where
// the character generator is, read the character under the cursor. Then
// it set mode 0Dh and never looked back. That is the era's standard
// opening — normalize the adapter into 80x25 text, look around, go to
// graphics — and every one of those five had to be answered before this
// machine could reach a title screen.
//
// The decision, recorded here because it widens what M2 settled:
//
//   * **0Dh is programmed**, as it always was. It is the one mode the
//     write pipeline and the renderer implement (PLAN.md §9).
//   * **03h is recorded and reported and nothing else.** The mode number,
//     the column count and the character height go into the BIOS data
//     area, so AH=0Fh answers truthfully and a program that reads 40:49
//     itself sees the same thing. *Nothing is written to the adapter*,
//     because there is no text path here to write: no CRTC (#47), no
//     character generator, and nothing claiming B8000 (memory_map.h).
//     The machine says so, once, through
//     `notice_kind::undisplayable_video_mode`.
//   * **Every other mode is refused**, exactly as before.
//
// Why 03h is not simply refused with the rest: refusing ends the run of
// any program that passes through text on its way to graphics, which is
// most of them, over a mode whose output nothing was ever going to look
// at. And why it is not silently accepted: because "we went along with
// it" with no line in the log is the failure mode PLAN.md §3 exists to
// prevent. A notice is what makes the accommodation a worklist entry
// instead of a secret.
//
// What this deliberately does *not* do is blank the display while the
// recorded mode is 03h. The frame the renderer composes is still the
// planes, so a program that stayed in text mode would see the last
// graphics frame rather than an empty screen. Nothing in M3 stays in
// text mode for a single frame, the notice already states the
// limitation, and coupling the renderer to a mode number is a change to
// make when something needs it.
//
//
// What refuses, and how
// ------------------------
//
// AH=00h with AL anything but 0Dh or 03h, AH=05h for any page but 0,
// AH=08h outside a text mode, AH=10h with AL anything but 00h/01h,
// AH=11h AL=30h asking for a ROM font this machine does not carry, and
// any AH outside 00h/05h/08h/0Fh/10h/11h
// are all the same shape: the vector *has* a
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
