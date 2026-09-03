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
// **Its points are addresses** — six of them, all in the resident image,
// none in an overlay. Four are keyboard and drawing routines the program
// reaches constantly; one is the return of the routine that draws the
// roster; and one is the thunk of the routine every menu bar in the game
// is put up through, which is how the panel knows whose screen it is on.
//
// **What it refuses** is more of it than what it does. It declines when
// the data segment is not where the fact table says it is, when the
// program's map pointer does not point inside conventional memory, when
// the party's facing is not one of the four the map format has, and it
// simply does nothing at all unless the program is on the 3D adventuring
// screen with a map loaded and the position settled. It also gives the
// screen back the moment anything other than the party's own command bar
// starts asking the player questions, which is the paragraph after next.
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
// **The program can draw its party roster on demand.** One routine, one
// argument — a far pointer to the current member, which is all it needs
// to know which row is white — and it paints the list from live state.
// That is what the panel calls when the player closes it, because the
// panel wrote over the roster and something has to redraw it, and a
// snapshot of the pixels could not: the program redraws single roster
// rows while the panel is up, so a snapshot is stale the moment a
// character takes damage.
//
// It is deliberately *not* the program's per-mode screen composer, which
// is the wider routine that would also repaint the viewport and the
// status line. A vendor's portrait lives in the viewport, and closing the
// panel through the composer painted the 3D view over the person the
// player was talking to. The panel covers the roster; the roster is what
// it owes back. What the drawer does not do is clear the row above the
// list or the rows below the party, so the panel's own rect is cleared
// through the program's region clear first — the same routine this seam's
// third point is the entry of.
//
// **Every bar in the game goes up through one routine**, and it is
// handed the bar as an argument. The adventuring screen hands it one of
// two strings it keeps in its data segment; a vendor's yes/no, a script's
// menu, a shop and the camp bar all hand it a copy built on the stack. So
// a far pointer into the data segment at one of two known offsets is the
// party's own command bar, and anything else in the world is not.
//
// That is how the panel knows a vendor is mid-question, which nothing
// else can tell it: such a question leaves the game mode at
// "adventuring", draws its portrait in the viewport and its text on the
// message row, and touches not one cell of the panel, so neither the mode
// byte nor the three drawing points above ever hear about it.
//
// The obvious cheaper answer was measured and thrown away. The program
// keeps a byte for "a script has the message area", zeroed by its PRINT
// opcodes and set back by the adventuring loop when it clears that area
// on the way out of a command — and driven against the real program in
// New Phlan it turned out to oscillate on *every step*, and to be down
// far more often than up at the very moment a player is standing at the
// bar. A gate on it would have taken the panel away for most of a walk.
// `--watch 84E4` is what said so, and it is why this seam has a sixth
// point rather than a fifth fact.
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
// Tab is not the only key claimed, though it is the only one claimed
// unconditionally. **While the panel is up it also takes the two keys
// that step the program's roster cursor** — the next and previous party
// member. The adventuring screen answers them by redrawing the party
// list, which is the block of cells the panel is drawn on: the map is
// painted over, the seam puts it back on the next pass, and what the
// player sees is a flash. A command whose whole visible effect is behind
// the panel is one the panel may decline while it is the thing on the
// screen. They are given straight back the moment it comes down.
//
// The claim at the *read* routine cannot turn a poll that said "a key is
// waiting" into a wait that never ends, and the argument is worth having
// written down. The pending routine ran first and left no Tab at the head
// of the ring; keys are only ever added at the tail; so a Tab at the head
// when the read routine is entered means the read was not preceded by a
// successful poll — an unconditional wait, which is exactly the case that
// wants the key taken.
//
// **A key taken there is put back** (#266) — not that key, a character
// the program throws away. The argument does not depend on anything
// above: a read is the program already committed to being handed a key,
// so a read this seam empties is a program asleep inside the BIOS, where
// no point of this engine is reached at all and the next key the player
// types is delivered unseen. What a player saw was Tab needing a second
// press, and the first press eating whatever they typed after it.
// `seam_key_read.h` has the character and why it is that one; the journal
// reader claims at the same address and answers with the same one, which
// is why it is spelled once and not twice.
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
// That second sentence is exact at the *poll*, which is where a key is
// meant to be taken, and it is one character wide of exact at the
// blocking read: a program that has committed to being handed a key is
// handed a `-` rather than nothing, because nothing is a program asleep
// in the BIOS (#266). The `-` is on none of its bars and its menu-bar
// routine does not even return on one, so what it does with the
// difference is go back to waiting.
//
// The roster-cursor keys are the one place that second sentence is
// narrower, and it is narrower on purpose: those keys *are* in the
// program's alphabet, and while the panel is up one of them is taken
// instead of delivered. The claim there is the one a modal view makes —
// with the panel down, which is every run in which Tab was never pressed,
// not a key of the program's is touched.
//
// Both are tests (`tests/core/machine/seam_automap_test.cpp`), and the
// second is a `tests/programs` stand-in so it runs on all four targets.
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
//   * **The door rule has only ever been driven through its fallback**
//     (#268). A leaf is drawn where a wall face's *kind* has been seen
//     shut — on this map, or in the table of every shut face in the
//     shipped data, below. New Phlan has no shut face anywhere on it, so
//     leg 8 and every still taken off it exercise the table and not the
//     rule. Kovel Mansion has forty-five, and is where a routed leg
//     would close it.
//   * **Learning across maps is deliberately not carried** (#268, and
//     #199 before it). The proven design learns at runtime across maps;
//     this one does not, and that is a decision rather than a debt.
//     It is written down here so that the next person to ask why the
//     panel does not do it finds the answer at the panel, and PLAN.md
//     §5's "designs are settled, not reopened" is what an argument to
//     carry it would have to get past.
//   * **Nobody has watched this panel move.** Its colours, its zone
//     label and its door leaves were each chosen and checked off dumped
//     frames, and every leg that has driven it ran headless; the
//     browser's checkbox that turns it on has not been ticked by a
//     person either (#274). #263 is the standing lesson there:
//     "measurably different" is not "legible", and only somebody looking
//     can say which one this is.

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
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/service_floor.h"
#include "automap_overland.h"
#include "seam_builtin.h"
#include "seam_key_read.h"

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

/// The resident thunk of the program's **one** menu-bar input routine
/// (M5-E2d): every bar the game puts on the screen and every key it reads
/// through one goes past here, with the bar itself as an argument. A far
/// jump into the overlay that holds the routine, so at its entry the
/// stack is still the caller's frame.
///
/// This is how the panel knows whose screen it is on. Three points tell
/// it when something has taken its *cells*, which is enough for anything
/// that takes the screen — a character sheet, a shop, a fight. It is not
/// enough for a vendor's question, which leaves the game mode at
/// "adventuring", draws its portrait in the viewport and its question on
/// the message row, and touches not one cell of the panel. The bar does
/// say: the adventuring screen's own is a pointer into the data segment,
/// and everybody else's is a string they built on the stack.
constexpr std::uint32_t command_bar_entry = 0x3C7A;

/// Where the bar is in that frame, and the two data-segment offsets that
/// say it is the party's own, are `automap_overland.h`'s: the explored
/// overlay (#179) has the same point for the same reason, and one reader
/// is what stops the two seams coming to different conclusions about
/// whose screen this is.

/// The far return of the routine that draws the party roster — its
/// `retf`, not its entry. The entry would be the wrong place: that
/// routine clears each of its own rows through the box-region clear
/// above, so a point at its head would say "the roster is back" and then
/// immediately be contradicted by its own clears. At the return the list
/// is on the screen and every clear it made is behind it.
constexpr std::uint32_t roster_drawn_return = 0x148A;

