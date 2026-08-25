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
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/machine/state.h"
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
std::uint32_t decline_hits = 0;
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

/// Declines, every time, and touches nothing.
void decline_always(machine& /*box*/, seam_context& ctx) {
  ++decline_hits;
  ctx.decline(seam_reason::point_not_recognized);
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

/// The triggered one (#161): the same point and the same handler as
/// `edit_seam`, and the difference is only that nothing happens there
/// until somebody pulls it.
constexpr seam_definition trigger_seam{
    .id = "test-trigger",
    .about = "sets AX at one instruction, when pulled",
    .fingerprints = claimed_binaries,
    .points = edit_points,
    .trigger = true};

/// A triggered seam whose handler always declines, for the one rule a
/// decline has that a plain seam's does not: a pull that arrived at a
/// point which was not the point is not a pull that was served.
constexpr std::array<seam_point, 1> declining_points{
    {{.module = resident_image,
      .offset = edit_offset,
      .run = &decline_always}}};
constexpr seam_definition declining_seam{
    .id = "test-trigger-declines",
    .about = "a trigger whose point is never what its facts say",
    .fingerprints = claimed_binaries,
    .points = declining_points,
    .trigger = true};

/// The address-free one (#163): a trigger whose point has no address at
/// all and is offered at every step boundary while the latch is set.
/// `offset` is deliberately left out — there is nothing for it to mean,
/// and a number here would be a fact nothing reads.
constexpr std::array<seam_point, 1> pull_points{
    {{.module = resident_image, .run = &edit_ax, .at_every_step = true}}};
constexpr seam_definition pull_seam{
    .id = "test-pull",
    .about = "a trigger with no address: it acts at the first step",
    .fingerprints = claimed_binaries,
    .points = pull_points,
    .trigger = true};

/// The same, with a handler that declines until the program has put
/// something in AX — a stand-in for the guard a real address-free point
/// has instead of an address (`seam_cheats.cpp`). The offers it turns
/// down are what keep the latch, and the one it takes is the "first step
/// at which acting is safe" the whole mechanism is about.
void edit_when_ax_is_set(machine& box, seam_context& ctx) {
  cpu::registers& regs = box.processor().regs();
  if (regs[cpu::reg16::ax] == 0) {
    ++decline_hits;
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  ++edit_hits;
  regs[cpu::reg16::ax] = 0x2222;
}

constexpr std::array<seam_point, 1> guarded_pull_points{
    {{.module = resident_image,
      .run = &edit_when_ax_is_set,
      .at_every_step = true}}};
constexpr seam_definition guarded_pull_seam{
    .id = "test-pull-guarded",
    .about = "a trigger with no address and a guard it waits on",
    .fingerprints = claimed_binaries,
    .points = guarded_pull_points,
    .trigger = true};

/// And one on a definition that is **not** a trigger, which is a mistake
/// nothing can ever set the latch of. It has to be inert rather than
/// firing at every step, which is the fail-closed direction.
constexpr seam_definition untriggered_pull_seam{
    .id = "test-pull-untriggered",
    .about = "an address-free point on a seam that takes no trigger",
    .fingerprints = claimed_binaries,
    .points = pull_points};

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

/// The same module again, but qualified the way a module the program can
/// move has to be (#131): the program keeps its current load segment in
/// one word of the resident image, and this is that word's offset. The
/// engine reads it at every step instead of trusting where a read
/// landed.
constexpr std::uint32_t moved_load_segment_at = 0x0100;
constexpr seam_module moved_module{.file = "OVL.BIN",
                                   .file_offset = 0x40,
                                   .length = 16,
                                   .load_segment_at = moved_load_segment_at};
constexpr std::array<seam_point, 1> moved_points{
    {{.module = moved_module, .offset = 0x0002, .run = &ask_host}}};
constexpr seam_definition moved_seam{.id = "test-moving-overlay",
                                     .about = "lives in a module that moves",
                                     .fingerprints = claimed_binaries,
                                     .points = moved_points};

/// A host that counts what it was asked for.
class counting_host final : public seam_host_services {
 public:
  void serve(machine& /*box*/, seam_host_service which,
             std::uint32_t argument) override {
    served.emplace_back(which, argument);
  }
  std::vector<std::pair<seam_host_service, std::uint32_t>> served;
};

// --- A gated seam and a synthetic document (M5-D3, #171) ---------------
//
// The gate is a possession gate (PLAN.md §5), so the mechanism is
// "somebody hashed a file and it was one this build knows". Neither half
// of that may be a real document here: a fingerprint of the code wheel
// is a fact this tree may keep (machine/document.cpp keeps one), but a
// *test* that needed the file itself would be a test nobody without the
// document could run. So this file claims a digest of its own, registers
// an edition naming it, and presents it — the same shape
// `claimed_binaries` above uses for the program, one artifact over.

constexpr std::string_view claimed_document_hex =
    "4444444444444444444444444444444444444444444444444444444444444444";

[[nodiscard]] sha256_digest claimed_document_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(claimed_document_hex, digest));
  return digest;
}

