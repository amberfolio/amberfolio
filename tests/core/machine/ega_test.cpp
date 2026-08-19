// SPDX-License-Identifier: AGPL-3.0-only
//
// The EGA: the write pipeline, the read/latch behaviour, and the pieces
// of the sequencer and graphics-controller register subset this device
// implements. ega.h has the design and the hardware reasoning; this file
// is what holds it to that reasoning, one stage of the pipeline at a
// time.
//
// Every test programs the device the way a program would — through
// write_port() at the real 3C4h/3C5h and 3CEh/3CFh pairs and through
// read_memory()/write_memory() at the real A0000-AFFFF addresses — rather
// than reaching into private state, so that a test failure here is a
// failure a program poking the same ports would also hit.

#include "amberfolio/machine/ega.h"

#include <array>
#include <cstdint>
#include <memory>

#include "amberfolio/machine/device.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

// --- Ports and addresses, as a program would name them -----------------

constexpr std::uint16_t seq_index_port = 0x3C4;
constexpr std::uint16_t seq_data_port = 0x3C5;
constexpr std::uint16_t gc_index_port = 0x3CE;
constexpr std::uint16_t gc_data_port = 0x3CF;
constexpr std::uint16_t attribute_port = 0x3C0;
constexpr std::uint16_t attribute_data_read_port = 0x3C1;
constexpr std::uint16_t status_port = 0x3DA;

constexpr std::uint32_t vram_first = 0xA0000;
constexpr std::uint32_t vram_last = 0xAFFFF;

[[nodiscard]] constexpr std::uint32_t addr(std::uint16_t offset) noexcept {
  return vram_first + offset;
}

// --- The rig -------------------------------------------------------------
//
// 256 KiB of planes inside it — heap-allocate, the same discipline
// tests/core/machine/machine_test.cpp uses for `machine` and its
// megabyte.

struct rig {
  rig() : dev(std::make_unique<ega>()) {}

  [[nodiscard]] ega& video() const noexcept { return *dev; }

  void set_seq(std::uint8_t index, std::uint8_t value) const {
    dev->write_port(seq_index_port, index);
    dev->write_port(seq_data_port, value);
  }

  void set_map_mask(std::uint8_t mask) const { set_seq(0x02, mask); }

  void set_gc(std::uint8_t index, std::uint8_t value) const {
    dev->write_port(gc_index_port, index);
    dev->write_port(gc_data_port, value);
  }

  /// The attribute controller's index/data protocol: a status read resets
  /// the flip-flop, then two writes to the one port load the index and
  /// the data — ega.h's "The attribute controller."
  void set_attribute(std::uint8_t index, std::uint8_t value) const {
    static_cast<void>(dev->read_port(status_port));
    dev->write_port(attribute_port, index);
    dev->write_port(attribute_port, value);
  }

  /// Land `value` on exactly one plane at `at`, using the pipeline at its
  /// power-on defaults (write mode 0, copy, no rotate, no substitution,
  /// bit mask FF) — which is what lets this be "the byte that ends up
  /// there" rather than a shortcut around the pipeline under test.
  void seed_plane(unsigned plane, std::uint32_t at, std::uint8_t value) const {
    set_map_mask(static_cast<std::uint8_t>(1u << plane));
    dev->write_memory(at, value);
  }

  std::unique_ptr<ega> dev;
};

// --- What the device claims ----------------------------------------------

TEST(ega_claims, claims_a0000_affff_and_not_the_b_series) {
  const rig r;
  const claims c = r.video().claimed();

  ASSERT_EQ(c.memory.size(), 1u);
  EXPECT_EQ(c.memory[0],
            (memory_window{.first = vram_first, .last = vram_last}));

  ASSERT_EQ(c.ports.size(), 4u);
  EXPECT_EQ(c.ports[0], (port_range{.first = 0x3C4, .last = 0x3C5}));
  EXPECT_EQ(c.ports[1], (port_range{.first = 0x3CE, .last = 0x3CF}));
  EXPECT_EQ(c.ports[2], (port_range{.first = 0x3C0, .last = 0x3C1}));
  EXPECT_EQ(c.ports[3], (port_range{.first = 0x3DA, .last = 0x3DA}));
}

// --- Write mode 0: rotate ------------------------------------------------

