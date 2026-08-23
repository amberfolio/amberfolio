// SPDX-License-Identifier: AGPL-3.0-only
//
// The seam engine (seam.h, PLAN.md §5; M4-F2 #96, M4-F3 #97, M4-F4 #98):
// what it refuses, what it costs when nothing is on, what a listing says,
// how a point qualified by an overlay arms and disarms as the program
// loads things, and — the one that matters — that an armed interception
// point runs its handler at the step boundary *before* the instruction
// there is fetched, and a disabled one is never consulted at all.
//
// Every seam here but the code-wheel's is this file's own, keyed to a
// digest this file claims, pointed at instructions this file writes from
// the encoding. The code-wheel seam is exercised through its mechanism
// and not through any program. Every byte here is this file's own
// (PLAN.md §6).

#include "amberfolio/machine/seam.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

using test::recording_diagnostics;

/// A digest a test claims for "the program": nothing hashes to it, which
/// is the point — the engine compares, it does not verify.
constexpr std::string_view claimed_hex =
    "1111111111111111111111111111111111111111111111111111111111111111";
constexpr std::array<std::string_view, 1> claimed_binaries{claimed_hex};

[[nodiscard]] sha256_digest claimed_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(claimed_hex, digest));
  return digest;
}

[[nodiscard]] const seam_definition& code_wheel() {
  for (const seam_definition& seam : all_seams()) {
    if (seam.id == "code-wheel") {
      return seam;
    }
  }
  ADD_FAILURE() << "the code-wheel seam is not in the table";
  return all_seams().front();
}

// --- The test seams ------------------------------------------------------
//
// Handlers are plain function pointers with nowhere to keep state, so the
// counters they bump are file-scope and reset by each test's rig.

std::uint32_t edit_hits = 0;
std::uint32_t key_hits = 0;
std::uint32_t redirect_hits = 0;
std::uint32_t host_calls = 0;
std::uint32_t last_module_base = 0;

/// Edits a register: AX becomes 2222h at the point.
void edit_ax(machine& box, seam_context& /*ctx*/) {
  ++edit_hits;
  box.processor().regs()[cpu::reg16::ax] = 0x2222;
}

/// Posts a keystroke: 'k' (scan code 25h), once per visit.
void post_k(machine& /*box*/, seam_context& ctx) {
  ++key_hits;
  static_cast<void>(ctx.inject_keystroke(0x25, 'k'));
}

/// Moves IP past the instruction at the point — to offset 10h of the same
/// segment, where the program's second half lives.
void skip_ahead(machine& box, seam_context& ctx) {
  ++redirect_hits;
  ctx.redirect(box.processor().regs()[cpu::sreg::cs], 0x0010);
}

/// Asks the host for something, and records where it was armed.
void ask_host(machine& /*box*/, seam_context& ctx) {
  last_module_base = ctx.module_base();
  if (ctx.call_host(seam_host_service::automap_update, 7)) {
    ++host_calls;
  }
}

/// Offsets in the image segment the test programs are placed at.
constexpr std::uint32_t edit_offset = 0x0003;
constexpr std::uint32_t key_offset = 0x0000;
constexpr std::uint32_t redirect_offset = 0x0000;

constexpr std::array<seam_point, 1> edit_points{
    {{.module = resident_image, .offset = edit_offset, .run = &edit_ax}}};
constexpr seam_definition edit_seam{.id = "test-edit",
                                    .about = "sets AX at one instruction",
                                    .fingerprints = claimed_binaries,
                                    .points = edit_points};

constexpr std::array<seam_point, 1> key_points{
    {{.module = resident_image, .offset = key_offset, .run = &post_k}}};
constexpr seam_definition key_seam{.id = "test-key",
                                   .about = "posts a keystroke",
                                   .fingerprints = claimed_binaries,
                                   .points = key_points};

constexpr std::array<seam_point, 1> redirect_points{{{.module = resident_image,
                                                      .offset = redirect_offset,
                                                      .run = &skip_ahead}}};
constexpr seam_definition redirect_seam{.id = "test-redirect",
                                        .about = "moves IP",
                                        .fingerprints = claimed_binaries,
                                        .points = redirect_points};

constexpr seam_definition stale_seam{.id = "test-stale",
                                     .about = "written against another schema",
                                     .fingerprints = claimed_binaries,
                                     .points = edit_points,
                                     .schema = seam_schema_version + 1};