constexpr document_edition claimed_document{
    .fingerprint = claimed_document_hex,
    .name = "a code wheel this test claims",
    .kind = document_kind::code_wheel};

/// A seam gated on it, at the same point `test-edit` uses — so the two
/// differ in the gate and in nothing else, which is what makes the pair
/// a measurement rather than a story.
constexpr seam_definition gated_seam{.id = "test-gated",
                                     .about = "needs a code wheel",
                                     .fingerprints = claimed_binaries,
                                     .points = edit_points,
                                     .gate = document_kind::code_wheel};

/// And one gated on the other document, so that presenting one does not
/// quietly satisfy the other.
constexpr seam_definition journal_gated_seam{.id = "test-gated-journal",
                                             .about = "needs a journal",
                                             .fingerprints = claimed_binaries,
                                             .points = edit_points,
                                             .gate = document_kind::journal};

/// A machine with the test seams registered and the claimed binary
/// "loaded" at the segment DOS would have put it at.
struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    edit_hits = key_hits = redirect_hits = host_calls = decline_hits = 0;
    last_module_base = 0;
    EXPECT_TRUE(box->seams().add(edit_seam));
    EXPECT_TRUE(box->seams().add(trigger_seam));
    EXPECT_TRUE(box->seams().add(declining_seam));
    EXPECT_TRUE(box->seams().add(pull_seam));
    EXPECT_TRUE(box->seams().add(guarded_pull_seam));
    EXPECT_TRUE(box->seams().add(untriggered_pull_seam));
    EXPECT_TRUE(box->seams().add(key_seam));
    EXPECT_TRUE(box->seams().add(redirect_seam));
    EXPECT_TRUE(box->seams().add(stale_seam));
    EXPECT_TRUE(box->seams().add(ovl_seam));
    EXPECT_TRUE(box->seams().add(wrong_seam));
    EXPECT_TRUE(box->seams().add(moved_seam));
    EXPECT_TRUE(box->seams().add(gated_seam));
    EXPECT_TRUE(box->seams().add(journal_gated_seam));
    EXPECT_TRUE(box->seams().add_document(claimed_document));
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

  /// Write what the program's overlay manager writes: the segment its
  /// module begins at right now, in the word `moved_module` names. Zero
  /// is "not loaded". Nothing is told about this — which is the point,
  /// because nothing tells the program's manager to announce a move
  /// either.
  void manager_says_module_at(std::uint16_t segment) const {
    const std::uint32_t at =
        cpu::physical_address(image_load_segment, 0) + moved_load_segment_at;
    box->memory().ram()[at] = static_cast<std::uint8_t>(segment);
    box->memory().ram()[at + 1] = static_cast<std::uint8_t>(segment >> 8U);
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

// --- The trigger (#161) ------------------------------------------------------
//
// A seam that is *pulled* rather than left on. The engine facility, not
// the cheat: these are this file's own seams, at this file's own points,
// so what is asserted is the mechanism and never a fact about a program.

TEST(SeamTrigger, ReachesItsPointAndDoesNothingUntilPulled) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  // MOV AX, 1111h ; NOP ; HLT — the point is on the NOP.
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();

  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111) << "nobody asked";
  EXPECT_EQ(edit_hits, 0u);

  const seam_status row = r.pc().seams().status("test-trigger");
  EXPECT_TRUE(row.trigger);
  EXPECT_FALSE(row.waiting);
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(row.reached, 1u)
      << "the point was arrived at; that is the number a latency is"
         " measured from";
}

TEST(SeamTrigger, ActsOnceWhenPulledAndThenWaitsToBeAskedAgain) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger", r.pc().time()),
            seam_reason::none);
  EXPECT_TRUE(r.pc().seams().waiting("test-trigger"));

  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x2222) << "asked, and served";
  EXPECT_EQ(edit_hits, 1u);
  EXPECT_FALSE(r.pc().seams().waiting("test-trigger")) << "one pull, one run";

  // Round again with nothing asked for: the point is reached and the
  // program keeps its own answer.
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111);
  EXPECT_EQ(edit_hits, 1u);

  const seam_status row = r.pc().seams().status("test-trigger");
  EXPECT_EQ(row.fired, 1u);
  EXPECT_EQ(row.reached, 2u) << "reached twice, served once";
  EXPECT_EQ(r.events(seam_event_kind::pulled), 1u);
  EXPECT_EQ(r.events(seam_event_kind::served), 1u);
}

