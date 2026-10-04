// SPDX-License-Identifier: AGPL-3.0-only
//
// The select-yellow seam (seam_select_yellow.cpp, #453), exercised through
// its mechanism and not through any program: the test stands the processor
// on one of the seam's seven points with the frame the facts say is there
// laid out, steps once, and reads the one stack word or local the handler
// may have written.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The interception addresses *are*
// read from the definition, because those are the mechanism. Every byte
// here is this file's own (PLAN.md §6).

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "select-yellow";

// The colours, restated.
constexpr std::uint16_t black = 0x00;
constexpr std::uint16_t green = 0x0A;
constexpr std::uint16_t cyan = 0x0B;
constexpr std::uint16_t light_magenta = 0x0D;
constexpr std::uint16_t yellow = 0x0E;
constexpr std::uint16_t white = 0x0F;

// The points, in the order the definition lists them.
constexpr std::size_t bar_group = 0;
constexpr std::size_t list_row = 1;
constexpr std::size_t roster_name = 2;
constexpr std::size_t ability_score = 3;
constexpr std::size_t hit_points = 4;
constexpr std::size_t bar_letter = 5;
constexpr std::size_t bar_rest = 6;

// The overlays' words and where the test says they are now.
constexpr std::uint32_t word_menu_bar = 0x3C60;
constexpr std::uint32_t word_sheet = 0x0860;
constexpr std::uint16_t menu_bar_segment = 0x6000;
constexpr std::uint16_t sheet_segment = 0x7000;

// The test's own frames.
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_bp = 0x0800;
constexpr std::uint16_t frame_sp = 0x0600;
constexpr std::uint16_t caller_bp = 0x0A00;
constexpr std::uint16_t list_context = 0x0C00;
constexpr std::uint16_t record_segment = 0x3000;
constexpr std::uint16_t record_offset = 0x0100;

// What the facts say the frames look like.
constexpr std::uint16_t leaf_caller_bp = 6;     // [bp + 6]
constexpr std::uint16_t leaf_group = 8;         // [bp + 8], a byte
constexpr std::uint16_t leaf_index = 1;         // [bp - 1], a byte
constexpr std::uint16_t caller_colour_hi = 14;  // [caller + 0x0E]
constexpr std::uint16_t caller_colour_lo = 16;  // [caller + 0x10]
constexpr std::uint16_t caller_bar = 0x53;      // the bar, a Pascal string
constexpr std::uint16_t caller_bar_length = 0x64;
constexpr std::uint16_t caller_groups = 0x8F;   // pairs of positions
constexpr std::uint16_t list_colour_hi = 0x20;  // [context + 0x20]

