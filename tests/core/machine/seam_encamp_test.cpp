// SPDX-License-Identifier: AGPL-3.0-only
//
// The Encamp (F)ix (seam_encamp_fix.cpp, M5-E1 #172, M5-E1a #186),
// exercised through its mechanism and not through any program: the test
// lays down a camp — the mode byte, a command bar, the rest clock and a
// party roster, the way the facts say the program lays them down — puts
// the machine at each of the seam's three points in turn, and watches what
// the handlers do. The addresses only mean something against the real
// binary; the mechanism has a public test (docs/seams.md §8 step 4).
//
// The offsets below are restated rather than read out of the seam, for
// the reason `seam_cheats_test.cpp` gives: these are the *facts* the test
// lays memory out by, and a test that read them out of the seam would be
// agreeing with itself. **Every byte here is this file's own** (PLAN.md
// §6) — including the command bar, which is three words this file made up
// so that the splice can be watched without any of the program's text
// being anywhere near this tree.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
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
constexpr std::uint16_t data_camp_bar = 0x508;
constexpr std::uint16_t data_roster_head = 0x5D96;
constexpr std::uint16_t data_rest_minute_units = 0x6DC4;
constexpr std::uint16_t data_rest_hours = 0x6DC8;
constexpr std::uint16_t data_rest_days = 0x6DCA;

/// The camp loop's own frame, as offsets from BP.
constexpr std::uint16_t frame_prompt = 0x0B;
constexpr std::uint16_t frame_out_flag = 0x04;

/// Where in the module the three points are.
constexpr std::uint16_t point_before_input = 0x1F06;
constexpr std::uint16_t point_after_input = 0x1F24;
constexpr std::uint16_t point_rest_entry = 0x077A;

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

/// The magic the Fix spends, and where the program keeps what it needs.
constexpr std::uint8_t cure_light_wounds = 3;
constexpr std::uint16_t rec_spell_slots = 0x17;
constexpr std::uint8_t spell_pending = 0x80;
constexpr std::uint16_t rec_can_act = 0x10D;
constexpr std::uint16_t data_area_record = 0x49D2;
constexpr std::uint16_t area_refuses_casting = 0x01CA;
constexpr std::uint16_t data_current_member = 0x5D92;
constexpr std::uint16_t data_cast_anchor = 0x6DB5;
constexpr std::uint16_t pick_keystroke = (std::uint16_t{0x1F} << 8U) | 'S';

/// Where the test puts the area record the cast gate reads.
constexpr std::uint16_t area_segment = 0x4800;
constexpr std::uint16_t area_offset = 0x0040;

/// The letter the Fix answers to, and the key it posts, as INT 16h hands
/// them back.
constexpr std::uint8_t fix_letter = 'F';
constexpr std::uint16_t rest_keystroke = (std::uint16_t{0x13} << 8U) | 'R';

/// Where the test puts things. Records live in a segment of their own so
/// that a wrong segment register shows up as a wrong answer, and the
/// module lives in a third.
constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t overlay_segment = 0x6000;

/// The camp loop's BP while it is in its menu loop — any value whose frame
/// is inside the stack segment will do, and this one is far enough in that
/// both frame offsets are positive.
constexpr std::uint16_t frame_base = 0x0100;

/// Where the first record goes, and how far apart the test spaces them.
constexpr std::uint16_t first_record = 0x0100;
constexpr std::uint16_t record_stride = 0x0200;

/// **This file's own command bar**: three words of its own invention, in
/// the shape the program's bars have — words separated by spaces, one
/// capital each. The splice has no idea what a bar says and this is how
/// the test says so.
constexpr std::string_view plain_bar = "Alpha Beta Gamma";
constexpr std::string_view fixed_bar = "Alpha Beta Fix Gamma";

