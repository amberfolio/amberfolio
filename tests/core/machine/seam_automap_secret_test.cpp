// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap on Secret of the Silver Blades (#609): its eight points, the
// party's two bars that are calls and not data, the keys its bar answers
// with a roster step, the read that follows the program's second poll,
// its "a grid is up" rule, the area whose script numbers its own squares,
// and the give-back through the roster drawer's overlay stub, exercised
// through the mechanism and not through any program. Each test stands the
// processor on one of the program's points (a HLT there, so the step is
// the arrival and nothing else) with memory where its facts put things.
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
#include <utility>
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
constexpr std::string_view secret_fingerprint =
    "8b937984878ec72aa531335dd381a21bda67675408b5fd8e716a35ccd049999e";

// --- Secret's facts, restated -----------------------------------------------

constexpr std::uint16_t dgroup_paragraphs = 0xDE2;

/// The points: the keyboard, the two region clears, the screen clear and
/// the menu-bar thunk in the resident image; the roster drawer's return
/// and the one-row redraw's in overlay 27.
constexpr std::uint16_t key_pending = 0xBA8A;
constexpr std::uint16_t key_read = 0xBA9C;
constexpr std::uint16_t region_clear = 0x1EE9;
constexpr std::uint16_t text_clear = 0x600E;
constexpr std::uint16_t screen_clear = 0x5EE4;
constexpr std::uint16_t roster_drawn = 0x0984;
constexpr std::uint16_t roster_row_drawn = 0x0B17;
constexpr std::uint16_t command_bar = 0x16DF;

/// The load-segment words of overlay 17 (the adventuring loop) and 27 (the
/// party summary); the drawer's stub paragraph and entry.
constexpr std::uint16_t word_adventure = 0xD70;
constexpr std::uint16_t word_summary = 0x1520;
constexpr std::uint16_t summary_stub_paragraph = 0x0151;
constexpr std::uint16_t summary_stub_entry = 0x002A;

/// The adventuring loop's two calls into the bar, by the instruction after
/// each: the party's bar and its move mode's.
constexpr std::uint16_t party_bar_return = 0x092D;
constexpr std::uint16_t move_bar_return = 0x0AD1;

/// The data segment.
constexpr std::uint16_t data_game_mode = 0x67E7;
constexpr std::uint16_t data_in_transition = 0x59B5;
constexpr std::uint16_t data_area_record = 0x67C6;
constexpr std::uint16_t record_geometry_block = 0x18A;
/// The status line's square where the area's script numbers its own: the
/// column (`0xFF`: the stored square), the row and the quarter turns; and
/// the 3D word, which makes a turn two of the facing byte's steps.
constexpr std::uint16_t record_shown_column = 0x3FA;
constexpr std::uint16_t record_shown_row = 0x3FC;
constexpr std::uint16_t record_shown_turn = 0x3FE;
constexpr std::uint16_t record_3d = 0x1CC;
constexpr std::uint16_t data_disk_number = 0x7420;
constexpr std::uint16_t data_area_id = 0xA4C2;
constexpr std::uint16_t data_party_x = 0x89D7;
constexpr std::uint16_t data_party_y = 0x89D8;
constexpr std::uint16_t data_party_facing = 0x89D9;
constexpr std::uint16_t data_map_pointer = 0x89CE;
constexpr std::uint16_t data_ground_colour = 0x89D5;
constexpr std::uint16_t data_key_pushback = 0xA6E9;
constexpr std::uint16_t data_current_member = 0x7D38;
constexpr std::uint16_t data_move_mode = 0xA4D4;
constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint8_t mode_camp = 2;

constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t map_segment = 0x5000;
constexpr std::uint16_t member_segment = 0x9000;
constexpr std::uint16_t adventure_segment = 0x6E00;
constexpr std::uint16_t summary_segment = 0x7400;

constexpr std::uint16_t key_tab = 0x0F09;
constexpr std::uint16_t key_up = 0x4800;
constexpr std::uint16_t key_down = 0x5000;
constexpr std::uint16_t key_left = 0x4B00;
constexpr std::uint16_t key_home = 0x4700;
constexpr std::uint16_t key_pad_8 = 0x4838;
constexpr std::uint16_t key_pad_4 = 0x4B34;
/// What the seam answers a read with (seam_key_read.h).
constexpr std::uint16_t key_dash = 0x0C2D;

