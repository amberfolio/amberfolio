// SPDX-License-Identifier: AGPL-3.0-only
//
// The C ABI. The wasm smoke check already calls it from JS, but only on
// the wasm target and only as a whole-module check — these run on every
// native target and pin the things a JS host has to do by hand: the
// version packing, the status codes, who owns which pointer, and what a
// call on a stopped or absent machine answers.
//
// The behaviour behind each entry point is tested in
// tests/core/machine/platform_test.cpp against the C++ interface. What is
// under test here is the *translation*: that the C face says the same
// thing the C++ face does, and that nothing about it traps.

#include "amberfolio/abi.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/version.h"

namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::Ge;
using ::testing::Le;

/// A machine for the length of a test, destroyed however the test ends.
///
/// There is one machine per loaded module (abi.h), so a test that leaked
/// it would make every test after it fail with a null handle. That is a
/// good property — it means "who destroys it" is not a question the ABI
/// leaves open — and this is how a test lives with it.
class machine_handle {
 public:
  machine_handle() : box_(af_machine_create()) {}
  ~machine_handle() { af_machine_destroy(box_); }

  machine_handle(const machine_handle&) = delete;
  machine_handle& operator=(const machine_handle&) = delete;
  machine_handle(machine_handle&&) = delete;
  machine_handle& operator=(machine_handle&&) = delete;

  [[nodiscard]] af_machine* get() const noexcept { return box_; }

 private:
  af_machine* box_;
};

TEST(Abi, VersionAgreesWithTheCxxApi) {
  const std::uint32_t packed = af_version();
  const amberfolio::version v = amberfolio::linked_version();

  EXPECT_EQ(AF_VERSION_MAJOR(packed), static_cast<std::uint32_t>(v.major));
  EXPECT_EQ(AF_VERSION_MINOR(packed), static_cast<std::uint32_t>(v.minor));
  EXPECT_EQ(AF_VERSION_PATCH(packed), static_cast<std::uint32_t>(v.patch));
}

// abi.h documents the layout as 0x00MMmmpp. The top byte being clear is
// part of that contract, not an accident of the current version number.
TEST(Abi, VersionLeavesTheTopByteClear) {
  EXPECT_THAT(af_version(), AllOf(Ge(0x0u), Le(0x00FFFFFFu)));
}

// The accessors are macros, so nothing type-checks them: a swapped shift
// would still compile and would still agree with itself. Feed them a value
// whose three bytes are distinguishable.
TEST(Abi, AccessorsUnpackEachByteFromItsOwnField) {
  constexpr std::uint32_t packed = 0x00123456u;

  EXPECT_EQ(AF_VERSION_MAJOR(packed), 0x12u);
  EXPECT_EQ(AF_VERSION_MINOR(packed), 0x34u);
  EXPECT_EQ(AF_VERSION_PATCH(packed), 0x56u);
}

// Whatever is in the bits above the packed triple is not the accessors'
// business — a future revision could use them for flags without breaking
// a host that unpacks a version the documented way.
TEST(Abi, AccessorsIgnoreBitsAboveTheTriple) {
  EXPECT_EQ(AF_VERSION_MAJOR(0xFF123456u), 0x12u);
  EXPECT_EQ(AF_VERSION_MINOR(0xFF123456u), 0x34u);
  EXPECT_EQ(AF_VERSION_PATCH(0xFF123456u), 0x56u);
}

// The constants a JS host would otherwise hard-code. They are functions
// because a browser has no headers, and the point of testing them is that
// the two sides then cannot disagree.
TEST(Abi, ReportsTheFactsAHostWouldOtherwiseGuess) {
  EXPECT_EQ(af_frame_width(), amberfolio::machine::frame_width);
  EXPECT_EQ(af_frame_height(), amberfolio::machine::frame_height);
  EXPECT_EQ(af_palette_entries(), amberfolio::machine::palette_entries);
  EXPECT_EQ(af_ticks_per_second(),
            static_cast<double>(amberfolio::machine::pit_input_hz));
}

