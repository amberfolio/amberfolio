// SPDX-License-Identifier: AGPL-3.0-only
//
// The menu-cursor seam (seam_menu_cursor.cpp, #434), exercised through its
// mechanism and not through any program: the test stands the processor on
// the seam's one point with the menu-bar routine's frame laid out where the
// facts say it is, the main menu's records laid out in the data segment, a
// keystroke at the head of the BIOS ring, and reads the ring, the data
// segment and a log the string drawer's stand-in keeps after the batch the
// handler queued has run itself out.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it is
// checking would be agreeing with itself. The words are made up, and
// **every byte here is this file's own** (PLAN.md §6).

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "menu-cursor";

/// The point: the call into the key-read routine, in overlay 25.
constexpr std::uint16_t point = 0x0572;

/// The words the program's overlay manager keeps the segments in: overlay
/// 25 (the menu-bar routine) and overlay 16 (the party-setup loop).
constexpr std::uint32_t word_menu_bar = 0x3C60;
constexpr std::uint32_t word_loop = 0x790;
constexpr std::uint16_t menu_bar_segment = 0x6000;
constexpr std::uint16_t loop_segment = 0x7800;

/// The return offsets: the loop's call, and two other callers of the
/// routine, one in the same overlay (the stat editor) and one in the
/// routine's own.
constexpr std::uint16_t ret_loop = 0x02FD;
constexpr std::uint16_t ret_editor = 0x216E;
constexpr std::uint16_t ret_yes_no = 0x111E;

/// The frame, below and above BP.
constexpr std::uint16_t frame_ip = 2;
constexpr std::uint16_t frame_cs = 4;
constexpr std::uint16_t local_bar = 0x53;

constexpr std::uint16_t data_segment = 0x3000;
constexpr std::uint16_t stack_segment = 0x5000;
constexpr std::uint16_t frame_base = 0x0600;

constexpr std::uint16_t data_pushback = 0x8501;
constexpr std::uint16_t data_highlight = 0x6B2B;

/// The command records: eleven, forty-two bytes each, the enable byte at
/// +0x29. Restated.
constexpr std::uint16_t records = 0x0619;
constexpr std::uint16_t stride = 0x2A;
constexpr std::uint8_t record_count = 11;
constexpr std::uint16_t enable_offset = 0x29;
constexpr std::uint8_t record_drop = 1;
constexpr std::uint8_t record_load = 7;

/// The made-up words, one to a record, in the order the program keeps
/// them.
constexpr std::array<std::string_view, record_count> words{
    "Cargo", "Dune", "Mist", "Tide",  "Vale", "Aster",
    "Reed",  "Lark", "Sage", "Birch", "Elm"};

/// The bar the routine was handed: a letter to a command.
constexpr std::string_view bar = "C D M T V A R L S B E J";

constexpr std::uint16_t row_zero = 0x0C;
constexpr std::uint16_t column_letter = 2;
constexpr std::uint16_t column_rest = 3;
constexpr std::uint16_t white = 0x0F;
constexpr std::uint16_t green = 0x0A;

/// The keys, as the ring holds them.
constexpr std::uint16_t up = 0x4800;
constexpr std::uint16_t down = 0x5000;
constexpr std::uint16_t left = 0x4B00;
constexpr std::uint16_t right = 0x4D00;
constexpr std::uint16_t home = 0x4700;
constexpr std::uint16_t end = 0x4F00;
constexpr std::uint16_t enter = 0x1C0D;
constexpr std::uint16_t letter_a = 0x1E41;
constexpr std::uint16_t placeholder = 0x0C2D;

constexpr std::uint16_t ring_first = 0x1E;
constexpr std::uint16_t ring_end = 0x3E;

/// The string drawer, and where the stand-in keeps its log.
constexpr std::uint16_t image_draw_string = 0x76B6;
constexpr std::uint16_t log_pointer = 0x7000;
constexpr std::uint16_t log_base = 0x7010;
constexpr std::uint16_t log_stride = 10 + 41;

struct drawn {
  std::uint16_t column;
  std::uint16_t row;
  std::uint16_t colour;
  std::string text;

