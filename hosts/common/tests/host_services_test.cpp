// SPDX-License-Identifier: AGPL-3.0-only
//
// The `seam_host_services` both hosts attach (M5-D1, #169).
//
// The *mechanism* — that a handler's `call_host()` routes, that the
// engine counts what a host served and keeps what it carried, that
// attaching one changes nothing about a machine with every seam off — is
// core's, and is asserted in tests/core/machine/seam_test.cpp against
// core's own stand-in host. What is asserted here is the object the two
// hosts actually attach: that it takes the call, that it reads the
// machine at the moment of the call rather than at some later moment,
// and that it writes nothing.
//
// Every byte of the program below is this file's own (PLAN.md §6).

#include "amberfolio/host/host_services.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::host {
namespace {

using machine::seam_context;
using machine::seam_definition;
using machine::seam_host_service;
using machine::seam_point;
using machine::seam_reason;

/// A digest this file claims for "the program": nothing hashes to it,
/// which is the point — the engine compares, it does not verify.
constexpr std::string_view claimed_hex =
    "3333333333333333333333333333333333333333333333333333333333333333";
constexpr std::array<std::string_view, 1> claimed_binaries{claimed_hex};

constexpr std::uint16_t image_segment = 0x0800;

/// What the seam's two points ask for. Distinct, so that "the last call
/// won" is a claim the record can be checked against.
constexpr std::uint32_t journal_argument = 0x0000'BEEF;
constexpr std::uint32_t automap_argument = 0x00C0'FFEE;

void ask_for_journal(machine::machine& /*box*/, seam_context& ctx) {
  static_cast<void>(
      ctx.call_host(seam_host_service::journal_open, journal_argument));
}

void ask_for_automap(machine::machine& /*box*/, seam_context& ctx) {
  static_cast<void>(
      ctx.call_host(seam_host_service::automap_update, automap_argument));
}

constexpr std::array<seam_point, 2> points{{{.module = machine::resident_image,
                                             .offset = 0x0000,
                                             .run = &ask_for_journal},
                                            {.module = machine::resident_image,
                                             .offset = 0x0001,
                                             .run = &ask_for_automap}}};
constexpr seam_definition calling_seam{
    .id = "test-calls-out",
    .about = "asks for both host services, one at each of two points",
    .fingerprints = claimed_binaries,
    .points = points};

/// A machine with that seam registered and the claimed binary "loaded"
/// at the segment DOS would have put it at.
struct rig {
  rig() : box(std::make_unique<machine::machine>(machine::memory_layout::pc)) {
    EXPECT_TRUE(box->seams().add(calling_seam));
    sha256_digest digest;
    EXPECT_TRUE(machine::parse_digest(claimed_hex, digest));
    box->seams().loaded(digest, image_segment);
  }

  [[nodiscard]] machine::machine& pc() const noexcept { return *box; }

  /// NOP, NOP, HLT at the image base, with the processor pointed at it.
  void program() const {
    const std::uint32_t base = cpu::physical_address(image_segment, 0);
    const std::span<std::uint8_t> ram = box->memory().ram();
    ram[base + 0] = 0x90;
    ram[base + 1] = 0x90;
    ram[base + 2] = 0xF4;
    cpu::registers& regs = box->processor().regs();
    regs[cpu::sreg::cs] = image_segment;
    regs[cpu::sreg::ss] = image_segment;
    regs.ip = 0;
    regs[cpu::reg16::sp] = 0x0F00;
  }