TEST(SeamTrigger, ASecondPullWhileOneIsOutstandingIsNotASecondRun) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger", 100), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger", 500), seam_reason::none);
  EXPECT_EQ(r.pc().seams().status("test-trigger").pulled_at, 100u)
      << "the wait a host shows is the wait since the person first asked";

  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(edit_hits, 1u);
}

TEST(SeamTrigger, TheWaitIsMeasuredInTicks) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  // Two instructions before the point, so the machine has spent time
  // between the pull and the arrival.
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  const ticks pulled = r.pc().time();
  ASSERT_EQ(r.pc().seams().pull("test-trigger", pulled), seam_reason::none);
  r.pc().step();
  const ticks after_mov = r.pc().time();
  ASSERT_GT(after_mov, pulled) << "a step costs virtual time";
  r.pc().step();

  const seam_status row = r.pc().seams().status("test-trigger");
  EXPECT_FALSE(row.waiting);
  EXPECT_EQ(row.waited, after_mov - pulled);
}

TEST(SeamTrigger, RefusesAPullOnASeamThatIsOffOrIsNotOne) {
  const rig r;
  EXPECT_EQ(r.pc().seams().pull("test-trigger", 0), seam_reason::not_enabled)
      << "a latch on a seam nobody turned on would fire at some unrelated"
         " later moment";
  EXPECT_EQ(r.pc().seams().pull("not-a-seam", 0), seam_reason::unknown_seam);

  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  EXPECT_EQ(r.pc().seams().pull("test-edit", 0), seam_reason::not_triggered)
      << "an ordinary seam acts whenever it is on; there is nothing to"
         " latch";
}

TEST(SeamTrigger, DisablingDropsAnOutstandingPull) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger", 0), seam_reason::none);
  ASSERT_EQ(r.pc().seams().disable("test-trigger"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  EXPECT_FALSE(r.pc().seams().waiting("test-trigger"));

  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111)
      << "the pull died with the enable";
  EXPECT_EQ(edit_hits, 0u);
}

TEST(SeamTrigger, AResetMachineHasNoLatch) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger", 0), seam_reason::none);
  r.pc().reset();
  EXPECT_FALSE(r.pc().seams().waiting("test-trigger"))
      << "a latch is configuration, and a reset machine has no program";
}

TEST(SeamTrigger, ADeclinedVisitKeepsTheLatch) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-trigger-declines"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-trigger-declines", 0), seam_reason::none);

  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  r.pc().step();
  r.pc().step();

  EXPECT_EQ(decline_hits, 1u);
  EXPECT_TRUE(r.pc().seams().waiting("test-trigger-declines"))
      << "a pull that arrived at a point which was not the point is not a"
         " pull that was served";
  EXPECT_EQ(r.pc().seams().status("test-trigger-declines").reached, 1u);
  EXPECT_EQ(r.pc().seams().status("test-trigger-declines").fired, 0u)
      << "and it is not a firing either (#163): a handler that says it did"
         " not act is exactly the failure `fired` exists to expose, and"
         " counting it made that failure look like success";
}

// --- A point with no address (#163) -----------------------------------------

TEST(SeamPullPoint, ActsAtTheVeryFirstStepAfterAPull) {
  // The whole of what an address-free point buys: no waiting for the
  // program to go anywhere. The point is offered at the first step
  // boundary after the pull, and that is where it acts.
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-pull", r.pc().time()), seam_reason::none);

  r.pc().step();  // MOV AX, 1111h — with the seam having run before it

  EXPECT_EQ(edit_hits, 1u);
  EXPECT_FALSE(r.pc().seams().waiting("test-pull")) << "one pull, one run";
  const seam_status row = r.pc().seams().status("test-pull");
  EXPECT_EQ(row.fired, 1u);
  EXPECT_EQ(row.reached, 0u)
      << "a point with no address has no arrivals to count; counting its"
         " offers would be counting steps";
}

TEST(SeamPullPoint, IsOfferedEveryStepAndKeepsTheLatchUntilItsGuardHolds) {
  // The shape a real one has: it cannot tell from an address whether
  // acting is safe, so it asks the machine, declines while the answer is
  // no — which keeps the latch — and acts at the first step where the
  // answer is yes. Here the guard is "the program has put something in
  // AX", and the program does that on its third instruction.
  const rig r;
  // NOP ; NOP ; MOV AX, 1111h ; HLT
  r.program_at(0, {0x90, 0x90, 0xB8, 0x11, 0x11, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull-guarded"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-pull-guarded", r.pc().time()),
            seam_reason::none);

  r.pc().step();  // offered, guard does not hold
  r.pc().step();  // offered again
  EXPECT_EQ(decline_hits, 2u) << "offered at every step, not at an address";
  EXPECT_EQ(edit_hits, 0u);
  EXPECT_TRUE(r.pc().seams().waiting("test-pull-guarded"))
      << "and every one of those declines kept the pull outstanding";

  r.pc().step();  // MOV AX, 1111h runs; the guard did not hold before it
  EXPECT_EQ(decline_hits, 3u);
  r.pc().step();  // now it does, before the HLT
  EXPECT_EQ(edit_hits, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x2222);
  EXPECT_FALSE(r.pc().seams().waiting("test-pull-guarded"));

  const seam_status row = r.pc().seams().status("test-pull-guarded");
  EXPECT_EQ(row.fired, 1u) << "three declines and one act is one firing";
  EXPECT_EQ(row.reached, 0u);
}