/// And the prompt the loop builds before it, of no interest except that
/// the seam blanks it.
constexpr std::string_view plain_prompt = "Camp";

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

  /// A Pascal string — length byte, then characters — where the program
  /// keeps one.
  void put_pascal(std::uint16_t segment, std::uint16_t offset,
                  std::string_view text) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(text.size()));
    for (std::size_t nth = 0; nth < text.size(); ++nth) {
      put_byte(segment, static_cast<std::uint16_t>(offset + 1 + nth),
               static_cast<std::uint8_t>(text[nth]));
    }
  }
  [[nodiscard]] std::string pascal_at(std::uint16_t segment,
                                      std::uint16_t offset) const {
    std::string out;
    const std::uint8_t length = byte_at(segment, offset);
    for (unsigned nth = 1; nth <= length; ++nth) {
      out.push_back(static_cast<char>(
          byte_at(segment, static_cast<std::uint16_t>(offset + nth))));
    }
    return out;
  }

  /// Write what the program's overlay manager writes: the segment the
  /// camp screen's module begins at right now, in the word the seam's
  /// facts name (overlay.h, `seam_module::load_segment_at`). Zero is "not
  /// loaded". It is the *only* thing the engine needs for a module that
  /// names such a word — the tracker's record of a read is the weaker
  /// qualifier and is not consulted (#131).
  void manager_says_module_at(std::uint16_t segment) const {
    const seam_module& module = seam("encamp-fix").points.front().module;
    ASSERT_TRUE(module.has_load_segment());
    put_word(image_load_segment,
             static_cast<std::uint16_t>(module.load_segment_at), segment);
  }

  /// The camp screen, as the program leaves it while its menu loop is
  /// running: the mode byte, the bar in the data segment, the prompt on
  /// the loop's own stack, and a clock the rest command has not been near.
  void camp() const {
    manager_says_module_at(overlay_segment);
    put_byte(data_segment, data_game_mode, mode_camp);
    put_pascal(data_segment, data_camp_bar, plain_bar);
    put_pascal(stack_segment,
               static_cast<std::uint16_t>(frame_base - frame_prompt),
               plain_prompt);
    put_byte(stack_segment,
             static_cast<std::uint16_t>(frame_base - frame_out_flag), 0);
    put_word(data_segment, data_rest_hours, 3);
    put_word(data_segment, data_rest_minute_units, 7);
    put_word(data_segment, data_rest_days, 0);
    put_word(data_segment, data_cast_anchor, 0);
    put_word(data_segment, static_cast<std::uint16_t>(data_cast_anchor + 2), 0);
    area(true);
  }

  /// A party: one record per entry, linked in order and terminated, with
  /// the head word pointing at the first.
  struct member {
    std::uint8_t status{status_unhurt};
    std::uint8_t hit_points{};
    std::uint8_t most_hit_points{};
    /// Ready cures this member holds, and whether they may act at all.
    unsigned ready_cures{};
    /// Cures queued for memorization: held, but not castable.
    unsigned pending_cures{};
    bool can_act{true};
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
      put_byte(record_segment, static_cast<std::uint16_t>(at + rec_can_act),
               members[nth].can_act ? 1 : 0);
      unsigned slot = 0;
      for (unsigned nth_cure = 0; nth_cure < members[nth].ready_cures;
           ++nth_cure, ++slot) {
        put_byte(record_segment,
                 static_cast<std::uint16_t>(at + rec_spell_slots + slot),
                 cure_light_wounds);
      }
      for (unsigned nth_cure = 0; nth_cure < members[nth].pending_cures;
           ++nth_cure, ++slot) {
        put_byte(record_segment,
                 static_cast<std::uint16_t>(at + rec_spell_slots + slot),
                 static_cast<std::uint8_t>(cure_light_wounds | spell_pending));
      }
    }
  }

  /// The area record the cast gate reads, and whether it lets anyone
  /// cast. Laid down by `camp()`, so a test that says nothing gets an
  /// area that allows it.
  void area(bool allows) const {
    put_word(data_segment, data_area_record, area_offset);
    put_word(data_segment, static_cast<std::uint16_t>(data_area_record + 2),
             area_segment);
    put_word(area_segment,
             static_cast<std::uint16_t>(area_offset + area_refuses_casting),
             allows ? 0 : 1);
  }

  /// The first slot of `nth` member's record that is not zero.
  [[nodiscard]] std::uint8_t slot_at(unsigned nth, unsigned slot) const {
    return byte_at(record_segment, static_cast<std::uint16_t>(
                                       first_record + nth * record_stride +
                                       rec_spell_slots + slot));
  }
  [[nodiscard]] std::uint16_t member_offset(unsigned nth) const {
    return static_cast<std::uint16_t>(first_record + nth * record_stride);
  }

  /// A HLT at the module's `offset`, the processor pointed at it with the
  /// registers the camp loop has there: the data segment, its own stack
  /// frame, and — at the point where the menu-bar routine returns — the
  /// letter it answered with, in AL.
  void at_point(std::uint16_t offset, std::uint8_t al = 0) const {
    box->memory().ram()[cpu::physical_address(overlay_segment, offset)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = overlay_segment;
    r.ip = offset;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0080;
    r[cpu::reg16::bp] = frame_base;
    r.set(cpu::reg8::al, al);
  }

  /// One step at a point: the instruction is a HLT, so the step is the
  /// arrival and nothing else.
  void step_at(std::uint16_t offset, std::uint8_t al = 0) const {
    at_point(offset, al);
    box->step();
  }

  /// One whole pass of the camp menu loop: the bar goes out, the routine
  /// answers with `letter`, and the bar comes back.
  void one_menu_pass(std::uint8_t letter, std::uint8_t out_flag = 0) const {
    step_at(point_before_input);
    put_byte(stack_segment,
             static_cast<std::uint16_t>(frame_base - frame_out_flag), out_flag);
    step_at(point_after_input, letter);
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable("encamp-fix"), seam_reason::none);
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

  [[nodiscard]] std::string bar() const {
    return pascal_at(data_segment, data_camp_bar);
  }
  [[nodiscard]] std::string prompt() const {
    return pascal_at(stack_segment,
                     static_cast<std::uint16_t>(frame_base - frame_prompt));
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

TEST(SeamEncampFix, IsThreeAddressPointsInTheCampScreensModule) {
  const rig r;
  const seam_definition& fix = r.seam("encamp-fix");

  EXPECT_FALSE(fix.about.empty());
  EXPECT_FALSE(fix.trigger)
      << "the asking is a key on the game's own bar, not a host pull (#186)";
  ASSERT_EQ(fix.points.size(), 3u);
  for (const seam_point& point : fix.points) {
    EXPECT_FALSE(point.at_every_step)
        << "every point has an address now, so none has to guess";
    EXPECT_NE(point.offset, 0u);
    EXPECT_FALSE(point.module.is_resident_image());
    EXPECT_EQ(point.module.file, "GAME.OVR");
    EXPECT_FALSE(point.module.digest.empty())
        << "the overlay is identified by its bytes as well as its place";
    EXPECT_TRUE(point.module.has_load_segment())
        << "and by the program's own note of where it is now (#131)";
  }
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

TEST(SeamEncampFix, IsInertWhileTheCampScreensModuleIsNotLoaded) {
  const rig r;
  r.arm();
  r.camp();
  r.manager_says_module_at(0);

  // The manager's word reads zero, which is what it reads while the
  // module is out of memory. The points stay in the table — a module can
  // become resident without a read, so nothing would put them back
  // (#131) — and the word is what every step compares against.
  EXPECT_EQ(r.status().reason, seam_reason::module_not_resident);
  r.step_at(point_before_input);
  EXPECT_EQ(r.bar(), plain_bar);
  EXPECT_EQ(r.prompt(), plain_prompt);
}

// --- The command on the bar ------------------------------------------------

TEST(SeamEncampFix, PutsTheFixOnTheBarBeforeItsLastCommand) {
  const rig r;
  r.arm();
  r.camp();

  r.step_at(point_before_input);

  EXPECT_EQ(r.bar(), fixed_bar)
      << "the seam inserts four characters and knows nothing else about it";
  EXPECT_EQ(r.prompt(), "")
      << "the columns the longer bar needs come from the prompt";
}

TEST(SeamEncampFix, TakesItBackOffAgainAsSoonAsTheBarHasBeenDrawn) {
  const rig r;
  r.arm();
  r.camp();

  r.one_menu_pass('S');  // some other command of the program's own

  EXPECT_EQ(r.bar(), plain_bar)
      << "outside the one call that drew it, the string is the program's";
}

TEST(SeamEncampFix, PutsItOnAgainOnEveryPassAndOnlyOnce) {
  const rig r;
  r.arm();
  r.camp();

  r.one_menu_pass('V');
  r.step_at(point_before_input);

  EXPECT_EQ(r.bar(), fixed_bar);

  // And a second arrival at the same point without the routine having run
  // — which cannot happen in the program, and must not double the command
  // if it ever did.
  r.step_at(point_before_input);
  EXPECT_EQ(r.bar(), fixed_bar);
}

TEST(SeamEncampFix, RefusesABarWithNoRoomForTheCommand) {
  const rig r;
  r.arm();
  r.camp();
  // Thirty-eight characters in a slot that holds forty: four more do not
  // fit, and a bar that ran off the row would be worse than no command.
  r.put_pascal(data_segment, data_camp_bar, std::string(38, 'A'));

  r.step_at(point_before_input);

  EXPECT_EQ(r.bar(), std::string(38, 'A'));
  EXPECT_EQ(r.prompt(), plain_prompt) << "and the prompt is left alone too";
  EXPECT_NE(r.status().declined, 0u);
}

TEST(SeamEncampFix, RefusesABarThatIsNotOne) {
  const rig r;
  r.arm();
  r.camp();
  r.put_pascal(data_segment, data_camp_bar, "One");  // no separator

  r.step_at(point_before_input);

  EXPECT_EQ(r.bar(), "One");
  EXPECT_NE(r.status().declined, 0u);
}

TEST(SeamEncampFix, DrawsNothingWhereTheModeSaysThisIsNotCamp) {
  const rig r;
  r.arm();
  r.camp();
  r.put_byte(data_segment, data_game_mode, mode_adventure);

  r.step_at(point_before_input);

  EXPECT_EQ(r.bar(), plain_bar);
  EXPECT_NE(r.status().declined, 0u);
}

// --- What it does when the command is chosen -------------------------------

TEST(SeamEncampFix, DialsTheDaysTheWorstWoundedMemberNeedsAndPressesRest) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  r.one_menu_pass(fix_letter);

  // Eleven hit points down, plus the day of slack the heal tick's own
  // counter costs a second rest in one camp session.
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 12u);
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_EQ(r.first_key(), rest_keystroke);

  // And nothing else about the clock: the hours and minutes the program's
  // own rest command computes are the memorization time, and this seam is
  // not there yet.
  EXPECT_EQ(r.word_at(data_segment, data_rest_hours), 3u);
  EXPECT_EQ(r.word_at(data_segment, data_rest_minute_units), 7u);
  EXPECT_EQ(r.bar(), plain_bar);
  EXPECT_EQ(r.status().fired, 2u) << "the pass drew the bar and took the key";
}

TEST(SeamEncampFix, StartsTheRestTheProgramThenAsksAbout) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(4));

  r.one_menu_pass(fix_letter);
  ASSERT_EQ(r.keys_waiting(), 1u);
  // The program reads that key and dispatches its own Rest command. The
  // seam sees the entry with a duration already dialled, which is a thing
  // only it can have done, and presses the rest screen's own Rest.
  r.put_word(bda::segment, bda::keyboard_buffer_head,
             r.word_at(bda::segment, bda::keyboard_buffer_tail));
  r.step_at(point_rest_entry);

  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_EQ(r.first_key(), rest_keystroke);
}

