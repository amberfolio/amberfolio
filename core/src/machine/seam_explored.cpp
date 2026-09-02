// SPDX-License-Identifier: AGPL-3.0-only
//
// The explored overlay: PLAN.md §5 item 5, M5-E5 (#179), the seam itself
// is M5-E5c (#255) and the marking it draws now is M5-E5f (#263).
//
// Where the party has walked, on the game's own overworld screen. The
// program shows a five-by-five window of a wilderness area's overhead
// map, scrolling with the party; this seam **covers every cell of that
// window the party has not been near with fog**, so what is on the
// screen is the country the party has seen and nothing else.
//
// **This is the second marking, and the first one shipped.** M5-E5c drew
// the opposite picture: the game's whole window as the game drew it, with
// the squares the party had walked lifted one shade brighter. The
// maintainer looked at that on a real run and the answer was that it does
// not read — a shade is a difference a player has to be told about before
// they can see it, and PLAN.md §5's rule for this item is that a person
// with a display decides. So the marking was reversed: what is known is
// drawn by the game, untouched, and what is not is covered. PLAN.md §5
// item 5 carries the change, because the sentence it used to end with
// — "it never obscures the unknown" — is exactly what this now does.
// `docs/explored-overlay.md` §5 keeps the lift as the first design with
// the reason it was rejected, and the fog candidates that were prototyped
// over a real frame beside it.
//
// The facts, the geometry and the three decisions are
// `docs/explored-overlay.md` (M5-E5a, #253), which was written before a
// line of this file. What this comment is about is what the seam does
// with them.
//
//
// The three decisions (docs/seams.md §8)
// --------------------------------------
//
// **Its surface is a setting.** No key, no pull, no panel, no chrome. On,
// it is there whenever the game is showing the overworld; off, it is not.
// So the definition is not a `trigger`, it claims no keystroke, and there
// is nothing for a player to learn beyond switching it on. That is also
// what makes the fidelity claim below the shape it is: this seam is
// *visible* the moment the program shows the overworld at all — a map
// nobody has walked is a window that is almost entirely fog — and no
// amount of not-pressing-anything hides it.
//
// **Its points are addresses**, three of them, all resident, and two of
// the three are the automap's (#173) — which is nothing new; several
// seams in this tree share those. The one that is this seam's own is the
// **return of the program's back-buffer present**. It draws at two of
// them and the paragraph after next says why two.
//
// **What it refuses** is longer than what it does, which is the usual
// proportion here. In order, cheapest first: the data segment not being
// where `image_base()` says; the game mode not being the travel view; the
// view kind not being one of the three wilderness areas; a scripted move
// in flight; the area record's far pointer not pointing inside
// conventional memory; **the word in that record that says the program is
// drawing these areas in the interior view instead**; the party's
// position not being on a 16-by-36 map; the column bias not being one a
// 44-column table could hold; the bar on the screen not being the
// adventuring screen's own; the position not having settled; and the
// party having been near every cell the window is showing, which is the
// one case where there is nothing to cover. Every one of those returns
// having touched no port and no pixel.
//
//
// Why the present's return, and not the painter
// ---------------------------------------------
//
// The program does not draw this screen where a reader would expect. It
// composes the whole thing — the twenty-five tiles and the party's icon —
// into an off-screen buffer, and a resident routine then **presents** it,
// flushing only the scanlines something dirtied, through a second display
// page and a latch copy.
//
// So the entry of that routine is the wrong place: the flush has not
// happened, and anything painted there is about to be copied over. Its
// *return* is the right one. Every path that repaints the window ends
// there — the composer's own redraw, and each step of the icon's
// animation, which advances a phase and presents again — so the overlay
// is repainted after each of them and no captured frame can catch it half
// drawn.
//
// **And that is not enough on its own**, which a driven run found and no
// test had (M5-E5d, #256). A party that loads a saved game and stands
// still gives the program nothing to redraw — so no present ever comes,
// and the trail the host had just read in beside the save stayed
// invisible until the player took a step. A seam that paints only where
// the program paints cannot show state that arrived without a redraw.
//
// So it paints at the keyboard poll as well, which is where the program
// is between commands. That point is reached thousands of times a virtual
// second, so it paints there only when something has moved: where the
// party is, and the exploration store's serial, which moves both when a
// cell is revealed and when a host reads a slot's table in. The present's
// return does not consult that signature at all, because the program has
// just wiped whatever was on those rows.
//
// The alternative, painting into the program's own back buffer so that
// the program's own present carries the marks, is memory surgery on a
// buffer the program reads back: its dirty tracking, its save-under path
// and its next composition all read it, and the marks would become part
// of what the program believes it drew. It is rejected here and revisited
// only if a driven run shows this path flickering (#256).
//
//
// The marking: fog, and why it is solid black
// -------------------------------------------
//
// A cell the party has not been near is **covered with black** — all four
// planes cleared over its 24 by 24 pixels, so it is colour 0, which is
// the colour the rest of this very screen already is.
//
// Ten markings have now been prototyped over real dumped frames of this
// screen: seven for the lift M5-E5c shipped, and, after the maintainer's
// look, six for the fog that replaced it. `docs/explored-overlay.md` §5
// has all of them with their reasons. The four that decided this one:
//
//   * **it is the one colour that cannot read as terrain.** The nearer
//     candidates — a checkerboard of black over the tile at one pixel in
//     two, at two-by-two blocks, at four-by-four, and a dither at one
//     pixel in four — all leave the tile's own hue showing through, and
//     at the resolution this game runs at a half-covered green tile
//     reads as *a different kind of green tile*. That is the same
//     objection that rejected the sparse dither as a marking in #253,
//     and it is worse here, because a fog covers most of the window
//     rather than a square of it;
//   * **it is the game's own vocabulary for the unknown.** Black is
//     already most of this screen: the message rows under the window and
//     the panel beside it are black, the game's 3D view draws black
//     beyond what the party can see, and the window sits inside the
//     game's own drawn border. So the fogged window reads as the border
//     framing a smaller opening, which is a thing this screen already
//     looks like — not as a pattern laid over it;
//   * **it is the same on every terrain.** A fog that lets the tile show
//     through is a different fog on grass, on water and on rough ground,
//     and a player would have to learn three of them. This one covers,
//     so there is one thing to learn. It is also the answer to the
//     failure the lift had: dimming was invisible on water because water
//     is a solid dark blue already, and no such case exists here;
//   * **it costs four planes and no read-back.** `map mask = 0x0F` and a
//     run of `0x00` bytes: a cell is three whole bytes by 24 scanlines,
//     so 72 byte writes, and 1,728 for a window with 24 cells fogged —
//     the same cost the lift had, against the automap panel's 9,856.
//     **Every fog that shows the terrain through needs a read of the
//     video window before each write**, to load the adapter's latches
//     for the pixels it is leaving alone; this one needs none, so no
//     latch of the program's is disturbed by a seam that is only looking
//     at a screen.
//
//
// How far the party sees
// ----------------------
//
// `explored_reveal_radius` (`automap.h`) — a Chebyshev distance, and one
// by default. A cell is shown when the party has stood within that many
// cells of it, so standing on a cell uncovers the three-by-three around
// it, and the trail a walk leaves is a three-wide corridor.
//
// **The record still holds only the cells the party stood on.** The
// reveal is the *dilation* of that by the radius, worked out here when
// the window is drawn, so a player who turns the knob up tomorrow sees
// more of the map they already walked rather than a map that has to be
// walked again. It is also why nothing in `automap.h`'s stored layout
// moved for this change and no sidecar anybody has is invalidated.
//
// **Why one and not the two or three that were asked for.** The window
// is five cells across and the party is its middle cell in open country,
// so every cell on the screen is already within two of the party. At a
// radius of two this seam covers nothing at all except where a map's own
// edge pushes the party off centre — measured, on the same walk: eight
// steps, and not one fogged cell until the party reaches the map's edge.
// One is the largest radius that leaves anything to cover.
//
//
// Two things this never covers
// ----------------------------
//
// **The party's own cell**, which is where the program draws the party's
// icon. At any radius of one or more it is revealed anyway — the party is
// standing on it — but the check is written down as its own line rather
// than left to arithmetic, because a fog over the party's own sprite is
// the one mistake here that would be a bug and not a preference.
//
// **Every pixel outside the five-by-five window.** The window is 120 by
// 120 at (8, 8) and every cell of it begins on a byte boundary and is
// three whole bytes wide, so the fog is written into whole bytes inside
// that rect and reaches nothing else — which is what lets a confinement
// leg (#256) mask the window and assert that the rest of the frame is
// byte for byte the seam-off run.
//
//
// The fidelity claim, stated for this seam (docs/seams.md §8.5)
// -------------------------------------------------------------
//
// **One claim, not two.** M5-E5c had a second and stronger one — on, the
// overworld shown, nothing walked, and the screen still the screen it
// would have been — which held only because a lift marks what is known
// and a map nobody has walked has nothing known on it. A fog marks what
// is *not* known, and a map nobody has walked is all of that: the outer
// ring of the window is covered the moment the party arrives. So that
// claim is gone rather than weakened, here, in `docs/seams.md` §10 and
// §8.5, and in the `wild` / `wild-trail` pair, which now diverges at the
// arrival rather than at the first step. What remains is true and is a
// test:
//
//   **On, and the overworld never shown, a run is byte for byte the run
//   with no engine at all.** Every point reads; the two shared with the
//   automap write only `machine::automap()`, which is observation and not
//   machine state (`automap.h`); and the third returns on the mode byte.
//   `tests/sessions/quiet-explored.rec` says it on the real program.
//
//
// What it is not yet, at the point of definition (docs/seams.md §8.5)
// -------------------------------------------------------------------
//
//   * Everything driven so far has been view kind 2 on disk 6. The other
//     two wilderness areas are the same arithmetic with a different bias
//     and nobody has stood on them (#256).
//   * The bar test is the automap's — a far pointer into the data segment
//     at one of two known offsets. If the travel view hands its input
//     routine a third string, this seam paints nothing there and a driven
//     run is what will say so (#256).
//   * The fog has been prototyped and driven over **grass, coast water
//     and the grey shore between them**, which is what the one area a
//     shipped save reaches has near its start. No frame of rough ground,
//     forest or a road has been under it. It cannot fail on one — the fog
//     does not depend on what it covers, which is the third reason it was
//     chosen — but nobody has seen it there.
//   * The radius is one, and one is the only radius that covers anything
//     on this screen. Whether one is the *right* amount of country to
//     hand a player is a judgement, and #263 is where it is asked.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/processor.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/seam.h"
#include "automap_overland.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts (docs/explored-overlay.md §2)
// ---------------------------------------------------------------------------

