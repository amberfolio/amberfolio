// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap panel: PLAN.md §5 item 3, M5-E2 (#173).
//
// A map of the squares the party has walked, drawn into the emulated EGA
// planes — so it is on the game's own screen, in captures, in dumps and
// in replays, and no host has to know the feature exists. It is reached
// the way the game's own views are reached: a key. Tab, because the
// program's 1988 input alphabet has no Tab in it, so a key that is this
// seam's cannot also be one of the program's.
//
// The design is settled (PLAN.md §5: "re-express proven designs as-is").
// What this file decides is how the engine carries it.
//
//
// Where it goes, and why there
// ----------------------------
//
// Over the party roster, at the whole interior of the adventuring
// screen's right-hand frame less the program's own status row —
// `automap.h` has the derivation and the numbers. That is the region the
// program's own AREA view uses, and it is why the panel reads as one of
// the game's screens rather than as a thing on top of one: it is inside
// the frame the game drew, on the game's pixel grid, in the game's
// palette.
//
//
// The three decisions (docs/seams.md §8)
// --------------------------------------
//
// **Its surface is a command**, not a setting and not a pull. A player
// presses a key and a view appears; they press it again and the party
// list comes back. So the definition is not a `trigger`, the panel starts
// **closed**, and the key is claimed inside the program's own keyboard
// path rather than taken from a host.
//
// **Its points are addresses** — five of them, all in the resident image,
// none in an overlay. Four are keyboard and drawing routines the program
// reaches constantly; one is the routine that puts the roster back.
//
// **What it refuses** is more of it than what it does. It declines when
// the data segment is not where the fact table says it is, when the
// program's map pointer does not point inside conventional memory, when
// the party's facing is not one of the four the map format has, and it
// simply does nothing at all unless the program is on the 3D adventuring
// screen with a map loaded and the position settled.
//
//
// What the program does, stated as facts
// --------------------------------------
//
// Addresses, offsets and a format description, which is the direction
// CONTRIBUTING.md allows. Not a byte of the program is reproduced here.
//
// **The program funnels every key through two routines.** One answers "is
// a key waiting" (it consults its own one-byte pushback slot, and
// otherwise asks the BIOS with AH=01h); the other answers "give me the
// next key" (the same pushback slot, otherwise the BIOS with AH=00h). The
// adventuring screen's command loop calls the first one on every pass, so
// it is reached thousands of times a second while a player can act — it
// is both the place to claim a key and the place to notice that the party
// has moved. That is the same seam the proven design ticks on, and for
// the same reason: it is where the program is between commands.
//
// **The program says when something else has taken the screen.** It
// cannot be worked out from the mode byte — a character sheet, an item
// list and a message all take the whole screen with the mode still
// "adventuring" — but every one of them clears the cells first, through
// one of two routines (a box-region clear, and the full-screen clear),
// and the program repaints the roster afterwards through a third. Three
// points, and the panel knows who owns its pixels without knowing what
// took them.
//
// **The program can put its own screen back.** Its per-mode screen
// composer repaints the roster, the viewport and the status line from
// live state, and it is what the program itself uses when a full-screen
// view is left. It takes no arguments and cleans nothing. That is what
// the panel calls when the player closes it, because the panel wrote over
// the roster and something has to redraw it — and a snapshot of the
// pixels could not, since the program redraws single roster rows while
// the panel is up.
//
// **The map is four planes of 256 bytes**, at a far pointer in the data
// segment. Plane 0's high and low nibbles are the north and east wall
// faces of cell `y * 16 + x`; plane 1's are south and west. Plane 3 holds
// two bits per face saying what kind of face it is — 0 solid, 1 a way
// through, 2 and 3 a closed door. A face with nibble 0 is no face at all.
// (Which passable faces are *doors* rather than archways is a question
// about the wall graphics rather than about this grid, and is M5-E2a's;
// this build draws a way through as a way through.)
//
//
// How the key is claimed, and why it is not register surgery
// -----------------------------------------------------------
//
// #173 proposed a point inside the INT 16h service with register surgery
// on the way out, so that the program sees "no key" or the next key
// exactly as often as it polled. What is here is one step earlier and one
// claim stronger: at the entry of the program's own key routines, **the
// keystroke is taken out of the BIOS buffer** — the ring at 40:1Eh that
// `seam_context::inject_keystroke()` puts keys into and that INT 16h
// answers out of.
//
// The program then polls, and the BIOS answers about the buffer as it now
// stands. Nothing about the number of polls changes, because nothing was
// waited for and no register was edited: the program observes exactly what
// it would have observed had the key never been typed. That is the same
// sentence #173 wanted, one layer down, where it needs no arithmetic to
// defend.
//
// Three things it is careful about:
//
//   * **Only the head of the ring**, never the middle. Keys keep their
//     order and their count; one leaves the queue, and nothing is
//     rearranged. A Tab behind another key is claimed on the pass after
//     that key is read, which is the pass on which it would have been
//     delivered.
//   * **Never while the program's own pushback slot is armed.** That
//     byte carries the second half of an extended key, and the two halves
//     have to stay adjacent.
//   * **The whole keystroke word, not the character.** Tab is scan code
//     0x0F with character 0x09; Ctrl-I is the same character under a
//     different scan code, and it is the program's.
//
// The claim at the *read* routine cannot turn a poll that said "a key is
// waiting" into a wait that never ends, and the argument is worth having
// written down. The pending routine ran first and left no Tab at the head
// of the ring; keys are only ever added at the tail; so a Tab at the head
// when the read routine is entered means the read was not preceded by a
// successful poll — an unconditional wait, which is exactly the case that
// wants the key taken.
//
//
// The eighth primitive: port surgery (docs/seams.md §3)
// -----------------------------------------------------
//
// The panel is drawn straight into the planes, which is how every other
// pixel of the game gets on the screen. A byte written into the video
// window with the map mask the program leaves behind reaches all four
// planes at once, and a panel drawn that way could be black and white and
// nothing else. Sixteen colours means selecting one plane at a time,
// which is a write to the sequencer's map mask — a **port** cycle.
//
// `machine::write_port8()` is the bus, the same bus
// `processor().write_byte()` is, and a seam writing it is the program's
// own OUT moved from outside: the device answers it exactly as it answers
// the program's, and a port nothing claims is noticed rather than
// invented. It is the sibling of "memory surgery, as the program" and it
// is documented as the eighth action primitive.
//
// What makes it safe *here* is a fact about the program: every one of its
// drawing primitives opens by resetting the graphics controller and the
// map mask and closes by putting the write mode back, so at the point
// this seam draws from — a keyboard poll, between commands — the
// registers are at that resting state, and the seam hands back exactly
// the resting state it found. It sets the four graphics-controller
// registers a plane-select write needs rather than assuming them, because
// assuming a register you cannot read back is not a check.
//
//
// The fidelity claim, stated for this seam (docs/seams.md §8.5)
// -------------------------------------------------------------
//
// The plain one holds, and it is the strongest of the five seams in this
// tree because the panel starts closed:
//
//   **On, and Tab never pressed, a run is byte for byte the run with the
//   seam off.** Every point reads and none of them writes: no keystroke
//   is claimed because none is there to claim, no port is written, no
//   pixel is drawn, and everything the seam learns goes into
//   `machine::automap()`, which is not machine state (`automap.h`).
//
// And the second one, which is what the funnel is for:
//
//   **Pressed, the program's input is what it would have been had the key
//   never been typed.** The count of polls and the sequence of keys the
//   program observes are unchanged; one keystroke that was never in its
//   alphabet is gone from the queue before it is asked for.
//
// Both are tests (`tests/core/machine/seam_automap_test.cpp`), and the
// second is a `tests/programs` stand-in so it runs on all four targets.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts
// ---------------------------------------------------------------------------

