// SPDX-License-Identifier: AGPL-3.0-only
//
// The debug cheats (seam_cheats.cpp, M4-F5 #99), exercised through their
// mechanism and not through any program: the test lays down a character
// record, a stack frame and a roster the way the facts say the program
// lays them down, points the machine at the interception point, and
// watches what the handler does to them. The addresses only mean
// something against the real binary; the mechanism has a public test.
//
// Everything here is read out of the seam definitions themselves — the
// offsets, the module, the digest — rather than restated, so this file
// cannot drift from the facts it is checking. Every byte here is this
// file's own (PLAN.md §6).

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

/// Record offsets and data-segment offsets, as seam_cheats.cpp names
/// them. Restated here on purpose — these are the *facts* the test lays
/// memory out by, and a test that read them out of the seam would be
/// agreeing with itself. The interception points and the module are read
/// from the definitions, because those are the mechanism.
constexpr std::uint16_t rec_next = 0x104;
constexpr std::uint16_t rec_scratch = 0x108;
constexpr std::uint16_t rec_status = 0x10C;
constexpr std::uint16_t rec_held = 0x10D;
constexpr std::uint16_t rec_side = 0x10E;
constexpr std::uint16_t rec_hp = 0x11B;
constexpr std::uint16_t data_mode = 0x49F3;
constexpr std::uint16_t data_roster_head = 0x5D96;
constexpr std::uint16_t data_side_counts = 0x6814;

/// Where the test puts things. DS is the "data segment" the handler reads
/// the roster head and the counts through; records and scratch blocks
/// live in segments of their own so a wrong segment register shows up as
/// a wrong answer.
constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t scratch_segment = 0x5000;
constexpr std::uint16_t overlay_segment = 0x6000;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }
  [[nodiscard]] const seam_definition& seam(std::string_view id) const {
    const seam_definition* found = box->seams().find(id);
    EXPECT_NE(found, nullptr) << id;
    return *found;
  }

  [[nodiscard]] std::uint8_t byte_at(std::uint16_t segment,
                                     std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }
  [[nodiscard]] std::uint16_t word_at(std::uint16_t segment,
                                      std::uint16_t offset) const {
    return static_cast<std::uint16_t>(
        byte_at(segment, offset) |
        (byte_at(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
  }
  void put_byte(std::uint16_t segment, std::uint16_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(segment, offset)] = value;
  }
  void put_word(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t value) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(value));
    put_byte(segment, static_cast<std::uint16_t>(offset + 1),
             static_cast<std::uint8_t>(value >> 8U));
  }

  /// A HLT at physical `at`, the processor pointed at it with DS, SS and
  /// SP as given.
  void halt_at(std::uint16_t cs, std::uint16_t ip, std::uint16_t ds,
               std::uint16_t ss, std::uint16_t sp) const {
    box->memory().ram()[cpu::physical_address(cs, ip)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = cs;
    r.ip = ip;
    r[cpu::sreg::ds] = ds;
    r[cpu::sreg::ss] = ss;
    r[cpu::reg16::sp] = sp;
  }

  /// A character record at `record_segment:offset`: its side, whether it
  /// is standing, its hit points, its scratch pointer and its next.
  ///
  /// "Standing" is the *status*, because that is what the program reads —
  /// unhurt when up, slain when not. The held byte is set alongside it the
  /// way the program leaves it, precisely so a test that confused the two
  /// would still pass here and fail on the machine.
  void record(std::uint16_t offset, std::uint8_t side, bool standing,
              std::uint8_t hp, std::uint16_t scratch_offset,
              std::uint16_t next_offset) const {
    put_byte(record_segment, static_cast<std::uint16_t>(offset + rec_side),
             side);
    put_byte(record_segment, static_cast<std::uint16_t>(offset + rec_held),
             standing ? 1 : 0);
    put_byte(record_segment, static_cast<std::uint16_t>(offset + rec_hp), hp);
    put_byte(record_segment, static_cast<std::uint16_t>(offset + rec_status),
             standing ? 0 : 6);
    put_word(record_segment, static_cast<std::uint16_t>(offset + rec_scratch),
             scratch_offset);
    put_word(record_segment,
             static_cast<std::uint16_t>(offset + rec_scratch + 2),
             scratch_offset == 0 ? 0 : scratch_segment);
    put_word(record_segment, static_cast<std::uint16_t>(offset + rec_next),
             next_offset);
    put_word(record_segment, static_cast<std::uint16_t>(offset + rec_next + 2),
             next_offset == 0 ? 0 : record_segment);
  }

  /// Pretend the overlay manager read the module the kill-all seam lives
  /// in to `overlay_segment:0000`, with the digest the seam demands —
  /// claimed, the way the program digest is: the engine compares, it does
  /// not verify.
  void load_combat_overlay() const {
    const seam_module& module = seam("cheat-kill-all").points.front().module;
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(module.digest, digest));
    const auto resolved =
        canonicalize(dos_path{}, std::span<const char>("\\GAME.OVR", 9));
    ASSERT_TRUE(resolved.ok());
    box->note_file_read(resolved.value, module.file_offset, overlay_segment, 0,
                        module.length, digest);
  }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
};

