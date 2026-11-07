// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap on Curse of the Azure Bonds (#555): its seven points, its
// one adventuring bar, its "an area is up" rule, and the give-back through
// the roster drawer's overlay stub, exercised through the mechanism and
// not through any program. Each test stands the processor on one of
// Curse's points (a HLT there, so the step is the arrival and nothing
// else) with memory where Curse's facts put things.
//
// The offsets below are restated rather than read out of the seam, which
// is the seam suites' rule: a test that took its layout from the code it
// is checking would be agreeing with itself. **Every byte here is this
// file's own** (PLAN.md §6).

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"

namespace amberfolio::machine {
namespace {

constexpr std::string_view seam_id = "automap";
constexpr std::string_view curse_fingerprint =
    "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd";

// --- Curse's facts, restated ------------------------------------------------

constexpr std::uint16_t dgroup_paragraphs = 0xABE;

/// The points, in the resident image but the one in overlay 25.
constexpr std::uint16_t key_pending = 0x86EB;
constexpr std::uint16_t key_read = 0x86FD;
constexpr std::uint16_t region_clear = 0x17FD;
constexpr std::uint16_t text_clear = 0x580E;
constexpr std::uint16_t screen_clear = 0x56E2;
constexpr std::uint16_t roster_drawn = 0x092E;
constexpr std::uint16_t command_bar = 0x106A;

/// The load-segment words of overlay 15 (the adventuring loop) and 25 (the
/// party summary drawer); the drawer's stub, record and entry.
constexpr std::uint16_t word_adventure = 0x9A0;
constexpr std::uint16_t word_summary = 0xEE0;
constexpr std::uint16_t summary_stub_paragraph = 0x00ED;
constexpr std::uint16_t summary_stub_entry = 0x002A;

/// The data segment.
constexpr std::uint16_t data_game_mode = 0x4FD3;
constexpr std::uint16_t data_in_transition = 0x47F9;
constexpr std::uint16_t data_area_record = 0x4FB2;
constexpr std::uint16_t record_geometry_block = 0x18A;
constexpr std::uint16_t data_disk_number = 0x5C08;
constexpr std::uint16_t data_area_id = 0x8B78;
constexpr std::uint16_t data_party_x = 0x7229;
constexpr std::uint16_t data_party_y = 0x722A;
constexpr std::uint16_t data_party_facing = 0x722B;
constexpr std::uint16_t data_map_pointer = 0x7220;
constexpr std::uint16_t data_ground_colour = 0x7227;
/// The wall-set descriptor: slot 1's block at +4, slot 3's at +12.
constexpr std::uint16_t data_wall_set = 0x722A;
constexpr std::uint16_t wall_set_slot_3 = data_wall_set + 12;
constexpr std::uint16_t data_key_pushback = 0x8CBF;
constexpr std::uint16_t data_current_member = 0x6520;
constexpr std::uint16_t data_adventure_bar = 0x03D8;
constexpr std::uint16_t adventure_bar_return = 0x092C;
constexpr std::uint8_t mode_adventure = 4;
/// The mode a temple's or a shop's screen is.
constexpr std::uint8_t mode_shop = 1;

/// Where the menu-bar routine's frame holds the bar, at the thunk.
constexpr std::uint16_t bar_frame_menu_offset = 18;
constexpr std::uint16_t bar_frame_menu_segment = 20;

constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t map_segment = 0x5000;
constexpr std::uint16_t member_segment = 0x9000;
constexpr std::uint16_t adventure_segment = 0x6E00;
constexpr std::uint16_t summary_segment = 0x7400;

constexpr std::uint16_t key_tab = 0x0F09;

[[nodiscard]] sha256_digest curse_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(curse_fingerprint, digest));
  return digest;
}

[[nodiscard]] const seam_definition& definition() {
  for (const seam_definition& s : all_seams()) {
    if (s.id == seam_id) {
      return s;
    }
  }
  ADD_FAILURE() << "no seam is called " << seam_id;
  return all_seams().front();
}

