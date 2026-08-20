// SPDX-License-Identifier: AGPL-3.0-only
//
// The renderer: known plane contents composing into exact framebuffer
// bytes, palette registers composing into exact RGB entries, frame
// deadlines landing at exact virtual times, and — the exit criterion
// (#48) — a self-drawn test pattern's framebuffer hash checked end to
// end, from a write through the EGA's own pipeline to what a host would
// read out of `machine::display()`.
//
// The test pattern below is self-written for this file, per the
// clean-content rule (PLAN.md §6, and this issue's own note that any
// test pattern here must be self-authored) — geometric, not derived from
// anything.

#include "amberfolio/machine/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "amberfolio/cpu/address.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

constexpr std::uint16_t seq_index_port = 0x3C4;
constexpr std::uint16_t seq_data_port = 0x3C5;
constexpr std::uint16_t attribute_port = 0x3C0;
constexpr std::uint16_t status_port = 0x3DA;

constexpr std::uint32_t vram_first = 0xA0000;

/// A machine with an EGA attached and a renderer scheduled against it —
/// every test here wants all three.
struct rig {
  rig()
      : box(std::make_unique<machine>(memory_layout::pc, &log)),
        video(std::make_unique<ega>(*box)),
        render(*box, *video) {
    box->attach(*video);
    box->schedule(render);
    render.reset();
  }

  void set_map_mask(std::uint8_t mask) const {
    video->write_port(seq_index_port, 0x02);
    video->write_port(seq_data_port, mask);
  }

  /// Land `value` on exactly one plane at VRAM offset `at`.
  void seed_plane(unsigned plane, std::uint16_t at, std::uint8_t value) const {
    set_map_mask(static_cast<std::uint8_t>(1u << plane));
    video->write_memory(vram_first + at, value);
  }

  /// The attribute controller's index/data protocol (ega.h).
  void set_palette(std::uint8_t index, std::uint8_t value) const {
    static_cast<void>(video->read_port(status_port));
    video->write_port(attribute_port, index);
    video->write_port(attribute_port, value);
  }

  /// A single HLT at 0000:0000, so `machine::run()` advances the virtual
  /// clock — and so lets the scheduler dispatch the renderer's deadline —
  /// without executing anything that matters. A halted step still costs
  /// `step_cost()` ticks (machine.h), which is the whole mechanism.
  void arm_halted_program() const {
    box->memory().ram()[cpu::physical_address(0, 0)] = 0xF4;
    box->processor().reset();
    box->processor().regs()[cpu::sreg::cs] = 0;
    box->processor().regs().ip = 0;
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
  renderer render;
};

// --- Known plane contents -> exact framebuffer bytes ----------------------

TEST(renderer_compose, known_plane_contents_produce_exact_pixel_indices) {
  rig r;
  // p0 = 1010_1010, p1 = 0000_0000, p2 = 1111_1111, p3 = 0000_0000.
  // Pixel index = p0 | p1<<1 | p2<<2 | p3<<3, MSB of the byte first.
  r.seed_plane(0, 0x0000, 0xAA);
  r.seed_plane(2, 0x0000, 0xFF);

  r.render.on_deadline(0);

  constexpr std::array<std::uint8_t, 8> expected{5, 4, 5, 4, 5, 4, 5, 4};
  const std::span<const std::uint8_t> pixels = r.box->display().pixels();
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(pixels[i], expected[i]) << "pixel " << i;
  }
}

TEST(renderer_compose, a_pixel_untouched_by_any_plane_is_index_zero) {
  rig r;
  r.render.on_deadline(0);

  const std::span<const std::uint8_t> pixels = r.box->display().pixels();
  EXPECT_EQ(pixels[frame_pixels - 1], 0);
}

// --- Palette registers -> exact RGB entries --------------------------------

TEST(renderer_compose, palette_registers_translate_through_the_fact_table) {
  rig r;
  r.set_palette(0x00, 0x04);  // red.
  r.set_palette(0x05, 0x3F);  // white.

  r.render.on_deadline(0);

  const std::span<const rgb> palette = r.box->display().palette();
  EXPECT_EQ(palette[0], ega_color_table[0x04]);
  EXPECT_EQ(palette[5], ega_color_table[0x3F]);
}