TEST(ega_rotate, rotates_the_cpu_byte_right_by_the_programmed_count) {
  // 1000_0001 rotated right by 0..7 — chosen because it has a bit at each
  // end, so every rotation produces a different, checkable byte.
  constexpr std::uint8_t source = 0x81;
  constexpr std::array<std::uint8_t, 8> expected{0x81, 0xC0, 0x60, 0x30,
                                                 0x18, 0x0C, 0x06, 0x03};

  for (unsigned count = 0; count < 8; ++count) {
    const rig r;
    r.set_gc(0x03, static_cast<std::uint8_t>(count));  // rotate only, fn=copy
    r.set_map_mask(0x01);

    r.video().write_memory(addr(0), source);

    EXPECT_EQ(r.video().plane_byte(0, 0), expected[count])
        << "rotate count " << count;
  }
}

// --- Write mode 0: the ALU, against a latch that differs per plane ------

TEST(ega_alu, combines_the_source_with_each_planes_own_latch) {
  struct alu_case {
    unsigned function_select;
    std::array<std::uint8_t, 4> expected;
  };

  // Seeded latches, one distinct byte per plane: 0x0F, 0xF0, 0xAA, 0x55.
  // The CPU byte under test is 0xCC (1100_1100). Expected values below
  // are `function(0xCC, latch[plane])` computed by hand.
  const std::array<alu_case, 4> cases{{
      // 0: copy — the latch is irrelevant.
      {.function_select = 0, .expected = {0xCC, 0xCC, 0xCC, 0xCC}},
      {.function_select = 1, .expected = {0x0C, 0xC0, 0x88, 0x44}},  // AND.
      {.function_select = 2, .expected = {0xCF, 0xFC, 0xEE, 0xDD}},  // OR.
      {.function_select = 3, .expected = {0xC3, 0x3C, 0x66, 0x99}},  // XOR.
  }};

  for (const alu_case& c : cases) {
    const rig r;
    const std::uint16_t offset = 0x200;
    constexpr std::array<std::uint8_t, 4> latch_seed{0x0F, 0xF0, 0xAA, 0x55};
    for (unsigned plane = 0; plane < 4; ++plane) {
      r.seed_plane(plane, addr(offset), latch_seed[plane]);
    }
    // Load the latches from what was just seeded.
    static_cast<void>(r.video().read_memory(addr(offset)));

    r.set_gc(0x03, static_cast<std::uint8_t>(c.function_select << 3u));
    r.set_map_mask(0x0F);
    r.video().write_memory(addr(offset), 0xCC);

    for (unsigned plane = 0; plane < 4; ++plane) {
      EXPECT_EQ(r.video().plane_byte(plane, offset), c.expected[plane])
          << "function " << c.function_select << " plane " << plane;
    }
  }
}

// --- Write mode 0: the bit mask -------------------------------------------

TEST(ega_bit_mask, selects_between_the_alu_result_and_the_latch_per_bit) {
  struct mask_case {
    std::uint8_t mask;
    std::uint8_t expected;
  };

  // Latch (pre-existing plane content) is 0xF0; the CPU byte is 0x0F,
  // combined with function 0 (copy) so the ALU's answer is exactly the
  // CPU byte and the only thing under test is the mask stage.
  const std::array<mask_case, 3> cases{{
      // Nothing changes: the latch survives untouched.
      {.mask = 0x00, .expected = 0xF0},
      // Everything changes: the ALU result passes whole.
      {.mask = 0xFF, .expected = 0x0F},
      // Sparse: (0x0F & 0xAA) | (0xF0 & 0x55) = 0x5A.
      {.mask = 0xAA, .expected = 0x5A},
  }};

  for (const mask_case& c : cases) {
    const rig r;
    r.seed_plane(0, addr(0x300), 0xF0);
    static_cast<void>(r.video().read_memory(addr(0x300)));  // load the latch

    r.set_gc(0x03, 0x00);  // rotate 0, function 0 (copy).
    r.set_gc(0x08, c.mask);
    r.set_map_mask(0x01);
    r.video().write_memory(addr(0x300), 0x0F);

    EXPECT_EQ(r.video().plane_byte(0, 0x300), c.expected)
        << "mask " << static_cast<unsigned>(c.mask);
  }
}

// --- Write mode 0: set/reset substitution ---------------------------------