struct curse_rig {
  curse_rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(curse_digest(), image_load_segment);
    put_word(image_load_segment, word_adventure, adventure_segment);
    put_word(image_load_segment, word_summary, summary_segment);
  }

  void enable() const {
    ASSERT_EQ(box->seams().enable(seam_id), seam_reason::none);
  }

  [[nodiscard]] static std::uint16_t dgroup() noexcept {
    return static_cast<std::uint16_t>(image_load_segment + dgroup_paragraphs);
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
    return static_cast<std::uint16_t>(
        byte(segment, offset) |
        (byte(segment, static_cast<std::uint16_t>(offset + 1)) << 8U));
  }

  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }
  [[nodiscard]] automap_state& map_state() const noexcept {
    return box->automap();
  }

  /// A HLT at `segment:offset`, the processor on it with DS and SS the
  /// data segment.
  void stand_on(std::uint16_t segment, std::uint16_t offset,
                std::uint16_t sp = 0x0400) const {
    put_byte(segment, offset, 0xF4);
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = segment;
    r.ip = offset;
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = sp;
  }

  /// The menu-bar thunk, with `segment:offset` as the bar and the far
  /// return `return_cs:return_ip` on top of the stack.
  void put_up_the_bar(std::uint16_t segment, std::uint16_t offset,
                      std::uint16_t return_cs = 0,
                      std::uint16_t return_ip = 0) const {
    constexpr std::uint16_t sp = 0x0400;
    stand_on(image_load_segment, command_bar, sp);
    put_word(dgroup(), sp, return_ip);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + 2), return_cs);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + bar_frame_menu_offset),
             offset);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + bar_frame_menu_segment),
             segment);
    box->step();
  }

  /// The adventuring screen: the mode, no transition, the area record and
  /// its geometry block, the area id `area` (zero is the titles), the
  /// party, a map, and the party's own bar.
  void adventuring(std::uint8_t area = 1) const {
    const std::uint16_t ds = dgroup();
    put_byte(ds, data_game_mode, mode_adventure);
    put_byte(ds, data_in_transition, 0);
    put_word(ds, data_area_record, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_area_record + 2),
             record_segment);
    put_byte(record_segment, record_geometry_block, 1);
    put_byte(ds, data_disk_number, 2);
    put_byte(ds, data_area_id, area);
    put_byte(ds, data_party_x, 7);
    put_byte(ds, data_party_y, 13);
    put_byte(ds, data_party_facing, 0);
    put_word(ds, data_map_pointer, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_map_pointer + 2), map_segment);
    put_byte(ds, data_ground_colour, 8);
    put_byte(ds, data_key_pushback, 0);
    put_word(ds, data_current_member, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_current_member + 2),
             member_segment);
    put_up_the_bar(ds, data_adventure_bar);
  }

  void type(std::uint16_t keystroke) const {
    const std::uint16_t tail = word(bda::segment, bda::keyboard_buffer_tail);
    put_word(bda::segment, tail, keystroke);
    auto next = static_cast<std::uint16_t>(tail + 2U);
    if (next >= bda::keyboard_buffer_end) {
      next = bda::keyboard_buffer;
    }
    put_word(bda::segment, bda::keyboard_buffer_tail, next);
  }

  void poll(unsigned times) const {
    for (unsigned i = 0; i < times; ++i) {
      stand_on(image_load_segment, key_pending);
      box->step();
    }
  }

  /// A wall face on the map: its kind in plane 0 or 1, its two style bits
  /// in plane 3 (automap_faces.h's format).
  void face(unsigned x, unsigned y, unsigned lane, std::uint8_t kind,
            std::uint8_t style) const {
    const auto cell = static_cast<std::uint16_t>((y * 16U) + x);
    const auto planes = static_cast<std::uint16_t>(
        lane == 0 || lane == 2 ? cell : 0x100U + cell);
    const bool high = lane == 0 || lane == 4;
    const std::uint8_t was = byte(map_segment, planes);
    put_byte(
        map_segment, planes,
        static_cast<std::uint8_t>(high ? ((was & 0x0FU) | (kind << 4U))
                                       : ((was & 0xF0U) | (kind & 0x0FU))));
    const auto styles = static_cast<std::uint16_t>(0x300U + cell);
    const std::uint8_t had = byte(map_segment, styles);
    put_byte(map_segment, styles,
             static_cast<std::uint8_t>((had & ~(3U << lane)) |
                                       ((style & 3U) << lane)));
  }

  /// One of the panel's pixels.
  [[nodiscard]] std::uint8_t pixel(unsigned x, unsigned y) const {
    return box->automap()
        .pixels()[(static_cast<std::size_t>(y) * automap_panel_width) + x];
  }

  /// The region clear entered with a rect: bottom, right, top, left.
  void clear(std::uint8_t bottom, std::uint8_t right, std::uint8_t top,
             std::uint8_t left) const {
    stand_on(image_load_segment, region_clear, 0x0400);
    put_word(dgroup(), 0x0404, bottom);
    put_word(dgroup(), 0x0406, right);
    put_word(dgroup(), 0x0408, top);
    put_word(dgroup(), 0x040A, left);
    box->step();
  }

  /// The roster drawer's return.
  void roster_drawn_back() const {
    stand_on(summary_segment, roster_drawn);
    box->step();
  }

  std::unique_ptr<machine> box;
};