TEST(SeamPullPoint, SaysItDeclinedOnceHoweverManyStepsItTook) {
  // A point offered at every step declines at most of them. One line
  // says what there is to say; a line per step would bury the run.
  const rig r;
  r.program_at(0, {0x90, 0x90, 0x90, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull-guarded"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-pull-guarded", r.pc().time()),
            seam_reason::none);
  for (int i = 0; i < 4; ++i) {
    r.pc().step();
  }

  EXPECT_EQ(decline_hits, 4u);
  std::size_t lines = 0;
  for (const seam_event& e : r.log.seam_events) {
    if (e.id == "test-pull-guarded" && e.kind == seam_event_kind::inert) {
      ++lines;
    }
  }
  EXPECT_EQ(lines, 1u);
}

TEST(SeamPullPoint, IsInertOnASeamThatTakesNoTrigger) {
  // Nothing can set that seam's latch — `pull()` refuses it — so the
  // point is never offered. Inert rather than firing at every step,
  // which is the fail-closed direction for a mistake in the table.
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull-untriggered"), seam_reason::none);
  EXPECT_EQ(r.pc().seams().pull("test-pull-untriggered", 0),
            seam_reason::not_triggered);

  r.pc().step();
  r.pc().step();
  EXPECT_EQ(edit_hits, 0u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111);
}

// --- What a row means (#163) ------------------------------------------------
//
// The defect these exist for: both hosts used to decide this out of the
// numbers beside it, and both said "armed and never reached; its point
// may not be where its facts say" over a row that read `fired=1
// reached=0` — which is what a seam served by a point with no address
// reports, and is a success. Measured against the real program, a pull
// made during a fight came back `fired=1 reached=1 waited=0` and one
// made before the round began came back `fired=1 reached=0
// waited=8327644`, and the second printed that warning. `fired=1` and
// "never reached" cannot both be true, and sending a reader to doubt a
// working address table is #131's harm with the sign flipped.

/// A row as a host is handed one, built by hand so a reading can be
/// asked about a *state* rather than about a run. Assigned field by
/// field rather than designated-initialized: the reading depends on six
/// of these and on none of the rest, and a list that named them in
/// declaration order would go stale the next time a field is added
/// between two of them.
[[nodiscard]] seam_status armed_row() {
  seam_status row;
  row.id = "a-row";
  row.state = seam_state::on;
  row.armed = true;
  return row;
}

TEST(SeamReading, NeverWarnsAboutAnAddressWhenTheSeamActed) {
  // The invariant, stated once and directly: whatever else a row says,
  // it does not tell a reader to go and doubt the fact table of a seam
  // that did the thing it was asked to do.
  for (const bool addressed : {false, true}) {
    for (const bool trigger : {false, true}) {
      for (const std::uint64_t reached : {std::uint64_t{0}, std::uint64_t{7}}) {
        for (const std::uint64_t declined :
             {std::uint64_t{0}, std::uint64_t{3}}) {
          seam_status row = armed_row();
          row.fired = 1;
          row.trigger = trigger;
          row.reached = reached;
          row.declined = declined;
          row.addressed = addressed;
          EXPECT_NE(seam_reading_of(row), seam_reading::never_reached)
              << "addressed=" << addressed << " trigger=" << trigger
              << " reached=" << reached << " declined=" << declined;
        }
      }
    }
  }
}

