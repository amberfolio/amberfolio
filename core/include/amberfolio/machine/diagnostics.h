// SPDX-License-Identifier: AGPL-3.0-only
//
// "Log, don't fake" at the machine layer.
//
// cpu/diagnostics.h states the rule and its two halves — a record the
// caller cannot ignore, and a sink a host can render — and this is the
// same shape one level up. What is different is that the machine has
// three kinds of thing to say, not one:
//
//   * A **stop**. The machine gave up, the way the processor does when it
//     will not invent an instruction. Sticky, inspectable, cleared by
//     reset().
//   * A **notice**. Something was asked of an address or a port that
//     nothing answers for. This is *not* a stop, because there is nothing
//     to invent: an unterminated bus reads FF and swallows writes, and
//     that is the true answer rather than a guess (device.h). PLAN.md §3
//     asks for the log line here — "ignored (logged, not faked)" — not
//     for the machine to halt every time a program probes for a card that
//     is not fitted.
//   * A **service call**. The program asked the BIOS/DOS layer for
//     something, and a native handler answered it or nothing did
//     (service_floor.h). Every one of them is reported, which is what
//     makes the service floor traceable when a host wants to trace it and
//     silent when it does not — the choice is the sink's, not the
//     machine's, exactly as it is for notices.
//   * A **device stop**. A device refused a configuration it does not
//     implement (device.h's `device_fault`, #65) — a stop, not a notice,
//     because there was something to invent and the device declined to.
//     `machine` is what turns it into one: a device has no channel back
//     here to report anything itself.
//
// One sink takes all of it, including the processor's own stops, so a
// host wires up one object rather than three. The core stays free of host
// dependencies: these are structured records, and the sink is what turns
// them into something a human reads.

#pragma once

#include <cstdint>

#include "amberfolio/cpu/diagnostics.h"

namespace amberfolio::machine {

/// Why the machine stopped — the machine layer's own reasons. The
/// processor's are in cpu::stop_reason, and `processor` below is how the
/// two meet.
enum class stop_reason : std::uint8_t {
  /// Nothing has gone wrong; the machine is running.
  none,
  /// The processor stopped, so the machine has. What it refused to
  /// invent is in `machine::processor().stop()` — a machine-level record
  /// that restated it would only be able to get it wrong.
  processor,
  /// An interrupt reached a service stub that no native handler backs.
  ///
  /// PLAN.md §3's discipline rule, at the layer where it bites hardest:
  /// the DOS and BIOS call surfaces are wide, we implement the part of
  /// them this game uses, and a program that asks for the rest has to
  /// find out rather than be handed a plausible answer. The
  /// `service_call` reported alongside says which service, with what in
  /// AX, from where.
  unimplemented_service,
  /// Two devices claimed the same ports or overlapping memory windows, or
  /// more claims arrived than the machine has room for.
  ///
  /// The one reason here the emulated program cannot cause: it is a
  /// mistake in how the machine was put together, caught at the moment it
  /// is made rather than surfacing later as a device that mysteriously
  /// never answers.
  conflicting_claim,
  /// A device was asked for a configuration it does not implement — a
  /// PIT mode, a PIC init sequence, an access pattern the hardware
  /// family this project targets has never been seen to use.
  ///
  /// One layer further out than `unimplemented_service`: that one is the
  /// BIOS/DOS call surface, this one is a register a device answers for
  /// at all but refuses part of the behaviour of, exactly as
  /// `unimplemented_service` refuses part of the call surface. `at` is
  /// the port or address `device::fault()` named — device.h's
  /// `device_fault`, which is how a device reaches this without a
  /// reference back to the machine (#65).
  unimplemented_device,
  /// The program terminated itself: the PSP's INT 20h (M2-D6, #51) or
  /// INT 21h AH=4Ch (M2-D7, #52) ran. Not a failure — it is the normal,
  /// expected way a DOS program ends — but it still stops the machine,
  /// because there is no resident DOS underneath to hand control back to
  /// (this is a single-program machine, PLAN.md §3): `stop_record::
  /// exit_code` is the answer the harness and every host are waiting for.
  program_exited,
  /// A native service handler understood what was asked and declined it —
  /// INT 10h asked for a video mode, or an INT 10h AH=10h sub-function,
  /// this machine does not have (M2-D3, #48). Distinct from
  /// `unimplemented_service`: the vector *has* a handler and it ran; this
  /// is the handler's own "no" to the specific request, PLAN.md §3's rule
  /// applied at the granularity of one call rather than one vector.
  unsupported_request,
};

struct stop_record {
  stop_reason reason{stop_reason::none};

  /// The physical address or the port the reason is about; zero when it
  /// is about neither.
  std::uint32_t at{};

  /// The program's exit code, meaningful only when
  /// `reason == program_exited`. INT 20h has none of its own — DOS
  /// reports 0 for it, the value every `COMMAND.COM ERRORLEVEL` check of
  /// the era treats as "ran fine" — and AH=4Ch's is whatever the program
  /// left in AL.
  std::uint8_t exit_code{};