/// The SHA-256 of the program image every offset below is a fact about —
/// the baseline edition (edition.h), and only it.
constexpr std::array<std::string_view, 1> explored_binaries{
    "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd"};

/// **The points**, as offsets from the image segment, all three resident.
///
/// The first is this seam's own: the far return of the routine that
/// presents the composed back buffer — the `mov sp, bp / pop bp / retf`
/// that is the only far return in it. Its second route is the
/// disassembly, which opens `push bp / mov bp, sp` and immediately reads
/// the six data-segment words the theory said it must: the adapter byte,
/// the back buffer's far pointer, the per-row dirty flags, the per-row
/// minimum and maximum x, and the per-row destination x.
///
/// The other two are the automap's (`seam_automap.cpp`), shared: the
/// program's "is a key waiting" routine, which is where the program is
/// between commands and therefore where the trail is recorded, and the
/// thunk every menu bar in the game goes up through, which is how any
/// seam here knows whose screen it is on.
constexpr std::uint32_t present_return = 0x649B;
constexpr std::uint32_t key_pending_entry = 0xA6FD;
constexpr std::uint32_t command_bar_entry = 0x3C7A;

/// Where the data segment begins, as an offset in the image — the one
/// fact every data-segment offset is relative to, checked against the DS
/// the program is actually holding rather than trusted.
constexpr std::uint32_t dgroup_offset = 0xC7C0;

