// SPDX-License-Identifier: AGPL-3.0-only
//
// The Encamp (F)ix (seam_encamp_fix.cpp, M5-E1 #172), exercised through
// its mechanism and not through any program: the test lays down a camp —
// the mode byte, the rest screen's own flag, the rest clock and a party
// roster, the way the facts say the program lays them down — offers the
// machine a step with the pull outstanding, and watches what the handler
// does to them. The addresses only mean something against the real
// binary; the mechanism has a public test (docs/seams.md §8 step 4).
//
// The offsets below are restated rather than read out of the seam, for
// the reason `seam_cheats_test.cpp` gives: these are the *facts* the test
// lays memory out by, and a test that read them out of the seam would be
// agreeing with itself. Every byte here is this file's own (PLAN.md §6).

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
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

/// The data segment, as the seam's facts name it.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_camp = 2;
constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint16_t data_roster_head = 0x5D96;
constexpr std::uint16_t data_rest_minute_units = 0x6DC4;
constexpr std::uint16_t data_rest_hours = 0x6DC8;
constexpr std::uint16_t data_rest_days = 0x6DCA;
constexpr std::uint16_t data_memorize_countdown = 0x6DD0;
constexpr std::uint16_t data_rest_screen_up = 0x6DDA;

/// The character record.
constexpr std::uint16_t rec_max_hit_points = 0x32;
constexpr std::uint16_t rec_next = 0x104;
constexpr std::uint16_t rec_status = 0x10C;
constexpr std::uint16_t rec_hit_points = 0x11B;

/// Wound statuses, by the names the program's healing applier sorts them
/// into: the four it accepts and two it refuses.
constexpr std::uint8_t status_unhurt = 0;
constexpr std::uint8_t status_animated = 1;
constexpr std::uint8_t status_unconscious = 4;
constexpr std::uint8_t status_dying = 5;
constexpr std::uint8_t status_slain = 6;
constexpr std::uint8_t status_petrified = 7;

/// The key the seam posts, as INT 16h hands it back.
constexpr std::uint16_t rest_keystroke = (std::uint16_t{0x13} << 8U) | 'R';

/// Where the test puts things. Records live in a segment of their own so
/// that a wrong segment register shows up as a wrong answer.
constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t record_segment = 0x4000;

/// Where the first record goes, and how far apart the test spaces them.
constexpr std::uint16_t first_record = 0x0100;
constexpr std::uint16_t record_stride = 0x0200;

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
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

  /// The camp screen with the rest screen up and the rest not started:
  /// the mode byte, the program's own rest-screen flag, a clock the rest
  /// wrapper has filled with a memorization time and no days, and a
  /// countdown block the rest has just zeroed.
  void camp_with_rest_screen(unsigned members) const {
    put_byte(data_segment, data_game_mode, mode_camp);
    put_byte(data_segment, data_rest_screen_up, 1);
    put_word(data_segment, data_rest_hours, 3);
    put_word(data_segment, data_rest_minute_units, 7);
    put_word(data_segment, data_rest_days, 0);
    for (unsigned nth = 0; nth < members; ++nth) {
      put_byte(data_segment,
               static_cast<std::uint16_t>(data_memorize_countdown + nth), 0);
    }
  }

  /// A party: one record per entry, linked in order and terminated, with
  /// the head word pointing at the first.
  struct member {
    std::uint8_t status{status_unhurt};
    std::uint8_t hit_points{};
    std::uint8_t most_hit_points{};
  };

  void party(const std::vector<member>& members) const {
    put_word(data_segment, data_roster_head,
             members.empty() ? 0 : first_record);
    put_word(data_segment, static_cast<std::uint16_t>(data_roster_head + 2),
             members.empty() ? 0 : record_segment);
    for (std::size_t nth = 0; nth < members.size(); ++nth) {
      const auto at = static_cast<std::uint16_t>(
          first_record + nth * static_cast<std::size_t>(record_stride));
      const bool last = nth + 1 == members.size();
      const auto next = static_cast<std::uint16_t>(at + record_stride);
      put_byte(record_segment, static_cast<std::uint16_t>(at + rec_status),
               members[nth].status);
      put_byte(record_segment, static_cast<std::uint16_t>(at + rec_hit_points),
               members[nth].hit_points);
      put_byte(record_segment,
               static_cast<std::uint16_t>(at + rec_max_hit_points),
               members[nth].most_hit_points);
      put_word(record_segment, static_cast<std::uint16_t>(at + rec_next),
               last ? 0 : next);
      put_word(record_segment, static_cast<std::uint16_t>(at + rec_next + 2),
               last ? 0 : record_segment);
    }
  }

  /// A HLT at physical `cs:ip`, the processor pointed at it with DS as
  /// given — one step of a machine that is otherwise doing nothing, which
  /// is all a point with no address needs to be offered.
  void halt_with_data_segment() const {
    constexpr std::uint16_t cs = 0x2000;
    constexpr std::uint16_t ip = 0;
    box->memory().ram()[cpu::physical_address(cs, ip)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = cs;
    r.ip = ip;
    r[cpu::sreg::ds] = data_segment;
  }

  /// Turn the Fix on and ask it for one firing. Both, because since #161
  /// neither is enough on its own: the flag says the seam may act and the
  /// pull says a person wanted it to.
  void arm() const {
    ASSERT_EQ(box->seams().enable("encamp-fix"), seam_reason::none);
    ASSERT_EQ(box->seams().pull("encamp-fix", box->time()), seam_reason::none);
  }

  /// What the BIOS keystroke buffer holds, as a program would read it: the
  /// number of keystrokes waiting and the first of them.
  [[nodiscard]] unsigned keys_waiting() const {
    const std::uint16_t head = word_at(bda::segment, bda::keyboard_buffer_head);
    const std::uint16_t tail = word_at(bda::segment, bda::keyboard_buffer_tail);
    constexpr std::uint16_t span =
        bda::keyboard_buffer_end - bda::keyboard_buffer;
    return static_cast<unsigned>(((tail + span - head) % span) / 2U);
  }
  [[nodiscard]] std::uint16_t first_key() const {
    return word_at(bda::segment,
                   word_at(bda::segment, bda::keyboard_buffer_head));
  }

  /// One step with everything laid out: the point has no address, so a
  /// single step boundary is one offer.
  void offer_one_step() const {
    halt_with_data_segment();
    box->step();
  }

  [[nodiscard]] seam_status status() const {
    return box->seams().status("encamp-fix");
  }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
};

