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

// The one format 1 recording left, and it stays readable (#155).
//
// The manifest recurses from format 2 on, so a recorder writes 3 now.
// Six game sessions were written at 1 and were pinned here as
// un-re-recordable, on the reasoning that their disk is nobody's in this
// tree; #293 re-recorded them, because the boot they were made on is a
// boot the code-wheel seam no longer produces, and a golden of a machine
// that cannot exist is not a golden. `spin.rec` is what carries format 1
// now — the only session whose disk *is* committed, so the format the
// oldest reader has to keep reading is pinned by the one recording
// nobody needs a game to remake.
//
// `docs/replay.md` §7's rule is that a version is read for as long as a
// recording of it may exist, and this is the assertion that keeps it: a
// change that stranded it fails here, on the file, rather than being
// noticed by whoever next tried to verify one.
TEST(SessionLibrary, EveryCommittedRecordingIsAFormatThisBuildStillReads) {
  const std::string text = read_session_file("spin.rec");
  ASSERT_FALSE(text.empty());
  EXPECT_THAT(text, ::testing::StartsWith("amberfolio-recording 1 state=1\n"))
      << "spin.rec is the tree's only format 1 recording and the whole of"
         " what keeps that reader honest; re-recording it is a decision"
         " (tests/sessions/README.md).";
}

// And every game session, which is format 3 because that is what the host
// writes now. Named one by one rather than globbed, exactly as
// tests/sessions/README.md's table names them: a session that stopped
// being read would otherwise stop being checked at the same moment.
TEST(SessionLibrary, TheRecordingsMadeSinceFormatThreeAreStillRead) {
  for (const std::string_view name : {"boot.rec",
                                      "boot-wheel.rec",
                                      "party.rec",
                                      "save.rec",
                                      "load.rec",
                                      "fight.rec",
                                      "fight-cheat.rec",
                                      "temple.rec",
                                      "camp.rec",
                                      "camp-fix.rec",
                                      "walk.rec",
                                      "walk-map.rec",
                                      "wild.rec",
                                      "wild-trail.rec",
                                      "reader.rec",
                                      "notes.rec",
                                      "cite.rec",
                                      "quiet.rec",
                                      "quiet-automap.rec",
                                      "quiet-encamp.rec",
                                      "quiet-cheats.rec",
                                      "quiet-explored.rec",
                                      "quiet-journal.rec",
                                      "quiet-all.rec",
                                      "subset-map-reader.rec"}) {
    const std::string text = read_session_file(name);
    ASSERT_FALSE(text.empty()) << name;
    EXPECT_THAT(text, ::testing::StartsWith("amberfolio-recording 3 state=1\n"))
        << name;
  }
}

// The boot pair is the code-wheel seam's `identical` half (#293), and the
// thing that makes it one is what its descriptors do *not* say: neither
// states `code-wheel-answered`, so a replay of either reaches the
// challenge and stops there, and the difference between them is only
// whether an engine was watching.
//
// Asserted on the descriptors rather than on a run, because a run of
// these needs the player's disk. What a machine does with the line is
// `scripts/sweep.py`'s.
TEST(SessionLibrary, TheBootPairLeavesTheChallengeUnanswered) {
  for (const std::string_view name : {"boot.session", "boot-wheel.session"}) {
    const std::string text = read_session_file(name);
    ASSERT_FALSE(text.empty()) << name;
    EXPECT_THAT(text,
                ::testing::Not(::testing::HasSubstr("\ncode-wheel-answered")))
        << name << ": the pair exists to record the challenge unanswered";
  }
  EXPECT_THAT(read_session_file("boot-wheel.session"),
              ::testing::HasSubstr("identical boot"));
  EXPECT_THAT(read_session_file("boot-wheel.rec"),
              ::testing::HasSubstr("\nseam code-wheel\n"));
  EXPECT_THAT(read_session_file("boot.rec"),
              ::testing::Not(::testing::HasSubstr("\nseam ")))
      << "the baseline has no engine at all";

  // And every other game session says the condition, because the boot it
  // was recorded on is the boot the challenge never stops.
  for (const std::string_view name :
       {"quiet.session", "walk.session", "camp.session", "reader.session",
        "party.session", "wild.session"}) {
    EXPECT_THAT(read_session_file(name),
                ::testing::HasSubstr("\ncode-wheel-answered\n"))
        << name;
  }
}

// A journal session names the store it was recorded over, and a store it
// carries is a store that has to be there: the reader replays with no
// text without one, and a recording of the reader would then diverge for
// a reason that is not the machine (#235).
//
// Every one of them is now a store **this repository wrote**, and that
// is the thing worth checking rather than which sessions name which file
// (#290). `cite` used to pin a real ingestion by digest, on the reasoning
// that a citation of the program's own wanted a real journal behind it.
// It does not: what the session proves is the program's — that the city
// hall's event names four proclamations, that the log takes them, and
// that opening one reaches the reader — and four proclamations written
// here prove it on anybody's machine instead of on one.
//
// The rule that replaces it: a store the library carries says `engine
// hand` and has no edition, so it cannot be a transcription of anybody's
// booklet. The `journal-store external` form stays in the grammar for a
// session that needs one; nothing uses it.
TEST(SessionLibrary, AJournalSessionNamesAStoreThisRepositoryWrote) {
  struct named_store {
    std::string_view session;
    std::string_view named;  // as the descriptor spells it
    std::string_view here;   // as this test, rooted at tests/sessions, reads it
  };
  const named_store named[] = {
      {.session = "reader.session",
       .named = "tests/visual/reader-store.txt",
       .here = "../visual/reader-store.txt"},
      {.session = "notes.session",
       .named = "tests/visual/reader-store.txt",
       .here = "../visual/reader-store.txt"},
      {.session = "cite.session",
       .named = "tests/visual/cite-store.txt",
       .here = "../visual/cite-store.txt"},
  };
  for (const named_store& row : named) {
    const std::string text = read_session_file(row.session);
    ASSERT_FALSE(text.empty()) << row.session;
    EXPECT_THAT(text, ::testing::HasSubstr(std::string("journal-store ") +
                                           std::string(row.named)))
        << row.session;

    const std::string carried = read_session_file(row.here);
    EXPECT_FALSE(carried.empty())
        << row.named << ": the store " << row.session << " names";
    EXPECT_THAT(carried, ::testing::HasSubstr("engine hand"))
        << row.named << ": a store here is written here, never read off a page";
    EXPECT_THAT(carried, ::testing::HasSubstr(std::string("edition ") +
                                              std::string(64, '0')))
        << row.named << ": and belongs to no edition of anybody's booklet";
  }

  // No session pins somebody's own ingestion any more. The form is still
  // in the grammar; a session that used it would be one nobody but its
  // author could verify.
  for (const std::string_view session :
       {"reader.session", "notes.session", "cite.session"}) {
    EXPECT_THAT(read_session_file(session),
                ::testing::Not(::testing::HasSubstr("journal-store external")))
        << session;
  }
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
