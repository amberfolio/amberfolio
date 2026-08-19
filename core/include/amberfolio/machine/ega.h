// SPDX-License-Identifier: AGPL-3.0-only
//
// The EGA: four 64 KiB bit planes behind one 64 KiB address window, and
// the sequencer / graphics-controller registers PLAN.md §3 names — "map
// mask, set/reset, data rotate/ALU, read map, write modes, bit mask,
// latches." This is M2-D2 (#47), and PLAN.md §8 names it the headline
// fidelity risk of the whole milestone: "EGA latch/ALU subtleties." The
// planar write pipeline below is the reason why — it is a small circuit
// with several stages, every stage has an edge case, and Pool of Radiance
// pokes all of them through its own graphics primitives.
//
// What is claimed, and what is deliberately not
// -----------------------------------------------
//
// The device claims 0xA0000-0xAFFFF: one 64 KiB window, four planes
// living behind it at the same offsets. It does **not** claim
// 0xB0000-0xBFFFF, on purpose. That is the other half of memory_map.h's
// `video_window`, and on real EGA hardware mode 0Dh (320x200x16, the only
// mode v1 targets — PLAN.md §9) does not map anything there; a card in
// that mode simply does not answer at B0000. Leaving it unclaimed is not
// an oversight to fix later: memory_map.h already logs a touch of
// anything nobody claimed, once per page, and that is exactly the
// "logging stub" B0000-BFFFF wants. Claiming it here and logging it
// ourselves would be the same behaviour written twice.
//
// What is in scope is the register subset PLAN.md §3 names, and nothing
// past it. Palette, the attribute controller, the CRTC, text modes and
// every resolution but 320x200x16, rendering to a framebuffer, and INT
// 10h are M2-D3 (#48) or explicitly out of v1 (PLAN.md §9) — this device
// answers bus cycles into planar VRAM and nothing else.
//
//
// The write pipeline
// -------------------
//
// The stage order is CPU byte -> rotate -> set/reset substitution ->
// ALU op against the latches -> bit-mask select -> map-mask gate ->
// planes. `write_pixel()` in the .cpp is a line-by-line walkthrough of
// that pipeline with the real hardware reasoning at each stage — read it
// there before touching the ALU code. What belongs here is the shape of
// the state each stage reads:
//
//   * **Rotate** reads the Data Rotate register's low 3 bits (a count,
//     0-7) and rotates the raw CPU byte right by that many places. This
//     happens unconditionally, before anything plane-specific, because
//     on real hardware the rotator sits on the CPU data bus itself and
//     has not seen a plane number yet.
//   * **Set/reset substitution** is a 2-to-1 mux, once per plane, gated
//     by the Enable Set/Reset register: a plane whose enable bit is 0
//     takes the rotated CPU byte; a plane whose bit is 1 takes an
//     expanded single bit instead — 0xFF or 0x00 — repeated across all
//     eight bit positions. In write mode 0 that bit comes from the
//     Set/Reset register; in write mode 2 (color expand) it comes from
//     the CPU byte itself, one bit per plane, bit *n* for plane *n*. The
//     mux does not know which write mode it is in beyond that one
//     difference — Enable Set/Reset gates both modes identically, which
//     is a real and easy-to-miss hardware fact: a program that leaves
//     Enable Set/Reset at 0x0 while trying to color-expand in write mode
//     2 gets the rotated CPU byte on every plane and no color expansion
//     at all, because nothing told the mux to take the expanded input.
//   * **The ALU** combines that per-plane source with the plane's latch
//     — loaded by the CPU's most recent read of *any* address in this
//     window, not this one — under the Data Rotate register's function
//     select (bits 3-4): 0 copy/replace, 1 AND, 2 OR, 3 XOR.
//   * **The bit mask** (register 08h) selects, bit by bit, between the
//     ALU's result and the latch: a mask bit of 1 takes the ALU's bit, a
//     mask bit of 0 leaves the latch's bit untouched. 0xFF is "the ALU
//     result, unmasked"; 0x00 is "nothing changes here no matter what the
//     ALU computed," which is the whole reason this stage exists rather
//     than being folded into the ALU step.
//   * **The map mask** (sequencer index 02h) gates the *plane*, not the
//     bit: a plane whose map-mask bit is 0 is never written, whatever the
//     first four stages computed for it.
//
// Write mode 1 (latch copy) and write mode 2 (color expand) sit beside
// that pipeline rather than as a fifth and sixth stage bolted onto the
// end of it:
//
//   * **Mode 1** writes the latch content straight to the planes the map
//     mask selects. The CPU byte is not read at all — not rotated, not
//     substituted, not combined with anything — which is what makes it
//     the fast VRAM-to-VRAM copy idiom: read a byte (which loads all four
//     latches) at the source address, then write mode 1 at the
//     destination replays exactly what was read, on every plane the
//     source had.
//   * **Mode 2** is the set/reset mux's other input, described above; the
//     ALU and bit-mask stages still run afterward exactly as in mode 0.
//
//
// Reads and the latches
// ----------------------
//
// Every CPU read of this window — regardless of read mode — loads all
// four latches from the addressed offset, one byte per plane, and that
// is the *only* place a latch ever changes; writes never touch it. Read
// mode 0 (register 05h bit 3 clear) then answers with the plane the Read
// Map Select register (04h) names. Read mode 1 (bit 3 set) answers with
// an eight-bit color-compare result instead: bit *i* of the answer is 1
// when, for every plane the Color Don't Care register (07h) says to
// consider, that plane's bit *i* equals the Color Compare register's
// (02h) bit for that plane.
//
// Color Don't Care is the one register in this subset whose name is
// backwards from what it does, and it is worth writing down plainly
// because it is a documented, well-known point of confusion: a bit set
// to **1** means that plane **is** considered — it can disqualify a
// match — and a bit of **0** means the plane is genuinely ignored. The
// register was clearly named from the angle of "which planes do I not
// care about, if I clear their bits," but read as a boolean per plane it
// says the opposite of what its name suggests. `color_compare_result()` in
// the .cpp has the truth table; this header just flags the trap.
//
// Because a word access on this bus is two byte cycles (cpu/bus.h — the
// 8088's data bus is eight bits wide, and there is no `read_memory16` on
// `device`), the two-byte read/write ordering falls out of that rule
// with no code of this device's own: a 16-bit read reloads the latches
// twice, once per byte, so after it they hold the *second* address's
// planes; a 16-bit write mode 1 copy therefore writes the same
// already-stale latch content to both destination bytes if the program
// did not re-read between them. That is real EGA behaviour, not a bug in
// either this device or the bus, and `ega_test.cpp` has it as a test
// rather than as anything coded here.
//
//
// Registers this device does not implement
// -------------------------------------------
//
// The sequencer (3C4h/3C5h) only gives register 02h — the map mask — real
// behaviour. Indices 00h (Reset), 01h (Clocking Mode), 03h (Character Map
// Select) and 04h (Memory Mode) are the mode-set path's (M2-D3, #48), and
// a write to any of them is accepted and stored without complaint: a
// program setting mode 0Dh legitimately touches all five sequencer
// registers, and refusing four of them would make mode set itself
// impossible to write. An index past 04h does not exist on real
// sequencer hardware, and a program that selects one is doing something
// this device has no model for.
//
// The graphics controller (3CEh/3CFh) implements all nine of its
// registers (00h-08h) for real, including the ones this issue does not
// otherwise act on — the Miscellaneous register (06h) is stored but never
// consulted, because the memory window it would otherwise steer (A0000
// vs. B0000/B8000 vs. odd/even addressing) is fixed by this device's
// claim rather than programmable, and a game running mode 0Dh does not
// depend on it being honoured. The one field this device does refuse is
// write mode 3 in the Mode register (05h): the EGA only has write modes
// 0-2, and a program that asks for the VGA's fourth mode is asking for
// hardware this machine does not have.
//
// Both refusals are "unimplemented register, loud log line, clean stop" —
// PLAN.md §3's rule and CLAUDE.md's non-negotiable one, "an unimplemented
// service, register, or port is a loud log line and a clean stop." What
// that means concretely here is narrower than it sounds, and the gap is
// worth being honest about: `device.h` (#42, settled) gives a device no
// channel back to the machine's own `stop()` from inside a bus cycle — no
// diagnostics pointer, no back-reference, nothing a `write_port` override
// can call. So "stop" here is this device's own stop, not the machine's:
// `halted()` goes true, every later cycle into this device (memory or
// port, any address) answers inertly — `open_bus_value` for a read,
// nothing for a write — and `halt()` names what tripped it, for a test or
// a future host-side inspector to read back. It is loud in the sense that
// it is a state nothing hides, not in the sense of a message printed
// anywhere; wiring a real host-visible line through `machine::diagnostics`
// needs a way for *some* device to ask the machine to stop, which does
// not exist yet and is not this issue's to invent. #46 (M2-D1, the PIT
// and the 8259) wants an unmistakably identical mechanism — "log-and-stop
// on a mode the family has never been seen to use" — so whichever of
// M2-D1 or a later framework issue builds that channel first, this device
// is the second, not the first, caller.
//
//
// Reset
// -----
//
// The planes keep what they held, on purpose, mirroring `machine::reset()`
// not clearing RAM: RESET on real hardware does not clear VRAM either, and
// a device whose own reset wiped 256 KiB of picture would be inventing
// behaviour the hardware does not have. What does go back to power-on
// state is every register, every latch, and the halt record — the same
// "wiring survives, state does not" split `machine::reset()` documents for
// attached devices in general.
//
//
// Memory footprint
// -----------------
//
// Four 64 KiB planes is 256 KiB of `std::array` inside this object —
// `core/` forbids dynamic allocation (PLAN.md §4), so, like `machine`'s
// own megabyte, this is not a type to put on a stack. Every test that
// constructs one heap-allocates it, the same way `tests/core/machine/
// machine_test.cpp`'s `rig` does for `machine` itself.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/device.h"

