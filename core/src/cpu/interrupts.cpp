// SPDX-License-Identifier: AGPL-3.0-only
//
// Interrupt delivery. interrupts.h is the model and processor.h is the
// surface; this file is the code and the reasons.
//
// It is a separate translation unit from processor.cpp for the same
// reason alu.cpp is: the step loop is about decoding and dispatch, and
// this is about a completely different thing that happens to need the
// same registers. The one place they meet is `service_interrupt`, which
// `step()` calls at the top of every step.

#include "amberfolio/cpu/interrupts.h"

#include <cstdint>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

// --- The lines --------------------------------------------------------

void processor::raise_nmi() noexcept { nmi_latched_ = true; }

void processor::raise_intr(std::uint8_t vector) noexcept {
  intr_asserted_ = true;
  // Last raise wins. A real interrupt controller is asked for the vector
  // during the acknowledge cycle, not when it asserts the line, so what
  // it would supply is whatever it has decided by then — and a machine
  // that raises twice before either is taken has decided twice.
  intr_vector_ = vector;
}

void processor::clear_intr() noexcept { intr_asserted_ = false; }

// --- Delivery ---------------------------------------------------------

void processor::deliver_interrupt(std::uint8_t vector) {
  const std::uint16_t return_ip = interrupt_return_ip();

  push_word(regs_.flags);
  // Both, and before the return address is pushed rather than after: the
  // flags the handler inherits are the ones that matter to it, and the
  // flags on the stack are the ones it will hand back. Clearing TF here
  // is the whole of what stops the single-step trap from recursing —
  // there is no separate guard, and there does not need to be one.
  regs_.set_flag(flag::if_ | flag::tf, false);
  push_word(regs_[sreg::cs]);
  push_word(return_ip);

  // After the pushes, which is the order the manual describes the
  // sequence in. It is invisible unless the stack and the vector table
  // overlap, and the only thing that can settle it is a vector whose
  // random state makes them — #31 (INT/INT3/INTO/IRET) is where that
  // happens, and where this gets confirmed or turned round.
  const std::uint16_t entry = vector_table_offset(vector);
  const std::uint16_t offset = read_word(vector_table_segment, entry);
  const std::uint16_t segment =
      read_word(vector_table_segment, static_cast<std::uint16_t>(entry + 2u));

  regs_[sreg::cs] = segment;
  regs_.ip = offset;

  // Whatever the processor was in the middle of, it is not in the middle
  // of it any more. A halt ends here — that is the only way one ever ends
  // — and a repeated string instruction that had not retired is
  // abandoned: the return address on the stack points at its last prefix,
  // and re-entering it is IRET's problem and the decoder's.
  resume();
  suspended_ = false;
  repeating_ = false;
}

// --- The boundary -----------------------------------------------------

interrupt_source processor::pending_source(bool inhibited) const noexcept {
  if (!inhibited && nmi_latched_) {
    // Ungated: IF has nothing to say about the non-maskable line, which
    // is the entire point of it.
    return interrupt_source::nmi;
  }
  if (!inhibited && intr_asserted_ && regs_.flag_set(flag::if_)) {
    return interrupt_source::intr;
  }
  if (trap_pending_) {
    // Last, because Intel puts single-step last. Not gated on
    // `inhibited`: the window a segment load opens is about external
    // interrupts, and this part is not the 286 that closed it against
    // traps too.
    return interrupt_source::single_step;
  }
  return interrupt_source::none;
}

bool processor::interrupt_due() const noexcept {
  return pending_source(inhibited_) != interrupt_source::none;
}

bool processor::service_interrupt() {
  // Consumed whether or not anything was pending: the window is one
  // instruction wide, and this is that instruction's boundary. Taking it
  // now is what makes STI's delay exactly one instruction long, because
  // the flag can only have been set by the instruction before this one.
  const bool inhibited = inhibited_;
  inhibited_ = false;

  std::uint8_t vector = 0;
  switch (pending_source(inhibited)) {
    case interrupt_source::none:
      return false;
    case interrupt_source::nmi:
      // The latch is the edge, so it is consumed here whether or not the
      // machine is still holding the pin down.
      nmi_latched_ = false;
      vector = interrupt_vector::nmi;
      break;
    case interrupt_source::intr:
      vector = intr_vector_;
      // The acknowledge cycle, such as this machine has one: a real 8259
      // drops INTR when the processor takes the vector off the bus, and
      // raises it again straight away if it has another request waiting.
      // M2's controller does the raising; this is the dropping.
      intr_asserted_ = false;
      break;
    case interrupt_source::single_step:
      vector = interrupt_vector::single_step;
      break;
  }

  // An owed trap does not survive a boundary that something else took.
  // Delivery clears TF, so by the time the winner's handler starts there
  // is nothing left to trap on, and queueing it would show the program
  // two interrupts where the part shows one. interrupts.h says why
  // dropping it is the only reading of "lowest priority" that holds
  // together.
  trap_pending_ = false;
  deliver_interrupt(vector);
  return true;
}

}  // namespace amberfolio::cpu