  friend constexpr bool operator==(const stop_record&,
                                   const stop_record&) = default;
};

/// What was asked for that nothing answers for.
enum class notice_kind : std::uint8_t {
  /// An address no region and no device claims.
  unmapped_memory_read,
  unmapped_memory_write,
  /// A write to the BIOS region, which is ROM (memory_map.h).
  rom_write,
  /// A port no device claims.
  unclaimed_port_read,
  unclaimed_port_write,
  /// A write into the video window before INT 10h AH=00h has programmed a
  /// mode (M2-D3, #48). The write still lands — the write pipeline does
  /// not know or care whether a mode is active, which is the true
  /// hardware answer (ega.h) — but a program drawing before it asked for
  /// a mode is running off-plan, and PLAN.md §3 wants that said once
  /// rather than silently accommodated.
  video_write_before_mode_set,
};

struct notice {
  notice_kind what{};

  /// The physical address, or the port number.
  std::uint32_t at{};

  /// The byte a dropped write was carrying. Zero for a read.
  std::uint8_t value{};

  /// Where the program was when it did this: the instruction being
  /// executed, at its first byte, prefixes included. The whole value of a
  /// line about an address nothing answers for is what asked.
  std::uint16_t cs{};
  std::uint16_t ip{};

  friend constexpr bool operator==(const notice&, const notice&) = default;
};

/// A device's own fault (device.h's `device_fault`), enriched with where
/// the program was — the one fact a device cannot know about itself,
/// added here the way `notice_memory`/`notice_port` add it for a touch
/// of nothing nobody answers for.
struct device_stop {
  /// The port or physical address `device::report_fault()` named.
  std::uint32_t at{};

  /// Whatever one byte the device chose to say about it (device.h).
  std::uint8_t detail{};

  std::uint16_t cs{};
  std::uint16_t ip{};

  friend constexpr bool operator==(const device_stop&,
                                   const device_stop&) = default;
};

/// How a call into the service floor ended.
enum class service_outcome : std::uint8_t {
  /// A native handler ran. The stub's IRET returns to the caller.
  handled,
  /// Nothing implements this vector, and the machine stops
  /// (stop_reason::unimplemented_service). The handler never runs and the
  /// IRET is never reached: a program that asked for a service we do not
  /// have gets no answer at all, which is the only honest one.
  unimplemented,
};

/// One call into the native BIOS/DOS layer, recorded as the stub is
/// reached and before the handler runs.
///
/// A trace record, not a problem report — `outcome` is what tells the two
/// apart, and a sink that only wants failures filters on it. Built on
/// every call whether or not anything is listening, for the same reason
/// the notice bookkeeping is: what the machine does must not depend on
/// who is watching.
struct service_call {
  /// The interrupt number the program used.
  std::uint8_t vector{};

  /// AX as the caller left it. AH selects the function in every service
  /// this layer provides, so `function()` is what a log line wants; the
  /// whole register is kept because the rest of it is the argument in
  /// about half of them.
  std::uint16_t ax{};

  /// Where the call came from: CS:IP as interrupt delivery pushed them,
  /// read back off the caller's stack rather than guessed. It is the
  /// return address — the instruction *after* the INT — because that is
  /// what the 8086 pushes (cpu/interrupts.h).
  std::uint16_t caller_cs{};
  std::uint16_t caller_ip{};

  service_outcome outcome{};

  [[nodiscard]] constexpr std::uint8_t function() const noexcept {
    return static_cast<std::uint8_t>(ax >> 8u);
  }

  friend constexpr bool operator==(const service_call&,
                                   const service_call&) = default;
};

class diagnostics {
 public:
  diagnostics() = default;
  diagnostics(const diagnostics&) = delete;
  diagnostics(diagnostics&&) = delete;
  diagnostics& operator=(const diagnostics&) = delete;
  diagnostics& operator=(diagnostics&&) = delete;

  /// Something was asked of nothing.
  ///
  /// Reported once per 4 KiB page of memory and once per port, until the
  /// next reset. A program that polls an absent card in a loop would
  /// otherwise produce a line per iteration and bury the one line that
  /// told you something — and the first touch is the one that says where
  /// the program was when it started.
  virtual void report(const notice& what) = 0;

  /// The program called the BIOS/DOS layer.
  ///
  /// Every call, with none of the first-touch filtering notices get: a
  /// service call is something the program *did*, not a symptom of
  /// something absent, and the point of the channel is that a run can be
  /// read back as the list of what the program asked its operating system
  /// for. A sink that does not want the volume drops them here, where it
  /// costs one branch, rather than the machine deciding for it.
  virtual void report(const service_call& call) = 0;

  /// The machine layer gave up.
  virtual void report(const stop_record& stop) = 0;

  /// The processor gave up, and so the machine has: `machine::stop()`
  /// reads `stop_reason::processor` from the same moment. Passed through
  /// rather than translated, because this record names the opcode.
  ///
  /// One report per stop, not two: this is the line that says what
  /// happened, and the machine's own record is there to be inspected.
  virtual void report(const cpu::stop_record& stop) = 0;

  /// A device refused something (device.h, #65). The same "one report
  /// per stop" rule as the processor's: `machine::stop()` already reads
  /// `stop_reason::unimplemented_device` and the port or address from
  /// the same moment, so this is the line that names what happened.
  virtual void report(const device_stop& stop) = 0;

 protected:
  // See cpu/bus.h: held by reference, never deleted through this type.
  ~diagnostics() = default;
};

}  // namespace amberfolio::machine
