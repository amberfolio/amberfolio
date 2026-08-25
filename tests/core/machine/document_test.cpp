// SPDX-License-Identifier: AGPL-3.0-only
//
// The document edition table (document.h, PLAN.md §2 and §5; M5-D3
// #171): what it knows, what it answers for a file it does not, and the
// shape every entry has to have.
//
// `edition_test.cpp` next door is the same suite one artifact over, and
// the two tables are checked the same way on purpose — a fingerprint has
// to mean the same thing whichever of them it is in.
//
// **No document is here.** Every digest below is either this file's own
// invention or the one fact `document.cpp` already keeps, and a
// fingerprint carries no byte of the file it names (CONTRIBUTING.md).
// A test that needed the document itself would be a test nobody without
// the document could run.

#include "amberfolio/machine/document.h"

#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

TEST(DocumentTable, EveryEntryIsAWellFormedFactAboutSomething) {
  // A fingerprint that is mistyped names nothing, which is the failure
  // this project wants over one that names everything (edition.h). So
  // the table is checked for shape rather than trusted to have it.
  EXPECT_FALSE(known_documents().empty())
      << "the baseline is the code wheel of the release every other fact"
         " in this tree was gathered against";
  for (const document_edition& known : known_documents()) {
    sha256_digest parsed;
    EXPECT_TRUE(parse_digest(known.fingerprint, parsed))
        << known.name << " has a fingerprint that is not 64 hex characters";
    EXPECT_FALSE(known.name.empty());
    EXPECT_NE(known.kind, document_kind::none)
        << known.name << " is a document for nothing, which is not a document";
    EXPECT_EQ(find_document(parsed), &known);
  }
}

TEST(DocumentTable, EveryFingerprintIsLowerCaseHex) {
  // The spelling `format_hex` produces, so that a line a host prints and
  // a line somebody adds to this table are comparable by eye.
  for (const document_edition& known : known_documents()) {
    for (const char c : known.fingerprint) {
      const bool digit = c >= '0' && c <= '9';
      const bool lower = c >= 'a' && c <= 'f';
      EXPECT_TRUE(digit || lower) << known.name << " has '" << c << "' in it";
    }
  }
}

TEST(DocumentTable, NoTwoEntriesShareAFingerprint) {
  for (const document_edition& a : known_documents()) {
    for (const document_edition& b : known_documents()) {
      if (&a == &b) {
        continue;
      }
      EXPECT_NE(a.fingerprint, b.fingerprint)
          << a.name << " and " << b.name << " are the same file";
    }
  }
}

TEST(DocumentTable, AnUnknownDocumentIsNull) {
  // Not a failure — the machine saying "I do not know this file", which
  // is the answer PLAN.md §9 asks for and the reason a gate can be
  // fail-closed at all.
  sha256_digest stranger;
  ASSERT_TRUE(parse_digest(
      "0000000000000000000000000000000000000000000000000000000000000000",
      stranger));
  EXPECT_EQ(find_document(stranger), nullptr);
}

TEST(DocumentTable, ADocumentIsNotABinaryAndTheTablesDoNotOverlap) {
  // The two tables are siblings and not one table with a column: a
  // program image is what the machine runs, a document is what the
  // player holds, and nothing should ever be found in both.
  for (const edition& binary : known_editions()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(binary.fingerprint, digest));
    EXPECT_EQ(find_document(digest), nullptr)
        << binary.name << " is in the document table";
  }
  for (const document_edition& document : known_documents()) {
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(document.fingerprint, digest));
    EXPECT_EQ(find_edition(digest), nullptr)
        << document.name << " is in the binary table";
  }
}

TEST(DocumentTable, TheJournalHasNoEntryYetAndThatIsSaidRatherThanGuessed) {
  // A fingerprint is a fact about a file somebody hashed, and nobody has
  // hashed that one. The consequence is deliberate: the journal reader's
  // gate (#174) refuses every document until this table gains its line.
  // If that ever changes, this test is the line that changes with it.
  for (const document_edition& known : known_documents()) {
    EXPECT_NE(known.kind, document_kind::journal)
        << "the journal has an entry now; update this test and #174's"
           " expectations with it";
  }
}

TEST(DocumentKindName, IsTheWordsAPersonReads) {
  EXPECT_STREQ(document_kind_name(document_kind::none), "no document");
  EXPECT_STREQ(document_kind_name(document_kind::code_wheel), "code wheel");
  EXPECT_STREQ(document_kind_name(document_kind::journal), "journal");
}

}  // namespace
}  // namespace amberfolio::machine
