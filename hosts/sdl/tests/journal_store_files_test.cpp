// SPDX-License-Identifier: AGPL-3.0-only
//
// Which file a program's journal is read from (user_files.h, #584): its
// own, the one a store was kept in before a store was a program's own
// where that one is its journal, and never another program's text under
// its name.
//
// Every store here is this file's own words, with the fingerprint of a
// shipped edition for its edition line and nothing of the edition in it.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/sha256.h"
#include "user_files.h"

namespace amberfolio::sdl {
namespace {

constexpr std::uint16_t image_segment = 0x1000;

/// The fingerprint of a shipped edition that is `program`'s journal.
[[nodiscard]] std::string_view journal_of(std::string_view program) {
  for (const host::journal_edition& edition : host::known_journals()) {
    if (edition.program == program) {
      return edition.fingerprint;
    }
  }
  ADD_FAILURE() << "no journal of that program";
  return {};
}

/// A store of `edition` with one entry, as the text a file holds.
[[nodiscard]] std::string store_of(std::string_view edition,
                                   std::string_view words) {
  host::journal_store store;
  store.set_edition(edition);
  EXPECT_TRUE(store.record_scan({.number = 12}, words));
  return store.serialize();
}

/// A scratch directory of this test's own, and a machine told which
/// program it is running.
class JournalStoreFiles : public ::testing::Test {
 protected:
  void SetUp() override {
    dir_ =
        std::filesystem::temp_directory_path() /
        ("amberfolio-journal-store-files-" +
         std::string(
             ::testing::UnitTest::GetInstance()->current_test_info()->name()));
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
    box_ = std::make_unique<machine::machine>(machine::memory_layout::pc);
  }

  void TearDown() override {
    std::error_code why;
    std::filesystem::remove_all(dir_, why);
  }

  void run_as(std::string_view fingerprint) {
    sha256_digest digest;
    ASSERT_TRUE(machine::parse_digest(fingerprint, digest));
    box_->seams().loaded(digest, image_segment);
  }

  [[nodiscard]] std::string path_of(std::string_view name) const {
    return (dir_ / name).string();
  }

  void write(std::string_view name, std::string_view text) const {
    std::ofstream file(dir_ / name, std::ios::binary | std::ios::trunc);
    file << text;
  }

  [[nodiscard]] bool exists(std::string_view name) const {
    return std::filesystem::exists(dir_ / name);
  }

  [[nodiscard]] std::string read(std::string_view name) const {
    std::ifstream file(dir_ / name, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

  [[nodiscard]] machine::machine& box() const { return *box_; }

 private:
  std::filesystem::path dir_;
  std::unique_ptr<machine::machine> box_;
};

TEST_F(JournalStoreFiles, AProgramsOwnFileIsReadAndTheOldOneIsNotLookedAt) {
  const std::string_view pool = machine::pool_of_radiance.fingerprint;
  run_as(pool);
  write("journal-pool.txt", store_of(journal_of(pool), "its own file"));
  write("journal.txt", store_of(journal_of(pool), "the file from before"));

  host::journal_store store;
  load_journal_store_from(path_of("journal-pool.txt"), path_of("journal.txt"),
                          box(), store);

  const host::journal_text* found = store.find({.number = 12});
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->text(), "its own file");
}

TEST_F(JournalStoreFiles, TheOldFileIsTakenByTheProgramWhoseJournalItIs) {
  const std::string_view pool = machine::pool_of_radiance.fingerprint;
  run_as(pool);
  const std::string before = store_of(journal_of(pool), "read last month");
  write("journal.txt", before);

  host::journal_store store;
  load_journal_store_from(path_of("journal-pool.txt"), path_of("journal.txt"),
                          box(), store);

  const host::journal_text* found = store.find({.number = 12});
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->text(), "read last month");
  EXPECT_FALSE(store.changed()) << "it has just been written";
  // Kept under the program's own name from here on, and the file it came
  // from left exactly as it was.
  ASSERT_TRUE(exists("journal-pool.txt"));
  host::journal_store kept;
  ASSERT_EQ(kept.parse(read("journal-pool.txt")), host::journal_trouble::none);
  EXPECT_EQ(kept.edition(), journal_of(pool));
  EXPECT_EQ(kept.size(), 1u);
  EXPECT_EQ(read("journal.txt"), before);
}

TEST_F(JournalStoreFiles, TheOldFileIsNeverReadForTheOtherProgram) {
  // What #584 is: Pool of Radiance's text answering Curse of the Azure
  // Bonds' citations, because one file was both games'.
  const std::string_view pool = machine::pool_of_radiance.fingerprint;
  run_as(machine::curse_of_the_azure_bonds.fingerprint);
  const std::string before = store_of(journal_of(pool), "the other game's");
  write("journal.txt", before);

  host::journal_store store;
  load_journal_store_from(path_of("journal-curse.txt"), path_of("journal.txt"),
                          box(), store);

  EXPECT_TRUE(store.empty());
  EXPECT_FALSE(store.changed()) << "nothing to write under this game's name";
  EXPECT_FALSE(exists("journal-curse.txt"));
  EXPECT_EQ(read("journal.txt"), before);
}

TEST_F(JournalStoreFiles, AnOldFileOfNoKnownEditionIsNotAdopted) {
  // Nothing says whose it is, so it is nobody's to take: a store like
  // that is read where somebody names it, as it always was.
  run_as(machine::pool_of_radiance.fingerprint);
  write("journal.txt", store_of(std::string(64, '0'), "written by hand"));

  host::journal_store store;
  load_journal_store_from(path_of("journal-pool.txt"), path_of("journal.txt"),
                          box(), store);

  EXPECT_TRUE(store.empty());
  EXPECT_FALSE(exists("journal-pool.txt"));
}

TEST_F(JournalStoreFiles, ANamedStoreOfTheOtherGamesJournalIsLeftAsItIs) {
  // `--journal-store`: there is no old file to look in, and the file
  // named is that game's to keep. The reader does not open it
  // (`HostServicesJournal.AnotherProgramsJournalIsNoJournalHere`).
  const std::string_view curse = machine::curse_of_the_azure_bonds.fingerprint;
  run_as(machine::pool_of_radiance.fingerprint);
  const std::string named = store_of(journal_of(curse), "the other game's");
  write("named.txt", named);

  host::journal_store store;
  load_journal_store_from(path_of("named.txt"), {}, box(), store);

  EXPECT_EQ(store.size(), 1u);
  EXPECT_FALSE(store.changed());
  EXPECT_EQ(read("named.txt"), named);
}

TEST_F(JournalStoreFiles, ANamedStoreThatIsNotThereLooksNowhereElse) {
  const std::string_view pool = machine::pool_of_radiance.fingerprint;
  run_as(pool);
  write("journal.txt", store_of(journal_of(pool), "the file from before"));

  host::journal_store store;
  load_journal_store_from(path_of("named.txt"), {}, box(), store);

  EXPECT_TRUE(store.empty());
  EXPECT_FALSE(exists("named.txt"));
}

}  // namespace
}  // namespace amberfolio::sdl