// --- Frame deadlines land at exact virtual times ---------------------------

TEST(renderer_scheduling, the_first_frame_completes_at_exactly_one_period) {
  const rig r;
  r.arm_halted_program();

  // machine::run(until) loops only while now_ < until, and dispatch_due()
  // is called with the clock's value *before* that step's tick — so a
  // deadline due at 19886 is not actually dispatched until a step begins
  // with now_ already at or past it (the next multiple of step_cost()
  // above it), one whole step_cost() later than the raw deadline value.
  // A margin of one step_cost() past the deadline is enough to guarantee
  // the loop takes that step; the assertions below are what prove the
  // deadline itself still landed on the exact tick it was armed for,
  // regardless of this run-to-boundary margin.
  r.box->run(renderer::frame_period + r.box->step_cost());

  EXPECT_EQ(r.box->display().completed_at(), renderer::frame_period);
  EXPECT_EQ(r.box->display().generation(), 1u);
}

TEST(renderer_scheduling, later_frames_never_drift_from_a_fixed_multiple) {
  const rig r;
  r.arm_halted_program();

  r.box->run(3 * renderer::frame_period + r.box->step_cost());

  EXPECT_EQ(r.box->display().completed_at(), 3 * renderer::frame_period);
  EXPECT_EQ(r.box->display().generation(), 3u);
}

// --- The exit criterion: a drawn pattern's framebuffer hash ---------------

/// FNV-1a, 64-bit — a small, well-known public-domain hash, used here only
/// as a test's checksum and not as anything shipped in core/.
[[nodiscard]] std::uint64_t fnv1a(std::span<const std::uint8_t> bytes) {
  std::uint64_t hash = 0xCBF29CE484222325ULL;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

[[nodiscard]] std::uint64_t framebuffer_hash(const framebuffer& fb) {
  std::uint64_t hash = fnv1a(fb.pixels());
  for (const rgb& color : fb.palette()) {
    const std::array<std::uint8_t, 3> channels{color.red, color.green,
                                               color.blue};
    hash ^= fnv1a(channels);
    hash *= 0x100000001B3ULL;
  }
  return hash;
}

TEST(renderer_end_to_end, a_drawn_test_pattern_hashes_to_a_known_value) {
  rig r;

  // A self-drawn diagonal-stripe test pattern, not derived from anything:
  // sixteen scanlines, each plane's byte at column 0 rotated one further
  // bit than the last, so every one of the sixteen palette indices
  // appears somewhere in the first sixteen rows.
  for (unsigned row = 0; row < 16; ++row) {
    const auto offset = static_cast<std::uint16_t>(row * 40);
    const auto shift = static_cast<unsigned>(row % 8);
    const auto p0 = static_cast<std::uint8_t>(0x11u << shift);
    r.seed_plane(0, offset, p0);
    r.seed_plane(1, offset, static_cast<std::uint8_t>(row & 0x0F));
    r.seed_plane(2, offset, static_cast<std::uint8_t>(row * 7));
    r.seed_plane(3, offset, static_cast<std::uint8_t>(~row));
  }

  // A non-default palette, so the palette half of the hash is exercised
  // too — the standard 16-colour default (int10.h), used here directly
  // rather than through INT 10h because this test is about the renderer,
  // not the video BIOS.
  constexpr std::array<std::uint8_t, 16> palette{
      0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63};
  for (unsigned i = 0; i < palette.size(); ++i) {
    r.set_palette(static_cast<std::uint8_t>(i), palette[i]);
  }

  r.render.on_deadline(0);

  // FNV-1a of the framebuffer this test pattern composes to, pinned once
  // and asserted exactly — the exit criterion (#48): "the host-visible
  // framebuffer hash matches."
  EXPECT_EQ(framebuffer_hash(r.box->display()), 0x07732BCF86BF5B92ULL);
}

}  // namespace
}  // namespace amberfolio::machine