/// The game mode, and the value that is the wilderness travel view.
constexpr std::uint16_t data_game_mode = 0x49F3;
constexpr std::uint8_t mode_overland = 3;

/// Which view is up — 2, 3 and 4 are the three wilderness areas — is
/// the shared recorder's to check, and it hands the answer back in its
/// `overland_look` (`automap_overland.h`), so this file does not name
/// that offset a second time.

/// The per-view-kind **column bias**, a byte table indexed by the view
/// kind. It puts each of the three areas in its own sixteen-column band
/// of the one 44-column terrain table, and the seam reads it out of the
/// program rather than carrying its values.
constexpr std::uint16_t data_view_column_bias = 0x3C76;

/// The widest bias a 44-column table could carry, which is what says a
/// byte read here is the byte the facts describe.
constexpr int max_column_bias = 0x2B;

/// The two halves of an overland record's identity — the disk the area's
/// files come from and the area's own id — are read by the shared
/// recorder and come back in its answer (`automap_overland.h`), so this
/// file does not name their offsets a second time.

/// All four EGA planes cleared over a pixel is colour 0, which is black
/// in the program's own palette and is the whole of this seam's drawing.
constexpr std::uint8_t all_planes = 0x0F;
constexpr std::uint8_t all_bits = 0xFF;
constexpr std::uint8_t no_pixels = 0x00;

