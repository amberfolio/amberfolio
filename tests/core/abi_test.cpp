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

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/abi_bridge.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/version.h"
#include "programs/machine_programs.h"

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
  EXPECT_EQ(af_machine_seam_count(nullptr), 0u);
  EXPECT_EQ(af_machine_seam_state(nullptr, 0), AF_SEAM_NONE);
  EXPECT_EQ(af_machine_seam_armed(nullptr, 0), 0);
  EXPECT_EQ(af_machine_seam_fired(nullptr, 0), 0.0);
  EXPECT_EQ(af_machine_seam_enable(nullptr, "probe"), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_seam_disable(nullptr, "probe"), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_post_key(nullptr, 0x1E, AF_KEY_DOWN), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_set_wall_clock(nullptr, 1990, 1, 1, 0, 0, 0, 0),
            AF_NO_MACHINE);
  EXPECT_EQ(af_machine_console_pending(nullptr), 0u);
  EXPECT_EQ(af_machine_console_dropped(nullptr), 0.0);
  EXPECT_EQ(af_machine_load_from_vfs(nullptr, "A.EXE", nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_load_error(nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_write_memory(nullptr, 0, nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_read_memory(nullptr, 0, nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_set_entry(nullptr, 0, 0, 0, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_steps(nullptr), 0.0);
  EXPECT_EQ(af_machine_set_trace(nullptr, 1), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_vfs_clear(nullptr), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_vfs_put(nullptr, "A.DAT", nullptr, 0), AF_NO_MACHINE);
  EXPECT_EQ(af_machine_vfs_count(nullptr), 0u);
  EXPECT_EQ(af_machine_vfs_size_at(nullptr, 0), 0u);
  EXPECT_EQ(af_machine_vfs_bytes_used(nullptr), 0.0);

  std::array<char, 128> text{};
  EXPECT_EQ(af_machine_vfs_name_at(nullptr, 0, text.data(),
                                   static_cast<std::uint32_t>(text.size())),
            0u);
  EXPECT_EQ(af_machine_vfs_fingerprint(nullptr, "A.DAT", text.data(),
                                       static_cast<std::uint32_t>(text.size())),
            0u);
  EXPECT_EQ(af_machine_stop_report(nullptr, AF_RUN_END_STOPPED, text.data(),
                                   static_cast<std::uint32_t>(text.size())),
            0u);
  EXPECT_EQ(af_machine_trace_report(nullptr, text.data(),
                                    static_cast<std::uint32_t>(text.size())),
            0u);

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

// --- The filesystem (M3-F2, #84) ---------------------------------------
//
// The wasm counterpart of the directory the SDL host is pointed at. The
// cases here are the boundary's own: that a bare machine says it has no
// filesystem rather than pretending, that names are canonicalized in core
// rather than by the caller, and that what goes in comes back out.
// `memory_vfs_test.cpp` is where the backend itself is tested.

/// A machine with the reference device set attached, which is the only
/// kind that has a filesystem at all.
struct equipped_machine {
  equipped_machine() {
    EXPECT_NE(box.get(), nullptr);
    EXPECT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);
  }

  /// The same, plus the RESET line — the self test that programs the PIT
  /// and the 8259 through real bus cycles. A machine that skipped it is a
  /// machine no host builds, which matters to anything comparing device
  /// state (`state_section::devices`) and to nothing else.
  void power_on() const { EXPECT_EQ(af_machine_reset(box.get()), AF_OK); }

  [[nodiscard]] af_machine* get() const { return box.get(); }

  machine_handle box;
};

TEST(AbiVfs, SaysThereIsNoFilesystemOnABareMachine) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  // A machine exists; a filesystem does not. AF_NO_MACHINE would be a lie
  // about the first, which is the whole reason AF_NO_FILESYSTEM has its
  // own number.
  EXPECT_EQ(af_machine_vfs_clear(box.get()), AF_NO_FILESYSTEM);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "A.DAT", nullptr, 0),
            AF_NO_FILESYSTEM);
  EXPECT_EQ(af_machine_load_from_vfs(box.get(), "A.EXE", nullptr),
            AF_NO_FILESYSTEM);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
}

TEST(AbiVfs, PutsAFileAndListsItBack) {
  const equipped_machine box;

  const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
  EXPECT_EQ(af_machine_vfs_put(box.get(), "GAME.DAT", bytes.data(),
                               static_cast<std::uint32_t>(bytes.size())),
            AF_OK);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 1u);
  EXPECT_EQ(af_machine_vfs_size_at(box.get(), 0), 4u);
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), 4.0);

  std::array<char, 16> name{};
  EXPECT_EQ(af_machine_vfs_name_at(box.get(), 0, name.data(),
                                   static_cast<std::uint32_t>(name.size())),
            8u);
  EXPECT_STREQ(name.data(), "GAME.DAT");
}

