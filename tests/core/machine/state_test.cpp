// SPDX-License-Identifier: AGPL-3.0-only
//
// The canonical machine-state serialization (state.h, M4-R1 #100): that
// two machines in the same state hash the same, that a change anywhere
// the program can see changes the hash and names its section, and that
// the things declared out of it — the seam engine, the trace ring, a
// diagnostics sink — leave it alone.
//
// Every byte here is this file's own (PLAN.md §6).

#include "amberfolio/machine/state.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/speaker.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

/// The whole reference device set on the heap, the way a host wires it
/// (machine_harness.cpp), so the serialization walks every real device.
struct wired {
  explicit wired(diagnostics* log = nullptr)
      : box(std::make_unique<machine>(memory_layout::pc, log)),
        irq(std::make_unique<pic::controller>(*box)),
        timer(std::make_unique<pit>(*box, *irq)),
        sound(std::make_unique<speaker>(*box, *timer)),
        video(std::make_unique<ega>(*box)),
        screen(std::make_unique<renderer>(*box, *video)) {
    box->attach(*irq);
    box->attach(*timer);
    box->attach(*sound);
    box->attach(*video);
    box->schedule(timer->channel0_deadline());
    box->schedule(timer->channel2_deadline());
    box->schedule(*sound);
    box->schedule(*screen);
    box->reset();
    screen->reset();
  }

  /// `MOV AX, 1234h ; JMP $` at 1000:0000, with a stack.
  void program() const {
    const std::uint32_t at = cpu::physical_address(0x1000, 0);
    const std::array<std::uint8_t, 5> code{0xB8, 0x34, 0x12, 0xEB, 0xFE};
    for (std::size_t i = 0; i < code.size(); ++i) {
      box->memory().ram()[at + i] = code[i];
    }
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = 0x1000;
    r.ip = 0;
    r[cpu::sreg::ss] = 0x1000;
    r[cpu::reg16::sp] = 0xFFFE;
  }

  std::unique_ptr<machine> box;
  std::unique_ptr<pic::controller> irq;
  std::unique_ptr<pit> timer;
  std::unique_ptr<speaker> sound;
  std::unique_ptr<ega> video;
  std::unique_ptr<renderer> screen;
};

/// A sink that keeps the bytes, per section, for a test that wants to
/// look rather than hash.
class capturing_sink final : public state_sink {
 public:
  void begin(state_section which) override { current_ = which; }
  void bytes(std::span<const std::uint8_t> data) override {
    auto& section = sections[static_cast<std::size_t>(current_)];
    section.insert(section.end(), data.begin(), data.end());
  }
  std::array<std::vector<std::uint8_t>, state_section_count> sections;

 private:
  state_section current_{};
};

TEST(StateHash, IsTheSameForTwoMachinesInTheSameState) {
  const wired a;
  const wired b;
  a.program();
  b.program();
  a.box->run(50'000);
  b.box->run(50'000);
  EXPECT_EQ(hash_state(*a.box), hash_state(*b.box));
}

TEST(StateHash, IsStableWhenTakenTwice) {
  const wired a;
  a.program();
  a.box->run(20'000);
  const state_hashes first = hash_state(*a.box);
  const state_hashes second = hash_state(*a.box);
  EXPECT_EQ(first, second) << "serializing changes nothing";
}

TEST(StateHash, NamesTheSectionThatChanged) {
  const wired a;
  const wired b;
  a.program();
  b.program();
  const state_hashes before = hash_state(*a.box);

  // RAM, somewhere the program is not.
  b.box->memory().ram()[0x50000] ^= 1;
  state_hashes after = hash_state(*b.box);
  EXPECT_NE(before.whole, after.whole);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::ram)],
            after.sections[static_cast<std::size_t>(state_section::ram)]);
  EXPECT_EQ(before.sections[static_cast<std::size_t>(state_section::cpu)],
            after.sections[static_cast<std::size_t>(state_section::cpu)]);
  b.box->memory().ram()[0x50000] ^= 1;

  // A register.
  b.box->processor().regs()[cpu::reg16::bx] = 7;
  after = hash_state(*b.box);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::cpu)],
            after.sections[static_cast<std::size_t>(state_section::cpu)]);
  EXPECT_EQ(before.sections[static_cast<std::size_t>(state_section::ram)],
            after.sections[static_cast<std::size_t>(state_section::ram)]);
  b.box->processor().regs()[cpu::reg16::bx] = 0;

  // A device register: the EGA's map mask, through its own port.
  b.box->write_port8(0x3C4, 0x02);
  b.box->write_port8(0x3C5, 0x05);
  after = hash_state(*b.box);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::devices)],
            after.sections[static_cast<std::size_t>(state_section::devices)]);

  // A queued key, and the wall clock.
  const wired c;
  c.program();
  c.box->post_key(0x1E, key_action::down);
  after = hash_state(*c.box);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::input)],
            after.sections[static_cast<std::size_t>(state_section::input)]);
  const wired d;
  d.program();
  d.box->set_wall_time({.year = 1990, .month = 3, .day = 4});
  after = hash_state(*d.box);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::wall)],
            after.sections[static_cast<std::size_t>(state_section::wall)]);
}

