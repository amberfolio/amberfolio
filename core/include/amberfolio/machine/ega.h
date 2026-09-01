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
// past it. The CRTC, text modes and every resolution but 320x200x16 stay
// out (PLAN.md §9); rendering to a framebuffer and INT 10h are their own
// files (renderer.h, int10.h) that read this device rather than becoming
// part of it. The palette and the attribute controller *are* this file's
// business, though, and the rest of this comment explains why, alongside
// the write pipeline the device was first built for.
//
//
// The attribute controller (3C0h/3C1h, and 3DAh for the flip-flop)
// --------------------------------------------------------------------
//
// This is a second chip on a real EGA card — the write pipeline above
// belongs to the sequencer and the graphics controller, and the palette
// belongs to the attribute controller, a genuinely different piece of
// silicon with its own index/data protocol. It lives in this file anyway,
// on M2-D3's (#48) own instruction: "you are extending this device, not
// writing a second one." A second `device` subclass here would answer
// bus cycles for what is, from the machine's point of view, the same
// card, and would buy nothing but a second claims() list and a second
// reset() to keep in step with the first.
//
// 3C0h is one port with two roles, told apart by an internal flip-flop
// rather than by which port is touched: the first write after the
// flip-flop resets loads the **index** register (which of the
// controller's internal registers the next write means) and flips to
// "expect data"; the next write loads that register and flips back to
// "expect index". Two writes, one port, and the flip-flop is the only
// thing that says which write means which — real hardware, not a
// simplification. 3C1h is a second, always-data port for reading back
// whichever register the index currently names, and does not touch the
// flip-flop at all.
//
// The flip-flop's reset is a *read* of 3DAh (the input status register),
// and only that: nothing else in this subset resets it, which is exactly
// what M2-D3 asks this file to implement.
//
//
// The raster, and why 3DAh stopped being a constant
// -------------------------------------------------
//
// M2 answered 3DAh with zero — "not in retrace", always. It was honest
// about being a stub and it was a trap: a program that waits for
// vertical retrace before drawing waits by polling this register until
// bit 3 changes, and against a constant it never does. Nothing in M2's
// scope polled it, so nothing found the trap; a game's title sequence is
// exactly the kind of thing that would, and #88 exists because "the
// machine hangs and nothing refused anything" is the one failure mode
// this project's discipline cannot catch by refusing to guess.
//
// So the two timing bits are computed, from virtual time, as a formula
// against the frame period — the same shape pit.h's counters have, and
// for the same reason: this device does not tick, it answers where the
// beam would be at `machine::time()`. Which makes it deterministic and
// replayable; a toggle that flipped on every read would answer "yes,
// eventually" to every poll and mean nothing.
//
// The geometry is the mode's own, and every number in `raster` below is
// derived rather than chosen: 262 scan lines at the 15.7 kHz line rate
// is the 60 Hz vertical rate mode 0Dh runs at, of which 200 are
// displayed; 455 dot times per line is the 14.318 MHz colour-burst clock
// halved for a 320-pixel line and divided by that same line rate, of
// which 320 are displayed.
//
// What is *not* modelled is where inside the 62 blanked lines the
// retrace pulse itself begins — the front-porch split is card-specific
// and this device would be inventing it. Bit 3 is therefore set for the
// whole vertical blanking interval, which contains the pulse. Every
// program that polls it is asking "is it safe to write to display memory
// now", and to that question this answers correctly, if slightly
// generously.
//
// Sixteen palette registers, index 00h-0Fh, each a 6-bit EGA colour code
// (masked on the way in, because the real register is six bits wide and
// that is a hardware fact, not a guess). Overscan (index 11h) is stored
// alongside them because AH=10h AL=01h and the mode-set path both want
// somewhere to put it, but nothing reads it back out: straight planar
// composition for mode 0Dh has no border to draw (the renderer's own
// comment says so), so it is exactly as inert as the graphics
// controller's Miscellaneous register above — present because a real
// program can set it, consulted by nothing because nothing in this
// machine's mode 0Dh needs it consulted. Every other attribute-controller
// index (10h Mode Control, 12h Color Plane Enable, 13h Horizontal Pixel
// Panning, 14h Color Select) is exactly the CRTC-adjacent hardware this
// issue rules out — "no CRTC, no panning, no split screen" — and gets
// the same device-local halt the sequencer and graphics controller give
// an index past their own range, for the reason given below under
// "Registers this device does not implement": there, and not here,
// because it is one mechanism, not two.
//
//
// The palette codes, mapped to RGB once
// ---------------------------------------
//
// `ega_color_table` is PLAN.md §4's "indexed framebuffer plus a 16-entry
// RGB palette" one step upstream: the renderer looks up each of the
// sixteen *palette registers'* 6-bit values in this 64-entry table to
// fill the framebuffer's own palette, once a frame. The table is a
// settled fact about the hardware, computed once as `constexpr` rather
// than carried as 64 magic numbers.
//
// **Which hardware, though, is a decision, and this machine makes it.**
// The card puts six colour wires on its connector — a primary and a
// secondary bit for red, green and blue — and what those wires *mean* is
// the display's answer, not the card's. On the Enhanced Color Display,
// all six arrive and each channel takes one of four levels: 64 colours.
// On the 200-line **Color Display**, only four pins are colour at all —
// red, green, blue and intensity — and the card's secondary green (bit 4)
// is the one wired to intensity. Bits 5 and 3 reach nothing. Sixteen
// colours, the CGA sixteen, and the two spare bits are simply not
// connected.
//
// This machine is the second one, because mode 0Dh is a 200-line mode and
// PLAN.md §3 scopes the video to it: 320x200, sixteen colours, on the
// display a 1988 PC owner had in front of it. The consequence is visible
// and it is the point — a program that writes `10h` into a palette
// register is asking for dark grey (intensity, no colour), not for the
// dark green that the same code means on an ECD, and this game asks for
// exactly that when it darkens a battlefield. Rendering it as green was
// this table's bug, not the program's.
//
// So: bits 2/1/0 are red, green and blue; bit 4 is intensity; a channel
// is 0x00 or 0xAA without intensity and 0x55 or 0xFF with it. Colour 6
// is the single irregularity — the display makes RGBI 0110 brown rather
// than the olive the formula gives, which is why every CGA and EGA colour
// chart in print shows brown there. All of that is documented, published
// hardware behaviour, and facts about published hardware are a thing this
// project's clean-content rule explicitly allows (CONTRIBUTING.md).
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
// The attribute controller gives real behaviour to indices 00h-0Fh (the
// palette) and stores 11h (overscan) inertly, for the reasons given
// above; every other index — 10h, 12h, 13h, 14h — gets the third member
// of the same refusal `halt_reason` gives the sequencer and the graphics
// controller, because it is the identical situation: a program asking
// this card to do something a real EGA's attribute controller could do
// but this issue's scope (and PLAN.md §9) does not cover.
//
// All three refusals are "unimplemented register, loud log line, clean
// stop" — PLAN.md §3's rule and CLAUDE.md's non-negotiable one, "an
// unimplemented service, register, or port is a loud log line and a
// clean stop." What
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
// What does clear the buffer is the machine's self test, immediately
// afterwards: it opens the map mask and writes zeroes over the whole
// window through the pipeline below, then puts the two sequencer
// registers back (service_floor.cpp's `program_hardware()`). That is
// where a real machine clears it too — inside the mode set its POST
// performs — and the split is worth keeping straight, because both halves
// are load-bearing. A card that wiped its own planes on RESET would be
// inventing hardware behaviour; a machine whose warm boot came up still
// showing the previous run's picture would be a bug no host can fix from
// its side, since by then the stale pixels are in the published frame and
// look exactly like pixels the new run drew.
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

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/device.h"
#include "amberfolio/machine/platform.h"

