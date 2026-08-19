// SPDX-License-Identifier: AGPL-3.0-only
//
// The service floor: the interrupt vector table, the BIOS data area, and
// the mechanism by which a native C++ handler answers an interrupt
// without the emulated program being able to tell.
//
// PLAN.md §3 says the thin DOS/BIOS layer under the game is provided by
// the emulator rather than emulated from a real ROM. This file is *how*.
// Every service M2 goes on to write — the INT 21h subset (M2-D7), the
// keyboard services (M2-D8), the INT 10h subset (M2-D3) — is a function
// installed here, so the shape of this mechanism is the shape of all of
// them.
//
// Note what this is not: it is not a seam. Seams are opt-in enhancements
// above the fidelity boundary, keyed by binary fingerprint and off by
// default (PLAN.md §5). This is the machine's own service floor, below
// that boundary and always present, because a PC without a BIOS is not a
// PC. Nothing here is toggleable and nothing here knows what program is
// running.
//
//
// The vector table is real memory
// -------------------------------
//
// Nothing about the IVT is virtual. At power-on the machine writes 256
// real four-byte far pointers at 0000:0000, and each of them points at a
// real address in the BIOS region. Interrupt delivery reads them with
// ordinary bus cycles and always has (cpu/interrupts.h): the CPU has no
// idea this layer exists.
//
// That is the whole reason it is done this way. Gold Box binaries hook
// vectors — the timer tick, the keyboard, INT 24h — and a program that
// stores four bytes at 0000:0070 has hooked INT 1Ch, full stop. There is
// no table of "vectors we own" to keep in step, no interception to
// disable, and no way for a hook to be half-applied: the native handler
// stops being reachable the instant the pointer stops pointing at it,
// *by construction*. An emulator that dispatched on the interrupt number
// instead would have to detect hooking, and detecting hooking is
// guessing.
//
// The rejected alternative was a magic opcode: put an illegal or
// otherwise reserved byte at the vector target and dispatch from the
// interpreter when it decodes one. It is cheaper — no CS compare at all
// — and it is wrong twice over. It puts a byte in memory that no real
// machine has, so a program that reads its own vector targets (which
// self-checking loaders of the era did) sees something impossible; and
// it makes the CPU know about the BIOS, which is the one thing the
// core/host split and cpu/bus.h have been careful about since M1.
//
//
// Callout by address
// ------------------
//
// Each vector points at its own one-byte stub in the BIOS region. The
// byte is 0xCF, IRET, and it is ours — written by this file, not copied
// from anything (the clean-content rule, CONTRIBUTING.md).
//
// The machine checks CS at every step boundary, before an instruction is
// fetched. That check is one comparison against a constant on the hot
// path; everything else — is the offset a stub, is a handler installed,
// which one — is inside a branch that is only taken when CS is already
// 0xF000, which nothing but a service call ever makes it. When it is a
// stub, the native handler runs and then the CPU executes the IRET byte
// itself.
//
// Letting the CPU do the return is the point. IRET pops IP, CS and FLAGS
// through the same stack code every other instruction uses, so the stack
// discipline and the restored flag word are the machine's real ones and
// not this layer's imitation of them. A handler that has to hand a flag
// back — DOS reports failure in CF — edits the flags *image on the
// stack* before returning (`set_caller_carry`), which is exactly what a
// handler written in 8086 would do and is why the offsets in
// `service::frame` are named rather than spelled 4 at the call sites.
//
//
// Vector to stub, stub to handler
// -------------------------------
//
// Both directions are arithmetic. Vector *n* gets the stub at
// `stub_base + n`, so a vector becomes an address with an add; an address
// becomes a slot index with a subtract and a range test
// (`service::stub_index`), and the slot holds the function pointer. No
// search, no allocation, nothing on the hot path but the CS compare that
// got us here. It also means the vector a stub belongs to is recoverable
// from the address alone, which is what lets an unimplemented service
// name itself in a log line.
//
// Past the 256 vector stubs sit a few *continuation* stubs. A native
// handler that has to let the emulated machine run and then carry on —
// the timer handler calls the user tick vector and then has an EOI left
// to send — claims one, sets IP to it and delivers the interrupt. The
// return address pushed is therefore the continuation, and when control
// comes back the second half of the handler runs there. It is the same
// thing a BIOS written in 8086 gets for free by having an instruction
// after its `INT 1Ch`; we need an address because our instructions are
// not in the emulated machine.
//
//
// Nothing is faked
// ----------------
//
// A vector with no handler installed still has a stub and still has an
// IVT entry — because a program is entitled to read one, and because
// "the vector points nowhere" is not a state a real machine has. What it
// does not have is a body: reaching it reports the interrupt number, AH
// and the caller's CS:IP, and stops the machine (PLAN.md §3). No
// IRET-and-hope. A service that silently returned would be discovered
// months later as a game that behaves subtly wrongly, which is the
// failure mode this project's discipline rule exists to prevent.
//
//
// Deliberately not here yet
// ------------------------
//
//   * **The services themselves.** INT 21h is M2-D7, INT 10h is M2-D3.
//     The keyboard (INT 16h, the BDA buffer, Ctrl-Break) is M2-D8 and
//     lives in keyboard.h — its handlers are `provide()`d there rather
//     than here, but the addresses they maintain are laid out in this
//     file's `bda` namespace, alongside the timer's, because this is the
//     one home the BDA has. The default timer tick is the only handler
//     installed *by this file*, kept as the proof the mechanism works
//     end to end.
//   * **The PIC.** M2-D1 (#46). The timer handler ends with a real EOI
//     write to port 20h; with no controller attached the port map finds
//     nothing there and says so once, which is the honest answer and
//     needs no edit when the 8259 arrives to claim the port.
//   * **A chain into a service with a native body.** A handler that
//     hands control to another stub gets that stub dispatched in the same
//     step, up to `service::max_chain` deep; the loop is in
//     `machine::step()` and its termination is argued there.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/machine/diagnostics.h"