// --- The definition ----------------------------------------------------------

TEST(SeamAutomapOnCurse, IsAvailableWithItsSevenPoints) {
  const curse_rig r;
  const std::span<const seam_point> mine =
      points_on(definition(), curse_digest());
  ASSERT_EQ(mine.size(), 7U);
  EXPECT_NE(mine.data(), definition().points.data());
  const std::array<std::uint16_t, 7> offsets{
      key_pending,  key_read,     region_clear, text_clear,
      screen_clear, roster_drawn, command_bar};
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    EXPECT_EQ(mine[i].offset, offsets[i]) << i;
    EXPECT_EQ(mine[i].module.is_resident_image(), i != 5) << i;
  }
  EXPECT_EQ(mine[5].module.load_segment_at, word_summary);
  EXPECT_EQ(r.box->seams().enable(seam_id), seam_reason::none);
}

// --- Fidelity ----------------------------------------------------------------

TEST(SeamAutomapOnCurse, OnAndTabNeverTypedTouchesNoMemory) {
  const curse_rig r;
  r.enable();
  r.adventuring();
  r.poll(4);
  const std::vector<std::uint8_t> before(r.box->memory().ram().begin(),
                                         r.box->memory().ram().end());
  r.poll(8);
  const std::vector<std::uint8_t> after(r.box->memory().ram().begin(),
                                        r.box->memory().ram().end());
  EXPECT_EQ(before, after);
  EXPECT_FALSE(r.map_state().panel_on_screen());
}

// --- The panel ---------------------------------------------------------------

TEST(SeamAutomapOnCurse, TabDrawsThePanelOnceAnAreaIsUp) {
  const curse_rig r;
  r.enable();
  r.adventuring(0);
  r.type(key_tab);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_open()) << "the Tab is the panel's";
  EXPECT_FALSE(r.map_state().panel_on_screen())
      << "area 0 is the titles: the program is in this mode from the start";

  r.put_byte(curse_rig::dgroup(), data_area_id, 1);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_on_screen());
}

// --- The one bar -------------------------------------------------------------

TEST(SeamAutomapOnCurse, TheOneAdventuringBarIsTheParty) {
  const curse_rig r;
  r.enable();
  r.adventuring();
  EXPECT_TRUE(r.map_state().at_command_bar()) << "the bar in the data segment";

  r.put_up_the_bar(curse_rig::dgroup(), 0x04DF);
  EXPECT_FALSE(r.map_state().at_command_bar())
      << "Pool of Radiance's 3D bar is nobody's here";

  r.put_up_the_bar(0x5000, 0x0300);
  EXPECT_FALSE(r.map_state().at_command_bar()) << "a copy on the stack";

  r.put_up_the_bar(0x5000, 0x0300, adventure_segment, adventure_bar_return);
  EXPECT_TRUE(r.map_state().at_command_bar())
      << "a copy, from the adventuring loop's own call";

  r.put_up_the_bar(0x5000, 0x0300, adventure_segment, 0x0C45);
  EXPECT_FALSE(r.map_state().at_command_bar())
      << "Pool of Radiance's second call site is not one here";
}

// --- The give-back -----------------------------------------------------------

/// A stand-in for the region clear: clean its eight bytes. A stand-in for
/// the drawer's stub entry: write down the segment it was entered with,
/// then clean its four bytes.
///
///   retf 8
///   mov ax, cs ; mov [witness], ax ; retf 4
constexpr std::uint16_t stub_witness = 0x0300;
constexpr std::array<std::uint8_t, 3> program_clear{0xCA, 0x08, 0x00};
constexpr std::array<std::uint8_t, 8> program_stub{0x8C, 0xC8, 0xA3, 0x00,
                                                   0x03, 0xCA, 0x04, 0x00};