TEST(StateHash, LeavesOutWhatIsNotMachineState) {
  // The same run, with and without a diagnostics sink, with the trace
  // ring on, and with a seam enabled and disabled again before the first
  // step. None of it is the program's, so none of it may move the hash.
  test::recording_diagnostics log;
  const wired plain;
  const wired watched(&log);
  plain.program();
  watched.program();
  watched.box->trace().enable(true);

  sha256_digest claimed{};
  claimed.bytes[0] = 9;
  watched.box->seams().loaded(claimed, image_load_segment);
  // The code-wheel seam cannot be enabled on this digest; that the
  // engine was consulted at all is the point.
  EXPECT_EQ(watched.box->seams().enable("code-wheel"),
            seam_reason::wrong_binary);

  plain.box->run(30'000);
  watched.box->run(30'000);
  EXPECT_EQ(hash_state(*plain.box), hash_state(*watched.box));
}

TEST(StateHash, AnAttachedDeviceWritesItself) {
  // A stand-in device with one byte of state: the byte is in the
  // devices section, and changing it changes the hash.
  auto box = std::make_unique<machine>(memory_layout::pc);
  test::recording_device dev;
  dev.wants(port_range{.first = 0x300, .last = 0x300});
  ASSERT_TRUE(box->attach(dev));
  const state_hashes before = hash_state(*box);
  dev.answer = 0x77;
  const state_hashes after = hash_state(*box);
  EXPECT_NE(before.sections[static_cast<std::size_t>(state_section::devices)],
            after.sections[static_cast<std::size_t>(state_section::devices)]);
}

TEST(StateLayout, WritesEverySectionInOrderWithTheClockFirst) {
  const wired a;
  a.program();
  a.box->run(10'000);
  capturing_sink sink;
  serialize_state(*a.box, sink);

  // Every section has something in it (the scheduler and the devices are
  // wired; the input queue and the console are empty but carry a count).
  for (std::size_t i = 0; i < state_section_count; ++i) {
    EXPECT_FALSE(sink.sections[i].empty())
        << state_section_name(static_cast<state_section>(i));
  }
  // The clock section is now, subtick, steps — little-endian 64-bit each.
  const std::vector<std::uint8_t>& clock =
      sink.sections[static_cast<std::size_t>(state_section::clock)];
  ASSERT_EQ(clock.size(), 24u);
  std::uint64_t now = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    now |= static_cast<std::uint64_t>(clock[i]) << (8 * i);
  }
  EXPECT_EQ(now, a.box->time());
  // RAM is the megabyte, whole.
  EXPECT_EQ(sink.sections[static_cast<std::size_t>(state_section::ram)].size(),
            cpu::address_space_size);
}

TEST(StateLayout, NamesEverySection) {
  EXPECT_STREQ(state_section_name(state_section::clock), "clock");
  EXPECT_STREQ(state_section_name(state_section::devices), "devices");
  EXPECT_STREQ(state_section_name(state_section::stop), "stop");
  EXPECT_EQ(state_format_version, 1u);
}

}  // namespace
}  // namespace amberfolio::machine