/// The SHA-256 of the program image every offset below is a fact about —
/// the baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> automap_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// **The points**, as offsets from the image segment. All five are in the
/// resident image: the keyboard, framing and roster routines are the
/// program's own substrate and are never overlaid.
///
/// Each was read off the disassembly and each has a second route. The two
/// keyboard entries are confirmed by a driven run's own stop line, which
/// names the far address of the instruction after the second one's INT
/// 16h; the box-region clear is `0x4047` in `seam_encamp_fix.cpp` too,
/// found independently for a different purpose; and the frame border
/// beside it at `0x3F10` puts the whole segment where both agree it is.
constexpr std::uint32_t key_pending_entry = 0xA6FD;
constexpr std::uint32_t key_read_entry = 0xA70F;
constexpr std::uint32_t clear_region_entry = 0x4047;
constexpr std::uint32_t clear_screen_entry = 0x7D3B;

/// The far return of the routine that draws the party roster — its
/// `retf`, not its entry. The entry would be the wrong place: that
/// routine clears each of its own rows through the box-region clear
/// above, so a point at its head would say "the roster is back" and then
/// immediately be contradicted by its own clears. At the return the list
/// is on the screen and every clear it made is behind it.
constexpr std::uint32_t roster_drawn_return = 0x148A;

/// The program's per-mode screen composer: no arguments, a plain far
/// return, and the program's own idiom for putting a screen back after
/// something took it.
///
/// **A paragraph and an offset, not a flat image offset**, and that is
/// the fact rather than a spelling. This routine reaches its own literals
/// as `CS:<constant>` and its own siblings as `push cs` plus a near call —
/// which is what a compiler of this era emits inside a segment — so it
/// only works when CS is the segment it was linked at. Called at
/// `image_base:0x3379`, the same bytes execute and every one of those
/// CS-relative reads lands somewhere else: the screen comes back drawn
/// out of whatever happens to be sixteen kilobytes lower down. It did,
/// on the first driven run, and it is written here rather than in a
/// commit message because the next routine a seam calls will have the
/// same property.
constexpr std::uint16_t screen_redraw_paragraph = 0x0BA;
constexpr std::uint16_t screen_redraw_offset = 0x27D9;

/// Where the data segment begins, as an offset in the image. The seam
/// reads DS at its points the way the rest of this tree does, and checks
/// it against this — which is the two-route rule applied to the one fact
/// every other fact below is relative to.
constexpr std::uint32_t dgroup_offset = 0xC7C0;

// --- Offsets in the data segment -------------------------------------------

/// The game mode byte, and the values that matter here. Already in this
/// tree twice (`seam_cheats.cpp`, `seam_encamp_fix.cpp`) and in
/// `docs/playable.md`'s watch list.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_camp = 2;
constexpr std::uint8_t mode_adventure_flat = 3;
constexpr std::uint8_t mode_adventure = 4;

/// Which kind of view the program is showing. 1 is the sixteen-by-sixteen
/// interior grid this panel is a map of; the overland screens are not,
/// which is why the panel does not appear on them (that map is the
/// explored overlay's, #179).
constexpr std::uint16_t data_view_kind = 0x49FA;
constexpr std::uint8_t view_kind_area = 1;

