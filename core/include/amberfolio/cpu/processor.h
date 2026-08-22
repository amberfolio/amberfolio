// SPDX-License-Identifier: AGPL-3.0-only
//
// The 8086 itself: register state, a bus to execute against, and a step
// loop that decodes an instruction and calls its handler. The sixteen
// instruction families of M1's wide phase hang off this.
//
// The issue that specifies this (M1-F1) calls the class CpuCore. It is
// spelled `processor` here for two reasons: types in this codebase are
// lower case (see amberfolio::version), and "core" already means the whole
// emulator library in PLAN.md and in the build. `amberfolio::cpu::core`
// would have been the one name in the project that means two things.

#pragma once

#include <cstdint>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/bus.h"
#include "amberfolio/cpu/decoder.h"
#include "amberfolio/cpu/diagnostics.h"
#include "amberfolio/cpu/dispatch.h"
#include "amberfolio/cpu/interrupts.h"
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

/// What one call to `step()` did.
enum class step_status : std::uint8_t {
  /// One instruction ran to completion.
  ran,
  /// One iteration of a repeated string instruction ran, and the
  /// instruction has *not* retired: IP is back at the instruction (or at
  /// its last prefix — see prefix_state::last_prefix_ip) and the next
  /// `step()` continues the run.
  ///
  /// This is the observable half of the step model PLAN.md §3 states: a
  /// REP run is many steps so that it stays interruptible between
  /// iterations. Without it a caller cannot tell "the instruction is
  /// still going" from "the instruction jumped to itself", which is a
  /// distinction the conformance harness (M1-F4) has to make on every
  /// string vector.
  repeating,
  /// No instruction ran: an interrupt was recognized at the step boundary
  /// and delivered, and the step ends at the first instruction of its
  /// handler.
  ///
  /// Its own status rather than `ran` because it is its own thing — a
  /// caller charging virtual time wants to charge for it, and a caller
  /// showing a human what the machine just did would be lying to call it
  /// an instruction. It is also the only status a conformance vector can
  /// never legitimately produce, which makes it a useful thing to be able
  /// to name.
  serviced,
  /// The processor is halted (HLT) and consumed nothing. It leaves this
  /// state the moment an interrupt is delivered — the next `step()` then
  /// reports `serviced`, not `halted`.
  halted,
  /// The processor stopped rather than invent behaviour it does not have.
  /// See `processor::stop()`; the machine is inspectable and unchanged.
  stopped,
};

class processor {
 public:
  /// An instruction may not be more than this many prefix bytes long.
  /// See stop_reason::prefix_chain_too_long for why there is a limit at
  /// all; the value is far above anything a real encoding uses and far
  /// below the 65536 it would take to actually wrap the segment.
  static constexpr unsigned prefix_limit = 256;

  /// `machine_bus`, `log` and `table` must outlive the processor. `log`
  /// may be null: a stop is still recorded and still returned
  /// (diagnostics.h explains why that is not a hole in the "log, don't
  /// fake" rule). `table` defaults to the instruction set the machine
  /// runs; a test passes its own.
  explicit processor(bus& machine_bus, diagnostics* log = nullptr,
                     const dispatch_table& table = instruction_set()) noexcept;

  /// Power-on / RESET state: CS=FFFF, IP=0000, every other register zero,
  /// no flag set. Real 8086 behaviour — execution begins at FFFF:0000,
  /// sixteen bytes below the top of the address space, which is why the
  /// ROM's first instruction there is always a jump.
  ///
  /// Clears any halt and any recorded stop; the constructor calls it, so
  /// a freshly built processor is a reset one.
  void reset() noexcept;

  /// Execute one scheduling step: one instruction, or one iteration of a
  /// repeated string instruction (PLAN.md §3 — a REP run must be
  /// interruptible between iterations, so it cannot be one step). What a
  /// step *costs* in virtual time is the M2 scheduler's business, not
  /// this function's.
  ///
  /// A step begins at an instruction boundary, which is where interrupts
  /// are recognized: if one is due, this delivers it and returns
  /// `serviced` without running an instruction at all.
  step_status step();

  [[nodiscard]] registers& regs() noexcept { return regs_; }
  [[nodiscard]] const registers& regs() const noexcept { return regs_; }

  /// What the decoder made of the instruction now executing. A handler
  /// reads its prefixes, its ModRM byte and its effective address from
  /// here rather than decoding anything itself.
  [[nodiscard]] const instruction& current() const noexcept { return current_; }

  /// True once the processor has stopped. Sticky: it stays stopped until
  /// `reset()`, and repeated `step()` calls keep returning
  /// `step_status::stopped` without touching the bus.
  [[nodiscard]] bool stopped() const noexcept {
    return stop_.reason != stop_reason::none;
  }

  /// What it stopped on. `reason == stop_reason::none` while running.
  [[nodiscard]] const stop_record& stop() const noexcept { return stop_; }

