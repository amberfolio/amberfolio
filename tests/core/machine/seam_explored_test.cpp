// SPDX-License-Identifier: AGPL-3.0-only
//
// The explored overlay (seam_explored.cpp, M5-E5 #179, the fog M5-E5f
// #263), exercised through its mechanism and not through any program.
//
// **The screen is filled before anything is asserted**, which is new with
// the fog: this seam's drawing is black, and black on a blank screen is
// no picture at all. So `fill()` paints every plane of the whole frame
// white first, the way the program's own composition would leave the
// window painted, and what the assertions read is where that went back to
// black.
//
// Two halves, and the split is `seam_automap_test.cpp`'s: the geometry is
// a pure function and is checked against arithmetic, and the seam is
// checked by laying a data segment and an area record out the way the
// facts say the program lays them out, standing the processor on an
// interception point, and looking at what reached the planes.
//
// The offsets below are restated rather than read out of the seam, which
// is this suite's rule and the reason for it: a test that took its layout
// from the code it is checking would be agreeing with itself. The
// interception addresses *are* read from the definition, because those
// are the mechanism rather than the layout. Every byte here is this
// file's own (PLAN.md §6).

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/sha256.h"
#include "gtest/gtest.h"
#include "machine/test_device.h"

namespace amberfolio::machine {
namespace {

// --- The facts this test lays memory out by --------------------------------

constexpr std::uint32_t dgroup_offset = 0xC7C0;

constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint16_t data_in_transition = 0x442F;
constexpr std::uint16_t data_area_record = 0x49D2;
constexpr std::uint16_t data_disk_number = 0x5376;
constexpr std::uint16_t data_area_id = 0x84DC;
constexpr std::uint16_t data_view_column_bias = 0x3C76;
constexpr std::uint16_t data_menu_area_view = 0x04B6;
constexpr std::uint16_t bar_frame_menu_offset = 18;
constexpr std::uint16_t bar_frame_menu_segment = 20;

constexpr std::uint16_t record_overland_column = 0x186;
constexpr std::uint16_t record_overland_row = 0x188;
constexpr std::uint16_t record_shown_in_3d = 0x1CC;

constexpr std::uint8_t mode_overland = 3;
constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint8_t view_kind_overland = 2;
constexpr std::uint8_t view_kind_area = 1;

/// The wilderness area the driven run in `docs/explored-overlay.md` §4
/// reached: view kind 2, disk 6, area 0x19, and a column bias of zero.
constexpr std::uint8_t overland_disk = 6;
constexpr std::uint8_t overland_area = 0x19;
constexpr std::uint8_t overland_bias = 0;

/// Where the test puts the area record: a segment of its own, so a
/// handler that followed the wrong pointer gives a wrong answer rather
/// than a lucky one.
constexpr std::uint16_t record_segment = 0x4000;

/// The window's rect, restated. `docs/explored-overlay.md` §3 measured
/// it off a real frame: 120 by 120 pixels at (8, 8), five cells of 24.
constexpr unsigned window_x = 8;
constexpr unsigned window_y = 8;
constexpr unsigned cell_pixels = 24;
constexpr unsigned window_cells = 5;

/// A five-byte program that does nothing but let steps happen:
/// `MOV AX,1111h ; NOP ; HLT`. The seam's points are reached because the
/// test puts CS:IP on them, not because this program goes there.
constexpr std::array<std::uint8_t, 5> idle_program{0xB8, 0x11, 0x11, 0x90,
                                                   0xF4};

struct rig {
  rig() : box(std::make_unique<machine>(memory_layout::pc, &log)) {
    sha256_digest baseline;
    EXPECT_TRUE(parse_digest(known_editions().front().fingerprint, baseline));
    box->seams().loaded(baseline, image_load_segment);
    attach_video();
  }

  [[nodiscard]] machine& pc() const noexcept { return *box; }
  [[nodiscard]] cpu::registers& regs() const noexcept {
    return box->processor().regs();
  }
  [[nodiscard]] automap_state& map_state() const noexcept {
    return box->automap();
  }

  [[nodiscard]] static std::uint16_t dgroup() noexcept {
    return static_cast<std::uint16_t>(image_load_segment +
                                      (dgroup_offset / 16));
  }