// abi.h promises that a null handle answers rather than traps, because a
// JS host whose create failed should get an error code and not a crash in
// the middle of its render loop. Every entry point, so that the promise is
// checked and not merely stated.
TEST(Abi, EveryCallToleratesANullHandle) {
  EXPECT_EQ(af_machine_attach_reference_devices(nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_reset(nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_run_until(nullptr, 10.0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_time(nullptr), 0.0);
  EXPECT_EQ(af_machine_stopped(nullptr), 0);
  EXPECT_EQ(af_machine_stop_reason(nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_set_speed(nullptr, AF_SPEED_AT), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_framebuffer(nullptr), nullptr);
  EXPECT_EQ(af_machine_palette(nullptr), nullptr);
  EXPECT_EQ(af_machine_frame_generation(nullptr), 0.0);
  EXPECT_EQ(af_machine_audio_underruns(nullptr), 0.0);
  EXPECT_EQ(af_machine_audio_resyncs(nullptr), 0.0);
  EXPECT_EQ(af_machine_post_key(nullptr, 0x1E, AF_KEY_DOWN), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_set_wall_clock(nullptr, 1990, 1, 1, 0, 0, 0, 0),
            AF_NO_MACHINE);
  EXPECT_EQ(af_machine_console_pending(nullptr), 0u);
  EXPECT_EQ(af_machine_console_dropped(nullptr), 0.0);
  EXPECT_EQ(af_machine_load_program(nullptr, nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_write_memory(nullptr, 0, nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_read_memory(nullptr, 0, nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_set_entry(nullptr, 0, 0, 0, 0), AF_NO_MACHINE);

  std::array<float, 4> samples{};
  EXPECT_EQ(af_machine_render_audio(nullptr, samples.data(), 4, 44100), 0u);
  std::array<std::uint8_t, 4> bytes{};
  EXPECT_EQ(af_machine_read_console(nullptr, bytes.data(), 4), 0u);

  // And destroy takes one without complaint, so a teardown path needs no
  // check of its own.
  af_machine_destroy(nullptr);
}

TEST(Abi, ThereIsOneMachineAndDestroyMakesRoomForTheNext) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  EXPECT_EQ(af_machine_create(), nullptr);

  af_machine* second = nullptr;
  {
    const machine_handle scoped;
    EXPECT_EQ(scoped.get(), nullptr);
  }

  // The first is still alive: a refused create must not have destroyed
  // anything.
  EXPECT_EQ(af_machine_reset(box.get()), AF_OK);
  EXPECT_EQ(second, nullptr);
}

TEST(Abi, RunsToAnAbsoluteTickAndReportsTheClock) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  // A HLT at 1000:0000 — a machine waiting for an interrupt, which
  // advances the clock and never runs off into memory nobody wrote.
  const std::array<std::uint8_t, 1> halt{0xF4};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, halt.data(),
                                    static_cast<std::uint32_t>(halt.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  ASSERT_EQ(af_machine_set_speed(box.get(), AF_SPEED_PC_XT), AF_OK);

  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_OK);
  EXPECT_GE(af_machine_time(box.get()), 10'000.0);
  EXPECT_EQ(af_machine_stopped(box.get()), 0);
  EXPECT_EQ(af_machine_stop_reason(box.get()), AF_OK);

  // Absolute, not a duration: running to a tick already passed does
  // nothing at all.
  const double reached = af_machine_time(box.get());
  EXPECT_EQ(af_machine_run_until(box.get(), 1.0), AF_OK);
  EXPECT_EQ(af_machine_time(box.get()), reached);

  EXPECT_EQ(af_machine_set_speed(box.get(), 99), AF_INVALID);
}

TEST(Abi, MemoryGoesInAndComesBackOut) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  const std::array<std::uint8_t, 4> written{0xDE, 0xAD, 0xBE, 0xEF};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x500, written.data(),
                                    static_cast<std::uint32_t>(written.size())),
            AF_OK);

  std::array<std::uint8_t, 4> read{};
  ASSERT_EQ(af_machine_read_memory(box.get(), 0x500, read.data(),
                                   static_cast<std::uint32_t>(read.size())),
            AF_OK);
  EXPECT_EQ(read, written);

  // The megabyte is the bound, and it is checked rather than trusted: a
  // JS host passing a bad offset is a bug report, not a corrupted heap.
  EXPECT_EQ(af_machine_write_memory(box.get(), 0xFFFFF, written.data(), 2),
            AF_INVALID);
  EXPECT_EQ(af_machine_read_memory(box.get(), 0x100000, read.data(), 1),
            AF_INVALID);
  EXPECT_EQ(af_machine_write_memory(box.get(), 0, nullptr, 4), AF_INVALID);
}

// Reserved and loud, not reserved and silent: PLAN.md §3's rule at the
// ABI. The MZ loader is M2-D6 (#51).
TEST(Abi, LoadProgramSaysItIsNotImplementedYet) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  const std::array<std::uint8_t, 2> image{'M', 'Z'};
  EXPECT_EQ(af_machine_load_program(box.get(), image.data(),
                                    static_cast<std::uint32_t>(image.size())),
            AF_UNIMPLEMENTED);
  EXPECT_EQ(af_machine_load_program(box.get(), nullptr, 0), AF_INVALID);
}

TEST(Abi, TheFramebufferIsCoreOwnedAndStable) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  const std::uint8_t* pixels = af_machine_framebuffer(box.get());
  const std::uint8_t* palette = af_machine_palette(box.get());
  ASSERT_NE(pixels, nullptr);
  ASSERT_NE(palette, nullptr);

  const double before = af_machine_frame_generation(box.get());
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);

  // Same pointers after a reset — a host caches the offset once and a
  // reset is not a reallocation — and a generation that moved, because
  // the frame it points at is a new (blank) one.
  EXPECT_EQ(af_machine_framebuffer(box.get()), pixels);
  EXPECT_EQ(af_machine_palette(box.get()), palette);
  EXPECT_GT(af_machine_frame_generation(box.get()), before);
}