/// Non-zero while the program is running a scripted move between areas.
/// The party's position words are not to be believed while it is.
constexpr std::uint16_t data_in_transition = 0x442F;

/// The current area record: a far pointer, offset then segment. Already
/// in this tree (`seam_encamp_fix.cpp` reads the game clock through it).
constexpr std::uint16_t data_area_record = 0x49D2;

/// Which geometry block of the area's file is loaded, inside that record.
/// The third byte of a map's identity (automap.h).
constexpr std::uint16_t record_geometry_block = 0x18A;

/// Which disk's files the area comes from, and the area's own id.
constexpr std::uint16_t data_disk_number = 0x5376;
constexpr std::uint16_t data_area_id = 0x84DC;

/// The party's cell and which way it faces. `docs/playable.md`'s watch
/// list is the second route for all three: `6AAD`, `6AAE`, `6AAF`, with
/// facing 0 north, 2 east, 4 south, 6 west.
constexpr std::uint16_t data_party_x = 0x6AAD;
constexpr std::uint16_t data_party_y = 0x6AAE;
constexpr std::uint16_t data_party_facing = 0x6AAF;

/// The loaded map: a far pointer, offset then segment. A zero segment is
/// "no map", which is the state every screen that is not the adventure
/// screen leaves it in.
constexpr std::uint16_t data_map_pointer = 0x6A5C;

/// The area's frame colour and the colour of the ground band in the 3D
/// view, both EGA palette indices the program computes for itself. The
/// second is what the panel's floor is drawn in, so the map's ground is
/// the colour of the ground the player is looking at — read from the
/// program's own byte rather than sampled off the screen, which cannot be
/// fooled by a portrait covering the viewport.
constexpr std::uint16_t data_frame_colour = 0x6A60;
constexpr std::uint16_t data_ground_colour = 0x6A63;

/// The program's own one-byte keyboard pushback slot: non-zero while the
/// second half of an extended key is waiting to be handed over.
constexpr std::uint16_t data_key_pushback = 0x8501;

// --- The map's format ------------------------------------------------------

/// The map is four 256-byte planes.
constexpr std::uint16_t map_plane_bytes = 0x100;
constexpr std::uint16_t map_bytes = 4 * map_plane_bytes;
constexpr std::uint16_t map_faces_ns = 0;
constexpr std::uint16_t map_faces_sw = map_plane_bytes;
constexpr std::uint16_t map_face_styles = 3 * map_plane_bytes;

/// The four wall faces of a cell, in the order the format numbers them.
/// The same numbers the party's facing byte uses, which is what makes
/// "the face ahead of the party" a lookup rather than a conversion.
constexpr unsigned lane_north = 0;
constexpr unsigned lane_east = 2;
constexpr unsigned lane_south = 4;
constexpr unsigned lane_west = 6;
constexpr std::array<unsigned, 4> lanes{lane_north, lane_east, lane_south,
                                        lane_west};

/// One step in each lane's direction. A property of the format's own
/// numbering, above, and confirmed against the facing values recorded in
/// `docs/playable.md`.
struct step {
  int dx;
  int dy;
};
[[nodiscard]] constexpr step step_of(unsigned lane) noexcept {
  switch (lane & 7U) {
    case lane_north:
      return {.dx = 0, .dy = -1};
    case lane_east:
      return {.dx = 1, .dy = 0};
    case lane_south:
      return {.dx = 0, .dy = 1};
    default:
      return {.dx = -1, .dy = 0};
  }
}

/// What plane 3's two bits say about a face that exists at all.
constexpr std::uint8_t face_solid = 0;

/// How far ahead the panel reveals: the cell the party is on and two
/// more, which is what the 3D view in front of them shows.
constexpr unsigned reveal_depth = 3;

// --- The panel's own colours ----------------------------------------------
//
// EGA palette indices, chosen the way the proven design chose them and
// for the same reason: the panel has to read on whatever floor colour the
// area turns out to have.

constexpr std::uint8_t colour_black = 0;
constexpr std::uint8_t colour_brown = 6;
constexpr std::uint8_t colour_grey = 7;
constexpr std::uint8_t colour_green = 10;
constexpr std::uint8_t colour_cyan = 11;
constexpr std::uint8_t colour_white = 15;

// ---------------------------------------------------------------------------
// Reading the machine
// ---------------------------------------------------------------------------

/// The data segment, if DS is where the fact table says it should be.
/// Zero — which is never a data segment, because that is the interrupt
/// vector table — when it is not, and every handler treats that as a
/// decline.
[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const auto expected =
      static_cast<std::uint16_t>((ctx.image_base() + dgroup_offset) / 16U);
  const std::uint16_t ds = cpu.regs()[cpu::sreg::ds];
  return ds == expected ? ds : 0;
}

