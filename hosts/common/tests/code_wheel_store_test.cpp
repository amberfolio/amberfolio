// SPDX-License-Identifier: AGPL-3.0-only
//
// The answered-copies store (code_wheel_store.h, M6-C1b #292).
//
// What it has to get right is small and every bit of it matters to a
// person: it must say yes for a copy that was answered for and no for one
// that was not, it must refuse a file it cannot read rather than half-read
// one, and it must not ask a host to rewrite a file because a run told it
// what it already knew.
//
// The format is restated here rather than read out of the code under
// test, which is the sibling suites' rule and the reason for it: a test
// that took its layout from the code it checks would be agreeing with
// itself. Every digest here is this file's own invention (PLAN.md §6) —
// nothing in this suite is a fingerprint of anybody's game.

#include "amberfolio/host/code_wheel_store.h"

#include <memory>
#include <string>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

/// Two copies nobody has: 64 hex characters each, made up here.
constexpr std::string_view first_hex =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::string_view second_hex =
    "2222222222222222222222222222222222222222222222222222222222222222";

[[nodiscard]] sha256_digest digest_of(std::string_view hex) {
  sha256_digest digest;
  EXPECT_TRUE(machine::parse_digest(hex, digest)) << hex;
  return digest;
}

TEST(CodeWheelStore, AFreshStoreHasAnsweredForNobody) {
  const code_wheel_store store;
  EXPECT_TRUE(store.empty());
  EXPECT_FALSE(store.answered(digest_of(first_hex)));
  EXPECT_FALSE(store.changed()) << "and it has nothing to be written down";
}

TEST(CodeWheelStore, RememberingACopyIsNewsExactlyOnce) {
  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  EXPECT_TRUE(store.changed());
  EXPECT_TRUE(store.answered(digest_of(first_hex)));

  store.clear_changed();
  EXPECT_FALSE(store.remember(digest_of(first_hex)))
      << "a run that told the store what it knew is not a file to rewrite";
  EXPECT_FALSE(store.changed());
  EXPECT_EQ(store.size(), 1U);
}

TEST(CodeWheelStore, OneCopysAnswerIsNotAnothers) {
  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  EXPECT_FALSE(store.answered(digest_of(second_hex)))
      << "a player who owns two editions has answered for one of them";
}

TEST(CodeWheelStore, ForgettingEmptiesItAndIsNewsOnlyWhenThereWasSomething) {
  code_wheel_store store;
  EXPECT_FALSE(store.forget()) << "nothing to forget is not a change";
  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  store.clear_changed();

  EXPECT_TRUE(store.forget());
  EXPECT_TRUE(store.changed());
  EXPECT_TRUE(store.empty());
  EXPECT_FALSE(store.answered(digest_of(first_hex)))
      << "and the next launch asks again, which is the whole of forget";
}

TEST(CodeWheelStore, WritesTheFormatItsHeaderDescribes) {
  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  EXPECT_TRUE(store.remember(digest_of(second_hex)));

  EXPECT_EQ(store.serialize(), std::string("amberfolio-code-wheel 1\n") +
                                   "answered " + std::string(first_hex) + "\n" +
                                   "answered " + std::string(second_hex) +
                                   "\n");
}

TEST(CodeWheelStore, ReadsBackWhatItWrote) {
  code_wheel_store written;
  EXPECT_TRUE(written.remember(digest_of(first_hex)));
  EXPECT_TRUE(written.remember(digest_of(second_hex)));

  code_wheel_store read;
  EXPECT_EQ(read.parse(written.serialize()), code_wheel_trouble::none);
  EXPECT_EQ(read.size(), 2U);
  EXPECT_TRUE(read.answered(digest_of(first_hex)));
  EXPECT_TRUE(read.answered(digest_of(second_hex)));
  EXPECT_FALSE(read.changed())
      << "what came out of the drawer came from the host, which holds it";
}

TEST(CodeWheelStore, AnEmptyStoreRoundTrips) {
  code_wheel_store store;
  EXPECT_EQ(store.parse(store.serialize()), code_wheel_trouble::none);
  EXPECT_TRUE(store.empty());
}

TEST(CodeWheelStore, RefusesAFileThatIsNotAStore) {
  code_wheel_store store;
  EXPECT_EQ(store.parse(""), code_wheel_trouble::not_a_store);
  EXPECT_EQ(store.parse("amberfolio-journal 3\n"),
            code_wheel_trouble::not_a_store);
  EXPECT_EQ(store.parse("amberfolio-code-wheel\n"),
            code_wheel_trouble::not_a_store);
  EXPECT_EQ(store.parse("amberfolio-code-wheel x\n"),
            code_wheel_trouble::not_a_store);
  EXPECT_EQ(store.parse("amberfolio-code-wheel 1"),
            code_wheel_trouble::not_a_store)
      << "every line this format has ends with a newline, and this has none";
}

