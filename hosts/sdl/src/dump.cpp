// SPDX-License-Identifier: AGPL-3.0-only

#include "dump.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <span>

#include "amberfolio/machine/platform.h"

namespace amberfolio::sdl {
namespace {

void write_u32(std::ofstream& out, std::uint32_t v) {
  const std::array<std::uint8_t, 4> b = {
      static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8u),
      static_cast<std::uint8_t>(v >> 16u), static_cast<std::uint8_t>(v >> 24u)};
  out.write(reinterpret_cast<const char*>(b.data()), 4);
}

void write_u16(std::ofstream& out, std::uint16_t v) {
  const std::array<std::uint8_t, 2> b = {static_cast<std::uint8_t>(v),
                                         static_cast<std::uint8_t>(v >> 8u)};
  out.write(reinterpret_cast<const char*>(b.data()), 2);
}

}  // namespace

bool write_ppm(const std::filesystem::path& where,
               std::span<const std::uint8_t> pixels,
               std::span<const machine::rgb> palette) {
  if (pixels.size() < machine::frame_pixels ||
      palette.size() < machine::palette_entries) {
    return false;
  }
  std::ofstream out(where, std::ios::binary | std::ios::trunc);
  out << "P6\n"
      << machine::frame_width << ' ' << machine::frame_height << "\n255\n";
  for (std::size_t i = 0; i < machine::frame_pixels; ++i) {
    const machine::rgb c = palette[pixels[i] & 0x0FU];
    const std::array<std::uint8_t, 3> rgb = {c.red, c.green, c.blue};
    out.write(reinterpret_cast<const char*>(rgb.data()), 3);
  }
  return static_cast<bool>(out);
}

bool write_wav(const std::filesystem::path& where,
               std::span<const float> samples, unsigned sample_rate) {
  if (samples.empty() || sample_rate == 0) {
    return false;
  }
  std::ofstream out(where, std::ios::binary | std::ios::trunc);
  const auto data_bytes = static_cast<std::uint32_t>(samples.size() * 2);

  out.write("RIFF", 4);
  write_u32(out, 36 + data_bytes);
  out.write("WAVEfmt ", 8);
  write_u32(out, 16);  // PCM header size
  write_u16(out, 1);   // PCM
  write_u16(out, 1);   // mono
  write_u32(out, sample_rate);
  write_u32(out, sample_rate * 2);  // byte rate
  write_u16(out, 2);                // block align
  write_u16(out, 16);               // bits
  out.write("data", 4);
  write_u32(out, data_bytes);

  for (const float s : samples) {
    const float clamped = s < -1.0F ? -1.0F : (s > 1.0F ? 1.0F : s);
    write_u16(out, static_cast<std::uint16_t>(
                       static_cast<std::int16_t>(clamped * 32767.0F)));
  }
  return static_cast<bool>(out);
}

}  // namespace amberfolio::sdl