TEST(AbiVfs, CanonicalizesTheNameInCoreRatherThanTakingItAsGiven) {
  const equipped_machine box;

  // Lower case in, upper case out — the caller does no name logic
  // (abi.h). A page that decided for itself what `game.dat` meant would
  // be a second implementation of the rule that says whether two
  // programs are looking at the same file.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "game.dat", nullptr, 0), AF_OK);
  std::array<char, 16> name{};
  EXPECT_EQ(af_machine_vfs_name_at(box.get(), 0, name.data(),
                                   static_cast<std::uint32_t>(name.size())),
            8u);
  EXPECT_STREQ(name.data(), "GAME.DAT");
}

TEST(AbiVfs, RefusesANameNoDosNameCouldEqual) {
  const equipped_machine box;

  // The useful answer, not a failure: a real game directory has files in
  // it DOS could never have named, and this is where a host gets its
  // "these were skipped" list.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "code wheel.pdf", nullptr, 0),
            AF_INVALID);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "TOOLONGNAME.DAT", nullptr, 0),
            AF_INVALID);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "", nullptr, 0), AF_INVALID);
  EXPECT_EQ(af_machine_vfs_put(box.get(), nullptr, nullptr, 0), AF_INVALID);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
}

TEST(AbiVfs, FingerprintsAFileTheSameWayTheDesktopHostDoes) {
  const equipped_machine box;

  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "ABC.DAT", bytes.data(), 3), AF_OK);

  std::array<char, 128> hex{};
  EXPECT_EQ(af_machine_vfs_fingerprint(box.get(), "ABC.DAT", hex.data(),
                                       static_cast<std::uint32_t>(hex.size())),
            64u);
  // FIPS 180-4 appendix B.1, the digest of "abc" — the same answer every
  // other SHA-256 in the world gives, which is the entire point of a
  // fingerprint.
  EXPECT_STREQ(
      hex.data(),
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

  EXPECT_EQ(af_machine_vfs_fingerprint(box.get(), "GONE.DAT", hex.data(),
                                       static_cast<std::uint32_t>(hex.size())),
            0u);
}

TEST(AbiVfs, ClearingEmptiesIt) {
  const equipped_machine box;
  ASSERT_EQ(af_machine_vfs_put(box.get(), "A.DAT", nullptr, 0), AF_OK);
  ASSERT_EQ(af_machine_vfs_clear(box.get()), AF_OK);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), 0.0);
}

TEST(AbiVfs, LoadsAnMzProgramOffTheFilesystem) {
  const equipped_machine box;

  // A two-paragraph MZ header with no relocations and a two-byte image:
  // `JMP $`. Self-written, as everything in this repository is.
  std::array<std::uint8_t, 34> image{};
  image[0] = 'M';
  image[1] = 'Z';
  image[2] = 34;  // bytes in the last page
  image[3] = 0;
  image[4] = 1;  // pages
  image[5] = 0;
  image[8] = 2;  // header paragraphs
  image[9] = 0;
  image[10] = 0x10;  // MINALLOC
  image[16] = 0x00;  // initial SP, low
  image[17] = 0x01;  // initial SP, high
  image[24] = 0x1C;  // relocation table offset
  image[32] = 0xEB;  // JMP $
  image[33] = 0xFE;

  ASSERT_EQ(af_machine_vfs_put(box.get(), "TINY.EXE", image.data(),
                               static_cast<std::uint32_t>(image.size())),
            AF_OK);

  EXPECT_EQ(af_machine_load_from_vfs(box.get(), "TINY.EXE", " ARGS"), AF_OK);
  EXPECT_EQ(af_machine_load_error(box.get()), 0u);

  // And it runs: a frame of virtual time with nothing refused.
  EXPECT_EQ(af_machine_run_until(box.get(), 1000.0), AF_OK);
  EXPECT_GT(af_machine_steps(box.get()), 0.0);
}