/// The routine that draws the party roster, and how to reach it. Its own
/// far return is `roster_drawn_return` above, which is this seam's fifth
/// point: the seam both watches this routine and calls it.
///
/// It takes one argument, a far pointer to the current party member, and
/// cleans four bytes on the way out. All it does with the pointer is
/// decide which row is drawn white — the list itself it walks from the
/// party head, which it reads for itself.
///
/// **A paragraph and an offset, not a flat image offset**, and that is
/// the fact rather than a spelling. This routine reaches its own literals
/// as `CS:<constant>` and its own siblings as `push cs` plus a near call —
/// which is what a compiler of this era emits inside a segment — so it
/// only works when CS is the segment it was linked at. The screen
/// composer this seam used to call sits in the same segment and has the
/// same property: called at `image_base:0x3379` the same bytes executed
/// and drew the roster out of whatever happened to be sixteen kilobytes
/// lower down, which looked like a corrupted machine. It is written here
/// rather than in a commit message because the next routine a seam calls
/// will have it too (`docs/seams.md` §8.4).
constexpr std::uint16_t roster_draw_paragraph = 0x0BA;
constexpr std::uint16_t roster_draw_offset = 0x0767;

/// The box-region clear again, as a call target rather than as a place to
/// stop: the panel's own rect is cleared before the roster is drawn back
/// over it (see `give_the_roster_back()`). One address, named twice,
/// because a point is a `std::uint32_t` offset into a module and a call is
/// a `std::uint16_t` offset into a segment.
constexpr auto image_clear_region =
    static_cast<std::uint16_t>(clear_region_entry);

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

/// The current party member: a far pointer, offset then segment. The one
/// argument the roster drawer takes.
constexpr std::uint16_t data_current_member = 0x5D92;

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

/// The **shape-tile table**: a far pointer to the buffer the wall sets are
/// loaded into. Row `n - 1` of it, stride `shape_row_bytes`, is the list
/// of 8x8 tile codes the 3D renderer blits for a wall face of kind `n`.
/// That is where a wall's colour comes from — the very tiles the player
/// is looking at.
constexpr std::uint16_t data_shape_tiles = 0x6A58;
constexpr std::uint16_t shape_row_bytes = 0x9C;

/// The **shape geometry**: for each of ten tile-block shapes the renderer
/// knows, where in a row its tiles start and how many columns and rows it
/// covers. Three byte tables, indexed by shape.
constexpr std::uint16_t data_shape_first_slot = 0x0C8C;
constexpr std::uint16_t data_shape_columns = 0x0C96;
constexpr std::uint16_t data_shape_rows = 0x0CA0;
constexpr unsigned shape_count = 10;

/// The **tile banks**: six far pointers to decoded tile sets, and the
/// table of the first tile code each bank answers for. A tile code is
/// mapped to a bank by range and then to a frame inside it by subtracting
/// that bank's first code.
constexpr std::uint16_t data_tile_banks = 0x5E3A;
constexpr std::uint16_t data_bank_first_tile = 0x2722;

/// The **wall-set descriptor**: which WALLDEF block is loaded in each of
/// the three slots, a word per slot at a stride of four. Slot 1 serves
/// wall faces 1 to 5, slot 2 serves 6 to 10, slot 3 serves 11 to 15.
/// `0xFFFF` is what the loader stamps when it filled a slot from a
/// multi-block load and cannot say which block a row came from.
constexpr std::uint16_t data_wall_set = 0x6AAE;
constexpr std::uint16_t wall_set_stride = 4;
constexpr unsigned wall_faces_per_slot = 5;
constexpr std::uint16_t wall_set_unknown = 0xFFFF;

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

/// The 8x8 font the program draws every menu and message with, as a far
/// pointer in the data segment (M5-E2b): sixty-four glyphs of eight
/// bytes, one byte to a scanline, bit 0x80 the leftmost pixel, indexed by
/// the character upper-cased and taken modulo sixty-four.
///
/// The program's own text primitive treats the two words being zero as
/// "the font has not been loaded yet" and draws nothing at all. So does
/// this: a label rasterized out of an empty buffer would be a black
/// rectangle, and the panel's black already means something.
constexpr std::uint16_t data_font_pointer = 0x5E20;
constexpr std::uint16_t font_glyphs = 64;
constexpr std::uint16_t font_glyph_bytes = 8;
constexpr std::uint16_t font_bytes = font_glyphs * font_glyph_bytes;

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

/// What plane 3's two bits say about a face that exists at all: solid, a
/// way through, or a door that is shut. Which *passable* faces are doors
/// is not in this grid at all — see `door_nibbles()` below.
constexpr std::uint8_t face_solid = 0;
constexpr std::uint8_t face_passable = 1;
constexpr std::uint8_t face_shut_door = 2;

// --- The tile a wall face is drawn with ------------------------------------
//
// A decoded tile set is a header and a run of frames. The header gives the
// frame's height in scanlines and its width in *bytes*, and the number of
// bytes one whole frame takes; the frames start at a fixed offset. Inside
// a frame the four EGA planes are interleaved a byte-column at a time, so
// a pixel's palette index is one bit out of each of four consecutive
// bytes — which is the same arrangement the adapter itself holds and the
// reason the loader can `rep movs` a frame straight at it.
constexpr std::uint16_t tile_header_height = 0x00;
constexpr std::uint16_t tile_header_width = 0x02;
constexpr std::uint16_t tile_header_frame_bytes = 0x11;
constexpr std::uint16_t tile_first_frame = 0x17;
constexpr unsigned tile_planes = 4;

/// The last tile code each bank answers for. The renderer's own range
/// map; a code past the last of these belongs to a bank that holds
/// something other than wall tiles, and this refuses it rather than
/// histogramming the wrong picture.
constexpr std::array<std::uint16_t, 4> bank_last_tile{0x2D, 0x73, 0xB9, 0xFF};

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
/// A door leaf. Yellow, and the one colour on this panel a wall may not
/// be: `wall_colour()` shifts a wall out of it.
constexpr std::uint8_t colour_door = 14;
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

/// Everything one pass of the tick reads off the data segment, which is
/// also everything the panel is drawn from. Gathered once, so that the
/// several things that want it are not several reads of the same byte.
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

// ---------------------------------------------------------------------------
// What colour a wall is (M5-E2a)
// ---------------------------------------------------------------------------
//
// **Derived from the game's data, never sampled off the screen.** The
// colour is histogrammed out of the 8x8 tiles the 3D renderer actually
// blits for that kind of wall, so water reads blue because its tiles are
// blue, and nothing changes as the party walks. An earlier cut of the
// proven design sampled the rendered ground band off the planes once and
// latched whatever happened to be covering the viewport at the time — on
// a fresh game, a tour guide's blue apron became the colour of the ground
// for the whole session.
//
// Which tiles: a wall face of kind `n` indexes row `n - 1` of the
// shape-tile table, and that row lists the tile codes for every *shape*
// the renderer draws that wall as — the near head-on face, the side
// slivers, the distant variants. Only the largest shape is histogrammed.
// The slivers are mostly post and edge and they outvote the face: water
// came out grey, off its pilings.

/// A far pointer out of the data segment, as the program's own `les`
/// would read it: offset first, segment second.
struct far_pointer {
  std::uint16_t offset;
  std::uint16_t segment;
};

[[nodiscard]] far_pointer far_at(cpu::processor& cpu, std::uint16_t segment,
                                 std::uint16_t offset) {
  return {.offset = cpu.read_word(segment, offset),
          .segment = cpu.read_word(segment, at(offset, 2))};
}

/// Whether a far pointer names conventional memory and can be followed
/// for `length` bytes. The rule every read in this file obeys: a pointer
/// the program has not set up yet points anywhere, and a read above
/// conventional memory is a read of the video window, which loads the
/// adapter's latches.
[[nodiscard]] bool followable(const far_pointer& pointer,
                              std::uint32_t length) {
  if (pointer.segment == 0 || length == 0) {
    return false;
  }
  if (static_cast<std::uint32_t>(pointer.offset) + length > 0x10000U) {
    return false;
  }
  return cpu::physical_address(pointer.segment, pointer.offset) + length <=
         conventional_ram_size;
}