/// The overlay-qualified one: a point at offset 2 of a module that is
/// 16 bytes read from offset 0x40 of OVL.BIN.
constexpr seam_module ovl_module{
    .file = "OVL.BIN", .file_offset = 0x40, .length = 16};
constexpr std::array<seam_point, 1> ovl_points{
    {{.module = ovl_module, .offset = 0x0002, .run = &ask_host}}};
constexpr seam_definition ovl_seam{.id = "test-overlay",
                                   .about = "lives in an overlay",
                                   .fingerprints = claimed_binaries,
                                   .points = ovl_points};

/// The same, but demanding a digest the bytes will not have.
constexpr seam_module wrong_bytes{
    .file = "OVL.BIN",
    .file_offset = 0x40,
    .length = 16,
    .digest =
        "2222222222222222222222222222222222222222222222222222222222222222"};
constexpr std::array<seam_point, 1> wrong_points{
    {{.module = wrong_bytes, .offset = 0x0002, .run = &ask_host}}};
constexpr seam_definition wrong_seam{.id = "test-wrong-bytes",
                                     .about = "wants bytes that never arrive",
                                     .fingerprints = claimed_binaries,
                                     .points = wrong_points};

/// A host that counts what it was asked for.
class counting_host final : public seam_host_services {
 public:
  void serve(machine& /*box*/, seam_host_service which,
             std::uint32_t argument) override {
    served.emplace_back(which, argument);
  }
  std::vector<std::pair<seam_host_service, std::uint32_t>> served;
};

/// A machine with the test seams registered and the claimed binary
/// "loaded" at the segment DOS would have put it at.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    edit_hits = key_hits = redirect_hits = host_calls = 0;
    last_module_base = 0;
    EXPECT_TRUE(box->seams().add(edit_seam));
    EXPECT_TRUE(box->seams().add(key_seam));
    EXPECT_TRUE(box->seams().add(redirect_seam));
    EXPECT_TRUE(box->seams().add(stale_seam));
    EXPECT_TRUE(box->seams().add(ovl_seam));
    EXPECT_TRUE(box->seams().add(wrong_seam));
    box->seams().loaded(claimed_digest(), image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }

  /// Put `bytes` at `image_offset` in the image segment and point the
  /// processor at it, with a stack.
  void program_at(std::uint32_t image_offset,
                  std::initializer_list<std::uint8_t> bytes) const {
    std::uint32_t at =
        cpu::physical_address(image_load_segment, 0) + image_offset;
    for (const std::uint8_t byte : bytes) {
      box->memory().ram()[at] = byte;
      ++at;
    }
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r[cpu::sreg::ss] = image_load_segment;
    r[cpu::reg16::sp] = 0x0400;
    r.ip = static_cast<std::uint16_t>(image_offset);
  }

  /// Pretend the program read 16 bytes of OVL.BIN at file offset 0x40
  /// into `segment`:0000 — the tracker's view of an overlay load, fed
  /// through the machine's own door so the engine is told.
  void load_overlay(std::uint16_t segment, std::uint32_t file_offset = 0x40,
                    std::uint32_t length = 16) const {
    dos_path path;
    const auto resolved =
        canonicalize(dos_path{}, std::span<const char>("\\OVL.BIN", 8));
    ASSERT_TRUE(resolved.ok());
    path = resolved.value;
    const std::array<std::uint8_t, 16> bytes{};
    box->note_file_read(
        path, file_offset, segment, 0, length,
        sha256(std::span<const std::uint8_t>(bytes.data(), length)));
  }

  [[nodiscard]] std::size_t events(seam_event_kind kind) const {
    std::size_t n = 0;
    for (const seam_event& e : log.seam_events) {
      if (e.kind == kind) {
        ++n;
      }
    }
    return n;
  }

  recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- The registry -----------------------------------------------------------

TEST(SeamRegistry, CarriesTheBuildsOwnSeamsAndTakesMore) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  const std::size_t built_in = box->seams().count();
  EXPECT_EQ(built_in, all_seams().size());
  EXPECT_NE(box->seams().find("code-wheel"), nullptr);

  EXPECT_TRUE(box->seams().add(edit_seam));
  EXPECT_EQ(box->seams().count(), built_in + 1);
  EXPECT_EQ(box->seams().find("test-edit"), &edit_seam);

  EXPECT_FALSE(box->seams().add(edit_seam)) << "an id is taken once";
  EXPECT_EQ(box->seams().count(), built_in + 1);
}