TEST(AbiVfs, ReportsWhyALoadFailedWithoutFoldingItIntoTheStatus) {
  const equipped_machine box;

  const std::array<std::uint8_t, 4> junk{'N', 'O', 'P', 'E'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "BAD.EXE", junk.data(), 4), AF_OK);

  EXPECT_EQ(af_machine_load_from_vfs(box.get(), "BAD.EXE", nullptr),
            AF_INVALID);
  EXPECT_EQ(af_machine_load_error(box.get()),
            static_cast<std::uint32_t>(
                amberfolio::machine::loader_error::bad_signature));

  // A file that is not there is a loader error too, and a different one.
  EXPECT_EQ(af_machine_load_from_vfs(box.get(), "GONE.EXE", nullptr),
            AF_INVALID);
  EXPECT_EQ(af_machine_load_error(box.get()),
            static_cast<std::uint32_t>(
                amberfolio::machine::loader_error::file_error));
}

// --- The report (M3-F1, #83, reached from here by M3-F2) ----------------

// --- Replay, through the ABI (#100) --------------------------------------
//
// `af_machine_state_hash` and `af_machine_verify_recording` are the two
// halves of what a host that did not record a run still has to be able to
// do with one. They are checked against each other on purpose: the
// recording below is written out of nothing but ABI answers — the
// program's fingerprint, the file manifest, the tick, the step count and
// the state hash — and then handed back to the verifier. If the two ever
// disagreed about what the machine is, a recording built from one would
// fail through the other, which is the only failure mode that matters
// here and the only one a golden could not catch.
//
// The program is the same two-byte `JMP $` the loader test uses: it never
// stops, so every checkpoint is of a running machine, and the recording
// ends on a tick this test chooses.

namespace {

/// The `JMP $` MZ file, and nothing about it that a program needs to be.
[[nodiscard]] std::array<std::uint8_t, 34> spinning_program() {
  std::array<std::uint8_t, 34> image{};
  image[0] = 'M';
  image[1] = 'Z';
  image[2] = 34;
  image[4] = 1;
  image[8] = 2;
  image[10] = 0x10;
  image[16] = 0x00;
  image[17] = 0x01;
  image[24] = 0x1C;
  image[32] = 0xEB;
  image[33] = 0xFE;
  return image;
}

/// One frame of virtual time, in ticks — the boundary
/// `af_machine_verify_recording` runs to, so the boundary a recording it
/// will read has to put its checkpoints on.
constexpr double frame_ticks = 1'193'182.0 / 60.0;

/// `af_machine_state_hash`, as the string a checkpoint line carries.
[[nodiscard]] std::string state_hash_of(const af_machine* box) {
  std::array<char, 96> hex{};
  const std::uint32_t n = af_machine_state_hash(
      box, hex.data(), static_cast<std::uint32_t>(hex.size()));
  return {hex.data(), n};
}

}  // namespace

// --- What a seam did, across the C boundary (#147) -----------------------
//
// `af_machine_seam_armed` says an address was computed out of a seam's
// fact table. It does not say a handler ever ran there, and #131's whole
// lesson is that the two read identically: a seam whose module has moved,
// or whose offset was never right, reports `armed`, fires nothing, and
// looks exactly like one that works. `af_machine_seam_fired` is the
// difference, and these run it on every native target so the browser is
// not the only place it is checked.
//
// The pair is deliberate. `probe` and `probe-unreached` are keyed to the
// same file, so both are available, both enable and both arm — every
// answer this ABI gives about them before the run is identical. Only the
// count afterwards tells them apart.

