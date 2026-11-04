// SPDX-License-Identifier: AGPL-3.0-only
//
// The explored overlay (seam_explored.cpp, M5-E5 #179, the fog M5-E5f
// #263, its colour and its radius M5-E5g #299), exercised through its
// mechanism and not through any program.
//
// **The screen is filled before anything is asserted**, which is what the
// fog needs: this seam draws over what the program drew, so a blank
// screen is no picture at all. `fill()` paints every plane of the whole
// frame white first, the way the program's own composition would leave
// the window painted, and what the assertions read is where that went
// grey.
//
// **The fog is a checker and the assertions are pixel-exact about it**
// (M5-E5f, #263; black since M5-E5g, #299). A covered square is *half*
// covered: palette index 0 wherever `x + y` is even in screen
// coordinates, and the fill's own colour 15 on the other half. So a
// square that came out solid, a square whose checker restarted at its own
// left edge instead of running on from its neighbour's, and a square
// drawn in the wrong colour are three different failures here rather than
// one, and `TheCheckerRunsUnbrokenAcrossTheSquareBoundaries` is the one
// that would catch a parity taken from the square instead of the screen.
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
#include <span>
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
constexpr std::uint16_t data_picture_in_view = 0x84E0;
constexpr std::uint16_t data_figure_in_view = 0x84C8;
constexpr std::uint16_t data_picture_open = 0x84C9;
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

  /// The same screen on one of the three wilderness areas by view kind:
  /// its disk and area from the automap's zone table, its column bias
  /// from `docs/explored-overlay.md` §2 (0, 13, 26).
  void on_band(std::uint8_t view_kind, std::uint8_t column,
               std::uint8_t row) const {
    const std::uint16_t ds = dgroup();
    const auto band = static_cast<std::uint8_t>(view_kind - view_kind_overland);
    put_byte(ds, data_view_kind, view_kind);
    put_byte(ds, data_disk_number,
             static_cast<std::uint8_t>(overland_disk + band));
    put_byte(ds, data_area_id, static_cast<std::uint8_t>(overland_area + band));
    put_byte(ds, static_cast<std::uint16_t>(data_view_column_bias + view_kind),
             static_cast<std::uint8_t>(band * 13));
    put_word(record_segment, record_overland_column, column);
    put_word(record_segment, record_overland_row, row);
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

  /// Step the window painter's per-square blit once: the program putting
  /// its map back in the window.
  void paint_the_map() const {
    stand_on(point(3));
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

/// The fog's colour: palette index 0, black (M5-E5g, #299). Stated here
/// rather than read out of the seam, which is this file's rule.
constexpr std::uint8_t fog_index = 0;

/// The colour `fill()` leaves, which is what an uncovered pixel must
/// still be.
constexpr std::uint8_t filled_index = 0x0F;

/// How many pixels of a 24 by 24 cell the checker covers: half of them.
constexpr std::size_t fogged_pixels_per_cell =
    std::size_t{cell_pixels} * cell_pixels / 2;

/// Is one cell of the window fogged — every pixel of it either the fog's
/// black or the colour the fill put there, in the checker's own pattern?
///
/// The parity is the **screen's** and not the cell's: a pixel is covered
/// when `x + y` is even in screen coordinates. The window begins at (8,
/// 8) and a cell is 24 across, so both are even and every cell's own
/// top-left corner is a covered pixel — which is exactly why a seam that
/// computed the parity from the cell would pass this and fail
/// `TheCheckerRunsUnbrokenAcrossTheSquareBoundaries`, and why that test
/// exists.
[[nodiscard]] bool cell_is_fogged(const rig& r, unsigned column, unsigned row) {
  for (unsigned y = 0; y < cell_pixels; ++y) {
    for (unsigned x = 0; x < cell_pixels; ++x) {
      const unsigned screen_x = window_x + (column * cell_pixels) + x;
      const unsigned screen_y = window_y + (row * cell_pixels) + y;
      const std::uint8_t wanted =
          ((screen_x + screen_y) % 2 == 0) ? fog_index : filled_index;
      if (r.screen_pixel(screen_x, screen_y) != wanted) {
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
                         window_y + (row * cell_pixels) + y) != filled_index) {
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
      const bool hazed = cell_is_fogged(r, column, row);
      EXPECT_TRUE(hazed || cell_is_clear(r, column, row))
          << "cell " << column << "," << row << " is neither covered nor "
          << "untouched";
      if (hazed) {
        fogged |= 1U << ((row * window_cells) + column);
      }
    }
  }
  return fogged;
}

/// Every cell of the window, as that bitmap.
constexpr std::uint32_t every_cell = (1U << (window_cells * window_cells)) - 1U;

/// The rectangle of window cells `[first_column, last_column] x
/// [first_row, last_row]`, which is the shape a walk makes on this
/// screen: at the radius this ships with, the party's own square, grown
/// by every other square it has stood on. It is still written as a
/// rectangle because a raised radius dilates one into a rectangle, and
/// this is where such a change would be asserted.
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

/// How many pixels of the whole 320 by 200 screen the fog is on: pixels
/// of palette index 0, which is a colour `fill()` puts nowhere, so every
/// one of them is this seam's.
[[nodiscard]] std::size_t pixels_covered(const rig& r) {
  std::size_t covered = 0;
  for (unsigned y = 0; y < 200; ++y) {
    for (unsigned x = 0; x < 320; ++x) {
      if (r.screen_pixel(x, y) == fog_index) {
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

TEST(ExploredOverlay, ArrivingCoversEverythingButTheCellUnderTheParty) {
  // The picture a player meets: the party has just arrived, the only
  // cell it has stood on is the one under its feet, and at a reveal
  // radius of zero (M5-E5g, #299) that cell is the whole of what it can
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
  // the party is window column 2, row 2 — which is the one cell the
  // reveal has in it.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 2));
  // Twenty-four cells, half of each of them, and not a pixel of the rest
  // of the frame.
  EXPECT_EQ(pixels_covered(r), 24U * fogged_pixels_per_cell);
}

TEST(ExploredOverlay, TheFogIsABlackCheckerOverThePixelsTheProgramDrew) {
  // **The marking itself** (M5-E5f, #263; its colour M5-E5g, #299),
  // asserted on one square rather than through the bitmap the other tests
  // use. A covered square keeps half of its pixels exactly as the program
  // drew them; the other half are palette index 0 — every plane clear —
  // which is what makes this a veil the terrain shows through rather than
  // a hole in the screen. The colour was the program's own dark grey
  // until that checker was walked rather than looked at.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();

  // Window cell (0, 0) is the corner of the outer ring, which the party
  // has not been near.
  std::size_t hazed = 0;
  std::size_t untouched = 0;
  for (unsigned y = 0; y < cell_pixels; ++y) {
    for (unsigned x = 0; x < cell_pixels; ++x) {
      const unsigned screen_x = window_x + x;
      const unsigned screen_y = window_y + y;
      const std::uint8_t colour = r.screen_pixel(screen_x, screen_y);
      if ((screen_x + screen_y) % 2 == 0) {
        ASSERT_EQ(colour, fog_index)
            << "the covered half, at " << screen_x << "," << screen_y;
        ++hazed;
      } else {
        ASSERT_EQ(colour, filled_index)
            << "the program's own pixel, at " << screen_x << "," << screen_y;
        ++untouched;
      }
    }
  }
  EXPECT_EQ(hazed, fogged_pixels_per_cell);
  EXPECT_EQ(untouched, fogged_pixels_per_cell);
}

TEST(ExploredOverlay, TheCheckerRunsUnbrokenAcrossTheSquareBoundaries) {
  // **The parity is the screen's, not the square's.** Every square of the
  // window is 24 pixels across and the window starts at an even x, so a
  // checker computed from a square's own corner would look right inside
  // every square and still be the same picture — unless the drawing were
  // ever moved, or a square's own origin were ever odd. This asserts the
  // rule rather than the accident: over the window's whole top row, which
  // is five fogged squares side by side, every pixel with an even `x + y`
  // is the fog's and every pixel with an odd one is the program's, with
  // no doubled line and no gap at any of the four boundaries.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();

  for (unsigned column = 0; column < window_cells; ++column) {
    ASSERT_TRUE(cell_is_fogged(r, column, 0)) << "the whole top row is fog";
  }
  for (unsigned y = window_y; y < window_y + cell_pixels; ++y) {
    for (unsigned x = window_x; x < window_x + (window_cells * cell_pixels);
         ++x) {
      ASSERT_EQ(r.screen_pixel(x, y),
                ((x + y) % 2 == 0) ? fog_index : filled_index)
          << "at " << x << "," << y;
    }
  }
}

TEST(ExploredOverlay, AStepUncoversTheRowTheWindowScrolledOnto) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  // North one square. The window scrolls with the party, and what the
  // party can now see is the two cells it has stood on.
  r.put_word(record_segment, record_overland_row, 31);
  r.poll(2);
  r.fill();
  r.present();

  // The party is at (3, 31): the window's top-left is (1, 29), so the
  // party is window column 2, row 2 and the square it came from is window
  // column 2, row 3. Everything else — the row ahead of the party and
  // both of its flanks — is still fog.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 3));
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
                filled_index)
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
      ASSERT_EQ(r.screen_pixel(x, y), filled_index) << "at " << x << "," << y;
    }
  }
}

TEST(ExploredOverlay, ThePartyAgainstTheMapsOwnEdgeIsStillItsOnlyClearCell) {
  // The party against the bottom of a 36-row map. The window cannot
  // scroll any further, so the party is its *fifth* row rather than its
  // third, and the four rows above it are country it has not walked.
  //
  // At the radius this ships with there is no dilation here to clip: the
  // reveal is the cell itself, and `revealed()`'s clip against the map's
  // own bounds is what would keep a *raised* radius from asking the
  // record about a row that is not on the map. This is where that would
  // be asserted, and the geometry is set up for it.
  const rig r;
  r.enable();
  r.travelling(3, 35);
  r.poll(4);
  r.fill();
  r.present();

  // The window's top-left is (1, 31), so the party — (3, 35) — is window
  // column 2, row 4.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 4, 4));
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
  // reveal is the party's own cell, window column 2, row 2.
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 2));
  EXPECT_TRUE(cell_is_fogged(r, 3, 2)) << "the neighbour's column 16";
  EXPECT_TRUE(cell_is_fogged(r, 4, 2)) << "and its column 17";
}

