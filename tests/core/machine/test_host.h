// SPDX-License-Identifier: AGPL-3.0-only
//
// A fake host, and the two stand-in devices it needs to have something to
// consume.
//
// The point of it is the one thing a per-class unit test cannot check:
// that the five parts of the platform interface work *together*, driven
// the way M2-H1 (#54) and M2-H2 (#55) will drive them — a main loop that
// runs virtual time to the next frame boundary, presents if the frame
// changed, drains the console, and pulls a frame's worth of audio. If the
// loop in this file is awkward to write, the interface is wrong, and that
// is worth finding out here rather than in a host.
//
// `frame_source` and `tone_source` stand in for M2-D3's renderer and
// M2-D4's speaker. They are deliberately trivial — a pattern and a square
// wave — because what is under test is the boundary, not the devices.
// They do sit on the real scheduler at real virtual-time deadlines, which
// is the part that matters: the frame the host presents was produced by a
// deadline, not by the test poking the buffer.
//
// Test-side code, so the rule that keeps core/ free of host dependencies
// does not apply: std::vector and std::unique_ptr are fine.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/scheduler.h"

namespace amberfolio::machine::test {

/// The frame rate the renderer will run at (M2-D3, #48), in virtual time.
inline constexpr ticks frame_period = pit_input_hz / 60;

/// A stand-in renderer: at every frame deadline it paints a pattern that
/// depends on the frame number, sets a palette entry, and completes the
/// frame.
class frame_source final : public scheduled {
 public:
  frame_source(machine& box, ticks period) : box_(&box), period_(period) {}

  void start() { box_->deadlines().arm(*this, period_); }

  void on_deadline(ticks due) override {
    ++frames;
    const std::span<std::uint8_t> pixels = box_->display().writable_pixels();
    for (std::size_t i = 0; i < pixels.size(); ++i) {
      pixels[i] = static_cast<std::uint8_t>((i + frames) & 0x0Fu);
    }
    box_->display().set_palette_entry(
        1, {.red = static_cast<std::uint8_t>(frames), .green = 2, .blue = 3});
    box_->display().complete(due);

    // Standing in for M2-D7's console output as well, so the host loop
    // has bytes to drain: one line per frame.
    box_->console().put('f');

    box_->deadlines().arm(*this, due + period_);
  }

  unsigned frames{};

 private:
  machine* box_;
  ticks period_;
};

/// A stand-in speaker: a square wave of a given half-period, published as
/// edges on virtual time exactly as M2-D4 (#49) will.
class tone_source final : public scheduled {
 public:
  tone_source(machine& box, ticks half_period)
      : box_(&box), half_period_(half_period) {}

  void start() {
    box_->audio().publish(0, false);
    box_->deadlines().arm(*this, half_period_);
  }

  void on_deadline(ticks due) override {
    level_ = !level_;
    box_->audio().publish(due, level_);
    ++edges;
    box_->deadlines().arm(*this, due + half_period_);
  }

  unsigned edges{};

 private:
  machine* box_;
  ticks half_period_;
  bool level_{};
};

/// The main loop a host runs, and everything it kept.
class fake_host {
 public:
  /// The segment the host's program is loaded at. Nothing about it
  /// matters except that it is not the BIOS.
  static constexpr std::uint16_t program_segment = 0x1000;

  explicit fake_host(unsigned sample_rate = 44100)
      : box_(std::make_unique<machine>(memory_layout::pc)),
        sample_rate_(sample_rate) {
    box_->reset();

    // HLT, and nothing else: a machine waiting for an interrupt. It costs
    // virtual time on every step and never retires an instruction, which
    // is exactly the shape a host's run loop has to cope with and the
    // shortest program that has it.
    const std::uint32_t at = cpu::physical_address(program_segment, 0);
    box_->memory().ram()[at] = 0xF4;

    cpu::registers& regs = box_->processor().regs();
    regs[cpu::sreg::cs] = program_segment;
    regs.ip = 0;
    regs[cpu::sreg::ss] = program_segment;
    regs[cpu::reg16::sp] = 0xFFFE;

    // A host has not presented anything yet, and the frame `reset()` just
    // published is the blank one it is already looking at. Starting the
    // comparison from where the machine is rather than from zero is what
    // a real host does, and it keeps `presented` a list of frames the
    // renderer made rather than one blank plus them.
    presented_generation_ = box_->display().generation();
  }

  [[nodiscard]] machine& pc() const noexcept { return *box_; }

  /// Attach the stand-in renderer and speaker. Separate from the
  /// constructor so a test can have a host with neither.
  void add_frame_source(ticks period = frame_period) {
    frames_ = std::make_unique<frame_source>(*box_, period);
    box_->schedule(*frames_);
    frames_->start();
  }

  void add_tone_source(ticks half_period) {
    tone_ = std::make_unique<tone_source>(*box_, half_period);
    box_->schedule(*tone_);
    tone_->start();
  }

  /// One turn of the main loop.
  ///
  /// Run, then present, then drain, then pull — and that order is the
  /// whole shape of the interface: the machine produces on virtual time
  /// inside `run()`, and everything after it is the host taking what is
  /// there.
  void turn() {
    next_frame_ += frame_period;
    box_->run(next_frame_);

    const std::uint64_t generation = box_->display().generation();
    if (generation != presented_generation_) {
      presented_generation_ = generation;
      presented.push_back(
          {.generation = generation,
           .completed_at = box_->display().completed_at(),
           .checksum = checksum(box_->display().pixels()),
           .first_palette_entry = box_->display().palette()[1]});
    }

    std::array<std::uint8_t, 64> drained{};
    for (;;) {
      const std::size_t got = box_->console().read(drained);
      if (got == 0) {
        break;
      }
      console.insert(console.end(), drained.begin(),
                     drained.begin() + static_cast<std::ptrdiff_t>(got));
    }

    if (!pulls_audio) {
      return;
    }
    const std::size_t wanted = sample_rate_ / 60;
    std::vector<float> block(wanted, 0.0F);
    settled_frames += box_->audio().render(block, sample_rate_);
    audio.insert(audio.end(), block.begin(), block.end());
  }

  /// Whether the main loop pulls audio itself.
  ///
  /// A host with a real audio device does not — its audio thread does,
  /// and platform.h says **exactly one** thread may call `render()`. A
  /// test that hands the pull to a thread has to turn this off, and that
  /// it must is the contract being demonstrated rather than described.
  bool pulls_audio{true};

  void turns(unsigned how_many) {
    for (unsigned i = 0; i < how_many; ++i) {
      turn();
    }
  }

  /// A keypress, the way a host's event handler delivers one: down now,
  /// up now. Both land at the machine's current tick, which is the
  /// contract.
  void press(std::uint8_t scancode) {
    box_->post_key(scancode, key_action::down);
    box_->post_key(scancode, key_action::up);
  }

  struct presented_frame {
    std::uint64_t generation{};
    ticks completed_at{};
    std::uint64_t checksum{};
    rgb first_palette_entry{};
  };

  std::vector<presented_frame> presented;
  std::vector<std::uint8_t> console;
  std::vector<float> audio;
  std::size_t settled_frames{};

 private:
  /// FNV-1a over the pixels — a host blits them; a test needs one number.
  static std::uint64_t checksum(std::span<const std::uint8_t> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t byte : bytes) {
      hash ^= byte;
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  std::unique_ptr<machine> box_;
  std::unique_ptr<frame_source> frames_;
  std::unique_ptr<tone_source> tone_;
  unsigned sample_rate_;
  ticks next_frame_{};
  std::uint64_t presented_generation_{};
};

}  // namespace amberfolio::machine::test