namespace amberfolio::machine {

/// The EGA video device: planar VRAM behind the sequencer and graphics
/// controller register subset PLAN.md §3 names. See this file's top
/// comment for the write pipeline, the read/latch behaviour and what is
/// deliberately unimplemented.
class ega final : public device {
 public:
  /// One plane, and how many of them there are. Four is not a tunable —
  /// it is what 320x200x16 (mode 0Dh) means: four bit planes combine to
  /// address 16 colors per pixel, one bit per plane.
  static constexpr std::size_t plane_count = 4;
  static constexpr std::size_t plane_size = 0x10000;  // 64 KiB.

  ega() = default;

  [[nodiscard]] claims claimed() const noexcept override;
  void reset() override;

  [[nodiscard]] std::uint8_t read_memory(std::uint32_t address) override;
  void write_memory(std::uint32_t address, std::uint8_t value) override;
  [[nodiscard]] std::uint8_t read_port(std::uint16_t port) override;
  void write_port(std::uint16_t port, std::uint8_t value) override;

  // --- Introspection -----------------------------------------------
  //
  // Read-only windows into device state that no bus cycle exposes
  // directly, for tests and for a future renderer (M2-D3, #48) that will
  // want to read planes without going through the CPU-facing pipeline.

  /// One byte of one plane, bypassing the read pipeline entirely — no
  /// latch load, no read-mode logic. What a test uses to check the write
  /// pipeline landed the byte it computed; what a renderer will use to
  /// turn planes into pixels.
  [[nodiscard]] std::uint8_t plane_byte(unsigned plane,
                                        std::uint16_t offset) const noexcept {
    return planes_[plane][offset];
  }

