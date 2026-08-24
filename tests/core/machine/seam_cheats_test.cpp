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

  /// Turn the kill-all cheat on and ask it for one firing (#161). Both,
  /// because since the trigger neither is enough on its own: the flag
  /// says the seam may act and the pull says a person wanted it to.
  void arm_kill_all() const {
    ASSERT_EQ(box->seams().enable("cheat-kill-all"), seam_reason::none);
    ASSERT_EQ(box->seams().pull("cheat-kill-all", box->time()),
              seam_reason::none);
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
  /// in to `segment:0000`, with the digest the seam demands — claimed,
  /// the way the program digest is: the engine compares, it does not
  /// verify.
  void read_combat_overlay_to(std::uint16_t segment) const {
    const seam_module& module = seam("cheat-kill-all").points.front().module;
    sha256_digest digest;
    ASSERT_TRUE(parse_digest(module.digest, digest));
    const auto resolved =
        canonicalize(dos_path{}, std::span<const char>("\\GAME.OVR", 9));
    ASSERT_TRUE(resolved.ok());
    box->note_file_read(resolved.value, module.file_offset, segment, 0,
                        module.length, digest);
  }

  /// Write what the program's overlay manager writes: the segment that
  /// module begins at right now, in the word the seam's facts name
  /// (overlay.h, `seam_module::load_segment_at`). Zero is "not loaded",
  /// which is what a reset machine's memory already says.
  ///
  /// This, and not the read above, is where the engine gets the address
  /// from — so a test that moves the module moves this (#131).
  void manager_says_overlay_at(std::uint16_t segment) const {
    const seam_module& module = seam("cheat-kill-all").points.front().module;
    ASSERT_TRUE(module.has_load_segment());
    put_word(image_load_segment,
             static_cast<std::uint16_t>(module.load_segment_at), segment);
  }

  /// The ordinary case: the manager reads the module in and says where
  /// it put it.
  void load_combat_overlay() const {
    read_combat_overlay_to(overlay_segment);
    manager_says_overlay_at(overlay_segment);
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
  EXPECT_TRUE(invulnerable.points.front().module.is_resident_image());

  // Two points since #163, and which is which matters: the first has no
  // address and is offered at every step while a pull is outstanding,
  // the second is the end check exactly as it was. Both name the same
  // module, because address-free is not qualifier-free — the immediate
  // point is offered only while the program's own record says the
  // tactical combat module is in memory.
  ASSERT_EQ(kill_all.points.size(), 2u);
  EXPECT_TRUE(kill_all.points.front().at_every_step);
  EXPECT_EQ(kill_all.points.front().offset, 0u)
      << "a point with no address carries no offset either";
  EXPECT_FALSE(kill_all.points.back().at_every_step);
  EXPECT_NE(kill_all.points.back().offset, 0u);
  EXPECT_TRUE(kill_all.trigger);

  for (const seam_point& point : kill_all.points) {
    EXPECT_FALSE(point.module.is_resident_image());
    EXPECT_EQ(point.module.file, "GAME.OVR");
    EXPECT_FALSE(point.module.digest.empty())
        << "the overlay is identified by its bytes as well as its place";
    EXPECT_TRUE(point.module.has_load_segment())
        << "and by the word the program keeps its whereabouts in (#131)";
  }

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

/// Every `inert` event the sink was told about that is a **handler**
/// saying it declined, with its reason.
///
/// `inert` carries two different sentences: a seam whose module is not
/// resident, which the engine says on a transition and which is not a
/// decline at all, and a handler that arrived and would not act, which
/// is. Only the second is what these tests are counting.
[[nodiscard]] std::vector<seam_reason> declines(const rig& r,
                                                std::string_view id) {
  std::vector<seam_reason> found;
  for (const seam_event& event : r.log.seam_events) {
    if (event.id == id && event.kind == seam_event_kind::inert &&
        event.reason != seam_reason::module_not_resident) {
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

  // And back again the moment the program says the module has gone, with
  // no read to announce it — which is the half of this a table of reads
  // cannot answer.
  r.manager_says_overlay_at(0);
  EXPECT_FALSE(r.pc().seams().status("cheat-kill-all").armed);
}

TEST(SeamCheatKillAll, DownsEveryStandingEnemyAndLeavesThePartyAlone) {
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

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

TEST(SeamCheatKillAll, IsOnAndDoesNothingUntilSomebodyPullsIt) {
  // #161. Before the trigger this seam fired at every end check, so
  // switching it on once decided every subsequent fight. Now the flag
  // says it *may* act and the pull says a person wanted it to, and the
  // point is arrived at and left alone in between.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  EXPECT_TRUE(r.pc().seams().status("cheat-kill-all").trigger);
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u)
      << "the enemy is still standing; nobody asked";
  const seam_status row = r.pc().seams().status("cheat-kill-all");
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(row.reached, 1u)
      << "and the end check was reached, which is the number a latency"
         " would be measured from";
  EXPECT_FALSE(row.waiting);
}

TEST(SeamCheatKillAll, OnePullIsOneFiring) {
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 2);
  r.record(0x0100, 1, true, 9, 0, 0);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 0u) << "downed";
  EXPECT_FALSE(r.pc().seams().waiting("cheat-kill-all"));

  // A second combat, with nobody asking: the next fight is the player's
  // own again, which is the half of the complaint this closes.
  r.put_byte(record_segment, 0x0100 + rec_status, 0);
  r.put_byte(record_segment, 0x0100 + rec_held, 1);
  r.put_byte(record_segment, 0x0100 + rec_hp, 9);
  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 1u);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").reached, 2u);
}

TEST(SeamCheatKillAll, ReadsTheStatusAndNotTheHeldByteNextToIt) {
  // The record carries a *held* byte one before the combat-side index,
  // and the routine that downs a combatant clears it — which makes it
  // read like a liveness flag. It is not one. A combatant already slain
  // can still have it set, and downing decrements its side's body count,
  // so a seam that tested the held byte would decrement twice and leave
  // the program to end the combat on a count that had gone past zero.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

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
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

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

TEST(SeamCheatKillAll, TheEndCheckPointFiresOnlyWhereTheProgramSaysItIs) {
  // The *end check* point, isolated. Since #163 this seam has a second
  // point with no address, which would act here on any roster it
  // recognizes — so the roster below is deliberately one it does not:
  // there is no party member on it, which is condition 4 of
  // `combat_roster_ready()`. What is left is the address point, and the
  // claim is the one it has always made.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0);

  // The same offset in another segment: not the module.
  r.halt_at(0x7000, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u);
  EXPECT_TRUE(r.pc().seams().waiting("cheat-kill-all"))
      << "and neither point served the pull";

  // Where the module is, it fires — which is what makes the line above a
  // claim about the address rather than about the roster.
  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 0u);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 1u);
}