/// A party wounded by `deficit` on its worst member: three members, the
/// middle one hurt.
[[nodiscard]] std::vector<rig::member> wounded_party(std::uint8_t deficit) {
  return {{.status = status_unhurt, .hit_points = 12, .most_hit_points = 12},
          {.status = status_unhurt,
           .hit_points = static_cast<std::uint8_t>(30 - deficit),
           .most_hit_points = 30},
          {.status = status_unhurt, .hit_points = 8, .most_hit_points = 9}};
}

// --- The definition --------------------------------------------------------

TEST(SeamEncampFix, IsAPulledSeamWithOnePointAndNoAddress) {
  const rig r;
  const seam_definition& fix = r.seam("encamp-fix");

  EXPECT_FALSE(fix.about.empty());
  EXPECT_TRUE(fix.trigger) << "camping is not by itself a request to rest";
  ASSERT_EQ(fix.points.size(), 1u);
  EXPECT_TRUE(fix.points.front().at_every_step)
      << "it acts where the program is waiting, which has no address here";
  EXPECT_EQ(fix.points.front().offset, 0u)
      << "a point with no address carries no offset either";
  EXPECT_TRUE(fix.points.front().module.is_resident_image());
  EXPECT_EQ(fix.gate, document_kind::none);
  EXPECT_EQ(fix.schema, seam_schema_version);

  EXPECT_EQ(r.pc().seams().status("encamp-fix").state, seam_state::off);
}

TEST(SeamEncampFix, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);
  EXPECT_EQ(box->seams().status("encamp-fix").reason,
            seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable("encamp-fix"), seam_reason::wrong_binary);
}

// --- What it does when it is asked -----------------------------------------

TEST(SeamEncampFix, DialsTheDaysTheWorstWoundedMemberNeedsAndPressesRest) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));

  r.offer_one_step();

  // Eleven hit points down, plus the day of slack the heal tick's own
  // counter costs a second rest in one camp session.
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 12u);
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_EQ(r.first_key(), rest_keystroke);

  // And nothing else about the clock: the hours and minutes the program's
  // own rest wrapper computed are the memorization time, and they stand.
  EXPECT_EQ(r.word_at(data_segment, data_rest_hours), 3u);
  EXPECT_EQ(r.word_at(data_segment, data_rest_minute_units), 7u);

  const seam_status row = r.status();
  EXPECT_EQ(row.fired, 1u);
  EXPECT_FALSE(row.waiting) << "the pull was served";
  EXPECT_EQ(row.reached, 0u) << "a point with no address counts no arrivals";
  EXPECT_EQ(seam_reading_of(row), seam_reading::served);
}

