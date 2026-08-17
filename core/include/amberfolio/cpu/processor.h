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
#include "amberfolio/cpu/registers.h"

namespace amberfolio::cpu {

/// What one call to `step()` did.
enum class step_status : std::uint8_t {
  /// One scheduling step of work happened.
  ran,
  /// The processor is halted (HLT) and consumed nothing. It leaves this
  /// state when an interrupt is delivered — M1-F7.
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

  registers regs_{};
  bus* bus_;
  diagnostics* log_;
  const dispatch_table* table_;
  instruction current_{};
  stop_record stop_{};
  bool halted_{false};
};

}  // namespace amberfolio::cpu