  /// The four latches as they stand right now — whatever the most recent
  /// CPU read (of any read mode) loaded them with.
  [[nodiscard]] std::array<std::uint8_t, plane_count> latches() const noexcept {
    return latches_;
  }

  /// The map mask's live value: which planes the next write reaches.
  [[nodiscard]] std::uint8_t map_mask() const noexcept {
    return seq_regs_[sequencer_map_mask_index] & 0x0Fu;
  }

  // --- The halt record -----------------------------------------------
  //
  // See this file's top comment, "Registers this device does not
  // implement," for what this is and, as importantly, what it is not.

  enum class halt_reason : std::uint8_t {
    /// Not halted.
    none,
    /// A sequencer data-port cycle (3C5h) named an index past 04h.
    sequencer_index,
    /// A graphics-controller data-port cycle (3CFh) named an index past
    /// 08h.
    gc_index,
    /// The Mode register (GC index 05h) asked for write mode 3, which
    /// this device — an EGA, not a VGA — does not have.
    write_mode,
  };

  struct halt_record {
    halt_reason reason{halt_reason::none};
    /// The port the offending cycle was on.
    std::uint16_t port{};
    /// The byte the cycle carried — the out-of-range index itself for an
    /// index fault, the whole Mode register byte for a write-mode fault.
    std::uint8_t value{};