TEST(SeamEncampFix, LeavesTheRecordsAloneEntirely) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));

  r.offer_one_step();

  // The hit points are the program's to restore, a day at a time, through
  // its own applier. This seam only asks for the days.
  EXPECT_EQ(
      r.byte_at(record_segment, first_record + record_stride + rec_hit_points),
      19u);
  EXPECT_EQ(
      r.byte_at(record_segment, first_record + record_stride + rec_status),
      status_unhurt);
}

TEST(SeamEncampFix, RestsForTheMemorizationTimeAloneWhenThePartyIsWhole) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(2);
  r.party({{.status = status_unhurt, .hit_points = 12, .most_hit_points = 12},
           {.status = status_unhurt, .hit_points = 9, .most_hit_points = 9}});

  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u)
      << "nothing to heal, so nothing is added to what the program dialled";
  EXPECT_EQ(r.keys_waiting(), 1u) << "and the rest is still asked for";
  EXPECT_EQ(r.status().fired, 1u);
}

TEST(SeamEncampFix, CountsAWoundedMemberWhoseStatusStillHeals) {
  for (const std::uint8_t status :
       {status_animated, status_unconscious, status_dying}) {
    const rig r;
    r.arm();
    r.camp_with_rest_screen(2);
    r.party({{.status = status_unhurt, .hit_points = 9, .most_hit_points = 9},
             {.status = status, .hit_points = 0, .most_hit_points = 20}});

    r.offer_one_step();

    EXPECT_EQ(r.word_at(data_segment, data_rest_days), 21u)
        << "status " << static_cast<unsigned>(status);
  }
}

TEST(SeamEncampFix, DoesNotSizeTheRestByAMemberRestingCannotHelp) {
  for (const std::uint8_t status : {status_slain, status_petrified}) {
    const rig r;
    r.arm();
    r.camp_with_rest_screen(2);
    r.party({{.status = status_unhurt, .hit_points = 7, .most_hit_points = 9},
             {.status = status, .hit_points = 0, .most_hit_points = 40}});

    r.offer_one_step();

    // The corpse's forty hit points are not a deficit any number of days
    // can close; the living member's two are.
    EXPECT_EQ(r.word_at(data_segment, data_rest_days), 3u)
        << "status " << static_cast<unsigned>(status);
  }
}

TEST(SeamEncampFix, StopsAtTheDaysFieldTheProgramItselfClampsTo) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(1);
  r.party({{.status = status_unhurt, .hit_points = 1, .most_hit_points = 255}});

  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 99u);
}

// --- What it does when it is not sure --------------------------------------

/// Every `inert` event the sink was told about that is a handler saying it
/// declined, with its reason — `seam_cheats_test.cpp`'s helper, for the
/// same reason: the engine says `inert` about a module too.
[[nodiscard]] std::vector<seam_reason> declines(const rig& r) {
  std::vector<seam_reason> found;
  for (const seam_event& event : r.log.seam_events) {
    if (event.id == "encamp-fix" && event.kind == seam_event_kind::inert &&
        event.reason == seam_reason::point_not_recognized) {
      found.push_back(event.reason);
    }
  }
  return found;
}

/// The shape every refusal test has: lay a camp down, break one thing
/// about it, and expect the seam to touch nothing and keep the pull.
void expect_declined(const rig& r) {
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u) << "and no key was posted";
  const seam_status row = r.status();
  EXPECT_EQ(row.fired, 0u);
  EXPECT_TRUE(row.waiting) << "a decline keeps the latch (#161)";
  EXPECT_NE(row.declined, 0u);
  EXPECT_EQ(seam_reading_of(row), seam_reading::pulled_and_declined);
  EXPECT_EQ(declines(r).size(), 1u) << "reported once per enable";
}

TEST(SeamEncampFix, WaitsWhileThePartyIsNotInCamp) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  r.put_byte(data_segment, data_game_mode, mode_adventure);

  r.offer_one_step();

  expect_declined(r);
}

TEST(SeamEncampFix, WaitsWhileTheRestScreenIsNotUp) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  r.put_byte(data_segment, data_rest_screen_up, 0);

  r.offer_one_step();

  expect_declined(r);
}

TEST(SeamEncampFix, StandsAsideWhenSomebodyElseDialledADuration) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  r.put_word(data_segment, data_rest_days, 2);

  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 2u)
      << "the player's own two days are the player's";
  EXPECT_EQ(r.keys_waiting(), 0u);
  EXPECT_TRUE(r.status().waiting);
}

TEST(SeamEncampFix, WaitsWhileTheRestIsAlreadyRunning) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  // One iteration of the rest has run: the countdown block the rest
  // zeroed has been decremented, which is what says so.
  r.put_byte(data_segment, data_memorize_countdown + 1, 0xFF);

  r.offer_one_step();

  expect_declined(r);
}