  bool operator==(const drawn&) const = default;
};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    lay_records();
    lay_bar();
    string_drawer();
    put_word(data_segment, log_pointer, log_base);
  }

  [[nodiscard]] const seam_definition& seam() const {
    const seam_definition* found = box->seams().find(seam_id);
    EXPECT_NE(found, nullptr);
    return *found;
  }

  void arm() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
    manager_says(word_menu_bar, menu_bar_segment);
    manager_says(word_loop, loop_segment);
  }

  void manager_says(std::uint32_t word, std::uint16_t segment) const {
    put_word(image_load_segment, static_cast<std::uint16_t>(word), segment);
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

  [[nodiscard]] std::uint8_t byte(std::uint16_t segment,
                                  std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
  }

  [[nodiscard]] std::uint16_t word(std::uint16_t segment,
                                   std::uint16_t offset) const {
    return static_cast<std::uint16_t>(byte(segment, offset) |
                                      (byte(segment, offset + 1U) << 8U));
  }

  /// The eleven records: the made-up words, every command off.
  void lay_records() const {
    for (std::uint8_t r = 0; r < record_count; ++r) {
      const auto base = static_cast<std::uint16_t>(records + (r * stride));
      put_byte(data_segment, base, static_cast<std::uint8_t>(words[r].size()));
      for (std::size_t i = 0; i < words[r].size(); ++i) {
        put_byte(data_segment, static_cast<std::uint16_t>(base + 1U + i),
                 static_cast<std::uint8_t>(words[r][i]));
      }
      put_byte(data_segment, static_cast<std::uint16_t>(base + enable_offset),
               0);
    }
  }

  [[nodiscard]] static std::uint16_t enable_at(std::uint8_t record) {
    return static_cast<std::uint16_t>(records + (record * stride) +
                                      enable_offset);
  }

  /// What the menu shows: these records on, and the rest off, each on at
  /// one, which is what the program's redraw leaves.
  void show(std::initializer_list<std::uint8_t> shown) const {
    for (std::uint8_t r = 0; r < record_count; ++r) {
      put_byte(data_segment, enable_at(r), 0);
    }
    for (const std::uint8_t r : shown) {
      put_byte(data_segment, enable_at(r), 1);
    }
  }

  /// The routine's own copy of the bar, a Pascal string.
  void lay_bar(std::string_view text = bar) const {
    put_byte(stack_segment, static_cast<std::uint16_t>(frame_base - local_bar),
             static_cast<std::uint8_t>(text.size()));
    for (std::size_t i = 0; i < text.size(); ++i) {
      put_byte(stack_segment,
               static_cast<std::uint16_t>(frame_base - local_bar + 1U + i),
               static_cast<std::uint8_t>(text[i]));
    }
  }

  /// The string drawer's stand-in. It appends to a log, for every call, the
  /// column, the row, the colour, the segment and offset it was handed, and
  /// the forty-one bytes at that far pointer.
  ///
  ///     push bp / mov bp, sp / push ds / push ds / pop es
  ///     mov di, [log_pointer]
  ///     five times: mov ax, [bp+k] / mov [di], ax / add di, 2
  ///     lds si, [bp+6] / mov cx, 41 / cld / rep movsb
  ///     pop ds / mov [log_pointer], di / pop bp / retf 0Ah
  void string_drawer() const {
    std::uint16_t put = image_draw_string;
    const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
      for (const std::uint8_t b : bytes) {
        put_byte(image_load_segment, put++, b);
      }
    };
    const auto emit_word = [&](std::uint16_t value) {
      emit({static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>(value >> 8U)});
    };
    emit({0x55, 0x89, 0xE5, 0x1E, 0x1E, 0x07});
    emit({0x8B, 0x3E});
    emit_word(log_pointer);
    for (const std::uint8_t k :
         std::array<std::uint8_t, 5>{0x0E, 0x0C, 0x0A, 0x08, 0x06}) {
      emit({0x8B, 0x46, k, 0x89, 0x05, 0x83, 0xC7, 0x02});
    }
    emit({0xC5, 0x76, 0x06, 0xB9, 0x29, 0x00, 0xFC, 0xF3, 0xA4, 0x1F});
    emit({0x89, 0x3E});
    emit_word(log_pointer);
    emit({0x5D, 0xCA, 0x0A, 0x00});
  }

  /// What the drawer was asked to draw, in order.
  [[nodiscard]] std::vector<drawn> calls() const {
    std::vector<drawn> out;
    const std::uint16_t end_of_log = word(data_segment, log_pointer);
    for (std::uint16_t at = log_base; at < end_of_log; at += log_stride) {
      drawn d;
      d.column = word(data_segment, at);
      d.row = word(data_segment, static_cast<std::uint16_t>(at + 2U));
      d.colour = word(data_segment, static_cast<std::uint16_t>(at + 4U));
      const auto length =
          byte(data_segment, static_cast<std::uint16_t>(at + 10U));
      for (std::uint8_t i = 0; i < length; ++i) {
        d.text.push_back(static_cast<char>(
            byte(data_segment, static_cast<std::uint16_t>(at + 11U + i))));
      }
      out.push_back(d);
    }
    return out;
  }

  /// Who called.
  void called_from(std::uint16_t segment, std::uint16_t offset) const {
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_ip),
             offset);
    put_word(stack_segment, static_cast<std::uint16_t>(frame_base + frame_cs),
             segment);
  }

  /// Keystrokes in the ring, the first at the head.
  void ring(std::initializer_list<std::uint16_t> keys) const {
    std::uint16_t slot = ring_first;
    put_word(0x40, 0x1A, ring_first);
    for (const std::uint16_t key : keys) {
      put_word(0x40, slot, key);
      slot = static_cast<std::uint16_t>(slot + 2U);
      if (slot >= ring_end) {
        slot = ring_first;
      }
    }
    put_word(0x40, 0x1C, slot);
  }

  [[nodiscard]] std::uint16_t head_word() const {
    return word(0x40, word(0x40, 0x1A));
  }

  /// Stand on the point and take one step; the instruction there is a HLT.
  void arrive() const {
    put_byte(menu_bar_segment, point, 0xF4);
    box->processor().reset();
    cpu::registers& r = box->processor().regs();
    r[cpu::sreg::cs] = menu_bar_segment;
    r.ip = point;
    r[cpu::sreg::ds] = data_segment;
    r[cpu::sreg::ss] = stack_segment;
    r[cpu::reg16::sp] = 0x0400;
    r[cpu::reg16::bp] = frame_base;
    box->step();
  }

  /// Steps until the batch the arrival queued has run itself out and the
  /// machine is back on the HLT.
  void run_the_calls() const {
    for (unsigned nth = 0; nth < 4096; ++nth) {
      if (box->processor().halted()) {
        return;
      }
      box->step();
    }
  }

  /// One arrival from the party-setup loop with `keys` waiting, and the
  /// batch it queued run out. Returns the head of the ring afterwards.
  [[nodiscard]] std::uint16_t press(
      std::initializer_list<std::uint16_t> keys) const {
    called_from(loop_segment, ret_loop);
    ring(keys);
    arrive();
    run_the_calls();
    return head_word();
  }

  /// The byte the cursor lives in, when one of the two is on.
  [[nodiscard]] std::uint8_t cursor_byte() const {
    const std::uint8_t drop = byte(data_segment, enable_at(record_drop));
    return drop != 0 ? drop : byte(data_segment, enable_at(record_load));
  }

  std::unique_ptr<machine> box;
};