TEST(SeamCheatKillAll, FollowsTheModuleWhenTheManagerMovesIt) {
  // #131, as a test. The overlay manager owns an arena and may shuffle a
  // module inside it — or answer a call from a copy it already holds —
  // without going near DOS, so the address the read landed at is not the
  // address the routine runs from. On the real program the module this
  // seam lives in was read to one segment and ran, nineteen frames
  // later, from the next one up.
  //
  // The seam has to follow the module, and it has to stop firing at the
  // place the module *was*: a handler that ran at the landing after the
  // move would be running on whatever the manager put there instead.
  const rig r;
  r.arm_kill_all();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);
  constexpr std::uint16_t moved_to = overlay_segment + 1;

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 2);
  r.record(0x0100, 1, true, 9, 0, 0x0300);
  r.record(0x0300, 1, true, 9, 0, 0);

  // Read to one segment; moved to the next, with no read to say so.
  r.read_combat_overlay_to(overlay_segment);
  r.manager_says_overlay_at(moved_to);

  // Where it landed is now somebody else's code.
  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u)
      << "nothing fires at the address the read landed at";
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 0u);

  // Where the program says it is, is where the point is.
  r.halt_at(moved_to, entry, data_segment, data_segment, 0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 0u) << "downed";
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 0u);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 1u);
}

TEST(SeamCheatKillAll, DoesNotFireWhileTheProgramSaysTheModuleIsGone) {
  // The manager may drop a module without anything reading over it, and
  // then the word it keeps reads zero. A point armed against a *read*
  // would still be sitting on the address that read landed at, firing on
  // whatever is there now; this one asks the program at the step it is
  // asked to fire, and declines to be anywhere.
  const rig r;
  r.arm_kill_all();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 1, 1);
  r.record(0x0100, 1, true, 9, 0, 0);

  r.read_combat_overlay_to(overlay_segment);
  r.manager_says_overlay_at(0);

  r.halt_at(overlay_segment, entry, data_segment, data_segment, 0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 9u);
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 0u);
}

// --- The point with no address (#163) -------------------------------------
//
// "The kill all should not trigger at the end of the round but it should
// be on a new hotkey/button and trigger immediately." #161 answered the
// first half. These are the second: a point with no address at all,
// offered at every step boundary while the pull is outstanding, which
// asks the machine whether acting is safe and declines — keeping the
// pull — until it is.