/// The graphics-controller registers a plane-selected byte write needs,
/// and the values a plain write-mode-0 copy wants. Set rather than
/// assumed: they cannot be read back, and assuming a register you cannot
/// read is not a check (docs/seams.md §3).
constexpr std::uint8_t gc_enable_set_reset_index = 1;
constexpr std::uint8_t gc_data_rotate_index = 3;
constexpr std::uint8_t gc_write_mode_index = 5;
constexpr std::uint8_t gc_bit_mask_index = 8;
constexpr std::uint8_t sequencer_map_mask_index = 2;

/// Bytes per scanline of one plane in the 320-pixel graphics mode the
/// program runs in, and the window it displays through.
constexpr std::uint16_t plane_bytes_per_row = 40;
constexpr std::uint16_t video_window_segment = 0xA000;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

/// The data segment, derived from where the loader put the image and
/// checked against what the program is holding. Zero means "not it", and
/// every handler declines on that.
[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const auto wanted = static_cast<std::uint16_t>((ctx.image_base() / 16U) +
                                                 (dgroup_offset / 16U));
  const std::uint16_t held = cpu.regs()[cpu::sreg::ds];
  return held == wanted ? held : static_cast<std::uint16_t>(0);
}

[[nodiscard]] std::uint16_t at(std::uint16_t base, std::uint16_t by) noexcept {
  return static_cast<std::uint16_t>(base + by);
}

void write_register(machine& box, std::uint16_t index_port,
                    std::uint16_t data_port, std::uint8_t index,
                    std::uint8_t value) {
  box.write_port8(index_port, index);
  box.write_port8(data_port, value);
}

// ---------------------------------------------------------------------------
// The drawing
// ---------------------------------------------------------------------------

/// One cell of the window, covered: 24 scanlines of three whole bytes,
/// written into every plane the sequencer's map mask has already
/// selected.
///
/// No read-back, because none is needed: the byte written is `0x00` and
/// the mask is all four planes, so every pixel of the cell becomes colour
/// 0 whatever it was. That is also why the rect's byte alignment matters
/// — a cell begins at x = 8 + 24j, which is byte column 1 + 3j, and is
/// three whole bytes across (`automap.h`) — and it is the whole of the
/// argument for a fog that covers rather than one that veils: a veil has
/// to keep the pixels it is not covering, and keeping them means reading
/// the video window back into the adapter's latches first.
void fog_cell(machine& box, unsigned column, unsigned row) {
  cpu::processor& cpu = box.processor();
  constexpr unsigned bytes_across = explored_cell_pixels / 8;
  const unsigned first_byte = (explored_window_x / 8) + (column * bytes_across);
  const unsigned first_line = explored_window_y + (row * explored_cell_pixels);
  for (unsigned line = 0; line < explored_cell_pixels; ++line) {
    const auto base = static_cast<std::uint16_t>(
        ((first_line + line) * plane_bytes_per_row) + first_byte);
    for (unsigned byte = 0; byte < bytes_across; ++byte) {
      cpu.write_byte(video_window_segment,
                     at(base, static_cast<std::uint16_t>(byte)), no_pixels);
    }
  }
}

/// The map's own bounds, as the signed type the arithmetic below is in.
/// Named rather than cast at the comparison, which is what
/// `modernize-use-integer-sign-comparison` actually wants.
constexpr int overland_rows = static_cast<int>(automap_overland_rows);
constexpr int overland_columns = static_cast<int>(automap_overland_columns);
constexpr int window_cells = static_cast<int>(explored_window_cells);