/// A byte argument as the program pushes it: a whole register, `mov al, x`
/// and `push ax`, with whatever the register's high half held. The routines
/// read the low byte only, so the seam has to as well.
constexpr std::uint16_t as_pushed(unsigned value) {
  return static_cast<std::uint16_t>(0xA500U | value);
}

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
  }

  [[nodiscard]] const seam_definition& seam() const {
    const seam_definition* found = box->seams().find(seam_id);
    EXPECT_NE(found, nullptr);
    return *found;
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    manager_says(word_menu_bar, menu_bar_segment);
    manager_says(word_sheet, sheet_segment);
  }

  void manager_says(std::uint32_t word, std::uint16_t segment) const {
    put_word(image_load_segment, static_cast<std::uint16_t>(word), segment);
  }

  void put_byte(std::uint16_t segment, std::uint32_t offset,
                std::uint8_t value) const {
    box->memory().ram()[cpu::physical_address(
        segment, static_cast<std::uint16_t>(offset))] = value;
  }

  void put_word(std::uint16_t segment, std::uint32_t offset,
                std::uint16_t value) const {
    put_byte(segment, offset, static_cast<std::uint8_t>(value));
    put_byte(segment, offset + 1U, static_cast<std::uint8_t>(value >> 8U));
  }

  [[nodiscard]] std::uint8_t byte(std::uint16_t segment,
                                  std::uint32_t offset) const {
    return box->memory().ram()[cpu::physical_address(
        segment, static_cast<std::uint16_t>(offset))];
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint32_t offset) const {
    return static_cast<std::uint16_t>(byte(segment, offset) |
                                      (byte(segment, offset + 1U) << 8U));
  }

  /// Stand on point `which` and step once. The instruction there is a NOP.
  void arrive(std::size_t which) const {
    const seam_point& p = seam().points[which];
    const std::uint16_t segment =
        p.module.is_resident_image()
            ? image_load_segment
            : word(image_load_segment,
                   static_cast<std::uint16_t>(p.module.load_segment_at));
    put_byte(segment, static_cast<std::uint16_t>(p.offset), 0x90);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = segment;
    r.ip = static_cast<std::uint16_t>(p.offset);
    r[cpu::sreg::ds] = stack_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = frame_sp;
    r[cpu::reg16::bp] = frame_bp;
    box->step();
  }

  /// The bar leaf's frame for the character at `index` (one-based) of
  /// `text`, with the highlight on `group`, in a bar drawn in the two
  /// colours; and its glyph call, with `colour` as the call's colour.
  /// The group table is laid out by the program's own rule, restated: a
  /// command letter (`0-9A-Z`) starts a group, which ends two characters
  /// before the next one starts, and the last ends with the bar.
  void lay_bar_call(std::string_view text, unsigned group, unsigned index,
                    std::uint16_t colour, std::uint16_t lo,
                    std::uint16_t hi) const {
    put_word(stack_segment, frame_bp + leaf_caller_bp, caller_bp);
    put_byte(stack_segment, caller_bp + caller_colour_hi,
             static_cast<std::uint8_t>(hi));
    put_byte(stack_segment, caller_bp + caller_colour_lo,
             static_cast<std::uint8_t>(lo));
    put_byte(stack_segment, caller_bp - caller_bar_length,
             static_cast<std::uint8_t>(text.size()));
    put_byte(stack_segment, caller_bp - caller_bar,
             static_cast<std::uint8_t>(text.size()));
    for (std::size_t i = 0; i < text.size(); ++i) {
      put_byte(stack_segment,
               static_cast<std::uint32_t>(caller_bp - caller_bar + 1U + i),
               static_cast<std::uint8_t>(text[i]));
    }
    unsigned count = 0;
    unsigned previous = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
      const char c = text[i];
      if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) {
        if (previous != 0) {
          put_byte(stack_segment, caller_bp - caller_groups + (2U * count) + 1U,
                   static_cast<std::uint8_t>(i + 1U - 2U));
        }
        ++count;
        put_byte(stack_segment, caller_bp - caller_groups + (2U * count),
                 static_cast<std::uint8_t>(i + 1U));
        previous = static_cast<unsigned>(i + 1U);
      }
    }
    put_byte(stack_segment, caller_bp - caller_groups + (2U * count) + 1U,
             static_cast<std::uint8_t>(text.size()));
    put_byte(stack_segment, frame_bp - leaf_index,
             static_cast<std::uint8_t>(index));
    put_word(stack_segment, frame_bp + leaf_group, as_pushed(group));
    put_word(stack_segment, frame_sp + 0, as_pushed(1));
    put_word(stack_segment, frame_sp + 2,
             static_cast<std::uint8_t>(text[index - 1]));
    put_word(stack_segment, frame_sp + 4, as_pushed(1));
    put_word(stack_segment, frame_sp + 6, as_pushed(colour));
    put_word(stack_segment, frame_sp + 8, as_pushed(0x18));
    put_word(stack_segment, frame_sp + 10, 7);
  }

  /// Which of the leaf's three calls draws the character at `index`, by the
  /// program's rule: the highlighted group's, else a command letter's, else
  /// the rest's; and what colour it was about to draw in.
  void draw_bar_character(std::string_view text, unsigned group, unsigned index,
                          std::uint16_t lo, std::uint16_t hi) const {
    const auto is_letter = [](char c) {
      return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
    };
    // Where the group starts and ends, as the table above has it.
    unsigned start = 0;
    unsigned end = 0;
    unsigned count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
      if (is_letter(text[i])) {
        ++count;
        if (count == group) {
          start = static_cast<unsigned>(i + 1U);
        } else if (count == group + 1U) {
          end = static_cast<unsigned>(i + 1U - 2U);
        }
      }
    }
    if (end == 0) {
      end = static_cast<unsigned>(text.size());
    }
    const char c = text[index - 1];
    if (index >= start && index <= end && hi != 0) {
      lay_bar_call(text, group, index, hi, lo, hi);
      arrive(bar_group);
    } else if (is_letter(c)) {
      lay_bar_call(text, group, index, hi, lo, hi);
      arrive(bar_letter);
    } else {
      lay_bar_call(text, group, index, lo, lo, hi);
      arrive(bar_rest);
    }
  }

  /// The colour the call is left with.
  [[nodiscard]] std::uint8_t pushed_colour() const {
    return byte(stack_segment, frame_sp + 6);
  }

  /// A bar's every character, as one letter each for the colour it is
  /// drawn in: W white, Y yellow, G green, M light magenta, K black.
  [[nodiscard]] std::string colours_of(std::string_view text, unsigned group,
                                       std::uint16_t lo,
                                       std::uint16_t hi) const {
    std::string out;
    for (unsigned index = 1; index <= text.size(); ++index) {
      draw_bar_character(text, group, index, lo, hi);
      switch (pushed_colour()) {
        case white:
          out.push_back('W');
          break;
        case yellow:
          out.push_back('Y');
          break;
        case green:
          out.push_back('G');
          break;
        case light_magenta:
          out.push_back('M');
          break;
        case black:
          out.push_back('K');
          break;
        default:
          out.push_back('?');
          break;
      }
    }
    return out;
  }

  /// The string call's five words, as a leaf has pushed them.
  void lay_string_call(std::uint16_t offset, std::uint16_t segment,
                       std::uint16_t colour) const {
    put_word(stack_segment, frame_sp + 0, offset);
    put_word(stack_segment, frame_sp + 2, segment);
    put_word(stack_segment, frame_sp + 4, as_pushed(colour));
    put_word(stack_segment, frame_sp + 6, 9);
    put_word(stack_segment, frame_sp + 8, 3);
  }

  /// The list leaf: a string on its own stack, and its context's colour.
  void lay_list_row(std::uint16_t colour, std::uint16_t context_colour) const {
    lay_string_call(0x0700, stack_segment, colour);
    put_word(stack_segment, frame_bp + 6, list_context);
    put_byte(stack_segment, list_context + list_colour_hi,
             static_cast<std::uint8_t>(context_colour));
  }

  /// The roster drawer: the cell and the member it is drawing, in its frame
  /// and again as the call's own words.
  void lay_roster_name(std::uint16_t colour) const {
    constexpr std::uint8_t column = 0x13;
    constexpr std::uint8_t row = 5;
    put_byte(stack_segment, frame_bp - 5, column);
    put_byte(stack_segment, frame_bp - 6, row);
    put_word(stack_segment, frame_bp - 4, record_offset);
    put_word(stack_segment, frame_bp - 2, record_segment);
    put_word(stack_segment, frame_sp + 0, record_offset);
    put_word(stack_segment, frame_sp + 2, record_segment);
    put_word(stack_segment, frame_sp + 4, as_pushed(colour));
    put_word(stack_segment, frame_sp + 6, as_pushed(row));
    put_word(stack_segment, frame_sp + 8, as_pushed(column));
  }

  /// A score draw: the first argument at BP + 6, and the colour local.
  void lay_score(std::uint8_t highlighted, std::uint16_t local,
                 std::uint8_t colour) const {
    put_word(stack_segment, frame_bp + 6, highlighted);
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_bp - local),
             colour);
  }

  [[nodiscard]] std::vector<std::uint16_t> stack_words(unsigned count) const {
    std::vector<std::uint16_t> out;
    out.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
      out.push_back(word(stack_segment, frame_sp + (2U * i)));
    }
    return out;
  }

  std::unique_ptr<machine> box;
};