TEST(Abi, AudioIsPulledIntoTheCallersBuffer) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  const std::array<std::uint8_t, 1> halt{0xF4};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, halt.data(),
                                    static_cast<std::uint32_t>(halt.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  ASSERT_EQ(af_machine_run_until(box.get(), 20'000.0), AF_OK);

  std::vector<float> samples(64, 1.0F);
  EXPECT_EQ(af_machine_render_audio(box.get(), samples.data(), 64, 44100), 64u);
  EXPECT_THAT(samples, Each(0.0F));

  // A rate the timeline will not honour writes nothing at all, so a host
  // that asked for something impossible finds out.
  std::vector<float> untouched(4, 1.0F);
  EXPECT_EQ(af_machine_render_audio(box.get(), untouched.data(), 4, 1), 0u);
  EXPECT_THAT(untouched, Each(1.0F));
  EXPECT_EQ(af_machine_render_audio(box.get(), nullptr, 4, 44100), 0u);
}

TEST(Abi, KeysAndTheWallClockArePushedIn) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  EXPECT_EQ(af_machine_post_key(box.get(), 0x1E, AF_KEY_DOWN), AF_OK);
  EXPECT_EQ(af_machine_post_key(box.get(), 0x1E, AF_KEY_UP), AF_OK);
  // The 0x80 bit is the release bit on the wire; `down` carries it here,
  // so a code with it set is a host that has not read the contract.
  EXPECT_EQ(af_machine_post_key(box.get(), 0x9E, AF_KEY_DOWN), AF_INVALID);
  EXPECT_EQ(af_machine_post_key(box.get(), 0, AF_KEY_DOWN), AF_INVALID);

  EXPECT_EQ(af_machine_set_wall_clock(box.get(), 1988, 9, 1, 10, 30, 0, 0),
            AF_OK);
  EXPECT_EQ(af_machine_set_wall_clock(box.get(), 1988, 2, 30, 0, 0, 0, 0),
            AF_INVALID);
  EXPECT_EQ(af_machine_set_wall_clock(box.get(), 1979, 1, 1, 0, 0, 0, 0),
            AF_INVALID);
  // Wider than the field it lands in, caught before the conversion.
  EXPECT_EQ(af_machine_set_wall_clock(box.get(), 1988, 300, 1, 0, 0, 0, 0),
            AF_INVALID);
}

TEST(Abi, TheConsoleDrainsThroughTheCallersBuffer) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  // Nothing writes to it until M2-D7 (#52), so the honest assertion is
  // that it is empty and says so rather than answering with rubbish.
  std::array<std::uint8_t, 8> out{};
  EXPECT_EQ(af_machine_console_pending(box.get()), 0u);
  EXPECT_EQ(af_machine_console_dropped(box.get()), 0.0);
  EXPECT_EQ(af_machine_read_console(box.get(), out.data(),
                                    static_cast<std::uint32_t>(out.size())),
            0u);
  EXPECT_EQ(af_machine_read_console(box.get(), nullptr, 8), 0u);
}