TEST(ExploredOverlay, AWellWalkedStretchOfCountryIsNotCoveredAtAll) {
  // The other end of the same rule, and the case that keeps the early
  // return honest: with every cell of the window somewhere the party has
  // stood, there is nothing to cover and not a port is written.
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

TEST(ExploredOverlay, TheFogComesBackWithThePartysBarAfterAnEncounter) {
  // What a party that flees an encounter on the overworld saw: the
  // program redraws the window while the encounter's own question is
  // still the bar, then puts the party's bar back and waits for a key —
  // with no present after it. The party has not moved and nothing was
  // revealed, so a poll that trusted what it drew before the encounter
  // left the whole window clear until the next step.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.fill();
  r.present();
  const std::size_t covered = pixels_covered(r);
  ASSERT_GT(covered, 0u) << "it was drawing a moment ago";

  r.somebody_else_asks();
  r.poll(2);
  r.fill();
  r.present();
  ASSERT_EQ(pixels_covered(r), 0u) << "the encounter's screen is its own";

  r.put_up_the_bar(rig::dgroup(), data_menu_area_view);
  r.poll(1);
  EXPECT_EQ(pixels_covered(r), covered);
}

TEST(ExploredOverlay, NothingIsDrawnOverAPictureAScriptPutInTheWindow) {
  // What a player saw (#535): a step onto a square whose event puts a
  // picture up. The event types its text at the party's own bar, and its
  // first polls are where the step's position settles, so a seam that
  // asked only the bar laid the fog over the picture.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.poll(4);
  r.put_word(record_segment, record_overland_row, 31);
  r.put_byte(rig::dgroup(), data_picture_open, 1);
  r.put_byte(rig::dgroup(), data_picture_in_view, 1);
  r.fill();
  r.poll(4);
  EXPECT_EQ(pixels_covered(r), 0u) << "the window is the script's picture";
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u) << "and says so after a present too";
}