TEST(SeamRegistry, RefusesMoreThanItHasRoomFor) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  std::vector<seam_definition> many(seam_engine::max_seams);
  std::vector<std::string> ids(seam_engine::max_seams);
  std::size_t taken = 0;
  for (std::size_t i = 0; i < many.size(); ++i) {
    ids[i] = "filler-" + std::to_string(i);
    many[i] = {.id = ids[i],
               .about = "filler",
               .fingerprints = claimed_binaries,
               .points = {}};
    if (box->seams().add(many[i])) {
      ++taken;
    }
  }
  EXPECT_EQ(taken + all_seams().size(), seam_engine::max_seams);
  EXPECT_EQ(box->seams().count(), seam_engine::max_seams);
}

// --- What enable() refuses -------------------------------------------------

TEST(SeamEnable, RefusesBeforeAProgramIsKnown) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  ASSERT_TRUE(box->seams().add(edit_seam));
  EXPECT_EQ(box->seams().enable("test-edit"), seam_reason::no_program);
  EXPECT_FALSE(box->seams().armed());
  EXPECT_EQ(box->seams().status("test-edit").state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status("test-edit").reason, seam_reason::no_program);
}

TEST(SeamEnable, RefusesANameThatIsNotASeam) {
  const rig r;
  EXPECT_EQ(r.pc().seams().enable("no-such-seam"), seam_reason::unknown_seam);
  EXPECT_EQ(r.pc().seams().disable("no-such-seam"), seam_reason::unknown_seam);
  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.events(seam_event_kind::refused), 2u);
}

TEST(SeamEnable, RefusesABinaryTheAddressesAreNotAbout) {
  const rig r;
  // The code-wheel seam's addresses are facts about a different binary
  // than the one this rig claims.
  EXPECT_EQ(r.pc().seams().enable("code-wheel"), seam_reason::wrong_binary);
  EXPECT_EQ(r.pc().seams().status("code-wheel").state, seam_state::unavailable);
  EXPECT_EQ(r.pc().seams().status("code-wheel").reason,
            seam_reason::wrong_binary);
  EXPECT_FALSE(r.pc().seams().armed());
}

TEST(SeamEnable, RefusesADefinitionWrittenAgainstAnotherSchema) {
  const rig r;
  EXPECT_EQ(r.pc().seams().enable("test-stale"), seam_reason::schema_mismatch);
  EXPECT_EQ(r.pc().seams().status("test-stale").state, seam_state::unavailable);
  EXPECT_EQ(r.pc().seams().status("test-stale").reason,
            seam_reason::schema_mismatch);
}

// --- What it does when it is on ------------------------------------------

TEST(SeamEnable, ArmsAndSaysWhatIsOn) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  EXPECT_TRUE(r.pc().seams().armed());
  EXPECT_TRUE(r.pc().seams().any_enabled());
  ASSERT_EQ(r.pc().seams().enabled_count(), 1u);
  EXPECT_EQ(r.pc().seams().enabled_id(0), "test-edit");

  const seam_status row = r.pc().seams().status("test-edit");
  EXPECT_EQ(row.state, seam_state::on);
  EXPECT_TRUE(row.armed);
  EXPECT_EQ(row.reason, seam_reason::none);
  EXPECT_EQ(r.events(seam_event_kind::enabled), 1u);
  EXPECT_EQ(r.events(seam_event_kind::armed), 1u);

  // A second enable is the same seam on, not a second copy of it.
  EXPECT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  EXPECT_EQ(r.pc().seams().enabled_count(), 1u);
}

TEST(SeamEnable, DisableTurnsItOffAgain) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().enable("test-key"), seam_reason::none);
  EXPECT_EQ(r.pc().seams().enabled_count(), 2u);

  EXPECT_EQ(r.pc().seams().disable("test-edit"), seam_reason::none);
  EXPECT_EQ(r.pc().seams().enabled_count(), 1u);
  EXPECT_EQ(r.pc().seams().enabled_id(0), "test-key");
  EXPECT_EQ(r.pc().seams().status("test-edit").state, seam_state::off);
  EXPECT_TRUE(r.pc().seams().armed()) << "the other is still on";

  EXPECT_EQ(r.pc().seams().disable("test-key"), seam_reason::none);
  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_FALSE(r.pc().seams().any_enabled());
  EXPECT_EQ(r.events(seam_event_kind::disabled), 2u);

  // Off is off; disabling again is not an event.
  EXPECT_EQ(r.pc().seams().disable("test-key"), seam_reason::none);
  EXPECT_EQ(r.events(seam_event_kind::disabled), 2u);
}