// --- The definition --------------------------------------------------------

TEST(SeamMenuCursor, IsOnePointInOverlay25) {
  const rig r;
  const seam_definition& s = r.seam();

  EXPECT_FALSE(s.about.empty());
  EXPECT_FALSE(s.trigger) << "a setting: the keys work, nothing to pull";
  EXPECT_EQ(s.gate, document_kind::none);
  EXPECT_TRUE(s.group.empty()) << "nothing is its alternative";
  EXPECT_EQ(s.schema, seam_schema_version);
  ASSERT_EQ(s.points.size(), 1u);

  const seam_point& p = s.points[0];
  EXPECT_EQ(p.offset, point);
  EXPECT_FALSE(p.module.is_resident_image());
  EXPECT_EQ(p.module.file, "GAME.OVR");
  EXPECT_EQ(p.module.file_offset, 182479u);
  EXPECT_EQ(p.module.length, 4682u);
  EXPECT_EQ(p.module.load_segment_at, word_menu_bar);
  EXPECT_FALSE(p.at_every_step);
  EXPECT_FALSE(p.inside_calls);
}

TEST(SeamMenuCursor, IsOffByDefault) {
  const rig r;
  EXPECT_EQ(r.box->seams().status(seam_id).state, seam_state::off);
}

TEST(SeamMenuCursor, IsUnavailableOnAnyOtherBinary) {
  auto box = std::make_unique<machine>(memory_layout::pc);
  sha256_digest other{};
  other.bytes[0] = 1;
  box->seams().loaded(other, image_load_segment);

  EXPECT_EQ(box->seams().status(seam_id).state, seam_state::unavailable);
  EXPECT_EQ(box->seams().status(seam_id).reason, seam_reason::wrong_binary);
  EXPECT_EQ(box->seams().enable(seam_id), seam_reason::wrong_binary);
}