TEST(SeamAutomapOnCurse, ClosingItCallsTheDrawerAtItsStubsOwnSegment) {
  const curse_rig r;
  r.enable();
  r.adventuring();
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  const auto place = [&r](std::uint32_t at,
                          std::span<const std::uint8_t> code) {
    for (std::size_t i = 0; i < code.size(); ++i) {
      r.box->memory().ram()[at + i] = code[i];
    }
  };
  const std::uint32_t image = cpu::physical_address(image_load_segment, 0);
  place(image + region_clear, program_clear);
  const auto stub =
      static_cast<std::uint16_t>(image_load_segment + summary_stub_paragraph);
  place(cpu::physical_address(stub, summary_stub_entry), program_stub);
  r.put_word(curse_rig::dgroup(), stub_witness, 0);

  r.type(key_tab);
  r.stand_on(image_load_segment, key_pending);
  const cpu::registers before = r.regs();
  r.box->step();
  EXPECT_FALSE(r.map_state().panel_open());
  for (int i = 0; i < 40; ++i) {
    r.box->step();
  }

  EXPECT_EQ(r.word(curse_rig::dgroup(), stub_witness), stub)
      << "the drawer's stub ran at the paragraph its record begins at, "
         "which is where the overlay manager reads the record from (#526)";
  EXPECT_EQ(r.regs()[cpu::sreg::cs], before[cpu::sreg::cs]);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], before[cpu::reg16::sp]);
}

// --- Doors (#555) ------------------------------------------------------------

/// The party's square on the panel, and the colours that matter.
constexpr unsigned party_x0 = 7 * automap_cell_pixels;
constexpr unsigned party_y0 = 13 * automap_cell_pixels;
constexpr std::uint8_t door_yellow = 14;
constexpr std::uint8_t ground = 8;
constexpr std::uint8_t mark_white = 15;
constexpr unsigned lane_west = 6;

/// A passable face of kind 11 west of the party, slot 3 holding `block`,
/// and a shut face of another kind elsewhere so the map has evidence of
/// its own and "no evidence, every way through is a door" is off.
void arched_way_west(const curse_rig& r, std::uint16_t block) {
  r.enable();
  r.adventuring();
  r.face(7, 13, lane_west, 11, 1);
  r.face(2, 2, 0, 5, 2);
  r.put_word(curse_rig::dgroup(), wall_set_slot_3, block);
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());
}

TEST(SeamAutomapOnCurse, BlockThreesFirstRowIsADoorWhereverItIsLoaded) {
  // Tilverton's arched doors: never shut on its own grid, shut on two
  // others, the same WALLDEF block and tiles on every disk that has it.
  const curse_rig r;
  arched_way_west(r, 3);
  EXPECT_EQ(r.pixel(party_x0, party_y0 + 3), door_yellow);
  EXPECT_NE(r.map_state().door_nibbles() & (1U << 11U), 0U);
}

TEST(SeamAutomapOnCurse, ARowNoTableNamesIsAWayThrough) {
  const curse_rig r;
  arched_way_west(r, 4);
  EXPECT_EQ(r.pixel(party_x0, party_y0 + 3), ground)
      << "block 4's first row: a way through, so the floor shows";
}

TEST(SeamAutomapOnCurse, ADoorOnThePartysSquareIsDrawnOverItsMark) {
  // The mark's black square covers the leaf's inner row; on Curse the
  // leaf goes back over it, under the arrow's white.
  const curse_rig r;
  arched_way_west(r, 3);
  EXPECT_EQ(r.pixel(party_x0 + 1, party_y0 + 2), door_yellow)
      << "the leaf's inner row, where the mark's black was";
  EXPECT_EQ(r.pixel(party_x0 + 1, party_y0 + 3), mark_white)
      << "the arrow's bar stays whole";
}

// --- A roster drawn over the panel (#555) ------------------------------------