[[nodiscard]] sha256_digest secret_digest() {
  sha256_digest digest;
  EXPECT_TRUE(parse_digest(secret_fingerprint, digest));
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

struct secret_rig {
  secret_rig() : box(std::make_unique<machine>(memory_layout::pc)) {
    box->seams().loaded(secret_digest(), image_load_segment);
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

  /// The menu-bar thunk, with the far return `return_cs:return_ip` on top
  /// of the stack and a bar copied onto the stack, as every caller of this
  /// program's hands it.
  void put_up_the_bar(std::uint16_t return_cs, std::uint16_t return_ip) const {
    constexpr std::uint16_t sp = 0x0400;
    stand_on(image_load_segment, command_bar, sp);
    put_word(dgroup(), sp, return_ip);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + 2), return_cs);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + 18), 0x0300);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + 20), dgroup());
    box->step();
  }

  /// The adventuring screen: the mode, no transition, the area record with
  /// a geometry block and the status line printing the stored square, the
  /// party, a map, and the party's bar.
  void adventuring() const {
    const std::uint16_t ds = dgroup();
    put_byte(ds, data_game_mode, mode_adventure);
    put_byte(ds, data_in_transition, 0);
    put_word(ds, data_area_record, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_area_record + 2),
             record_segment);
    put_word(record_segment, record_geometry_block, 16);
    put_word(record_segment, record_shown_column, 0xFF);
    put_byte(ds, data_disk_number, 1);
    put_byte(ds, data_area_id, 16);
    put_byte(ds, data_party_x, 3);
    put_byte(ds, data_party_y, 3);
    put_byte(ds, data_party_facing, 4);
    put_word(ds, data_map_pointer, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_map_pointer + 2), map_segment);
    put_byte(ds, data_ground_colour, 0);
    put_byte(ds, data_key_pushback, 0);
    put_byte(ds, data_move_mode, 0);
    put_word(ds, data_current_member, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_current_member + 2),
             member_segment);
    put_up_the_bar(adventure_segment, party_bar_return);
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

  [[nodiscard]] bool ring_empty() const {
    return word(bda::segment, bda::keyboard_buffer_head) ==
           word(bda::segment, bda::keyboard_buffer_tail);
  }
  [[nodiscard]] std::uint16_t ring_head() const {
    return word(bda::segment, word(bda::segment, bda::keyboard_buffer_head));
  }

  void poll(unsigned times) const {
    for (unsigned i = 0; i < times; ++i) {
      stand_on(image_load_segment, key_pending);
      box->step();
    }
  }

  void read() const {
    stand_on(image_load_segment, key_read);
    box->step();
  }

  /// One of the panel's pixels.
  [[nodiscard]] std::uint8_t pixel(unsigned x, unsigned y) const {
    return box->automap()
        .pixels()[(static_cast<std::size_t>(y) * automap_panel_width) + x];
  }

  /// A region clear entered with a rect: bottom, right, top, left.
  void clear(std::uint16_t entry, std::uint8_t bottom, std::uint8_t right,
             std::uint8_t top, std::uint8_t left) const {
    stand_on(image_load_segment, entry, 0x0400);
    put_word(dgroup(), 0x0404, bottom);
    put_word(dgroup(), 0x0406, right);
    put_word(dgroup(), 0x0408, top);
    put_word(dgroup(), 0x040A, left);
    box->step();
  }

  /// The panel up at the party's bar.
  void panel_up() const {
    enable();
    adventuring();
    type(key_tab);
    poll(6);
    ASSERT_TRUE(map_state().panel_on_screen());
  }

  std::unique_ptr<machine> box;
};

// --- The definition ----------------------------------------------------------

