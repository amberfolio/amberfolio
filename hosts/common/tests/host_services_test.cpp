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
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/edition.h"
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

}  // namespace
}  // namespace amberfolio::host
