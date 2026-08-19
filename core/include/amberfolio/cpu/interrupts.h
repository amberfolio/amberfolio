// SPDX-License-Identifier: AGPL-3.0-only
//
// Interrupts: one delivery sequence, four sources, and the timing windows
// that make them subtle.
//
// This header is the model. The state and the API are on `processor`
// (processor.h), because they need the registers and the bus; the reasons
// are in interrupts.cpp. Read this one first — the timing windows below
// are the part that is easy to implement plausibly and wrongly, and the
// single-step vectors cannot catch you at it, because IF and TF are never
// set in the suite.
//
//
// The sequence
// ------------
//
// Everything that interrupts this machine — INT n, INT3, INTO, a divide
// error, the single-step trap, NMI, and whatever the interrupt controller
// puts on the bus — goes through the same five steps, in this order:
//
//     push FLAGS
//     clear IF and TF
//     push CS
//     push IP
//     load CS:IP from the vector table entry at vector * 4
//
// That is `processor::deliver_interrupt`, and it is the only
// implementation there is. The INT family (#31) and the DIV family (#26)
// call it rather than restating it, for the same reason the sixteen
// instruction families call the ALU kernel rather than deriving CF: five
// steps written six times is six chances to get the frame subtly wrong,
// and a wrong interrupt frame surfaces as a crash thousands of
// instructions after the cause.
//
// Clearing IF is why an interrupt handler runs with interrupts off until
// it says otherwise; clearing TF is why a handler is not single-stepped
// by the trap that got you there, and is the whole of what stops the trap
// from recursing.
//
// The return address pushed is IP as the *caller* leaves it. That is not
// a shortcut — it is the 8086's rule. A software interrupt returns past
// its own instruction, and so does a divide error, which is one of the
// places this part differs from every processor after it: the 286 pushes
// the address of the faulting DIV so the fault can be retried, and the
// 8086 pushes the address after it. #26 owns that fact; delivery just
// takes what it is given.
//
//
// The sources, and which one wins
// -------------------------------
//
// Two of them are the program's own and happen inside an instruction:
// INT n / INT3 / INTO, and the divide error. Those are not interrupts in
// the timing sense at all — the handler calls `deliver_interrupt`
// directly, mid-instruction, and nothing below applies to them.
//
// The other three are recognized at a step boundary, and the processor
// checks them there in this order:
//
//     NMI          latched on the edge, ungated: a machine that raises it
//                  gets it whatever IF says. Vector 2.
//     INTR         a level the machine holds up, with the vector the
//                  controller would put on the bus. Gated on IF.
//     single step  the trap owed by an instruction that began with TF
//                  set. Vector 1.
//
// The single-step trap is last because Intel documents it as the lowest
// priority, and lowest priority is meant literally here: if NMI or an
// unmasked INTR is pending at the same boundary, that one is delivered
// and the owed trap is dropped rather than queued. It cannot be queued
// and stay honest — delivery clears TF, so by the time the higher
// handler's first instruction runs there is nothing to trap on, and the
// program sees exactly one interrupt. Nothing in this emulator can reach
// that combination (nothing single-steps, and there is no NMI source in
// v1), which is precisely why it is written down rather than left to be
// rediscovered.
//
//
// The three timing windows
// ------------------------
//
// **TF fires one instruction late.** TF is sampled when an instruction
// *starts*; the trap is delivered at the boundary *after* it. So an
// instruction that sets TF — POPF, IRET, and nothing else — is not itself
// trapped: it began with TF clear. The instruction after it is. This is
// not a rounding of the rule, it is the rule, and it is what makes a
// debugger's "trace" command work at all: the IRET that returns to the
// traced program is the instruction that turns TF back on, and trapping
// on it would put the debugger back in its own code.
//
// **STI takes effect one instruction late.** STI sets IF, but interrupt
// recognition is held off until after the instruction that follows it.
// That is what lets a driver end a critical section with `STI` / `RET`
// and be sure the RET happens before the interrupt does. CLI has no such
// window: it clears IF, and the very next boundary is already closed.
//
// **A segment-register load holds interrupts off for one instruction.**
// The reason is `MOV SS, ax` / `MOV SP, bx`: SS and SP are loaded by two
// instructions and there is an instant between them where the stack
// pointer belongs to neither the old stack nor the new one. An interrupt
// taken there pushes six bytes into whatever that is. So the 8086
// suppresses recognition across the pair — and, on this part, it does so
// after a load of *any* segment register, not just SS. That is the choice
// this emulator makes: the SS case is why the mechanism exists, but early
// silicon does not check which register it was, and being right about the
// common case by being wrong about the rule is not a trade this codebase
// makes. `MOV Sreg, r/m` (#18) and `POP Sreg` (#23) call
// `inhibit_interrupts()`; so does STI (#33), because it is the same
// one-instruction window and the same flag.
//
// What that window does *not* cover here is the single-step trap. The
// 80286 suppresses the trap after a MOV to SS as well, and says so; the
// 8086 is the part that made that fix worth documenting. We are not a
// 286 (registers.h says the same thing about the top four flag bits), so
// the trap goes through and only external recognition is held off.
//
//
// HLT
// ---
//
// HLT stops the processor until something interrupts it. `step()` reports
// `step_status::halted` and consumes nothing meanwhile; NMI or an
// unmasked INTR ends it, and so does an owed single-step trap, because
// delivering an interrupt is the one thing that can. The address pushed
// is the one after the HLT, so IRET resumes past it rather than halting
// again.
//
//
// An interrupted REP, and the prefix the 8086 forgets
// --------------------------------------------------
//
// A repeated string instruction is one step per iteration (PLAN.md §3),
// so an interrupt can land between iterations — which is the point of
// modelling it that way. The real part backs IP up so that the run
// continues after IRET, and this is where it goes wrong: it backs up to
// the *last prefix byte*, not to the start of the instruction.
//
// With one prefix that is the same thing. With two it is not:
//
//     2E F3 A4      CS: REP MOVSB   interrupted -> resumes at F3
//                                   the run continues, reading from DS
//                                   instead of CS for the rest of it
//
//     F3 2E A4      REP CS: MOVSB   interrupted -> resumes at 2E
//                                   the repeat is gone: one more byte
//                                   moves, and CX keeps whatever was
//                                   left in it
//
// This is silicon, not a simplification, and software of the era worked
// around it by not putting a segment override on a REP — or by disabling
// interrupts around one. It is implemented as fact, and the run resumes
// from `prefix_state::last_prefix_ip`, which the decoder records for
// exactly this.
//
// Note what the same fact means for an *uninterrupted* run: the real part
// never re-fetches between iterations at all, so every prefix stays in
// force. That is why the string family's handler rewinds IP to
// `instruction::start_ip` and delivery, not the handler, is what backs up
// to the prefix.