[[nodiscard]] std::uint16_t at(std::uint16_t base, std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

// ---------------------------------------------------------------------------
// The map, decoded
// ---------------------------------------------------------------------------

using map_grid = std::array<std::uint8_t, map_bytes>;

[[nodiscard]] unsigned cell_of(unsigned x, unsigned y) noexcept {
  return ((y & 0x0FU) * automap_map_side) + (x & 0x0FU);
}

/// The wall-face nibble of one lane of one cell. Zero is "no face here".
[[nodiscard]] std::uint8_t face_of(const map_grid& grid, unsigned x, unsigned y,
                                   unsigned lane) noexcept {
  const unsigned cell = cell_of(x, y);
  switch (lane & 7U) {
    case lane_north:
      return static_cast<std::uint8_t>(grid[map_faces_ns + cell] >> 4U);
    case lane_east:
      return static_cast<std::uint8_t>(grid[map_faces_ns + cell] & 0x0FU);
    case lane_south:
      return static_cast<std::uint8_t>(grid[map_faces_sw + cell] >> 4U);
    default:
      return static_cast<std::uint8_t>(grid[map_faces_sw + cell] & 0x0FU);
  }
}

/// The two style bits of one lane of one cell. Only meaningful where the
/// face nibble above is non-zero.
[[nodiscard]] std::uint8_t style_of(const map_grid& grid, unsigned x,
                                    unsigned y, unsigned lane) noexcept {
  const std::uint8_t byte = grid[map_face_styles + cell_of(x, y)];
  return static_cast<std::uint8_t>((byte >> (lane & 6U)) & 0x03U);
}

/// The lane on the other side of a border.
[[nodiscard]] constexpr unsigned opposite(unsigned lane) noexcept {
  return (lane + 4U) & 7U;
}

/// The grid's side as a signed number, because every coordinate that has
/// to be bounds-checked is one a step could have taken off the edge.
constexpr int map_side = static_cast<int>(automap_map_side);

[[nodiscard]] constexpr bool on_map(int x, int y) noexcept {
  return x >= 0 && x < map_side && y >= 0 && y < map_side;
}

/// Whether sight crosses a border. Both faces have to be absent.
///
/// Absent, not passable: a doorway that is permanently open in the data
/// is still something the party has to walk through before it can say
/// what is on the other side, and most of this program's city doorways
/// are exactly that. Seeing through them revealed the insides of
/// buildings from the street, which is why the proven design settled on
/// the stricter rule and why this carries it.
///
/// Both faces, not the near one: the data often records a border's wall
/// on one side only, and checking the near face alone let sight leak
/// through a wall recorded on the far cell.
[[nodiscard]] bool sight_crosses(const map_grid& grid, unsigned x, unsigned y,
                                 unsigned lane, unsigned nx,
                                 unsigned ny) noexcept {
  return face_of(grid, x, y, lane) == 0 &&
         face_of(grid, nx, ny, opposite(lane)) == 0;
}

/// Reveal what the party can see from where it stands: its own cell, the
/// cells beside it that nothing blocks, and the same again for as far
/// ahead as sight carries.
///
/// Sight does not wrap. The program's coordinates do — the grid is a
/// torus — but a reveal that wrapped would paint disconnected cells on
/// the opposite edge of the panel, which reads as a fault rather than as
/// a map.
void reveal_from(automap_state& state, automap_record& map,
                 const map_grid& grid, unsigned px, unsigned py,
                 unsigned facing) {
  const unsigned ahead = facing & 7U;
  const unsigned left = (facing + 6U) & 7U;
  const unsigned right = (facing + 2U) & 7U;

  auto x = static_cast<int>(px & 0x0FU);
  auto y = static_cast<int>(py & 0x0FU);
  for (unsigned depth = 0; depth < reveal_depth; ++depth) {
    const auto ux = static_cast<unsigned>(x);
    const auto uy = static_cast<unsigned>(y);
    state.reveal(map, ux, uy);

    for (const unsigned side : {left, right}) {
      const step to = step_of(side);
      const int nx = x + to.dx;
      const int ny = y + to.dy;
      if (on_map(nx, ny) &&
          sight_crosses(grid, ux, uy, side, static_cast<unsigned>(nx),
                        static_cast<unsigned>(ny))) {
        state.reveal(map, static_cast<unsigned>(nx), static_cast<unsigned>(ny));
      }
    }

    const step forward = step_of(ahead);
    const int nx = x + forward.dx;
    const int ny = y + forward.dy;
    if (!on_map(nx, ny) ||
        !sight_crosses(grid, ux, uy, ahead, static_cast<unsigned>(nx),
                       static_cast<unsigned>(ny))) {
      break;
    }
    x = nx;
    y = ny;
  }
}

// ---------------------------------------------------------------------------
// Drawing the panel, into its own buffer
// ---------------------------------------------------------------------------

using panel_pixels = std::array<std::uint8_t, automap_panel_pixels>;

/// The panel's bounds as signed numbers, because everything that draws
/// works in offsets that can go negative before they are clipped.
constexpr int panel_width = static_cast<int>(automap_panel_width);
constexpr int panel_height = static_cast<int>(automap_panel_height);

void put(panel_pixels& panel, int x, int y, std::uint8_t colour) noexcept {
  if (x < 0 || y < 0 || x >= panel_width || y >= panel_height) {
    return;
  }
  panel[(static_cast<std::size_t>(y) * automap_panel_width) +
        static_cast<std::size_t>(x)] = colour;
}

/// One cell's wall stroke along one lane, optionally with a gap in the
/// middle of it — which is how a way through is drawn: the stroke stops,
/// leaving a one-pixel stub at each end so the opening is an opening in
/// something rather than a missing wall.
void stroke(panel_pixels& panel, int x0, int y0, unsigned lane, unsigned gap,
            std::uint8_t colour) noexcept {
  const auto cell = static_cast<int>(automap_cell_pixels);
  const auto first = static_cast<int>((automap_cell_pixels - gap) / 2);
  const int last = first + static_cast<int>(gap);
  for (int i = 0; i < cell; ++i) {
    if (gap > 0 && i >= first && i < last) {
      continue;
    }
    switch (lane) {
      case lane_north:
        put(panel, x0 + i, y0, colour);
        break;
      case lane_south:
        put(panel, x0 + i, y0 + cell - 1, colour);
        break;
      case lane_west:
        put(panel, x0, y0 + i, colour);
        break;
      default:
        put(panel, x0 + cell - 1, y0 + i, colour);
        break;
    }
  }
}

/// The five-by-five marks the panel draws in a cell, one bit per pixel
/// from bit 4 down. Drawn for this project, like every other glyph in
/// this tree (font.h): nothing here is anybody else's bitmap.
constexpr std::array<std::uint8_t, 5> glyph_north{0x04, 0x0E, 0x1F, 0x04, 0x04};
constexpr std::array<std::uint8_t, 5> glyph_east{0x04, 0x02, 0x1F, 0x02, 0x04};
constexpr std::array<std::uint8_t, 5> glyph_south{0x04, 0x04, 0x1F, 0x0E, 0x04};
constexpr std::array<std::uint8_t, 5> glyph_west{0x04, 0x08, 0x1F, 0x08, 0x04};
constexpr std::array<std::uint8_t, 5> glyph_entrance{0x0E, 0x1F, 0x1B, 0x1B,
                                                     0x1B};
constexpr std::array<std::uint8_t, 5> glyph_exit{0x10, 0x18, 0x1C, 0x1E, 0x1F};

[[nodiscard]] const std::array<std::uint8_t, 5>& arrow_for(
    unsigned facing) noexcept {
  switch (facing & 7U) {
    case lane_north:
      return glyph_north;
    case lane_east:
      return glyph_east;
    case lane_south:
      return glyph_south;
    default:
      return glyph_west;
  }
}

/// A mark in a cell: five pixels of black under it first, so it reads as
/// a mark whatever the floor is, and clear of the one-pixel wall strokes
/// on all four sides.
void draw_mark(panel_pixels& panel, int x0, int y0,
               const std::array<std::uint8_t, 5>& rows,
               std::uint8_t colour) noexcept {
  const auto cell = static_cast<int>(automap_cell_pixels);
  for (int y = 1; y < cell - 1; ++y) {
    for (int x = 1; x < cell - 1; ++x) {
      put(panel, x0 + x, y0 + y, colour_black);
    }
  }
  const int inset = (cell - 5) / 2;
  for (int row = 0; row < 5; ++row) {
    for (int bit = 0; bit < 5; ++bit) {
      if (((rows[static_cast<std::size_t>(row)] >> (4 - bit)) & 1U) != 0) {
        put(panel, x0 + inset + bit, y0 + inset + row, colour);
      }
    }
  }
}

/// The colour a wall is drawn in.
///
/// M5-E2 draws every wall of a map in one shade: the area's own frame
/// colour, which is the colour the program frames that area's screens in,
/// and brown when the program has not set one. Deriving a wall's colour
/// from the texture the 3D view actually blits for it — so that water is
/// blue because its tiles are blue — is M5-E2a, and it changes this
/// function and nothing else.
///
/// The one rule that survives either way: a wall the same colour as the
/// floor is not a wall a player can see, so it is shifted to its bright
/// twin, and away from black if that is where it lands.
[[nodiscard]] std::uint8_t wall_colour(std::uint8_t frame_colour,
                                       std::uint8_t floor_colour) noexcept {
  std::uint8_t colour = frame_colour != 0 ? frame_colour : colour_brown;
  if (colour == floor_colour) {
    colour = static_cast<std::uint8_t>(colour ^ 8U);
    if (colour == colour_black) {
      colour = colour_grey;
    }
  }
  return colour;
}

void render(automap_state& state, const automap_record& map,
            const map_grid& grid, unsigned px, unsigned py, unsigned facing,
            std::uint8_t floor_colour, std::uint8_t frame_colour) {
  panel_pixels& panel = state.pixels();
  panel.fill(colour_black);

  const std::uint8_t wall = wall_colour(frame_colour, floor_colour);
  const auto cell = static_cast<int>(automap_cell_pixels);

  for (unsigned cy = 0; cy < automap_map_side; ++cy) {
    for (unsigned cx = 0; cx < automap_map_side; ++cx) {
      if (!automap_state::seen(map, cx, cy)) {
        continue;
      }
      const int x0 = static_cast<int>(cx) * cell;
      const int y0 = static_cast<int>(cy) * cell;
      for (int y = 0; y < cell; ++y) {
        for (int x = 0; x < cell; ++x) {
          put(panel, x0 + x, y0 + y, floor_colour);
        }
      }

      for (const unsigned lane : lanes) {
        const std::uint8_t near_face = face_of(grid, cx, cy, lane);
        const std::uint8_t near_style = style_of(grid, cx, cy, lane);

        // A border's wall is often recorded on one side only, so a cell
        // with no face of its own borrows the far cell's — which is what
        // makes a wall show from the side the party is standing on while
        // the cell beyond it is still unexplored. An off-map neighbour
        // contributes nothing: reading one with the coordinates wrapped
        // would paint the opposite edge's walls along the border.
        const step to = step_of(lane);
        const int nx = static_cast<int>(cx) + to.dx;
        const int ny = static_cast<int>(cy) + to.dy;
        std::uint8_t far_face = 0;
        std::uint8_t far_style = 0;
        if (on_map(nx, ny)) {
          const auto ux = static_cast<unsigned>(nx);
          const auto uy = static_cast<unsigned>(ny);
          far_face = face_of(grid, ux, uy, opposite(lane));
          far_style = style_of(grid, ux, uy, opposite(lane));
        }
        if (near_face == 0 && far_face == 0) {
          continue;
        }

        // The near face decides when there is one, exactly as the
        // program's own movement and 3D view decide: both gate on the
        // party cell's own lane.
        const std::uint8_t face_style = near_face != 0 ? near_style : far_style;
        stroke(panel, x0, y0, lane,
               face_style == face_solid ? 0U : automap_cell_pixels - 2U, wall);
      }
    }
  }

  for (std::size_t i = 0; i < map.marker_count; ++i) {
    const unsigned mx = map.marker_x[i];
    const unsigned my = map.marker_y[i];
    if (!automap_state::seen(map, mx, my) ||
        (mx == (px & 0x0FU) && my == (py & 0x0FU))) {
      continue;
    }
    const int x0 = static_cast<int>(mx) * cell;
    const int y0 = static_cast<int>(my) * cell;
    if (map.marker_kind[i] == automap_marker::entrance) {
      draw_mark(panel, x0, y0, glyph_entrance, colour_green);
    } else if (map.marker_kind[i] == automap_marker::exit) {
      draw_mark(panel, x0, y0, glyph_exit, colour_cyan);
    }
  }

  // The party last, so nothing is ever drawn over it.
  draw_mark(panel, static_cast<int>(px & 0x0FU) * cell,
            static_cast<int>(py & 0x0FU) * cell, arrow_for(facing),
            colour_white);
}

// ---------------------------------------------------------------------------
// Putting it on the planes
// ---------------------------------------------------------------------------

/// The graphics-controller registers a plane-select write needs, and the
/// values a write mode 0 byte-for-byte copy wants: no set/reset
/// substitution, no rotate and no logical function, and every bit of the
/// byte taken from the CPU rather than from the latches.
constexpr std::uint8_t gc_enable_set_reset_index = 1;
constexpr std::uint8_t gc_data_rotate_index = 3;
constexpr std::uint8_t gc_write_mode_index = 5;
constexpr std::uint8_t gc_bit_mask_index = 8;
constexpr std::uint8_t sequencer_map_mask_index = 2;
constexpr std::uint8_t all_planes = 0x0F;
constexpr std::uint8_t all_bits = 0xFF;

/// Bytes per scanline of one plane in the 320-pixel graphics mode the
/// program runs in, and the screen the panel lands on. Page zero: the
/// program keeps a second page for its own composition, but page zero is
/// the one being displayed while the party is on the adventure screen.
constexpr std::uint16_t plane_bytes_per_row = 40;
constexpr std::uint16_t video_window_segment = 0xA000;

void write_register(machine& box, std::uint16_t index_port,
                    std::uint16_t data_port, std::uint8_t index,
                    std::uint8_t value) {
  box.write_port8(index_port, index);
  box.write_port8(data_port, value);
}

/// The panel into the planes, one plane at a time.
///
/// A row of the panel is twenty-two whole bytes of a plane, so nothing is
/// shifted and nothing is read back to be merged (`automap.h` has why the
/// rect is the rect). The registers are left as the program's own drawing
/// primitives leave them, which is the state they were found in.
void blit(machine& box, const automap_state& state) {
  const panel_pixels& panel = state.pixels();
  cpu::processor& cpu = box.processor();

  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);

  constexpr std::uint16_t bytes_across = automap_panel_width / 8;
  constexpr std::uint16_t first_byte_column = automap_panel_x / 8;

  for (std::uint8_t plane = 0; plane < ega::plane_count; ++plane) {
    write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                   sequencer_map_mask_index,
                   static_cast<std::uint8_t>(1U << plane));
    for (std::uint16_t row = 0; row < automap_panel_height; ++row) {
      const auto line = static_cast<std::uint16_t>(
          ((automap_panel_y + row) * plane_bytes_per_row) + first_byte_column);
      const std::size_t source =
          static_cast<std::size_t>(row) * automap_panel_width;
      for (std::uint16_t column = 0; column < bytes_across; ++column) {
        std::uint8_t bits = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
          const std::uint8_t colour =
              panel[source + (static_cast<std::size_t>(column) * 8) + bit];
          if (((colour >> plane) & 1U) != 0) {
            bits = static_cast<std::uint8_t>(bits | (0x80U >> bit));
          }
        }
        cpu.write_byte(video_window_segment, at(line, column), bits);
      }
    }
  }

  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);
}