/// One number that changes whenever the loaded tile sets do. Not a hash
/// of anything and not compared with anything outside this file: it is
/// what tells a cached wall colour that the tiles it was worked out from
/// have been swapped underneath it.
[[nodiscard]] std::uint16_t tile_bank_generation(cpu::processor& cpu,
                                                 std::uint16_t ds) {
  std::uint16_t generation = 0;
  for (std::size_t bank = 0; bank < bank_last_tile.size(); ++bank) {
    const std::uint16_t segment = cpu.read_word(
        ds, at(data_tile_banks, static_cast<std::uint16_t>((bank * 4) + 2)));
    generation = static_cast<std::uint16_t>((generation * 31U) + segment);
  }
  return generation;
}

/// Add every pixel of one tile to a histogram of palette indices.
void histogram_tile(cpu::processor& cpu, std::uint16_t ds, std::uint16_t code,
                    std::array<std::uint32_t, 16>& counts) {
  std::size_t bank = bank_last_tile.size();
  for (std::size_t i = 0; i < bank_last_tile.size(); ++i) {
    if (code <= bank_last_tile[i]) {
      bank = i;
      break;
    }
  }
  if (bank == bank_last_tile.size()) {
    // Past the last bank that holds wall tiles. The renderer would send
    // it to the border tile set, which is not this wall's picture.
    return;
  }

  const far_pointer tiles = far_at(
      cpu, ds, at(data_tile_banks, static_cast<std::uint16_t>(bank * 4)));
  if (!followable(tiles, tile_first_frame)) {
    return;
  }
  const std::uint16_t first_code = cpu.read_word(
      ds, at(data_bank_first_tile, static_cast<std::uint16_t>(bank * 2)));
  if (code < first_code) {
    return;
  }
  const auto frame = static_cast<std::uint16_t>(code - first_code);

  const std::uint16_t height =
      cpu.read_word(tiles.segment, at(tiles.offset, tile_header_height));
  const std::uint16_t width =
      cpu.read_word(tiles.segment, at(tiles.offset, tile_header_width));
  const std::uint16_t frame_bytes =
      cpu.read_word(tiles.segment, at(tiles.offset, tile_header_frame_bytes));
  if (height == 0 || width == 0 || frame_bytes == 0) {
    return;
  }
  // The header has to agree with itself before a byte of it is followed:
  // a frame is its scanlines times its byte columns times the four
  // planes, and anything else is not a tile set.
  if (static_cast<std::uint32_t>(height) * width * tile_planes > frame_bytes) {
    return;
  }
  const std::uint32_t span =
      static_cast<std::uint32_t>(tile_first_frame) +
      ((static_cast<std::uint32_t>(frame) + 1U) * frame_bytes);
  if (!followable(tiles, span)) {
    return;
  }

  const auto base = static_cast<std::uint16_t>(
      tiles.offset + tile_first_frame +
      static_cast<std::uint16_t>(frame * frame_bytes));
  for (std::uint16_t row = 0; row < height; ++row) {
    for (std::uint16_t column = 0; column < width; ++column) {
      std::array<std::uint8_t, tile_planes> planes{};
      const auto pixels = static_cast<std::uint16_t>(
          base + (((row * width) + column) * tile_planes));
      for (unsigned plane = 0; plane < tile_planes; ++plane) {
        planes[plane] = cpu.read_byte(
            tiles.segment, at(pixels, static_cast<std::uint16_t>(plane)));
      }
      for (unsigned bit = 0; bit < 8; ++bit) {
        const unsigned shift = 7U - bit;
        unsigned colour = 0;
        for (unsigned plane = 0; plane < tile_planes; ++plane) {
          colour |= ((planes[plane] >> shift) & 1U) << plane;
        }
        ++counts[colour];
      }
    }
  }
}

/// The colour of a wall face of kind `nibble`, or -1 when the program is
/// not holding what this would need to work it out.
[[nodiscard]] int texture_colour(cpu::processor& cpu, std::uint16_t ds,
                                 std::uint8_t nibble) {
  if (nibble < 1 || nibble > 15) {
    return -1;
  }
  const far_pointer table = far_at(cpu, ds, data_shape_tiles);
  const auto row_at = static_cast<std::uint32_t>(nibble - 1) * shape_row_bytes;
  if (!followable(table, row_at + shape_row_bytes)) {
    return -1;
  }

  // The largest shape the renderer has: the one whose rectangle of tiles
  // covers the most of the view, which is the head-on face.
  unsigned widest = 0;
  unsigned widest_extent = 0;
  for (unsigned shape = 0; shape < shape_count; ++shape) {
    const unsigned extent =
        static_cast<unsigned>(cpu.read_byte(
            ds, at(data_shape_columns, static_cast<std::uint16_t>(shape)))) *
        cpu.read_byte(ds,
                      at(data_shape_rows, static_cast<std::uint16_t>(shape)));
    if (extent > widest_extent) {
      widest_extent = extent;
      widest = shape;
    }
  }
  if (widest_extent == 0 || widest_extent > shape_row_bytes) {
    return -1;
  }
  const std::uint8_t first_slot = cpu.read_byte(
      ds, at(data_shape_first_slot, static_cast<std::uint16_t>(widest)));

  std::array<std::uint32_t, 16> counts{};
  const auto row = static_cast<std::uint16_t>(table.offset + row_at);
  for (unsigned i = 0; i < widest_extent; ++i) {
    // The slot is a byte and the renderer lets it wrap, so this does too;
    // a slot past the end of a row is not part of this shape.
    const auto slot = static_cast<std::uint8_t>(first_slot + i);
    if (slot >= shape_row_bytes) {
      continue;
    }
    const std::uint8_t code = cpu.read_byte(table.segment, at(row, slot));
    if (code != 0) {
      histogram_tile(cpu, ds, code, counts);
    }
  }

  // The modal colour that is not black: black is the gap between things
  // in almost every one of these tiles, and it is also the panel's own
  // "nobody has been here".
  unsigned best = 0;
  std::uint32_t best_count = 0;
  for (unsigned colour = 1; colour < counts.size(); ++colour) {
    if (counts[colour] > best_count) {
      best_count = counts[colour];
      best = colour;
    }
  }
  return best_count > 0 ? static_cast<int>(best) : -1;
}

// ---------------------------------------------------------------------------
// Which wall faces are doors (M5-E2a)
// ---------------------------------------------------------------------------
//
// **The renderer picks a wall's graphic from its face nibble alone.** It
// never consults the style bits — those govern movement and the prompt
// that asks whether to force a door, and are invisible in the view. So a
// door *leaf* is drawn exactly when the nibble indexes a door graphic,
// and neither "style is not solid" (which paints every archway as a door)
// nor "style is shut" (which drops every open one) can ever match what
// the player is looking at. Both test the wrong plane.
//
// There is no boolean anywhere that says "this graphic is a door". What
// there is, is evidence: a face that is *shut* is unarguably a door, and
// it names its nibble. Two sources of that evidence are combined:
//
//   * **this map's own shut faces**, scanned when the party arrives;
//   * **a table of every shut face in the shipped data**, below, because
//     the evidence is scattered. Five sub-maps of one castle share a wall
//     set and the shut instances are on three of them; without the table
//     the other two draw their doors as archways.
//
// All four rules that can end in a leaf — a shut face, a kind seen shut
// on this map, a kind only the table names, and a map nothing is known
// about — draw the same two yellow pixels, so the panel **counts** the
// leaves it draws by which rule drew each (`automap_door_evidence`). It
// is a counter and not a decision: the pixels are what they were before
// it was kept, nothing in the machine reads it, and it exists because
// every driven run up to #268 was over New Phlan, where the last of the
// four is the only one that ever ran, and a still could not say so.
//
// The durable identity of a wall graphic is (disk, WALLDEF block, row):
// the same block reused in another slot or another area is the same
// pictures. That is what the table is keyed on, and it is why it is a
// fact table rather than a list of areas.
//
// **What this deliberately does not carry** is the proven design's
// runtime *learning* across maps — a table that remembers a shut face
// seen on one map and applies it to another. It is there to be robust
// against other revisions of the data. This seam is unavailable for any
// binary its fingerprint does not name (`seam.h`), and the table below
// was derived from that binary's own data, so learning could only ever
// matter for data this seam refuses to run against.