TEST(SeamEncampFix, LeavesTheRecordsAloneEntirely) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  r.one_menu_pass(fix_letter);

  // The hit points are the program's to restore, a day at a time, through
  // its own applier. This seam only asks for the days.
  EXPECT_EQ(
      r.byte_at(record_segment, first_record + record_stride + rec_hit_points),
      19u);
  EXPECT_EQ(
      r.byte_at(record_segment, first_record + record_stride + rec_status),
      status_unhurt);
}

/// **A party with nothing wrong with it loses no time**, which is the
/// bug this seam shipped with: it slept a full day for a party that was
/// already whole, because the day was keeping a signature alive rather
/// than healing anybody. Nothing to heal and nothing to memorize means
/// nothing to do.
TEST(SeamEncampFix, DoesNothingAtAllWhenThereIsNothingToDo) {
  const rig r;
  r.arm();
  r.camp();
  r.party({{.status = status_unhurt, .hit_points = 12, .most_hit_points = 12},
           {.status = status_unhurt, .hit_points = 9, .most_hit_points = 9}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u) << "no days dialled";
  EXPECT_EQ(r.keys_waiting(), 0u) << "and no rest asked for";
  EXPECT_NE(r.status().declined, 0u) << "it says so rather than going quiet";
}

/// But a spell somebody is holding *pending* is a reason to rest even
/// when every hit point is where it should be — only time turns it into
/// one they can cast. The duration is then the program's own, which is
/// the rest the player's own Rest key would have given them.
TEST(SeamEncampFix, RestsForTheMemorizationWhenSpellsArePending) {
  const rig r;
  r.arm();
  r.camp();
  r.party({{.status = status_unhurt, .hit_points = 12, .most_hit_points = 12},
           {.status = status_unhurt,
            .hit_points = 9,
            .most_hit_points = 9,
            .pending_cures = 2}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u)
      << "no days of its own: the wrapper's memorization time is the rest";
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_EQ(r.first_key(), rest_keystroke);
}

TEST(SeamEncampFix, CountsAWoundedMemberWhoseStatusStillHeals) {
  for (const std::uint8_t status :
       {status_animated, status_unconscious, status_dying}) {
    const rig r;
    r.arm();
    r.camp();
    r.party({{.status = status_unhurt, .hit_points = 9, .most_hit_points = 9},
             {.status = status, .hit_points = 0, .most_hit_points = 20}});

    r.one_menu_pass(fix_letter);

    EXPECT_EQ(r.word_at(data_segment, data_rest_days), 21u)
        << "status " << static_cast<unsigned>(status);
  }
}

TEST(SeamEncampFix, DoesNotSizeTheRestByAMemberRestingCannotHelp) {
  for (const std::uint8_t status : {status_slain, status_petrified}) {
    const rig r;
    r.arm();
    r.camp();
    r.party({{.status = status_unhurt, .hit_points = 7, .most_hit_points = 9},
             {.status = status, .hit_points = 0, .most_hit_points = 40}});

    r.one_menu_pass(fix_letter);

    // The corpse's forty hit points are not a deficit any number of days
    // can close; the living member's two are.
    EXPECT_EQ(r.word_at(data_segment, data_rest_days), 3u)
        << "status " << static_cast<unsigned>(status);
  }
}

TEST(SeamEncampFix, StopsAtTheDaysFieldTheProgramItselfClampsTo) {
  const rig r;
  r.arm();
  r.camp();
  r.party({{.status = status_unhurt, .hit_points = 1, .most_hit_points = 255}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 99u);
}

// --- Spending the cures the party already holds (#189) ---------------------
//
// The rule of record: the Fix casts **only** cures a member already holds
// memorized, and queues one back for every one it spends, so the party
// ends holding what it started with. The queue-back happens *before* the
// cast, which is what makes that true at every instant rather than at the
// end — and is why none of this needs the handler to remember anything.

/// A party with somebody to heal and somebody who can heal them: two
/// wounded, and a caster holding `cures` ready ones.
[[nodiscard]] std::vector<rig::member> party_with_a_healer(unsigned cures) {
  return {{.status = status_unhurt, .hit_points = 15, .most_hit_points = 17},
          {.status = status_unhurt, .hit_points = 14, .most_hit_points = 18},
          {.status = status_unhurt,
           .hit_points = 30,
           .most_hit_points = 30,
           .ready_cures = cures}};
}

TEST(SeamEncampFix, SpendsAReadyCureOnTheWorstWoundedMember) {
  const rig r;
  r.arm();
  r.camp();
  r.party(party_with_a_healer(2));

  r.one_menu_pass(fix_letter);

  // The worst wounded is the second member, four down; the caster is the
  // third, the only one holding anything.
  EXPECT_EQ(r.word_at(data_segment, data_cast_anchor), r.member_offset(1))
      << "the picker opens on the member the anchor names";
  EXPECT_EQ(r.word_at(data_segment, data_current_member), r.member_offset(2))
      << "and the caster is the current member, as the cast screen sets it";
  EXPECT_EQ(r.keys_waiting(), 1u);
  EXPECT_EQ(r.first_key(), pick_keystroke)
      << "one key finishes a pick that is already positioned";
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u)
      << "no rest is asked for while there is still a cure to spend";
}

TEST(SeamEncampFix, QueuesTheCureBackBeforeItSpendsIt) {
  const rig r;
  r.arm();
  r.camp();
  r.party(party_with_a_healer(2));

  r.one_menu_pass(fix_letter);

  // Two ready in slots 0 and 1; the queued-back one goes in the first
  // empty slot, pending, so that ready-plus-pending never drops.
  EXPECT_EQ(r.slot_at(2, 0), cure_light_wounds);
  EXPECT_EQ(r.slot_at(2, 1), cure_light_wounds);
  EXPECT_EQ(r.slot_at(2, 2),
            static_cast<std::uint8_t>(cure_light_wounds | spell_pending))
      << "the replacement is queued before the original is spent";
}

TEST(SeamEncampFix, RestsRatherThanCastingWhatNobodyHoldsReady) {
  const rig r;
  r.arm();
  r.camp();
  // Cures queued for memorization are held and not castable.
  r.party({{.status = status_unhurt, .hit_points = 15, .most_hit_points = 17},
           {.status = status_unhurt,
            .hit_points = 30,
            .most_hit_points = 30,
            .pending_cures = 3}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 3u)
      << "two down plus the day of slack";
  EXPECT_EQ(r.first_key(), rest_keystroke);
  EXPECT_EQ(r.word_at(data_segment, data_cast_anchor), 0u);
}

TEST(SeamEncampFix, RestsRatherThanCastingWhereTheAreaRefusesIt) {
  const rig r;
  r.arm();
  r.camp();
  r.party(party_with_a_healer(2));
  r.area(false);

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 5u)
      << "four down plus the day of slack, and no spell asked for";
  EXPECT_EQ(r.first_key(), rest_keystroke);
}

TEST(SeamEncampFix, WillNotCastFromSomebodyWhoCannotAct) {
  const rig r;
  r.arm();
  r.camp();
  r.party({{.status = status_unhurt, .hit_points = 15, .most_hit_points = 17},
           {.status = status_unhurt,
            .hit_points = 30,
            .most_hit_points = 30,
            .ready_cures = 2,
            .can_act = false}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 3u);
  EXPECT_EQ(r.first_key(), rest_keystroke);
}

TEST(SeamEncampFix, SpendsNothingOnAMemberRestingCannotHelp) {
  const rig r;
  r.arm();
  r.camp();
  // The only wounded one is past healing; the cure stays in the book.
  r.party({{.status = status_slain, .hit_points = 0, .most_hit_points = 40},
           {.status = status_unhurt,
            .hit_points = 30,
            .most_hit_points = 30,
            .ready_cures = 2}});

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.keys_waiting(), 0u)
      << "nothing to cast at, and no rest can help them either";
  EXPECT_EQ(r.slot_at(1, 2), 0u) << "and nothing queued back";
}

/// **The player can stop it.** The Fix decides one act per arrival, and
/// every arrival stands aside if there is a key the program has not read
/// yet — so anything typed during the run ends it there, with whatever
/// healing has already happened kept and the camp menu in front of the
/// player. That is the interruption point; the rest itself is
/// interruptible by the program's own "stop resting" question.
TEST(SeamEncampFix, StopsWhenThePlayerTypesDuringTheRun) {
  const rig r;
  r.arm();
  r.camp();
  r.party(party_with_a_healer(2));
  ASSERT_TRUE(r.pc().inject_keystroke(0x01 << 8U | 0x1B));  // Esc

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.keys_waiting(), 1u) << "the player's key is the only one";
  EXPECT_EQ(r.first_key(), static_cast<std::uint16_t>(0x01 << 8U | 0x1B));
  EXPECT_EQ(r.word_at(data_segment, data_cast_anchor), 0u)
      << "nothing was set up";
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u)
      << "and no rest was asked for either";
}