TEST(SeamEnable, AResetMachineHasNoProgramAndNoSeams) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  ASSERT_TRUE(r.pc().seams().armed());

  r.pc().reset();

  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.pc().seams().enabled_count(), 0u);
  EXPECT_FALSE(r.pc().seams().have_program());
  EXPECT_EQ(r.pc().seams().enable("test-edit"), seam_reason::no_program);
}

TEST(SeamListing, NamesEverySeamAndWhereItStands) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-key"), seam_reason::none);

  bool saw_code_wheel = false;
  bool saw_key = false;
  bool saw_edit = false;
  for (std::size_t i = 0; i < r.pc().seams().count(); ++i) {
    const seam_status row = r.pc().seams().status(i);
    EXPECT_FALSE(row.id.empty());
    EXPECT_FALSE(row.about.empty());
    if (row.id == "code-wheel") {
      saw_code_wheel = true;
      EXPECT_EQ(row.state, seam_state::unavailable);
      EXPECT_EQ(row.reason, seam_reason::wrong_binary);
    } else if (row.id == "test-key") {
      saw_key = true;
      EXPECT_EQ(row.state, seam_state::on);
      EXPECT_TRUE(row.armed);
    } else if (row.id == "test-edit") {
      saw_edit = true;
      EXPECT_EQ(row.state, seam_state::off);
      EXPECT_FALSE(row.armed);
    }
  }
  EXPECT_TRUE(saw_code_wheel && saw_key && saw_edit);

  // Past the end is an empty row, not a crash.
  EXPECT_TRUE(r.pc().seams().status(r.pc().seams().count()).id.empty());
  EXPECT_STREQ(seam_state_name(seam_state::unavailable), "unavailable");
  EXPECT_STREQ(seam_reason_name(seam_reason::module_not_resident),
               "module_not_resident");
}

TEST(SeamIdentity, KnowsTheEditionOrSaysItDoesNot) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  EXPECT_FALSE(box->seams().have_program());
  EXPECT_EQ(box->seams().known_edition(), nullptr);

  sha256_digest baseline;
  ASSERT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
  box->seams().loaded(baseline, image_load_segment);
  ASSERT_NE(box->seams().known_edition(), nullptr);
  EXPECT_EQ(box->seams().known_edition()->name, known_editions().front().name);
  EXPECT_EQ(box->seams().program(), baseline);

  box->seams().loaded(claimed_digest(), image_load_segment);
  EXPECT_EQ(box->seams().known_edition(), nullptr) << "unrecognized";
  EXPECT_TRUE(box->seams().have_program());
}

// --- What a seam actually did (#131) ------------------------------------
//
// `armed` says an address was computed from where a module was recorded.
// It does not say a handler ever ran there — and a seam whose point is not
// where its facts claim reports `armed`, fires nothing, and reads exactly
// like one that works. `fired` is the difference, and these are the tests
// that it counts runs rather than intentions.

TEST(SeamFired, IsZeroForASeamThatIsOnAndNeverReached) {
  const rig r;
  // HLT at offset 0. The point is at offset 3 and is never reached, which
  // is the shape of a seam armed at an address the program does not go to.
  r.program_at(0, {0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  ASSERT_TRUE(r.pc().seams().status("test-edit").armed);

  r.pc().step();
  r.pc().step();

  EXPECT_EQ(r.pc().seams().status("test-edit").fired, 0u)
      << "armed is not the same claim as fired";
  EXPECT_EQ(edit_hits, 0u);
}

TEST(SeamFired, CountsEveryRunOfTheHandler) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);

  // MOV AX, 1111h ; NOP ; HLT — the point is on the NOP. Three passes,
  // each one a fresh entry to the same two instructions.
  for (unsigned i = 0; i < 3; ++i) {
    r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
    r.pc().step();  // MOV
    r.pc().step();  // the point fires, then NOP
  }

  EXPECT_EQ(r.pc().seams().status("test-edit").fired, 3u);
  EXPECT_EQ(edit_hits, 3u) << "the count and the handler agree";
}

