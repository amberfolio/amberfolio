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
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/abi_bridge.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
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
  EXPECT_EQ(af_machine_audio_logging_edges(nullptr), 0);
  EXPECT_EQ(af_machine_audio_edges_dropped(nullptr), 0.0);
  EXPECT_EQ(af_machine_audio_edges_pending(nullptr), 0.0);
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
  std::array<double, 4> ticks{};
  std::array<std::uint8_t, 4> levels{};
  EXPECT_EQ(
      af_machine_audio_read_edges(nullptr, ticks.data(), levels.data(), 4), 0u);
  af_machine_audio_log_edges(nullptr, 1);
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
  // The count is what takes the listing (abi.h); the rows read it.
  ASSERT_EQ(af_machine_vfs_count(box.get()), 1u);
  std::array<char, 16> name{};
  EXPECT_EQ(af_machine_vfs_name_at(box.get(), 0, name.data(),
                                   static_cast<std::uint32_t>(name.size())),
            8u);
  EXPECT_STREQ(name.data(), "GAME.DAT");
}

TEST(AbiVfs, TheListingIsTakenByTheCountAndReadByTheRows) {
  // M5-D2's contract, stated out loud (abi.h): one walk per listing,
  // because a walk per row is quadratic on top of an `entry_at()` that
  // is already quadratic and does not finish on a real installation.
  //
  // What that buys, beside finishing, is a listing that does not shift
  // underneath the loop reading it — which a live one did, silently,
  // whenever anything changed the filesystem in between.
  const equipped_machine box;
  ASSERT_EQ(af_machine_vfs_put(box.get(), "A.DAT", nullptr, 0), AF_OK);
  ASSERT_EQ(af_machine_vfs_count(box.get()), 1u);

  ASSERT_EQ(af_machine_vfs_put(box.get(), "B.DAT", nullptr, 0), AF_OK);
  std::array<char, 16> name{};
  EXPECT_EQ(af_machine_vfs_name_at(box.get(), 1, name.data(),
                                   static_cast<std::uint32_t>(name.size())),
            0u)
      << "the listing is the one the count took, and it had one row";

  EXPECT_EQ(af_machine_vfs_count(box.get()), 2u) << "and asking again takes"
                                                    " a new one";
  EXPECT_EQ(af_machine_vfs_name_at(box.get(), 1, name.data(),
                                   static_cast<std::uint32_t>(name.size())),
            5u);
  EXPECT_STREQ(name.data(), "B.DAT");
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

// --- Out of room is not "your name was wrong" (M4, #158) ----------------
//
// Both refusals were `AF_INVALID` until #158, and a browser handed a real
// installation reported seven of the game's own data files in the same
// words as a PDF it was right to ignore — `status 3` apiece. A host
// cannot act on that, and neither can a person reading it: one is the
// machine working and the other is a hole in the disk it is running.

TEST(AbiVfs, SaysOutOfRoomRatherThanInvalidWhenTheEntryTableIsFull) {
  const equipped_machine box;

  // Fill it exactly. Every name here is a legal DOS short name, so
  // nothing below is refused for anything but capacity.
  for (std::size_t i = 0;
       i < amberfolio::machine::memory_filesystem::max_entries; ++i) {
    const std::string name = std::to_string(i) + ".DAT";
    ASSERT_EQ(af_machine_vfs_put(box.get(), name.c_str(), nullptr, 0), AF_OK)
        << i;
  }

  // The two answers, side by side, from one full filesystem. This is the
  // whole of the fix: a caller can tell them apart, so a report can.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "WALLDEF3.DAX", nullptr, 0),
            AF_NO_ROOM);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "code wheel.pdf", nullptr, 0),
            AF_INVALID);
  static_assert(AF_NO_ROOM != AF_INVALID,
                "the two refusals must be different answers");

  // And a full table is still a refusal, never a silent overwrite of
  // something in use (PLAN.md §3).
  EXPECT_EQ(af_machine_vfs_count(box.get()),
            static_cast<std::uint32_t>(
                amberfolio::machine::memory_filesystem::max_entries));

  // Room again after a clear, which is what abi.h tells a host that
  // reached this to do.
  ASSERT_EQ(af_machine_vfs_clear(box.get()), AF_OK);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "WALLDEF3.DAX", nullptr, 0), AF_OK);
}

TEST(AbiVfs, SaysOutOfRoomWhenTheBytesDoNotFitAndLeavesNoFragment) {
  const equipped_machine box;

  // Larger than one file may be. The backend answers a short count, this
  // takes the fragment away again (abi.h), and the status says which
  // kind of refusal it was — bytes, not a bad name.
  const std::vector<std::uint8_t> big(
      amberfolio::machine::memory_filesystem::max_file_size + 1, 0x5A);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "HUGE.DAT", big.data(),
                               static_cast<std::uint32_t>(big.size())),
            AF_NO_ROOM);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), 0.0);
}