/// A combat the seam should be willing to act on: the mode byte, a
/// roster head, a standing party member, two standing enemies and body
/// counts that agree with all three.
void lay_down_a_fight(const rig& r) {
  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 0, 1);
  r.put_byte(data_segment, data_side_counts + 1, 2);
  r.record(0x0100, 0, true, 20, 0, 0x0300);
  r.record(0x0300, 1, true, 9, 0x0040, 0x0500);
  r.record(0x0500, 1, true, 11, 0, 0);
}

/// Somewhere the program might be that is not either of this seam's
/// points: not the end check, not the module.
constexpr std::uint16_t elsewhere_segment = 0x7000;
constexpr std::uint16_t elsewhere_offset = 0x0100;

TEST(SeamCheatKillAll, ServesThePullWhereverTheProgramIs) {
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  lay_down_a_fight(r);

  // Not at the end check, not in the module, not at any address this
  // seam's facts name. One step is all it takes.
  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 0u);
  EXPECT_FALSE(r.pc().seams().waiting("cheat-kill-all"));
  const seam_status row = r.pc().seams().status("cheat-kill-all");
  EXPECT_EQ(row.fired, 1u);
  EXPECT_EQ(row.reached, 0u)
      << "the end check was never arrived at, and the point that served"
         " this has no address to arrive at";
  // And the party is the party.
  EXPECT_EQ(r.byte_at(record_segment, 0x0100 + rec_hp), 20u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 0), 1u);
}

TEST(SeamCheatKillAll, DealsDamageAndLeavesAnEnemyItCannotFinishStanding) {
  // The change from #163: damage, not a written corpse. A combatant with
  // more hit points than the debug value survives with fewer of them and
  // is left entirely alone otherwise — no status, no held byte, no
  // decrement, no scratch write — because that is what the program's own
  // damage routine does with somebody who is still up, and because a
  // count kept by hand only moves when a body drops.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();

  r.put_byte(data_segment, data_mode, 5);
  r.put_word(data_segment, data_roster_head, 0x0100);
  r.put_word(data_segment, data_roster_head + 2, record_segment);
  r.put_byte(data_segment, data_side_counts + 0, 1);
  r.put_byte(data_segment, data_side_counts + 1, 2);
  r.record(0x0100, 0, true, 20, 0, 0x0300);
  r.record(0x0300, 1, true, 200, 0x0040, 0x0500);  // survives 120
  r.record(0x0500, 1, true, 9, 0x0080, 0);         // does not
  r.put_byte(scratch_segment, 0x0040 + 3, 0x55);
  r.put_byte(scratch_segment, 0x0080 + 3, 0x55);

  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 80u)
      << "200 less 120, and it never wraps";
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_status), 0u)
      << "still unhurt as far as the program's own test goes: still up";
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_held), 1u)
      << "still in its side's count";
  EXPECT_EQ(r.byte_at(scratch_segment, 0x0040 + 3), 0x55)
      << "and nothing was written into its scratch block";

  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_status), 6u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_held), 0u);
  EXPECT_EQ(r.byte_at(scratch_segment, 0x0080 + 3), 0u);

  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 1u)
      << "one body dropped, so the count moves by one — not by two, and"
         " not at all for the survivor";
}

TEST(SeamCheatKillAll, SaturatesRatherThanWrappingAtZero) {
  // The one arithmetic mistake this could make, asked directly: a
  // combatant on fewer hit points than the damage must reach zero and
  // not the other end of a byte.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  lay_down_a_fight(r);
  r.put_byte(record_segment, 0x0300 + rec_hp, 1);
  r.put_byte(record_segment, 0x0500 + rec_hp, 0);

  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();

  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_hp), 0u);
}

TEST(SeamCheatKillAll, KeepsThePullUntilThereIsAFightItRecognizes) {
  // The guard is what the address-free point has instead of an address,
  // and a pull that arrives while it does not hold is not a pull that
  // was served. Out of combat: nothing happens and the person's request
  // is still outstanding. In combat: it is served.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  lay_down_a_fight(r);
  r.put_byte(data_segment, data_mode, 4);  // not combat

  for (int i = 0; i < 3; ++i) {
    r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
              0x0400);
    r.pc().step();
  }
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 9u);
  EXPECT_TRUE(r.pc().seams().waiting("cheat-kill-all"));
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 0u)
      << "a decline is not a firing";
  EXPECT_EQ(declines(r, "cheat-kill-all").size(), 1u)
      << "and three steps' worth of declining is one line, not three";

  r.put_byte(data_segment, data_mode, 5);
  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
  EXPECT_FALSE(r.pc().seams().waiting("cheat-kill-all"));
}