TEST(SeamReading, SaysWhichOfTheThreeThingsHappened) {
  // Did it act, and if not, why not. A person watching for a cheat to go
  // off wants exactly one of these three, and the two ways of waiting
  // mean opposite things: declined is the seam working and refusing,
  // not-reached is the program not having been there.
  seam_status served = armed_row();
  served.fired = 1;
  served.trigger = true;
  served.addressed = true;
  EXPECT_EQ(seam_reading_of(served), seam_reading::served);

  seam_status waiting = served;
  waiting.fired = 0;
  waiting.waiting = true;
  EXPECT_EQ(seam_reading_of(waiting), seam_reading::pulled_and_not_served);

  waiting.declined = 4;
  EXPECT_EQ(seam_reading_of(waiting), seam_reading::pulled_and_declined);

  seam_status idle = armed_row();
  idle.trigger = true;
  idle.reached = 12;
  idle.addressed = true;
  EXPECT_EQ(seam_reading_of(idle), seam_reading::reached_and_never_pulled);

  // Three different sentences, and none of them empty.
  for (const seam_reading reading :
       {seam_reading::served, seam_reading::pulled_and_not_served,
        seam_reading::pulled_and_declined,
        seam_reading::reached_and_never_pulled, seam_reading::never_reached}) {
    EXPECT_STRNE(seam_reading_text(reading), "");
  }
  EXPECT_STREQ(seam_reading_text(seam_reading::nothing_to_say), "");
}

TEST(SeamReading, KeepsHash131sWarningForASeamThatHasAnAddress) {
  seam_status unreached = armed_row();
  unreached.addressed = true;
  EXPECT_EQ(seam_reading_of(unreached), seam_reading::never_reached);

  // The same row for a seam with no address in it at all: there is no
  // fact table to doubt, so there is nothing to say.
  seam_status address_free = unreached;
  address_free.addressed = false;
  EXPECT_EQ(seam_reading_of(address_free), seam_reading::nothing_to_say);

  // And an inert seam says `inert` and its reason already.
  seam_status inert = unreached;
  inert.armed = false;
  EXPECT_EQ(seam_reading_of(inert), seam_reading::nothing_to_say);
}

TEST(SeamReading, IsWhatTheRowsOfThisBuildsSeamsActuallySay) {
  // The three states, taken off real rows rather than off hand-built
  // ones — `test-pull`'s point has no address, so a served pull leaves
  // exactly the row that used to be reported as a broken address table.
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull"), seam_reason::none);
  EXPECT_EQ(seam_reading_of(r.pc().seams().status("test-pull")),
            seam_reading::nothing_to_say)
      << "on, never pulled, and no address to be wrong about";

  ASSERT_EQ(r.pc().seams().pull("test-pull", r.pc().time()), seam_reason::none);
  EXPECT_EQ(seam_reading_of(r.pc().seams().status("test-pull")),
            seam_reading::pulled_and_not_served);

  r.pc().step();
  const seam_status row = r.pc().seams().status("test-pull");
  ASSERT_EQ(row.fired, 1u);
  ASSERT_EQ(row.reached, 0u);
  EXPECT_EQ(seam_reading_of(row), seam_reading::served)
      << "fired=1 reached=0 is a success and has to read as one";
}

TEST(SeamReading, CountsDeclinesAndSaysSoOnce) {
  const rig r;
  r.program_at(0, {0x90, 0x90, 0x90, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-pull-guarded"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().pull("test-pull-guarded", r.pc().time()),
            seam_reason::none);
  for (int i = 0; i < 4; ++i) {
    r.pc().step();
  }

  const seam_status row = r.pc().seams().status("test-pull-guarded");
  EXPECT_EQ(row.declined, 4u) << "counted every time";
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(seam_reading_of(row), seam_reading::pulled_and_declined)
      << "which is a different thing to be waiting on than a point the"
         " program has not been to";
  EXPECT_EQ(r.events(seam_event_kind::inert), 1u) << "and said once";
}

TEST(SeamReading, KnowsWhichSeamsInThisBuildHaveAddressesInThem) {
  const rig r;
  EXPECT_TRUE(r.pc().seams().status("test-edit").addressed);
  EXPECT_FALSE(r.pc().seams().status("test-pull").addressed);
  // The cheats' kill-all has one of each, so it is addressed — and its
  // `reached` goes on measuring the end check, which is the number that
  // says what a pull used to cost.
  const seam_definition* kill_all = r.pc().seams().find("cheat-kill-all");
  ASSERT_NE(kill_all, nullptr);
  bool addressed = false;
  bool address_free = false;
  for (const seam_point& point : kill_all->points) {
    if (point.at_every_step) {
      address_free = true;
    } else {
      addressed = true;
    }
  }
  EXPECT_TRUE(addressed);
  EXPECT_TRUE(address_free);
}

TEST(SeamPullPoint, EveryAddressFreePointInThisBuildIsOnATrigger) {
  // The rule the case above rests on, asked of the seams this build
  // actually carries rather than of the test's own.
  for (const seam_definition& seam : all_seams()) {
    for (const seam_point& point : seam.points) {
      if (point.at_every_step) {
        EXPECT_TRUE(seam.trigger)
            << seam.id
            << ": a point with no address on a seam nobody can pull is a"
               " point that never runs";
      }
    }
  }
}

// --- Document gates (M5-D3, #171) --------------------------------------
//
// PLAN.md §5's possession gate, as a mechanism: a seam names a document
// kind, and until a recognized document of that kind has been presented
// it is on, inert, and says why. Fail-closed by construction — the gate
// is tested where residency is tested, so there is no path that arms a
// gated seam without a satisfied gate.

TEST(SeamGate, AGatedSeamIsOnAndInertUntilTheDocumentIsPresented) {
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});

  // It enables. That is the point: a gate is not a refusal — the seam
  // took, and the player has not shown the thing they are asked to hold.
  ASSERT_EQ(r.pc().seams().enable("test-gated"), seam_reason::none);
  const seam_status before = r.pc().seams().status("test-gated");
  EXPECT_EQ(before.state, seam_state::on);
  EXPECT_FALSE(before.armed);
  EXPECT_EQ(before.reason, seam_reason::document_not_presented);
  EXPECT_FALSE(r.pc().seams().armed())
      << "and no point of it is in the armed table at all";

  // Two steps of the program the ungated twin of this seam edits, and
  // nothing was edited.
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(edit_hits, 0u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x1111);

  // Presented, and it arms — with no re-enable, because the gate is a
  // condition and not a toggle.
  EXPECT_EQ(r.pc().seams().present_document(claimed_document_digest()),
            &claimed_document);
  const seam_status after = r.pc().seams().status("test-gated");
  EXPECT_EQ(after.state, seam_state::on);
  EXPECT_TRUE(after.armed);
  EXPECT_EQ(after.reason, seam_reason::none);
}