/// The probe program on the filesystem, loaded, with both of its seams
/// registered and the index of each. Everything the two tests below
/// share.
class seam_probe_rig {
 public:
  seam_probe_rig() {
    // The RESET line first, as a host does — and before the registry is
    // touched, since `machine::reset()` turns every seam off.
    box_.power_on();

    amberfolio::machine::machine* pc =
        amberfolio::af_machine_unwrap(box_.get());
    EXPECT_NE(pc, nullptr);
    if (pc != nullptr) {
      EXPECT_TRUE(
          pc->seams().add(amberfolio::programs::seam_probe_definition()));
      EXPECT_TRUE(pc->seams().add(
          amberfolio::programs::seam_probe_unreached_definition()));
    }

    const std::vector<std::uint8_t>& exe =
        amberfolio::programs::seam_probe_file();
    EXPECT_EQ(af_machine_vfs_put(box_.get(), "PROBE.EXE", exe.data(),
                                 static_cast<std::uint32_t>(exe.size())),
              AF_OK);
    EXPECT_EQ(af_machine_load_from_vfs(box_.get(), "PROBE.EXE", nullptr),
              AF_OK);
  }

  [[nodiscard]] af_machine* get() const noexcept { return box_.get(); }

  /// The registry index of `id`, or `count()` if there is no such seam —
  /// found the way a JS host finds it, by asking for each id in turn.
  [[nodiscard]] std::uint32_t index_of(std::string_view id) const {
    const std::uint32_t count = af_machine_seam_count(box_.get());
    for (std::uint32_t i = 0; i < count; ++i) {
      std::array<char, 64> text{};
      const std::uint32_t length = af_machine_seam_id(
          box_.get(), i, text.data(), static_cast<std::uint32_t>(text.size()));
      if (std::string_view(text.data(), length) == id) {
        return i;
      }
    }
    return count;
  }

  /// Run until the program exits, or until a generous budget of virtual
  /// time is gone. The probe program polls 16,384 times at most.
  void run_to_the_end() const {
    for (int frame = 0; frame < 120 && af_machine_stopped(box_.get()) == 0;
         ++frame) {
      af_machine_run_until(box_.get(),
                           af_ticks_per_second() * (frame + 1) / 60.0);
    }
  }

 private:
  equipped_machine box_;
};

TEST(AbiSeams, CountsWhatTheHandlersActuallyDid) {
  const seam_probe_rig rig;
  const std::uint32_t probe = rig.index_of("probe");
  ASSERT_LT(probe, af_machine_seam_count(rig.get()));

  // Off, and never run: zero, and it is zero for the honest reason.
  EXPECT_EQ(af_machine_seam_state(rig.get(), probe), AF_SEAM_OFF);
  EXPECT_EQ(af_machine_seam_fired(rig.get(), probe), 0.0);

  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe"), AF_OK);
  EXPECT_EQ(af_machine_seam_armed(rig.get(), probe), 1);
  EXPECT_EQ(af_machine_seam_fired(rig.get(), probe), 0.0)
      << "armed before a step is not fired";

  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  EXPECT_GT(af_machine_seam_fired(rig.get(), probe), 0.0);
  EXPECT_EQ(
      af_machine_seam_fired(rig.get(), af_machine_seam_count(rig.get()) + 1),
      0.0)
      << "an index past the end answers zero, like every other call here";
}