TEST(SeamFired, StartsAgainWhenTheSeamIsEnabledAgain) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  ASSERT_EQ(r.pc().seams().status("test-edit").fired, 1u);

  ASSERT_EQ(r.pc().seams().disable("test-edit"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);

  EXPECT_EQ(r.pc().seams().status("test-edit").fired, 0u)
      << "a count belongs to the enable it was made under";
}

TEST(SeamFired, StaysZeroWhileTheSeamIsOff) {
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(r.pc().seams().status("test-edit").fired, 0u);
  EXPECT_EQ(edit_hits, 0u);
}

// --- The fidelity boundary ---------------------------------------------------

TEST(SeamFidelity, AnUnarmedMachineRunsTheProgramUntouched) {
  const rig r;
  // MOV AX, 1111h ; NOP ; HLT — and nothing enabled.
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111);
  EXPECT_EQ(edit_hits, 0u);
  EXPECT_FALSE(r.pc().seams().armed());
}

TEST(SeamFidelity, ADisabledSeamsBreakpointIsNeverConsulted) {
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().disable("test-edit"), seam_reason::none);
  ASSERT_FALSE(r.pc().seams().armed());

  r.pc().step();
  r.pc().step();
  EXPECT_EQ(edit_hits, 0u) << "off means the handler does not exist";
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111);
}

TEST(SeamFidelity, EnablingThenDisablingLeavesTheRunIdentical) {
  // The same program, run twice: once on a machine nobody asked about
  // seams, once on a machine that had a seam on and off again before the
  // first step. Registers and the program's memory have to agree.
  const rig plain;
  plain.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  for (int i = 0; i < 4; ++i) {
    plain.pc().step();
  }

  const rig toggled;
  toggled.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  ASSERT_EQ(toggled.pc().seams().enable("test-edit"), seam_reason::none);
  ASSERT_EQ(toggled.pc().seams().disable("test-edit"), seam_reason::none);
  for (int i = 0; i < 4; ++i) {
    toggled.pc().step();
  }

  EXPECT_EQ(plain.regs(), toggled.regs());
  const std::span<const std::uint8_t> a = plain.pc().memory().ram();
  const std::span<const std::uint8_t> b = toggled.pc().memory().ram();
  const std::uint32_t base = cpu::physical_address(image_load_segment, 0);
  for (std::uint32_t i = 0; i < 0x400; ++i) {
    ASSERT_EQ(a[base + i], b[base + i]) << "byte " << i;
  }
  EXPECT_EQ(plain.pc().time(), toggled.pc().time());
}

// --- The action primitives ----------------------------------------------------

TEST(SeamActions, ARegisterEditLandsBeforeTheInstructionRuns) {
  const rig r;
  // MOV AX, 1111h ; MOV [0200h], AX ; HLT. The point is on the MOV to
  // memory, so what is stored is what the seam put in AX.
  r.program_at(0, {0xB8, 0x11, 0x11, 0xA3, 0x00, 0x02, 0xF4});
  r.regs()[cpu::sreg::ds] = image_load_segment;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);

  r.pc().step();  // MOV AX, 1111h
  EXPECT_EQ(edit_hits, 0u);
  r.pc().step();  // the point fires, then MOV [0200h], AX runs
  EXPECT_EQ(edit_hits, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x2222);
  const std::uint32_t at = cpu::physical_address(image_load_segment, 0x0200);
  EXPECT_EQ(r.pc().memory().ram()[at], 0x22);
  EXPECT_EQ(r.pc().memory().ram()[at + 1], 0x22);
}