TEST(SeamAutomapOnSecret, IsAvailableWithItsEightPoints) {
  const secret_rig r;
  const std::span<const seam_point> mine =
      points_on(definition(), secret_digest());
  ASSERT_EQ(mine.size(), 8U);
  EXPECT_NE(mine.data(), definition().points.data());
  const std::array<std::uint16_t, 8> offsets{
      key_pending,  key_read,     region_clear,     text_clear,
      screen_clear, roster_drawn, roster_row_drawn, command_bar};
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    EXPECT_EQ(mine[i].offset, offsets[i]) << i;
    EXPECT_EQ(mine[i].module.is_resident_image(), i != 5 && i != 6) << i;
  }
  EXPECT_EQ(mine[5].module.load_segment_at, word_summary);
  EXPECT_EQ(mine[6].module.load_segment_at, word_summary);
  EXPECT_EQ(r.box->seams().enable(seam_id), seam_reason::none);
}

// --- Fidelity ----------------------------------------------------------------

TEST(SeamAutomapOnSecret, OnAndTabNeverTypedTouchesNoMemory) {
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.poll(4);
  r.read();
  const std::vector<std::uint8_t> before(r.box->memory().ram().begin(),
                                         r.box->memory().ram().end());
  r.poll(8);
  r.read();
  const std::vector<std::uint8_t> after(r.box->memory().ram().begin(),
                                        r.box->memory().ram().end());
  EXPECT_EQ(before, after);
  EXPECT_FALSE(r.map_state().panel_on_screen());
}

// --- When there is a map -------------------------------------------------------

TEST(SeamAutomapOnSecret, TabDrawsThePanelOnceAGridIsUp) {
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.put_word(record_segment, record_geometry_block, 0);
  r.type(key_tab);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_open()) << "the Tab is the panel's";
  EXPECT_FALSE(r.map_state().panel_on_screen())
      << "a page of story: the program is in this mode with no grid";
  EXPECT_EQ(r.map_state().find(1, 16, 0), nullptr) << "and nothing recorded";

  r.put_word(record_segment, record_geometry_block, 16);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_on_screen());
  EXPECT_NE(r.map_state().find(1, 16, 16), nullptr);
}

// --- An area whose script numbers its squares (#609) --------------------------

/// The ruins as their script has them: area 32 with its block, the party
/// on stored square (`sx`, `sy`) facing `facing`, and the status line
/// printing (`x`, `y`) with the grid turned `turns` quarter turns, the 3D
/// word set.
void in_the_ruins(const secret_rig& r, std::uint8_t sx, std::uint8_t sy,
                  std::uint8_t facing, std::uint16_t x, std::uint16_t y,
                  std::uint16_t turns) {
  const std::uint16_t ds = secret_rig::dgroup();
  r.put_byte(ds, data_area_id, 32);
  r.put_word(record_segment, record_geometry_block, 32);
  r.put_word(record_segment, record_shown_column, x);
  r.put_word(record_segment, record_shown_row, y);
  r.put_word(record_segment, record_shown_turn, turns);
  r.put_word(record_segment, record_3d, 1);
  r.put_byte(ds, data_party_x, sx);
  r.put_byte(ds, data_party_y, sy);
  r.put_byte(ds, data_party_facing, facing);
}

/// One face of one stored square: its wall kind and its two style bits.
void face(const secret_rig& r, unsigned x, unsigned y, unsigned lane,
          std::uint8_t kind, std::uint8_t style) {
  const auto cell = static_cast<std::uint16_t>((y * 16U) + x);
  const auto faces =
      static_cast<std::uint16_t>((lane < 4 ? 0U : 0x100U) + cell);
  const bool high = lane == 0 || lane == 4;
  const std::uint8_t was = r.byte(map_segment, faces);
  r.put_byte(map_segment, faces,
             high ? static_cast<std::uint8_t>((was & 0x0FU) | (kind << 4U))
                  : static_cast<std::uint8_t>((was & 0xF0U) | kind));
  const auto styles = static_cast<std::uint16_t>(0x300U + cell);
  const std::uint8_t had = r.byte(map_segment, styles);
  r.put_byte(map_segment, styles,
             static_cast<std::uint8_t>((had & ~(3U << lane)) |
                                       (unsigned{style} << lane)));
}