TEST(AbiVfs, SaysOutOfRoomWhenADirectoryOnTheWayCannotBeMade) {
  const equipped_machine box;

  // Full, and then a path one directory down — so the refusal happens
  // in `make_parents`, before the file is even reached: `\SAVE` has
  // nowhere to go. That is why it answers a status rather than a bool
  // (#158). The path was fine; the machine was not.
  for (std::size_t i = 0;
       i < amberfolio::machine::memory_filesystem::max_entries; ++i) {
    const std::string name = std::to_string(i) + ".DAT";
    ASSERT_EQ(af_machine_vfs_put(box.get(), name.c_str(), nullptr, 0), AF_OK)
        << i;
  }
  EXPECT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", nullptr, 0),
            AF_NO_ROOM);
}

// --- The shape that actually failed (M4, #158) --------------------------
//
// A real installation is a flat directory of about a hundred and twenty
// files with a `SAVE\` under it holding the slot files an archive release
// ships — 195 entries before a player has saved anything, against a bound
// that was 192. The table filled part-way through, the rest of the disk
// was refused, and the game booted and ran with holes in it; the symptom
// loud enough to notice was that no save could be written.
//
// No game content is here (CONTRIBUTING.md) — the *shape* is the fact
// being reproduced, and these names are this test's own.
TEST(AbiVfs, TakesAShippedInstallationWholeAndStillHasRoomToSave) {
  const equipped_machine box;

  constexpr std::uint32_t root_files = 122;
  constexpr std::uint32_t shipped_slots = 72;

  const std::array<std::uint8_t, 4> bytes{1, 2, 3, 4};
  const auto put = [&](const std::string& path) {
    return af_machine_vfs_put(box.get(), path.c_str(), bytes.data(),
                              static_cast<std::uint32_t>(bytes.size()));
  };

  for (std::uint32_t i = 0; i < root_files; ++i) {
    ASSERT_EQ(put("DATA" + std::to_string(i) + ".DAT"), AF_OK) << i;
  }
  for (std::uint32_t i = 0; i < shipped_slots; ++i) {
    ASSERT_EQ(put("SAVE/SLOT" + std::to_string(i) + ".DAT"), AF_OK) << i;
  }

  // Complete: every file, wherever it lives. Nothing skipped, which is
  // the claim — 190 of 195 was the bug. And since #170 the listing walks
  // the tree, so the seventy-two under `\SAVE\` are *in* this number
  // rather than standing behind one row saying `SAVE`.
  EXPECT_EQ(af_machine_vfs_count(box.get()), root_files + shipped_slots);

  // And what the bug's symptom was: a save still has somewhere to go. A
  // party of six writes a character file each beside the slot, so this
  // is one save, not one file.
  ASSERT_EQ(put("SAVE/SAVGAMA.DAT"), AF_OK);
  for (std::uint32_t i = 0; i < 6; ++i) {
    ASSERT_EQ(put("SAVE/CHAR" + std::to_string(i) + ".CHA"), AF_OK) << i;
  }
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

// --- A path, not merely a name (M4, #146) --------------------------------
//
// The door used to reach the root and no further, so a host handing over
// a real installation dropped every subdirectory — and `\SAVE\` is where
// a shipped save slot lives, which is why a browser could start a game
// and never resume one. What follows is the widened door: a path in
// either spelling, the directories on the way made in core, and the
// paths no file can live at refused before anything is made.

/// The SHA-256 of "abc" (FIPS 180-4 appendix B.1) — used below as a
/// read-back: a fingerprint that matches is a file whose bytes arrived
/// under the name the caller asked for.
constexpr const char* abc_digest =
    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

/// `af_machine_vfs_fingerprint` of `path`, or an empty string.
std::string fingerprint_of(af_machine* box, const char* path) {
  std::array<char, 128> hex{};
  const std::uint32_t length = af_machine_vfs_fingerprint(
      box, path, hex.data(), static_cast<std::uint32_t>(hex.size()));
  return length == 0 ? std::string{} : std::string(hex.data());
}

/// The whole tree as `{path, size}` pairs, in the walk order the ABI
/// pins (abi.h) — every file, at the path it lives at, and no
/// directories, because a directory is not something any other call in
/// this section takes.
std::vector<std::pair<std::string, std::uint32_t>> tree_listing(
    af_machine* box) {
  std::vector<std::pair<std::string, std::uint32_t>> out;
  // The count takes the listing and the rows read it (abi.h), which is
  // what every caller of this trio does anyway.
  const std::uint32_t count = af_machine_vfs_count(box);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::array<char, AF_PATH_CAPACITY> path{};
    if (af_machine_vfs_path_at(box, i, path.data(),
                               static_cast<std::uint32_t>(path.size())) == 0) {
      continue;
    }
    out.emplace_back(std::string(path.data()), af_machine_vfs_size_at(box, i));
  }
  return out;
}