/// The panel up, then the program's roster drawn over it the way a
/// temple's screen draws it: one row cleared at a time, then the drawer's
/// return, then the shop's mode and a bar of its own. The stand-ins for
/// the clear and the drawer are in place for the give-back.
void temple_over_the_panel(const curse_rig& r) {
  r.enable();
  r.adventuring();
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());

  for (std::uint8_t row = 4; row <= 10; ++row) {
    r.clear(row, 0x26, row, 0x11);
  }
  EXPECT_TRUE(r.map_state().stranded())
      << "rows 1 to 3 and 11 to 14 are still the map's";
  EXPECT_FALSE(r.map_state().panel_on_screen());
  r.roster_drawn_back();
  EXPECT_TRUE(r.map_state().roster_over_stranded());

  r.put_byte(curse_rig::dgroup(), data_game_mode, mode_shop);
  r.put_up_the_bar(0x5000, 0x0300);
  ASSERT_FALSE(r.map_state().at_command_bar());
}

void place_the_stand_ins(const curse_rig& r) {
  const auto place = [&r](std::uint32_t at,
                          std::span<const std::uint8_t> code) {
    for (std::size_t i = 0; i < code.size(); ++i) {
      r.box->memory().ram()[at + i] = code[i];
    }
  };
  const std::uint32_t image = cpu::physical_address(image_load_segment, 0);
  place(image + region_clear, program_clear);
  const auto stub =
      static_cast<std::uint16_t>(image_load_segment + summary_stub_paragraph);
  place(cpu::physical_address(stub, summary_stub_entry), program_stub);
  r.put_word(curse_rig::dgroup(), stub_witness, 0);
}

TEST(SeamAutomapOnCurse, ARosterDrawnOverThePanelIsGivenBackUnderAShopsBar) {
  const curse_rig r;
  temple_over_the_panel(r);
  place_the_stand_ins(r);

  r.stand_on(image_load_segment, key_pending);
  r.box->step();
  for (int i = 0; i < 40; ++i) {
    r.box->step();
  }
  EXPECT_EQ(r.word(curse_rig::dgroup(), stub_witness),
            image_load_segment + summary_stub_paragraph)
      << "the panel's rect cleared and the roster drawn again, in the "
         "shop's mode, because the program has just drawn it there";
  EXPECT_TRUE(r.map_state().panel_open()) << "off the screen, not closed";
  EXPECT_FALSE(r.map_state().stranded());
  EXPECT_FALSE(r.map_state().roster_over_stranded());
}

TEST(SeamAutomapOnCurse, SomethingElseDrawnAfterTheRosterIsNotGivenBack) {
  const curse_rig r;
  temple_over_the_panel(r);
  // A list over most of the right-hand frame: its own screen now.
  r.clear(0x0F, 0x26, 0x04, 0x02);
  EXPECT_FALSE(r.map_state().roster_over_stranded());
  place_the_stand_ins(r);

  r.poll(2);
  for (int i = 0; i < 40; ++i) {
    r.box->step();
  }
  EXPECT_EQ(r.word(curse_rig::dgroup(), stub_witness), 0U);
}

TEST(SeamAutomapOnCurse, AWholePanelClearedLeavesNothingBehind) {
  const curse_rig r;
  r.enable();
  r.adventuring();
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());
  r.clear(0x16, 0x26, 0x01, 0x01);
  EXPECT_FALSE(r.map_state().stranded());
  r.roster_drawn_back();
  EXPECT_FALSE(r.map_state().roster_over_stranded());
}

// --- Who owns the cells ------------------------------------------------------

TEST(SeamAutomapOnCurse, BothRegionClearsCoverAndTheDrawersReturnUncovers) {
  const curse_rig r;
  r.enable();
  r.adventuring();
  for (const std::uint16_t clear : {region_clear, text_clear}) {
    r.map_state().set_panel_covered(false);
    // bottom, right, top, left: the roster's rows.
    r.stand_on(image_load_segment, clear, 0x0400);
    r.put_word(curse_rig::dgroup(), 0x0404, 0x0E);
    r.put_word(curse_rig::dgroup(), 0x0406, 0x26);
    r.put_word(curse_rig::dgroup(), 0x0408, 0x02);
    r.put_word(curse_rig::dgroup(), 0x040A, 0x11);
    r.box->step();
    EXPECT_TRUE(r.map_state().panel_covered()) << clear;
  }

  r.stand_on(summary_segment, roster_drawn);
  r.box->step();
  EXPECT_FALSE(r.map_state().panel_covered())
      << "the drawer's return, in overlay 25 where the manager says it is";

  r.stand_on(image_load_segment, screen_clear);
  r.box->step();
  EXPECT_TRUE(r.map_state().panel_covered());
}

}  // namespace
}  // namespace amberfolio::machine