TEST(SeamActions, APostedKeystrokeIsWhatInt16hHandsBack) {
  const rig r;
  // MOV AH, 00h ; INT 16h ; HLT — a blocking read, at the point where the
  // seam posts 'k'. No host key event anywhere, so the read would have
  // halted the machine forever without it.
  r.program_at(0, {0xB4, 0x00, 0xCD, 0x16, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-key"), seam_reason::none);

  for (unsigned i = 0; i < 64 && !r.pc().processor().halted(); ++i) {
    r.pc().step();
  }
  EXPECT_EQ(key_hits, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x256B) << "scan code 25h, 'k'";
  EXPECT_TRUE(r.pc().input().empty())
      << "nothing went through the host's queue";
}

TEST(SeamActions, ARedirectMovesIp) {
  const rig r;
  // At 0: HLT (never reached). At 10h: MOV AX, 3333h ; HLT.
  r.program_at(0x10, {0xB8, 0x33, 0x33, 0xF4});
  r.program_at(0, {0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-redirect"), seam_reason::none);

  r.pc().step();
  EXPECT_EQ(redirect_hits, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x3333) << "the MOV at 10h ran";
  EXPECT_EQ(r.regs().ip, 0x0013) << "and left IP on the HLT after it";
}

TEST(SeamActions, AHostCallReachesAnAttachedHostAndNoOther) {
  const rig r;
  r.load_overlay(0x3000);
  const std::uint32_t overlay_base = cpu::physical_address(0x3000, 0);
  r.program_at(0, {0x90});  // the point is in the overlay, at offset 2
  r.pc().memory().ram()[overlay_base + 2] = 0xF4;  // HLT there
  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  ASSERT_EQ(r.pc().seams().enable("test-overlay"), seam_reason::none);
  ASSERT_TRUE(r.pc().seams().status("test-overlay").armed);

  // No host: the call answers false and nothing happens.
  r.pc().step();
  EXPECT_EQ(host_calls, 0u);
  EXPECT_EQ(last_module_base, overlay_base)
      << "the context knows where its module is";

  counting_host host;
  r.pc().seams().set_host(&host);
  r.regs().ip = 2;
  r.pc().processor().resume();
  r.pc().step();
  EXPECT_EQ(host_calls, 1u);
  ASSERT_EQ(host.served.size(), 1u);
  EXPECT_EQ(host.served[0].first, seam_host_service::automap_update);
  EXPECT_EQ(host.served[0].second, 7u);
}

// --- Overlay qualification ---------------------------------------------------

TEST(SeamOverlay, AnEnabledSeamWaitsForItsModuleAndSaysSo) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-overlay"), seam_reason::none)
      << "enabling takes even though nothing is resident";
  const seam_status before = r.pc().seams().status("test-overlay");
  EXPECT_EQ(before.state, seam_state::on);
  EXPECT_FALSE(before.armed);
  EXPECT_EQ(before.reason, seam_reason::module_not_resident);
  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.events(seam_event_kind::inert), 1u);

  r.load_overlay(0x3000);
  const seam_status after = r.pc().seams().status("test-overlay");
  EXPECT_TRUE(after.armed);
  EXPECT_EQ(after.reason, seam_reason::none);
  EXPECT_TRUE(r.pc().seams().armed());
  EXPECT_EQ(r.events(seam_event_kind::armed), 1u);
}

TEST(SeamOverlay, FiresWhereTheModuleLandedAndNowhereElse) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-overlay"), seam_reason::none);
  r.load_overlay(0x3000);

  // HLT at overlay+2, and a HLT at the same offset of another segment.
  r.pc().memory().ram()[cpu::physical_address(0x3000, 2)] = 0xF4;
  r.pc().memory().ram()[cpu::physical_address(0x4000, 2)] = 0xF4;
  counting_host host;
  r.pc().seams().set_host(&host);

  r.regs()[cpu::sreg::cs] = 0x4000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 0u) << "same offset, wrong segment: not the module";

  r.pc().processor().resume();
  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 1u);
}

TEST(SeamOverlay, AReplacedModuleDisarmsTheSeam) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-overlay"), seam_reason::none);
  r.load_overlay(0x3000);
  ASSERT_TRUE(r.pc().seams().status("test-overlay").armed);

  // Something else read into the same memory: a different offset of the
  // same file, over the top of the module.
  r.load_overlay(0x3000, 0x80, 16);
  const seam_status row = r.pc().seams().status("test-overlay");
  EXPECT_EQ(row.state, seam_state::on);
  EXPECT_FALSE(row.armed);
  EXPECT_EQ(row.reason, seam_reason::module_not_resident);
  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.events(seam_event_kind::inert), 2u) << "once on enable, once now";

  // And back again when it is reloaded, wherever it lands this time.
  r.load_overlay(0x5000);
  EXPECT_TRUE(r.pc().seams().status("test-overlay").armed);
  EXPECT_EQ(r.events(seam_event_kind::armed), 2u);
}

