// SPDX-License-Identifier: AGPL-3.0-only
//
// A seam switched off while it holds something only one of its own points
// can give back (seam.h, `seam_context::hold_for_points`, #657): it stays
// on, and says so, until a point has given it back and let go, and the
// switch-off is finished there with its `disabled` event; after the batch
// that point started, when it started one. A seam that holds nothing goes
// off at once, as every seam always has. Switched on again first, it is
// on as it was; a reset drops it; and a group never has two members on.
//
// The program is this file's own: a NOP the points sit on and a jump back
// to it, so the point is reached every second step; and a far return a
// batch can call.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view claimed_hex =
    "5757575757575757575757575757575757575757575757575757575757575757";
constexpr std::array<std::string_view, 1> claimed_binaries{claimed_hex};

/// Where the far return a give-back calls is, from the image segment.
constexpr std::uint16_t give_back_offset = 0x0010;

/// What the holding seam's point does, and what it saw.
bool keep_holding = true;        // hold while on; let go only when stopping
bool give_back_by_call = false;  // when stopping, call the program first
std::vector<bool> saw_stopping;
unsigned give_backs = 0;

void hold_until_stopping(machine& /*box*/, seam_context& ctx) {
  saw_stopping.push_back(ctx.stopping());
  if (!ctx.stopping()) {
    ctx.hold_for_points(keep_holding);
    return;
  }
  // On its way off: give back once, and let go.
  if (give_back_by_call && give_backs == 0) {
    ++give_backs;
    const std::array<std::uint16_t, 0> none{};
    EXPECT_TRUE(ctx.call_program(image_load_segment, give_back_offset, none));
  } else if (!give_back_by_call) {
    ++give_backs;
  }
  ctx.hold_for_points(false);
}

unsigned plain_runs = 0;
void hold_nothing(machine& /*box*/, seam_context& /*ctx*/) { ++plain_runs; }

constexpr std::array<seam_point, 1> holding_points{
    {{.module = resident_image, .offset = 0, .run = &hold_until_stopping}}};
constexpr seam_definition holding_seam{
    .id = "test-held",
    .about = "draws something only its own point can take back",
    .fingerprints = claimed_binaries,
    .points = holding_points};

constexpr std::array<seam_point, 1> plain_points{
    {{.module = resident_image, .offset = 0, .run = &hold_nothing}}};
constexpr seam_definition plain_seam{.id = "test-plain",
                                     .about = "holds nothing",
                                     .fingerprints = claimed_binaries,
                                     .points = plain_points};

/// Two alternatives, the first of which holds.
constexpr seam_definition grouped_holding{.id = "test-group-held",
                                          .about = "one of two, and holds",
                                          .fingerprints = claimed_binaries,
                                          .points = holding_points,
                                          .group = "test-group"};
constexpr seam_definition grouped_plain{.id = "test-group-plain",
                                        .about = "the other of two",
                                        .fingerprints = claimed_binaries,
                                        .points = plain_points,
                                        .group = "test-group"};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    keep_holding = true;
    give_back_by_call = false;
    saw_stopping.clear();
    give_backs = 0;
    plain_runs = 0;
    EXPECT_TRUE(box->seams().add(holding_seam));
    EXPECT_TRUE(box->seams().add(plain_seam));
    EXPECT_TRUE(box->seams().add(grouped_holding));
    EXPECT_TRUE(box->seams().add(grouped_plain));
    load();
  }

  void load() const {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(claimed_hex, digest));
    box->seams().loaded(digest, image_load_segment);
    // NOP; JMP back to it. And a RETF for a batch to call.
    const std::uint32_t at = cpu::physical_address(image_load_segment, 0);
    box->memory().ram()[at] = 0x90;
    box->memory().ram()[at + 1] = 0xEB;
    box->memory().ram()[at + 2] = 0xFD;
    box->memory().ram()[at + give_back_offset] = 0xCB;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = image_load_segment;
    r[cpu::sreg::ss] = image_load_segment;
    r[cpu::reg16::sp] = 0x0400;
    r.ip = 0;
  }

  [[nodiscard]] seam_status row(std::string_view id) const {
    return box->seams().status(id);
  }

  void steps(unsigned n) const {
    for (unsigned i = 0; i < n; ++i) {
      box->step();
    }
  }

  /// How many `disabled` events the engine has said for `id`.
  [[nodiscard]] std::size_t offs(std::string_view id) const {
    std::size_t n = 0;
    for (const seam_event& event : log.seam_events) {
      n += event.id == id && event.kind == seam_event_kind::disabled ? 1U : 0U;
    }
    return n;
  }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
};