  void enable() const {
    ASSERT_EQ(box->seams().enable("explored"), seam_reason::none);
  }

  /// The physical address of one of the seam's points, by index into the
  /// definition — read from the definition, because that is the mechanism
  /// under test. 0 is the present's return, 1 the keyboard poll, 2 the
  /// menu-bar thunk.
  [[nodiscard]] std::uint32_t point(std::size_t which) const {
    const seam_definition* found = box->seams().find("explored");
    EXPECT_NE(found, nullptr);
    EXPECT_LT(which, found->points.size());
    return cpu::physical_address(image_load_segment, 0) +
           found->points[which].offset;
  }

  [[nodiscard]] std::uint8_t byte_at(std::uint16_t segment,
                                     std::uint16_t offset) const {
    return box->memory().ram()[cpu::physical_address(segment, offset)];
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

  /// Put a HLT at a point's address and stand the processor on it, with a
  /// stack and the program's own data segment. Stepping then runs the
  /// handler and, if it returns, the HLT.
  void stand_on(std::uint32_t address, std::uint16_t sp = 0x0400) const {
    box->memory().ram()[address] = 0xF4;
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = image_load_segment;
    r.ip = static_cast<std::uint16_t>(
        address - cpu::physical_address(image_load_segment, 0));
    r[cpu::sreg::ds] = dgroup();
    r[cpu::sreg::ss] = dgroup();
    r[cpu::reg16::sp] = sp;
  }

  /// The wilderness travel view, as far as the seam's guards can see it.
  void travelling(std::uint8_t column, std::uint8_t row,
                  std::uint8_t view_kind = view_kind_overland) const {
    const std::uint16_t ds = dgroup();
    put_byte(ds, data_game_mode, mode_overland);
    put_byte(ds, data_view_kind, view_kind);
    put_byte(ds, data_in_transition, 0);
    put_word(ds, data_area_record, 0x0000);
    put_word(ds, static_cast<std::uint16_t>(data_area_record + 2),
             record_segment);
    put_word(record_segment, record_overland_column, column);
    put_word(record_segment, record_overland_row, row);
    put_word(record_segment, record_shown_in_3d, 0);
    put_byte(ds, data_disk_number, overland_disk);
    put_byte(ds, data_area_id, overland_area);
    put_byte(ds, static_cast<std::uint16_t>(data_view_column_bias + view_kind),
             overland_bias);
    put_up_the_bar(dgroup(), data_menu_area_view);
  }

  /// Put a command bar up, the way the program does: stand on the bar
  /// point with the menu's far pointer where its caller's frame would
  /// have it, and step. Driven through the point rather than by setting a
  /// flag, because the point *is* the mechanism.
  void put_up_the_bar(std::uint16_t segment, std::uint16_t offset) const {
    constexpr std::uint16_t sp = 0x0400;
    stand_on(point(2), sp);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + bar_frame_menu_offset),
             offset);
    put_word(dgroup(), static_cast<std::uint16_t>(sp + bar_frame_menu_segment),
             segment);
    box->step();
  }

  /// Somebody other than the adventuring screen asks the player
  /// something: its bar is a string built on the stack, which is what
  /// makes it not the party's.
  void somebody_else_asks() const { put_up_the_bar(dgroup(), 0x03F0); }

  /// Step the keyboard-poll point `times` times. Three is what it takes
  /// for a position to be believed.
  void poll(unsigned times) const {
    for (unsigned i = 0; i < times; ++i) {
      stand_on(point(1));
      box->step();
    }
  }

  /// Step the present's return once, which is where the overlay draws.
  void present() const {
    stand_on(point(0));
    box->step();
  }