/// The leaf names of that listing, in the same order — what
/// `af_machine_vfs_name_at` answers, kept separate so that a test can say
/// which of the two it is checking.
std::vector<std::string> tree_names(af_machine* box) {
  std::vector<std::string> out;
  const std::uint32_t count = af_machine_vfs_count(box);
  for (std::uint32_t i = 0; i < count; ++i) {
    std::array<char, 16> name{};
    if (af_machine_vfs_name_at(box, i, name.data(),
                               static_cast<std::uint32_t>(name.size())) == 0) {
      continue;
    }
    out.emplace_back(name.data());
  }
  return out;
}

TEST(AbiVfs, PutsAFileBelowTheRootAndMakesTheDirectoryOnTheWay) {
  const equipped_machine box;

  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  EXPECT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);

  // Nothing made `\SAVE` first — the host handed over one file and core
  // made the directory it named, which is what
  // `machine::filesystem::create()` deliberately will not do. The
  // listing says so by holding the file at its full path: a path that
  // resolves is a directory that was made.
  const auto tree = tree_listing(box.get());
  ASSERT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree[0].first, "\\SAVE\\SAVE1.DAT");
  EXPECT_EQ(tree[0].second, 3u);
  EXPECT_EQ(tree_names(box.get()), std::vector<std::string>{"SAVE1.DAT"})
      << "the leaf name and the path are different answers, and both are"
         " wanted";

  // And the file is at the path it was given, by its own bytes.
  EXPECT_EQ(fingerprint_of(box.get(), "SAVE/SAVE1.DAT"), abc_digest);
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), 3.0);
}

TEST(AbiVfs, TakesEitherSpellingOfASeparatorAndFoldsCaseThroughEveryComponent) {
  const equipped_machine box;

  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "save/save1.dat", bytes.data(), 3),
            AF_OK);

  // One file, four spellings. A browser hands a page `SAVE/SAVE1.DAT` and
  // a script hands it `\SAVE\SAVE1.DAT`; if either side folded that
  // itself there would be two rules for what a path is, and eventually
  // two answers (abi.h).
  EXPECT_EQ(fingerprint_of(box.get(), "save/save1.dat"), abc_digest);
  EXPECT_EQ(fingerprint_of(box.get(), "SAVE/SAVE1.DAT"), abc_digest);
  EXPECT_EQ(fingerprint_of(box.get(), "\\SAVE\\SAVE1.DAT"), abc_digest);
  EXPECT_EQ(fingerprint_of(box.get(), "C:\\save\\Save1.Dat"), abc_digest);

  // Upper case out, in every component of the path the listing gives
  // back.
  const auto tree = tree_listing(box.get());
  ASSERT_EQ(tree.size(), 1u);
  EXPECT_EQ(tree[0].first, "\\SAVE\\SAVE1.DAT");
}

TEST(AbiVfs, MakesEveryMissingDirectoryAndReusesTheOnesItAlreadyMade) {
  const equipped_machine box;

  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "A/B/C/D.DAT", bytes.data(), 3),
            AF_OK);
  // A second file two directories down the same branch: the existing
  // directories are used, not refused as already-taken names and not
  // remade.
  ASSERT_EQ(af_machine_vfs_put(box.get(), "A/B/E.DAT", bytes.data(), 3), AF_OK);

  EXPECT_EQ(fingerprint_of(box.get(), "A/B/C/D.DAT"), abc_digest);
  EXPECT_EQ(fingerprint_of(box.get(), "A/B/E.DAT"), abc_digest);

  // Two files, three directories deep and two deep, in the walk order:
  // depth-first, each level in pinned name order, so `A\B\C\D.DAT`
  // comes before `A\B\E.DAT`.
  EXPECT_EQ(tree_listing(box.get()),
            (std::vector<std::pair<std::string, std::uint32_t>>{
                {"\\A\\B\\C\\D.DAT", 3u}, {"\\A\\B\\E.DAT", 3u}}));
}

// --- Reading back and taking away (M5-D2, #170) -------------------------
//
// A browser could hand an installation over one file at a time and never
// read one byte back, and could never remove one. The exploration sidecar
// (#173) and M6's persistence both walk out through here.