// ---------------------------------------------------------------------------
// The hotkey
// ---------------------------------------------------------------------------

/// Tab, as the BIOS hands it over: scan code in the high byte, character
/// in the low one. Matched whole — the character alone would also catch
/// Ctrl-I, which is the program's key and not this seam's.
constexpr std::uint16_t key_tab = 0x0F09;

/// Take Tab out of the BIOS keystroke buffer if it is the next key there.
/// True when one was taken, which is a player asking for the panel.
[[nodiscard]] bool claim_hotkey(cpu::processor& cpu, std::uint16_t ds) {
  if (cpu.read_byte(ds, data_key_pushback) != 0) {
    // The second half of an extended key is waiting. The two halves have
    // to stay adjacent, so nothing is touched on this pass.
    return false;
  }

  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  const std::uint16_t tail =
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
  if (head == tail) {
    return false;
  }
  if (cpu.read_word(bda::segment, head) != key_tab) {
    return false;
  }

  auto next = static_cast<std::uint16_t>(head + 2U);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  cpu.write_word(bda::segment, bda::keyboard_buffer_head, next);
  return true;
}

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------

/// A cheap mixed hash of the few bytes a decision depends on. Not a
/// digest of anything and not compared against anything outside this
/// file — it exists so that a screen nothing has changed about costs a
/// comparison instead of ten thousand bus cycles.
[[nodiscard]] std::uint32_t mix(std::uint32_t seed, std::uint32_t value) {
  std::uint32_t hash = seed ^ value;
  hash *= 16777619U;
  return hash ^ (hash >> 13U);
}