  std::unique_ptr<machine::machine> box;
};

TEST(HostServices, ItTakesTheCallAndRemembersWhatItCarried) {
  const rig r;
  host_services services;
  r.pc().seams().set_host(&services);
  r.program();
  ASSERT_EQ(r.pc().seams().enable("test-calls-out"), seam_reason::none);

  EXPECT_FALSE(services.record(seam_host_service::journal_open).seen);
  EXPECT_FALSE(services.record(seam_host_service::automap_update).seen);

  r.pc().step();
  EXPECT_TRUE(services.record(seam_host_service::journal_open).seen);
  EXPECT_EQ(services.record(seam_host_service::journal_open).argument,
            journal_argument);
  EXPECT_FALSE(services.record(seam_host_service::automap_update).seen)
      << "the other point has not been reached yet";

  r.pc().step();
  EXPECT_TRUE(services.record(seam_host_service::automap_update).seen);
  EXPECT_EQ(services.record(seam_host_service::automap_update).argument,
            automap_argument);
}

TEST(HostServices, ItReadsTheMachineAtTheMomentOfTheCall) {
  // The one fact this object keeps, and the reason it is C++ inside the
  // module rather than a queue a page drains later: the machine's own
  // virtual time at the instant the seam called out. A handler runs at
  // the step boundary *before* the instruction at its point, so the tick
  // recorded is the tick the machine stood on then — and the two points
  // are on consecutive instructions, so the second is strictly later.
  const rig r;
  host_services services;
  r.pc().seams().set_host(&services);
  r.program();
  ASSERT_EQ(r.pc().seams().enable("test-calls-out"), seam_reason::none);

  const machine::ticks before = r.pc().time();
  r.pc().step();
  const machine::ticks first =
      services.record(seam_host_service::journal_open).at;
  EXPECT_EQ(first, before) << "the boundary before the instruction, not after";

  r.pc().step();
  const machine::ticks second =
      services.record(seam_host_service::automap_update).at;
  EXPECT_GT(second, first);
  EXPECT_LE(second, r.pc().time());
}

TEST(HostServices, ItWritesNothing) {
  // PLAN.md §4's rule from the far side: a host reads this machine and
  // only a seam may move it. Two runs of the same program, one with the
  // seam on and the services answering and one with neither, have to
  // hash the same — because all the seam does is ask, and all the host
  // does is listen.
  const rig plain;
  plain.program();
  plain.pc().step();
  plain.pc().step();
  const machine::state_hashes plain_hash = machine::hash_state(plain.pc());

  const rig hosted;
  host_services services;
  hosted.pc().seams().set_host(&services);
  hosted.program();
  ASSERT_EQ(hosted.pc().seams().enable("test-calls-out"), seam_reason::none);
  hosted.pc().step();
  hosted.pc().step();

  EXPECT_TRUE(services.record(seam_host_service::journal_open).seen)
      << "and it really was asked, which is what makes the equality mean"
         " something";
  EXPECT_EQ(plain_hash.whole, machine::hash_state(hosted.pc()).whole);
  EXPECT_EQ(plain.pc().time(), hosted.pc().time());
}

// ---------------------------------------------------------------------------
// The journal reader's service (M5-E4, #175)
// ---------------------------------------------------------------------------
//
// The seam that consumes this lives in core and is tested there. What is
// asserted here is the half this object owns: which of the four answers a
// given store produces, and that the answer reaches the machine's own
// delivery buffer rather than being lost in a `void` return.
//
// Every byte of text below is this file's own. Nothing in it is a journal
// or resembles one (`docs/journal.md`).

/// The seam above asks for entry 0xBEEF, which no store here has. These
/// tests ask directly instead, which is what `serve()`'s contract allows:
/// it is a plain virtual taking the machine and one word.
///
/// That word is a *packed citation* since #218 — a section and a number —
/// so these go through `journal_open_argument` rather than casting, and
/// the raw-word cases below pass the word itself on purpose.
[[nodiscard]] machine::journal_delivery ask(host_services& services,
                                            machine::machine& box,
                                            std::uint32_t argument) {
  services.serve(box, seam_host_service::journal_open, argument);
  return box.journal().delivery();
}

[[nodiscard]] std::uint32_t Entry(std::uint16_t number) {
  return machine::journal_open_argument(
      {.kind = machine::journal_kind::entry, .number = number});
}

[[nodiscard]] std::uint32_t Tale(std::uint16_t number) {
  return machine::journal_open_argument(
      {.kind = machine::journal_kind::tale, .number = number});
}

TEST(HostServicesJournal, WithNoStoreAtAllNobodyHasReadAJournal) {
  const rig r;
  host_services services;
  EXPECT_EQ(services.journal(), nullptr);
  EXPECT_EQ(ask(services, r.pc(), Entry(12)),
            machine::journal_delivery::no_journal);
}

TEST(HostServicesJournal, AnEmptyStoreIsTheSameAnswerAsNoStore) {
  const rig r;
  host_services services;
  const journal_store store;
  services.set_journal_store(&store);
  EXPECT_EQ(ask(services, r.pc(), Entry(12)),
            machine::journal_delivery::no_journal);
}

TEST(HostServicesJournal, AnEntryTheStoreHasComesBackAsItsText) {
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(store.record_scan({.number = 12}, "what the engine read"));
  services.set_journal_store(&store);

  EXPECT_EQ(ask(services, r.pc(), Entry(12)), machine::journal_delivery::ready);
  EXPECT_EQ(r.pc().journal().text(), "what the engine read");
  EXPECT_EQ(r.pc().journal().entry(), machine::journal_citation{})
      << "the entry number is the seam's to record when it asks; this"
         " object only answers";
}

TEST(HostServicesJournal, ACorrectionIsWhatTheReaderGets) {
  // The whole reason a store keeps two texts per entry
  // (`journal_store.h`): a person's transcription is what a reader shows.
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(store.record_scan({.number = 12}, "vvhat the enginc read"));
  ASSERT_TRUE(store.correct({.number = 12}, "what the engine read"));
  services.set_journal_store(&store);

  EXPECT_EQ(ask(services, r.pc(), Entry(12)), machine::journal_delivery::ready);
  EXPECT_EQ(r.pc().journal().text(), "what the engine read");
}

TEST(HostServicesJournal, AnEntryTheStoreHasNotIsItsOwnAnswer) {
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(store.record_scan({.number = 12}, "text"));
  services.set_journal_store(&store);

  EXPECT_EQ(ask(services, r.pc(), Entry(13)),
            machine::journal_delivery::no_entry);
  EXPECT_EQ(ask(services, r.pc(), Entry(0)),
            machine::journal_delivery::no_entry)
      << "an entry number that is not one is not an entry";
  EXPECT_EQ(ask(services, r.pc(), 0x0009'000C),
            machine::journal_delivery::no_entry)
      << "a section this build has no name for is not a section";

  // The point of the pair: the same number in another section is another
  // text, and the store has not got this one.
  EXPECT_EQ(ask(services, r.pc(), Tale(12)),
            machine::journal_delivery::no_entry);
}

TEST(HostServicesJournal, TheSameNumberInTwoSectionsIsTwoTexts) {
  // #218's whole reason. Before the kind these two rows could not both
  // exist, and a citation naming either got whichever one was written.
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(store.record_scan({.number = 4}, "the fourth entry"));
  ASSERT_TRUE(store.record_scan(
      {.kind = machine::journal_kind::tale, .number = 4}, "the fourth tale"));
  services.set_journal_store(&store);

  EXPECT_EQ(ask(services, r.pc(), Entry(4)), machine::journal_delivery::ready);
  EXPECT_EQ(r.pc().journal().text(), "the fourth entry");
  EXPECT_EQ(ask(services, r.pc(), Tale(4)), machine::journal_delivery::ready);
  EXPECT_EQ(r.pc().journal().text(), "the fourth tale");
}

TEST(HostServicesJournal, AnEntryWithNothingInItSaysThatAndNotNoEntry) {
  // The two are fixed by different things: one by ingesting a journal,
  // the other by ingesting it with an engine that works.
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(store.record_scan({.number = 12}, "text"));
  ASSERT_TRUE(store.record_scan({.number = 13}, ""));
  services.set_journal_store(&store);

  EXPECT_EQ(ask(services, r.pc(), Entry(13)),
            machine::journal_delivery::no_text);
}

TEST(HostServicesJournal, AnsweringIsNotWritingMachineState) {
  // `serve()`'s contract, with the one consumer that writes anything at
  // all: the delivery buffer is observation, on `machine/journal.h`'s own
  // three terms, and a machine holding a page of text hashes as the
  // machine that is not.
  const rig r;
  host_services services;
  journal_store store;
  ASSERT_TRUE(
      store.record_scan({.number = 12}, "a whole page of somebody's own text"));
  services.set_journal_store(&store);

  const machine::state_hashes before = machine::hash_state(r.pc());
  ASSERT_EQ(ask(services, r.pc(), Entry(12)), machine::journal_delivery::ready);
  EXPECT_EQ(before, machine::hash_state(r.pc()));
}

}  // namespace
}  // namespace amberfolio::host