TEST(SeamOverlay, AModuleWithTheWrongBytesStaysInertWithTheReason) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-wrong-bytes"), seam_reason::none);
  r.load_overlay(0x3000);
  const seam_status row = r.pc().seams().status("test-wrong-bytes");
  EXPECT_EQ(row.state, seam_state::on);
  EXPECT_FALSE(row.armed);
  EXPECT_EQ(row.reason, seam_reason::module_not_resident);
  EXPECT_FALSE(r.pc().seams().armed());
}

TEST(SeamOverlay, TheTrackerIsNotConsultedWhileNothingIsOn) {
  const rig r;
  r.load_overlay(0x3000);
  r.load_overlay(0x3000, 0x80, 16);
  EXPECT_TRUE(r.log.seam_events.empty())
      << "reads on a machine with every seam off say nothing about seams";
  EXPECT_EQ(r.pc().overlays().count(), 1u) << "but the table is kept";
}

// --- The code-wheel handler ---------------------------------------------
//
// Its whole contract: at the compare loop, when the expected operand
// points into the candidate-word table, leave the program's own routine
// nothing to disagree about — no iterations, the zero flag set, and the
// two lengths equal.

/// A machine with the code-wheel seam's own binary claimed to be loaded.
struct wheel_rig : rig {
  wheel_rig() {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(code_wheel().fingerprints.front(), digest));
    pc().seams().loaded(digest, image_load_segment);
  }
};

[[nodiscard]] std::uint32_t interception_offset() {
  return code_wheel().points.front().offset;
}

/// Inside the candidate-word table, comfortably: the first entry's
/// characters. The table's own geometry is seam_code_wheel.cpp's; all a
/// test needs is an address the handler must accept and one it must not.
constexpr std::uint16_t in_the_table = 0xC7C3;
constexpr std::uint16_t not_in_the_table = 0x0100;

TEST(SeamCodeWheel, IsQualifiedByTheResidentImage) {
  EXPECT_TRUE(code_wheel().points.front().module.is_resident_image());
  EXPECT_EQ(code_wheel().schema, seam_schema_version);
}

TEST(SeamCodeWheel, MakesTheProgramsOwnCompareReportEqual) {
  const wheel_rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_reason::none);

  r.program_at(interception_offset(), {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);  // one character typed
  regs.set(cpu::reg8::ah, 6);  // six expected
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;
  regs.set_flag(cpu::flag::zf, false);

  r.pc().step();

  EXPECT_EQ(regs[cpu::reg16::cx], 0u) << "no iteration to disagree over";
  EXPECT_TRUE(regs.flag_set(cpu::flag::zf));
  EXPECT_EQ(regs.get(cpu::reg8::al), regs.get(cpu::reg8::ah))
      << "and the length comparison after it agrees too";
}

TEST(SeamCodeWheel, LeavesEveryOtherStringComparisonAlone) {
  const wheel_rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_reason::none);

  r.program_at(interception_offset(), {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);
  regs.set(cpu::reg8::ah, 6);
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = not_in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;

  r.pc().step();

  EXPECT_NE(regs[cpu::reg16::cx], 0u) << "the compare should have run";
  EXPECT_EQ(regs.get(cpu::reg8::al), 1) << "and AL should be untouched";
}

TEST(SeamCodeWheel, DoesNothingAnywhereButItsOwnAddress) {
  const wheel_rig r;
  ASSERT_EQ(r.pc().seams().enable("code-wheel"), seam_reason::none);

  // The identical instruction, one paragraph earlier. A seam is a point,
  // not a pattern.
  r.program_at(interception_offset() - 0x10, {0xF3, 0xA6, 0xF4});
  cpu::registers& regs = r.regs();
  regs[cpu::reg16::cx] = 6;
  regs.set(cpu::reg8::al, 1);
  regs.set(cpu::reg8::ah, 6);
  regs[cpu::sreg::es] = image_load_segment;
  regs[cpu::reg16::di] = in_the_table;
  regs[cpu::sreg::ds] = image_load_segment;
  regs[cpu::reg16::si] = 0x0200;

  r.pc().step();

  EXPECT_NE(regs[cpu::reg16::cx], 0u);
  EXPECT_EQ(regs.get(cpu::reg8::al), 1);
}

}  // namespace
}  // namespace amberfolio::machine