namespace amberfolio::machine {

class machine;

/// One channel of an RGBI colour, at the two levels a four-wire display
/// drives: 0xAA or 0x00 without the intensity line, 0xFF or 0x55 with it.
/// See this file's top comment, "The palette codes, mapped to RGB once."
[[nodiscard]] constexpr std::uint8_t rgbi_channel(bool primary,
                                                  bool intense) noexcept {
  if (primary) {
    return intense ? std::uint8_t{0xFFu} : std::uint8_t{0xAAu};
  }
  return intense ? std::uint8_t{0x55u} : std::uint8_t{0x00u};
}

/// One of the sixteen colours a 200-line Color Display produces, from the
/// four wires that reach it. The one irregularity is colour 6: RGBI 0110
/// would be olive by the formula, and the display makes it brown, which
/// is why every CGA/EGA colour chart shows brown there.
[[nodiscard]] constexpr rgb rgbi_color(unsigned index) noexcept {
  const bool intense = (index & 0x08u) != 0;
  const rgb plain{
      .red = rgbi_channel((index & 0x04u) != 0, intense),
      .green = rgbi_channel((index & 0x02u) != 0, intense),
      .blue = rgbi_channel((index & 0x01u) != 0, intense),
  };
  if (index == 6) {
    return rgb{.red = plain.red, .green = 0x55u, .blue = plain.blue};
  }
  return plain;
}

/// One 6-bit EGA colour code, translated to RGB the way the connector
/// this machine's display hangs off does it. Bits 2/1/0 are primary red,
/// green and blue; bit 4 — secondary green on the card, wired to the
/// display's intensity pin — is intensity. Bits 5 and 3 reach no pin and
/// change nothing. See this file's top comment.
[[nodiscard]] constexpr rgb ega_color(std::uint8_t code) noexcept {
  return rgbi_color((code & 0x07u) | ((code & 0x10u) != 0 ? 0x08u : 0x00u));
}

/// Every one of the 64 codes, translated once. "Map the 64-colour EGA
/// space to RGB once, as the settled fact table it is" — the issue's own
/// words for exactly this.
[[nodiscard]] constexpr std::array<rgb, 64> make_ega_color_table() noexcept {
  std::array<rgb, 64> table{};
  for (unsigned code = 0; code < table.size(); ++code) {
    table[code] = ega_color(static_cast<std::uint8_t>(code));
  }
  return table;
}

inline constexpr std::array<rgb, 64> ega_color_table = make_ega_color_table();

/// The EGA video device: planar VRAM behind the sequencer and graphics
/// controller register subset PLAN.md §3 names, plus the attribute
/// controller's palette (M2-D3, #48). See this file's top comment for the
/// write pipeline, the palette, the read/latch behaviour and what is
/// deliberately unimplemented.
class ega final : public device {
 public:
  /// One plane, and how many of them there are. Four is not a tunable —
  /// it is what 320x200x16 (mode 0Dh) means: four bit planes combine to
  /// address 16 colors per pixel, one bit per plane.
  static constexpr std::size_t plane_count = 4;
  static constexpr std::size_t plane_size = 0x10000;  // 64 KiB.