TEST(ExploredOverlay, NothingIsDrawnOverAFigureTheMessagePrinterDrew) {
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.put_byte(rig::dgroup(), data_figure_in_view, 1);
  r.fill();
  r.poll(4);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, AfterAPictureTheFogWaitsForTheProgramsOwnRedraw) {
  // The end of an event clears the script's bytes without redrawing the
  // window, so the picture is still on the screen when the party's bar
  // polls again. The fog comes back once the program paints its map back.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.put_byte(rig::dgroup(), data_picture_open, 1);
  r.put_byte(rig::dgroup(), data_picture_in_view, 1);
  r.poll(4);
  r.put_byte(rig::dgroup(), data_picture_open, 0);
  r.put_byte(rig::dgroup(), data_picture_in_view, 0);
  r.fill();
  r.poll(4);
  EXPECT_EQ(pixels_covered(r), 0u) << "the event is over; its picture is not";

  r.paint_the_map();
  r.present();
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 2));
}

TEST(ExploredOverlay, APresentIsNotTheMapPutBack) {
  // What a player saw (#545): an event that ends on a choice leaves its
  // picture in the window, and putting the party's bar back up presents
  // the screen. The picture is still there after that present.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.put_byte(rig::dgroup(), data_picture_open, 1);
  r.put_byte(rig::dgroup(), data_picture_in_view, 1);
  r.poll(4);
  r.put_byte(rig::dgroup(), data_picture_open, 0);
  r.put_byte(rig::dgroup(), data_picture_in_view, 0);
  r.fill();
  r.present();
  r.poll(4);
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u) << "the picture is still in the window";
}

TEST(ExploredOverlay, OffTheTravelViewTheMapPainterReleasesNothing) {
  // The combat view blits its squares through the same thunk. Only the
  // travel view's own painting puts the map back.
  const rig r;
  r.enable();
  r.travelling(3, 32);
  r.put_byte(rig::dgroup(), data_picture_open, 1);
  r.put_byte(rig::dgroup(), data_picture_in_view, 1);
  r.poll(4);
  r.put_byte(rig::dgroup(), data_picture_open, 0);
  r.put_byte(rig::dgroup(), data_picture_in_view, 0);
  r.put_byte(rig::dgroup(), data_game_mode, mode_overland + 2);
  r.paint_the_map();
  r.put_byte(rig::dgroup(), data_game_mode, mode_overland);
  r.fill();
  r.present();
  EXPECT_EQ(pixels_covered(r), 0u);
}

TEST(ExploredOverlay, TheFogIsThereOnTheFirstFrameAcrossABandEdge) {
  // Walking east off the first wilderness area at column 15 lands the
  // party at column 3 of the second. The program loads the new area
  // first, then plays the keys pressed during the load straight away, so
  // the party is a step further on before it has polled three times at
  // any one cell. A seam that waited for three looks drew nothing for
  // those steps.
  const rig r;
  r.enable();
  r.travelling(15, 26);
  r.poll(4);

  r.on_band(3, 3, 26);
  r.fill();
  r.present();
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 2))
      << "the new area's first frame";

  // The queued step, drawn and presented before any poll.
  r.put_word(record_segment, record_overland_column, 4);
  r.fill();
  r.present();
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(1, 2, 2, 2))
      << "and the step after it";
}