struct sample {
  std::uint8_t disk;
  std::uint8_t area;
  std::uint8_t geo;
  std::uint8_t x;
  std::uint8_t y;
  std::uint8_t facing;
  std::uint8_t floor_colour;
  std::uint8_t frame_colour;
  std::uint16_t map_offset;
  std::uint16_t map_segment;
};

/// Whether the program is on the screen this panel is a map of, with a
/// map loaded and nothing scripted in flight.
[[nodiscard]] bool adventuring(cpu::processor& cpu, std::uint16_t ds) {
  return cpu.read_byte(ds, data_game_mode) == mode_adventure &&
         cpu.read_byte(ds, data_view_kind) == view_kind_area &&
         cpu.read_byte(ds, data_in_transition) == 0 &&
         cpu.read_word(ds, at(data_map_pointer, 2)) != 0;
}

/// Everything one pass needs off the data segment. The geometry block
/// comes through the area record's far pointer, which is checked before
/// it is followed: nothing lives in segment zero.
[[nodiscard]] bool take_sample(cpu::processor& cpu, std::uint16_t ds,
                               sample& out) {
  const std::uint16_t record_offset = cpu.read_word(ds, data_area_record);
  const std::uint16_t record_segment =
      cpu.read_word(ds, at(data_area_record, 2));
  if (record_segment == 0) {
    return false;
  }

  out.disk = cpu.read_byte(ds, data_disk_number);
  out.area = cpu.read_byte(ds, data_area_id);
  out.geo =
      cpu.read_byte(record_segment, at(record_offset, record_geometry_block));
  out.x = cpu.read_byte(ds, data_party_x);
  out.y = cpu.read_byte(ds, data_party_y);
  out.facing = cpu.read_byte(ds, data_party_facing);
  out.floor_colour = cpu.read_byte(ds, data_ground_colour);
  out.frame_colour = cpu.read_byte(ds, data_frame_colour);
  out.map_offset = cpu.read_word(ds, data_map_pointer);
  out.map_segment = cpu.read_word(ds, at(data_map_pointer, 2));

  // The facing byte indexes the map's own lane numbering, and only the
  // four cardinals are in it. Anything else is not a party facing.
  if ((out.facing & 1U) != 0 || out.facing > lane_west) {
    return false;
  }
  // A floor that is fog-black would draw an explored cell as an
  // unexplored one. The program has not set the byte yet; brown is what
  // its own default terrain is.
  if (out.floor_colour == 0) {
    out.floor_colour = colour_brown;
  }
  return true;
}