/// Has the party stood within `explored_reveal_radius` of this cell?
///
/// **The dilation, computed rather than stored** — the record holds the
/// cells the party walked and nothing else, so a radius that changes
/// tomorrow changes what a player sees of the map they already have
/// rather than what they have to walk again (`automap.h`).
///
/// The neighbourhood is clipped to the map, because a cell off the map is
/// a cell nobody stood on, and the record cannot answer for one.
[[nodiscard]] bool revealed(const automap_record& map, int column,
                            int row) noexcept {
  for (int dy = -explored_reveal_radius; dy <= explored_reveal_radius; ++dy) {
    const int near_row = row + dy;
    if (near_row < 0 || near_row >= overland_rows) {
      continue;
    }
    for (int dx = -explored_reveal_radius; dx <= explored_reveal_radius; ++dx) {
      const int near_col = column + dx;
      if (near_col < 0 || near_col >= overland_columns) {
        continue;
      }
      if (automap_state::seen(map, static_cast<unsigned>(near_col),
                              static_cast<unsigned>(near_row))) {
        return true;
      }
    }
  }
  return false;
}

/// Which of the twenty-five cells of the window are covered, as a bitmap
/// of `row * 5 + column`. Pure, so a test can check it against a store it
/// laid out itself.
///
/// A cell is fogged unless the party has stood within the reveal radius
/// of it — so fog is the default and being shown is what has to be
/// earned, which is the reversal M5-E5f is.
///
/// Two cells are never fogged, and they are different in kind. The one
/// the party is **standing on** is where the program draws the party's
/// icon, and covering it would cover the player's own sprite; it is
/// revealed by the radius as well, at any radius at all, and is written
/// here anyway because that is a rule and not an accident. A cell off
/// this area's own sixteen columns belongs to a **neighbouring
/// wilderness area** — the three of them are bands of one 44-column
/// table and the window may overhang — and this seam has no record for
/// one, so nothing has been stood near it and it is covered like any
/// other unknown.
[[nodiscard]] std::uint32_t cells_to_fog(const automap_record& map, int bias,
                                         int party_x, int party_y) noexcept {
  const explored_window origin =
      explored_window_top_left(bias, party_x, party_y);
  std::uint32_t fogged = 0;
  for (int row = 0; row < window_cells; ++row) {
    const int map_row = origin.row + row;
    for (int column = 0; column < window_cells; ++column) {
      const int map_col = origin.col + column - bias;
      const bool off_this_area = map_col < 0 || map_col >= overland_columns ||
                                 map_row < 0 || map_row >= overland_rows;
      if (map_col == party_x && map_row == party_y) {
        continue;
      }
      if (!off_this_area && revealed(map, map_col, map_row)) {
        continue;
      }
      fogged |= 1U << static_cast<unsigned>((row * window_cells) + column);
    }
  }
  return fogged;
}

// ---------------------------------------------------------------------------
// The handlers
// ---------------------------------------------------------------------------

/// A cheap mixed hash of the few things a repaint depends on. Not a
/// digest of anything and not compared against anything outside this
/// file.
[[nodiscard]] std::uint32_t mix(std::uint32_t seed,
                                std::uint32_t value) noexcept {
  std::uint32_t hash = seed ^ value;
  hash *= 16777619U;
  return hash ^ (hash >> 13U);
}