  /// Paint every plane of the whole frame, the way the program's own
  /// composition leaves the window painted. Written through the bus, as a
  /// program would write it, so what comes back afterwards is the seam's
  /// work over a screen that had something on it.
  ///
  /// `0xFF` on four planes is colour 15, and this seam's drawing is
  /// colour 0 — so a fogged pixel is unmistakable and so is one that was
  /// left alone.
  void paint_screen(std::uint8_t value) const {
    cpu::processor& cpu = box->processor();
    // A plain write-mode-0 copy to every plane. Set rather than assumed,
    // because the seam sets them too and a helper that inherited its
    // state would be reading its own leftovers.
    box->write_port8(ega::graphics_index_port, 1);
    box->write_port8(ega::graphics_data_port, 0);
    box->write_port8(ega::graphics_index_port, 3);
    box->write_port8(ega::graphics_data_port, 0);
    box->write_port8(ega::graphics_index_port, 5);
    box->write_port8(ega::graphics_data_port, 0);
    box->write_port8(ega::graphics_index_port, 8);
    box->write_port8(ega::graphics_data_port, 0xFF);
    box->write_port8(ega::sequencer_index_port, 2);
    box->write_port8(ega::sequencer_data_port, 0x0F);
    for (unsigned line = 0; line < 200; ++line) {
      for (unsigned byte = 0; byte < 40; ++byte) {
        cpu.write_byte(0xA000, static_cast<std::uint16_t>((line * 40) + byte),
                       value);
      }
    }
  }

  /// The screen as the program would have left it: everything lit.
  void fill() const { paint_screen(0xFF); }

  /// The screen with nothing on it.
  void wipe() const { paint_screen(0x00); }

  /// One pixel of the screen, out of the planes.
  [[nodiscard]] std::uint8_t screen_pixel(unsigned x, unsigned y) const {
    const auto offset = static_cast<std::uint16_t>((y * 40U) + (x / 8U));
    const unsigned shift = 7U - (x % 8U);
    std::uint8_t colour = 0;
    for (unsigned plane = 0; plane < ega::plane_count; ++plane) {
      const std::uint8_t bits = video->plane_byte(plane, offset);
      colour =
          static_cast<std::uint8_t>(colour | (((bits >> shift) & 1U) << plane));
    }
    return colour;
  }

  /// The EGA, attached so there is something for a plane write to reach.
  void attach_video() {
    video = std::make_unique<ega>(*box);
    box->attach(*video);
  }