TEST(AbiSeams, ASeamArmedWhereTheProgramNeverGoesReportsZero) {
  const seam_probe_rig rig;
  const std::uint32_t probe = rig.index_of("probe");
  const std::uint32_t never = rig.index_of("probe-unreached");
  ASSERT_LT(probe, af_machine_seam_count(rig.get()));
  ASSERT_LT(never, af_machine_seam_count(rig.get()));

  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe"), AF_OK);
  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe-unreached"), AF_OK);

  // Before the run the two are indistinguishable through this ABI: same
  // state, same reason, same `armed`. That is the point — it is why
  // `armed` was not enough.
  EXPECT_EQ(af_machine_seam_state(rig.get(), probe),
            af_machine_seam_state(rig.get(), never));
  EXPECT_EQ(af_machine_seam_armed(rig.get(), probe),
            af_machine_seam_armed(rig.get(), never));
  EXPECT_EQ(af_machine_seam_armed(rig.get(), never), 1);

  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  // And afterwards they are not.
  EXPECT_GT(af_machine_seam_fired(rig.get(), probe), 0.0);
  EXPECT_EQ(af_machine_seam_fired(rig.get(), never), 0.0)
      << "a seam armed past the program's exit reported a run";

  // The handler that must never run writes 0xFFFF over the program's own
  // first answer if it ever does (tests/programs/machine_programs.cpp),
  // so the result block is a second witness to the same fact.
  std::array<std::uint8_t, 2> answer{};
  ASSERT_EQ(af_machine_read_memory(rig.get(), 0x600 + 0x800, answer.data(), 2),
            AF_OK);
  EXPECT_NE(answer[0] | (answer[1] << 8), 0xFFFF);
}

