// SPDX-License-Identifier: AGPL-3.0-only
//
// The edition table (edition.h, M4-F1 #95): a fingerprint the build
// knows answers a name, one it does not answers "unrecognized", and the
// two hex helpers every fact table compares through refuse anything that
// is not exactly sixty-four hex characters.

#include "amberfolio/machine/edition.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

/// The baseline's own fingerprint, read out of the table rather than
/// pasted: what is under test is the lookup, not the value.
[[nodiscard]] const edition& baseline() {
  const std::span<const edition> table = known_editions();
  EXPECT_FALSE(table.empty());
  return table.front();
}

TEST(EditionTable, KnowsTheBaselineByItsFingerprint) {
  sha256_digest digest;
  ASSERT_TRUE(parse_digest(baseline().fingerprint, digest));

  const edition* found = find_edition(digest);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, baseline().name);
  EXPECT_FALSE(found->name.empty());
}

TEST(EditionTable, AnswersUnrecognizedForAnythingElse) {
  sha256_digest digest;
  ASSERT_TRUE(parse_digest(baseline().fingerprint, digest));
  digest.bytes[31] = static_cast<std::uint8_t>(digest.bytes[31] ^ 0x01U);

  EXPECT_EQ(find_edition(digest), nullptr);
  EXPECT_EQ(find_edition(sha256_digest{}), nullptr);
}

TEST(EditionTable, EveryEntryHasAWellFormedFingerprintAndAName) {
  for (const edition& known : known_editions()) {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(known.fingerprint, digest))
        << "not sixty-four hex characters: " << known.fingerprint;
    EXPECT_FALSE(known.name.empty());
  }
}

TEST(EditionTable, KnowsCurseByItsFingerprint) {
  // The Steam release's START.EXE, v1.3 (PLAN.md §7, M9).
  sha256_digest digest;
  ASSERT_TRUE(parse_digest(
      "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd",
      digest));
  const edition* found = find_edition(digest);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->name, "Curse of the Azure Bonds (START.EXE)");
  EXPECT_EQ(found->configuration.file, "CURSE.CFG");
  EXPECT_EQ(found->configuration.sound_line, 2U);
  EXPECT_EQ(found->configuration.save_line, 3U);
}

TEST(EditionTable, EachProgramReadsAConfigurationFileOfItsOwn) {
  // The baseline's layout, unchanged: sound on line 2, saves on line 4.
  EXPECT_EQ(baseline().configuration.file, "POOL.CFG");
  EXPECT_EQ(baseline().configuration.sound_line, 2U);
  EXPECT_EQ(baseline().configuration.save_line, 4U);

  // No two programs read one name, so a file's name says whose it is.
  const std::span<const edition> table = known_editions();
  for (std::size_t i = 0; i < table.size(); ++i) {
    const launcher_configuration& mine = table[i].configuration;
    EXPECT_FALSE(mine.file.empty()) << table[i].name;
    EXPECT_GT(mine.sound_line, 0U) << table[i].name;
    EXPECT_GT(mine.save_line, 0U) << table[i].name;
    EXPECT_NE(mine.sound_line, mine.save_line) << table[i].name;
    EXPECT_EQ(find_configuration(mine.file), &mine) << table[i].name;
    for (std::size_t j = i + 1; j < table.size(); ++j) {
      EXPECT_NE(mine.file, table[j].configuration.file);
    }
  }
  EXPECT_EQ(find_configuration("OTHER.CFG"), nullptr);
  EXPECT_EQ(find_configuration("pool.cfg"), nullptr) << "names are upper case";
}

// --- The hex helpers -----------------------------------------------------

TEST(DigestText, RoundTripsThroughFormatHex) {
  sha256_digest digest;
  for (std::size_t i = 0; i < sha256_digest::byte_length; ++i) {
    digest.bytes[i] = static_cast<std::uint8_t>(i * 7 + 3);
  }
  std::array<char, sha256_digest::text_length + 1> text{};
  ASSERT_EQ(format_hex(digest, text), sha256_digest::text_length);

  EXPECT_TRUE(digest_is(digest, std::string_view(text.data())));
  sha256_digest parsed;
  ASSERT_TRUE(parse_digest(std::string_view(text.data()), parsed));
  EXPECT_EQ(parsed, digest);
}

TEST(DigestText, AcceptsEitherCaseAndNothingElse) {
  const std::string lower(64, 'a');
  std::string upper(64, 'A');
  sha256_digest from_lower;
  sha256_digest from_upper;
  ASSERT_TRUE(parse_digest(lower, from_lower));
  ASSERT_TRUE(parse_digest(upper, from_upper));
  EXPECT_EQ(from_lower, from_upper);

  sha256_digest scratch;
  EXPECT_FALSE(parse_digest(std::string(63, 'a'), scratch))
      << "one short is not a fingerprint";
  EXPECT_FALSE(parse_digest(std::string(65, 'a'), scratch))
      << "one long is not one either";
  std::string with_g(64, 'a');
  with_g[10] = 'g';
  EXPECT_FALSE(parse_digest(with_g, scratch)) << "g is not hex";
  EXPECT_FALSE(digest_is(from_lower, with_g));
}

}  // namespace
}  // namespace amberfolio::machine