  /// Sixteen palette registers — mode 0Dh addresses 16 colors per pixel,
  /// one per combination of the four planes' bits (renderer.h).
  static constexpr unsigned palette_register_count = 16;

  /// The attribute controller index that names the overscan register.
  static constexpr unsigned attribute_overscan_index = 0x11;

  /// The vertical rate this card runs at, as a period in PIT input ticks
  /// — 60 Hz, truncated, so a frame is 19,886 ticks and 60 frames are
  /// 1,193,160 of the 1,193,182 in a second. The card is what has a
  /// vertical rate, so the constant lives here; `renderer::frame_period`
  /// is this, and renderer.h is where the argument about the rounding is
  /// written down.
  static constexpr ticks frame_period = pit_input_hz / 60;

  /// Mode 0Dh's raster, in the numbers 3DAh is computed from. See this
  /// file's "The raster" for where each comes from.
  struct raster {
    static constexpr std::uint32_t scan_lines = 262;
    static constexpr std::uint32_t displayed_scan_lines = 200;
    static constexpr std::uint32_t dots_per_line = 455;
    static constexpr std::uint32_t displayed_dots = 320;
  };

  /// Bit 0 of the input status register: display *disabled* — the beam is
  /// in a blanking interval, horizontal or vertical, and display memory
  /// is not being read.
  static constexpr std::uint8_t status_display_disabled = 0x01;

  /// Bit 3: vertical retrace in progress. The bit a program waits on.
  static constexpr std::uint8_t status_vertical_retrace = 0x08;

  /// `box` must outlive this, and is read for one thing: the tick 3DAh's
  /// timing bits are computed at. Taken as a constructor argument the way
  /// pit.h and speaker.h take theirs, rather than left null and set
  /// later, because a card whose raster sometimes stands still is not a
  /// state any machine has.
  explicit ega(const machine& box) noexcept : box_(&box) {}