  /// The HLT state. `halt()` is what the HLT handler calls; `resume()` is
  /// what interrupt delivery calls. Both are here rather than inside an
  /// instruction handler because the step loop is what has to honour them.
  [[nodiscard]] bool halted() const noexcept { return halted_; }
  void halt() noexcept { halted_ = true; }
  void resume() noexcept { halted_ = false; }

  /// What a repeated string instruction's handler calls when it has run
  /// one iteration and the repeat count has not run out: the step returns
  /// `step_status::repeating` instead of `ran`. The handler is still the
  /// one that rewinds IP — this only changes what the step loop reports.
  ///
  /// Cleared at the top of every `step()`, so it cannot leak from one
  /// instruction into the next. (The string family — issue #30 — is what
  /// will call it; nothing does yet.)
  void keep_repeating() noexcept { repeating_ = true; }

  // --- Interrupts ------------------------------------------------------
  //
  // interrupts.h is the model: the sequence, which source wins, and the
  // three timing windows. What follows is the surface.

  /// The delivery sequence, and the only one: push FLAGS, clear IF and
  /// TF, push CS, push IP, load CS:IP from the vector table entry at
  /// `vector * 4`. INT n / INT3 / INTO (#31) and the divide error (#26)
  /// call this instead of restating it.
  ///
  /// The address pushed is IP as the caller leaves it, so a handler that
  /// has already fetched its own immediate gets the 8086's "return past
  /// the instruction" behaviour without doing anything. Delivery also
  /// ends a halt, and abandons a repeated string instruction that had not
  /// retired.
  void deliver_interrupt(std::uint8_t vector);

  /// The external lines, for the machine (M2) to drive.
  ///
  /// NMI is an edge: `raise_nmi()` latches a request that survives until
  /// it is delivered, and IF does not gate it. INTR is a level with the
  /// vector the interrupt controller would put on the bus, sampled at the
  /// step boundary and gated on IF; delivering it drops the level, which
  /// is this machine's whole model of the acknowledge cycle. `clear_intr`
  /// withdraws a request that has not been taken yet.
  void raise_nmi() noexcept;
  void raise_intr(std::uint8_t vector) noexcept;
  void clear_intr() noexcept;

  /// Whether the next step boundary would deliver an interrupt rather
  /// than execute an instruction — the same question `step()` asks
  /// itself, asked from outside and consuming nothing.
  ///
  /// The machine's service layer needs it. A native handler runs at a
  /// step boundary, immediately before the CPU executes the stub's IRET,
  /// and those two have to be one thing: if an interrupt is delivered in
  /// between, the IRET that eventually returns to the stub would arrive
  /// at a boundary that runs the handler all over again. So the service
  /// layer defers to the interrupt and dispatches when the boundary is
  /// clear (machine/service_floor.h).
  [[nodiscard]] bool interrupt_due() const noexcept;

  [[nodiscard]] bool nmi_pending() const noexcept { return nmi_latched_; }
  [[nodiscard]] bool intr_pending() const noexcept { return intr_asserted_; }

  /// True when an instruction that began with TF set has finished and the
  /// single-step trap it owes has not been delivered yet.
  [[nodiscard]] bool trap_pending() const noexcept { return trap_pending_; }

  /// The vector the controller is holding up with INTR — meaningful while
  /// `intr_pending()`. A replay's state serialization (machine/state.h)
  /// needs it; nothing else reads it from outside.
  [[nodiscard]] std::uint8_t intr_vector() const noexcept {
    return intr_vector_;
  }

  /// Whether a repeated string instruction is suspended between
  /// iterations, and where an interrupt taken now would resume it — the
  /// last prefix byte, as interrupts.h explains. For the same reader.
  [[nodiscard]] bool repeat_suspended() const noexcept { return suspended_; }
  [[nodiscard]] std::uint16_t repeat_resume_ip() const noexcept {
    return resume_ip_;
  }

  /// Hold external interrupt recognition off until after the next
  /// instruction. STI (#33), `MOV Sreg, r/m` (#18) and `POP Sreg` (#23)
  /// call this — one window, one flag, for the reasons interrupts.h
  /// gives. The single-step trap is not held off.
  void inhibit_interrupts() noexcept { inhibited_ = true; }

  [[nodiscard]] bool interrupts_inhibited() const noexcept {
    return inhibited_;
  }

  // --- Memory, addressed the way the program addresses it -------------
  //
  // Offsets are segment-relative and wrap at 64 KiB *within the segment*:
  // a word at offset FFFF is the byte at FFFF and the byte at 0000 of the
  // same segment. The segment:offset pair is then folded to a physical
  // address, which wraps again at 1 MiB (address.h). A word is two byte
  // accesses in that order, low half first — the bus is eight bits wide
  // and stays that way.