TEST(SeamMenuCursor, IsInertWhileOverlay25IsNotLoaded) {
  const rig r;
  r.arm();
  r.show({0, 1, 2});
  r.manager_says(word_menu_bar, 0);

  EXPECT_EQ(r.box->seams().status(seam_id).reason,
            seam_reason::module_not_resident);
  EXPECT_EQ(r.press({down}), down);
  EXPECT_TRUE(r.calls().empty());
}

TEST(SeamMenuCursor, DoesNothingWhileItIsOff) {
  const rig r;
  r.manager_says(word_menu_bar, menu_bar_segment);
  r.manager_says(word_loop, loop_segment);
  r.show({0, 1, 2});

  EXPECT_EQ(r.press({down}), down);
  EXPECT_TRUE(r.calls().empty());
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(record_drop)), 1);
}

// --- Hidden until used ------------------------------------------------------

TEST(SeamMenuCursor, TouchesNothingForAnyKeyButUpDownAndReturn) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  for (const std::uint16_t key :
       {letter_a, left, right, home, end, std::uint16_t{0x011B}, placeholder}) {
    EXPECT_EQ(r.press({key}), key) << std::hex << key;
  }
  EXPECT_TRUE(r.calls().empty());
  EXPECT_EQ(r.cursor_byte(), 1);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamMenuCursor, LeavesReturnAloneWhileNoCursorIsDrawn) {
  // The program drops a Return at this menu, and with nothing drawn it
  // keeps doing so: there is no command a player can see to take.
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  EXPECT_EQ(r.press({enter}), enter);
  EXPECT_TRUE(r.calls().empty());
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 0u);
}

TEST(SeamMenuCursor, KeepsTheBarsHighlightByteOutOfIt) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.put_byte(data_segment, data_highlight, 3);

  (void)r.press({down});
  (void)r.press({down});
  EXPECT_EQ(r.byte(data_segment, data_highlight), 3);
}

// --- Up and Down ------------------------------------------------------------

TEST(SeamMenuCursor, DownFromNothingMovesToTheSecondCommandShown) {
  // The cursor starts on the first command, so the first press moves it:
  // Down, Down takes the third.
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  EXPECT_EQ(r.press({down}), placeholder);
  EXPECT_EQ(r.calls(),
            (std::vector<drawn>{{column_letter, row_zero + 1, white, "Dune"}}));
  EXPECT_EQ(r.cursor_byte(), 3) << "two, and the row it is on";
}

TEST(SeamMenuCursor, UpFromNothingWrapsToTheLastCommandShown) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  EXPECT_EQ(r.press({up}), placeholder);
  EXPECT_EQ(r.calls(),
            (std::vector<drawn>{{column_letter, row_zero + 3, white, "Tide"}}));
  EXPECT_EQ(r.cursor_byte(), 5);
}

TEST(SeamMenuCursor, SkipsTheCommandsTheMenuDoesNotShow) {
  // Records 0, 2, 5, 7 and 10 are on: the rows are those, packed from the
  // first, and Down walks them and not the records.
  const rig r;
  r.arm();
  r.show({0, 2, 5, 7, 10});

  (void)r.press({down});
  (void)r.press({down});
  const std::vector<drawn> seen = r.calls();
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[0], (drawn{column_letter, row_zero + 1, white, "Mist"}));
  EXPECT_EQ(seen[1], (drawn{column_letter, row_zero + 2, white, "Aster"}));
}

TEST(SeamMenuCursor, WrapsAtBothEnds) {
  const rig r;
  r.arm();
  r.show({0, 1, 3});

  (void)r.press({down});
  (void)r.press({down});
  EXPECT_EQ(r.cursor_byte(), 4) << "the last row";
  (void)r.press({down});
  EXPECT_EQ(r.cursor_byte(), 2) << "the first, after the last";
  (void)r.press({up});
  EXPECT_EQ(r.cursor_byte(), 4) << "and back round";
}