TEST(ExploredOverlay, HalfwayAcrossABandEdgeNothingIsRecordedOrDrawn) {
  // Crossing, the program names the new area's disk and area a moment
  // before the new view kind. The position is the old band's then, and
  // believing it would credit a cell to the new band nobody stood on.
  const rig r;
  r.enable();
  r.travelling(15, 26);
  r.poll(4);

  r.put_byte(rig::dgroup(), data_disk_number, overland_disk + 1);
  r.put_byte(rig::dgroup(), data_area_id, overland_area + 1);
  r.poll(4);
  r.fill();
  r.present();
  EXPECT_EQ(r.map_state().find_overland(overland_disk + 1, overland_area + 1),
            nullptr);
  EXPECT_EQ(pixels_covered(r), 0u);
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
  ASSERT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 2))
      << "nothing stood on but its own square";

  // What a host does when the program loads a save slot: it replaces the
  // table, which moves the store's serial. (3, 33) is one square south,
  // and it uncovers that square and no other.
  automap_record& map =
      r.map_state().record_for_overland(overland_disk, overland_area);
  ASSERT_TRUE(r.map_state().reveal(map, 3, 33));
  r.fill();
  r.poll(1);
  EXPECT_EQ(fogged_cells(r), every_cell & ~block(2, 2, 2, 3))
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

TEST(ExploredDefinition, TheRevealRadiusIsZero) {
  // The one number in this enhancement a person is meant to turn, so a
  // change to it fails here, by name, rather than in nine expectations
  // that each look like a wrong picture (M5-E5f, #263; M5-E5g, #299).
  //
  // Both bounds on it are measured. Above: two and three cover nothing at
  // all on a five-by-five window centred on the party, so the fog would
  // never appear except at a map's own edge. Below: one was shipped and
  // **walked**, and it uncovers a corridor three cells wide, which fills
  // the map faster than the party explores it. **The maintainer set zero
  // while playing**, which is a stronger look than the still that settled
  // one.
  EXPECT_EQ(explored_reveal_radius, 0);
}

TEST(ExploredDefinition, ItIsASettingAndNotAPullAndItNamesTheBaseline) {
  const rig r;
  const seam_definition* explored = r.pc().seams().find("explored");
  ASSERT_NE(explored, nullptr);
  EXPECT_FALSE(explored->trigger) << "a setting is not a pull";
  EXPECT_EQ(explored->gate, document_kind::none);
  EXPECT_EQ(explored->schema, seam_schema_version);
  EXPECT_EQ(explored->points.size(), 4u);
  for (const seam_point& point : explored->points) {
    EXPECT_FALSE(point.at_every_step) << "every point of this seam has an "
                                         "address, which is where its safety "
                                         "comes from";
    EXPECT_EQ(point.module.file, resident_image.file);
  }
  ASSERT_EQ(explored->fingerprints.size(), 2u)
      << "the baseline, and Curse of the Azure Bonds with points of its own";
  EXPECT_EQ(explored->fingerprints.front(),
            known_editions().front().fingerprint);
}

// ---------------------------------------------------------------------------
// On Curse of the Azure Bonds: a picture with places on it (#555)
// ---------------------------------------------------------------------------
//
// The same seam on a second program, by a mechanism of its own: the fog
// is laid over one picture of the region, from the program's own copy of
// that picture, and what it leaves clear is a disc around each place the
// party has stood on and a corridor along each way it went between two.
// The facts are restated here, as above; the place table is this file's
// own, three places at cells it chose.

namespace curse {
constexpr std::string_view fingerprint =
    "42fcefd6785679568b00bc1fb5b1d81ac2773416be4a35e0363920ddf6b78acd";
constexpr std::uint16_t data_segment =
    static_cast<std::uint16_t>(image_load_segment + 0xABE);
constexpr std::uint16_t menu_word = 0x1050;
constexpr std::uint16_t mark_test = 0x04AC;
constexpr std::uint16_t blit_entry = 0x3347;
constexpr std::uint16_t blit_exit = 0x3542;
constexpr std::uint16_t compositor_entry = 0x3548;
constexpr std::uint16_t blit_next_line = 0x343B;

constexpr std::uint16_t data_game_mode = 0x4FD3;
constexpr std::uint16_t data_area_record = 0x4FB2;
constexpr std::uint16_t data_disk_number = 0x5C08;
constexpr std::uint16_t data_picture_block = 0x72AA;
constexpr std::uint16_t data_last_picture = 0x72A4;
constexpr std::uint16_t data_picture_pointer = 0x72A6;
constexpr std::uint16_t data_mark_block = 0x8C90;
constexpr std::uint16_t data_mark_saved = 0x65E4;
constexpr std::uint16_t data_mark_column = 0x8C8C;
constexpr std::uint16_t data_mark_row = 0x8C8E;
constexpr std::uint16_t data_adapter = 0x5000;
constexpr std::uint16_t data_plane_order = 0x2590;
constexpr std::uint16_t data_place_columns = 0x0A4A;
constexpr std::uint16_t data_place_rows = 0x0A6A;
constexpr std::uint16_t area_current_place = 0x342;

constexpr std::uint8_t mode_start_menu = 0;
constexpr std::uint8_t mode_overland = 3;
constexpr std::uint8_t mode_adventure = 4;
constexpr std::uint8_t overland_picture = 0x79;
constexpr std::uint8_t no_picture = 0xFF;

/// Where this test puts things: a segment each, so a handler that
/// followed the wrong pointer gives a wrong answer rather than a lucky
/// one.
constexpr std::uint16_t menu_segment = 0x2000;
constexpr std::uint16_t record_segment = 0x4000;
constexpr std::uint16_t picture_segment = 0x5000;
constexpr std::uint16_t mark_segment = 0x6000;
constexpr std::uint16_t other_segment = 0x6100;
constexpr std::uint16_t stack_segment = 0x7000;
constexpr std::uint16_t frame = 0x0800;

/// The picture: 38 byte columns by 120 lines at cell (1, 1), the frame's
/// whole interior, every plane byte 0xFF, which is colour 15.
constexpr unsigned picture_columns = 38;
constexpr unsigned picture_lines = 120;
constexpr unsigned picture_left = 8;
constexpr unsigned picture_top = 8;

/// Three places, by cell: far enough apart that a disc around one does
/// not reach another.
constexpr std::array<std::uint8_t, 3> place_columns{5, 20, 33};
constexpr std::array<std::uint8_t, 3> place_rows{12, 3, 12};
}  // namespace curse