TEST(SeamStopping, HeldItStaysOnUntilItsPointHasLetGo) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);  // reached once: holds
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);

  // Not off yet, and every surface that asks says so.
  EXPECT_EQ(r.row("test-held").state, seam_state::on);
  EXPECT_TRUE(r.row("test-held").stopping);
  EXPECT_TRUE(r.box->seams().armed());
  EXPECT_EQ(r.box->seams().enabled_count(), 1U);
  EXPECT_EQ(r.offs("test-held"), 0U) << "no `disabled` before it is off";

  r.steps(2);  // reached again: sees it is stopping, gives back, lets go
  ASSERT_EQ(saw_stopping.size(), 2U);
  EXPECT_FALSE(saw_stopping[0]);
  EXPECT_TRUE(saw_stopping[1]);
  EXPECT_EQ(give_backs, 1U);
  EXPECT_EQ(r.row("test-held").state, seam_state::off);
  EXPECT_FALSE(r.row("test-held").stopping);
  EXPECT_FALSE(r.box->seams().armed()) << "the machine is plain again";
  EXPECT_EQ(r.offs("test-held"), 1U);

  r.steps(10);
  EXPECT_EQ(saw_stopping.size(), 2U) << "an off seam is offered nothing";
}

TEST(SeamStopping, ABatchItStartedRunsBeforeItIsOff) {
  const rig r;
  give_back_by_call = true;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  r.steps(1);  // reached: the call is queued and under way
  EXPECT_EQ(give_backs, 1U);
  EXPECT_TRUE(r.box->seams().calling());
  EXPECT_EQ(r.row("test-held").state, seam_state::on)
      << "on while its own give-back runs";
  r.steps(4);  // the RETF, the stub's return, the point offered again
  EXPECT_FALSE(r.box->seams().calling());
  EXPECT_EQ(r.row("test-held").state, seam_state::off);
  EXPECT_EQ(r.offs("test-held"), 1U);
  EXPECT_EQ(r.box->processor().regs()[cpu::sreg::cs], image_load_segment)
      << "and the program is back where it was";
}

TEST(SeamStopping, HoldingNothingItGoesOffAtOnce) {
  const rig r;
  keep_holding = false;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  EXPECT_EQ(r.row("test-held").state, seam_state::off);
  EXPECT_EQ(r.offs("test-held"), 1U);

  // Nor does a seam that has never heard of holding change at all.
  ASSERT_EQ(r.box->seams().enable("test-plain"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-plain"), seam_reason::none);
  EXPECT_EQ(r.row("test-plain").state, seam_state::off);
  EXPECT_FALSE(r.row("test-plain").stopping);
  EXPECT_FALSE(r.box->seams().armed());
}

TEST(SeamStopping, SwitchedOnAgainFirstItIsOnAsItWas) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  EXPECT_FALSE(r.row("test-held").stopping);
  r.steps(6);
  EXPECT_EQ(give_backs, 0U) << "nothing to give back: it never went";
  for (const bool stopping : saw_stopping) {
    EXPECT_FALSE(stopping);
  }
  EXPECT_EQ(r.row("test-held").state, seam_state::on);
  EXPECT_EQ(r.offs("test-held"), 0U);

  // And still holds, so the next switch-off waits as the first did.
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  EXPECT_TRUE(r.row("test-held").stopping);
}

TEST(SeamStopping, AResetDropsItAndTheSeamIsSimplyOff) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  r.box->reset();
  EXPECT_FALSE(r.box->seams().armed());
  r.load();
  EXPECT_EQ(r.row("test-held").state, seam_state::off);
  EXPECT_FALSE(r.row("test-held").stopping);
  r.steps(6);
  EXPECT_EQ(give_backs, 0U);
}

TEST(SeamStopping, AGroupNeverHasTwoOnWhileOneIsStopping) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-group-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-group-held"), seam_reason::none);
  ASSERT_TRUE(r.row("test-group-held").stopping);

  ASSERT_EQ(r.box->seams().enable("test-group-plain"), seam_reason::none);
  EXPECT_EQ(r.row("test-group-held").state, seam_state::off)
      << "a sibling goes off at once, whatever it holds";
  EXPECT_FALSE(r.row("test-group-held").stopping);
  EXPECT_EQ(r.row("test-group-plain").state, seam_state::on);
  EXPECT_EQ(r.box->seams().enabled_count(), 1U);

  // And one that holds, with its sibling switched on beside it.
  const rig other;
  ASSERT_EQ(other.box->seams().enable("test-group-held"), seam_reason::none);
  other.steps(2);
  ASSERT_EQ(other.box->seams().enable("test-group-plain"), seam_reason::none);
  EXPECT_EQ(other.row("test-group-held").state, seam_state::off);
  EXPECT_EQ(other.box->seams().enabled_count(), 1U);
}

TEST(SeamStopping, AskingTwiceIsAskingOnce) {
  const rig r;
  ASSERT_EQ(r.box->seams().enable("test-held"), seam_reason::none);
  r.steps(2);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  ASSERT_EQ(r.box->seams().disable("test-held"), seam_reason::none);
  r.steps(2);
  EXPECT_EQ(give_backs, 1U);
  EXPECT_EQ(r.row("test-held").state, seam_state::off);
  EXPECT_EQ(r.offs("test-held"), 1U);
}

}  // namespace
}  // namespace amberfolio::machine
