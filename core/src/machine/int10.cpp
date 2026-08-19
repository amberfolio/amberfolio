// SPDX-License-Identifier: AGPL-3.0-only
//
// int10.h has the design; this is the mode-set table and the dispatch
// over AH.

#include "amberfolio/machine/int10.h"

#include <array>
#include <cstddef>
#include <cstdint>

#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"

namespace amberfolio::machine {
namespace {

// --- Ports, named the way a program addressing the hardware would ------

constexpr std::uint16_t sequencer_index_port = 0x3C4;
constexpr std::uint16_t sequencer_data_port = 0x3C5;
constexpr std::uint16_t graphics_index_port = 0x3CE;
constexpr std::uint16_t graphics_data_port = 0x3CF;
constexpr std::uint16_t attribute_port = 0x3C0;
constexpr std::uint16_t status_port = 0x3DA;

/// Attribute controller index 11h: overscan. ega.h names the same fact;
/// repeated here because this is the caller's side of the port protocol,
/// not the device's.
constexpr std::uint8_t attribute_overscan_index = 0x11;

/// The one mode this machine's write pipeline and renderer understand
/// (PLAN.md §9).
constexpr std::uint8_t mode_0dh = 0x0D;

/// One register write: an index, and the value that goes with it.
struct register_write {
  std::uint8_t index;
  std::uint8_t value;
};

/// The sequencer's mode-0Dh table. Only the map mask (index 02h) is
/// load-bearing — see int10.h's top comment — set to 0Fh so a program's
/// writes after mode-set actually reach all four planes. The rest match
/// ordinary EGA graphics-mode practice (reset released, 8-dot character
/// clock, sequential — not chain-4 — addressing) but this device stores
/// them inertly (ega.h), so their values matter to a test's assertions
/// and not to the machine's behaviour.
constexpr std::array<register_write, 5> mode_0d_sequencer{{
    {.index = 0x00, .value = 0x03},
    {.index = 0x01, .value = 0x01},
    {.index = 0x02, .value = 0x0F},
    {.index = 0x03, .value = 0x00},
    {.index = 0x04, .value = 0x06},
}};

/// The graphics controller's mode-0Dh table. Every value here already
/// matches this device's own reset() defaults (ega.cpp) — write mode 0,
/// read mode 0, no rotate, copy function, bit mask FF — except the
/// Miscellaneous register (06h), which reset() zeroes and this table sets
/// to 03h: bit 0 selects graphics mode, bits 2:1 select the A0000-AFFFF
/// 64K memory map — the field values ega.h documents for that register.
/// Written explicitly and not left to reset()'s defaults, because a real
/// mode-set genuinely writes the whole table and doing the same here
/// means this function does not depend on what state the device was left
/// in.
constexpr std::array<register_write, 9> mode_0d_graphics{{
    {.index = 0x00, .value = 0x00},
    {.index = 0x01, .value = 0x00},
    {.index = 0x02, .value = 0x00},
    {.index = 0x03, .value = 0x00},
    {.index = 0x04, .value = 0x00},
    {.index = 0x05, .value = 0x00},
    {.index = 0x06, .value = 0x03},
    {.index = 0x07, .value = 0x00},
    {.index = 0x08, .value = 0xFF},
}};

/// The standard 16-colour EGA/CGA-compatible default palette (int10.h's
/// top comment). Index *i* of this array is the value AH=00h writes to
/// palette register *i*; the discontinuity at index 6 (20, not 6) and
/// indices 8-15 (56-63, not 8-15) is the historical CGA
/// backward-compatibility quirk every EGA/VGA hardware reference
/// documents, not a typo.
constexpr std::array<std::uint8_t, ega::palette_register_count>
    mode_0d_default_palette{0,  1,  2,  3,  4,  5,  20, 7,
                            56, 57, 58, 59, 60, 61, 62, 63};

/// The attribute controller's index/data protocol (ega.h): a status read
/// resets the flip-flop, then two writes to the one port load the index
/// and the data. Every AH=10h write and the palette half of mode-set go
/// through this.
void write_attribute_register(machine& box, std::uint8_t index,
                              std::uint8_t value) {
  static_cast<void>(box.read_port8(status_port));
  box.write_port8(attribute_port, index);
  box.write_port8(attribute_port, value);
}

/// AH=00h AL=0Dh: program the sequencer, the graphics controller and the
/// palette for 320x200x16, and tell the machine a mode is now active
/// (machine.h's "Video mode discipline").
void set_mode_0d(machine& box) {
  for (const register_write& reg : mode_0d_sequencer) {
    box.write_port8(sequencer_index_port, reg.index);
    box.write_port8(sequencer_data_port, reg.value);
  }
  for (const register_write& reg : mode_0d_graphics) {
    box.write_port8(graphics_index_port, reg.index);
    box.write_port8(graphics_data_port, reg.value);
  }
  for (std::size_t i = 0; i < mode_0d_default_palette.size(); ++i) {
    write_attribute_register(box, static_cast<std::uint8_t>(i),
                             mode_0d_default_palette[i]);
  }
  write_attribute_register(box, attribute_overscan_index, 0x00);

  box.note_video_mode_set();
}

/// AH=10h: the palette register set PLAN.md §3 names, plus the trivial
/// overscan set the mode-set path above already uses. AL=00h: BL =
/// register 0-15, BH = colour. AL=01h: BH = overscan colour. Both are the
/// documented public BIOS convention (int10.h's top comment) and both
/// mask nothing here — the device masks to its own register widths
/// (ega.cpp), and forwarding the raw byte lets an out-of-range BL reach
/// the device's own refusal instead of this file silently reinterpreting
/// it.
void set_palette(machine& box) {
  const cpu::registers& regs = box.processor().regs();
  const std::uint8_t sub = regs.get(cpu::reg8::al);

  if (sub == 0x00) {
    write_attribute_register(box, regs.get(cpu::reg8::bl),
                             regs.get(cpu::reg8::bh));
    return;
  }
  if (sub == 0x01) {
    write_attribute_register(box, attribute_overscan_index,
                             regs.get(cpu::reg8::bh));
    return;
  }

  box.stop_unsupported_request(regs[cpu::reg16::ax]);
}

void video_bios(service_floor& floor, std::uint8_t /*vector*/) {
  machine& box = floor.box();
  const cpu::registers& regs = box.processor().regs();
  const std::uint8_t function = regs.get(cpu::reg8::ah);

  switch (function) {
    case 0x00:
      if (regs.get(cpu::reg8::al) == mode_0dh) {
        set_mode_0d(box);
      } else {
        box.stop_unsupported_request(regs[cpu::reg16::ax]);
      }
      return;
    case 0x10:
      set_palette(box);
      return;
    default:
      box.stop_unsupported_request(regs[cpu::reg16::ax]);
  }
}

}  // namespace

void install_int10(service_floor& floor) {
  floor.provide(service::video_vector, &video_bios);
}

}  // namespace amberfolio::machine