struct curse_rig : rig {
  curse_rig() {
    sha256_digest digest;
    EXPECT_TRUE(parse_digest(curse::fingerprint, digest));
    box->seams().loaded(digest, image_load_segment);
    put_word(image_load_segment, curse::menu_word, curse::menu_segment);
    const std::uint16_t ds = curse::data_segment;
    put_byte(ds, curse::data_adapter, 1);
    for (std::uint16_t k = 0; k < 4; ++k) {
      put_byte(ds, static_cast<std::uint16_t>(curse::data_plane_order + k),
               static_cast<std::uint8_t>(1U << k));
    }
    for (std::size_t p = 0; p < curse::place_columns.size(); ++p) {
      put_byte(ds, static_cast<std::uint16_t>(curse::data_place_columns + p),
               curse::place_columns[p]);
      put_byte(ds, static_cast<std::uint16_t>(curse::data_place_rows + p),
               curse::place_rows[p]);
    }
    put_word(ds, curse::data_area_record, 0);
    put_word(ds, static_cast<std::uint16_t>(curse::data_area_record + 2),
             curse::record_segment);
    put_byte(ds, curse::data_disk_number, 1);
    put_word(ds, curse::data_picture_pointer, 0);
    put_word(ds, static_cast<std::uint16_t>(curse::data_picture_pointer + 2),
             curse::picture_segment);
    put_word(ds, curse::data_mark_block, 0);
    put_word(ds, static_cast<std::uint16_t>(curse::data_mark_block + 2),
             curse::mark_segment);
    put_word(ds, curse::data_mark_saved, 0x0100);
    put_word(ds, static_cast<std::uint16_t>(curse::data_mark_saved + 2),
             curse::mark_segment);
    put_word(curse::picture_segment, 0, curse::picture_lines);
    put_word(curse::picture_segment, 2, curse::picture_columns);
    for (unsigned i = 0; i < curse::picture_lines * curse::picture_columns * 4;
         ++i) {
      put_byte(curse::picture_segment, static_cast<std::uint16_t>(0x17 + i),
               0xFF);
    }
    // The mark and the block it saves into: one cell, eight lines.
    put_word(curse::mark_segment, 0, 8);
    put_word(curse::mark_segment, 2, 1);
    put_word(curse::mark_segment, 0x0100, 8);
    put_word(curse::mark_segment, 0x0102, 1);
    // Somebody else's picture, at (3, 3).
    put_word(curse::other_segment, 0, 40);
    put_word(curse::other_segment, 2, 5);
  }

  /// The overland, as the program's own test of it sees it, at `place`.
  void on_the_overland(std::uint16_t place) const {
    const std::uint16_t ds = curse::data_segment;
    put_byte(ds, curse::data_game_mode, curse::mode_overland);
    put_byte(ds, curse::data_picture_block, curse::overland_picture);
    put_byte(ds, curse::data_last_picture, curse::no_picture);
    put_word(curse::record_segment, curse::area_current_place, place);
    put_word(ds, curse::data_mark_column, curse::place_columns[place]);
    put_word(ds, curse::data_mark_row, curse::place_rows[place]);
  }

  void in_mode(std::uint8_t mode) const {
    put_byte(curse::data_segment, curse::data_game_mode, mode);
  }

  void stand_at(std::uint16_t cs, std::uint16_t ip) const {
    box->memory().ram()[cpu::physical_address(cs, ip)] = 0xF4;
    box->processor().reset();
    cpu::registers& r = regs();
    r[cpu::sreg::cs] = cs;
    r.ip = ip;
    r[cpu::sreg::ds] = curse::data_segment;
    r[cpu::sreg::ss] = curse::stack_segment;
    r[cpu::reg16::sp] = curse::frame;
    r[cpu::reg16::bp] = curse::frame;
  }

  /// The menu-bar routine decides whether to put the mark up.
  void mark_test() const {
    stand_at(curse::menu_segment, curse::mark_test);
    box->step();
  }

  /// The blitter has drawn `segment:0` at (`row`, `column`): its frame
  /// still up, BP on it.
  void blit_done(std::uint16_t segment, std::uint16_t row,
                 std::uint16_t column) const {
    put_word(curse::stack_segment, curse::frame + 6, 0);
    put_word(curse::stack_segment, curse::frame + 8, segment);
    put_word(curse::stack_segment, curse::frame + 10, row);
    put_word(curse::stack_segment, curse::frame + 12, column);
    stand_at(image_load_segment, curse::blit_exit);
    box->step();
  }