TEST(ega_set_reset, substitutes_expanded_bits_only_where_enabled) {
  const rig r;
  constexpr std::uint8_t cpu_byte = 0x3C;  // 0011_1100 — the fallback value.

  r.set_gc(0x00, 0b0001);  // Set/Reset: plane 0 -> 1, plane 2 -> 0.
  r.set_gc(0x01, 0b0101);  // Enable Set/Reset: planes 0 and 2 substitute.
  r.set_gc(0x03, 0x00);    // rotate 0, function 0 (copy).
  r.set_map_mask(0x0F);

  r.video().write_memory(addr(0x400), cpu_byte);

  EXPECT_EQ(r.video().plane_byte(0, 0x400), 0xFF);      // substituted, bit 1
  EXPECT_EQ(r.video().plane_byte(1, 0x400), cpu_byte);  // not substituted
  EXPECT_EQ(r.video().plane_byte(2, 0x400), 0x00);      // substituted, bit 0
  EXPECT_EQ(r.video().plane_byte(3, 0x400), cpu_byte);  // not substituted
}

// --- Write mode 1: latch copy ---------------------------------------------

TEST(ega_write_mode_1, writes_the_latches_verbatim_and_ignores_the_cpu_byte) {
  const rig r;
  constexpr std::array<std::uint8_t, 4> seed{0x11, 0x22, 0x33, 0x44};
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.seed_plane(plane, addr(0x500), seed[plane]);
  }
  static_cast<void>(r.video().read_memory(addr(0x500)));  // load the latches

  r.set_gc(0x05, 0x01);    // write mode 1.
  r.set_map_mask(0b0101);  // only planes 0 and 2 land at the destination.
  r.video().write_memory(addr(0x600), 0x99);  // ignored entirely

  EXPECT_EQ(r.video().plane_byte(0, 0x600), seed[0]);
  EXPECT_EQ(r.video().plane_byte(1, 0x600), 0x00);  // map mask excluded it
  EXPECT_EQ(r.video().plane_byte(2, 0x600), seed[2]);
  EXPECT_EQ(r.video().plane_byte(3, 0x600), 0x00);
}

// --- Write mode 2: color expand --------------------------------------------

TEST(ega_write_mode_2, expands_one_cpu_data_bit_per_plane) {
  const rig r;
  r.set_gc(0x01, 0x0F);  // Enable Set/Reset: every plane substitutes.
  r.set_gc(0x03, 0x00);  // rotate 0, function 0 (copy).
  r.set_gc(0x05, 0x02);  // write mode 2.
  r.set_map_mask(0x0F);

  // bit0=1, bit1=0, bit2=1, bit3=0.
  r.video().write_memory(addr(0x700), 0x05);

  EXPECT_EQ(r.video().plane_byte(0, 0x700), 0xFF);
  EXPECT_EQ(r.video().plane_byte(1, 0x700), 0x00);
  EXPECT_EQ(r.video().plane_byte(2, 0x700), 0xFF);
  EXPECT_EQ(r.video().plane_byte(3, 0x700), 0x00);
}

TEST(ega_write_mode_2, still_needs_enable_set_reset_per_plane) {
  // The documented, easy-to-miss hardware fact this device reproduces
  // (ega.h): Enable Set/Reset gates write mode 2 exactly as it gates
  // write mode 0. A plane whose enable bit is clear gets the rotated CPU
  // byte, not a color-expanded bit, even in write mode 2.
  const rig r;
  r.set_gc(0x01, 0b0011);  // only planes 0 and 1 color-expand.
  r.set_gc(0x03, 0x00);    // rotate 0, function 0 (copy).
  r.set_gc(0x05, 0x02);    // write mode 2.
  r.set_map_mask(0x0F);

  constexpr std::uint8_t cpu_byte = 0x05;  // bit0=1, bit1=0.
  r.video().write_memory(addr(0x710), cpu_byte);

  EXPECT_EQ(r.video().plane_byte(0, 0x710), 0xFF);      // expanded
  EXPECT_EQ(r.video().plane_byte(1, 0x710), 0x00);      // expanded
  EXPECT_EQ(r.video().plane_byte(2, 0x710), cpu_byte);  // not enabled
  EXPECT_EQ(r.video().plane_byte(3, 0x710), cpu_byte);  // not enabled
}

// --- Reads: latches and read map select -----------------------------------

