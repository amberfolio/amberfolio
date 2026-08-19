// SPDX-License-Identifier: AGPL-3.0-only
//
// renderer.h has the design; this is straight planar composition for mode
// 0Dh and nothing past it — no CRTC, no panning, no split screen, exactly
// as the issue scopes it.

#include "amberfolio/machine/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"

namespace amberfolio::machine {
namespace {

/// Bytes per plane per scanline: 320 pixels at 8 per byte, one bit per
/// plane per pixel.
constexpr std::size_t bytes_per_row = frame_width / 8u;

}  // namespace

void renderer::reset() {
  box_->deadlines().arm(*this, box_->time() + frame_period);
}

void renderer::on_deadline(ticks due) {
  compose();
  box_->display().complete(due);
  box_->deadlines().arm(*this, due + frame_period);
}

void renderer::compose() const {
  framebuffer& target = box_->display();

  // The palette: sixteen registers, each a 6-bit EGA colour code, looked
  // up once a frame in the settled fact table (ega.h). Recomputing all
  // sixteen unconditionally rather than tracking which changed is the
  // straightforward answer and costs nothing that matters — sixteen
  // table lookups beside the 64,000 pixels below.
  for (unsigned i = 0; i < palette_entries; ++i) {
    target.set_palette_entry(i, ega_color_table[video_->palette_register(i)]);
  }

  // The pixels: each of the four planes contributes one bit per pixel,
  // plane *n* to bit *n* of the four-bit palette index — the standard EGA
  // plane-to-bit assignment mode 0Dh uses. A byte in a plane is eight
  // horizontal pixels, MSB first (bit 7 is the leftmost pixel of the
  // byte), which is why `shift` counts down from 7.
  const std::span<std::uint8_t> pixels = target.writable_pixels();
  for (std::size_t row = 0; row < frame_height; ++row) {
    for (std::size_t byte_col = 0; byte_col < bytes_per_row; ++byte_col) {
      const std::size_t offset = row * bytes_per_row + byte_col;
      const std::array<std::uint8_t, ega::plane_count> plane_bytes{
          video_->plane_byte(0, static_cast<std::uint16_t>(offset)),
          video_->plane_byte(1, static_cast<std::uint16_t>(offset)),
          video_->plane_byte(2, static_cast<std::uint16_t>(offset)),
          video_->plane_byte(3, static_cast<std::uint16_t>(offset)),
      };

      for (std::size_t bit = 0; bit < 8; ++bit) {
        const unsigned shift = 7u - static_cast<unsigned>(bit);
        std::uint8_t index = 0;
        for (unsigned plane = 0; plane < ega::plane_count; ++plane) {
          const unsigned pixel_bit = (plane_bytes[plane] >> shift) & 1u;
          index = static_cast<std::uint8_t>(index | (pixel_bit << plane));
        }

        const std::size_t col = byte_col * 8 + bit;
        pixels[row * std::size_t{frame_width} + col] = index;
      }
    }
  }
}

}  // namespace amberfolio::machine