TEST(SeamGate, ThePointsOfAGatedSeamAreNeverArmedWhileItIsShut) {
  // The fail-closed half, stated separately because it is the half that
  // matters: an unsatisfied gate does not arm a point and then decline
  // at it. An address in the armed table is an address `dispatch()`
  // compares against, and the gate keeps it out of the table.
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-gated"), seam_reason::none);
  EXPECT_FALSE(r.pc().seams().armed());

  ASSERT_NE(r.pc().seams().present_document(claimed_document_digest()),
            nullptr);
  EXPECT_TRUE(r.pc().seams().armed());
  // The point is on the third instruction, and a handler runs at the
  // boundary *before* the instruction there — so the first step is the
  // MOV and the second is the arrival.
  r.pc().step();
  r.pc().step();
  EXPECT_EQ(edit_hits, 1u);
  EXPECT_EQ(r.regs()[cpu::reg16::ax], 0x2222);
}

TEST(SeamGate, OneDocumentDoesNotSatisfyAnothersGate) {
  const rig r;
  r.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(r.pc().seams().enable("test-gated"), seam_reason::none);
  ASSERT_EQ(r.pc().seams().enable("test-gated-journal"), seam_reason::none);
  ASSERT_NE(r.pc().seams().present_document(claimed_document_digest()),
            nullptr);

  EXPECT_TRUE(r.pc().seams().status("test-gated").armed);
  const seam_status journal = r.pc().seams().status("test-gated-journal");
  EXPECT_FALSE(journal.armed);
  EXPECT_EQ(journal.reason, seam_reason::document_not_presented)
      << "a code wheel is not a journal, and holding one is not holding the"
         " other";
}

TEST(SeamGate, AnUnrecognizedDocumentSatisfiesNothingAndIsNotRemembered) {
  // PLAN.md §9's path, as a mechanism: reported, never guessed. A gate
  // that armed on a document this build cannot name would be a gate that
  // armed on anything.
  const rig r;
  sha256_digest stranger;
  ASSERT_TRUE(parse_digest(
      "9999999999999999999999999999999999999999999999999999999999999999",
      stranger));

  EXPECT_EQ(r.pc().seams().present_document(stranger), nullptr);
  EXPECT_EQ(r.pc().seams().document_count(), 0u);
  ASSERT_EQ(r.pc().seams().enable("test-gated"), seam_reason::none);
  EXPECT_FALSE(r.pc().seams().status("test-gated").armed);
}

TEST(SeamGate, PresentingTheSameDocumentTwiceIsPresentingIt) {
  const rig r;
  EXPECT_NE(r.pc().seams().present_document(claimed_document_digest()),
            nullptr);
  EXPECT_NE(r.pc().seams().present_document(claimed_document_digest()),
            nullptr);
  EXPECT_EQ(r.pc().seams().document_count(), 1u);
  ASSERT_NE(r.pc().seams().document_at(0), nullptr);
  EXPECT_EQ(r.pc().seams().document_at(0)->name, claimed_document.name);
  EXPECT_EQ(r.pc().seams().document_at(1), nullptr);
}