TEST(AbiVfs, ReadsAFileBackOutByPathAndSaysHowBigItIsFirst) {
  const equipped_machine box;
  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);

  // Query, then fill — the shape the rest of this ABI has.
  EXPECT_EQ(af_machine_vfs_size(box.get(), "save/save1.dat"), 3u)
      << "and the path is canonicalized here like every other one";

  std::array<std::uint8_t, 3> out{};
  EXPECT_EQ(af_machine_vfs_get(box.get(), "SAVE/SAVE1.DAT", out.data(), 3),
            AF_OK);
  EXPECT_EQ(out, bytes);

  // A buffer that will not hold the file is the caller's problem and is
  // told about it, rather than being handed a prefix it cannot tell from
  // the whole thing.
  std::array<std::uint8_t, 3> partial{};
  EXPECT_EQ(af_machine_vfs_get(box.get(), "SAVE/SAVE1.DAT", partial.data(), 2),
            AF_NO_ROOM);
  EXPECT_EQ(partial, (std::array<std::uint8_t, 3>{}))
      << "and nothing was copied";
}

TEST(AbiVfs, TellsAnEmptyFileFromOneThatIsNotThere) {
  // The one ambiguity a size can never resolve on its own, resolved by
  // the call beside it: zero bytes and no file both measure zero.
  const equipped_machine box;
  ASSERT_EQ(af_machine_vfs_put(box.get(), "EMPTY.DAT", nullptr, 0), AF_OK);

  EXPECT_EQ(af_machine_vfs_size(box.get(), "EMPTY.DAT"), 0u);
  EXPECT_EQ(af_machine_vfs_size(box.get(), "GONE.DAT"), 0u);
  EXPECT_EQ(af_machine_vfs_get(box.get(), "EMPTY.DAT", nullptr, 0), AF_OK);
  EXPECT_EQ(af_machine_vfs_get(box.get(), "GONE.DAT", nullptr, 0), AF_INVALID);
}

TEST(AbiVfs, RefusesToReadAnythingThatIsNotAFile) {
  const equipped_machine box;
  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);

  std::array<std::uint8_t, 8> out{};
  const auto max = static_cast<std::uint32_t>(out.size());
  EXPECT_EQ(af_machine_vfs_get(box.get(), "SAVE", out.data(), max), AF_INVALID)
      << "a directory has no bytes";
  EXPECT_EQ(af_machine_vfs_get(box.get(), "", out.data(), max), AF_INVALID)
      << "nor has the root";
  EXPECT_EQ(af_machine_vfs_get(box.get(), "code wheel.pdf", out.data(), max),
            AF_INVALID)
      << "nor has a name no DOS path can equal";
  EXPECT_EQ(af_machine_vfs_get(nullptr, "SAVE/SAVE1.DAT", out.data(), max),
            AF_NO_MACHINE);
}

TEST(AbiVfs, RemovesAFileAndLeavesTheDirectoryItWasIn) {
  const equipped_machine box;
  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE2.DAT", bytes.data(), 3),
            AF_OK);
  ASSERT_EQ(af_machine_vfs_count(box.get()), 2u);

  EXPECT_EQ(af_machine_vfs_remove(box.get(), "save/save1.dat"), AF_OK);
  EXPECT_EQ(tree_listing(box.get()),
            (std::vector<std::pair<std::string, std::uint32_t>>{
                {"\\SAVE\\SAVE2.DAT", 3u}}));
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), 3.0)
      << "and the bytes came back";

  // Gone means gone, and asking again says so rather than answering as
  // though it had done something.
  EXPECT_EQ(af_machine_vfs_remove(box.get(), "SAVE/SAVE1.DAT"), AF_INVALID);

  // The last file out of `\SAVE` leaves `\SAVE` behind — abi.h says why,
  // and the listing cannot show it because a listing is files. What
  // shows it is that a put under it still works without making anything.
  EXPECT_EQ(af_machine_vfs_remove(box.get(), "SAVE/SAVE2.DAT"), AF_OK);
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
  EXPECT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE3.DAT", bytes.data(), 3),
            AF_OK);
  EXPECT_EQ(af_machine_vfs_size(box.get(), "SAVE/SAVE3.DAT"), 3u);
}

TEST(AbiVfs, RefusesToRemoveAnythingThatIsNotAFile) {
  const equipped_machine box;
  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);

  EXPECT_EQ(af_machine_vfs_remove(box.get(), "SAVE"), AF_INVALID)
      << "a directory is not removed by this call, and never silently";
  EXPECT_EQ(af_machine_vfs_remove(box.get(), ""), AF_INVALID);
  EXPECT_EQ(af_machine_vfs_remove(box.get(), "code wheel.pdf"), AF_INVALID);
  EXPECT_EQ(af_machine_vfs_remove(nullptr, "SAVE/SAVE1.DAT"), AF_NO_MACHINE);

  // Nothing was taken away by any of those.
  EXPECT_EQ(af_machine_vfs_size(box.get(), "SAVE/SAVE1.DAT"), 3u);
}