TEST(AbiReplay, StateHashIsSixtyFourHexDigitsAndMovesWithTheMachine) {
  const equipped_machine box;
  box.power_on();
  const std::array<std::uint8_t, 34> image = spinning_program();
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SPIN.EXE", image.data(),
                               static_cast<std::uint32_t>(image.size())),
            AF_OK);
  ASSERT_EQ(af_machine_load_from_vfs(box.get(), "SPIN.EXE", ""), AF_OK);

  const std::string first = state_hash_of(box.get());
  EXPECT_EQ(first.size(), 64u);
  EXPECT_TRUE(std::ranges::all_of(first, [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  })) << first;

  // Asking twice does not change the answer, and running does.
  EXPECT_EQ(state_hash_of(box.get()), first);
  ASSERT_EQ(af_machine_run_until(box.get(), 40'000.0), AF_OK);
  EXPECT_NE(state_hash_of(box.get()), first);

  // A buffer that cannot hold the digest and its terminator is refused
  // rather than filled with two thirds of an answer.
  std::array<char, 64> narrow{};
  EXPECT_EQ(af_machine_state_hash(box.get(), narrow.data(),
                                  static_cast<std::uint32_t>(narrow.size())),
            0u);
  EXPECT_EQ(af_machine_state_hash(nullptr, narrow.data(), 96u), 0u);
}

TEST(AbiReplay, ARecordingBuiltFromAbiAnswersVerifiesThroughTheAbi) {
  const std::array<std::uint8_t, 34> image = spinning_program();

  // The run that is recorded.
  std::string text;
  {
    const equipped_machine box;
    box.power_on();
    ASSERT_EQ(af_machine_vfs_put(box.get(), "SPIN.EXE", image.data(),
                                 static_cast<std::uint32_t>(image.size())),
              AF_OK);
    ASSERT_EQ(af_machine_load_from_vfs(box.get(), "SPIN.EXE", ""), AF_OK);

    std::array<char, 96> hex{};
    ASSERT_GT(
        af_machine_program_fingerprint(box.get(), hex.data(),
                                       static_cast<std::uint32_t>(hex.size())),
        0u);

    text += "amberfolio-recording 1 state=1\n";
    text += "program SPIN.EXE ";
    text += hex.data();
    text += "\ntail\n";

    // The manifest, in the filesystem's own order — which is what the
    // player will walk when it checks it.
    const std::uint32_t files = af_machine_vfs_count(box.get());
    ASSERT_EQ(files, 1u);
    for (std::uint32_t i = 0; i < files; ++i) {
      std::array<char, 32> name{};
      ASSERT_GT(af_machine_vfs_name_at(box.get(), i, name.data(),
                                       static_cast<std::uint32_t>(name.size())),
                0u);
      std::array<char, 96> digest{};
      ASSERT_GT(
          af_machine_vfs_fingerprint(box.get(), name.data(), digest.data(),
                                     static_cast<std::uint32_t>(digest.size())),
          0u);
      text += "file ";
      text += name.data();
      text += ' ';
      text += std::to_string(af_machine_vfs_size_at(box.get(), i));
      text += ' ';
      text += digest.data();
      text += '\n';
    }

    // Four frames, a checkpoint after each — the boundaries the verifier
    // runs to, and the reason this loop uses the same number it does.
    for (int frame = 1; frame <= 4; ++frame) {
      ASSERT_EQ(af_machine_run_until(box.get(), frame_ticks * frame), AF_OK);
      text += "checkpoint ";
      text += std::to_string(
          static_cast<std::uint64_t>(af_machine_time(box.get())));
      text += ' ';
      text += std::to_string(
          static_cast<std::uint64_t>(af_machine_steps(box.get())));
      text += ' ';
      text += state_hash_of(box.get());
      text += '\n';
    }
    text +=
        "end " +
        std::to_string(static_cast<std::uint64_t>(af_machine_time(box.get()))) +
        ' ' +
        std::to_string(
            static_cast<std::uint64_t>(af_machine_steps(box.get()))) +
        '\n';
  }

  // And the run that has to be it. A second machine, equipped and loaded
  // the same way and then handed nothing but the text.
  {
    const equipped_machine box;
    box.power_on();
    ASSERT_EQ(af_machine_vfs_put(box.get(), "SPIN.EXE", image.data(),
                                 static_cast<std::uint32_t>(image.size())),
              AF_OK);
    ASSERT_EQ(af_machine_load_from_vfs(box.get(), "SPIN.EXE", ""), AF_OK);

    std::array<char, 512> report{};
    EXPECT_EQ(
        af_machine_verify_recording(
            box.get(), text.data(), static_cast<std::uint32_t>(text.size()),
            report.data(), static_cast<std::uint32_t>(report.size())),
        AF_OK)
        << report.data();
    EXPECT_THAT(std::string(report.data()),
                ::testing::HasSubstr("replay verified checkpoints=4"));
  }
}

TEST(AbiReplay, ARecordingOfAnotherMachineIsRefusedAndSaysWhy) {
  const std::array<std::uint8_t, 34> image = spinning_program();

  // A recording whose every checkpoint hash is wrong. The preamble is
  // right, so what this asks is whether the *state* comparison bites —
  // the initial-condition check would refuse it either way otherwise.
  const equipped_machine first;
  first.power_on();
  ASSERT_EQ(af_machine_vfs_put(first.get(), "SPIN.EXE", image.data(),
                               static_cast<std::uint32_t>(image.size())),
            AF_OK);
  ASSERT_EQ(af_machine_load_from_vfs(first.get(), "SPIN.EXE", ""), AF_OK);

  std::array<char, 96> hex{};
  ASSERT_GT(
      af_machine_program_fingerprint(first.get(), hex.data(),
                                     static_cast<std::uint32_t>(hex.size())),
      0u);
  std::array<char, 96> digest{};
  ASSERT_GT(
      af_machine_vfs_fingerprint(first.get(), "SPIN.EXE", digest.data(),
                                 static_cast<std::uint32_t>(digest.size())),
      0u);

  std::string text = "amberfolio-recording 1 state=1\nprogram SPIN.EXE ";
  text += hex.data();
  text += "\ntail\nfile SPIN.EXE 34 ";
  text += digest.data();
  text += '\n';
  text += "checkpoint " +
          std::to_string(static_cast<std::uint64_t>(frame_ticks)) + " 0 " +
          std::string(64, 'a') + '\n';
  text += "end 19886 0\n";

  std::array<char, 512> report{};
  EXPECT_EQ(
      af_machine_verify_recording(
          first.get(), text.data(), static_cast<std::uint32_t>(text.size()),
          report.data(), static_cast<std::uint32_t>(report.size())),
      AF_INVALID);
  EXPECT_THAT(std::string(report.data()),
              ::testing::HasSubstr("amberfolio: replay diverged"));

  // And a text that is not a recording at all is refused as malformed
  // rather than as a machine that differs.
  const std::string junk = "hello\n";
  EXPECT_EQ(
      af_machine_verify_recording(
          first.get(), junk.data(), static_cast<std::uint32_t>(junk.size()),
          report.data(), static_cast<std::uint32_t>(report.size())),
      AF_INVALID);
  EXPECT_THAT(std::string(report.data()),
              ::testing::HasSubstr("amberfolio: replay refused"));

  EXPECT_EQ(af_machine_verify_recording(nullptr, junk.data(), 6u, nullptr, 0u),
            AF_NO_MACHINE);
  EXPECT_EQ(af_machine_verify_recording(first.get(), nullptr, 0u, nullptr, 0u),
            AF_INVALID);
}

TEST(AbiReport, WritesTheSameStopLineTheDesktopHostPrints) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  // INT 21h on a bare machine: nothing backs the vector, so the machine
  // refuses (PLAN.md §3).
  const std::array<std::uint8_t, 2> program{0xCD, 0x21};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, program.data(), 2),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10000.0), AF_STOPPED);

  std::array<char, 512> text{};
  const std::uint32_t written =
      af_machine_stop_report(box.get(), AF_RUN_END_STOPPED, text.data(),
                             static_cast<std::uint32_t>(text.size()));
  ASSERT_GT(written, 0u);

  const std::string_view report(text.data(), written);
  EXPECT_NE(report.find("amberfolio: stop reason=unimplemented_service "),
            std::string_view::npos)
      << report;
  EXPECT_NE(report.find("amberfolio: stop next=INT 21h AH=00h"),
            std::string_view::npos)
      << report;
}