/// Lay the fog down, if the guard lets it and there is anything to cover.
///
/// `after_a_present` says the program has just put the screen up and
/// whatever this seam had drawn on those rows is gone, so the answer is
/// not compared against what was drawn last time. Everywhere else it is:
/// the keyboard poll is reached thousands of times a virtual second, and
/// a repaint on every pass would be that many.
///
/// **What the signature has in it, and why each.** Where the party is,
/// because the window scrolls with it; the store's serial, because that
/// moves when a cell is revealed *and* when a host reads a saved slot's
/// table in (M5-E2c) — which is the case a present alone never covers,
/// since a party that loads a save and stands still gives the program
/// nothing to redraw.
void paint(machine& box, seam_context& ctx, std::uint16_t ds,
           const overland_look& look, bool after_a_present) {
  cpu::processor& cpu = box.processor();
  automap_state& state = box.automap();

  if (!state.at_command_bar()) {
    // Somebody other than the adventuring screen is asking the player
    // something — a script's menu, an encounter's prompt — and whatever
    // it has drawn in the viewport is not this seam's to paint over.
    return;
  }

  const automap_record* map = state.find_overland(look.disk, look.area);
  if (map == nullptr) {
    return;
  }

  std::uint32_t signature = mix(2166136261U, look.disk);
  signature = mix(signature, look.area);
  signature = mix(signature, look.x);
  signature = mix(signature, look.y);
  signature = mix(signature, state.serial());
  if (signature == 0) {
    // Zero is this seam's "nothing has been drawn", so it is not allowed
    // to be a real answer.
    signature = 1;
  }
  if (!after_a_present && signature == state.explored_signature()) {
    // The common case by a wide margin: the program is polling and
    // nothing about the party or the store has moved. Six bytes of the
    // data segment were read to decide it.
    return;
  }

  const int bias = cpu.read_byte(ds, at(data_view_column_bias, look.view_kind));
  if (bias > max_column_bias) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint32_t fogged = cells_to_fog(*map, bias, look.x, look.y);
  if (fogged == 0) {
    // The party has been near every cell on the screen, so there is
    // nothing to cover and **not a port is written**. On a five-by-five
    // window at a radius of one this is the state of a well-walked
    // stretch of country; at a radius of two it is nearly every frame,
    // which is the measurement that settled the radius (`automap.h`).
    state.set_explored_signature(signature);
    return;
  }

  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);
  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);

  for (int row = 0; row < window_cells; ++row) {
    for (int column = 0; column < window_cells; ++column) {
      if ((fogged &
           (1U << static_cast<unsigned>((row * window_cells) + column))) != 0) {
        fog_cell(box, static_cast<unsigned>(column),
                 static_cast<unsigned>(row));
      }
    }
  }

  // Handed back in the state the program's own drawing primitives leave,
  // which is the state this found them in (docs/seams.md §3).
  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);
  state.set_explored_signature(signature);
}

/// The program has just put the screen up. If it is the overworld, the
/// country the party has not been near goes back under fog.
void at_present_return(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // The cheapest byte first. This routine is the program's one present
  // and is reached from every screen it draws, so the common case by an
  // enormous margin is one read and a return.
  if (cpu.read_byte(ds, data_game_mode) != mode_overland) {
    return;
  }

  // The rest of the guard is the recorder's, and it is the same guard,
  // so it is asked rather than repeated: the view kind, the transition
  // byte, the area record's far pointer and its bounds, the word that
  // says these areas are being drawn in the interior view, and the
  // position's range and settling.
  const overland_look look = observe_overland(box, ctx, ds);
  if (!look.on_screen || !look.settled) {
    return;
  }
  paint(box, ctx, ds, look, true);
}

/// The program is asking whether a key is waiting, which is where it is
/// between commands. Two things happen here and this seam claims no key:
/// the party's square is recorded, and the fog is laid down when
/// something has changed that no repaint of the program's would have
/// shown — a saved slot's table read in under a party that is standing
/// still, most of all.
void at_key_pending(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const overland_look look = observe_overland(box, ctx, ds);
  if (!look.on_screen || !look.settled) {
    // Not this seam's screen. What was drawn last is gone or is about to
    // be, so the next pass that *is* on the overworld draws again.
    box.automap().set_explored_signature(0);
    return;
  }
  paint(box, ctx, ds, look, false);
}

/// The program is putting a command bar up, and which bar it is says
/// whose screen this is. The same point, the same reading and the same
/// note as the automap's (M5-E2d): either seam alone keeps it current,
/// and both together write it twice with the same answer.
void at_command_bar(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  note_command_bar(box, ds);
}

// ---------------------------------------------------------------------------
// The definition
// ---------------------------------------------------------------------------

constexpr std::array<seam_point, 3> explored_points{
    {{.module = resident_image,
      .offset = present_return,
      .run = &at_present_return},
     {.module = resident_image,
      .offset = key_pending_entry,
      .run = &at_key_pending},
     {.module = resident_image,
      .offset = command_bar_entry,
      .run = &at_command_bar}}};

constexpr seam_definition explored_definition{
    .id = "explored",
    .about =
        "fog of war on the overworld map: the country the party has "
        "walked is the game's own, and the rest is covered",
    .fingerprints = explored_binaries,
    .points = explored_points,
    .trigger = false,
    .gate = document_kind::none,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& explored_seam() noexcept { return explored_definition; }

}  // namespace amberfolio::machine