TEST(SeamAutomapOnSecret, AnAreaWhoseScriptNumbersItsSquaresIsMappedInThem) {
  // The ruins' arrival: stored 11,2 facing east, the line printing 0,15.
  const secret_rig r;
  r.enable();
  r.adventuring();
  in_the_ruins(r, 11, 2, 2, 0, 15, 0);
  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.type(key_tab);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_on_screen());
  EXPECT_EQ(r.map_state().find(1, 32, 32), nullptr)
      << "no map of the stored grid";
  const automap_record* page = r.map_state().find_numbered(1, 32, 32, 0, 15);
  ASSERT_NE(page, nullptr);
  EXPECT_TRUE(automap_state::seen(*page, 0, 15));
  EXPECT_EQ(automap_state::numbered_stored_x(*page, 0, 15), 11U);
  EXPECT_EQ(automap_state::numbered_stored_y(*page, 0, 15), 2U);
  EXPECT_FALSE(automap_state::seen(*page, 1, 15))
      << "only the square stood on: sight is the stored grid's";

  // The party's mark is on the line's square, bottom-left of the page.
  constexpr unsigned x0 = 0;
  constexpr unsigned y0 = 15 * automap_cell_pixels;
  EXPECT_EQ(r.pixel(x0 + 3, y0 + 1), 15U) << "the arrow's tip";
  EXPECT_EQ(
      r.pixel((11 * automap_cell_pixels) + 3, (2 * automap_cell_pixels) + 1),
      0U)
      << "and nothing where the stored square is";

  in_the_ruins(r, 11, 1, 0, 0, 14, 0);
  r.poll(1);
  EXPECT_TRUE(automap_state::seen(*page, 0, 14)) << "a step north, one square";
}

TEST(SeamAutomapOnSecret, ItsSquareIsBelievedOnceThePartysBarIsUpOnTheMap) {
  // Between the area's load and its script, the record still holds the last
  // script's numbering and the party the square it left.
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());
  in_the_ruins(r, 15, 8, 2, 0, 15, 0);
  r.poll(6);
  EXPECT_EQ(r.map_state().find_numbered(1, 32, 32, 0, 15), nullptr)
      << "the bar has not come up here";

  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.poll(6);
  EXPECT_NE(r.map_state().find_numbered(1, 32, 32, 0, 15), nullptr);
}

TEST(SeamAutomapOnSecret, ThePortalsArrivalIsBelievedWhereTheScriptPutsIt) {
  // The mayor's portal at the town's 3,0 (#651): its question's YES loads
  // area 33 with the party still on 3,0 facing east, and the Well's script
  // prints and waits before it puts the party on 1,1 facing south. The
  // transition byte is down all the while.
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.put_byte(secret_rig::dgroup(), data_party_y, 0);
  r.put_byte(secret_rig::dgroup(), data_party_facing, 2);
  r.poll(6);
  r.put_up_the_bar(0x2000, 0x0100);  // the archway's YES NO

  r.put_byte(secret_rig::dgroup(), data_area_id, 33);
  r.put_word(record_segment, record_geometry_block, 33);
  r.poll(6);
  EXPECT_EQ(r.map_state().find(1, 33, 33), nullptr)
      << "nothing of the Well from the town's square";

  r.put_byte(secret_rig::dgroup(), data_party_x, 1);
  r.put_byte(secret_rig::dgroup(), data_party_y, 1);
  r.put_byte(secret_rig::dgroup(), data_party_facing, 4);
  r.poll(6);
  const automap_record* well = r.map_state().find(1, 33, 33);
  ASSERT_NE(well, nullptr) << "its own square, with the PRESS RETURN still up";
  EXPECT_TRUE(automap_state::seen(*well, 1, 1));
  // Behind the party facing south: what the town's square revealed, facing
  // east from 3,0 on the Well's grid.
  constexpr std::array<std::pair<unsigned, unsigned>, 3> town_squares{
      {{3, 0}, {4, 0}, {5, 0}}};
  for (const auto& [x, y] : town_squares) {
    EXPECT_FALSE(automap_state::seen(*well, x, y)) << x << ',' << y;
    EXPECT_EQ(automap_state::marker_at(*well, x, y), automap_marker::none);
  }
  EXPECT_EQ(automap_state::marker_at(*well, 1, 1), automap_marker::entrance);
  const automap_record* town = r.map_state().find(1, 16, 16);
  ASSERT_NE(town, nullptr);
  EXPECT_EQ(automap_state::marker_at(*town, 3, 0), automap_marker::exit);
}