  test::recording_diagnostics log;
  std::unique_ptr<machine> box;
  std::unique_ptr<ega> video;
};

/// Is every pixel of one cell of the window black — which is what this
/// seam's fog is, and nothing else on this screen is once `fill()` has
/// run.
[[nodiscard]] bool cell_is_fogged(const rig& r, unsigned column, unsigned row) {
  for (unsigned y = 0; y < cell_pixels; ++y) {
    for (unsigned x = 0; x < cell_pixels; ++x) {
      if (r.screen_pixel(window_x + (column * cell_pixels) + x,
                         window_y + (row * cell_pixels) + y) != 0) {
        return false;
      }
    }
  }
  return true;
}

/// The opposite, and not the negation of it: every pixel of the cell is
/// still the colour the fill put there. A cell that was half covered
/// would fail both.
[[nodiscard]] bool cell_is_clear(const rig& r, unsigned column, unsigned row) {
  for (unsigned y = 0; y < cell_pixels; ++y) {
    for (unsigned x = 0; x < cell_pixels; ++x) {
      if (r.screen_pixel(window_x + (column * cell_pixels) + x,
                         window_y + (row * cell_pixels) + y) != 0x0F) {
        return false;
      }
    }
  }
  return true;
}

/// Which of the twenty-five cells of the window are fogged, as a bitmap
/// of `row * 5 + column` — the same numbering the seam uses. Asserted as
/// a whole, so a cell that should have been covered and was not is as
/// much a failure as one that should not have been and was.
[[nodiscard]] std::uint32_t fogged_cells(const rig& r) {
  std::uint32_t fogged = 0;
  for (unsigned row = 0; row < window_cells; ++row) {
    for (unsigned column = 0; column < window_cells; ++column) {
      const bool black = cell_is_fogged(r, column, row);
      EXPECT_TRUE(black || cell_is_clear(r, column, row))
          << "cell " << column << "," << row << " is neither covered nor "
          << "untouched";
      if (black) {
        fogged |= 1U << ((row * window_cells) + column);
      }
    }
  }
  return fogged;
}

/// Every cell of the window, as that bitmap.
constexpr std::uint32_t every_cell = (1U << (window_cells * window_cells)) - 1U;

/// The rectangle of window cells `[first_column, last_column] x
/// [first_row, last_row]`, which is the shape a reveal radius makes on
/// this screen: the party's own three-by-three, grown by a step of the
/// walk and clipped by the map's own edges.
[[nodiscard]] constexpr std::uint32_t block(unsigned first_column,
                                            unsigned last_column,
                                            unsigned first_row,
                                            unsigned last_row) {
  std::uint32_t mask = 0;
  for (unsigned row = first_row; row <= last_row; ++row) {
    for (unsigned column = first_column; column <= last_column; ++column) {
      mask |= 1U << ((row * window_cells) + column);
    }
  }
  return mask;
}

/// How many pixels of the whole 320 by 200 screen are black.
[[nodiscard]] std::size_t pixels_covered(const rig& r) {
  std::size_t covered = 0;
  for (unsigned y = 0; y < 200; ++y) {
    for (unsigned x = 0; x < 320; ++x) {
      if (r.screen_pixel(x, y) == 0) {
        ++covered;
      }
    }
  }
  return covered;
}

// ---------------------------------------------------------------------------
// The geometry, on its own
// ---------------------------------------------------------------------------

TEST(ExploredGeometry, TheWindowFollowsThePartyAndStopsAtTheEdges) {
  // In open country the party is the middle cell of the five: the window
  // starts two cells before it, in both directions.
  EXPECT_EQ(explored_window_top_left(0, 3, 32).col, 1);
  EXPECT_EQ(explored_window_top_left(0, 3, 32).row, 30);

  // Against the top and left the window cannot back up any further, and
  // the party is the first or second cell instead.
  EXPECT_EQ(explored_window_top_left(0, 0, 0).col, 0);
  EXPECT_EQ(explored_window_top_left(0, 0, 0).row, 0);
  EXPECT_EQ(explored_window_top_left(0, 1, 1).col, 0);
  EXPECT_EQ(explored_window_top_left(0, 1, 1).row, 0);

  // Against the bottom: the last row of a 36-row map is 35, and the
  // window's own top-left row is clamped at 0x1F, so the party is the
  // fifth cell rather than the third.
  EXPECT_EQ(explored_window_top_left(0, 3, 33).row, 31);
  EXPECT_EQ(explored_window_top_left(0, 3, 34).row, 31);
  EXPECT_EQ(explored_window_top_left(0, 3, 35).row, 31);

  // The bias puts each area in its own band of the one 44-column table,
  // and the column clamp is that table's, not the area's.
  EXPECT_EQ(explored_window_top_left(13, 3, 10).col, 14);
  EXPECT_EQ(explored_window_top_left(26, 0, 10).col, 24);
  EXPECT_EQ(explored_window_top_left(26, 15, 10).col, 0x27);
  EXPECT_EQ(explored_window_top_left(26, 14, 10).col, 0x26);
}

// ---------------------------------------------------------------------------
// The seam
// ---------------------------------------------------------------------------

TEST(ExploredOverlay, ArrivingCoversEverythingButTheThreeByThreeAround) {
  // The picture a player meets: the party has just arrived, the only
  // cell it has stood on is the one under its feet, and at a reveal
  // radius of one the three-by-three around that is the country it can
  // see. Everything else in the window goes under fog.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  ASSERT_NE(r.map_state().find_overland(overland_disk, overland_area), nullptr)
      << "it was recorded on arrival";
  r.fill();
  r.present();

  // The party is at (3, 32), so the window's top-left cell is (1, 30) and
  // the party is window column 2, row 2. The reveal is map columns 2..4
  // and rows 31..33, which is window columns 1..3 and rows 1..3.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 3, 1, 3));
  // Sixteen cells of 24 by 24, and not a pixel of the rest of the frame.
  EXPECT_EQ(pixels_covered(r), 16U * cell_pixels * cell_pixels);
}

TEST(ExploredOverlay, AStepUncoversTheRowTheWindowScrolledOnto) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  // North one square. The window scrolls with the party, and what the
  // party can now see is the union of the two three-by-threes.
  r.put_word(record_segment, record_overland_row, 31);
  r.poll(2);
  r.fill();
  r.present();

  // The party is at (3, 31): the window's top-left is (1, 29), the reveal
  // is map columns 2..4 and rows 30..33, which is window columns 1..3 and
  // rows 1..4. The row ahead of the party — window row 0 — is still fog.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 3, 1, 4));
}