/// Copy the program's map out of its memory, through the bus, as the
/// program's own reads would.
///
/// The pointer is bounds-checked first, because a handler at a keyboard
/// poll runs whenever the program feels like polling and a far pointer
/// that has not been set up yet points anywhere. A read above
/// conventional memory is a read of the video window, which loads the
/// adapter's latches — a seam that wandered there would be changing the
/// machine to look at it.
[[nodiscard]] bool copy_map(cpu::processor& cpu, const sample& state,
                            map_grid& grid) {
  const std::uint32_t base =
      cpu::physical_address(state.map_segment, state.map_offset);
  if (base + map_bytes > conventional_ram_size) {
    return false;
  }
  if (state.map_offset > static_cast<std::uint16_t>(0x10000U - map_bytes)) {
    return false;
  }
  for (std::uint16_t i = 0; i < map_bytes; ++i) {
    grid[i] = cpu.read_byte(state.map_segment, at(state.map_offset, i));
  }
  return true;
}

/// Put the program's own screen back, because the panel wrote over the
/// party list and only the program can redraw it from live state.
///
/// A batch of one call (`seam_context::call_program`, #188). The panel is
/// marked down *before* it is queued: when the batch finishes the engine
/// offers this point again, and a handler that had not already recorded
/// what it was doing would queue the same call a second time.
void give_the_roster_back(machine& box, seam_context& ctx, std::uint16_t ds) {
  automap_state& state = box.automap();
  state.set_panel_on_screen(false);
  state.set_drawn_signature(0);

  const std::uint8_t mode = box.processor().read_byte(ds, data_game_mode);
  if (mode != mode_camp && mode != mode_adventure_flat &&
      mode != mode_adventure) {
    // Belt and braces, and the program's own rule: the composer is only
    // real for the modes that have a screen to compose. A repaint the
    // program cannot perform would leave a corrupted screen, which is a
    // worse answer than a missing one.
    return;
  }
  ctx.call_program(static_cast<std::uint16_t>((ctx.image_base() / 16U) +
                                              screen_redraw_paragraph),
                   screen_redraw_offset, {});
}

// ---------------------------------------------------------------------------
// The handlers
// ---------------------------------------------------------------------------