// --- The definitions -----------------------------------------------------

TEST(SeamCheats, AreTwoSeamsKeyedToTheBaselineAndQualifiedAsTheFactsSay) {
  const rig r;
  const seam_definition& invulnerable = r.seam("cheat-invulnerable");
  const seam_definition& kill_all = r.seam("cheat-kill-all");

  EXPECT_FALSE(invulnerable.about.empty());
  EXPECT_FALSE(kill_all.about.empty());
  ASSERT_EQ(invulnerable.points.size(), 1u);
  ASSERT_EQ(kill_all.points.size(), 1u);
  EXPECT_TRUE(invulnerable.points.front().module.is_resident_image());
  EXPECT_FALSE(kill_all.points.front().module.is_resident_image());
  EXPECT_EQ(kill_all.points.front().module.file, "GAME.OVR");
  EXPECT_FALSE(kill_all.points.front().module.digest.empty())
      << "the overlay is identified by its bytes as well as its place";

  // Both keyed to the baseline, and therefore available on this rig.
  EXPECT_EQ(r.pc().seams().status("cheat-invulnerable").state, seam_state::off);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").state, seam_state::off);
}

TEST(SeamCheats, AreUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(box->seams().status("cheat-invulnerable").reason,
            seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().status("cheat-kill-all").reason,
            seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable("cheat-kill-all"), seam_reason::wrong_binary);
}

// --- Invulnerability -----------------------------------------------------

TEST(SeamCheatInvulnerable, ZeroesTheDamageWordForAPartyMember) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-invulnerable"), seam_reason::none);
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);

  // The record: a party member with 20 hit points.
  r.record(0x0100, 0, true, 20, 0, 0);
  // The frame at SP, in the order the program actually pushes it
  // (seam_cheats.cpp): the far return address, the damage word, then the
  // record's offset and segment.
  constexpr std::uint16_t sp = 0x0400;
  r.put_word(data_segment, sp + 0, 0x1234);
  r.put_word(data_segment, sp + 2, 0x5678);
  r.put_word(data_segment, sp + 4, 7);
  r.put_word(data_segment, sp + 6, 0x0100);
  r.put_word(data_segment, sp + 8, record_segment);
  r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);

  r.pc().step();

  EXPECT_EQ(r.word_at(data_segment, sp + 4), 0u) << "no damage reaches them";
  EXPECT_EQ(r.word_at(data_segment, sp + 6), 0x0100u)
      << "the pointer is untouched";
  EXPECT_EQ(r.word_at(data_segment, sp + 8), record_segment)
      << "and so is its segment";
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 20u)
      << "the record is the program's to update, not the seam's";
}

TEST(SeamCheatInvulnerable, LeavesAnEnemysDamageAlone) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-invulnerable"), seam_reason::none);
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);

  r.record(0x0100, 1, true, 9, 0, 0);
  constexpr std::uint16_t sp = 0x0400;
  r.put_word(data_segment, sp + 4, 7);
  r.put_word(data_segment, sp + 6, 0x0100);
  r.put_word(data_segment, sp + 8, record_segment);
  r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);

  r.pc().step();

  EXPECT_EQ(r.word_at(data_segment, sp + 4), 7u);
}

TEST(SeamCheatInvulnerable, DoesNothingWhenOff) {
  const rig r;
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);
  r.record(0x0100, 0, true, 20, 0, 0);
  constexpr std::uint16_t sp = 0x0400;
  r.put_word(data_segment, sp + 4, 7);
  r.put_word(data_segment, sp + 6, 0x0100);
  r.put_word(data_segment, sp + 8, record_segment);
  r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);

  r.pc().step();

  EXPECT_EQ(r.word_at(data_segment, sp + 4), 7u);
  EXPECT_FALSE(r.pc().seams().armed());
}

// --- Kill-all-enemies ------------------------------------------------------

// --- The frame that is not a frame (#99, #103) --------------------------
//
// Driven against the real program (`docs/playable.md`), this point fires
// during an encounter with a stack that does not hold what the facts
// above describe: the record argument reads `0000:0004`, which is inside
// the interrupt vector table and cannot be a character record. A seam
// that wrote its zero anyway would put it into whatever is eight bytes up
// from SP, and the party's hit points would come out neither invulnerable
// nor untouched — the worst of the three answers. So it declines.

/// Every `inert` event the sink was told about, with its reason.
[[nodiscard]] std::vector<seam_reason> declines(const rig& r,
                                                std::string_view id) {
  std::vector<seam_reason> found;
  for (const seam_event& event : r.log.seam_events) {
    if (event.id == id && event.kind == seam_event_kind::inert) {
      found.push_back(event.reason);
    }
  }
  return found;
}

/// The frame the real program turned out to present: a record pointer
/// whose segment is zero.
void unrecognizable_frame(const rig& r, std::uint16_t sp) {
  r.put_word(data_segment, sp + 4, 7);       // the damage
  r.put_word(data_segment, sp + 6, 0x0004);  // and a "record" in segment 0
  r.put_word(data_segment, sp + 8, 0x0000);
}