// --- What it does when the command is not chosen ---------------------------

/// The shape every refusal test has: the pass happened, the bar came back,
/// and the seam asked for nothing.
void expect_no_rest_asked_for(const rig& r) {
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u) << "and no key was posted";
  EXPECT_EQ(r.bar(), plain_bar);
}

TEST(SeamEncampFix, SaysNothingAboutTheProgramsOwnCommands) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  r.one_menu_pass('R');  // the program's own Rest, chosen by hand

  expect_no_rest_asked_for(r);
}

TEST(SeamEncampFix, SaysNothingAboutAKeyThatWasNotChosenOffTheBar) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  // The same letter, but the routine is passing a raw key through rather
  // than answering with a command.
  r.one_menu_pass(fix_letter, /*out_flag=*/1);

  expect_no_rest_asked_for(r);
}

TEST(SeamEncampFix, StandsAsideWhenAKeyIsAlreadyWaiting) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));
  ASSERT_TRUE(r.pc().inject_keystroke(0x1F << 8U | 's'));

  r.one_menu_pass(fix_letter);

  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 1u) << "the key that was there is the only one";
  EXPECT_EQ(r.first_key(), static_cast<std::uint16_t>(0x1F << 8U | 's'))
      << "and it is the player's, not the seam's";
}

TEST(SeamEncampFix, AsksForNothingWhenTheRosterHeadIsNotAPointer) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));
  // Nothing lives in segment 0: the interrupt vector table and the BDA.
  r.put_word(data_segment, data_roster_head + 2, 0);

  r.one_menu_pass(fix_letter);

  expect_no_rest_asked_for(r);
}

