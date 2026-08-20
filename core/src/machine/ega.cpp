// SPDX-License-Identifier: AGPL-3.0-only
//
// ega.h has the design; this is the pipeline itself, walked stage by
// stage so a future reader can check the order against real hardware
// without a manual open beside them.

#include "amberfolio/machine/ega.h"

#include <bit>
#include <cstdint>

namespace amberfolio::machine {
namespace {

/// The function-select field of the Data Rotate register, applied between
/// this write's per-plane source and that plane's latch. 0 is "replace" —
/// the source stands as computed, the latch contributes nothing — because
/// on real hardware this is a 4-way mux with AND/OR/XOR as the other
/// three positions, not a fifth operation bolted on.
[[nodiscard]] std::uint8_t apply_function(unsigned function_select,
                                          std::uint8_t source,
                                          std::uint8_t latch) noexcept {
  switch (function_select) {
    case 1:
      return static_cast<std::uint8_t>(source & latch);
    case 2:
      return static_cast<std::uint8_t>(source | latch);
    case 3:
      return static_cast<std::uint8_t>(source ^ latch);
    default:
      return source;
  }
}

}  // namespace

claims ega::claimed() const noexcept {
  return {.memory = windows_, .ports = ports_};
}

void ega::reset() {
  // Registers, latches and the halt record go back to power-on state.
  // The planes do not — see ega.h's "Reset" section for why that split is
  // deliberate rather than an oversight.
  seq_index_ = 0;
  seq_regs_.fill(0);

  gc_index_ = 0;
  set_reset_ = 0;
  enable_set_reset_ = 0;
  color_compare_ = 0;
  data_rotate_ = 0;
  read_map_select_ = 0;
  mode_ = 0;
  misc_ = 0;
  color_dont_care_ = 0;
  bit_mask_ = 0xFF;

  attr_index_ = 0;
  attr_expect_data_ = false;
  palette_.fill(0);
  overscan_ = 0;

  latches_.fill(0);
  halt_ = {};
}

std::uint8_t ega::read_memory(std::uint32_t address) {
  if (halted()) {
    return open_bus_value;
  }
  const auto offset = static_cast<std::uint16_t>(address - vram_window.first);
  return read_pixel(offset);
}

void ega::write_memory(std::uint32_t address, std::uint8_t value) {
  if (halted()) {
    return;
  }
  const auto offset = static_cast<std::uint16_t>(address - vram_window.first);
  write_pixel(offset, value);
}

std::uint8_t ega::read_port(std::uint16_t port) {
  if (halted()) {
    return open_bus_value;
  }
  switch (port) {
    case sequencer_index_port:
      return seq_index_;
    case sequencer_data_port:
      return read_sequencer_data();
    case graphics_index_port:
      return gc_index_;
    case graphics_data_port:
      return read_graphics_data();
    case attribute_port:
      // Real hardware's answer here is the index register, with a bit
      // this subset does not track (the palette address source). Nothing
      // in v1's scope reads it back; the index is the honest half.
      return attr_index_;
    case attribute_data_read_port:
      return read_attribute_data();
    case status_port:
      // The flip-flop reset this file's top comment describes, plus the
      // stub status byte: always "not in retrace," because this device
      // has no raster to report one from. See "The attribute controller"
      // above for why that is the honest answer rather than a guess.
      attr_expect_data_ = false;
      return 0;
    default:
      // machine::read_port8 only ever calls this with a port from
      // claimed().ports (device.h), so this is unreachable in practice —
      // open bus is the honest answer if it is ever reached anyway.
      return open_bus_value;
  }
}

void ega::write_port(std::uint16_t port, std::uint8_t value) {
  if (halted()) {
    return;
  }
  switch (port) {
    case sequencer_index_port:
      seq_index_ = value;
      return;
    case sequencer_data_port:
      write_sequencer_data(value);
      return;
    case graphics_index_port:
      gc_index_ = value;
      return;
    case graphics_data_port:
      write_graphics_data(value);
      return;
    case attribute_port:
      write_attribute_port(value);
      return;
    default:
      // Covers 3C1h and 3DAh along with every other unclaimed value: 3C1h
      // is documented read-only on real hardware, and a write to the
      // status port at 3DAh (a read-only register on this subset — the
      // Feature Control register some clones put on the same address is
      // out of scope) means nothing here. Neither is a program asking for
      // a register this device does not have — it is a program writing
      // where nothing reads — so this is silence, not a refusal, and the
      // same silence the port map's own default already is.
      return;
  }
}

// --- The write pipeline --------------------------------------------------
//
// CPU byte -> rotate -> set/reset substitution -> ALU op against the
// latches -> bit-mask select -> map-mask gate -> planes. Every stage is
// named in ega.h; this is the same order, as code, with the hardware
// reasoning kept next to the line it explains rather than gathered above
// it — the point being that reading this function top to bottom *is*
// checking the order.

void ega::write_pixel(std::uint16_t offset, std::uint8_t cpu_byte) {
  const unsigned write_mode = mode_ & 0x03u;

  // Write mode 1: the latch content goes straight to the planes the map
  // mask selects. Nothing above this line in the pipeline runs — the CPU
  // byte is not even read — which is the whole point of the mode: it is
  // how a VRAM-to-VRAM block copy replays a byte that was already loaded
  // into the latches by a prior read at the source address.
  if (write_mode == 1) {
    for (unsigned plane = 0; plane < plane_count; ++plane) {
      if (((map_mask() >> plane) & 1u) != 0) {
        planes_[plane][offset] = latches_[plane];
      }
    }
    return;
  }

  // Rotate: unconditional, on the raw CPU byte, before any plane has been
  // considered. The real rotator sits on the CPU data bus itself, ahead
  // of the four-way plane split, so every plane that ends up using the
  // rotated byte (rather than a set/reset substitution) sees the same
  // rotated value.
  const unsigned rotate_count = data_rotate_ & 0x07u;
  const unsigned function_select = (data_rotate_ >> 3u) & 0x03u;
  const std::uint8_t rotated =
      std::rotr(cpu_byte, static_cast<int>(rotate_count));

  for (unsigned plane = 0; plane < plane_count; ++plane) {
    // Set/reset substitution: a per-plane 2-to-1 mux. Enable Set/Reset
    // decides which input a plane takes; the write mode only changes
    // *what the substituted input is*, not whether the mux is gated by
    // Enable Set/Reset at all. That is real hardware behaviour and the
    // reason write mode 2 still needs Enable Set/Reset set to reach every
    // plane it looks like it should — see ega.h.
    const bool substitute = ((enable_set_reset_ >> plane) & 1u) != 0;
    std::uint8_t source;
    if (substitute) {
      const bool bit = write_mode == 2 ? (((cpu_byte >> plane) & 1u) != 0)
                                       : (((set_reset_ >> plane) & 1u) != 0);
      source = bit ? std::uint8_t{0xFF} : std::uint8_t{0x00};
    } else {
      source = rotated;
    }

    // ALU: source against this plane's latch, under the function select.
    const std::uint8_t combined =
        apply_function(function_select, source, latches_[plane]);

    // Bit mask: a 1 bit takes the ALU's answer, a 0 bit keeps the latch's
    // own bit untouched — the reason this is its own stage rather than
    // folded into the ALU step above is exactly that it can override the
    // ALU's answer per bit, which no single ALU function can express.
    const auto kept = static_cast<std::uint8_t>(~bit_mask_);
    const auto result = static_cast<std::uint8_t>((combined & bit_mask_) |
                                                  (latches_[plane] & kept));

    // Map mask: the last stage, and it gates the whole plane rather than
    // a bit within it — a plane the map mask excludes is untouched by
    // this write regardless of what the first four stages computed.
    if (((map_mask() >> plane) & 1u) != 0) {
      planes_[plane][offset] = result;
    }
  }
}

std::uint8_t ega::read_pixel(std::uint16_t offset) {
  // Every CPU read loads all four latches, whatever the read mode — the
  // read mode only decides what the CPU sees back, not what the latches
  // hold afterward.
  for (unsigned plane = 0; plane < plane_count; ++plane) {
    latches_[plane] = planes_[plane][offset];
  }

  const bool read_mode_1 = ((mode_ >> 3u) & 1u) != 0;
  if (!read_mode_1) {
    return latches_[read_map_select_ & 0x03u];
  }
  return color_compare_result();
}

std::uint8_t ega::color_compare_result() const noexcept {
  std::uint8_t result = 0;
  for (unsigned bit = 0; bit < 8u; ++bit) {
    bool match = true;
    for (unsigned plane = 0; plane < plane_count; ++plane) {
      // Color Don't Care: 1 means this plane counts toward the match —
      // the register's name reads backwards from what the bit does. See
      // ega.h.
      const bool cares = ((color_dont_care_ >> plane) & 1u) != 0;
      if (!cares) {
        continue;
      }
      const bool plane_bit = ((latches_[plane] >> bit) & 1u) != 0;
      const bool want_bit = ((color_compare_ >> plane) & 1u) != 0;
      if (plane_bit != want_bit) {
        match = false;
        break;
      }
    }
    if (match) {
      result = static_cast<std::uint8_t>(result | (1u << bit));
    }
  }
  return result;
}

// --- Register plumbing ----------------------------------------------------

void ega::write_sequencer_data(std::uint8_t value) {
  if (seq_index_ >= sequencer_register_count) {
    halt_now(halt_reason::sequencer_index, sequencer_data_port, seq_index_);
    return;
  }
  seq_regs_[seq_index_] = value;
}

std::uint8_t ega::read_sequencer_data() {
  if (seq_index_ >= sequencer_register_count) {
    halt_now(halt_reason::sequencer_index, sequencer_data_port, seq_index_);
    return open_bus_value;
  }
  return seq_regs_[seq_index_];
}

void ega::write_graphics_data(std::uint8_t value) {
  if (gc_index_ >= gc_register_count) {
    halt_now(halt_reason::gc_index, graphics_data_port, gc_index_);
    return;
  }
  switch (gc_index_) {
    case 0:
      set_reset_ = value;
      return;
    case 1:
      enable_set_reset_ = value;
      return;
    case 2:
      color_compare_ = value;
      return;
    case 3:
      data_rotate_ = value;
      return;
    case 4:
      read_map_select_ = value;
      return;
    case 5:
      // Write modes 0-2 exist on an EGA; mode 3 is a VGA-only field value
      // this hardware never had. Caught here, at the point the program
      // asks for it, rather than lazily inside write_pixel().
      if ((value & 0x03u) == 0x03u) {
        halt_now(halt_reason::write_mode, graphics_data_port, value);
        return;
      }
      mode_ = value;
      return;
    case 6:
      misc_ = value;
      return;
    case 7:
      color_dont_care_ = value;
      return;
    case 8:
      bit_mask_ = value;
      return;
    default:
      return;  // Unreachable: guarded above.
  }
}

std::uint8_t ega::read_graphics_data() {
  if (gc_index_ >= gc_register_count) {
    halt_now(halt_reason::gc_index, graphics_data_port, gc_index_);
    return open_bus_value;
  }
  switch (gc_index_) {
    case 0:
      return set_reset_;
    case 1:
      return enable_set_reset_;
    case 2:
      return color_compare_;
    case 3:
      return data_rotate_;
    case 4:
      return read_map_select_;
    case 5:
      return mode_;
    case 6:
      return misc_;
    case 7:
      return color_dont_care_;
    case 8:
      return bit_mask_;
    default:
      return open_bus_value;  // Unreachable: guarded above.
  }
}

// --- Attribute controller (3C0h index+data / 3C1h data / 3DAh reset) -----
//
// See this file's top comment, "The attribute controller," for the
// flip-flop protocol this pair of functions implements.

void ega::write_attribute_port(std::uint8_t value) {
  if (!attr_expect_data_) {
    // The address register is five bits wide on real hardware (the sixth
    // bit of the byte selects the palette address source, which this
    // subset does not track); masking it here is the same hardware-width
    // truncation the map mask and the palette below apply to their own
    // registers, not a guess at what the program meant.
    attr_index_ = value & 0x1Fu;
    attr_expect_data_ = true;
    return;
  }
  attr_expect_data_ = false;

  if (attr_index_ < palette_register_count) {
    // 6-bit register; the top two bits of the byte the program wrote
    // never latch, which is what a real attribute controller does with
    // them.
    palette_[attr_index_] = value & 0x3Fu;
    return;
  }
  if (attr_index_ == attribute_overscan_index) {
    overscan_ = value & 0x3Fu;
    return;
  }
  halt_now(halt_reason::attribute_index, attribute_port, attr_index_);
}

std::uint8_t ega::read_attribute_data() {
  if (attr_index_ < palette_register_count) {
    return palette_[attr_index_];
  }
  if (attr_index_ == attribute_overscan_index) {
    return overscan_;
  }
  halt_now(halt_reason::attribute_index, attribute_data_read_port, attr_index_);
  return open_bus_value;
}

void ega::halt_now(halt_reason reason, std::uint16_t port,
                   std::uint8_t value) noexcept {
  // A stop is an event, not a state to keep re-recording — the same
  // discipline machine::step() follows once the processor has stopped
  // (see tests/core/machine/machine_test.cpp,
  // machine_step.stays_stopped_and_stops_touching_anything).
  if (halted()) {
    return;
  }
  halt_ = {.reason = reason, .port = port, .value = value};

  // And out to the machine, which is what makes this a stop rather than
  // a device quietly going inert.
  //
  // When this device was written (#47) `device` had no channel back to
  // the machine, so the refusal could only be local: `halted()` went
  // true, later cycles answered open bus, and nothing said so. That was
  // weaker than CLAUDE.md's rule — "a loud log line and a clean stop" —
  // and it was filed as #65 rather than left in a comment. #46 built the
  // channel; this is the EGA joining it.
  //
  // Both records are kept, because they answer different questions.
  // `device_fault` carries what the machine needs to stop and report;
  // `halt_` names *which* register was refused, which a test reads back
  // and a human debugging a mode-set wants. Neither is derivable from
  // the other.
  report_fault(port, value);
}

}  // namespace amberfolio::machine