  [[nodiscard]] claims claimed() const noexcept override;
  void reset() override;
  void save_state(state_sink& out) const override;

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

  /// Palette register `index`'s live 6-bit EGA colour code (0-63), masked
  /// to the low four bits of `index` the same way the pipeline masks the
  /// map mask above — the attribute controller only has sixteen of them.
  /// What the renderer looks up in `ega_color_table`.
  [[nodiscard]] std::uint8_t palette_register(unsigned index) const noexcept {
    return palette_[index & (palette_register_count - 1u)];
  }

  /// The overscan register's live value. Stored, not consulted — see this
  /// file's top comment — and exposed for the same reason `mode_set`
  /// leaves the documented state a test can check.
  [[nodiscard]] std::uint8_t overscan_register() const noexcept {
    return overscan_;
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
    /// An attribute-controller cycle (3C0h write or 3C1h) named an index
    /// this subset does not implement — anything but 00h-0Fh (palette)
    /// and 11h (overscan). CRTC-adjacent registers (Mode Control, Color
    /// Plane Enable, Pixel Panning, Color Select) land here.
    attribute_index,
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

  // --- The bus cycle this device claims -------------------------------
  //
  // The adapter's own numbers. Public because they are the *hardware's*
  // facts rather than this class's: the automap seam programs the map
  // mask to reach one plane at a time (`seam_automap.cpp`, and
  // docs/seams.md §3's eighth primitive), and a second spelling of 3C4h
  // somewhere else would be two numbers that can disagree.

  static constexpr memory_window vram_window{.first = 0xA0000, .last = 0xAFFFF};

  static constexpr std::uint16_t sequencer_index_port = 0x3C4;
  static constexpr std::uint16_t sequencer_data_port = 0x3C5;
  static constexpr std::uint16_t graphics_index_port = 0x3CE;
  static constexpr std::uint16_t graphics_data_port = 0x3CF;

 private:
  /// One port, two roles told apart by the flip-flop — see this file's
  /// top comment. `attribute_data_read_port` is the second, always-data
  /// port real hardware answers 3C1h with; nothing ever writes it.
  static constexpr std::uint16_t attribute_port = 0x3C0;
  static constexpr std::uint16_t attribute_data_read_port = 0x3C1;

  /// The input status register. A read resets the attribute controller's
  /// flip-flop; nothing else in this subset does. What it *answers* is
  /// `status_byte()`.
  static constexpr std::uint16_t status_port = 0x3DA;

  static constexpr port_range sequencer_ports{.first = sequencer_index_port,
                                              .last = sequencer_data_port};
  static constexpr port_range graphics_ports{.first = graphics_index_port,
                                             .last = graphics_data_port};
  static constexpr port_range attribute_ports{.first = attribute_port,
                                              .last = attribute_data_read_port};
  static constexpr port_range status_ports{.first = status_port,
                                           .last = status_port};

  static constexpr std::array<memory_window, 1> windows_{vram_window};
  static constexpr std::array<port_range, 4> ports_{
      sequencer_ports, graphics_ports, attribute_ports, status_ports};

  /// Where the beam is at `box_->time()`, as the two timing bits of the
  /// input status register. See this file's "The raster".
  [[nodiscard]] std::uint8_t status_byte() const noexcept;

  /// The clock the raster is computed against, and the only thing this
  /// device reads its machine for.
  const machine* box_;

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

  // --- Attribute controller (3C0h index+data / 3C1h data / 3DAh reset) --

  std::uint8_t attr_index_{};
  /// The flip-flop: false means the next 3C0h write is an index, true
  /// means it is data for the register that index named. A 3DAh read
  /// forces this back to false — see this file's top comment.
  bool attr_expect_data_{false};

  std::array<std::uint8_t, palette_register_count> palette_{};
  std::uint8_t overscan_{};

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

  void write_attribute_port(std::uint8_t value);
  [[nodiscard]] std::uint8_t read_attribute_data();

  void halt_now(halt_reason reason, std::uint16_t port,
                std::uint8_t value) noexcept;
};

}  // namespace amberfolio::machine