  [[nodiscard]] std::uint8_t read_byte(std::uint16_t segment,
                                       std::uint16_t offset);
  void write_byte(std::uint16_t segment, std::uint16_t offset,
                  std::uint8_t value);

  [[nodiscard]] std::uint16_t read_word(std::uint16_t segment,
                                        std::uint16_t offset);
  void write_word(std::uint16_t segment, std::uint16_t offset,
                  std::uint16_t value);

  [[nodiscard]] std::uint16_t read(width w, address at);
  void write(width w, address at, std::uint16_t value);

  // --- The stack -------------------------------------------------------
  //
  // SP moves first on a push and last on a pop, and its offset wraps in
  // 16 bits like any other: pushing with SP=0000 writes at SS:FFFE. Here
  // rather than in the stack family (#23) because interrupt delivery has
  // to push three words long before PUSH exists; #23 and IRET (#31) are
  // the other callers. Doing it in this order is also what gives `PUSH
  // SP` the value the 8086 pushes — the already-decremented one, where
  // every processor after it pushes the original.

  void push_word(std::uint16_t value);
  [[nodiscard]] std::uint16_t pop_word();

  // --- The instruction stream -----------------------------------------

  /// Read the byte at CS:IP and advance IP. IP wraps in 16 bits: an
  /// instruction stream that runs off the end of a segment continues at
  /// the bottom of the same one, it does not roll into the next. Public
  /// because immediates are the handler's to fetch.
  std::uint8_t fetch_byte();
  std::uint16_t fetch_word();

  // --- The decoded operands -------------------------------------------

  /// The r/m operand: a register when the ModRM byte names one, and the
  /// memory at the effective address otherwise. A byte value is returned
  /// in the low half.
  [[nodiscard]] std::uint16_t read_rm(width w);
  void write_rm(width w, std::uint16_t value);

  /// The reg operand, which is always a register.
  [[nodiscard]] std::uint16_t read_reg(width w);
  void write_reg(width w, std::uint16_t value);

  [[nodiscard]] bus& machine_bus() noexcept { return *bus_; }

 private:
  /// Consume prefix bytes into `current_.prefixes` until a non-prefix
  /// byte turns up, and answer that byte. Sets `stop_` and answers
  /// nothing useful if the run exceeds `prefix_limit`.
  std::uint8_t fetch_opcode();

  /// Consume the ModRM byte and its displacement, and work out the
  /// effective address if it names memory.
  void decode_operands();

  /// The effective address for a ModRM byte that names memory, including
  /// the displacement fetch, the default segment and any override.
  address effective_address(const cpu::modrm& m);

  /// Rewind IP to the instruction's first byte, record the stop, and
  /// tell the sink. Returns `stopped` so a caller can `return` it.
  step_status stop_with(stop_reason reason, std::uint8_t extension);

  /// Which source wins at a boundary reached with `inhibited` in force,
  /// or `interrupt_source::none`. Const, and it consumes nothing: the NMI
  /// latch and the INTR level are dropped by whoever acts on the answer.
  /// One function so that the priority interrupts.h states — NMI, then
  /// INTR, then the owed trap — exists once, whether it is being asked
  /// in order to deliver or in order to stay out of the way.
  [[nodiscard]] interrupt_source pending_source(bool inhibited) const noexcept;

  /// The step boundary: recognize one interrupt, if any is due, and
  /// deliver it. True if it did, in which case no instruction runs this
  /// step. Called before anything is fetched, so that an interrupt the
  /// machine raised while the last step was returning is taken *before*
  /// the next instruction rather than after it.
  bool service_interrupt();

  /// Where an interrupt taken at this boundary has to return to. IP,
  /// except while a repeated string instruction is suspended between
  /// iterations — see interrupts.h on the prefix the 8086 forgets.
  [[nodiscard]] std::uint16_t interrupt_return_ip() const noexcept {
    return suspended_ ? resume_ip_ : regs_.ip;
  }

  registers regs_{};
  bus* bus_;
  diagnostics* log_;
  const dispatch_table* table_;
  instruction current_{};
  stop_record stop_{};
  bool halted_{false};
  bool repeating_{false};

  // --- Interrupt state -------------------------------------------------

  /// The NMI edge, latched until it is delivered.
  bool nmi_latched_{false};

  /// The INTR level and the vector the controller is holding up with it.
  bool intr_asserted_{false};
  std::uint8_t intr_vector_{};

  /// Set by STI and by a segment-register load; consumed at the next step
  /// boundary, which is the one that does not get to recognize anything.
  bool inhibited_{false};

  /// An instruction began with TF set and has finished. The trap is owed
  /// at the next boundary.
  bool trap_pending_{false};

  /// A repeated string instruction has run an iteration and not retired.
  /// `resume_ip_` is where an interrupt taken now must return to: the
  /// instruction's last prefix byte, not its first.
  bool suspended_{false};
  std::uint16_t resume_ip_{};
};

}  // namespace amberfolio::cpu