TEST(AbiVfs, TheListingIsTheSetTheOtherTwoCallsWorkOn) {
  // The property that lets a row be a path and a size with no kind flag
  // beside it: everything the listing names can be read and removed.
  const equipped_machine box;
  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "START.EXE", bytes.data(), 3), AF_OK);
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);
  ASSERT_EQ(af_machine_vfs_put(box.get(), "A/B/C/D.DAT", bytes.data(), 3),
            AF_OK);

  const auto tree = tree_listing(box.get());
  ASSERT_EQ(tree.size(), 3u);
  for (const auto& [path, size] : tree) {
    std::array<std::uint8_t, 8> out{};
    EXPECT_EQ(af_machine_vfs_size(box.get(), path.c_str()), size) << path;
    EXPECT_EQ(af_machine_vfs_get(box.get(), path.c_str(), out.data(),
                                 static_cast<std::uint32_t>(out.size())),
              AF_OK)
        << path;
    EXPECT_EQ(af_machine_vfs_remove(box.get(), path.c_str()), AF_OK) << path;
  }
  EXPECT_EQ(af_machine_vfs_count(box.get()), 0u);
}

TEST(AbiVfs, EveryCallHereNeedsAFilesystem) {
  // A bare machine has no reference devices, so it has no filesystem —
  // its own code rather than a lie about a machine that exists (abi.h).
  af_machine* bare = af_machine_create();
  ASSERT_NE(bare, nullptr);
  std::array<std::uint8_t, 4> out{};
  EXPECT_EQ(af_machine_vfs_get(bare, "X.DAT", out.data(), 4), AF_NO_FILESYSTEM);
  EXPECT_EQ(af_machine_vfs_remove(bare, "X.DAT"), AF_NO_FILESYSTEM);
  EXPECT_EQ(af_machine_vfs_size(bare, "X.DAT"), 0u);
  EXPECT_EQ(af_machine_vfs_count(bare), 0u);
  af_machine_destroy(bare);
}