  /// The program draws its overland picture.
  void draw_the_picture() const { blit_done(curse::picture_segment, 1, 1); }

  /// The blitter is about to draw `segment:offset` at (`row`, `column`):
  /// the far return on top, then its arguments.
  void blit_begins(std::uint16_t segment, std::uint16_t offset,
                   std::uint16_t row, std::uint16_t column) const {
    put_word(curse::stack_segment, curse::frame + 4, offset);
    put_word(curse::stack_segment, curse::frame + 6, segment);
    put_word(curse::stack_segment, curse::frame + 8, row);
    put_word(curse::stack_segment, curse::frame + 10, column);
    stand_at(image_load_segment, curse::blit_entry);
    box->step();
  }

  /// The blitter has drawn the overland picture's `screen_line` and goes
  /// on to the next: its frame up, the line at BP-4.
  void line_done(std::uint16_t screen_line) const {
    put_word(curse::stack_segment, curse::frame + 6, 0);
    put_word(curse::stack_segment, curse::frame + 8, curse::picture_segment);
    put_word(curse::stack_segment, curse::frame + 10, 1);
    put_word(curse::stack_segment, curse::frame + 12, 1);
    put_word(curse::stack_segment, curse::frame - 4, screen_line);
    stand_at(image_load_segment, curse::blit_next_line);
    box->step();
  }

  void compositor() const {
    stand_at(image_load_segment, curse::compositor_entry);
    box->step();
  }

  /// The centre pixel of a place's cell, and a pixel `dx` and `dy` from
  /// it.
  [[nodiscard]] static unsigned place_x(unsigned place, int dx = 0) {
    return static_cast<unsigned>((curse::place_columns[place] * 8) + 4 + dx);
  }
  [[nodiscard]] static unsigned place_y(unsigned place, int dy = 0) {
    return static_cast<unsigned>((curse::place_rows[place] * 8) + 4 + dy);
  }

  /// How many pixels of the picture are the fog's.
  [[nodiscard]] std::size_t picture_fogged() const {
    std::size_t fogged = 0;
    for (unsigned y = curse::picture_top;
         y < curse::picture_top + curse::picture_lines; ++y) {
      for (unsigned x = curse::picture_left;
           x < curse::picture_left + (curse::picture_columns * 8); ++x) {
        if (screen_pixel(x, y) == fog_index) {
          ++fogged;
        }
      }
    }
    return fogged;
  }

  /// Is the pixel at (x, y) what the fog makes of colour 15 there?
  [[nodiscard]] bool fogged_as_it_should_be(unsigned x, unsigned y) const {
    const std::uint8_t wanted = ((x + y) % 2 == 0) ? fog_index : filled_index;
    return screen_pixel(x, y) == wanted;
  }
};

TEST(ExploredOnCurse, IsAvailableWithPointsOfItsOwn) {
  const curse_rig r;
  sha256_digest digest;
  ASSERT_TRUE(parse_digest(curse::fingerprint, digest));
  const seam_definition* explored = r.pc().seams().find("explored");
  ASSERT_NE(explored, nullptr);
  const std::span<const seam_point> mine = points_on(*explored, digest);
  ASSERT_EQ(mine.size(), 5U);
  EXPECT_EQ(mine[0].module.load_segment_at, curse::menu_word);
  EXPECT_EQ(mine[0].offset, curse::mark_test);
  EXPECT_TRUE(mine[1].module.is_resident_image());
  EXPECT_EQ(mine[1].offset, curse::blit_entry);
  EXPECT_TRUE(mine[2].module.is_resident_image());
  EXPECT_EQ(mine[2].offset, curse::blit_exit);
  EXPECT_TRUE(mine[3].module.is_resident_image());
  EXPECT_EQ(mine[3].offset, curse::compositor_entry);
  EXPECT_TRUE(mine[4].module.is_resident_image());
  EXPECT_EQ(mine[4].offset, curse::blit_next_line);
  r.enable();
}

TEST(ExploredOnCurse, ThePictureIsFoggedButForTheCurrentPlace) {
  const curse_rig r;
  r.enable();
  r.wipe();
  r.on_the_overland(0);
  r.draw_the_picture();

  // The current place's cell and a disc around it are the picture's own.
  for (int d = -8; d <= 8; ++d) {
    EXPECT_EQ(r.screen_pixel(curse_rig::place_x(0, d), curse_rig::place_y(0)),
              filled_index)
        << d;
    EXPECT_EQ(r.screen_pixel(curse_rig::place_x(0), curse_rig::place_y(0, d)),
              filled_index)
        << d;
  }
  // Everywhere else is the checker over it, the other places included.
  for (const unsigned place : {1U, 2U}) {
    EXPECT_TRUE(r.fogged_as_it_should_be(curse_rig::place_x(place),
                                         curse_rig::place_y(place)));
    EXPECT_TRUE(r.fogged_as_it_should_be(curse_rig::place_x(place) + 1,
                                         curse_rig::place_y(place)));
  }
  EXPECT_TRUE(
      r.fogged_as_it_should_be(curse::picture_left, curse::picture_top));
  EXPECT_TRUE(r.fogged_as_it_should_be(curse::picture_left + 303,
                                       curse::picture_top + 119));
  // And nothing outside the picture: the frame's cells stay as they were.
  for (unsigned x = 0; x < 320; ++x) {
    EXPECT_EQ(r.screen_pixel(x, curse::picture_top - 1), 0U) << x;
    EXPECT_EQ(r.screen_pixel(x, curse::picture_top + curse::picture_lines), 0U)
        << x;
  }
  for (unsigned y = 0; y < 200; ++y) {
    EXPECT_EQ(r.screen_pixel(curse::picture_left - 1, y), 0U) << y;
    EXPECT_EQ(r.screen_pixel(curse::picture_left + 304, y), 0U) << y;
  }
}