namespace amberfolio::machine {

class machine;
class service_floor;

/// A native service body: what runs when the program's interrupt reaches
/// our stub.
///
/// A plain function pointer, like `cpu::handler` and for the same reason
/// — core/ carries no `<functional>` and allocates nothing (PLAN.md §4).
/// The `service_floor` is the handler's whole world: through it the
/// machine, the processor, memory and the caller's stack frame. The
/// vector is passed as well so that one function can back several
/// vectors, which the DOS layer will want.
using service_handler = void (*)(service_floor& floor, std::uint8_t vector);

/// Where the service floor lives and how it is addressed. Constants
/// rather than members: they are facts about the machine's layout, and
/// the machine has exactly one layout.
namespace service {

/// The segment the stubs live in — the bottom of the BIOS region, which
/// memory_map.h reserves and backs as ROM. Nothing else in this machine
/// executes with CS here, which is what makes the step-boundary compare
/// against it both cheap and sufficient.
inline constexpr std::uint16_t stub_segment = 0xF000;

/// Offset of the first stub. The low page of the segment is left clear
/// so that F000:0000 — a plausible value for an uninitialized far
/// pointer — is not a valid stub address.
inline constexpr std::uint16_t stub_base = 0x0100;

/// One stub per interrupt vector. All 256 of them, including the ones no
/// service will ever back: a vector that points nowhere is not a state a
/// real machine has, and a program is entitled to read the entry.
inline constexpr unsigned vector_stubs = 256;

/// Continuation stubs, for handlers that resume after letting the
/// emulated machine run. One is used today (the timer's); eight leaves
/// the M2-D services room without making this a data structure.
inline constexpr unsigned continuation_stubs = 8;

inline constexpr unsigned stub_count = vector_stubs + continuation_stubs;

/// The byte every stub is made of: IRET. Ours, written here, not taken
/// from anywhere — see the clean-content rule in CONTRIBUTING.md.
inline constexpr std::uint8_t iret_opcode = 0xCF;

/// `stub_index`'s answer when the offset is not one of ours.
inline constexpr unsigned not_a_stub = stub_count;

/// The stub address for `vector`, as an offset in `stub_segment`.
[[nodiscard]] constexpr std::uint16_t stub_offset(
    std::uint8_t vector) noexcept {
  return static_cast<std::uint16_t>(stub_base + static_cast<unsigned>(vector));
}

/// The stub address for continuation `slot`.
[[nodiscard]] constexpr std::uint16_t continuation_offset(
    unsigned slot) noexcept {
  return static_cast<std::uint16_t>(stub_base + vector_stubs + slot);
}

/// Which slot the stub at `offset` is, or `not_a_stub`. The other half
/// of the mapping, and the reason the hot path needs no search.
[[nodiscard]] constexpr unsigned stub_index(std::uint16_t offset) noexcept {
  const unsigned slot = static_cast<unsigned>(offset) - stub_base;
  return (offset >= stub_base && slot < stub_count) ? slot : not_a_stub;
}

/// How many stubs one step may dispatch. A handler that hands control to
/// another stub is dispatched again in the same step, because the second
/// stub's IRET would otherwise execute without its handler having run.
/// Two is the deepest anything goes today (the timer, into an unhooked
/// INT 1Ch); four is the bound, and it is a bound rather than a
/// correctness argument because a cycle between two handlers would
/// otherwise be an emulator hang instead of a bug report.
inline constexpr unsigned max_chain = 4;

/// The interrupt frame, as offsets from SP at the moment a stub is
/// reached. Delivery pushes FLAGS, then CS, then IP (cpu/interrupts.h),
/// so IP is on top and the flags image — the one a handler edits to
/// report CF to its caller — is six bytes down, at +4.
namespace frame {

inline constexpr std::uint16_t return_ip = 0;
inline constexpr std::uint16_t return_cs = 2;
inline constexpr std::uint16_t flags = 4;
inline constexpr std::uint16_t size = 6;

}  // namespace frame

/// The system timer interrupt: IRQ0 as the PC wires it, and the vector
/// this file's one real handler backs.
inline constexpr std::uint8_t timer_vector = 0x08;

/// The video BIOS: PLAN.md §3's INT 10h subset, "mode set, palette
/// register set" (M2-D3, #48, int10.h). Named here alongside
/// `timer_vector` because both are facts about which vector a service
/// answers, even though — unlike the timer — this file installs no
/// handler for it; `install_int10` does that from its own file.
inline constexpr std::uint8_t video_vector = 0x10;

/// The user tick vector the timer handler calls. Its default body is an
/// IRET and nothing else, which is what makes it the era's standard place
/// to hang a periodic routine — and Gold Box code does.
inline constexpr std::uint8_t user_tick_vector = 0x1C;

/// The continuation the default timer handler resumes at, once INT 1Ch
/// has returned. A constant rather than an allocation, so the address is
/// the same on both sides without either keeping state.
inline constexpr unsigned timer_continuation = 0;

}  // namespace service

/// The BIOS data area at 0040:0000, and the fields in it this machine
/// maintains at their real addresses.
///
/// Real memory, again, and for the same reason: programs of the era read
/// it directly rather than paying for an INT, and one that reads the tick
/// count out of 40:6C must see the number the timer handler put there.
namespace bda {

inline constexpr std::uint16_t segment = 0x0040;

/// 256 bytes, 00400-004FF physical. Cleared and laid out at power-on,
/// which is what the real machine's self test does.
inline constexpr std::uint16_t size = 0x0100;

/// 40:13 — conventional memory in KiB, the number INT 12h reports. A
/// fact of the memory map (memory_map.h), so it is written from it.
inline constexpr std::uint16_t memory_size_kb = 0x0013;

/// 40:6C — the 32-bit tick count, incremented by the timer interrupt at
/// the PC's 18.2 Hz. The field this issue's exit criterion reads.
inline constexpr std::uint16_t timer_ticks = 0x006C;

/// 40:70 — the midnight rollover flag: the tick count wrapped, and INT
/// 1Ah reports and clears it so a program can notice a day boundary.
inline constexpr std::uint16_t timer_rollover = 0x0070;

/// Ticks in twenty-four hours, at which the count returns to zero. The
/// number is 24 * 60 * 60 * 1193182 / 65536, rounded as the hardware
/// rounds it, and it is quoted here as the constant the BIOS compares
/// against rather than recomputed.
inline constexpr std::uint32_t ticks_per_day = 0x1800B0;

/// 40:17 — the shift/lock status byte the keyboard service (M2-D8, #53)
/// maintains: which of shift/ctrl/alt are currently held and which of
/// caps/num/scroll lock are latched. Real memory, not a value INT 16h
/// AH=02h computes on demand, because programs of the era read this byte
/// directly instead of paying for the INT.
inline constexpr std::uint16_t keyboard_shift_flags = 0x0017;

/// 40:1A / 40:1C — the circular keystroke buffer's read and write
/// pointers, and 40:1E — the buffer itself: sixteen words running to
/// 40:3E, AL (ASCII, or 0) in the low byte and AH (scan code) in the
/// high one. The pointers are segment offsets, not slot indices — a
/// program that walks the buffer by hand, and some did, expects to find
/// an address there, not a count.
///
/// Sixteen slots hold fifteen keystrokes: head == tail has to mean
/// "empty" rather than be ambiguous with "full", so one slot is always
/// kept open. That is not this emulator rationing itself — the real
/// BIOS's buffer has exactly the same limit for exactly the same reason.
inline constexpr std::uint16_t keyboard_buffer_head = 0x001A;
inline constexpr std::uint16_t keyboard_buffer_tail = 0x001C;
inline constexpr std::uint16_t keyboard_buffer = 0x001E;
inline constexpr unsigned keyboard_buffer_slots = 16;
inline constexpr std::uint16_t keyboard_buffer_end =
    static_cast<std::uint16_t>(keyboard_buffer + keyboard_buffer_slots * 2u);

/// 40:71 — the Ctrl-Break flag. Bit 7 set is the keyboard service's own
/// record that Ctrl-Break happened, independent of what INT 1Bh's
/// handler (hooked or default) does about it; DOS's Ctrl-Break checking
/// (M2-D7, #52) reads this rather than re-deriving it.
inline constexpr std::uint16_t keyboard_break_flag = 0x0071;
inline constexpr std::uint8_t keyboard_break_flag_set = 0x80;
/// 40:71 — the Ctrl-Break flag: bit 7 set means Ctrl-Break has been seen
/// and not yet acted on. A real, documented BIOS Data Area byte (Ralf
/// Brown's Interrupt List and any BIOS technical reference describe it at
/// this address; nothing about the address or the bit comes from a Gold
/// Box binary).
///
/// M2-D8 (#53) is what ever sets it — the translated Ctrl-Break key event
/// raises INT 1Bh and sets this flag, on its own schedule. Until #53
/// lands nothing sets it, so the check M2-D7 (#52) makes against it is
/// dormant, and that is the honest state of a machine with no keyboard
/// service yet: the mechanism is real, the flag is just never raised.
inline constexpr std::uint16_t break_flag = 0x0071;

/// The bit `break_flag` actually uses. The other seven are unused by
/// anything this machine implements.
inline constexpr std::uint8_t break_flag_bit = 0x80;

}  // namespace bda

/// The interrupt controller's acknowledge, until M2-D1 (#46) brings a
/// real 8259. Here rather than guessed at the call site: the timer
/// handler must end with an EOI or a real controller would deliver
/// nothing else, and writing it now means the day the PIC claims port
/// 20h nothing in this file changes.
namespace pic {

inline constexpr std::uint16_t master_command_port = 0x20;
inline constexpr std::uint8_t end_of_interrupt = 0x20;

}  // namespace pic

/// The vector table, the BDA, the stubs and the handlers behind them.
///
/// Owned by the machine, which is also the only caller of `reset()` and
/// `call()`. A service layer (M2-D7/D8/D3) reaches it through
/// `machine::services()` and installs handlers with `provide()`.
class service_floor {
 public:
  /// `box` must outlive this; `log` may be null, and the floor does the
  /// same thing either way — the trace record is built on every call
  /// whether or not there is anywhere to send it, so a run with a sink
  /// attached and a run without one are the same run.
  ///
  /// The default handlers are installed here, before the machine exists
  /// enough to be touched: nothing in this constructor reads `box`.
  explicit service_floor(machine& box, diagnostics* log) noexcept;