// --- The definition --------------------------------------------------------

TEST(SeamSelectYellow, IsSevenPointsAndOneOfThemIsInsideCalls) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: nothing to pull";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 7u);

  EXPECT_EQ(s.points[bar_group].offset, 0x0273u);
  EXPECT_EQ(s.points[list_row].offset, 0x09FDu);
  EXPECT_EQ(s.points[bar_letter].offset, 0x02CAu);
  EXPECT_EQ(s.points[bar_rest].offset, 0x0305u);
  for (const std::size_t which : {bar_group, list_row, bar_letter, bar_rest}) {
    const seam_point& p = s.points[which];
    EXPECT_EQ(p.module.file, "GAME.OVR");
    EXPECT_EQ(p.module.file_offset, 182479u);
    EXPECT_EQ(p.module.length, 4682u);
    EXPECT_EQ(p.module.load_segment_at, word_menu_bar);
    EXPECT_FALSE(p.inside_calls);
  }

  // The roster is given back through a batch by the automap and the
  // journal, and the point has to be there too.
  EXPECT_TRUE(s.points[roster_name].module.is_resident_image());
  EXPECT_EQ(s.points[roster_name].offset, 0x13AFu);
  EXPECT_TRUE(s.points[roster_name].inside_calls);

  EXPECT_FALSE(s.points[ability_score].module.is_resident_image());
  EXPECT_EQ(s.points[ability_score].module.file_offset, 135226u);
  EXPECT_EQ(s.points[ability_score].module.length, 11026u);
  EXPECT_EQ(s.points[ability_score].module.load_segment_at, word_sheet);
  EXPECT_EQ(s.points[ability_score].offset, 0x0918u);
  EXPECT_FALSE(s.points[ability_score].inside_calls);

  EXPECT_TRUE(s.points[hit_points].module.is_resident_image());
  EXPECT_EQ(s.points[hit_points].offset, 0x153Fu);
  EXPECT_FALSE(s.points[hit_points].inside_calls);

  for (const seam_point& p : s.points) {
    EXPECT_FALSE(p.at_every_step);
    EXPECT_FALSE(p.module.digest.empty() && !p.module.is_resident_image());
  }
}