TEST(ega_read_mode_0, answers_with_the_plane_read_map_select_names) {
  const rig r;
  constexpr std::array<std::uint8_t, 4> seed{0xA1, 0xB2, 0xC3, 0xD4};
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.seed_plane(plane, addr(0x800), seed[plane]);
  }

  for (unsigned plane = 0; plane < 4; ++plane) {
    r.set_gc(0x04, static_cast<std::uint8_t>(plane));
    EXPECT_EQ(r.video().read_memory(addr(0x800)), seed[plane]);
  }
}

TEST(ega_read, every_read_loads_all_four_latches_not_just_the_one_answered) {
  const rig r;
  constexpr std::array<std::uint8_t, 4> seed{0x01, 0x02, 0x04, 0x08};
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.seed_plane(plane, addr(0x900), seed[plane]);
  }

  r.set_gc(0x04, 0x00);  // read map select names plane 0.
  static_cast<void>(r.video().read_memory(addr(0x900)));

  const std::array<std::uint8_t, 4> latched = r.video().latches();
  EXPECT_EQ(latched, seed);
}

// --- Read mode 1: the color-compare truth table ----------------------------

TEST(ega_read_mode_1, compare_truth_table) {
  struct compare_case {
    std::uint8_t dont_care;
    std::uint8_t compare;
    std::array<std::uint8_t, 4> latches;
    std::uint8_t expected;
  };

  const std::array<compare_case, 6> cases{{
      // Nobody cares: every bit matches trivially.
      {.dont_care = 0x0,
       .compare = 0b1111,
       .latches = {0x00, 0xFF, 0xAA, 0x55},
       .expected = 0xFF},
      // Everybody cares, everybody agrees at every bit.
      {.dont_care = 0xF,
       .compare = 0b0000,
       .latches = {0x00, 0x00, 0x00, 0x00},
       .expected = 0xFF},
      // Everybody cares; plane 0 disagrees at every bit, so nothing
      // matches regardless of the other three planes.
      {.dont_care = 0xF,
       .compare = 0b0000,
       .latches = {0xFF, 0x00, 0x00, 0x00},
       .expected = 0x00},
      // Only plane 0 cares, wants bit == 1: the answer is plane 0's byte.
      {.dont_care = 0b0001,
       .compare = 0b0001,
       .latches = {0xAA, 0x00, 0xFF, 0x55},
       .expected = 0xAA},
      // Only plane 0 cares, wants bit == 0: the answer is plane 0's byte,
      // inverted.
      {.dont_care = 0b0001,
       .compare = 0b0000,
       .latches = {0xAA, 0x00, 0xFF, 0x55},
       .expected = 0x55},
      // Planes 0 and 1 care (0 wants 1, 1 wants 0); planes 2 and 3 are
      // ignored. Matches only where plane0=1 AND plane1=0, which is the
      // top nibble here.
      {.dont_care = 0b0011,
       .compare = 0b0001,
       .latches = {0xF0, 0x0F, 0xFF, 0x00},
       .expected = 0xF0},
  }};

  for (const compare_case& c : cases) {
    const rig r;
    const std::uint16_t offset = 0xA00;
    for (unsigned plane = 0; plane < 4; ++plane) {
      r.seed_plane(plane, addr(offset), c.latches[plane]);
    }

    r.set_gc(0x02, c.compare);
    r.set_gc(0x07, c.dont_care);
    r.set_gc(0x05, 0x08);  // read mode 1, write mode 0.

    EXPECT_EQ(r.video().read_memory(addr(offset)), c.expected)
        << "dont_care=" << static_cast<unsigned>(c.dont_care)
        << " compare=" << static_cast<unsigned>(c.compare);
  }
}

// --- A full scanline: fill, then read every byte back ----------------------

TEST(ega_scanline, fills_and_reads_back_a_full_scanline_on_every_plane) {
  // 320x200x16 (mode 0Dh) is 320 pixels per row, one bit per pixel per
  // plane: 40 bytes.
  constexpr std::uint16_t scanline_bytes = 40;

  const rig r;
  r.set_gc(0x03, 0x00);  // rotate 0, function 0 (copy).
  r.set_gc(0x08, 0xFF);  // bit mask: nothing held back.

  std::array<std::array<std::uint8_t, scanline_bytes>, 4> expected{};
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.set_map_mask(static_cast<std::uint8_t>(1u << plane));
    for (std::uint16_t x = 0; x < scanline_bytes; ++x) {
      const auto value = static_cast<std::uint8_t>(x ^ (plane * 0x11u));
      expected[plane][x] = value;
      r.video().write_memory(addr(x), value);
    }
  }

  for (unsigned plane = 0; plane < 4; ++plane) {
    r.set_gc(0x04, static_cast<std::uint8_t>(plane));
    for (std::uint16_t x = 0; x < scanline_bytes; ++x) {
      EXPECT_EQ(r.video().read_memory(addr(x)), expected[plane][x])
          << "plane " << plane << " x " << x;
    }
  }
}