  /// Back `vector` with `handler`. Overwrites whatever was there, which
  /// is how a service layer replaces a default.
  ///
  /// Wiring, not state: the handler table survives `reset()` for the same
  /// reason attached devices do. What a RESET rebuilds is the memory —
  /// the vectors, the stubs and the BDA.
  void provide(std::uint8_t vector, service_handler handler);

  /// Claim continuation `slot` for `vector` and answer the offset to set
  /// IP to. `service::timer_continuation` is the only slot spoken for.
  std::uint16_t provide_continuation(unsigned slot, std::uint8_t vector,
                                     service_handler handler);

  /// Power-on: write the 256 vector entries, the stub bytes and the BDA
  /// into memory. This is the self test, not the RESET line — the line
  /// leaves RAM alone (machine::reset()) and what puts the table back is
  /// the ROM code that runs afterwards. We are that code.
  ///
  /// Every write goes through `memory_map::ram()` rather than a bus
  /// cycle, because the BIOS region is `region::rom` and refuses a
  /// program's writes (memory_map.h). This is the machine writing its own
  /// memory, which is the case that back door exists for.
  ///
  /// Does nothing under `memory_layout::flat`, which is a megabyte of RAM
  /// and no PC at all: it has no BIOS region to put stubs in, and the
  /// programs that run on it are self-contained (tests/programs).
  void reset();