TEST(SeamSelectYellow, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamSelectYellow, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamSelectYellow, EveryBuiltInSeamFitsTheEngineWithRoomToSpare) {
  // The faces are alternatives, so one of them counts.
  std::size_t points = 0;
  for (const seam_definition& s : all_seams()) {
    if (s.id != "font-chisel") {
      points += s.points.size();
    }
  }
  EXPECT_LE(points, seam_engine::max_points);

  const rig r;
  for (const seam_definition& s : all_seams()) {
    EXPECT_EQ(r.box->seams().enable(s.id), seam_reason::none) << s.id;
  }
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::on);
}

TEST(SeamSelectYellow, DoesNothingWhileItIsOff) {
  const rig r;
  r.manager_says(word_menu_bar, menu_bar_segment);
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  const auto before = r.stack_words(6);
  r.arrive(bar_group);
  EXPECT_EQ(r.stack_words(6), before);
}

TEST(SeamSelectYellow, IsInertWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.manager_says(word_menu_bar, 0);
  EXPECT_EQ(r.box->seams().status(seam_id).reason,
            seam_reason::module_not_resident);
}

// --- The bar ------------------------------------------------------------------
//
// The bars here are this file's own words: one capital to a word, as the
// program's are, and the same shapes that matter (a digit that ends a word,
// two command letters in one).

TEST(SeamSelectYellow, TheHighlightedWordIsYellowAndItsKeyStaysWhite) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Alpha Beta Gamma", 1, green, white),
            "WYYYYGWGGGGWGGGG");
  EXPECT_EQ(r.colours_of("Alpha Beta Gamma", 2, green, white),
            "WGGGGGWYYYGWGGGG");
  EXPECT_EQ(r.colours_of("Alpha Beta Gamma", 3, green, white),
            "WGGGGGWGGGGWYYYY");
}