TEST(SeamAutomapOnSecret, TheRuinsLeaveNoRecordOfTheStoredGridOnArrival) {
  // Through the gate (#651): between the ruins' load and their script the
  // line prints the stored square, the party on the town's 15,8.
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.put_byte(secret_rig::dgroup(), data_party_x, 15);
  r.put_byte(secret_rig::dgroup(), data_party_y, 8);
  r.put_byte(secret_rig::dgroup(), data_party_facing, 2);
  r.poll(6);
  r.put_up_the_bar(0x2000, 0x0100);  // the gate's YES NO
  in_the_ruins(r, 15, 8, 2, 0xFF, 0, 0);
  r.poll(6);
  in_the_ruins(r, 11, 2, 2, 0, 15, 0);
  r.poll(6);
  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.poll(6);
  EXPECT_EQ(r.map_state().find(1, 32, 32), nullptr)
      << "no record of the stored grid";
  EXPECT_NE(r.map_state().find_numbered(1, 32, 32, 0, 15), nullptr);
  EXPECT_EQ(r.map_state().records_used(), 2U) << "the town and the page";
}

TEST(SeamAutomapOnSecret,
     AFaceTheGridHasSolidIsADoorOnceTheScriptCarriesThePartyThrough) {
  // Stored 14,2's east face is a wall of kind 8 with no way through; the
  // script carries the party through it to stored 10,3, one square east in
  // its numbering.
  const secret_rig r;
  face(r, 14, 2, 2, 8, 0);
  face(r, 10, 3, 6, 7, 0);
  r.enable();
  r.adventuring();
  in_the_ruins(r, 14, 2, 2, 3, 15, 0);
  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.type(key_tab);
  r.poll(6);
  EXPECT_EQ(r.map_state().doors_drawn().walked, 0U) << "not crossed yet";

  in_the_ruins(r, 10, 3, 2, 4, 15, 0);
  r.poll(2);
  const automap_record* page = r.map_state().find_numbered(1, 32, 32, 4, 15);
  ASSERT_NE(page, nullptr);
  EXPECT_TRUE(automap_state::numbered_crossed(*page, 3, 15, 2));
  EXPECT_TRUE(automap_state::numbered_crossed(*page, 4, 15, 6));
  EXPECT_EQ(r.map_state().doors_drawn().walked, 2U)
      << "the border, drawn from each side of it";
}

TEST(SeamAutomapOnSecret, APieceTheScriptTurnsIsDrawnTurned) {
  // Stored 11,4 facing south, the grid turned half round: the line prints
  // 13,19 facing north, and so does the panel.
  const secret_rig r;
  r.enable();
  r.adventuring();
  in_the_ruins(r, 11, 4, 4, 13, 19, 2);
  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.type(key_tab);
  r.poll(6);
  ASSERT_TRUE(r.map_state().panel_on_screen());
  const automap_record* page = r.map_state().find_numbered(1, 32, 32, 13, 19);
  ASSERT_NE(page, nullptr);
  EXPECT_EQ(automap_state::numbered_turns(*page, 13, 19), 2U);
  constexpr unsigned x0 = 13 * automap_cell_pixels;
  constexpr unsigned y0 = 3 * automap_cell_pixels;
  // The arrow's second row is three wide pointing north, one pointing south.
  EXPECT_EQ(r.pixel(x0 + 2, y0 + 2), 15U) << "an arrow pointing north";
}

TEST(SeamAutomapOnSecret, TheFloorIsDarkGreyWhereTheGroundByteIsNotSet) {
  const secret_rig r;
  r.panel_up();
  constexpr unsigned party_x0 = 3 * automap_cell_pixels;
  constexpr unsigned party_y0 = 3 * automap_cell_pixels;
  EXPECT_EQ(r.pixel(party_x0, party_y0), 8U)
      << "the program draws its ground from tiles and never sets the byte";
}

// --- The party's bars ----------------------------------------------------------