TEST(SeamCheatInvulnerable, DeclinesAFrameWhoseRecordIsNotAPointer) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-invulnerable"), seam_reason::none);
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);

  constexpr std::uint16_t sp = 0x0400;
  unrecognizable_frame(r, sp);
  r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);

  r.pc().step();

  EXPECT_EQ(r.word_at(data_segment, sp + 4), 7u)
      << "nothing is written into a frame the seam does not recognize";
  EXPECT_EQ(declines(r, "cheat-invulnerable"),
            std::vector{seam_reason::point_not_recognized});
}

TEST(SeamCheatInvulnerable, SaysSoOnceHoweverOftenThePointFires) {
  // A point inside a combat round fires many times. One line says what
  // there is to say; a line per firing buries it.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-invulnerable"), seam_reason::none);
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);

  constexpr std::uint16_t sp = 0x0400;
  unrecognizable_frame(r, sp);
  for (int i = 0; i < 5; ++i) {
    r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);
    r.pc().step();
  }

  EXPECT_EQ(declines(r, "cheat-invulnerable").size(), 1u);
}

TEST(SeamCheatInvulnerable, AsksAgainAfterBeingTurnedOffAndOn) {
  const rig r;
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-invulnerable").points.front().offset);
  constexpr std::uint16_t sp = 0x0400;
  unrecognizable_frame(r, sp);

  for (int round = 0; round < 2; ++round) {
    ASSERT_EQ(r.pc().seams().enable("cheat-invulnerable"), seam_reason::none);
    r.halt_at(image_load_segment, entry, data_segment, data_segment, sp);
    r.pc().step();
    ASSERT_EQ(r.pc().seams().disable("cheat-invulnerable"), seam_reason::none);
  }

  EXPECT_EQ(declines(r, "cheat-invulnerable").size(), 2u);
}

TEST(SeamCheatKillAll, IsInertUntilTheCombatOverlayIsResident) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  const seam_status before = r.pc().seams().status("cheat-kill-all");
  EXPECT_EQ(before.state, seam_state::on);
  EXPECT_FALSE(before.armed);
  EXPECT_EQ(before.reason, seam_reason::module_not_resident);

  r.load_combat_overlay();
  const seam_status after = r.pc().seams().status("cheat-kill-all");
  EXPECT_TRUE(after.armed);
  EXPECT_EQ(after.reason, seam_reason::none);
}

TEST(SeamCheatKillAll, DownsEveryStandingEnemyAndLeavesThePartyAlone) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-kill-all").points.front().offset);

  // The data segment: combat mode, the roster head, one body each side up.
  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 0, 1);
  r.put_byte(data_segment, data_side_counts + 1, 2);

  // Three records: a party member, a standing enemy with a scratch block,
  // and an enemy already down.
  r.record(0x0100, 0, true, 20, 0, 0x0300);
  r.record(0x0300, 1, true, 9, 0x0040, 0x0500);
  r.record(0x0500, 1, false, 0, 0, 0);
  r.put_byte(scratch_segment, 0x0040 + 3, 0x55);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();

  // The standing enemy is down the way the program downs one.
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_status), 6u) << "slain";
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_held), 0u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(scratch_segment, 0x0040 + 3), 0u);
  // The enemies' count went from two to one — the one that was already
  // down was not counted twice.
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 1u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_status), 6u)
      << "an enemy already down is left as it was";
  // The party member is untouched, and so is their count.
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 20u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_held), 1u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 0), 1u);
}

TEST(SeamCheatKillAll, ReadsTheStatusAndNotTheHeldByteNextToIt) {
  // The record carries a *held* byte one before the combat-side index,
  // and the routine that downs a combatant clears it — which makes it
  // read like a liveness flag. It is not one. A combatant already slain
  // can still have it set, and downing decrements its side's body count,
  // so a seam that tested the held byte would decrement twice and leave
  // the program to end the combat on a count that had gone past zero.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-kill-all").points.front().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);

  // One enemy, slain already, but with the held byte still set.
  r.record(0x0100, 1, false, 0, 0, 0);
  r.put_byte(record_segment, 0x0100 + rec_held, 1);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 1u)
      << "the body count is decremented once per body, not once per pass";
}

TEST(SeamCheatKillAll, DoesNothingOutsideCombat) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-kill-all").points.front().offset);

  r.put_byte(data_segment, data_mode, 4);  // not combat
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 1u);
}

TEST(SeamCheatKillAll, FiresOnlyWhereTheOverlayLanded) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-kill-all").points.front().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0);

  // The same offset in another segment: not the module.
  r.halt_at(0x7000, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u);
}

TEST(SeamCheatKillAll, BoundsTheRosterWalk) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  const auto entry = static_cast<std::uint16_t>(
      r.seam("cheat-kill-all").points.front().offset);

  // A record whose next pointer is itself: a list that never ends.
  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0x0100);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();  // returns, rather than spinning

  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_status), 6u);
  EXPECT_TRUE(r.pc().processor().halted());
}

}  // namespace
}  // namespace amberfolio::machine