// --- Word access: two byte cycles, and what that does to the latches -----

TEST(ega_word_access, a_word_write_lands_two_independent_bytes) {
  const rig r;
  r.set_gc(0x03, 0x00);
  r.set_map_mask(0x0F);

  r.video().write_memory(addr(0xB00), 0x11);
  r.video().write_memory(addr(0xB01), 0x22);

  for (unsigned plane = 0; plane < 4; ++plane) {
    EXPECT_EQ(r.video().plane_byte(plane, 0xB00), 0x11);
    EXPECT_EQ(r.video().plane_byte(plane, 0xB01), 0x22);
  }
}

TEST(ega_word_access,
     a_word_read_then_a_write_mode_1_word_write_uses_only_the_second_latch) {
  // The quirk ega.h documents: nothing about this device's write_memory
  // or read_memory needs to know it is being called twice for one word —
  // it falls out of there being no read_memory16 on `device` (cpu/bus.h)
  // and mode 1 bypassing the CPU byte entirely. A word read reloads the
  // latches on *each* byte cycle, so by the time the second byte cycle
  // runs, the first byte's latch content is gone; a word write in write
  // mode 1 right afterward has only the second byte's content to copy,
  // twice.
  const rig r;
  constexpr std::array<std::uint8_t, 4> low{0x01, 0x02, 0x03, 0x04};
  constexpr std::array<std::uint8_t, 4> high{0xF1, 0xF2, 0xF3, 0xF4};
  for (unsigned plane = 0; plane < 4; ++plane) {
    r.seed_plane(plane, addr(0xC00), low[plane]);
    r.seed_plane(plane, addr(0xC01), high[plane]);
  }

  static_cast<void>(r.video().read_memory(addr(0xC00)));  // latches <- low
  static_cast<void>(r.video().read_memory(addr(0xC01)));  // latches <- high

  r.set_gc(0x05, 0x01);  // write mode 1.
  r.set_map_mask(0x0F);
  r.video().write_memory(addr(0xD00), 0x00);
  r.video().write_memory(addr(0xD01), 0x00);

  for (unsigned plane = 0; plane < 4; ++plane) {
    EXPECT_EQ(r.video().plane_byte(plane, 0xD00), high[plane]);
    EXPECT_EQ(r.video().plane_byte(plane, 0xD01), high[plane]);
  }
}

// --- The sequencer: the map mask is real, the rest is accepted -----------

TEST(ega_sequencer, map_mask_gates_which_planes_a_write_reaches) {
  const rig r;
  r.set_gc(0x03, 0x00);
  r.set_map_mask(0b0110);  // planes 1 and 2 only.

  r.video().write_memory(addr(0xE00), 0x7E);

  EXPECT_EQ(r.video().plane_byte(0, 0xE00), 0x00);
  EXPECT_EQ(r.video().plane_byte(1, 0xE00), 0x7E);
  EXPECT_EQ(r.video().plane_byte(2, 0xE00), 0x7E);
  EXPECT_EQ(r.video().plane_byte(3, 0xE00), 0x00);
}

TEST(ega_sequencer, accepts_and_stores_the_mode_set_indices_without_halting) {
  const rig r;
  constexpr std::array<std::uint8_t, 4> accepted_indices{0x00, 0x01, 0x03,
                                                         0x04};
  for (std::uint8_t index : accepted_indices) {
    r.set_seq(index, 0xA5);
    EXPECT_FALSE(r.video().halted())
        << "index " << static_cast<unsigned>(index);

    // Reselect and read back: accepted means stored, not merely ignored.
    r.video().write_port(seq_index_port, index);
    EXPECT_EQ(r.video().read_port(seq_data_port), 0xA5);
  }
}