TEST(SeamAutomapOnSecret, ThePartysBarsAreTheLoopsTwoCalls) {
  const secret_rig r;
  r.enable();
  r.adventuring();
  EXPECT_TRUE(r.map_state().at_command_bar()) << "the party's bar";

  r.put_up_the_bar(0x2000, 0x0100);
  EXPECT_FALSE(r.map_state().at_command_bar()) << "somebody else's copy";

  r.put_up_the_bar(adventure_segment, move_bar_return);
  EXPECT_TRUE(r.map_state().at_command_bar()) << "the move mode's bar";

  r.put_up_the_bar(adventure_segment, 0x0AD0);
  EXPECT_FALSE(r.map_state().at_command_bar()) << "not a call of the two";

  r.put_up_the_bar(0x2000, party_bar_return);
  EXPECT_FALSE(r.map_state().at_command_bar())
      << "the right offset in a segment the adventure overlay is not at";
}

/// Where the stand-in frames go, in the data segment the rig's stack is in.
constexpr std::uint16_t bar_frame = 0x0500;
constexpr std::uint16_t inner_frame = 0x0480;

/// A frame of the program's: its caller's `BP` at `[bp]` and its far
/// return above it.
void put_frame(const secret_rig& r, std::uint16_t bp, std::uint16_t caller,
               std::uint16_t return_cs, std::uint16_t return_ip) {
  r.put_word(secret_rig::dgroup(), bp, caller);
  r.put_word(secret_rig::dgroup(), static_cast<std::uint16_t>(bp + 2),
             return_ip);
  r.put_word(secret_rig::dgroup(), static_cast<std::uint16_t>(bp + 4),
             return_cs);
}

TEST(SeamAutomapOnSecret, SwitchedOnInsideTheMoveModesBarTabAnswersAtOnce) {
  // The poll is the key wrapper's, whose frame is between the bar's and
  // the poll; the bar is the move mode's.
  const secret_rig r;
  r.adventuring();
  put_frame(r, bar_frame, 0, adventure_segment, move_bar_return);
  put_frame(r, inner_frame, bar_frame, 0x4336, 0x0599);
  r.enable();
  r.type(key_tab);
  for (int i = 0; i < 6; ++i) {
    r.stand_on(image_load_segment, key_pending);
    r.regs()[cpu::reg16::bp] = inner_frame;
    r.box->step();
  }
  EXPECT_TRUE(r.map_state().at_command_bar());
  EXPECT_TRUE(r.map_state().panel_on_screen());
}

// --- The bar's keys ------------------------------------------------------------

TEST(SeamAutomapOnSecret, UpAndDownAreRosterStepsAtThePartysBar) {
  // The party's bar does not move the party; Up and Down step the current
  // member, and Left and Right move the bar's lit word.
  const secret_rig r;
  r.panel_up();
  for (const std::uint16_t key : {key_up, key_down, key_home, key_pad_8}) {
    r.type(key);
    r.poll(1);
    EXPECT_TRUE(r.ring_empty()) << key;
  }
  for (const std::uint16_t key : {key_left, key_pad_4}) {
    r.type(key);
    r.poll(1);
    EXPECT_FALSE(r.ring_empty()) << key << " is the bar's";
    EXPECT_EQ(r.ring_head(), key);
    // The program reads it.
    r.put_word(bda::segment, bda::keyboard_buffer_head,
               r.word(bda::segment, bda::keyboard_buffer_tail));
  }
}

TEST(SeamAutomapOnSecret, InTheProgramsMoveModeNoKeyButTabIsTheSeams) {
  const secret_rig r;
  r.panel_up();
  r.put_byte(secret_rig::dgroup(), data_move_mode, 1);
  r.put_up_the_bar(adventure_segment, move_bar_return);
  r.type(key_up);
  r.poll(2);
  EXPECT_FALSE(r.ring_empty()) << "Up is a step";
  EXPECT_EQ(r.ring_head(), key_up);
}

// --- The read after the second poll --------------------------------------------

