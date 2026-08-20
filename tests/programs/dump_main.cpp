// SPDX-License-Identifier: AGPL-3.0-only
//
// Runs a machine program and writes what it produced somewhere a human
// can look at it: the frame as a PPM, the speaker as a WAV.
//
// The suite next door asserts these programs against hashes and probes,
// which is what a test should do — but a hash is unreadable by eye, and
// "the EGA write pipeline is correct" is a claim worth being able to
// *look* at once in a while. This is that. It asserts nothing and is not
// a test; it is the window into the same run the tests make.
//
//     amberfolio-dump <program> [<dir>]
//
// PPM and WAV because both are a header and then the samples, so this
// needs no image or audio library to write a file every viewer opens.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/platform.h"
#include "programs/machine_harness.h"
#include "programs/machine_programs.h"

namespace {

using namespace amberfolio;

/// The sample rate the WAV is written at, and it is not a choice: the
/// harness pulled these samples at `programs::audio_sample_rate`
/// (machine_harness.h), so that is the rate they are. Writing any other
/// number into the header would leave a file that plays the machine's
/// tone at the wrong pitch and says nothing about the machine.
constexpr unsigned sample_rate = programs::audio_sample_rate;

void write_u32(std::ofstream& out, std::uint32_t v) {
  const std::uint8_t b[4] = {
      static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(v >> 8),
      static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 24)};
  out.write(reinterpret_cast<const char*>(b), 4);
}

void write_u16(std::ofstream& out, std::uint16_t v) {
  const std::uint8_t b[2] = {static_cast<std::uint8_t>(v),
                             static_cast<std::uint8_t>(v >> 8)};
  out.write(reinterpret_cast<const char*>(b), 2);
}

/// The frame as a binary PPM: 320x200 true colour, palette already
/// applied, so what lands on disk is what the host would put on screen.
bool write_ppm(const std::filesystem::path& where,
               std::span<const std::uint8_t> pixels,
               std::span<const machine::rgb> palette) {
  if (pixels.size() < machine::frame_pixels || palette.size() < 16) {
    return false;
  }
  std::ofstream out(where, std::ios::binary | std::ios::trunc);
  out << "P6\n"
      << machine::frame_width << ' ' << machine::frame_height << "\n255\n";
  for (std::size_t i = 0; i < machine::frame_pixels; ++i) {
    const machine::rgb c = palette[pixels[i] & 0x0FU];
    const std::uint8_t rgb[3] = {c.red, c.green, c.blue};
    out.write(reinterpret_cast<const char*>(rgb), 3);
  }
  return static_cast<bool>(out);
}

/// The pulled samples as a 16-bit mono WAV.
bool write_wav(const std::filesystem::path& where,
               std::span<const float> samples) {
  if (samples.empty()) {
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

}  // namespace

int main(int argc, char** argv) try {
  if (argc < 2 || argc > 3) {
    std::fprintf(stderr, "usage: amberfolio-dump <program> [<dir>]\n\n");
    std::fprintf(stderr, "programs:\n");
    for (const programs::machine_program& p :
         programs::all_machine_programs()) {
      std::fprintf(stderr, "  %-12s %.*s\n", std::string(p.name).c_str(),
                   static_cast<int>(p.about.size()), p.about.data());
    }
    return EXIT_FAILURE;
  }

  const std::string_view wanted = argv[1];
  const programs::machine_program* p = programs::find_machine_program(wanted);
  if (p == nullptr) {
    std::fprintf(stderr, "no program called '%.*s'\n",
                 static_cast<int>(wanted.size()), wanted.data());
    return EXIT_FAILURE;
  }

  const std::filesystem::path dir =
      argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path(".");
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  const programs::machine_outcome got = programs::run_machine_setup(p->setup);

  std::printf("%.*s: %llu steps, %llu frames, %zu samples\n",
              static_cast<int>(p->name.size()), p->name.data(),
              static_cast<unsigned long long>(got.steps),
              static_cast<unsigned long long>(got.frames), got.audio.size());

  if (!got.console.empty()) {
    std::printf("console: ");
    std::fwrite(got.console.data(), 1, got.console.size(), stdout);
    std::printf("\n");
  }

  const std::string base(p->name);
  if (got.frames > 0) {
    const std::filesystem::path ppm = dir / (base + ".ppm");
    if (write_ppm(ppm, got.frame_pixels, got.frame_palette)) {
      std::printf("frame: %s\n", ppm.string().c_str());
    }
  } else {
    std::printf("frame: none (the program exited inside the first frame)\n");
  }

  if (!got.audio.empty()) {
    const std::filesystem::path wav = dir / (base + ".wav");
    if (write_wav(wav, got.audio)) {
      std::printf("audio: %s (%u Hz mono)\n", wav.string().c_str(),
                  sample_rate);
    }
  } else {
    std::printf("audio: none (this program makes no sound)\n");
  }

  return EXIT_SUCCESS;
} catch (const std::exception& e) {
  std::fprintf(stderr, "amberfolio-dump: %s\n", e.what());
  return EXIT_FAILURE;
}