TEST(SeamGate, AnUngatedSeamNeedsNothingAndSaysSo) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-edit"), seam_reason::none);
  EXPECT_TRUE(r.pc().seams().status("test-edit").armed)
      << "no document has been presented, and it does not need one";
  EXPECT_TRUE(r.pc().seams().holds_document(document_kind::none));
  EXPECT_FALSE(r.pc().seams().holds_document(document_kind::journal));
}

TEST(SeamGate, APresentedDocumentIsConfigurationAndSurvivesAReset) {
  // What the player holds is not something the machine arrived at. A
  // reset machine has no program; the player still has their code wheel,
  // and being asked to present it again because the game restarted would
  // be the machine confusing its own state for theirs.
  const rig r;
  ASSERT_NE(r.pc().seams().present_document(claimed_document_digest()),
            nullptr);
  r.pc().reset();
  EXPECT_EQ(r.pc().seams().document_count(), 1u);
  EXPECT_TRUE(r.pc().seams().holds_document(document_kind::code_wheel));
}

TEST(SeamGate, ADefinitionWithAGateNamesTheCurrentSchema) {
  // The version moved for this field (seam.h): a definition written
  // before schema 5 read as ungated would be a possession gate silently
  // not applied, which is the one failure a gate has.
  EXPECT_EQ(seam_schema_version, 5);
  for (const seam_definition& seam : all_seams()) {
    EXPECT_EQ(seam.schema, seam_schema_version) << seam.id;
  }
}

TEST(SeamGate, EveryDocumentKindHasAName) {
  EXPECT_STREQ(document_kind_name(document_kind::none), "no document");
  EXPECT_STREQ(document_kind_name(document_kind::code_wheel), "code wheel");
  EXPECT_STREQ(document_kind_name(document_kind::journal), "journal");
}

TEST(SeamGate, PresentingADocumentWithEverySeamOffChangesNothing) {
  // The fidelity invariant, for the gate (#171). A document is
  // configuration: it is not machine state, it is not in the
  // serialization, and a machine that has been shown one is — to the
  // byte and to the tick — the machine that has not.
  const rig plain;
  plain.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  for (int i = 0; i < 4; ++i) {
    plain.pc().step();
  }
  const cpu::registers plain_regs = plain.regs();
  const state_hashes plain_hash = hash_state(plain.pc());
  const ticks plain_time = plain.pc().time();

  const rig shown;
  ASSERT_NE(shown.pc().seams().present_document(claimed_document_digest()),
            nullptr);
  shown.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  for (int i = 0; i < 4; ++i) {
    shown.pc().step();
  }

  EXPECT_EQ(plain_regs, shown.regs());
  EXPECT_EQ(plain_time, shown.pc().time());
  EXPECT_EQ(plain_hash.whole, hash_state(shown.pc()).whole)
      << "the whole machine, and not only the page the program wrote";
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

TEST(SeamFidelity, ATriggeredSeamNobodyPulledLeavesTheRunIdentical) {
  // #161's half of #96's rule: a trigger that is *on* and never pulled
  // has to be the run the same program has with it off, byte for byte.
  // The point is reached — the address is compared, the arrival is
  // counted — and nothing about the machine moves.
  const rig plain;
  plain.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  for (int i = 0; i < 4; ++i) {
    plain.pc().step();
  }

  const rig armed;
  armed.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  ASSERT_EQ(armed.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_TRUE(armed.pc().seams().armed()) << "its point is armed all the same";
  for (int i = 0; i < 4; ++i) {
    armed.pc().step();
  }

  EXPECT_EQ(plain.regs(), armed.regs());
  const std::span<const std::uint8_t> a = plain.pc().memory().ram();
  const std::span<const std::uint8_t> b = armed.pc().memory().ram();
  const std::uint32_t base = cpu::physical_address(image_load_segment, 0);
  for (std::uint32_t i = 0; i < 0x400; ++i) {
    ASSERT_EQ(a[base + i], b[base + i]) << "byte " << i;
  }
  EXPECT_EQ(plain.pc().time(), armed.pc().time());
  EXPECT_GT(armed.pc().seams().status("test-trigger").reached, 0u)
      << "and it was reached, which is what makes the equality mean"
         " something";
}

TEST(SeamFidelity, APointWithNoAddressNobodyPulledLeavesTheRunIdentical) {
  // #163's half of #96's rule, and the one that had to be made rather
  // than inherited: a point offered at *every step boundary* is a
  // sentence about the hot path, and the only thing standing between it
  // and a run that differs is that the offer is behind the latch. On and
  // never pulled, the run has to be the run the same program has with
  // the seam off — byte for byte, tick for tick, and with the handler
  // never having been called at all.
  const rig plain;
  plain.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  for (int i = 0; i < 4; ++i) {
    plain.pc().step();
  }
  const cpu::registers plain_regs = plain.regs();
  const state_hashes plain_hash = hash_state(plain.pc());
  const ticks plain_time = plain.pc().time();

  const rig armed;
  armed.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xA3, 0x00, 0x02, 0xF4});
  ASSERT_EQ(armed.pc().seams().enable("test-pull"), seam_reason::none);
  ASSERT_TRUE(armed.pc().seams().armed());
  for (int i = 0; i < 4; ++i) {
    armed.pc().step();
  }

  EXPECT_EQ(plain_regs, armed.regs());
  EXPECT_EQ(plain_time, armed.pc().time());
  EXPECT_EQ(plain_hash.whole, hash_state(armed.pc()).whole)
      << "the whole machine, and not only the page the program wrote";
  EXPECT_EQ(edit_hits, 0u) << "the handler was never called";
  const seam_status row = armed.pc().seams().status("test-pull");
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(row.reached, 0u);
}