TEST(ExploredOnCurse, EachLineIsFoggedAsTheBlitterFinishesIt) {
  const curse_rig r;
  r.enable();
  r.fill();
  r.on_the_overland(0);
  // The picture is coming in: line 20 of the screen is drawn, 21 is not.
  r.line_done(20);
  for (unsigned x = curse::picture_left; x < curse::picture_left + 304; ++x) {
    EXPECT_TRUE(r.fogged_as_it_should_be(x, 20)) << x;
    EXPECT_EQ(r.screen_pixel(x, 21), filled_index) << x;
  }
  // Not the overland's picture: nothing.
  r.put_byte(curse::data_segment, curse::data_picture_block, 0x78);
  r.line_done(22);
  for (unsigned x = curse::picture_left; x < curse::picture_left + 304; ++x) {
    EXPECT_EQ(r.screen_pixel(x, 22), filled_index) << x;
  }
}

TEST(ExploredOnCurse, AJourneyUncoversThePlaceAndTheWayToIt) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.mark_test();
  const std::size_t before = r.picture_fogged();

  // The script moves the party to place 2, and the program asks whether
  // to put the mark up there.
  r.on_the_overland(2);
  r.mark_test();

  const automap_record* map =
      r.map_state().find_places(1, curse::overland_picture);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 0, 0));
  EXPECT_TRUE(automap_state::seen(*map, 2, 2));
  EXPECT_TRUE(automap_state::seen(*map, 0, 2));
  EXPECT_TRUE(automap_state::seen(*map, 2, 0)) << "one way, either direction";
  EXPECT_FALSE(automap_state::seen(*map, 1, 1));
  EXPECT_FALSE(automap_state::seen(*map, 0, 1));

  EXPECT_LT(r.picture_fogged(), before);
  // Both places are clear, and so is the way between them, all along it.
  EXPECT_EQ(r.screen_pixel(curse_rig::place_x(0), curse_rig::place_y(0)),
            filled_index);
  EXPECT_EQ(r.screen_pixel(curse_rig::place_x(2), curse_rig::place_y(2)),
            filled_index);
  const unsigned mid_x = (curse_rig::place_x(0) + curse_rig::place_x(2)) / 2;
  const unsigned mid_y = curse_rig::place_y(0);
  for (int d = -4; d <= 4; ++d) {
    EXPECT_EQ(r.screen_pixel(mid_x, static_cast<unsigned>(mid_y + d)),
              filled_index)
        << d;
  }
  // A pixel well off the way is still under the fog, and so is the place
  // nobody went to.
  EXPECT_TRUE(r.fogged_as_it_should_be(mid_x, mid_y - 20));
  EXPECT_TRUE(r.fogged_as_it_should_be(mid_x + 1, mid_y - 20));
  EXPECT_TRUE(
      r.fogged_as_it_should_be(curse_rig::place_x(1), curse_rig::place_y(1)));
}

TEST(ExploredOnCurse, TheStartMenuIsNotAWayBetweenTwoPlaces) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.mark_test();
  // A save loaded from the start menu puts the party somewhere else.
  r.in_mode(curse::mode_start_menu);
  r.mark_test();
  r.on_the_overland(1);
  r.draw_the_picture();
  r.mark_test();

  const automap_record* map =
      r.map_state().find_places(1, curse::overland_picture);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 0, 0));
  EXPECT_TRUE(automap_state::seen(*map, 1, 1));
  EXPECT_FALSE(automap_state::seen(*map, 0, 1));
}

TEST(ExploredOnCurse, APictureDrawnOverTheMapIsLeftAlone) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.mark_test();
  // A script's picture at (3, 3), over the map's own cells.
  r.blit_begins(curse::other_segment, 0, 3, 3);
  r.fill();
  // The party moves on while it is up: recorded, and not painted.
  r.on_the_overland(2);
  r.mark_test();
  EXPECT_EQ(r.picture_fogged(), 0U) << "the screen is the script's";
  const automap_record* map =
      r.map_state().find_places(1, curse::overland_picture);
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(automap_state::seen(*map, 0, 2));

  // The program draws its map again, and the fog is laid with the way.
  r.draw_the_picture();
  EXPECT_GT(r.picture_fogged(), 0U);
  EXPECT_EQ(r.screen_pixel(curse_rig::place_x(2), curse_rig::place_y(2)),
            filled_index);
}

TEST(ExploredOnCurse, AMaskedPictureOverTheMapIsLeftAloneToo) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.compositor();
  r.fill();
  r.on_the_overland(2);
  r.mark_test();
  EXPECT_EQ(r.picture_fogged(), 0U);
}