/// One WALLDEF block known to hold door graphics, and which of its five
/// rows they are on.
///
/// Facts about the shipped data — a disk number, a block number and a
/// bitmap of row indices — derived by sweeping every shut face in the
/// area files and asking which wall set was loaded for it. No byte of
/// anything is here and neither is any picture: this says *where* the
/// doors are, and the player's own copy says what they look like.
struct door_graphics {
  std::uint8_t disk;
  std::uint16_t block;
  std::uint8_t rows;
};

constexpr std::array<door_graphics, 19> door_table{
    {{.disk = 1, .block = 1, .rows = 0x10},
     {.disk = 1, .block = 3, .rows = 0x10},
     {.disk = 2, .block = 1, .rows = 0x10},
     {.disk = 2, .block = 2, .rows = 0x10},
     {.disk = 2, .block = 4, .rows = 0x10},
     {.disk = 2, .block = 9, .rows = 0x01},
     {.disk = 2, .block = 19, .rows = 0x11},
     {.disk = 3, .block = 1, .rows = 0x10},
     {.disk = 3, .block = 3, .rows = 0x02},
     {.disk = 4, .block = 1, .rows = 0x10},
     {.disk = 4, .block = 3, .rows = 0x10},
     {.disk = 4, .block = 20, .rows = 0x10},
     {.disk = 4, .block = 22, .rows = 0x10},
     {.disk = 5, .block = 1, .rows = 0x10},
     {.disk = 5, .block = 24, .rows = 0x09},
     {.disk = 6, .block = 2, .rows = 0x10},
     {.disk = 6, .block = 6, .rows = 0x08},
     {.disk = 7, .block = 1, .rows = 0x10},
     {.disk = 8, .block = 18, .rows = 0x08}}};

/// The WALLDEF block serving wall face `nibble`, or `wall_set_unknown`
/// when the loader filled that slot from a multi-block load and cannot
/// say which — which is the same value the loader itself stamps, and is
/// not a block number, so it can be the answer as well as the reason.
[[nodiscard]] std::uint16_t block_serving(cpu::processor& cpu, std::uint16_t ds,
                                          std::uint8_t nibble) {
  if (nibble < 1 || nibble > 15) {
    return wall_set_unknown;
  }
  const unsigned slot = ((nibble - 1U) / wall_faces_per_slot) + 1U;
  const std::uint16_t block = cpu.read_word(
      ds,
      at(data_wall_set, static_cast<std::uint16_t>(slot * wall_set_stride)));
  return block > 0xFF ? wall_set_unknown : block;
}

/// The two sources' answers, kept apart (#268).
///
/// The renderer wants the union and asks `automap_state` for it. What
/// wants them apart is a person looking at a still: a leaf drawn because
/// *this map* had a shut face of that kind and a leaf drawn because the
/// shipped table said so are the same yellow pixels, and until they were
/// counted separately no screenshot could tell you which rule had run.
struct door_evidence {
  /// Kinds seen shut on this map, scanned on arrival — the rule.
  std::uint16_t seen{};
  /// Kinds the shipped table names for the wall sets this map has
  /// loaded — the fallback.
  std::uint16_t from_table{};
};

/// Which wall faces of this map are doors: the map's own shut faces, and
/// the table's answer for the wall sets this map has loaded.
[[nodiscard]] door_evidence door_nibbles_of(cpu::processor& cpu,
                                            std::uint16_t ds,
                                            const map_grid& grid,
                                            std::uint8_t disk) {
  door_evidence evidence;
  for (unsigned y = 0; y < automap_map_side; ++y) {
    for (unsigned x = 0; x < automap_map_side; ++x) {
      for (const unsigned lane : lanes) {
        const std::uint8_t face = face_of(grid, x, y, lane);
        if (face != 0 && style_of(grid, x, y, lane) >= face_shut_door) {
          evidence.seen =
              static_cast<std::uint16_t>(evidence.seen | (1U << face));
        }
      }
    }
  }
  for (std::uint8_t nibble = 1; nibble <= 15; ++nibble) {
    const std::uint16_t block = block_serving(cpu, ds, nibble);
    if (block == wall_set_unknown) {
      continue;
    }
    const auto row = static_cast<unsigned>((nibble - 1U) % wall_faces_per_slot);
    for (const door_graphics& known : door_table) {
      if (known.disk == disk && known.block == block &&
          ((known.rows >> row) & 1U) != 0) {
        evidence.from_table =
            static_cast<std::uint16_t>(evidence.from_table | (1U << nibble));
      }
    }
  }
  return evidence;
}

/// Is this face a door?
///
/// A shut one always is. A passable one is a door when its nibble is a
/// door graphic — and when *nothing at all* is known about this map's
/// wall sets, every passable face is drawn as a door, which is the
/// pre-nibble rule and is better than drawing none.
[[nodiscard]] bool is_door(std::uint8_t face, std::uint8_t style,
                           std::uint16_t doors) {
  if (face == 0 || style == face_solid) {
    return false;
  }
  if (style >= face_shut_door) {
    return true;
  }
  if (doors == 0) {
    return true;
  }
  return ((doors >> face) & 1U) != 0;
}