TEST(SeamAutomapOnSecret, AReadStraightAfterAPollThatTookTheKeyIsAnswered) {
  // The key wrapper polls and, told no, reads anyway: the read would wait
  // in the BIOS for the next key, which nothing would then see.
  const secret_rig r;
  r.panel_up();
  r.type(key_home);
  r.poll(1);
  ASSERT_TRUE(r.ring_empty());
  r.read();
  ASSERT_FALSE(r.ring_empty()) << "answered";
  EXPECT_EQ(r.ring_head(), key_dash);
}

TEST(SeamAutomapOnSecret, AReadAfterAnIdlePollWaits) {
  const secret_rig r;
  r.panel_up();
  r.type(key_home);
  r.poll(1);
  r.poll(1);
  r.read();
  EXPECT_TRUE(r.ring_empty()) << "a poll in between said the key was gone";
}

// --- The give-back -------------------------------------------------------------

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

void place_the_stand_ins(const secret_rig& r) {
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
  r.put_word(secret_rig::dgroup(), stub_witness, 0);
}

TEST(SeamAutomapOnSecret, ClosingItCallsTheDrawerAtItsStubsOwnSegment) {
  const secret_rig r;
  r.panel_up();
  place_the_stand_ins(r);

  r.type(key_tab);
  r.stand_on(image_load_segment, key_pending);
  const cpu::registers before = r.regs();
  r.box->step();
  EXPECT_FALSE(r.map_state().panel_open());
  for (int i = 0; i < 40; ++i) {
    r.box->step();
  }
  EXPECT_EQ(r.word(secret_rig::dgroup(), stub_witness),
            image_load_segment + summary_stub_paragraph)
      << "the drawer's stub ran at the paragraph its record begins at (#526)";
  EXPECT_EQ(r.regs()[cpu::sreg::cs], before[cpu::sreg::cs]);
  EXPECT_EQ(r.regs()[cpu::reg16::sp], before[cpu::reg16::sp]);
}

TEST(SeamAutomapOnSecret, TheCampIsNotThePartysBar) {
  const secret_rig r;
  r.panel_up();
  place_the_stand_ins(r);
  r.put_byte(secret_rig::dgroup(), data_game_mode, mode_camp);
  r.put_up_the_bar(0x2000, 0x0100);
  r.poll(1);
  for (int i = 0; i < 40; ++i) {
    r.box->step();
  }
  EXPECT_EQ(r.word(secret_rig::dgroup(), stub_witness),
            image_load_segment + summary_stub_paragraph)
      << "the roster given back for the camp's bar";
  EXPECT_TRUE(r.map_state().panel_open()) << "off the screen, not closed";
}

// --- Who owns the cells ----------------------------------------------------------

TEST(SeamAutomapOnSecret, BothRegionClearsCoverAndBothRosterReturnsUncover) {
  const secret_rig r;
  r.enable();
  r.adventuring();
  for (const std::uint16_t entry : {region_clear, text_clear}) {
    r.map_state().set_panel_covered(false);
    r.clear(entry, 0x0E, 0x26, 0x02, 0x11);
    EXPECT_TRUE(r.map_state().panel_covered()) << entry;
  }
  r.stand_on(summary_segment, roster_drawn);
  r.box->step();
  EXPECT_FALSE(r.map_state().panel_covered()) << "the drawer's return";

  r.stand_on(image_load_segment, screen_clear);
  r.box->step();
  EXPECT_TRUE(r.map_state().panel_covered());
}

TEST(SeamAutomapOnSecret, ACursorStepWithThePanelDownLeavesTabItsPanel) {
  // The program redraws one row for a cursor step and clears the row after
  // the party, inside the panel's rect; its return is a roster on the
  // screen, and the next Tab is drawn.
  const secret_rig r;
  r.enable();
  r.adventuring();
  r.clear(region_clear, 0x0A, 0x26, 0x0A, 0x11);
  ASSERT_TRUE(r.map_state().panel_covered());
  r.stand_on(summary_segment, roster_row_drawn);
  r.box->step();
  EXPECT_FALSE(r.map_state().panel_covered());
  r.type(key_tab);
  r.poll(6);
  EXPECT_TRUE(r.map_state().panel_on_screen());
}

}  // namespace
}  // namespace amberfolio::machine