TEST(SeamEncampFix, AsksForNothingWhenTheRosterDoesNotEnd) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));
  // A record whose next is itself: a walk that would not terminate is a
  // walk over something that is not a roster.
  r.put_word(record_segment, first_record + rec_next, first_record);
  r.put_word(record_segment, first_record + rec_next + 2, record_segment);

  r.one_menu_pass(fix_letter);

  expect_no_rest_asked_for(r);
}

TEST(SeamEncampFix, FollowsNoRosterPointerOutOfConventionalMemory) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));
  // A head that is a pointer and is not a record: the walk must stop at
  // it rather than read it, because a read through the bus above
  // conventional memory is the video window and loads the adapter's
  // latches (ega.h).
  r.put_word(data_segment, data_roster_head + 2, 0xC000);

  r.one_menu_pass(fix_letter);

  EXPECT_TRUE(r.log.notices.empty()) << "the walk read the video window";
  expect_no_rest_asked_for(r);
}

// --- The rest command's own entry ------------------------------------------

TEST(SeamEncampFix, LeavesARestThePlayerAskedForToThePlayer) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  // The days field is zero at the rest command's entry unless this seam
  // wrote it: camp entry zeroes it, the end of a rest zeroes it, and the
  // rest command's own set-up writes the fields below it and never it.
  r.step_at(point_rest_entry);

  EXPECT_EQ(r.keys_waiting(), 0u)
      << "the player pressed Rest, and the rest screen is theirs";
  EXPECT_NE(r.status().declined, 0u);
}

