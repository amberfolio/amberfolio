// SPDX-License-Identifier: AGPL-3.0-only
//
// The committed session library (tests/sessions/, M4-R1 #100): a
// recording made once, checked into the repository, and reproduced here
// exactly.
//
// This is the cross-target proof, and its other half is in
// `hosts/web/tests/smoke.mjs` — the same file, the same ABI call, under a
// compiler, a standard library and a build of SHA-256 that the native
// suite shares nothing with. A test that only ever compared a build to
// itself would pass on two machines that disagreed about every byte.
//
// What passing means is stronger than "the program answered the same".
// A recording is keys, ticks and hashes (docs/replay.md), so reproducing
// one means every byte of RAM, every attached device's registers, the
// scheduler's armed deadlines, the DOS handle table, the framebuffer and
// the stop record agreed at every checkpoint. tests/sessions/README.md
// says what each session pins and when one may legitimately be
// re-recorded — which is a short list, and "the test went red" is not on
// it.
//
// Through the ABI rather than through core's own headers, on purpose:
// the ABI is what the browser has, so a native check that went around it
// would be checking a path only one of the two targets uses.

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>

#include "amberfolio/abi.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

/// The session directory, as the build system knows it. A compile
/// definition and not a runtime search: the sessions are source, they sit
/// beside the test that reads them, and a test that hunted for them could
/// pass by finding nothing.
#ifndef AMBERFOLIO_SESSIONS_DIR
#error "AMBERFOLIO_SESSIONS_DIR is not defined; see tests/CMakeLists.txt"
#endif

[[nodiscard]] std::string read_session_file(std::string_view name) {
  const std::string path =
      std::string(AMBERFOLIO_SESSIONS_DIR) + "/" + std::string(name);
  std::ifstream in(path, std::ios::binary);
  EXPECT_TRUE(in.good()) << "cannot read " << path;
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The program `spin.rec` was recorded of: `JMP $` behind a two-paragraph
/// MZ header, ten bytes of which are instructions.
///
/// Read off the session's own disk rather than assembled here, because a
/// session *is* a recording plus the disk it was recorded against — that
/// is what lets `scripts/sweep.py` hand the same pair to the desktop host
/// with no special case, and what keeps one copy of the bytes rather than
/// three that could drift. The recording's manifest pins them by SHA-256
/// either way.
[[nodiscard]] std::string spinning_program() {
  return read_session_file("spin/SPIN.EXE");
}

/// A machine equipped and loaded the way the recording's initial
/// conditions name — which is the reference device set and nothing else
/// done to it.
class session_machine {
 public:
  session_machine() : box_(af_machine_create()) {
    EXPECT_NE(box_, nullptr);
    EXPECT_EQ(af_machine_attach_reference_devices(box_), AF_OK);
    // The RESET line, and it is not a formality: the self test programs
    // the PIT and the 8259 through real bus cycles (docs/machine.md), so
    // a machine that skipped it has different device state from one that
    // powered on — which is a difference `state_section::devices` sees
    // and nothing else does. A session of a machine that never reset
    // would be a golden of a machine no host builds.
    EXPECT_EQ(af_machine_reset(box_), AF_OK);
    const std::string image = spinning_program();
    EXPECT_EQ(image.size(), 34u);
    EXPECT_EQ(
        af_machine_vfs_put(box_, "SPIN.EXE",
                           reinterpret_cast<const std::uint8_t*>(image.data()),
                           static_cast<std::uint32_t>(image.size())),
        AF_OK);
    EXPECT_EQ(af_machine_load_from_vfs(box_, "SPIN.EXE", ""), AF_OK);
  }

  session_machine(const session_machine&) = delete;
  session_machine& operator=(const session_machine&) = delete;
  session_machine(session_machine&&) = delete;
  session_machine& operator=(session_machine&&) = delete;

  ~session_machine() { af_machine_destroy(box_); }

  [[nodiscard]] af_machine* get() const { return box_; }

 private:
  af_machine* box_;
};

}  // namespace

TEST(SessionLibrary, SpinReproducesEveryCheckpoint) {
  const std::string text = read_session_file("spin.rec");
  ASSERT_FALSE(text.empty());

  const session_machine box;
  std::array<char, 512> report{};
  EXPECT_EQ(af_machine_verify_recording(
                box.get(), text.data(), static_cast<std::uint32_t>(text.size()),
                report.data(), static_cast<std::uint32_t>(report.size())),
            AF_OK)
      << report.data()
      << "\n\ntests/sessions/README.md says when a session may legitimately"
         " be re-recorded. If none of those changed, this is a finding about"
         " the machine and not about the golden.";
  EXPECT_THAT(std::string(report.data()),
              ::testing::HasSubstr("replay verified checkpoints=4"));
}