TEST(AbiReport, PrintsTheHostsOwnReasonWhenTheMachineIsStillRunning) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  const std::array<std::uint8_t, 2> program{0xEB, 0xFE};  // JMP $
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, program.data(), 2),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  ASSERT_EQ(af_machine_run_until(box.get(), 400.0), AF_OK);

  std::array<char, 512> text{};
  const std::uint32_t written =
      af_machine_stop_report(box.get(), AF_RUN_END_STEP_BUDGET, text.data(),
                             static_cast<std::uint32_t>(text.size()));
  const std::string_view report(text.data(), written);
  EXPECT_NE(report.find("reason=step_budget "), std::string_view::npos)
      << report;
}

TEST(AbiReport, TracingIsOffUntilAskedForAndSurvivesAReset) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  std::array<char, 256> text{};
  ASSERT_GT(af_machine_trace_report(box.get(), text.data(),
                                    static_cast<std::uint32_t>(text.size())),
            0u);
  EXPECT_STREQ(text.data(), "amberfolio: stop trace=off\n");

  ASSERT_EQ(af_machine_set_trace(box.get(), 1), AF_OK);
  const std::array<std::uint8_t, 2> program{0xEB, 0xFE};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, program.data(), 2),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  ASSERT_EQ(af_machine_run_until(box.get(), 400.0), AF_OK);

  std::vector<char> big(24576);
  ASSERT_GT(af_machine_trace_report(box.get(), big.data(),
                                    static_cast<std::uint32_t>(big.size())),
            0u);
  EXPECT_NE(std::string_view(big.data()).find("trace=on"),
            std::string_view::npos);

  // A setting, not state: the ring is emptied and recording stays on.
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);
  ASSERT_GT(af_machine_trace_report(box.get(), big.data(),
                                    static_cast<std::uint32_t>(big.size())),
            0u);
  EXPECT_NE(std::string_view(big.data()).find("steps_seen=0"),
            std::string_view::npos);
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