/// A duration on the clock is **not** what makes a rest this seam's.
/// Point 3 reads a word of the seam's own, so a player who dialled a
/// duration by hand and pressed Rest gets their own rest screen.
TEST(SeamEncampFix, DoesNotClaimARestJustBecauseTheClockIsDialled) {
  const rig r;
  r.arm();
  r.camp();
  r.put_word(data_segment, data_rest_days, 7);

  r.step_at(point_rest_entry);

  EXPECT_EQ(r.keys_waiting(), 0u);
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 7u)
      << "and the player's own duration is left alone";
}

/// And a claim this seam made does not outlive the command that made it.
/// It posts Rest expecting the menu to read it next; if a key the player
/// typed gets there first, the claim is dropped rather than spent on the
/// player's own next rest.
TEST(SeamEncampFix, DropsItsClaimOnARestWhenSomebodyElsesKeyArrives) {
  const rig r;
  r.arm();
  r.camp();
  r.party({{.status = status_unhurt, .hit_points = 15, .most_hit_points = 17}});

  r.one_menu_pass(fix_letter);  // claims a rest and posts Rest
  ASSERT_EQ(r.keys_waiting(), 1u);
  r.put_word(bda::segment, bda::keyboard_buffer_head,
             r.word_at(bda::segment, bda::keyboard_buffer_tail));

  // The menu reads something else instead — the claim goes with it.
  r.one_menu_pass('V');
  r.step_at(point_rest_entry);

  EXPECT_EQ(r.keys_waiting(), 0u)
      << "the rest the player then asks for is the player's";
}