    friend bool operator==(const halt_record&, const halt_record&) = default;
  };

  [[nodiscard]] bool halted() const noexcept {
    return halt_.reason != halt_reason::none;
  }
  [[nodiscard]] const halt_record& halt() const noexcept { return halt_; }

 private:
  // --- The bus cycle this device claims -------------------------------

  static constexpr memory_window vram_window{.first = 0xA0000, .last = 0xAFFFF};

  static constexpr std::uint16_t sequencer_index_port = 0x3C4;
  static constexpr std::uint16_t sequencer_data_port = 0x3C5;
  static constexpr std::uint16_t graphics_index_port = 0x3CE;
  static constexpr std::uint16_t graphics_data_port = 0x3CF;

  static constexpr port_range sequencer_ports{.first = sequencer_index_port,
                                              .last = sequencer_data_port};
  static constexpr port_range graphics_ports{.first = graphics_index_port,
                                             .last = graphics_data_port};

  static constexpr std::array<memory_window, 1> windows_{vram_window};
  static constexpr std::array<port_range, 2> ports_{sequencer_ports,
                                                    graphics_ports};

  // --- Sequencer (3C4h index / 3C5h data) -----------------------------

  static constexpr unsigned sequencer_register_count = 5;
  static constexpr unsigned sequencer_map_mask_index = 2;

  std::uint8_t seq_index_{};
  std::array<std::uint8_t, sequencer_register_count> seq_regs_{};

  // --- Graphics controller (3CEh index / 3CFh data) -------------------
  //
  // One named byte per register rather than an array indexed by register
  // number: every one of the nine is read on almost every write cycle
  // (the pipeline touches six of them per byte written), and a name says
  // what a `gc_regs_[3]` would need this comment to say at every call
  // site instead.

  static constexpr unsigned gc_register_count = 9;

  std::uint8_t gc_index_{};
  std::uint8_t set_reset_{};         // 00h.
  std::uint8_t enable_set_reset_{};  // 01h.
  std::uint8_t color_compare_{};     // 02h.
  std::uint8_t data_rotate_{};       // 03h: rotate count + function select.
  std::uint8_t read_map_select_{};   // 04h.
  std::uint8_t mode_{};              // 05h: write mode + read mode.
  std::uint8_t misc_{};              // 06h: stored, never consulted.
  std::uint8_t color_dont_care_{};   // 07h.
  std::uint8_t bit_mask_{0xFF};      // 08h.

  // --- VRAM and the latches --------------------------------------------

  /// 256 KiB. See this file's top comment, "Memory footprint."
  std::array<std::array<std::uint8_t, plane_size>, plane_count> planes_{};
  std::array<std::uint8_t, plane_count> latches_{};

  halt_record halt_{};

  // --- The pipeline, and the register plumbing around it --------------

  void write_pixel(std::uint16_t offset, std::uint8_t cpu_byte);
  [[nodiscard]] std::uint8_t read_pixel(std::uint16_t offset);
  [[nodiscard]] std::uint8_t color_compare_result() const noexcept;

  void write_sequencer_data(std::uint8_t value);
  [[nodiscard]] std::uint8_t read_sequencer_data();
  void write_graphics_data(std::uint8_t value);
  [[nodiscard]] std::uint8_t read_graphics_data();

  void halt_now(halt_reason reason, std::uint16_t port,
                std::uint8_t value) noexcept;
};

}  // namespace amberfolio::machine