TEST(SeamFidelity, ALatchIsNotMachineState) {
  // Pulled but not yet served, the machine has to hash as the machine it
  // would have been. A latch is configuration (seam.h), and the
  // serialization has never carried a seam.
  const rig plain;
  plain.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(plain.pc().seams().enable("test-trigger"), seam_reason::none);

  const rig pulled;
  pulled.program_at(0, {0xB8, 0x11, 0x11, 0x90, 0xF4});
  ASSERT_EQ(pulled.pc().seams().enable("test-trigger"), seam_reason::none);
  ASSERT_EQ(pulled.pc().seams().pull("test-trigger", pulled.pc().time()),
            seam_reason::none);

  EXPECT_EQ(hash_state(plain.pc()).whole, hash_state(pulled.pc()).whole);
  EXPECT_TRUE(pulled.pc().seams().waiting("test-trigger"))
      << "and the latch really is set";
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

// --- A module the program moves (#131) ------------------------------------

TEST(SeamMovingOverlay, FollowsTheWordTheProgramKeepsAndNotTheRead) {
  // The failure this exists to make impossible: the manager reads a
  // module in, then shuffles it inside its own arena — no DOS call, no
  // event, nothing the tracker can see — and a point armed at the read's
  // landing goes on reporting `armed` while sitting on somebody else's
  // code.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-moving-overlay"), seam_reason::none);
  counting_host host;
  r.pc().seams().set_host(&host);

  r.load_overlay(0x3000);
  r.manager_says_module_at(0x3000);
  r.pc().memory().ram()[cpu::physical_address(0x3000, 2)] = 0xF4;
  r.pc().memory().ram()[cpu::physical_address(0x3400, 2)] = 0xF4;

  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 1u);
  EXPECT_EQ(last_module_base, cpu::physical_address(0x3000, 0));

  // Moved, with nothing to announce it.
  r.manager_says_module_at(0x3400);

  r.pc().processor().resume();
  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 1u) << "the landing is not the module any more";

  r.pc().processor().resume();
  r.regs()[cpu::sreg::cs] = 0x3400;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 2u) << "and where the program says it is, it is";
  EXPECT_EQ(last_module_base, cpu::physical_address(0x3400, 0));
}

TEST(SeamMovingOverlay, IsInertWhileTheProgramSaysTheModuleIsNotLoaded) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-moving-overlay"), seam_reason::none);
  counting_host host;
  r.pc().seams().set_host(&host);

  // A read the tracker records, and a program that says the module is
  // not there. The program wins: zero is not an address.
  r.load_overlay(0x3000);
  r.manager_says_module_at(0);
  EXPECT_FALSE(r.pc().seams().status("test-moving-overlay").armed);
  EXPECT_EQ(r.pc().seams().status("test-moving-overlay").reason,
            seam_reason::module_not_resident);

  r.pc().memory().ram()[cpu::physical_address(0x3000, 2)] = 0xF4;
  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 0u);
  EXPECT_EQ(r.pc().seams().status("test-moving-overlay").fired, 0u);
}

TEST(SeamMovingOverlay, ArmsWithoutAReadWhenTheProgramSaysItIsThere) {
  // A manager may answer a call from a copy it already holds, and then
  // there is no read at all. Nothing would call `rearm()`, so a point
  // that needed one would never come back.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("test-moving-overlay"), seam_reason::none);
  counting_host host;
  r.pc().seams().set_host(&host);
  ASSERT_FALSE(r.pc().seams().status("test-moving-overlay").armed);

  r.manager_says_module_at(0x3000);
  EXPECT_TRUE(r.pc().seams().status("test-moving-overlay").armed);

  r.pc().memory().ram()[cpu::physical_address(0x3000, 2)] = 0xF4;
  r.regs()[cpu::sreg::cs] = 0x3000;
  r.regs().ip = 2;
  r.pc().step();
  EXPECT_EQ(host_calls, 1u) << "no read ever happened, and the point fired";
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