// --- The fidelity invariant, for this seam ---------------------------------

TEST(SeamEncampFix, DoesNothingWhenItIsOff) {
  const rig r;
  r.camp();
  r.party(wounded_party(11));

  r.one_menu_pass(fix_letter);

  EXPECT_FALSE(r.pc().seams().armed());
  EXPECT_EQ(r.bar(), plain_bar);
  EXPECT_EQ(r.prompt(), plain_prompt);
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u);
}

/// #186's claim, which is the one the trigger's removal cost and the one
/// that replaces it: on, and the command never chosen, the difference this
/// seam makes to the machine is on the screen and nowhere else. Between
/// one menu draw and the next, the program's own memory is what it would
/// have been.
TEST(SeamEncampFix, LeavesNoTraceInMemoryWhenTheCommandIsNeverChosen) {
  const rig r;
  r.arm();
  r.camp();
  r.party(wounded_party(11));

  r.one_menu_pass('S');
  r.one_menu_pass('V');
  r.one_menu_pass('E');

  EXPECT_EQ(r.bar(), plain_bar);
  EXPECT_EQ(r.word_at(data_segment, data_rest_days), 0u);
  EXPECT_EQ(r.keys_waiting(), 0u);
}

TEST(SeamEncampFix, CannotBePulled) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.pc().seams().pull("encamp-fix", r.pc().time()),
            seam_reason::not_triggered)
      << "there is nothing to pull: the command is on the game's own bar";
}

}  // namespace
}  // namespace amberfolio::machine