TEST(AbiVfs, RefusesAPathNoFileCanLiveAtAndMakesNothingOnTheWayToIt) {
  const equipped_machine box;

  const std::array<std::uint8_t, 3> bytes{'a', 'b', 'c'};
  ASSERT_EQ(af_machine_vfs_put(box.get(), "START.EXE", bytes.data(), 3), AF_OK);
  ASSERT_EQ(af_machine_vfs_put(box.get(), "SAVE/SAVE1.DAT", bytes.data(), 3),
            AF_OK);
  const auto before = tree_listing(box.get());
  const double bytes_before = af_machine_vfs_bytes_used(box.get());

  // A component above the leaf that is a file. Overwriting `START.EXE`
  // with a directory to make room for this would be the machine deciding
  // it knew better than the caller about the caller's own disk.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "START.EXE/X.DAT", bytes.data(), 3),
            AF_INVALID);
  // A leaf that is already a directory.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "SAVE", bytes.data(), 3), AF_INVALID);
  // A component no DOS short name can equal, anywhere in the path — the
  // same refusal a bare name gets, applied component by component.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "code wheel/X.DAT", bytes.data(), 3),
            AF_INVALID);
  EXPECT_EQ(
      af_machine_vfs_put(box.get(), "DOCS/code wheel.pdf", bytes.data(), 3),
      AF_INVALID);
  // Deeper than `machine::dos_path::max_depth`, which is eight: nine
  // components do not resolve, and a truncated path would resolve to a
  // different, real place (machine/vfs.h).
  EXPECT_EQ(
      af_machine_vfs_put(box.get(), "A/B/C/D/E/F/G/H/I.DAT", bytes.data(), 3),
      AF_INVALID);
  // The root itself is not a file.
  EXPECT_EQ(af_machine_vfs_put(box.get(), "\\", bytes.data(), 3), AF_INVALID);

  // Nothing was made on the way to any of them — no `DOCS`, no `A`, and
  // `START.EXE` is still the file it was. A refusal that left a
  // half-built tree behind would be the quiet fiction the whole rule
  // exists to prevent.
  EXPECT_EQ(tree_listing(box.get()), before);
  EXPECT_EQ(af_machine_vfs_bytes_used(box.get()), bytes_before);
  EXPECT_EQ(fingerprint_of(box.get(), "START.EXE"), abc_digest);
  EXPECT_EQ(fingerprint_of(box.get(), "SAVE/SAVE1.DAT"), abc_digest);
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
      EXPECT_TRUE(pc->seams().add(
          amberfolio::programs::seam_probe_trigger_definition()));
      EXPECT_TRUE(
          pc->seams().add(amberfolio::programs::seam_probe_pull_definition()));
      EXPECT_TRUE(
          pc->seams().add(amberfolio::programs::seam_probe_host_definition()));
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

  /// Attach a `seam_host_services` to this machine, so a seam's
  /// `call_host()` reaches somebody (M5-D1, #169). Off unless a test asks
  /// for it: a machine with none is where "the callout was refused" can
  /// be produced deliberately, which is the half of this door with a
  /// failure mode.
  void attach_host() {
    amberfolio::machine::machine* pc =
        amberfolio::af_machine_unwrap(box_.get());
    ASSERT_NE(pc, nullptr);
    pc->seams().set_host(&host_);
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
  /// A host that answers and remembers nothing. What the ABI hands back
  /// is the *engine's* record of what it routed (abi.h), so this only
  /// has to exist for a call to have been served.
  class silent_host final : public amberfolio::machine::seam_host_services {
   public:
    void serve(amberfolio::machine::machine& /*box*/,
               amberfolio::machine::seam_host_service /*which*/,
               std::uint32_t /*argument*/) override {}
  };

  equipped_machine box_;
  silent_host host_;
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

// --- The host-service door (M5-D1, #169) --------------------------------
//
// A seam may call out to a host service, and a page has to be able to
// learn that it did and what it carried. #153's lesson is why these are
// *polled* rather than only streamed: an empty stream cannot tell a
// callout that was served from one that was never made, and both of
// those are things a browser needs to distinguish.

TEST(AbiSeams, AServedCalloutIsCountedAndItsArgumentIsReadable) {
  seam_probe_rig rig;
  rig.attach_host();
  const auto journal = static_cast<std::uint32_t>(
      amberfolio::machine::seam_host_service::journal_open);
  const auto automap = static_cast<std::uint32_t>(
      amberfolio::machine::seam_host_service::automap_update);

  EXPECT_EQ(af_machine_seam_host_calls(rig.get(), journal), 0.0);
  EXPECT_EQ(af_machine_seam_host_calls(rig.get(), automap), 0.0);

  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe-host"), AF_OK);
  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  // The seam's two points are each reached exactly once, so these are
  // numbers a reader checks against the program rather than against a
  // loop bound (tests/programs/machine_programs.cpp).
  EXPECT_EQ(af_machine_seam_host_calls(rig.get(), journal), 1.0);
  EXPECT_EQ(af_machine_seam_host_calls(rig.get(), automap), 1.0);
  EXPECT_EQ(af_machine_seam_host_argument(rig.get(), journal), 0x1234u);
  EXPECT_EQ(af_machine_seam_host_argument(rig.get(), automap), 0x00ABCDEFu);

  // A `which` that is not a service answers zero, like every other
  // out-of-range index in this ABI.
  EXPECT_EQ(af_machine_seam_host_calls(rig.get(), 99), 0.0);
  EXPECT_EQ(af_machine_seam_host_argument(rig.get(), 99), 0u);
  EXPECT_EQ(af_machine_seam_host_calls(nullptr, journal), 0.0);
}

TEST(AbiSeams, ACalloutWithNoHostAttachedCountsNothing) {
  // The entry that makes the count worth having. The same seam, the same
  // program, the same two points reached — and no host, so `call_host()`
  // answers false and nothing is counted, because nothing happened. In a
  // stream this run and a run whose seam was never on look identical.
  const seam_probe_rig rig;
  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe-host"), AF_OK);
  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  const std::uint32_t probe_host = rig.index_of("probe-host");
  ASSERT_LT(probe_host, af_machine_seam_count(rig.get()));
  EXPECT_GT(af_machine_seam_fired(rig.get(), probe_host), 0.0)
      << "its handlers did run, which is what makes the zeroes below mean"
         " something";
  EXPECT_EQ(
      af_machine_seam_host_calls(
          rig.get(), static_cast<std::uint32_t>(
                         amberfolio::machine::seam_host_service::journal_open)),
      0.0);
  EXPECT_EQ(af_machine_seam_host_calls(
                rig.get(),
                static_cast<std::uint32_t>(
                    amberfolio::machine::seam_host_service::automap_update)),
            0.0);
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

// --- The trigger, through the ABI (#161) ---------------------------------
//
// The host -> seam direction a page needs: `_triggered` says a seam takes
// one, `_pull` sets the latch, `_waiting` says a pull is outstanding, and
// `_reached` is what `_fired` cannot be for a trigger — the arrivals at
// its point, whether or not anybody asked. `probe-trigger` is `probe`'s
// register edit declared as a trigger, so the only difference between the
// two runs below is whether the pull was made.

TEST(AbiSeamTrigger, ATriggerNobodyPulledLeavesTheProgramsOwnAnswer) {
  const seam_probe_rig rig;
  const std::uint32_t trigger = rig.index_of("probe-trigger");
  ASSERT_LT(trigger, af_machine_seam_count(rig.get()));
  EXPECT_EQ(af_machine_seam_triggered(rig.get(), trigger), 1);
  EXPECT_EQ(af_machine_seam_triggered(rig.get(), rig.index_of("probe")), 0);

  // Off: a latch on a seam nobody turned on is refused rather than
  // remembered.
  EXPECT_EQ(af_machine_seam_pull(rig.get(), "probe-trigger"), AF_INVALID);
  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe-trigger"), AF_OK);
  EXPECT_EQ(af_machine_seam_pull(rig.get(), "probe"), AF_INVALID)
      << "a seam that acts whenever it is on has nothing to latch";
  EXPECT_EQ(af_machine_seam_waiting(rig.get(), trigger), 0);

  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  std::array<std::uint8_t, 2> answer{};
  ASSERT_EQ(af_machine_read_memory(rig.get(), 0x600 + 0x800, answer.data(), 2),
            AF_OK);
  EXPECT_EQ(answer[0] | (answer[1] << 8), 0x1111)
      << "a trigger nobody pulled changed the run";
  EXPECT_EQ(af_machine_seam_fired(rig.get(), trigger), 0.0);
  EXPECT_GT(af_machine_seam_reached(rig.get(), trigger), 0.0)
      << "and its point *was* reached, which is what makes that mean"
         " something";
}

TEST(AbiSeamTrigger, APulledTriggerActsOnceAndSaysWhatItWaited) {
  const seam_probe_rig rig;
  const std::uint32_t trigger = rig.index_of("probe-trigger");
  ASSERT_LT(trigger, af_machine_seam_count(rig.get()));
  ASSERT_EQ(af_machine_seam_enable(rig.get(), "probe-trigger"), AF_OK);
  ASSERT_EQ(af_machine_seam_pull(rig.get(), "probe-trigger"), AF_OK);
  EXPECT_EQ(af_machine_seam_waiting(rig.get(), trigger), 1)
      << "asked, and the program has not been there yet";

  rig.run_to_the_end();
  ASSERT_NE(af_machine_stopped(rig.get()), 0);

  std::array<std::uint8_t, 2> answer{};
  ASSERT_EQ(af_machine_read_memory(rig.get(), 0x600 + 0x800, answer.data(), 2),
            AF_OK);
  EXPECT_EQ(answer[0] | (answer[1] << 8), 0x2222) << "asked, and served";
  EXPECT_EQ(af_machine_seam_fired(rig.get(), trigger), 1.0);
  EXPECT_EQ(af_machine_seam_waiting(rig.get(), trigger), 0);

  // Every one of these answers zero for an index past the end, like the
  // rest of this file's seam calls.
  const std::uint32_t past = af_machine_seam_count(rig.get()) + 1;
  EXPECT_EQ(af_machine_seam_triggered(rig.get(), past), 0);
  EXPECT_EQ(af_machine_seam_waiting(rig.get(), past), 0);
  EXPECT_EQ(af_machine_seam_reached(rig.get(), past), 0.0);
  EXPECT_EQ(af_machine_seam_waited(rig.get(), past), 0.0);
  EXPECT_EQ(af_machine_seam_pulled_at(rig.get(), past), 0.0);
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

  std::vector<char> big(32768);
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

// The edge log through the ABI (#148). What the machine *published*, as
// against `af_machine_render_audio`'s rendering of it - the half of #106
// a browser could not ask at all until this door existed, so its
// measurable half stopped at the desktop host.
//
// The program is the smallest thing that makes a tone: divisor into
// channel 2, then the two port 61h bits that gate it. An even divisor,
// so mode 3's square wave is exactly symmetric and every gap in the list
// is the same number - which is the assertion, because a list whose gaps
// are all equal is a list of a *tone* and not of a machine twitching.
TEST(Abi, TheEdgeLogSaysWhatTheSpeakerPublished) {
  const machine_handle box;
  ASSERT_NE(box.get(), nullptr);
  ASSERT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);

  // mov al,0B6h / out 43h,al        channel 2, mode 3, lobyte/hibyte
  // mov al,54h  / out 42h,al        divisor 0x0554 = 1364, low
  // mov al,05h  / out 42h,al        and high
  // in al,61h / or al,3 / out 61h,al  gate and data on
  // hlt
  const std::array<std::uint8_t, 19> tone{
      0xB0, 0xB6, 0xE6, 0x43, 0xB0, 0x54, 0xE6, 0x42, 0xB0, 0x05,
      0xE6, 0x42, 0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61, 0xF4};
  ASSERT_EQ(af_machine_write_memory(box.get(), 0x10000, tone.data(),
                                    static_cast<std::uint32_t>(tone.size())),
            AF_OK);
  ASSERT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE), AF_OK);

  // Off unless asked for, and that is a claim about every other run in
  // this file: nothing pays for the log by existing.
  EXPECT_EQ(af_machine_audio_logging_edges(box.get()), 0);
  ASSERT_EQ(af_machine_run_until(box.get(), 20'000.0), AF_OK);

  std::array<double, 256> at{};
  std::array<std::uint8_t, 256> level{};
  EXPECT_EQ(af_machine_audio_read_edges(box.get(), at.data(), level.data(),
                                        static_cast<std::uint32_t>(at.size())),
            0u);

  af_machine_audio_log_edges(box.get(), 1);
  EXPECT_NE(af_machine_audio_logging_edges(box.get()), 0);
  ASSERT_EQ(af_machine_run_until(box.get(), 40'000.0), AF_OK);
  EXPECT_GT(af_machine_audio_edges_pending(box.get()), 0.0);

  const std::uint32_t got =
      af_machine_audio_read_edges(box.get(), at.data(), level.data(),
                                  static_cast<std::uint32_t>(at.size()));
  ASSERT_GT(got, 4u);
  EXPECT_EQ(af_machine_audio_edges_dropped(box.get()), 0.0);

  // A tone: strictly increasing ticks, alternating levels, one gap.
  const double gap = at[1] - at[0];
  EXPECT_GT(gap, 0.0);
  for (std::uint32_t i = 1; i < got; ++i) {
    EXPECT_DOUBLE_EQ(at[i] - at[i - 1], gap) << "at edge " << i;
    EXPECT_NE(level[i], level[i - 1]) << "at edge " << i;
  }

  // Drained means gone, which is what makes a host's file the whole run
  // rather than the last thousand edges of it.
  EXPECT_EQ(af_machine_audio_edges_pending(box.get()), 0.0);
  EXPECT_EQ(af_machine_audio_read_edges(box.get(), at.data(), level.data(),
                                        static_cast<std::uint32_t>(at.size())),
            0u);

  // The setting survives a reset and what the log held does not - the
  // edges belonged to the run that just ended (platform.h). What is in
  // it afterwards is the reset's own doing, at the new timeline's
  // origin: the speaker device coming back up and publishing the level
  // it powers on at. So the test is not "empty" but "nothing older than
  // this machine", which is the claim that matters and the stronger one.
  ASSERT_EQ(af_machine_reset(box.get()), AF_OK);
  EXPECT_NE(af_machine_audio_logging_edges(box.get()), 0);
  const std::uint32_t after =
      af_machine_audio_read_edges(box.get(), at.data(), level.data(),
                                  static_cast<std::uint32_t>(at.size()));
  for (std::uint32_t i = 0; i < after; ++i) {
    EXPECT_DOUBLE_EQ(at[i], 0.0) << "at edge " << i;
  }

  af_machine_audio_log_edges(box.get(), 0);
  EXPECT_EQ(af_machine_audio_logging_edges(box.get()), 0);
}

// The log is an observation of a run and not part of one: a machine
// asked to log hashes exactly as the same machine not asked to. If this
// ever failed, every recording in tests/sessions would have become a
// statement about the observer (abi.h, platform.h).
TEST(Abi, LoggingEdgesDoesNotMoveTheStateHash) {
  const auto run = [](bool logging) {
    const machine_handle box;
    EXPECT_EQ(af_machine_attach_reference_devices(box.get()), AF_OK);
    const std::array<std::uint8_t, 19> tone{
        0xB0, 0xB6, 0xE6, 0x43, 0xB0, 0x54, 0xE6, 0x42, 0xB0, 0x05,
        0xE6, 0x42, 0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61, 0xF4};
    EXPECT_EQ(af_machine_write_memory(box.get(), 0x10000, tone.data(),
                                      static_cast<std::uint32_t>(tone.size())),
              AF_OK);
    EXPECT_EQ(af_machine_set_entry(box.get(), 0x1000, 0, 0x1000, 0xFFFE),
              AF_OK);
    af_machine_audio_log_edges(box.get(), logging ? 1 : 0);
    EXPECT_EQ(af_machine_run_until(box.get(), 40'000.0), AF_OK);
    std::array<char, 96> digest{};
    EXPECT_GT(af_machine_state_hash(box.get(), digest.data(),
                                    static_cast<std::uint32_t>(digest.size())),
              0u);
    return std::string(digest.data());
  };

  EXPECT_EQ(run(true), run(false));
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