  /// Whether this machine has a service floor at all — see `reset()`.
  [[nodiscard]] bool enabled() const noexcept;

  /// Run the handler for stub `slot`, which must be a valid index.
  /// Reports the call and answers whether anything backed it; the machine
  /// turns `unimplemented` into a stop, because stopping is the machine's
  /// to do.
  service_outcome call(unsigned slot);

  // --- The caller's frame ---------------------------------------------
  //
  // Named accessors over `service::frame`, so that a handler reporting an
  // error says what it means instead of writing to SP+4.

  [[nodiscard]] std::uint16_t caller_ip();
  [[nodiscard]] std::uint16_t caller_cs();

  /// The flags image the stub's IRET will pop. Reading it is how a
  /// handler sees the flags its caller had; writing it is how a handler
  /// hands flags back.
  [[nodiscard]] std::uint16_t caller_flags();
  void set_caller_flags(std::uint16_t value);

  /// DOS's error convention, which every INT 21h function in M2-D7 uses:
  /// CF set on failure with the code in AX, CF clear on success. Edited
  /// on the stack rather than in the register file, because IRET is about
  /// to overwrite the register file with what is on the stack.
  void set_caller_carry(bool failed);

  /// The machine this floor serves. A handler's way to registers, memory
  /// and ports.
  [[nodiscard]] machine& box() noexcept { return *box_; }

 private:
  /// One word of the caller's interrupt frame, by `service::frame`
  /// offset. Through the bus, because the stack is memory like any other
  /// and a handler must see it exactly as the program does.
  [[nodiscard]] std::uint16_t frame_word(std::uint16_t at);
  void set_frame_word(std::uint16_t at, std::uint16_t value);

  /// What a stub means. A null handler is the whole of "unimplemented":
  /// there is no default body to accidentally leave in place, so a vector
  /// gets a real answer only when somebody has written one.
  struct stub {
    service_handler handler{nullptr};
    std::uint8_t vector{};
  };

  machine* box_;
  diagnostics* log_;
  std::array<stub, service::stub_count> stubs_{};
};

}  // namespace amberfolio::machine