// The golden has to be able to fail, or the test above is a test of a
// function that always says yes. One checkpoint hash replaced with
// something no machine will ever produce, and the same machine that just
// verified the real one must refuse this.
TEST(SessionLibrary, ASessionWithAWrongCheckpointIsRefused) {
  std::string text = read_session_file("spin.rec");
  const std::size_t at = text.find("checkpoint ");
  ASSERT_NE(at, std::string::npos);
  const std::size_t digest = text.find_last_of(' ', text.find('\n', at));
  ASSERT_NE(digest, std::string::npos);
  text.replace(digest + 1, 64, std::string(64, 'a'));

  const session_machine box;
  std::array<char, 512> report{};
  EXPECT_EQ(af_machine_verify_recording(
                box.get(), text.data(), static_cast<std::uint32_t>(text.size()),
                report.data(), static_cast<std::uint32_t>(report.size())),
            AF_INVALID);
  EXPECT_THAT(std::string(report.data()),
              ::testing::HasSubstr("amberfolio: replay diverged"));
}

// Every committed recording is format 1, and stays readable (#155).
//
// The manifest recurses from format 2 on, so a recorder writes 2 now —
// but these seven were written at 1, and six of them are of a game whose
// disk is nobody's in this tree to re-record. `docs/replay.md` §7's rule
// is that a version is read for as long as a recording of it may exist,
// and this is the assertion that keeps it: a change that stranded them
// fails here, on the files, rather than being noticed by whoever next
// tried to verify one.
//
// Named one by one rather than globbed, exactly as tests/sessions/
// README.md's table names them: a session that stopped being read would
// otherwise stop being checked at the same moment.
TEST(SessionLibrary, EveryCommittedRecordingIsAFormatThisBuildStillReads) {
  for (const std::string_view name :
       {"spin.rec", "party.rec", "save.rec", "load.rec", "fight.rec",
        "fight-cheat.rec", "temple.rec"}) {
    const std::string text = read_session_file(name);
    ASSERT_FALSE(text.empty()) << name;
    EXPECT_THAT(text, ::testing::StartsWith("amberfolio-recording 1 state=1\n"))
        << name
        << ": re-recording one of these is a decision (tests/sessions/"
           "README.md), and for the six recorded from a game it is not one"
           " this repository can make.";
  }
}

// And the ones recorded since the format moved on, named the same way and
// for the same reason. They are format 3 because that is what the host
// writes now; what matters is that a build still reads every recording
// this tree carries, whichever format it was written in.
TEST(SessionLibrary, TheRecordingsMadeSinceFormatThreeAreStillRead) {
  for (const std::string_view name : {"reader.rec", "notes.rec", "cite.rec"}) {
    const std::string text = read_session_file(name);
    ASSERT_FALSE(text.empty()) << name;
    EXPECT_THAT(text, ::testing::StartsWith("amberfolio-recording 3 state=1\n"))
        << name;
  }
}

// A journal session names the store it was recorded over, and a store it
// carries is a store that has to be there: the reader replays with no
// text without one, and a recording of the reader would then diverge for
// a reason that is not the machine (#235).
TEST(SessionLibrary, AJournalSessionNamesAStoreThatIsThere) {
  for (const std::string_view name : {"reader.session", "notes.session"}) {
    const std::string text = read_session_file(name);
    ASSERT_FALSE(text.empty()) << name;
    EXPECT_THAT(text, ::testing::HasSubstr(
                          "journal-store tests/visual/reader-store.txt"))
        << name;
  }
  EXPECT_FALSE(read_session_file("../visual/reader-store.txt").empty())
      << "the store those two name";
  EXPECT_THAT(read_session_file("cite.session"),
              ::testing::HasSubstr("journal-store external "))
      << "a real ingestion is pinned by digest and never carried here";
}

// And so does the other half of an initial condition: the same recording
// against a machine with no program loaded is refused before a step is
// taken, naming the condition rather than a state that differs.
TEST(SessionLibrary, ASessionIsRefusedAgainstAMachineItDoesNotDescribe) {
  const std::string text = read_session_file("spin.rec");

  af_machine* bare = af_machine_create();
  ASSERT_NE(bare, nullptr);
  ASSERT_EQ(af_machine_attach_reference_devices(bare), AF_OK);

  std::array<char, 512> report{};
  EXPECT_EQ(af_machine_verify_recording(
                bare, text.data(), static_cast<std::uint32_t>(text.size()),
                report.data(), static_cast<std::uint32_t>(report.size())),
            AF_INVALID);
  EXPECT_THAT(std::string(report.data()),
              ::testing::HasSubstr("the program loaded is not the one"
                                   " recorded"));
  af_machine_destroy(bare);
}