/// Which of the four rules made this face a door (#268).
///
/// Called only where `is_door` has already said yes, so it is a
/// classification and not a second decision: nothing it answers is read
/// back by anything that draws. `seen` is the map's own shut kinds, and
/// a kind in it is the rule's however many other witnesses agree.
[[nodiscard]] automap_door_evidence door_evidence_for(
    std::uint8_t face, std::uint8_t style, std::uint16_t doors,
    std::uint16_t seen) noexcept {
  if (style >= face_shut_door) {
    return automap_door_evidence::shut;
  }
  if (doors == 0) {
    return automap_door_evidence::no_evidence;
  }
  return ((seen >> face) & 1U) != 0 ? automap_door_evidence::seen_kind
                                    : automap_door_evidence::table_kind;
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

/// One cell's wall stroke along one lane.
///
/// `thick` grows the stroke inward from the cell's edge, `inset` shortens
/// it at both ends, and `gap` opens a hole in the middle of it. Between
/// them those three draw all three kinds of border the panel has: a solid
/// wall is one pixel thick and unbroken, a way through is one pixel thick
/// with the middle missing and a stub at each end, and a door is a
/// two-pixel leaf inset by one so it reads as a door *within* the wall
/// rather than as a differently coloured wall.
void stroke(panel_pixels& panel, int x0, int y0, unsigned lane, int thick,
            int inset, int gap, std::uint8_t colour) noexcept {
  const auto cell = static_cast<int>(automap_cell_pixels);
  const int first = (cell - gap) / 2;
  const int last = first + gap;
  for (int t = 0; t < thick; ++t) {
    for (int i = inset; i < cell - inset; ++i) {
      if (gap > 0 && i >= first && i < last) {
        continue;
      }
      switch (lane) {
        case lane_north:
          put(panel, x0 + i, y0 + t, colour);
          break;
        case lane_south:
          put(panel, x0 + i, y0 + cell - 1 - t, colour);
          break;
        case lane_west:
          put(panel, x0 + t, y0 + i, colour);
          break;
        default:
          put(panel, x0 + cell - 1 - t, y0 + i, colour);
          break;
      }
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

// ---------------------------------------------------------------------------
// The zone label (M5-E2b)
// ---------------------------------------------------------------------------
//
// The band the panel's geometry leaves — `automap.h`'s eight text columns
// to the right of the map — says where the party is, because nothing else
// on this screen does: the program's own status row under the panel shows
// coordinates, a compass and the clock, and never a place.

using font_table = std::array<std::uint8_t, font_bytes>;

/// The glyph box in the signed numbers everything that draws works in,
/// which is also where `panel_width` and `panel_height` above are.
constexpr int glyph_rows = static_cast<int>(font_glyph_bytes);
constexpr int glyph_columns = 8;

/// The program's own glyphs, copied out of its memory through the bus.
///
/// False when the far pointer is not one that can be followed, which
/// covers the program's own "the font is not installed yet" and every way
/// a pointer it has not set up can point somewhere a seam must not read.
[[nodiscard]] bool read_font(cpu::processor& cpu, std::uint16_t ds,
                             font_table& font) {
  const far_pointer pointer = far_at(cpu, ds, data_font_pointer);
  if (!followable(pointer, font_bytes)) {
    return false;
  }
  for (std::uint16_t i = 0; i < font_bytes; ++i) {
    font[i] = cpu.read_byte(pointer.segment, at(pointer.offset, i));
  }
  return true;
}

/// One run of text into the panel, in the program's own glyphs.
///
/// Rasterized here, into this seam's own linear buffer, rather than by
/// calling the program's text primitive: the screen is planar and the
/// program's, the panel is linear and this seam's, and the panel goes
/// onto the planes in one piece. The glyphs are the same bytes the
/// program draws its own menus with, so the label is pixel-identical to
/// the text around it — which is what "in the game's own font" has to
/// mean to be worth claiming.
void draw_text(panel_pixels& panel, int x, int y, std::string_view text,
               std::uint8_t colour, const font_table& font) noexcept {
  for (const char ch : text) {
    auto code = static_cast<std::uint8_t>(ch);
    if (code >= 0x61 && code <= 0x7A) {
      code = static_cast<std::uint8_t>(code - 0x20);
    }
    const auto glyph = static_cast<std::size_t>(code % font_glyphs) *
                       static_cast<std::size_t>(font_glyph_bytes);
    for (int row = 0; row < glyph_rows; ++row) {
      const std::uint8_t bits = font[glyph + static_cast<std::size_t>(row)];
      for (int bit = 0; bit < glyph_columns; ++bit) {
        if (((bits >> (glyph_columns - 1 - bit)) & 1U) != 0) {
          put(panel, x + bit, y + row, colour);
        }
      }
    }
    x += glyph_columns;
  }
}

/// The marker that says a word may be split here.
constexpr char soft_break = '|';

/// A friendly name for a map, by the two bytes the program tells one from
/// another with: the disk its files come from and the area's script id.
///
/// **A name is a fact** (CONTRIBUTING.md: "a SHA-256, a name, and the
/// offsets a fact table needs"). This is a table of short labels for
/// places, of the same kind as the addresses and the door table beside
/// it, and not a line of anybody's prose. It has to be a table because
/// the program holds no such string anywhere for the panel to read: the
/// names live in its scripts as narration, and each of these was derived
/// from one. Carried from the proven design, which is what PLAN.md §5
/// means by re-expressing it as-is.
///
/// The id is the *script* id, and a sub-map that a script swaps in under
/// a fixed area keeps its parent's — so those need no row of their own.
/// The third byte of the map identity is what tells them apart for the
/// fog (`automap.h`), and to a player they are the same place.
///
/// A `|` is a soft break: a point inside a word where the label may be
/// split across two lines of the band with a hyphen. It costs no column
/// and prints nothing where the word fits. Six of these have a word
/// longer than the band is wide, and each carries one at a syllable.
struct zone_name {
  std::uint8_t disk;
  std::uint8_t area;
  std::string_view name;
};

constexpr std::array<zone_name, 29> zone_names{
    {{.disk = 1, .area = 18, .name = "PODOL PLAZA"},
     {.disk = 1, .area = 24, .name = "TEMPLE OF BANE"},
     {.disk = 2, .area = 9, .name = "STOJANOW GATE"},
     {.disk = 2, .area = 15, .name = "MENDOR'S LIBRARY"},
     {.disk = 2, .area = 20, .name = "SLUMS"},
     {.disk = 3, .area = 0, .name = "NEW PHLAN"},
     {.disk = 3, .area = 8, .name = "CITY HALL"},
     {.disk = 3, .area = 11, .name = "TRAINING HALL"},
     {.disk = 3, .area = 14, .name = "KOVEL MANSION"},
     {.disk = 4, .area = 2, .name = "TEXTILE HOUSE"},
     {.disk = 4, .area = 10, .name = "VALHIN|GEN GRAVE|YARD"},
     {.disk = 4, .area = 21, .name = "SOKAL KEEP"},
     {.disk = 5, .area = 3, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 4, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 5, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 6, .name = "VALJEVO CASTLE"},
     {.disk = 5, .area = 7, .name = "VALJEVO CASTLE"},
     {.disk = 6, .area = 1, .name = "BUCCA|NEER BASE"},
     {.disk = 6, .area = 19, .name = "DRAGON'S CAVE"},
     {.disk = 6, .area = 25, .name = "WILDER|NESS"},
     {.disk = 6, .area = 28, .name = "ZHENTIL OUTPOST"},
     {.disk = 7, .area = 17, .name = "NOMAD CAMP"},
     {.disk = 7, .area = 22, .name = "SORCER|ER'S ISLAND"},
     {.disk = 7, .area = 23, .name = "YARASH'S PYRAMID"},
     {.disk = 7, .area = 26, .name = "WILDER|NESS"},
     {.disk = 8, .area = 13, .name = "KOBOLD CAVES"},
     {.disk = 8, .area = 16, .name = "LIZARD|MAN KEEP"},
     {.disk = 8, .area = 27, .name = "WILDER|NESS"},
     {.disk = 8, .area = 29, .name = "KUTO'S WELL"}}};

/// How many characters the fallback needs: "AREA " and three digits.
constexpr std::size_t zone_fallback_bytes = 8;
using zone_fallback = std::array<char, zone_fallback_bytes>;

/// The label for a map, or `AREA <n>` where the table has no row.
///
/// The fallback is written into the caller's buffer rather than a local
/// one, so the view this returns always names storage that outlives it.
/// A map with no row is a map nobody has named yet, and saying its number
/// is a better answer than saying nothing: the panel is still a map of
/// somewhere, and the number is what a player would put in a bug report.
[[nodiscard]] std::string_view zone_label(std::uint8_t disk, std::uint8_t area,
                                          zone_fallback& fallback) noexcept {
  for (const zone_name& zone : zone_names) {
    if (zone.disk == disk && zone.area == area) {
      return zone.name;
    }
  }

  std::size_t used = 0;
  for (const char ch : std::string_view{"AREA "}) {
    fallback[used++] = ch;
  }
  std::array<char, 3> digits{};
  std::size_t count = 0;
  auto value = static_cast<unsigned>(area);
  do {
    digits[count++] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  } while (value != 0);
  while (count > 0) {
    fallback[used++] = digits[--count];
  }
  return std::string_view{fallback.data(), used};
}

/// How wide a word prints, up to the next space or the end of the label.
/// Soft-break markers are not characters and cost no column.
[[nodiscard]] std::size_t word_columns(std::string_view text) noexcept {
  std::size_t columns = 0;
  for (const char ch : text) {
    if (ch == ' ') {
      break;
    }
    if (ch != soft_break) {
      ++columns;
    }
  }
  return columns;
}

/// Where the band's first line of text sits, and how far apart the lines
/// are: an eight-pixel glyph with two pixels of leading. The top matches
/// the inset the map's own first row of cells has from the panel's edge.
constexpr int label_top = 8;
constexpr int label_line_pitch = 10;

/// The label's colour: the yellow the program highlights its own text in.
/// The same index the door leaves are drawn in, and deliberately so —
/// this panel has one accent colour and it is the game's.
constexpr std::uint8_t colour_label = 14;

/// The zone label into the band: greedy word wrap to the band's columns,
/// each line centred.
///
/// A word too long for a whole line on its own is broken at the last soft
/// break that still leaves room for the hyphen, and the remainder wraps —
/// so a name marked `WILDER|NESS` sets as `WILDER-` over `NESS`. With no
/// marker at all, which is the `AREA <n>` fallback and any name nobody
/// has marked yet, it takes what fits. That is the deliberate answer
/// rather than an oversight: a new name never vanishes, it just breaks in
/// an ugly place until somebody puts a marker in it.
void draw_label(panel_pixels& panel, std::uint8_t disk, std::uint8_t area,
                const font_table& font) noexcept {
  zone_fallback fallback{};
  const std::string_view name = zone_label(disk, area, fallback);

  constexpr auto columns = static_cast<std::size_t>(automap_text_columns);
  constexpr int band_x = static_cast<int>(automap_band_x);
  constexpr int band_width =
      static_cast<int>(automap_panel_width - automap_band_x);
  std::array<char, columns> line{};
  std::size_t p = 0;
  int y = label_top;
  while (p < name.size() && y + glyph_rows <= panel_height) {
    std::size_t filled = 0;
    while (p < name.size() && name[p] == ' ') {
      ++p;
    }
    while (p < name.size()) {
      const std::size_t width = word_columns(name.substr(p));
      const std::size_t separator = filled > 0 ? 1U : 0U;
      if (filled + separator + width <= columns) {
        if (separator != 0) {
          line[filled++] = ' ';
        }
        for (; p < name.size() && name[p] != ' '; ++p) {
          if (name[p] != soft_break) {
            line[filled++] = name[p];
          }
        }
        // Past the space that ended it, so the next pass measures the
        // next word rather than an empty one and pads the line out with
        // separators until it runs out of columns.
        while (p < name.size() && name[p] == ' ') {
          ++p;
        }
        continue;
      }
      if (filled > 0) {
        // It will fit on a line of its own; leave it for the next one.
        break;
      }

      // Alone on the line and still too long: hyphenate at the last
      // marker that fits, and take what fits where there is none.
      std::size_t cut = 0;
      bool marked = false;
      std::size_t seen = 0;
      for (std::size_t q = p; q < name.size() && name[q] != ' '; ++q) {
        if (name[q] != soft_break) {
          ++seen;
        } else if (seen + 1 <= columns) {
          cut = q;
          marked = true;
        }
      }
      if (marked) {
        for (std::size_t q = p; q < cut; ++q) {
          if (name[q] != soft_break) {
            line[filled++] = name[q];
          }
        }
        line[filled++] = '-';
        p = cut + 1;
      } else {
        for (; filled < columns && p < name.size() && name[p] != ' '; ++p) {
          if (name[p] != soft_break) {
            line[filled++] = name[p];
          }
        }
      }
      break;
    }

    draw_text(
        panel,
        band_x +
            ((band_width - (static_cast<int>(filled) * glyph_columns)) / 2),
        y, std::string_view{line.data(), filled}, colour_label, font);
    y += label_line_pitch;
  }
}

/// The colour a wall face of kind `nibble` is drawn in.
///
/// Its texture's own colour where one has been worked out (M5-E2a), and
/// otherwise the area's frame colour — the colour the program frames that
/// area's screens in — and brown when the program has not set even that.
/// The fallback matters: a wall set that has not finished loading, or a
/// nibble whose tiles are entirely black, has to draw as *something*.
///
/// The one rule on top: a wall the same colour as the floor is not a wall
/// a player can see, so it is shifted to its bright twin — away from
/// black if that is where it lands, and away from the door yellow, which
/// means something else on this panel.
[[nodiscard]] std::uint8_t wall_colour(const automap_state& state,
                                       std::uint8_t nibble,
                                       std::uint8_t frame_colour,
                                       std::uint8_t floor_colour) noexcept {
  std::uint8_t colour = state.wall_colour_known(nibble)
                            ? state.wall_colour(nibble)
                            : (frame_colour != 0 ? frame_colour : colour_brown);
  if (colour == floor_colour) {
    colour = static_cast<std::uint8_t>(colour ^ 8U);
    if (colour == colour_door) {
      colour = colour_white;
    }
    if (colour == colour_black) {
      colour = colour_grey;
    }
  }
  return colour;
}

/// Work out, once for this map, what colour each kind of wall face on it
/// is and which kinds are doors.
///
/// Only the faces the map actually uses are histogrammed — there are
/// fifteen possible and a map uses a handful, and each one costs a walk
/// over every pixel of a tile.
void learn_appearance(machine& box, std::uint16_t ds, const sample& now,
                      const map_grid& grid, std::uint16_t banks) {
  automap_state& state = box.automap();
  state.begin_appearance(now.area, now.geo, banks);
  const door_evidence doors =
      door_nibbles_of(box.processor(), ds, grid, now.disk);
  state.set_door_nibbles(doors.seen, doors.from_table);

  std::uint16_t wanted = 0;
  for (unsigned y = 0; y < automap_map_side; ++y) {
    for (unsigned x = 0; x < automap_map_side; ++x) {
      for (const unsigned lane : lanes) {
        const std::uint8_t face = face_of(grid, x, y, lane);
        if (face != 0) {
          wanted = static_cast<std::uint16_t>(wanted | (1U << face));
        }
      }
    }
  }
  for (std::uint8_t nibble = 1; nibble <= 15; ++nibble) {
    if (((wanted >> nibble) & 1U) == 0) {
      continue;
    }
    const int colour = texture_colour(box.processor(), ds, nibble);
    if (colour > 0) {
      state.set_wall_colour(nibble, static_cast<std::uint8_t>(colour));
    }
  }
}

/// The whole panel into its own buffer. `font` is null when the program
/// has not installed its glyphs yet, which is the one thing that can
/// leave the band empty.
void render(automap_state& state, const automap_record& map,
            const map_grid& grid, const sample& now, const font_table* font) {
  panel_pixels& panel = state.pixels();
  panel.fill(colour_black);

  const unsigned px = now.x;
  const unsigned py = now.y;
  const unsigned facing = now.facing;
  const std::uint8_t floor_colour = now.floor_colour;
  const std::uint8_t frame_colour = now.frame_colour;

  // The band first. It is disjoint from the map's sixteen cells by
  // construction (`automap.h`), so nothing below can reach it and nothing
  // here can reach the map.
  if (font != nullptr) {
    draw_label(panel, now.disk, now.area, *font);
  }

  const std::uint16_t doors = state.door_nibbles();
  const std::uint16_t seen_kinds = state.door_nibbles_seen();
  state.begin_doors_drawn();
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
        // party cell's own lane. Folding the far face in with an `or`
        // would paint a door leaf over a plain solid wall the player can
        // neither walk through nor see a door in.
        //
        // A **shut** door is the exception, because being shut is a
        // property of the border rather than of a side: one recorded only
        // on the far cell would otherwise draw as a blank wall.
        const bool far_shut = far_face != 0 && far_style >= face_shut_door;
        bool door = false;
        automap_door_evidence why = automap_door_evidence::shut;
        if (near_face != 0) {
          if (is_door(near_face, near_style, doors)) {
            door = true;
            why = door_evidence_for(near_face, near_style, doors, seen_kinds);
          } else if (far_shut) {
            door = true;
          }
        } else if (is_door(far_face, far_style, doors)) {
          door = true;
          why = door_evidence_for(far_face, far_style, doors, seen_kinds);
        }
        const std::uint8_t face_style = near_face != 0 ? near_style : far_style;
        const std::uint8_t wall =
            wall_colour(state, near_face != 0 ? near_face : far_face,
                        frame_colour, floor_colour);

        if (door) {
          // Which of the four rules put it there (#268). A counter and
          // nothing else: the pixels below are what they were before it
          // was kept, and a host prints it so a still of a leaf is
          // evidence for a rule rather than an inference about one.
          state.count_door_drawn(why);

          // The wall-colour flanks first, at the ordinary one-pixel
          // thickness, so the leaf stays joined to the wall on either
          // side of it rather than floating in a gap; then the leaf over
          // them, inset by one at each end and two pixels thick.
          stroke(panel, x0, y0, lane, 1, 0, 0, wall);
          stroke(panel, x0, y0, lane, 2, 1, 0, colour_door);
        } else if (face_style == face_passable) {
          stroke(panel, x0, y0, lane, 1, 0,
                 static_cast<int>(automap_cell_pixels) - 2, wall);
        } else {
          stroke(panel, x0, y0, lane, 1, 0, 0, wall);
        }
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

/// The two keystrokes that step the program's roster cursor, which the
/// panel takes for as long as it is up (M5-E2d).
///
/// **Why it takes them at all.** The adventuring screen answers a key it
/// has no command for by stepping that cursor and then redrawing the
/// party list — which is the block of cells the panel is drawn on. The
/// map is painted over, this seam puts it back on the next pass, and what
/// the player sees is a flash. Most of those keys are a stray press; two
/// of them are a command a player means, the next and the previous party
/// member, and a command whose whole visible effect is behind the panel
/// is one the panel may decline while it is the thing on the screen.
///
/// **Why two shapes each.** They reach the program as one of two things.
/// With Num Lock on the keypad sends the digits, which the program's own
/// menu reader translates into command letters through a table in its
/// data segment; with Num Lock off the same keys send extended
/// keystrokes, and the program takes those at their scan code — which,
/// for these two keys, *is* the letter the table would have produced.
/// Hence a character for the one and a scan code for the other. The
/// character alone is enough here, unlike Tab: nothing else on this
/// machine makes a '1'.
constexpr std::uint8_t key_next_member_char = '1';
constexpr std::uint8_t key_prev_member_char = '7';
constexpr std::uint8_t key_next_member_scan = 0x4F;
constexpr std::uint8_t key_prev_member_scan = 0x47;

/// What the keystroke at the head of the buffer is, to this seam.
enum class claimable : std::uint8_t {
  /// Somebody else's key. Every key is this one, nearly always.
  none,
  /// Tab: show the panel, or take it away.
  panel,
  /// A roster-cursor step, which is only this seam's while the panel is
  /// the thing on those cells.
  roster_cursor,
};

[[nodiscard]] claimable claimable_of(std::uint16_t key) noexcept {
  if (key == key_tab) {
    return claimable::panel;
  }
  const auto character = static_cast<std::uint8_t>(key & 0xFFU);
  const auto scan = static_cast<std::uint8_t>(key >> 8U);
  if (character == key_next_member_char || character == key_prev_member_char) {
    return claimable::roster_cursor;
  }
  if (character == 0 &&
      (scan == key_next_member_scan || scan == key_prev_member_scan)) {
    return claimable::roster_cursor;
  }
  return claimable::none;
}

/// Take the next keystroke out of the BIOS buffer if it is one this seam
/// wants **right now** — which is the whole of the fidelity argument: a
/// key the seam is not going to act on is left exactly where the program
/// would have found it.
[[nodiscard]] claimable claim_key(cpu::processor& cpu, std::uint16_t ds,
                                  bool want_panel, bool want_cursor) {
  if (cpu.read_byte(ds, data_key_pushback) != 0) {
    // The second half of an extended key is waiting. The two halves have
    // to stay adjacent, so nothing is touched on this pass.
    return claimable::none;
  }

  const std::uint16_t head =
      cpu.read_word(bda::segment, bda::keyboard_buffer_head);
  const std::uint16_t tail =
      cpu.read_word(bda::segment, bda::keyboard_buffer_tail);
  if (head == tail) {
    return claimable::none;
  }
  const claimable which = claimable_of(cpu.read_word(bda::segment, head));
  if (which == claimable::none || (which == claimable::panel && !want_panel) ||
      (which == claimable::roster_cursor && !want_cursor)) {
    return claimable::none;
  }

  auto next = static_cast<std::uint16_t>(head + 2U);
  if (next >= bda::keyboard_buffer_end) {
    next = bda::keyboard_buffer;
  }
  cpu.write_word(bda::segment, bda::keyboard_buffer_head, next);
  return which;
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

/// Put the party roster back, because the panel wrote over it and only
/// the program can redraw it from live state.
///
/// **The roster and nothing else** (M5-E2d). Until this, the panel closed
/// by calling the program's per-mode screen composer, which repaints the
/// viewport and the status line as well. A vendor's portrait lives in the
/// viewport: closing the panel in the middle of a conversation painted
/// the 3D view over the person the player was talking to and left the
/// question on the screen with nothing asking it. The panel covers the
/// roster, so the roster is what it owes back, and the property the
/// composer was chosen for survives — the drawer paints from live state,
/// which a snapshot of the pixels could never do, since the program
/// redraws single roster rows while the panel is up.
///
/// **Two calls, because the drawer alone is not enough.** It puts the
/// header on the roster's own row and one row per member below it, and
/// clears exactly one row after the last — so the panel's first row,
/// which is above that header, and every row below the party would keep
/// their pixels. The clear ahead of it is the panel's rect exactly, which
/// is the same rect the covered-cells test is made in.
///
/// A batch (`seam_context::call_program`, #188). The panel is marked down
/// *before* it is queued: when the batch finishes the engine offers this
/// point again, and a handler that had not already recorded what it was
/// doing would queue the same calls a second time.
void give_the_roster_back(machine& box, seam_context& ctx, std::uint16_t ds) {
  automap_state& state = box.automap();
  state.set_panel_on_screen(false);
  state.set_drawn_signature(0);

  cpu::processor& cpu = box.processor();
  const std::uint8_t mode = cpu.read_byte(ds, data_game_mode);
  if (mode != mode_camp && mode != mode_adventure_flat &&
      mode != mode_adventure) {
    // Belt and braces, and the program's own rule: the roster is only
    // there to be redrawn on the modes that have one. A repaint the
    // program cannot perform would leave a corrupted screen, which is a
    // worse answer than a missing one.
    return;
  }

  const auto image = static_cast<std::uint16_t>(ctx.image_base() / 16U);
  const std::array<std::uint16_t, 4> clear{
      automap_panel_left_col, automap_panel_top_row, automap_panel_right_col,
      automap_panel_bottom_row};
  const std::array<std::uint16_t, 2> current{
      cpu.read_word(ds, at(data_current_member, 2)),
      cpu.read_word(ds, data_current_member)};
  (void)(ctx.call_program(image, image_clear_region, clear) &&
         ctx.call_program(
             static_cast<std::uint16_t>(image + roster_draw_paragraph),
             roster_draw_offset, current));
}

/// Take the panel away because somebody other than the adventuring
/// screen is asking the player something, and the panel is sitting on the
/// party roster while they answer (M5-E2d).
///
/// **Only when it is really on the screen**, and that is the whole of the
/// distinction. Everything that takes the panel's *cells* cleared them
/// first and is drawing there: the panel has already stopped claiming
/// them, the program will repaint the roster when it is done, and the map
/// comes back the way it always has. What is left over is the case this
/// is for — a question that takes neither the screen nor the cells, and
/// would otherwise be answered from behind a map.
///
/// True when it acted, and then the handler is finished for this pass: a
/// batch may be outstanding, and there is nothing to draw while the panel
/// is coming down anyway.
[[nodiscard]] bool close_for_the_program(machine& box, seam_context& ctx,
                                         std::uint16_t ds) {
  automap_state& state = box.automap();
  if (!state.panel_open() || !state.panel_on_screen()) {
    return false;
  }
  state.set_panel_open(false);
  give_the_roster_back(box, ctx, ds);
  return true;
}

/// What the panel does with a Tab it has decided is its own. True when
/// the roster is coming back through a batch, which is the caller's cue
/// that it is finished for this pass.
[[nodiscard]] bool toggle_panel(machine& box, seam_context& ctx,
                                std::uint16_t ds) {
  automap_state& state = box.automap();
  const bool open = !state.panel_open();
  state.set_panel_open(open);
  if (!open && state.panel_on_screen()) {
    give_the_roster_back(box, ctx, ds);
    return true;
  }
  state.set_drawn_signature(0);
  return false;
}

/// Whether a roster-cursor step is this seam's key on this pass: only
/// while the panel is the thing on the cells that step would repaint.
[[nodiscard]] bool cursor_keys_are_the_panels(
    const automap_state& state) noexcept {
  return state.panel_open() && !state.panel_covered();
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

  if (!state.at_command_bar()) {
    // Somebody other than the adventuring screen is asking the player
    // something. The panel comes down if it is sitting on the roster
    // while they answer, and its key is nobody's until the party's own
    // bar is back. Exploration is deliberately *not* gated on this — the
    // party can be standing where a script has something to say about,
    // and a map that skipped that square would stay wrong for the rest of
    // the session. Only the presentation and the key yield, which is the
    // line the covered-cells rule draws too.
    if (close_for_the_program(box, ctx, ds)) {
      return;
    }
  } else {
    switch (claim_key(cpu, ds, true, cursor_keys_are_the_panels(state))) {
      case claimable::panel:
        if (toggle_panel(box, ctx, ds)) {
          // The roster is on its way back through a batch. Nothing else
          // this pass.
          return;
        }
        break;
      case claimable::roster_cursor:
      case claimable::none:
        // A cursor step taken and nothing done with it — its whole
        // visible effect is a repaint of the cells the panel is sitting
        // on — or nobody's key at all. Neither has anything more to do.
        break;
    }
  }

  // **The overland is recorded here too** (M5-E5b, #254). It is not this
  // panel's screen and never will be — the panel stays gated on the
  // interior view below — but it is the same *store*, so a player who has
  // only the automap switched on still has a wilderness trail when they
  // switch the explored overlay on. One recorder, two callers
  // (`automap_overland.h`); this caller draws nothing with what it
  // returns, and on any screen but the travel view it costs three bytes
  // of the data segment and returns.
  (void)observe_overland(box, ctx, ds);

  if (!adventuring(cpu, ds)) {
    return;
  }
  sample now{};
  if (!take_sample(cpu, ds, now)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // Where the party is, as one number: what the fog was last brought up
  // to date from, and most of what the panel was last drawn from.
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

  // The whole picture, as one number. Everything the panel is drawn from
  // is in it: where the party is (which is also everything the fog and
  // the marks can depend on), the two colours the program computed for
  // this area, and which tile sets are loaded — because that last is what
  // the wall colours were histogrammed out of, and a wall set swapped
  // under a fixed map identity has to redraw them.
  //
  // What is deliberately *not* in it is the exploration store's serial.
  // It only ever moves when the party's cell does, which is already here,
  // and having it would mean deciding whether to draw before or after the
  // fog was brought up to date.
  const std::uint16_t banks = tile_bank_generation(cpu, ds);
  std::uint32_t drawn = mix(where, now.floor_colour);
  drawn = mix(drawn, now.frame_colour);
  drawn = mix(drawn, banks);
  // And whether the glyphs are there, so a panel first drawn with an empty
  // band gets its label the moment the program installs its font.
  drawn = mix(drawn, cpu.read_word(ds, at(data_font_pointer, 2)));
  if (drawn == 0) {
    // Zero is this seam's "nothing has been drawn" (automap.h), so it is
    // not allowed to be a real answer.
    drawn = 1;
  }

  // The journal reader is the same cells (M5-E4, #175), and it is modal
  // over the map: while an entry is up the map does not draw, and it comes
  // back on its own when the entry is put away — the reader closes by
  // asking the program to paint the roster again, which is what clears
  // this seam's own drawn signature. Neither seam knows anything else
  // about the other and either works with the other switched off.
  const bool shown = state.panel_open() && !state.panel_covered() &&
                     !box.journal().reader_open();
  const bool want_reveal = where != state.revealed_signature();
  const bool want_appearance =
      shown && !state.appearance_is_for(now.area, now.geo, banks);
  const bool want_draw =
      shown && (drawn != state.drawn_signature() || !state.panel_on_screen());
  if (!want_reveal && !want_appearance && !want_draw) {
    // The common case by a wide margin: the program is polling and
    // nothing about the party or the screen has moved since the last
    // pass. Six bytes and four words of the data segment were read to
    // decide it, and the map itself was not touched.
    return;
  }

  map_grid grid{};
  if (!copy_map(cpu, now, grid)) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  if (want_appearance) {
    learn_appearance(box, ds, now, grid, banks);
  }
  if (want_reveal) {
    const std::uint32_t was = state.serial();
    reveal_from(state, state.record_for(now.disk, now.area, now.geo), grid,
                now.x, now.y, now.facing);
    state.set_revealed_signature(where);
    if (state.serial() != was) {
      // Something is explored that was not (M5-E2c). The host is told so
      // it can persist beside the save, and the serial is the argument
      // because it is the one number that says *which* version was
      // handed over. Nothing is done with the answer: a host that has
      // attached nothing, or has not been asked to store anything, is
      // the ordinary case and not a failure.
      (void)ctx.call_host(seam_host_service::automap_update, state.serial());
    }
  }
  if (!want_draw) {
    return;
  }

  const automap_record* map = state.find(now.disk, now.area, now.geo);
  if (map == nullptr) {
    return;
  }
  font_table font{};
  const bool have_font = read_font(cpu, ds, font);
  render(state, *map, grid, now, have_font ? &font : nullptr);
  blit(box, state);
  state.set_panel_on_screen(true);
  state.set_drawn_signature(drawn);
}

/// The program is about to wait for a key. The claim happens here too,
/// and so does the yield to a script: the program is not between commands
/// here so the panel has nothing to *draw*, but a key it wants may arrive
/// at an unconditional wait that no poll preceded, and a script may be
/// what is doing the waiting.
///
/// **And a key taken here is answered** (#266), which is the one thing
/// this point does that the poll may not: the program has already
/// committed to being handed a key, so a read this seam empties puts it
/// to sleep inside the BIOS where no point of this engine is reached, and
/// the *next* key the player types reaches it unseen.
/// `seam_key_read.h` is the whole of that argument. Every claim is
/// answered and not just Tab's: the roster-cursor keys are taken off the
/// ring by the same call, and a key taken is a key the read is short.
void at_key_read(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  automap_state& state = box.automap();
  if (!state.at_command_bar()) {
    (void)close_for_the_program(box, ctx, ds);
    return;
  }
  const claimable which =
      claim_key(cpu, ds, true, cursor_keys_are_the_panels(state));
  if (which == claimable::none) {
    return;
  }
  if (which == claimable::panel) {
    (void)toggle_panel(box, ctx, ds);
  }
  static_cast<void>(ctx.inject_keystroke(key_ignored_scan, key_ignored_ascii));
}

/// The program is putting a command bar up. Which bar it is says whose
/// screen this is (M5-E2d), and that is the only thing this point does.
///
/// The adventuring screen hands its input routine one of two strings it
/// keeps in its data segment; every other caller — a vendor's yes/no, a
/// script's menu, a shop, the camp bar — hands it a copy built on the
/// stack. So a far pointer into the data segment at one of two known
/// offsets is the party's own command bar, and anything else is not.
void at_command_bar(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  // The reading itself is shared with the explored overlay (#179), which
  // has the same point for the same reason: one reader, so the two seams
  // cannot come to different conclusions about whose screen this is
  // (`automap_overland.h`).
  note_command_bar(box, ds);
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

constexpr std::array<seam_point, 6> automap_points{
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
      .run = &at_roster_drawn},
     {.module = resident_image,
      .offset = command_bar_entry,
      .run = &at_command_bar}}};

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