/// The workhorse: the program is asking whether a key is waiting, which
/// is where it is between commands.
void at_key_pending(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  automap_state& state = box.automap();

  if (claim_hotkey(cpu, ds)) {
    const bool open = !state.panel_open();
    state.set_panel_open(open);
    if (!open && state.panel_on_screen()) {
      give_the_roster_back(box, ctx, ds);
      return;
    }
    state.set_drawn_signature(0);
  }

  if (!adventuring(cpu, ds)) {
    return;
  }
  sample now{};
  if (!take_sample(cpu, ds, now)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // Where the party is, as one number: what the fog was last brought up
  // to date from, and half of what the panel was last drawn from.
  std::uint32_t where = mix(2166136261U, now.disk);
  where = mix(where, now.area);
  where = mix(where, now.geo);
  where = mix(where, now.x);
  where = mix(where, now.y);
  where = mix(where, now.facing);

  const bool settled = state.observe(now.disk, now.area, now.geo, now.x, now.y);
  if (!settled) {
    // The party's position is not to be believed yet — the program's own
    // arrival script has not placed it. Nothing is revealed and nothing
    // is drawn from it.
    return;
  }

  map_grid grid{};
  bool have_grid = false;
  if (where != state.revealed_signature()) {
    if (!copy_map(cpu, now, grid)) {
      ctx.decline(seam_reason::point_not_recognized);
      return;
    }
    have_grid = true;
    reveal_from(state, state.record_for(now.disk, now.area, now.geo), grid,
                now.x, now.y, now.facing);
    state.set_revealed_signature(where);
  }

  if (!state.panel_open() || state.panel_covered()) {
    return;
  }

  std::uint32_t drawn = mix(where, now.floor_colour);
  drawn = mix(drawn, now.frame_colour);
  drawn = mix(drawn, state.serial());
  if (drawn == 0) {
    // Zero is this seam's "nothing has been drawn" (automap.h), so it is
    // not allowed to be a real answer.
    drawn = 1;
  }
  if (drawn == state.drawn_signature() && state.panel_on_screen()) {
    return;
  }

  if (!have_grid && !copy_map(cpu, now, grid)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const automap_record* map = state.find(now.disk, now.area, now.geo);
  if (map == nullptr) {
    return;
  }
  render(state, *map, grid, now.x, now.y, now.facing, now.floor_colour,
         now.frame_colour);
  blit(box, state);
  state.set_panel_on_screen(true);
  state.set_drawn_signature(drawn);
}

/// The program is about to wait for a key. Only the claim happens here:
/// this is not a moment when the program is between commands, and the
/// panel has nothing to say about it.
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (!claim_hotkey(cpu, ds)) {
    return;
  }
  automap_state& state = box.automap();
  const bool open = !state.panel_open();
  state.set_panel_open(open);
  if (!open && state.panel_on_screen()) {
    give_the_roster_back(box, ctx, ds);
    return;
  }
  state.set_drawn_signature(0);
}

/// A box region is about to be cleared. If it meets the panel's cells,
/// something else is taking the screen there.
void at_clear_region(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];

  // At the routine's entry the stack holds its far return address and
  // then its four arguments, each a word whose low byte is the value:
  // bottom, right, top, left, in the order the program's own callers
  // push them.
  constexpr std::uint16_t frame_bottom = 4;
  constexpr std::uint16_t frame_right = 6;
  constexpr std::uint16_t frame_top = 8;
  constexpr std::uint16_t frame_left = 10;

  const auto bottom = cpu.read_byte(ss, at(sp, frame_bottom));
  const auto right = cpu.read_byte(ss, at(sp, frame_right));
  const auto top = cpu.read_byte(ss, at(sp, frame_top));
  const auto left = cpu.read_byte(ss, at(sp, frame_left));
  if (bottom < top || right < left) {
    // Not a rect. Nothing is concluded from it.
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (automap_state::rect_meets_panel(bottom, right, top, left)) {
    box.automap().set_panel_covered(true);
  }
}

/// The whole screen is about to be cleared, which certainly includes the
/// panel's cells.
void at_clear_screen(machine& box, seam_context& ctx) {
  (void)ctx;
  box.automap().set_panel_covered(true);
}

/// The party roster is on the screen again: those cells are the panel's
/// to claim once more, and the program has just painted over whatever was
/// there.
void at_roster_drawn(machine& box, seam_context& ctx) {
  (void)ctx;
  box.automap().set_panel_covered(false);
}

// ---------------------------------------------------------------------------
// The definition
// ---------------------------------------------------------------------------

constexpr std::array<seam_point, 5> automap_points{
    {{.module = resident_image,
      .offset = key_pending_entry,
      .run = &at_key_pending},
     {.module = resident_image, .offset = key_read_entry, .run = &at_key_read},
     {.module = resident_image,
      .offset = clear_region_entry,
      .run = &at_clear_region},
     {.module = resident_image,
      .offset = clear_screen_entry,
      .run = &at_clear_screen},
     {.module = resident_image,
      .offset = roster_drawn_return,
      .run = &at_roster_drawn}}};

constexpr seam_definition automap_definition{
    .id = "automap",
    .about =
        "a map of where the party has been, on Tab, in the game's own "
        "screen",
    .fingerprints = automap_binaries,
    .points = automap_points,
    .trigger = false,
    .gate = document_kind::none,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& automap_seam() noexcept { return automap_definition; }

}  // namespace amberfolio::machine