TEST(ega_sequencer, halts_on_an_index_past_the_real_register_set) {
  const rig r;
  r.set_seq(0x05, 0x00);

  ASSERT_TRUE(r.video().halted());
  EXPECT_EQ(r.video().halt(),
            (ega::halt_record{.reason = ega::halt_reason::sequencer_index,
                              .port = seq_data_port,
                              .value = 0x05}));
}

// --- The graphics controller: the out-of-range and unsupported cases -----

TEST(ega_graphics_controller, halts_on_an_index_past_the_real_register_set) {
  const rig r;
  r.set_gc(0x09, 0x00);

  ASSERT_TRUE(r.video().halted());
  EXPECT_EQ(r.video().halt(),
            (ega::halt_record{.reason = ega::halt_reason::gc_index,
                              .port = gc_data_port,
                              .value = 0x09}));
}

TEST(ega_graphics_controller, halts_on_write_mode_3) {
  const rig r;
  r.set_gc(0x05, 0x03);

  ASSERT_TRUE(r.video().halted());
  EXPECT_EQ(r.video().halt(),
            (ega::halt_record{.reason = ega::halt_reason::write_mode,
                              .port = gc_data_port,
                              .value = 0x03}));
}

TEST(ega_graphics_controller, misc_register_is_stored_but_never_consulted) {
  const rig r;
  r.set_gc(0x06, 0xFF);
  EXPECT_FALSE(r.video().halted());
}

// --- The attribute controller: the flip-flop, the palette, overscan ------

TEST(ega_attribute_controller, a_status_read_then_two_writes_load_a_register) {
  const rig r;
  r.set_attribute(0x03, 0x2A);  // palette register 3.

  EXPECT_EQ(r.video().palette_register(3), 0x2A);
}

TEST(ega_attribute_controller,
     without_the_status_read_first_the_second_write_is_the_data) {
  const rig r;
  // No status read: the flip-flop is wherever the last operation left it.
  // A fresh device starts "expecting an index" (power-on state), so the
  // first write below is the index and the second is the data — the same
  // as set_attribute() does, just without the explicit reset.
  r.video().write_port(attribute_port, 0x05);
  r.video().write_port(attribute_port, 0x15);

  EXPECT_EQ(r.video().palette_register(5), 0x15);
}

TEST(ega_attribute_controller, a_status_read_resets_the_flip_flop_mid_write) {
  const rig r;
  // Leave the flip-flop in "expect data" for register 1, then read the
  // status port — which must make the *next* write an index again rather
  // than the data register 1 was waiting for.
  r.video().write_port(attribute_port, 0x01);
  static_cast<void>(r.video().read_port(status_port));
  r.video().write_port(attribute_port, 0x07);  // now an index: register 7.
  r.video().write_port(attribute_port, 0x11);  // and its data.

  EXPECT_EQ(r.video().palette_register(7), 0x11);
  // Register 1 was never given data — the reset flip-flop discarded that
  // half-finished write, which is the entire point of the reset existing.
  EXPECT_EQ(r.video().palette_register(1), 0x00);
}

TEST(ega_attribute_controller, palette_registers_mask_to_six_bits) {
  const rig r;
  r.set_attribute(0x00, 0xFF);

  EXPECT_EQ(r.video().palette_register(0), 0x3F);
}

TEST(ega_attribute_controller,
     every_palette_register_is_independently_addressable) {
  const rig r;
  for (unsigned reg = 0; reg < 16; ++reg) {
    r.set_attribute(static_cast<std::uint8_t>(reg),
                    static_cast<std::uint8_t>(reg * 3));
  }
  for (unsigned reg = 0; reg < 16; ++reg) {
    EXPECT_EQ(r.video().palette_register(reg),
              static_cast<std::uint8_t>(reg * 3))
        << "register " << reg;
  }
}

TEST(ega_attribute_controller, overscan_is_stored_and_reads_back) {
  const rig r;
  r.set_attribute(0x11, 0x3F);

  EXPECT_EQ(r.video().overscan_register(), 0x3F);
}

TEST(ega_attribute_controller, data_read_port_answers_the_indexed_register) {
  const rig r;
  r.set_attribute(0x09, 0x2C);

  // Select register 9 again (a plain write leaves the flip-flop expecting
  // data; a status read puts it back to "expect index" first) and read it
  // back through 3C1h, which never participates in the flip-flop.
  static_cast<void>(r.video().read_port(status_port));
  r.video().write_port(attribute_port, 0x09);
  EXPECT_EQ(r.video().read_port(attribute_data_read_port), 0x2C);
}