TEST(SeamCheatKillAll, DeclinesARosterThatIsNotAList) {
  // Three of the guard's five conditions, one case each, and all of them
  // end the same way: nothing is written and the pull is kept.
  const auto nothing_happened = [](const rig& r) {
    r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
              0x0400);
    r.pc().step();
    EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 9u);
    EXPECT_TRUE(r.pc().seams().waiting("cheat-kill-all"));
  };

  {
    // A head in segment 0, which is the interrupt vector table.
    const rig r;
    r.arm_kill_all();
    r.load_combat_overlay();
    lay_down_a_fight(r);
    r.put_word(data_segment, data_roster_head + 2, 0);
    nothing_happened(r);
  }
  {
    // A list that never ends: the last record points back at the first.
    const rig r;
    r.arm_kill_all();
    r.load_combat_overlay();
    lay_down_a_fight(r);
    r.put_word(record_segment, 0x0500 + rec_next, 0x0100);
    r.put_word(record_segment, 0x0500 + rec_next + 2, record_segment);
    nothing_happened(r);
  }
  {
    // The two structures disagree: a body is up and its side's count
    // says nobody is, which is what being part-way through something
    // looks like from outside the program.
    const rig r;
    r.arm_kill_all();
    r.load_combat_overlay();
    lay_down_a_fight(r);
    r.put_byte(data_segment, data_side_counts + 1, 0);
    nothing_happened(r);
  }
}

TEST(SeamCheatKillAll, DeclinesARosterWithNobodyLeftToFight) {
  // Nothing to do is not "done": a pull served by damaging nobody would
  // answer a person's request with nothing at all.
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  lay_down_a_fight(r);
  r.record(0x0300, 1, false, 0, 0, 0x0500);
  r.record(0x0500, 1, false, 0, 0, 0);

  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();
  EXPECT_TRUE(r.pc().seams().waiting("cheat-kill-all"));
  EXPECT_EQ(r.pc().seams().status("cheat-kill-all").fired, 0u);
}

TEST(SeamCheatKillAll, IsNotConsultedAtAllUntilSomebodyPullsIt) {
  // #96's rule where #163 could most easily have broken it: a point
  // offered at every step boundary is a sentence about the hot path, and
  // the only thing between it and a run that differs is the latch. On,
  // armed, module resident, a fight laid out in front of it, and nobody
  // has asked — so nothing moves, however many steps go by.
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("cheat-kill-all"), seam_reason::none);
  r.load_combat_overlay();
  lay_down_a_fight(r);
  ASSERT_TRUE(r.pc().seams().status("cheat-kill-all").armed);

  for (int i = 0; i < 8; ++i) {
    r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
              0x0400);
    r.pc().step();
  }

  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 9u);
  EXPECT_EQ(r.byte_at(record_segment, 0x0500 + rec_hp), 11u);
  EXPECT_EQ(r.byte_at(data_segment, data_side_counts + 1), 2u);
  const seam_status row = r.pc().seams().status("cheat-kill-all");
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(row.reached, 0u);
  EXPECT_TRUE(declines(r, "cheat-kill-all").empty())
      << "not even a decline: the handler was never called";
}

TEST(SeamCheatKillAll, IsNotOfferedWhileTheModuleIsGoneEitherWayIn) {
  // Address-free is not qualifier-free. The immediate point names the
  // same module the end check does, so "the tactical combat code is in
  // memory" is a precondition of it too — asked of the program's own
  // record, at the step it is asked.
  const rig r;
  r.arm_kill_all();
  r.read_combat_overlay_to(overlay_segment);
  r.manager_says_overlay_at(0);
  lay_down_a_fight(r);

  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 9u);
  EXPECT_TRUE(r.pc().seams().waiting("cheat-kill-all"));

  r.manager_says_overlay_at(overlay_segment);
  r.halt_at(elsewhere_segment, elsewhere_offset, data_segment, data_segment,
            0x0400);
  r.pc().step();
  EXPECT_EQ(r.byte_at(record_segment, 0x0300 + rec_hp), 0u);
}

TEST(SeamCheatKillAll, BoundsTheRosterWalk) {
  const rig r;
  r.arm_kill_all();
  r.load_combat_overlay();
  const auto entry =
      static_cast<std::uint16_t>(r.seam("cheat-kill-all").points.back().offset);

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