TEST(SeamEncampFix, WaitsWhileAKeyIsAlreadyWaiting) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  ASSERT_TRUE(r.pc().inject_keystroke(0x1F << 8U | 's'));

  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 1u) << "the key that was there is the only one";
  EXPECT_EQ(r.first_key(), static_cast<std::uint16_t>(0x1F << 8U | 's'))
      << "and it is the player's, not the seam's";
  EXPECT_TRUE(r.status().waiting);
}

TEST(SeamEncampFix, WaitsWhenTheRosterHeadIsNotAPointer) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  // Nothing lives in segment 0: the interrupt vector table and the BDA.
  r.put_word(data_segment, data_roster_head + 2, 0);

  r.offer_one_step();

  expect_declined(r);
}

TEST(SeamEncampFix, WaitsWhenTheRosterDoesNotEnd) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  // A record whose next is itself: a walk that would not terminate is a
  // walk over something that is not a roster.
  r.put_word(record_segment, first_record + rec_next, first_record);
  r.put_word(record_segment, first_record + rec_next + 2, record_segment);

  r.offer_one_step();

  expect_declined(r);
}

TEST(SeamEncampFix, ServesThePullAtTheFirstStepWhereTheCampIsReady) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(4));
  r.put_byte(data_segment, data_game_mode, mode_adventure);

  // Three steps outside camp: offered, and refused, every one of them.
  r.offer_one_step();
  r.offer_one_step();
  r.offer_one_step();
  EXPECT_TRUE(r.status().waiting);
  EXPECT_EQ(r.status().declined, 3u);

  // Then the party camps and asks for a rest.
  r.put_byte(data_segment, data_game_mode, mode_camp);
  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 5u);
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_FALSE(r.status().waiting);
  EXPECT_EQ(r.status().fired, 1u);
}

// --- Reading nothing it is not sure of (#172) ------------------------------
//
// The point has no address, so while a pull is outstanding the guard runs
// at every step boundary — with DS holding whatever the program happens
// to have loaded. Every read it makes is therefore a read at an address
// nobody chose, and a read through the bus is a bus cycle: above
// conventional memory it is the video window, where a read loads the
// adapter's latches.
//
// This was found on the real program, not here: the seam declined its way
// from the pull to the camp screen and left seven `unmapped_memory_read`
// notices behind it, which is the machine correctly reporting that
// something had touched memory nobody answers for. These two are that,
// written down.

TEST(SeamEncampFix, ReadsNothingOutsideConventionalMemory) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));

  // A data segment whose mode byte would land in the video window.
  r.halt_with_data_segment();
  r.pc().processor().regs()[cpu::sreg::ds] = 0xB000;
  r.pc().step();

  EXPECT_TRUE(r.log.notices.empty())
      << "the guard touched memory nobody answers for";
  EXPECT_TRUE(r.status().waiting);
  EXPECT_EQ(r.status().fired, 0u);
}

TEST(SeamEncampFix, FollowsNoRosterPointerOutOfConventionalMemory) {
  const rig r;
  r.arm();
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));
  // A head that is a pointer and is not a record: the walk must stop at
  // it rather than read it.
  r.put_word(data_segment, data_roster_head + 2, 0xC000);

  r.offer_one_step();

  EXPECT_TRUE(r.log.notices.empty()) << "the walk read the video window";
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u);
  EXPECT_TRUE(r.status().waiting);
}

// --- The fidelity invariant, for this seam ---------------------------------

TEST(SeamEncampFix, DoesNothingWhenItIsOff) {
  const rig r;
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));

  r.offer_one_step();

  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u);
}

TEST(SeamEncampFix, DoesNothingWhenItIsOnAndNobodyPulledIt) {
  const rig r;
  ASSERT_EQ(r.pc().seams().enable("encamp-fix"), seam_reason::none);
  r.camp_with_rest_screen(3);
  r.party(wounded_party(11));

  r.offer_one_step();

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u)
      << "a camp with the Fix on and unasked is a plain camp";
  EXPECT_EQ(r.keys_waiting(), 0u);
  const seam_status row = r.status();
  EXPECT_EQ(row.fired, 0u);
  EXPECT_EQ(row.declined, 0u) << "the guard is not even consulted";
  EXPECT_FALSE(row.waiting);
}

TEST(SeamEncampFix, CannotBePulledWhileItIsOff) {
  const rig r;
  EXPECT_EQ(r.pc().seams().pull("encamp-fix", r.pc().time()),
            seam_reason::not_enabled);
}

}  // namespace
}  // namespace amberfolio::machine