TEST(ega_attribute_controller, halts_on_an_index_past_the_implemented_subset) {
  const rig r;
  static_cast<void>(r.video().read_port(status_port));
  r.video().write_port(attribute_port, 0x10);  // Mode Control: not ours.
  r.video().write_port(attribute_port, 0x00);

  ASSERT_TRUE(r.video().halted());
  EXPECT_EQ(r.video().halt(),
            (ega::halt_record{.reason = ega::halt_reason::attribute_index,
                              .port = attribute_port,
                              .value = 0x10}));
}

TEST(ega_attribute_controller,
     ega_color_table_reproduces_the_standard_16_palette) {
  // A handful of the well-known standard EGA/CGA-compatible 16-colour
  // values (int10.h's default palette), spot-checked against the
  // documented DAC bit layout ega.h describes — a fact, not a game
  // asset, and cross-checked here from the table's own logic rather than
  // trusted blindly.
  EXPECT_EQ(ega_color_table[0], (rgb{0x00, 0x00, 0x00}));   // black
  EXPECT_EQ(ega_color_table[4], (rgb{0xAA, 0x00, 0x00}));   // red
  EXPECT_EQ(ega_color_table[2], (rgb{0x00, 0xAA, 0x00}));   // green
  EXPECT_EQ(ega_color_table[1], (rgb{0x00, 0x00, 0xAA}));   // blue
  EXPECT_EQ(ega_color_table[7], (rgb{0xAA, 0xAA, 0xAA}));   // light gray
  EXPECT_EQ(ega_color_table[56], (rgb{0x55, 0x55, 0x55}));  // dark gray
  EXPECT_EQ(ega_color_table[63], (rgb{0xFF, 0xFF, 0xFF}));  // white
  EXPECT_EQ(ega_color_table[20], (rgb{0xAA, 0x55, 0x00}));  // brown
}

// --- Halting: a clean stop, not a crash ------------------------------------

TEST(ega_halt, stays_on_the_first_fault_and_ignores_later_ones) {
  const rig r;
  r.set_seq(0x05, 0x00);  // sequencer_index fault.
  r.set_gc(0x09, 0x00);   // would be a gc_index fault, if it were recorded.

  EXPECT_EQ(r.video().halt().reason, ega::halt_reason::sequencer_index);
}

TEST(ega_halt, every_later_cycle_is_inert) {
  const rig r;
  r.set_seq(0x05, 0x00);

  r.video().write_memory(addr(0), 0xFF);
  EXPECT_EQ(r.video().read_memory(addr(0)), open_bus_value);
  EXPECT_EQ(r.video().read_port(seq_index_port), open_bus_value);
  r.video().write_port(gc_index_port, 0x08);  // also swallowed
  EXPECT_TRUE(r.video().halted());
}

// --- Reset: registers and latches go back; VRAM does not -----------------

TEST(ega_reset, clears_registers_latches_and_the_halt_but_keeps_vram) {
  const rig r;
  r.seed_plane(0, addr(0x1000), 0x42);
  r.set_map_mask(0b1010);
  r.set_attribute(0x00, 0x2A);
  r.set_seq(0x05, 0x00);  // halt it.
  ASSERT_TRUE(r.video().halted());

  r.video().reset();

  EXPECT_FALSE(r.video().halted());
  EXPECT_EQ(r.video().map_mask(), 0x00);
  EXPECT_EQ(r.video().palette_register(0), 0x00);
  EXPECT_EQ(r.video().overscan_register(), 0x00);
  EXPECT_EQ(r.video().latches(),
            (std::array<std::uint8_t, 4>{0x00, 0x00, 0x00, 0x00}));

  // The picture survives the line, exactly as machine::reset() leaves RAM
  // alone.
  EXPECT_EQ(r.video().plane_byte(0, 0x1000), 0x42);

  // And the device works again — reset cleared the halt, not just the
  // flag that reports it.
  r.set_map_mask(0x0F);
  r.video().write_memory(addr(0x1100), 0x77);
  EXPECT_EQ(r.video().plane_byte(0, 0x1100), 0x77);
}

}  // namespace
}  // namespace amberfolio::machine