TEST(ExploredOverlay, ThePartysOwnCellIsNeverCovered) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();

  // (3, 32) is where the party is standing and where the program draws
  // its icon: window column 2, row 2.
  EXPECT_TRUE(cell_is_clear(r, 2, 2));
  for (unsigned y = 0; y < cell_pixels; ++y) {
    for (unsigned x = 0; x < cell_pixels; ++x) {
      ASSERT_EQ(r.screen_pixel(window_x + (2 * cell_pixels) + x,
                               window_y + (2 * cell_pixels) + y),
                0x0F)
          << "not one pixel of it";
    }
  }
}

TEST(ExploredOverlay, NothingOutsideTheWindowIsTouched) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();

  for (unsigned y = 0; y < 200; ++y) {
    for (unsigned x = 0; x < 320; ++x) {
      if (x >= window_x && x < window_x + (window_cells * cell_pixels) &&
          y >= window_y && y < window_y + (window_cells * cell_pixels)) {
        continue;
      }
      ASSERT_EQ(r.screen_pixel(x, y), 0x0F) << "at " << x << "," << y;
    }
  }
}

TEST(ExploredOverlay, TheDilationIsClippedAtTheMapsOwnEdge) {
  // The party against the bottom of a 36-row map. The window cannot
  // scroll any further, so the party is its *fifth* row rather than its
  // third, and the reveal has no row below it to include.
  const rig r;
  r.enable();
  r.travelling(3, 35);
  r.poll(4);
  r.fill();
  r.present();

  // The window's top-left is (1, 31), so the party — (3, 35) — is window
  // column 2, row 4. The reveal is map columns 2..4 and rows 34..35,
  // which is window columns 1..3 and rows 3..4.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 3, 3, 4));
}

TEST(ExploredOverlay, ACellOfANeighbouringAreaIsCovered) {
  // The three wilderness areas are three sixteen-column bands of one
  // 44-column table, so the window can overhang into a neighbour's
  // columns. This seam has no record for those, so nothing has been
  // stood near them and they are covered like any other unknown — which
  // is the reversal: with a lift, marking them would have claimed
  // knowledge; with a fog, *not* covering them would.
  const rig r;
  r.enable();
  r.travelling(15, 20);
  r.poll(4);
  r.fill();
  r.present();

  // The party is at column 15, so the window's top-left column is 13 and
  // it covers columns 13..17 — 16 and 17 belong to the next area. The
  // reveal is map columns 14..15 (16 is off this map) and rows 19..21,
  // which is window columns 1..2 and rows 1..3.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 2, 1, 3));
  EXPECT_TRUE(cell_is_fogged(r, 3, 2)) << "the neighbour's column 16";
  EXPECT_TRUE(cell_is_fogged(r, 4, 2)) << "and its column 17";
}

TEST(ExploredOverlay, AWellWalkedStretchOfCountryIsNotCoveredAtAll) {
  // The other end of the same rule, and the case that keeps the early
  // return honest: with every cell of the window within a step of
  // somewhere the party has stood, there is nothing to cover and not a
  // port is written.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  automap_record& map =
      r.map_state().record_for_overland(overland_disk, overland_area);
  for (unsigned column = 0; column < 8; ++column) {
    for (unsigned row = 28; row < 36; ++row) {
      (void)r.map_state().reveal(map, column, row);
    }
  }
  r.poll(2);
  r.fill();
  r.present();
  EXPECT_EQ(fogged_cells(r), 0u);
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, NothingIsDrawnOffTheTravelView) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_GT(pixels_covered(r), 0u) << "it was drawing a moment ago";
  r.fill();
  ASSERT_EQ(pixels_covered(r), 0u);

  r.put_byte(rig::dgroup(), data_game_mode, mode_adventure);
  r.put_byte(rig::dgroup(), data_view_kind, view_kind_area);
  r.poll(2);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, NothingIsDrawnWhileSomebodyElseAsksThePlayer) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  const std::size_t covered = pixels_covered(r);
  ASSERT_GT(covered, 0u) << "it was drawing a moment ago";
  r.fill();
  ASSERT_EQ(pixels_covered(r), 0u);

  r.somebody_else_asks();
  r.poll(2);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u)
      << "a script's portrait is not this seam's to paint over";

  // And it comes back when the party's own bar does.
  r.put_up_the_bar(rig::dgroup(), data_menu_area_view);
  r.present();
  EXPECT_EQ(pixels_covered(r), covered);
}