TEST(SeamSelectYellow, AWordIsLitWholeWhereTheKeyIsNotItsFirstLetter) {
  // The program's group runs from a command letter to two before the next,
  // so with a digit ending a word its highlight is a word and a half.
  // The seam lights the word that holds the key, and nothing of the next.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Alpha bet9 gam8 Quit", 1, green, white),
            "WYYYYGGGGWGGGGWGWGGG");
  EXPECT_EQ(r.colours_of("Alpha bet9 gam8 Quit", 2, green, white),
            "WGGGGGYYYWGGGGWGWGGG");
  EXPECT_EQ(r.colours_of("Alpha bet9 gam8 Quit", 3, green, white),
            "WGGGGGGGGWGYYYWGWGGG");
  EXPECT_EQ(r.colours_of("Alpha bet9 gam8 Quit", 4, green, white),
            "WGGGGGGGGWGGGGWGWYYY");
}

TEST(SeamSelectYellow, EveryCommandLetterOfTheLitWordStaysWhite) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Ab9 Cd", 1, green, white), "WYWGWG");
  EXPECT_EQ(r.colours_of("Ab9 Cd", 2, green, white), "WYWGWG");
  EXPECT_EQ(r.colours_of("Ab9 Cd", 3, green, white), "WGWGWY");
}

TEST(SeamSelectYellow, ABarHandedTheColoursTheWrongWayRoundIsDrawnTheRightWay) {
  // white for the dim and green for the bright: every non-letter white, every
  // command letter green and the highlight green, without the seam.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Alpha Beta Gamma", 1, white, green),
            "WYYYYGWGGGGWGGGG");
  EXPECT_EQ(r.colours_of("Alpha Beta Gamma", 2, white, green),
            "WGGGGGWYYYGWGGGG");
}

TEST(SeamSelectYellow, WhereTheBarsBrightIsYellowTheKeyIsDrawnWhite) {
  const rig r;
  r.arm();
  // The highlighted word's key is white and the rest yellow; the other
  // words keep the bar's yellow letters, which this seam does not choose.
  EXPECT_EQ(r.colours_of("Alpha Beta", 2, green, yellow), "YGGGGGWYYY");
  EXPECT_EQ(r.colours_of("Alpha Beta", 1, green, yellow), "WYYYYGYGGG");
}

TEST(SeamSelectYellow, AnyOtherBrightIsKeptForTheKey) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Yes No", 1, green, light_magenta), "MYYGMG");
  EXPECT_EQ(r.colours_of("Yes No", 2, green, light_magenta), "MGGGMY");
}

TEST(SeamSelectYellow, ABarWithNoColourIsNotDrawnAndIsNotTouched) {
  // The main menu's bar is handed zero for both: the program draws it in
  // black, which is how it is not there.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Alpha Beta", 1, black, black), "KKKKKKKKKK");
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, TheBarWritesTheColoursLowByteAndNothingElse) {
  const rig r;
  r.arm();
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  const auto before = r.stack_words(6);
  r.arrive(bar_group);
  auto after = r.stack_words(6);
  EXPECT_EQ(after[3] & 0xFFU, yellow);
  EXPECT_EQ(after[3] & 0xFF00U, before[3] & 0xFF00U)
      << "the register's other half is the program's";
  after[3] = before[3];
  EXPECT_EQ(after, before) << "the character, the cell and the counts";
  EXPECT_EQ(r.word(stack_segment, frame_bp + leaf_caller_bp), caller_bp);
  EXPECT_EQ(r.byte(stack_segment, caller_bp + caller_colour_hi), white);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, TheBarDeclinesACallThatIsNotTheLeafItNames) {
  const rig r;
  r.arm();

  // The count or the fold flag is not one.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  r.put_word(stack_segment, frame_sp + 0, 2);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  // The row is not the bar's.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  r.put_word(stack_segment, frame_sp + 8, 0x17);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  // The colour is not what the caller's frame says it handed over.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, cyan);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  // The character is not the one the bar has at that position.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  r.put_word(stack_segment, frame_sp + 2, 'Z');
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  // The position is not in the bar, or the group is not in its table.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  r.put_byte(stack_segment, frame_bp - leaf_index, 11);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);
  r.lay_bar_call("Alpha Beta", 0, 2, white, green, white);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  // The group's letter is a space.
  r.lay_bar_call("Alpha Beta", 1, 2, white, green, white);
  r.put_byte(stack_segment, caller_bp - caller_groups + 2, 6);
  r.arrive(bar_group);
  EXPECT_EQ(r.pushed_colour(), white);

  EXPECT_EQ(r.box->seams().status(seam_id).declined, 7u);
}