TEST(CodeWheelStore, RefusesAStoreFromALaterBuild) {
  code_wheel_store store;
  EXPECT_EQ(store.parse("amberfolio-code-wheel 2\n"),
            code_wheel_trouble::later_version);
}

TEST(CodeWheelStore, RefusesALineItCannotRead) {
  code_wheel_store store;
  EXPECT_EQ(store.parse("amberfolio-code-wheel 1\nanswered nope\n"),
            code_wheel_trouble::bad_line);
  EXPECT_EQ(store.parse(std::string("amberfolio-code-wheel 1\nheld ") +
                        std::string(first_hex) + "\n"),
            code_wheel_trouble::bad_line);
  EXPECT_EQ(store.parse(std::string("amberfolio-code-wheel 1\nanswered ") +
                        std::string(first_hex) + " and more\n"),
            code_wheel_trouble::bad_line);
}

TEST(CodeWheelStore, ARefusedStoreLeavesTheObjectExactlyAsItWas) {
  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));

  EXPECT_EQ(store.parse(std::string("amberfolio-code-wheel 1\nanswered ") +
                        std::string(second_hex) + "\nrubbish\n"),
            code_wheel_trouble::bad_line);
  EXPECT_EQ(store.size(), 1U);
  EXPECT_TRUE(store.answered(digest_of(first_hex)))
      << "half a store is worse than none";
  EXPECT_FALSE(store.answered(digest_of(second_hex)));
}

TEST(CodeWheelStore, EveryTroubleHasAName) {
  EXPECT_STREQ(code_wheel_trouble_name(code_wheel_trouble::none), "ok");
  EXPECT_STREQ(code_wheel_trouble_name(code_wheel_trouble::not_a_store),
               "not-a-store");
  EXPECT_STREQ(code_wheel_trouble_name(code_wheel_trouble::later_version),
               "later-version");
  EXPECT_STREQ(code_wheel_trouble_name(code_wheel_trouble::bad_line),
               "bad-line");
}

// --- What a host does with it ----------------------------------------------

TEST(CodeWheelStore, TellsAMachineNothingWhenItHasNoProgram) {
  // On the heap and not the stack: a machine carries its megabyte of RAM
  // inside it, which is what the sibling suites' `make_unique` is for.
  const auto held =
      std::make_unique<machine::machine>(machine::memory_layout::pc);
  machine::machine& box = *held;
  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));

  EXPECT_FALSE(apply_code_wheel_store(box, store))
      << "there is nothing to look up until a program is loaded";
  EXPECT_FALSE(box.seams().code_wheel_answered());
}

TEST(CodeWheelStore, TellsAMachineAboutTheCopyItIsRunning) {
  // On the heap and not the stack: a machine carries its megabyte of RAM
  // inside it, which is what the sibling suites' `make_unique` is for.
  const auto held =
      std::make_unique<machine::machine>(machine::memory_layout::pc);
  machine::machine& box = *held;
  box.seams().loaded(digest_of(first_hex), 0x1000);

  code_wheel_store store;
  EXPECT_FALSE(apply_code_wheel_store(box, store))
      << "an empty store leaves the challenge exactly where it was";
  EXPECT_FALSE(box.seams().code_wheel_answered());

  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  EXPECT_TRUE(apply_code_wheel_store(box, store));
  EXPECT_TRUE(box.seams().code_wheel_answered());
}

TEST(CodeWheelStore, SaysNothingAboutACopyItHasNeverHeardOf) {
  // On the heap and not the stack: a machine carries its megabyte of RAM
  // inside it, which is what the sibling suites' `make_unique` is for.
  const auto held =
      std::make_unique<machine::machine>(machine::memory_layout::pc);
  machine::machine& box = *held;
  box.seams().loaded(digest_of(second_hex), 0x1000);

  code_wheel_store store;
  EXPECT_TRUE(store.remember(digest_of(first_hex)));
  EXPECT_FALSE(apply_code_wheel_store(box, store));
  EXPECT_FALSE(box.seams().code_wheel_answered())
      << "and the game asks, which is the point of keying it by copy";
}

}  // namespace
}  // namespace amberfolio::host