#pragma once

#include <cstdint>

namespace amberfolio::cpu {

/// The vectors the processor raises for itself. Everything from 5 up
/// belongs to somebody else — the BIOS takes 8-1F, DOS takes 20-2F, and a
/// program may claim any of them.
namespace interrupt_vector {

/// A zero divisor, or a quotient too wide for the destination (#26).
inline constexpr std::uint8_t divide_error = 0;

/// The trap owed by an instruction that began with TF set.
inline constexpr std::uint8_t single_step = 1;

/// The non-maskable line.
inline constexpr std::uint8_t nmi = 2;

/// INT3, the one-byte breakpoint a debugger writes over an instruction
/// (#31).
inline constexpr std::uint8_t breakpoint = 3;

/// INTO, when OF is set (#31).
inline constexpr std::uint8_t overflow = 4;

}  // namespace interrupt_vector

/// Which of the three step-boundary sources an interrupt is coming from,
/// or `none`. Named because the boundary has to answer two different
/// questions about it — "is one due?" and "which one wins?" — and the
/// priority rule above is worth stating in exactly one place.
///
/// The machine's service layer asks the first question: a native BIOS
/// handler must not run at a boundary that is about to deliver an
/// interrupt instead of executing the stub's IRET, or the return from
/// that interrupt would land on the stub a second time and run the
/// handler twice (machine/service_floor.h).
enum class interrupt_source : std::uint8_t {
  /// Nothing is due at this boundary.
  none,
  /// The non-maskable line, latched on its edge.
  nmi,
  /// The maskable line, with the vector the controller is holding up.
  intr,
  /// The trap owed by an instruction that began with TF set.
  single_step,
};

/// One vector table entry: the offset, then the segment. Four bytes.
inline constexpr std::uint16_t vector_entry_size = 4;

/// The table lives at the bottom of memory, and it is ordinary RAM: a
/// program that writes four bytes at 0000:0020 has hooked the timer
/// interrupt, which is how essentially all of the era's resident software
/// worked. Nothing here treats it as special.
inline constexpr std::uint16_t vector_table_segment = 0;

/// Offset of `vector`'s entry within that segment. The whole table is
/// 1 KiB, so this cannot leave the segment however high the vector is.
[[nodiscard]] constexpr std::uint16_t vector_table_offset(
    std::uint8_t vector) noexcept {
  return static_cast<std::uint16_t>(static_cast<unsigned>(vector) *
                                    vector_entry_size);
}

}  // namespace amberfolio::cpu