TEST(SeamSelectYellow, TheOtherTwoBarPointsDeclineTheSameWay) {
  const rig r;
  r.arm();
  r.lay_bar_call("Alpha Beta", 1, 7, green, green, white);
  r.arrive(bar_letter);  // a letter is drawn in `color_hi`, not green
  r.lay_bar_call("Alpha Beta", 1, 3, white, green, white);
  r.arrive(bar_rest);  // a plain character is drawn in `color_lo`
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
}

// --- A bar with one command letter is not a selection (#460) -----------------

TEST(SeamSelectYellow, AOneChoiceNoticeIsNotLitLikeAMenu) {
  // The script runner's `Press <enter>/<return> to continue`: one capital,
  // so one group, which is the whole bar. The program draws it in the
  // bright from end to end, and the seam leaves it so.
  const rig r;
  r.arm();
  const std::string notice = "Press <enter>/<return> to continue";
  EXPECT_EQ(r.colours_of(notice, 1, white, white),
            std::string(notice.size(), 'W'));
  EXPECT_EQ(r.colours_of(notice, 1, green, white),
            std::string(notice.size(), 'W'));
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, ALoneWordWithOneKeyIsLeftAsTheProgramDrawsIt) {
  // A leading space is outside the group and is drawn in the dim.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Exit", 1, green, white), "WWWW");
  EXPECT_EQ(r.colours_of(" Pay it", 1, green, white), "GWWWWWW");
}

TEST(SeamSelectYellow, ANoticeHandedTheColoursTheWrongWayRoundIsPutRight) {
  // The swapped pair is normalised whether or not there is a selection: the
  // program draws this one green end to end, and a leading space white.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Press <enter> to go", 1, white, green),
            std::string(19, 'W'));
  EXPECT_EQ(r.colours_of(" Pay it", 1, white, green), "GWWWWWW");
}

TEST(SeamSelectYellow, ABarOfAllCapitalsWithSeveralKeysIsStillAChoice) {
  // The save and load slot bars are every letter a capital and one letter to
  // a group. The rule is the number of keys, not the case: the letters stay
  // white and the highlighted one is not lost.
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("A B C E J ", 1, green, white), "WGWGWGWGWG");
  EXPECT_EQ(r.colours_of("A B C E J ", 3, green, white), "WGWGWGWGWG");
  EXPECT_EQ(r.colours_of("A B C E J ", 3, white, green), "WGWGWGWGWG");
}

TEST(SeamSelectYellow, AMixedCaseBarWithTwoKeysKeepsItsHighlight) {
  const rig r;
  r.arm();
  EXPECT_EQ(r.colours_of("Yes No", 1, green, white), "WYYGWG");
  EXPECT_EQ(r.colours_of("Yes No", 2, green, white), "WGGGWY");
}

// --- The pick-list's row ------------------------------------------------------

TEST(SeamSelectYellow, TheListsRowIsYellow) {
  const rig r;
  r.arm();
  r.lay_list_row(white, white);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), yellow);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 6), 9u) << "the row";
  EXPECT_EQ(r.word(stack_segment, frame_sp + 8), 3u) << "the column";
  EXPECT_EQ(r.word(stack_segment, frame_sp + 0), 0x0700u);
  EXPECT_EQ(r.word(stack_segment, frame_sp + 2), stack_segment);
}

TEST(SeamSelectYellow,
     TheListsRowKeepsAnyOtherBrightAsYellowAndNoBrightAsNone) {
  const rig r;
  r.arm();
  r.lay_list_row(green, green);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), yellow);

  r.lay_list_row(yellow, yellow);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), yellow);

  r.lay_list_row(black, black);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), black);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, TheListDeclinesAStringThatIsNotOnItsOwnStack) {
  const rig r;
  r.arm();
  r.lay_list_row(white, white);
  r.put_word(stack_segment, frame_sp + 2, record_segment);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);

  r.lay_list_row(white, cyan);
  r.arrive(list_row);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
}

// --- The roster's selected member ---------------------------------------------