// A stopped machine keeps answering. The host's next job is to report the
// stop, and the console and the last frame are how it does that — so
// nothing here may become unusable at the moment it matters most.
TEST(Abi, AStoppedMachineStillAnswersEveryPull) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  // INT 21h with nothing behind it. The vector points at a real stub in
  // the BIOS region and no native handler backs it, so the machine stops
  // rather than IRET-and-hope — the "log, don't fake" path (PLAN.md §3),
  // and the stop a host is most likely to have to report during M3.
  const std::array<std::uint8_t, 2> refused{0xCD, 0x21};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, refused.data(),
                                    static_cast<std::uint32_t>(refused.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);

  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_STOPPED);
  EXPECT_NE(af_machine_stopped(box.get()), 0);
  EXPECT_NE(af_machine_stop_reason(box.get()), AF_OK);

  const double stalled = af_machine_time(box.get());
  EXPECT_EQ(af_machine_run_until(box.get(), 1'000'000.0), AF_STOPPED);
  EXPECT_EQ(af_machine_time(box.get()), stalled);

  EXPECT_NE(af_machine_framebuffer(box.get()), nullptr);
  std::array<std::uint8_t, 4> drained{};
  EXPECT_EQ(af_machine_read_console(box.get(), drained.data(), 4), 0u);
  std::array<float, 4> samples{};
  af_machine_render_audio(box.get(), samples.data(), 4, 44100);
  EXPECT_EQ(af_machine_post_key(box.get(), 0x1E, AF_KEY_DOWN), AF_OK);

  // And the RESET line clears it, which is the way out.
  EXPECT_EQ(af_machine_reset(box.get()), AF_OK);
  EXPECT_EQ(af_machine_stopped(box.get()), 0);
}

// M2-H2 (#55): the opt-in reference device set. The test above proves a
// *bare* machine's INT 21h stops; this is the other half — attaching the
// reference set turns that same vector into something real, which is
// what lets the wasm dev page (and this test) tell "the ABI's device
// wiring is present and correct" from "a bare machine happens to answer
// something."
TEST(Abi, AttachReferenceDevicesUnlocksTheDeviceSetAndIsIdempotent) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  EXPECT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);
  // Idempotent: a second call must not fault, leak, or disturb the first
  // one's wiring (abi.h's own documented contract for this call).
  EXPECT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);

  // mov ax, 0x000D ; int 10h ; hlt — sets video mode 0Dh. On a bare
  // machine this is exactly the shape of program
  // AStoppedMachineStillAnswersEveryPull uses to prove a vector with no
  // handler stops the machine; here it must not, because
  // install_int10() is part of what attach() just did.
  const std::array<std::uint8_t, 6> set_mode{0xB8, 0x0D, 0x00, 0xCD, 0x10, 0xF4};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, set_mode.data(),
                                    static_cast<std::uint32_t>(set_mode.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);

  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_OK);
  EXPECT_EQ(af_machine_stopped(box.get()), 0);
  EXPECT_EQ(af_machine_stop_reason(box.get()), AF_OK);

  // The renderer (attached by this call, not by af_machine_create()) has
  // had its own 60 Hz virtual-time deadline armed and had time to fire
  // at least once, so the frame generation has moved off zero.
  EXPECT_GT(af_machine_frame_generation(box.get()), 0.0);

  // reset() clears the stop and the mode, exactly as the RESET line does
  // on a real machine; the device set itself stays attached (it is
  // wiring, not run state — machine.h's own distinction), so the same
  // program runs clean a second time.
  EXPECT_EQ(af_machine_reset(box.get()), AF_OK);
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, set_mode.data(),
                                    static_cast<std::uint32_t>(set_mode.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_OK);
  EXPECT_EQ(af_machine_stopped(box.get()), 0);
}

}  // namespace