TEST(ExploredOverlay, NothingIsDrawnWhileTheAreaIsShownInTheInteriorView) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_GT(pixels_covered(r), 0u) << "it was drawing a moment ago";
  r.fill();

  // The word in the area record that makes the program draw this area in
  // 3D leaves the view-kind byte reading 2. A seam that trusted the kind
  // byte would fog the interior view.
  r.put_word(record_segment, record_shown_in_3d, 1);
  r.poll(2);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, NothingIsDrawnWhileTheProgramIsMovingTheParty) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_GT(pixels_covered(r), 0u) << "it was drawing a moment ago";
  r.fill();

  r.put_byte(rig::dgroup(), data_in_transition, 1);
  r.poll(2);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, AWildAreaRecordIsNotFollowed) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_GT(pixels_covered(r), 0u) << "it was drawing a moment ago";
  r.fill();

  // The video window. Reading it would load the adapter's latches, which
  // is a seam changing the machine in order to look at it.
  r.put_word(rig::dgroup(), static_cast<std::uint16_t>(data_area_record + 2),
             0xA000);
  r.poll(2);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, ABiasNoTableCouldHoldIsDeclined) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_GT(pixels_covered(r), 0u) << "it was drawing a moment ago";
  r.fill();

  r.put_byte(
      rig::dgroup(),
      static_cast<std::uint16_t>(data_view_column_bias + view_kind_overland),
      0xF0);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
  EXPECT_GT(r.pc().seams().status("explored").declined, 0u);
}

TEST(ExploredOverlay, WithTheSeamOffNothingIsDrawnAtAll) {
  const rig r;  // not enabled
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
  EXPECT_EQ(r.map_state().records_used(), 0u)
      << "and nothing was recorded either";
}

TEST(ExploredOverlay, ATableThatArrivesUnderAStandingPartyIsDrawn) {
  // The finding a driven run made, as a test (M5-E5d, #256). A seam that
  // painted only where the program paints could not show this: a party
  // that loads a saved game and stands still gives the program nothing
  // to redraw, so no present ever comes, and the map the host had just
  // read in beside the save stayed unshown until the player moved.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  ASSERT_EQ(fogged_cells(r), every_cell & ~block(1, 3, 1, 3))
      << "nothing stood on but its own square";

  // What a host does when the program loads a save slot: it replaces the
  // table, which moves the store's serial. (3, 33) is one square south,
  // and at a radius of one it uncovers the window's bottom row of three.
  automap_record& map =
      r.map_state().record_for_overland(overland_disk, overland_area);
  ASSERT_TRUE(r.map_state().reveal(map, 3, 33));
  r.fill();
  r.poll(1);
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 3, 1, 4))
      << "and no present was needed to show it";
}

TEST(ExploredOverlay, APollThatChangesNothingRepaintsNothing) {
  // The other half of the same decision: the keyboard poll is reached
  // thousands of times a virtual second, so it repaints only when
  // something has moved.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  const std::size_t covered = pixels_covered(r);
  ASSERT_GT(covered, 0u);
  r.fill();
  r.poll(8);
  EXPECT_EQ(pixels_covered(r), 0u) << "nothing moved, so nothing was drawn";

  // And the program's own present says the screen was repainted under
  // it, which is what brings the fog back.
  r.present();
  EXPECT_EQ(pixels_covered(r), covered);
}

TEST(ExploredOverlay, ItRecordsWhereThePartyStoodAndNotWhatItCouldSee) {
  // The recorder is shared with the automap (#254), and this seam calls
  // it from its own tick — so a player with only this one on has a
  // trail. What it stores is the cell the party stood on and nothing
  // derived from it: the reveal radius is applied when the window is
  // drawn, which is what lets the knob move without invalidating a
  // sidecar (`automap.h`).
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  const automap_record* map =
      r.map_state().find_overland(overland_disk, overland_area);
  ASSERT_NE(map, nullptr);
  EXPECT_EQ(map->kind, automap_map_kind::overland);
  EXPECT_TRUE(automap_state::seen(*map, 3, 32));
  EXPECT_FALSE(automap_state::seen(*map, 3, 31));
}