TEST(ExploredOnCurse, TheBlinkingMarkAndAFrameOutsideThePictureAreNotACover) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.mark_test();
  // The mark and the screen it saved, at its own cell.
  r.blit_begins(curse::mark_segment, 0, curse::place_rows[0],
                curse::place_columns[0]);
  r.blit_begins(curse::mark_segment, 0x0100, curse::place_rows[0],
                curse::place_columns[0]);
  // A frame cell, below the picture, drawn from the saved-screen block.
  r.blit_begins(curse::mark_segment, 0x0100, 16, 4);
  r.fill();
  r.on_the_overland(2);
  r.mark_test();
  EXPECT_GT(r.picture_fogged(), 0U) << "the picture was still the map";
}

TEST(ExploredOnCurse, NothingIsRecordedOrDrawnOffTheOverland) {
  const curse_rig r;
  r.enable();
  r.fill();
  r.on_the_overland(0);
  r.in_mode(curse::mode_adventure);
  r.draw_the_picture();
  r.mark_test();
  EXPECT_EQ(r.map_state().records_used(), 0U);
  EXPECT_EQ(r.picture_fogged(), 0U);

  // The overland's picture, but the last picture loaded keeps the mark
  // down: the program's own test says no, and so does the seam.
  r.on_the_overland(0);
  r.put_byte(curse::data_segment, curse::data_last_picture, 0x50);
  r.draw_the_picture();
  r.mark_test();
  EXPECT_EQ(r.map_state().records_used(), 0U);
  EXPECT_EQ(r.picture_fogged(), 0U);
}

TEST(ExploredOnCurse, AnotherAdaptersPictureIsNotPainted) {
  const curse_rig r;
  r.enable();
  r.fill();
  r.put_byte(curse::data_segment, curse::data_adapter, 2);
  r.on_the_overland(0);
  r.draw_the_picture();
  EXPECT_EQ(r.picture_fogged(), 0U);
}

TEST(ExploredOnCurse, APlaceBeyondTheTableIsDeclined) {
  const curse_rig r;
  r.enable();
  r.on_the_overland(0);
  r.put_word(curse::record_segment, curse::area_current_place, 33);
  r.mark_test();
  EXPECT_EQ(r.map_state().records_used(), 0U);
}

TEST(ExploredOnCurse, ThePaintTouchesNoLatchAndLeavesEveryPlaneEnabled) {
  const curse_rig r;
  r.enable();
  r.fill();
  // Load the latches with something the paint would disturb if it read.
  static_cast<void>(r.pc().processor().read_byte(0xA000, 0x1F3F));
  const auto latches = r.video->latches();
  r.on_the_overland(0);
  r.draw_the_picture();
  EXPECT_GT(r.picture_fogged(), 0U);
  EXPECT_EQ(r.video->latches(), latches);
  EXPECT_EQ(r.video->map_mask(), 0x0FU);
}

TEST(ExploredOnCurse, WithTheSeamOffNothingIsRecordedOrDrawn) {
  const curse_rig r;
  r.fill();
  r.on_the_overland(0);
  r.draw_the_picture();
  r.mark_test();
  r.on_the_overland(2);
  r.mark_test();
  EXPECT_EQ(r.map_state().records_used(), 0U);
  EXPECT_EQ(r.picture_fogged(), 0U);
}

TEST(ExploredPlaces, APairOfPlacesIsOneBitWhicheverWayRound) {
  EXPECT_EQ(automap_cell_index(automap_map_kind::places, 3, 3), 3U);
  EXPECT_EQ(automap_cell_index(automap_map_kind::places, 2, 5),
            33U + (5U * 4U / 2U) + 2U);
  EXPECT_EQ(automap_cell_index(automap_map_kind::places, 5, 2),
            automap_cell_index(automap_map_kind::places, 2, 5));
  EXPECT_EQ(automap_cell_index(automap_map_kind::places, 31, 32),
            33U + (32U * 31U / 2U) + 31U);
  EXPECT_LT(automap_cell_index(automap_map_kind::places, 31, 32),
            automap_seen_bytes * 8);
  EXPECT_EQ(automap_cell_index(automap_map_kind::places, 33, 0),
            automap_no_cell);
}

TEST(ExploredPlaces, ARecordGoesIntoTheSidecarAndComesBack) {
  automap_state before;
  automap_record& map = before.record_for_places(1, 0x79);
  EXPECT_TRUE(before.reveal(map, 0, 0));
  EXPECT_TRUE(before.reveal(map, 0, 15));
  std::array<std::uint8_t, 1024> bytes{};
  const std::size_t size = before.write_sidecar(bytes);
  ASSERT_GT(size, 0U);

  automap_state after;
  ASSERT_TRUE(after.read_sidecar({bytes.data(), size}));
  const automap_record* back = after.find_places(1, 0x79);
  ASSERT_NE(back, nullptr);
  EXPECT_EQ(back->kind, automap_map_kind::places);
  EXPECT_TRUE(automap_state::seen(*back, 0, 0));
  EXPECT_TRUE(automap_state::seen(*back, 15, 0));
  EXPECT_FALSE(automap_state::seen(*back, 15, 15));
  EXPECT_EQ(after.find_overland(1, 0x79), nullptr)
      << "the kind is part of the key";
}

}  // namespace
}  // namespace amberfolio::machine