TEST(SeamSelectYellow, TheSelectedMembersNameIsYellow) {
  const rig r;
  r.arm();
  r.lay_roster_name(white);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), yellow);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 6), 5u);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 8), 0x13u);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, ANamedInAStatusColourIsLeftAlone) {
  // The drawer's other arm has its own call; a colour that is not white
  // is not the selection.
  const rig r;
  r.arm();
  for (const std::uint16_t colour : {cyan, yellow, std::uint16_t{0x0C}}) {
    r.lay_roster_name(colour);
    r.arrive(roster_name);
    EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), colour);
  }
}

TEST(SeamSelectYellow, TheRosterDeclinesACallThatIsNotTheMemberTheDrawerHas) {
  const rig r;
  r.arm();
  r.lay_roster_name(white);
  r.put_word(stack_segment, frame_sp + 0, record_offset + 1);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);

  r.lay_roster_name(white);
  r.put_word(stack_segment, frame_sp + 6, 6);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);

  r.lay_roster_name(white);
  r.put_word(stack_segment, frame_sp + 8, 0x11);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);

  r.lay_roster_name(white);
  r.put_word(stack_segment, frame_sp + 2, record_segment + 1);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), white);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 4u);
}

TEST(SeamSelectYellow, TheRostersNameFollowsTheNumberHeroKeysMovedItBy) {
  // `hero-keys` moves the column byte two to the right before the name is
  // drawn; the call's column is then the byte's, and this seam asks that
  // they agree rather than what either is.
  const rig r;
  r.arm();
  r.lay_roster_name(white);
  r.put_byte(stack_segment, frame_bp - 5, 0x15);
  r.put_word(stack_segment, frame_sp + 8, 0x15);
  r.arrive(roster_name);
  EXPECT_EQ(r.byte(stack_segment, frame_sp + 4), yellow);
}

// --- Modify's selected score ---------------------------------------------------

TEST(SeamSelectYellow, ModifysSelectedAbilityScoreIsYellow) {
  const rig r;
  r.arm();
  r.lay_score(1, 0x2B, static_cast<std::uint8_t>(light_magenta));
  r.arrive(ability_score);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 0x2B), yellow);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, AnAbilityScoreThatIsNotSelectedIsLeftGreen) {
  const rig r;
  r.arm();
  r.lay_score(0, 0x2B, static_cast<std::uint8_t>(green));
  r.arrive(ability_score);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 0x2B), green);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, TheAbilityScoreDeclinesAColourItDoesNotChoose) {
  const rig r;
  r.arm();
  r.lay_score(1, 0x2B, static_cast<std::uint8_t>(green));
  r.arrive(ability_score);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 0x2B), green);
  r.lay_score(0, 0x2B, static_cast<std::uint8_t>(light_magenta));
  r.arrive(ability_score);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 0x2B), light_magenta);
  r.lay_score(0, 0x2B, static_cast<std::uint8_t>(yellow));
  r.arrive(ability_score);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 0x2B), yellow);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 3u)
      << "a hurt character's yellow is for hit points only";
}

TEST(SeamSelectYellow, ModifysSelectedHitPointsAreYellow) {
  const rig r;
  r.arm();
  r.lay_score(1, 1, static_cast<std::uint8_t>(light_magenta));
  r.arrive(hit_points);
  EXPECT_EQ(r.byte(stack_segment, frame_bp - 1), yellow);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamSelectYellow, HitPointsThatAreNotSelectedKeepTheirOwnColours) {
  const rig r;
  r.arm();
  for (const std::uint16_t colour : {green, yellow}) {
    r.lay_score(0, 1, static_cast<std::uint8_t>(colour));
    r.arrive(hit_points);
    EXPECT_EQ(r.byte(stack_segment, frame_bp - 1), colour);
  }
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u)
      << "green for a whole one, yellow for one that is hurt";
}

TEST(SeamSelectYellow, TheHitPointsDeclineAColourTheyDoNotChoose) {
  const rig r;
  r.arm();
  r.lay_score(1, 1, static_cast<std::uint8_t>(green));
  r.arrive(hit_points);
  r.lay_score(0, 1, static_cast<std::uint8_t>(light_magenta));
  r.arrive(hit_points);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
}

}  // namespace
}  // namespace amberfolio::machine