TEST(SeamMenuCursor, PutsTheWordItLeavesBackInTheMenusTwoColours) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});
  (void)r.press({down});

  // The second press lights the third row and then puts the second back:
  // the first letter in white at the first column, the rest in green at
  // the next.
  const std::vector<drawn> seen = r.calls();
  ASSERT_EQ(seen.size(), 4u);
  EXPECT_EQ(seen[1], (drawn{column_letter, row_zero + 2, white, "Mist"}));
  EXPECT_EQ(seen[2], (drawn{column_letter, row_zero + 1, white, "D"}));
  EXPECT_EQ(seen[3], (drawn{column_rest, row_zero + 1, green, "une"}));
}

TEST(SeamMenuCursor, LightsTheWordBeforeItPutsTheOldOneBack) {
  // So that the cursor is on the glass first while the program's slow
  // string routine works through the rest.
  const rig r;
  r.arm();
  r.show({0, 1, 2});
  (void)r.press({down});
  (void)r.press({down});

  const std::vector<drawn> seen = r.calls();
  ASSERT_GE(seen.size(), 3u);
  EXPECT_EQ(seen[1].colour, white);
  EXPECT_EQ(seen[1].text, "Mist");
}

TEST(SeamMenuCursor, HasNoRestToDrawForAOneLetterWord) {
  const rig r;
  r.arm();
  r.show({0, 1, 2});
  r.put_byte(data_segment, records + (1 * stride), 1);
  (void)r.press({down});
  (void)r.press({down});

  const std::vector<drawn> seen = r.calls();
  ASSERT_EQ(seen.size(), 3u) << "the new word, and the old one's letter";
  EXPECT_EQ(seen[2], (drawn{column_letter, row_zero + 1, white, "D"}));
}

TEST(SeamMenuCursor, LeavesTheRecordsAsItFoundThem) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});
  (void)r.press({down});

  for (std::uint8_t rec = 0; rec < record_count; ++rec) {
    const auto base = static_cast<std::uint16_t>(records + (rec * stride));
    EXPECT_EQ(r.byte(data_segment, base),
              static_cast<std::uint8_t>(words[rec].size()));
    for (std::size_t i = 0; i < words[rec].size(); ++i) {
      EXPECT_EQ(r.byte(data_segment, static_cast<std::uint16_t>(base + 1U + i)),
                static_cast<std::uint8_t>(words[rec][i]));
    }
  }
}

TEST(SeamMenuCursor, LivesInTheLoadByteWhenThereIsNoPartyMember) {
  // The menu's redraw turns Drop on with a member and Load on without, and
  // one is always on: the cursor is held in whichever it is.
  const rig r;
  r.arm();
  r.show({0, 7, 10});

  (void)r.press({down});
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(record_load)), 3);
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(record_drop)), 0);
  EXPECT_EQ(r.calls().front().text, "Lark");

  // And the byte still says "shown", the way the program tests it.
  (void)r.press({down});
  EXPECT_EQ(r.calls().size(), 4u) << "Elm, then Lark put back in two parts";
}

TEST(SeamMenuCursor, WritesNothingButTheCursorByteAndTheHeadWord) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.called_from(loop_segment, ret_loop);
  r.ring({down, 0xBEEF});
  r.arrive();

  EXPECT_EQ(r.word(0x40, ring_first + 2), 0xBEEF) << "the key behind it";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first) << "the head did not move";
  EXPECT_EQ(r.word(0x40, 0x1C), ring_first + 4) << "nor the tail";
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(0)), 1);
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(2)), 1);
  EXPECT_EQ(r.byte(data_segment, rig::enable_at(3)), 1);
}

// --- Return -----------------------------------------------------------------

TEST(SeamMenuCursor, ReturnBecomesTheLetterOfTheCommandUnderTheCursor) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});
  (void)r.press({down});

  const std::uint16_t taken = r.press({enter});
  EXPECT_EQ(taken, 0x1C4D) << "Mist, under Return's own scan code";
  EXPECT_EQ(r.calls().size(), 4u) << "and nothing is drawn for it";
}

TEST(SeamMenuCursor, ReturnWorksAfterAKeyPressedWhileTheCursorWasBeingDrawn) {
  // The cursor's byte is written when the move is decided, not when its
  // pixels are done.
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.called_from(loop_segment, ret_loop);
  r.ring({down, enter});
  r.arrive();
  r.run_the_calls();

  // The placeholder, then the Return behind it, which is now the head.
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first + 2);
  EXPECT_EQ(r.head_word(), 0x1C44) << "Dune: the cursor is on the second row";
}

TEST(SeamMenuCursor, ReturnDeclinesALetterTheBarDoesNotHold) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});
  r.lay_bar("A B C");

  EXPECT_EQ(r.press({enter}), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
}