// M4-W1 (#108): the account crosses the boundary as characters.
//
// Before this, a run through the C ABI said nothing about what the
// program did beyond the number it stopped with — the diagnostics sink is
// a C++ interface and does not cross (abi.h), so a browser run was blind
// where a desktop run printed notices, file activity and every seam
// transition. What crosses now is the same line the SDL host prints,
// rendered once in core (machine/report.h).
TEST(Abi, TheLogCarriesTheSameSentenceTheDesktopHostPrints) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);

  EXPECT_EQ(af_machine_log_pending(box.get()), 0u);
  EXPECT_EQ(af_machine_log_dropped(box.get()), 0.0);

  // The same refusal the test above drives, for the same reason: it is
  // the stop a host is most likely to have to report.
  const std::array<std::uint8_t, 2> refused{0xCD, 0x21};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, refused.data(),
                                    static_cast<std::uint32_t>(refused.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_STOPPED);

  // It survives the stop, like the console ring and for the same reason:
  // it is where the answer is.
  EXPECT_NE(af_machine_log_pending(box.get()), 0u);
  std::array<char, 1024> out{};
  const std::uint32_t got = af_machine_read_log(
      box.get(), out.data(), static_cast<std::uint32_t>(out.size()));
  ASSERT_NE(got, 0u);
  const std::string_view text(out.data(), got);
  EXPECT_NE(text.find("amberfolio: machine stopped, unimplemented_service"),
            std::string_view::npos);
  EXPECT_EQ(text.back(), '\n');

  // Drained is drained.
  EXPECT_EQ(af_machine_log_pending(box.get()), 0u);
  EXPECT_EQ(af_machine_read_log(box.get(), out.data(),
                                static_cast<std::uint32_t>(out.size())),
            0u);
  EXPECT_EQ(af_machine_read_log(box.get(), nullptr, 8), 0u);
}

// The high-volume half of the channel, and the one switch that owns both
// halves of the facility (abi.h's `af_machine_set_trace`).
TEST(Abi, TheLogTakesServiceCallsOnlyWhenTracingIsOn) {
  const auto drain = [](af_machine* box) {
    std::array<char, 4096> out{};
    const std::uint32_t got = af_machine_read_log(
        box, out.data(), static_cast<std::uint32_t>(out.size()));
    return std::string(out.data(), got);
  };

  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);
  ASSERT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);

  // INT 21h AH=4Ch, which the reference set answers: the program exits,
  // and a program exiting is not a diagnostic (machine/report.h), so with
  // tracing off there is nothing at all to say about this run.
  const std::array<std::uint8_t, 4> exits{0xB4, 0x4C, 0xCD, 0x21};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, exits.data(),
                                    static_cast<std::uint32_t>(exits.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_STOPPED);
  EXPECT_EQ(drain(box.get()), "");

  ASSERT_EQ(af_machine_set_trace(box.get(), 1), AF_OK);
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_STOPPED);
  const std::string traced = drain(box.get());
  EXPECT_NE(traced.find("amberfolio: call INT21 ax=4C00"), std::string::npos);

  // And the log is the host's to clear, not the RESET line's: it is not
  // machine state (machine/log.h).
  ASSERT_EQ(af_machine_set_trace(box.get(), 1), AF_OK);
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 10'000.0), AF_STOPPED);
  EXPECT_NE(af_machine_log_pending(box.get()), 0u);
  EXPECT_EQ(af_machine_clear_log(box.get()), AF_OK);
  EXPECT_EQ(af_machine_log_pending(box.get()), 0u);
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
  const std::array<std::uint8_t, 6> set_mode{0xB8, 0x0D, 0x00,
                                             0xCD, 0x10, 0xF4};
  ASSERT_EQ(
      af_machine_write_memory(box.get(), 0x10000, set_mode.data(),
                              static_cast<std::uint32_t>(set_mode.size())),
      AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);

  // 25,000 ticks, comfortably past one renderer frame period
  // (pit_input_hz / 60, about 19,886 ticks — renderer.h) even though the
  // program itself HLTs almost immediately: a halted machine still burns
  // virtual time every step (machine.h), so the deadline still arrives.
  EXPECT_EQ(af_machine_run_until(box.get(), 25'000.0), AF_OK);
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
  ASSERT_EQ(
      af_machine_write_memory(box.get(), 0x10000, set_mode.data(),
                              static_cast<std::uint32_t>(set_mode.size())),
      AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);
  EXPECT_EQ(af_machine_run_until(box.get(), 25'000.0), AF_OK);
  EXPECT_EQ(af_machine_stopped(box.get()), 0);
}

}  // namespace