// ---------------------------------------------------------------------------
// The fidelity claim (docs/seams.md §8.5)
// ---------------------------------------------------------------------------

TEST(ExploredFidelity, OnAndTheOverworldNeverShownLeavesTheRunIdentical) {
  const auto run = [](bool on) {
    const rig r;
    if (on) {
      EXPECT_EQ(r.pc().seams().enable("explored"), seam_reason::none);
      EXPECT_TRUE(r.pc().seams().armed());
    }
    // The interior adventuring screen, which is not this seam's.
    r.put_byte(rig::dgroup(), data_game_mode, mode_adventure);
    r.put_byte(rig::dgroup(), data_view_kind, view_kind_area);

    const std::uint32_t at = r.point(0);
    for (std::size_t i = 0; i < idle_program.size(); ++i) {
      r.pc().memory().ram()[at + i] = idle_program[i];
    }
    r.stand_on(at);
    r.pc().memory().ram()[at] = idle_program[0];
    for (int i = 0; i < 4; ++i) {
      r.pc().step();
    }
    return hash_state(r.pc());
  };

  EXPECT_EQ(run(false), run(true));
}

TEST(ExploredFidelity, TheArrivalIsNoLongerTheScreenItWouldHaveBeen) {
  // **The claim M5-E5c had and M5-E5f gives up**, asserted in the
  // direction it now holds. A lift marks what is known, and a map nobody
  // has walked has nothing known on it, so arriving used to be
  // pixel-identical to the seam-off run. A fog marks what is *not*
  // known, and a map nobody has walked is nearly all of that: the outer
  // ring of the window is covered the moment the party gets there.
  // Saying so as a test is what keeps a later change from quietly
  // re-acquiring a claim this enhancement can no longer make
  // (docs/seams.md §8.5).
  const auto run = [](bool on) {
    const rig r;
    if (on) {
      EXPECT_EQ(r.pc().seams().enable("explored"), seam_reason::none);
    }
    r.travelling(3, 32);
    r.poll(4);
    r.fill();
    r.present();
    return hash_state(r.pc());
  };

  EXPECT_NE(run(false), run(true));
}

TEST(ExploredFidelity, WhatItLearnsIsNotMachineState) {
  // The store is observation and not machine state (`automap.h`), and
  // this says so on the one path where the seam records without drawing:
  // somebody other than the adventuring screen is asking the player
  // something, so the trail is written down and no pixel is.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.somebody_else_asks();
  r.poll(4);
  ASSERT_GT(r.map_state().records_used(), 0u);

  const rig plain;
  plain.travelling(3, 32);
  plain.somebody_else_asks();
  plain.poll(4);
  EXPECT_EQ(plain.map_state().records_used(), 0u);
  EXPECT_EQ(hash_state(r.pc()), hash_state(plain.pc()));
}

TEST(ExploredDefinition, TheRevealRadiusIsOne) {
  // The one number in this enhancement a person is meant to turn, so a
  // change to it fails here, by name, rather than in nine expectations
  // that each look like a wrong picture (M5-E5f, #263). One is also the
  // only value that covers anything on a five-by-five window centred on
  // the party: at two, every cell on the screen is already within the
  // radius and the fog never appears except at a map's own edge.
  EXPECT_EQ(explored_reveal_radius, 1);
}

TEST(ExploredDefinition, ItIsASettingAndNotAPullAndItNamesTheBaseline) {
  const rig r;
  const seam_definition* explored = r.pc().seams().find("explored");
  ASSERT_NE(explored, nullptr);
  EXPECT_FALSE(explored->trigger) << "a setting is not a pull";
  EXPECT_EQ(explored->gate, document_kind::none);
  EXPECT_EQ(explored->schema, seam_schema_version);
  EXPECT_EQ(explored->points.size(), 3u);
  for (const seam_point& point : explored->points) {
    EXPECT_FALSE(point.at_every_step) << "every point of this seam has an "
                                         "address, which is where its safety "
                                         "comes from";
    EXPECT_EQ(point.module.file, resident_image.file);
  }
  ASSERT_EQ(explored->fingerprints.size(), 1u);
  EXPECT_EQ(explored->fingerprints.front(),
            known_editions().front().fingerprint);
}

}  // namespace
}  // namespace amberfolio::machine