TEST(SeamMenuCursor, ReturnDeclinesAWordWithNothingInIt) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});
  r.put_byte(data_segment, records + (1 * stride), 0);

  EXPECT_EQ(r.press({enter}), enter);
  EXPECT_EQ(r.box->seams().status(seam_id).declined, 1u);
}

// --- A second press while the cursor is being drawn --------------------------

TEST(SeamMenuCursor, HandlesAKeyPressedBehindItsOwnPlaceholder) {
  // The program's read keeps the last key waiting and drops the rest, so
  // the placeholder is taken off and the key behind it is the one moved.
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  (void)r.press({down});

  EXPECT_EQ(r.press({placeholder, down}), placeholder);
  EXPECT_EQ(r.cursor_byte(), 4) << "the third row";
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first + 2) << "the placeholder is gone";
}

TEST(SeamMenuCursor, LeavesItsPlaceholderAloneWhenNothingFollows) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  EXPECT_EQ(r.press({placeholder}), placeholder);
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first);
  EXPECT_TRUE(r.calls().empty());
}

TEST(SeamMenuCursor, LeavesItsPlaceholderAloneWhenALetterFollows) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  EXPECT_EQ(r.press({placeholder, letter_a}), placeholder);
  EXPECT_EQ(r.word(0x40, 0x1A), ring_first);
  EXPECT_EQ(r.word(0x40, ring_first + 2), letter_a);
}

// --- The caller -------------------------------------------------------------

TEST(SeamMenuCursor, IsTheMainMenusAndNoOtherCallersOfTheRoutine) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  // The stat editor is in the same overlay and is not the menu.
  r.called_from(loop_segment, ret_editor);
  r.ring({down});
  r.arrive();
  EXPECT_EQ(r.head_word(), down);

  // Nor the Yes/No prompt, in the routine's own overlay.
  r.called_from(menu_bar_segment, ret_yes_no);
  r.ring({down});
  r.arrive();
  EXPECT_EQ(r.head_word(), down);

  // Nor the loop's offset in some other module's segment.
  r.called_from(loop_segment + 0x40, ret_loop);
  r.ring({down});
  r.arrive();
  EXPECT_EQ(r.head_word(), down);

  EXPECT_TRUE(r.calls().empty());
  EXPECT_EQ(r.cursor_byte(), 1);
}

TEST(SeamMenuCursor, IsInertForTheLoopWhileOverlay16IsNotLoaded) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.manager_says(word_loop, 0);

  EXPECT_EQ(r.press({down}), down);
  EXPECT_TRUE(r.calls().empty());
}

// --- What it will not read --------------------------------------------------

TEST(SeamMenuCursor, DeclinesAMenuWhoseCursorByteIsNotThereToHoldIt) {
  // Neither of the two is on, or both: not the menu the facts describe.
  const rig r;
  r.arm();
  r.show({0, 2, 3});
  EXPECT_EQ(r.press({down}), down);
  r.show({0, 1, 7, 3});
  EXPECT_EQ(r.press({down}), down);

  EXPECT_EQ(r.box->seams().status(seam_id).declined, 2u);
  EXPECT_TRUE(r.calls().empty());
}

TEST(SeamMenuCursor, LeavesTheKeyAloneWhileThePushbackSlotIsArmed) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.put_byte(data_segment, data_pushback, 0x48);

  EXPECT_EQ(r.press({down}), down);
  EXPECT_TRUE(r.calls().empty());
}

TEST(SeamMenuCursor, LeavesTheKeyAloneWhileTheJournalReaderIsOpen) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});

  r.box->journal().set_reader(journal_reader_mode::listing);
  EXPECT_EQ(r.press({down}), down);
  EXPECT_TRUE(r.calls().empty());

  r.box->journal().set_reader(journal_reader_mode::closed);
  EXPECT_EQ(r.press({down}), placeholder);
}

TEST(SeamMenuCursor, LeavesTheRingAloneWhenItIsEmpty) {
  const rig r;
  r.arm();
  r.show({0, 1, 2, 3});
  r.called_from(loop_segment, ret_loop);
  r.ring({});
  r.put_word(0x40, ring_first, down);
  r.arrive();

  EXPECT_EQ(r.word(0x40, ring_first), down);
  EXPECT_TRUE(r.calls().empty());
}

}  // namespace
}  // namespace amberfolio::machine
