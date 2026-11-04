// SPDX-License-Identifier: AGPL-3.0-only
//
// The explored overlay: PLAN.md §5 item 5, M5-E5 (#179), the seam itself
// is M5-E5c (#255) and the marking it draws now is M5-E5f (#263).
//
// Where the party has walked, on the game's own overworld screen. The
// program shows a five-by-five window of a wilderness area's overhead
// map, scrolling with the party; this seam **hazes every cell of that
// window the party has not been near over with fog**, so what is plainly
// on the screen is the country the party has seen and the rest of it is
// under a covering.
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
// **Its points are addresses**, four of them, all resident, and two of
// the four are the automap's (#173) — which is nothing new; several
// seams in this tree share those. This seam's own are the **return of the
// program's back-buffer present** and the **window painter's per-square
// blit**. It draws at two of them and the paragraph after next says why
// two; the blit only says the program has put its map back.
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
// adventuring screen's own; the position not having settled; a script
// holding the window with a picture or a figure of its own, or the
// program not having painted its map back since one did (#535, #545);
// and the
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
// The marking: fog, and why it is a black checker
// -----------------------------------------------
//
// A cell the party has not been near is **hazed over with a one-pixel
// checkerboard of palette index 0** — black — laid over half of its 24 by
// 24 pixels. The other half are the pixels the program drew, untouched,
// so the terrain is still faintly there under the haze rather than gone.
//
// **This is the fourth design and the third look**, and the third is the
// first one taken while walking rather than at a still. M5-E5c shipped a
// lift and the maintainer said it did not read; M5-E5f replaced it with a
// *solid black* cover, and then, off five coverings composited over a
// real dumped frame — solid black, a one-pixel black checker, a one-pixel
// dark-grey checker, a one-pixel light-grey checker and a two-by-two
// dark-grey checker — the maintainer picked the **dark-grey checker**.
// M5-E5g (#299) is that checker walked in the game, and the answer was
// that grey is not visible enough: the covering keeps the geometry the
// composite chose it for and takes the colour the composite argued away.
// Sixteen markings have now been prototyped or shipped over real frames
// of this screen; `docs/explored-overlay.md` §5 has all of them with
// their reasons, the grey included, because a rejected candidate with its
// cost is what makes each of these changes an edit rather than a
// re-investigation.
//
// What the composite said about black, and what walking it answered:
//
//   * the composite's finding was that **a one-pixel checker of black
//     collapses** — the terrain here is itself a two-green dither at
//     one-pixel granularity, so a black checker over it was judged to
//     interfere with that dither into a flat dark mesh that reads as one
//     more kind of ground. Walked, it does not: at a step's cadence the
//     covered half is plainly a covering, and the difference between a
//     fogged cell and the cell the party is standing on is the thing the
//     eye lands on — which is what a still with no motion and no
//     known-cell beside it could not show;
//   * **light grey reads as paler terrain**, for the same reason a lift
//     did — a covering whose value is near the tile's own is a variation
//     on the tile. That one stands;
//   * **dark grey read as haze on the still and reads as thin in the
//     game.** It is the colour of mountain rock, so a third of the
//     covering wrote grey onto grey and did nothing there (#267 item 2),
//     and it is near enough the terrain's own values everywhere else to
//     be a shade rather than a cover — which is the failure the lift had,
//     one remove away.
//
// **What black had going for it all along:** it is the one colour that
// cannot read as terrain at all, it is the game's own vocabulary for the
// unknown — the message rows, the panel beside the window and the 3D
// view's own distance are all black — it is the same on every terrain,
// and as a *checker* it costs the one read a veil costs and no more. What
// the composite rejected black for was the *solid* cover's fault and not
// the colour's: a solid cover throws away the shape of the country the
// party is standing at the edge of, and a player who cannot see a coast
// through the fog cannot see there is a coast to walk to. A black checker
// keeps the coast and is still unmistakably a cover.
//
// **The third reason survives intact.** The checker does not depend on
// what is underneath: it is index 0 on the same half of the pixels
// whatever the tile is, so there is still one fog to learn and it is
// still not the failure the lift's *dim* direction had on water.
//
// **The fourth reason is what this now pays.** A checker keeps the
// pixels it is not covering, and keeping them means loading the
// adapter's latches from the screen first: one read and one write per
// byte instead of one write, 72 reads and 72 writes a cell, 1,728 of
// each for a window with 24 cells fogged, against the automap panel's
// 9,856 writes. `fog_cell` below is where that is spelt out. A seam that
// draws now disturbs the latches; a seam that is only *looking* at a
// screen still does not, because nothing here reads a pixel unless it is
// about to cover one.
//
//
// How far the party sees
// ----------------------
//
// `explored_reveal_radius` (`automap.h`) — a Chebyshev distance, and
// **zero** since M5-E5g (#299). A cell is shown when the party has stood
// within that many cells of it, so at zero a cell is shown when the party
// has stood *on* it, and the trail a walk leaves is one cell wide.
//
// **The record still holds only the cells the party stood on.** The
// reveal is the *dilation* of that by the radius, worked out here when
// the window is drawn, so a player who turns the knob up tomorrow sees
// more of the map they already walked rather than a map that has to be
// walked again. At zero the dilation is the identity and `revealed()`
// below is one lookup; the loop is still written for a radius, because
// the knob is the point and nothing in the stored layout turns on it.
//
// **Why zero, with two and three refused above it and one tried below
// it.** The window is five cells across and the party is its middle cell
// in open country, so every cell on the screen is already within two of
// the party: at a radius of two this seam covers nothing at all except
// where a map's own edge pushes the party off centre — measured, 523
// driven frames, byte for byte the seam-off run. That refused two and
// three. One was then shipped and walked: it uncovers the three-by-three
// around every cell stood on, so a straight walk hands the player a
// corridor three cells wide and two rows of country nobody went near, and
// the map fills faster than it is explored. Zero reveals what was walked
// and nothing else, which is what the record holds and what the
// enhancement is named for (`docs/explored-overlay.md` §5.3).
//
//
// Two things this never covers
// ----------------------------
//
// **The party's own cell**, which is where the program draws the party's
// icon. It is revealed anyway at every radius — the party is standing on
// it, so the record has it — but the check is written down as its own
// line rather than left to arithmetic, because a fog over the party's own
// sprite is the one mistake here that would be a bug and not a
// preference. At radius zero it is also the *only* cell that is certainly
// clear, which makes that line load-bearing in a way it was not before: a
// record that came back empty would otherwise fog the whole window, the
// party's own square included.
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
//   * The bar test is the automap's — a far pointer into the data segment
//     at one of two known offsets. If the travel view hands its input
//     routine a third string, this seam paints nothing there and a driven
//     run is what will say so. #256 closed with it unticked and #267 is
//     the open issue it hangs off now.
//   * **The radius is zero and the maintainer set it while playing**
//     (#299), which is the strongest look any number in this enhancement
//     has had: one was confirmed off a still in #263 and reversed off a
//     walk. Two and three remain arithmetically empty on a five-wide
//     window.
//   * **The colour is black and was chosen the same way** (#299), against
//     a composite that had argued it away. What is still owed is the fog
//     looked at on the two wilderness areas nobody has stood on (#267
//     item 1); the fog over rough ground was #267 item 2, and the colour
//     change is its answer rather than a look
//     (`docs/explored-overlay.md` §8).

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
#include "amberfolio/machine/memory_map.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/program_profile.h"
#include "amberfolio/machine/seam.h"
#include "automap_overland.h"
#include "seam_builtin.h"

namespace amberfolio::machine {
namespace {

// ---------------------------------------------------------------------------
// The facts (docs/explored-overlay.md §2)
// ---------------------------------------------------------------------------

/// **The points**, as offsets from the image segment, all four resident.
///
/// The first is this seam's own: the far return of the routine that
/// presents the composed back buffer — the `mov sp, bp / pop bp / retf`
/// that is the only far return in it. Its second route is the
/// disassembly, which opens `push bp / mov bp, sp` and immediately reads
/// the six data-segment words the theory said it must: the adapter byte,
/// the back buffer's far pointer, the per-row dirty flags, the per-row
/// minimum and maximum x, and the per-row destination x.
///
/// The other two are the profile's, and the automap's points too: the
/// program's "is a key waiting" routine, which is where the program is
/// between commands and therefore where the trail is recorded, and the
/// thunk every menu bar in the game goes up through, which is how any
/// seam here knows whose screen it is on.
///
/// The fourth is this seam's own too: the far-jump thunk the 5x5 window
/// painter blits each square through (`docs/explored-overlay.md` §2).
/// Reaching it on the travel view is the program putting its map back in
/// the window, which nothing in the data segment records. The only other
/// caller is the combat view's, which is never the travel view. Second
/// route: a step's redraw reaches it 25 times, and a run that ends an
/// event with its picture still up does not reach it at all (#545).
///
/// Which view is up — 2, 3 and 4 are the three wilderness areas — is
/// the shared recorder's to check, and it hands the answer back in its
/// `overland_look` (`automap_overland.h`), so this file does not name
/// that offset a second time.
///
/// **The program's own word that the window is not the map.** A script
/// can draw over the travel view's window: the picture opcode (overlay 3,
/// `0x08B2`) and the message printer's picture branch (overlay 7,
/// `0x0636`) both set `0x84E0` to 1 *before* they draw a picture there,
/// and the paths that put the view back (the step's redraw, the opcode
/// that closes the picture, the conditional redraw) clear it. Beside it
/// are the script's two "open" bytes, cleared at the end of every event:
/// `0x84C8`, a figure the message printer drew in the view (its "SPRIT"
/// branch), and `0x84C9`, the picture opcode's own (`0x08AD`). Second
/// route: the disassembly of each writer, and a player's screen, where
/// the fog was laid over the picture an arrival event drew (#535).
struct explored_facts {
  /// The program these facts are about (program_profile.h).
  const program_profile& profile;
  std::uint32_t present_return;
  std::uint32_t window_square_blit;
  /// The widest bias a 44-column table could carry, which is what says a
  /// byte read here is the byte the facts describe.
  int max_column_bias;
  std::uint16_t data_picture_in_view;
  std::uint16_t data_figure_in_view;
  std::uint16_t data_picture_open;
};

/// Pool of Radiance, the baseline edition: the travel view's five-by-five
/// window of a wilderness area walked square by square.
constexpr explored_facts pool_of_radiance_explored{
    .profile = pool_of_radiance,
    .present_return = 0x649B,
    .window_square_blit = 0x3EBA,
    .max_column_bias = 0x2B,
    .data_picture_in_view = 0x84E0,
    .data_figure_in_view = 0x84C8,
    .data_picture_open = 0x84C9};

constexpr auto explored_binaries =
    fingerprints_of(std::array{&pool_of_radiance, &curse_of_the_azure_bonds});

/// The seam's one word of its own: the program has put something over the
/// window since it last painted its map there.
constexpr unsigned word_window_taken = 0;

/// The two halves of an overland record's identity — the disk the area's
/// files come from and the area's own id — are read by the shared
/// recorder and come back in its answer (`automap_overland.h`), so this
/// file does not name their offsets a second time.

/// **The fog's colour: palette index 0**, black — every plane clear. It
/// is laid as a one-pixel checkerboard, half of the square's pixels
/// covered and half of them left exactly as the program drew them, which
/// is the geometry the maintainer chose over a solid fill on a
/// side-by-side of five (M5-E5f, #263). The colour was index 8, the
/// program's own dark grey, until that checker was walked rather than
/// looked at: grey is a shade away from the terrain's own values and is
/// the very colour of mountain rock, and black is the game's own word for
/// the unknown (M5-E5g, #299; `docs/explored-overlay.md` §5.2).
constexpr std::uint8_t fog_colour = 0x00;
constexpr std::uint8_t all_planes = 0x0F;
constexpr std::uint8_t all_bits = 0xFF;
constexpr std::uint8_t no_planes = 0x00;

/// **The checker's two masks, and why the parity is the screen's.** A
/// covered pixel is one whose `x + y` is even in *screen* coordinates and
/// not in the square's own, so the pattern runs unbroken across the
/// boundary between two fogged squares instead of restarting at each of
/// them. A byte column begins at an x that is a multiple of eight, so the
/// parity of `x + y` inside a byte is the parity of the bit's position
/// plus the scanline's: on an even scanline the covered pixels are bits
/// 7, 5, 3 and 1, and on an odd one they are the other four.
constexpr std::uint8_t checker_even_line = 0xAA;
constexpr std::uint8_t checker_odd_line = 0x55;

/// The graphics-controller registers a plane-selected byte write needs,
/// and the values a plain write-mode-0 copy wants. Set rather than
/// assumed: they cannot be read back, and assuming a register you cannot
/// read is not a check (docs/seams.md §3).
constexpr std::uint8_t gc_set_reset_index = 0;
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
template <const program_profile& P>
[[nodiscard]] std::uint16_t data_segment(cpu::processor& cpu,
                                         const seam_context& ctx) noexcept {
  const auto wanted = static_cast<std::uint16_t>((ctx.image_base() / 16U) +
                                                 (P.dgroup_offset() / 16U));
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
/// every other pixel of them turned to the fog's colour and the rest left
/// exactly as the program drew them.
///
/// **This is a masked write, and a masked write reads first.** The
/// graphics controller's bit mask decides, bit by bit, which pixels of a
/// byte take what the CPU is writing; every bit it clears takes its pixel
/// from the adapter's *latches* instead. So the latches have to be
/// holding the byte that is already on the screen, and the only way to
/// load them is to read that byte through the bus — one read and one
/// write per byte, where the solid fill this replaced needed the write
/// alone. That read is the whole extra cost of a veil over a cover; it is
/// what `docs/explored-overlay.md` §5.2 counted against every checker
/// before the maintainer looked at them, and it is the reason this file
/// reads the video window at all.
///
/// **The colour comes from set/reset, not from the byte written.** With
/// enable-set/reset on all four planes each plane takes its bit from the
/// set/reset register, so one CPU write paints the fog's index across the
/// planes and the byte handed to it is immaterial. The bit mask is set once per
/// scanline, because the checker alternates with the scanline's parity.
///
/// The rect's byte alignment is what makes any of this three whole bytes
/// rather than a shift: a cell begins at x = 8 + 24j, which is byte
/// column 1 + 3j, and is three whole bytes across (`automap.h`).
void fog_cell(machine& box, unsigned column, unsigned row) {
  cpu::processor& cpu = box.processor();
  constexpr unsigned bytes_across = explored_cell_pixels / 8;
  const unsigned first_byte = (explored_window_x / 8) + (column * bytes_across);
  const unsigned first_line = explored_window_y + (row * explored_cell_pixels);
  for (unsigned line = 0; line < explored_cell_pixels; ++line) {
    const unsigned screen_line = first_line + line;
    write_register(
        box, ega::graphics_index_port, ega::graphics_data_port,
        gc_bit_mask_index,
        (screen_line % 2) == 0 ? checker_even_line : checker_odd_line);
    const auto base = static_cast<std::uint16_t>(
        (screen_line * plane_bytes_per_row) + first_byte);
    for (unsigned byte = 0; byte < bytes_across; ++byte) {
      const std::uint16_t offset = at(base, static_cast<std::uint16_t>(byte));
      // The read is the write's other half: it loads the latches with the
      // pixels this write is going to keep. What it hands back is of no
      // interest, and the latches load whatever the read mode is.
      static_cast<void>(cpu.read_byte(video_window_segment, offset));
      cpu.write_byte(video_window_segment, offset, all_bits);
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
/// revealed by the record as well, at any radius at all, and is written
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

/// Whether a script has drawn into the window and not given it back.
template <const explored_facts& F>
[[nodiscard]] bool script_holds_the_window(cpu::processor& cpu,
                                           std::uint16_t ds) {
  return cpu.read_byte(ds, F.data_picture_in_view) != 0 ||
         cpu.read_byte(ds, F.data_figure_in_view) != 0 ||
         cpu.read_byte(ds, F.data_picture_open) != 0;
}

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
template <const explored_facts& F>
void paint(machine& box, seam_context& ctx, std::uint16_t ds,
           const overland_look& look, bool after_a_present) {
  cpu::processor& cpu = box.processor();
  automap_state& state = box.automap();

  if (!state.at_command_bar()) {
    // Somebody other than the adventuring screen is asking the player
    // something — a script's menu, an encounter's prompt — and whatever
    // it has drawn in the viewport is not this seam's to paint over. Nor
    // is what this seam drew before still on the screen: the program may
    // put the window back before its own bar returns, with no present
    // after it (an encounter the party flees), so the next poll at that
    // bar has to draw rather than trust the signature.
    state.set_explored_signature(0);
    return;
  }

  if (script_holds_the_window<F>(cpu, ds)) {
    // A script has put a picture or a figure where the map was, and is
    // still at the party's own bar while it types its text: an arrival
    // event, whose first polls are where a step's position settles. It
    // stays the script's until the program paints its map back. Not
    // until the next present: the end of an event clears those bytes
    // without a redraw, and putting the party's bar back up presents the
    // screen with the picture still in the window (#545).
    ctx.set_scratch(word_window_taken, 1);
    state.set_explored_signature(0);
    return;
  }
  if (ctx.scratch(word_window_taken) != 0) {
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

  const int bias =
      cpu.read_byte(ds, at(F.profile.data_view_column_bias, look.view_kind));
  if (bias > F.max_column_bias) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  const std::uint32_t fogged = cells_to_fog(*map, bias, look.x, look.y);
  if (fogged == 0) {
    // The party has been near every cell on the screen, so there is
    // nothing to cover and **not a port is written**. At the radius this
    // ships with it takes a party that has stood on all twenty-five cells
    // of the window; at a radius of two it would be nearly every frame,
    // which is the measurement that refused two (`automap.h`).
    state.set_explored_signature(signature);
    return;
  }

  // Set/reset carries the colour, so one CPU write puts the fog's index
  // on every plane at once; the bit mask is the checker and `fog_cell` sets it per
  // scanline.
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_set_reset_index, fog_colour);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, all_planes);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
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
  // which is the state this found them in (docs/seams.md §3): no
  // set/reset substitution, every bit of a written byte the CPU's own,
  // and every plane enabled. The adapter's *latches* cannot be handed
  // back — loading them is what a read is — so they are left holding the
  // last byte of the window this read, which is exactly what the
  // program's own read-modify-write primitives leave in them.
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, no_planes);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_set_reset_index, no_planes);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);
  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);
  state.set_explored_signature(signature);
}

/// The program has just put the screen up. If it is the overworld, the
/// country the party has not been near goes back under fog.
template <const explored_facts& F>
void at_present_return(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  // The cheapest byte first. This routine is the program's one present
  // and is reached from every screen it draws, so the common case by an
  // enormous margin is one read and a return.
  if (cpu.read_byte(ds, F.profile.data_game_mode) != F.profile.mode_travel) {
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
  paint<F>(box, ctx, ds, look, true);
}

/// The program is painting a square of its map into the window, so
/// whatever a script put there is going.
template <const explored_facts& F>
void at_window_square(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (cpu.read_byte(ds, F.profile.data_game_mode) == F.profile.mode_travel) {
    ctx.set_scratch(word_window_taken, 0);
  }
}

/// The program is asking whether a key is waiting, which is where it is
/// between commands. Two things happen here and this seam claims no key:
/// the party's square is recorded, and the fog is laid down when
/// something has changed that no repaint of the program's would have
/// shown — a saved slot's table read in under a party that is standing
/// still, most of all.
template <const explored_facts& F>
void at_key_pending(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
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
  paint<F>(box, ctx, ds, look, false);
}

/// The program is putting a command bar up, and which bar it is says
/// whose screen this is. The same point, the same reading and the same
/// note as the automap's (M5-E2d): either seam alone keeps it current,
/// and both together write it twice with the same answer.
template <const explored_facts& F>
void at_command_bar(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  note_command_bar(box, ctx, ds);
}

// ---------------------------------------------------------------------------
// On Curse of the Azure Bonds: a picture with places on it (#555)
// ---------------------------------------------------------------------------
//
// Curse has no overland walked square by square. Its overland, game mode
// 3, is **one picture of the whole region**, a big picture block drawn at
// the wilderness frame's cell (1, 1), with towns and trails drawn on it;
// the party's mark blinks at the place it is at, and a journey is chosen
// from a list in the text area below. So the fog is laid over that
// picture, and what it leaves clear is what the party has seen
// (`docs/explored-overlay.md` §9, the maintainer's design):
//
//   * a **disc** around every place the party's mark has stood on;
//   * a **corridor** along the straight line between two places the party
//     went between one after the other, which is a trail taken, or the
//     way across country between two places a journey passed through;
//   * the **current place**, always.
//
// The covering is Pool of Radiance's: palette index 0 on the half of the
// pixels whose `x + y` is even on the screen. What is different is how it
// is laid, because what is different is the screen.
//
// **The program does not repaint this picture when the party moves.** A
// journey moves the mark from place to place and the picture stays as it
// was drawn, so a reveal cannot wait for the program to put the country
// back the way Pool of Radiance's step does. The seam puts it back
// itself, **from the program's own copy of the picture**: the loaded
// block is in conventional memory, four plane bytes to a byte column in
// the order a table in the data segment gives, and the program's own
// blitter copies it to the screen a byte at a time. Every paint writes the
// whole picture, each byte its plane bytes from that copy less the fog's
// pixels: four plane-selected writes a byte, no read of the video window
// at all, so the adapter's latches are not touched.
//
// **When**, and the five points:
//
//   * **the blitter's line loop** (image `0x343B`), drawing the overland's
//     picture on the overland: the scanline it has just finished gets its
//     fog at once. The blitter takes about six frames over the picture,
//     and fogging only at its return left the country it had drawn
//     uncovered for five of them;
//   * **the blitter's return** (image `0x3542`), with the picture block
//     the overland's and its far pointer the one it was handed at (1, 1):
//     the program has just drawn the picture, so it is clean, and on the
//     overland the whole fog is laid again, which changes nothing the
//     line loop laid;
//   * **the blitter's entry** (image `0x3347`): anything else drawn into
//     the picture's cells, a script's picture, a figure, a frame of
//     another screen, means the picture is no longer the program's map,
//     and nothing is painted until the program draws it again. The mark
//     and the screen it saved under it are let through, at the mark's own
//     cell, because the menu-bar routine blinks them there;
//   * **the overlay compositor's entry** (image `0x3548`), which is how a
//     masked picture or a sprite reaches the screen: the same, without a
//     cell to test;
//   * **the menu-bar routine's own test** (overlay 27 `+0x04AC`), the
//     compare of the game mode, the loaded picture and the last picture
//     loaded that decides whether the mark is put up at all. With the
//     program's own answer yes, the place is recorded, and the way from
//     the last one; the fog is painted if anything has moved since it
//     last was. The mark is down here (the routine ends by putting back
//     what it saved under it), so the program saves the screen under the
//     mark *after* the fog is laid, and what it saves at the current
//     place is the picture, clear.
//
// **The fidelity claim is Pool of Radiance's**: on, and the overland map
// never shown, a run is the run with no engine at all. The points read;
// what they write is the seam's own words and `machine::automap()`.
//
// **What it refuses.** The data segment not being where `image_base()`
// says; the program's adapter byte not being the EGA's (another adapter's
// picture is laid out differently); the area record's far pointer, the
// picture's or a block's not being inside conventional memory; a current
// place beyond the table the program keeps (a word, as the program reads
// it); and a picture that would reach outside the frame's interior.

/// The facts, one row: Curse is the only program this mechanism is for.
struct explored_picture_facts {
  /// The program these facts are about (program_profile.h).
  const program_profile& profile;

  /// The menu-bar routine's test of whether to put the mark up, in the
  /// menu overlay (the profile's): `CMP` of the game mode with 3, of the
  /// loaded big picture with the overland's and of the last picture
  /// loaded with the one that keeps the mark down, then the three far
  /// calls that place, draw and blink-restore it.
  std::uint32_t mark_test;

  /// The resident picture blitter: its entry, and the `mov sp, bp` its
  /// every path ends at, with its frame still up. Arguments, far pointer
  /// first: the block at `BP+6`, its row in eight-pixel cells at `BP+0Ah`
  /// and its column (a byte column) at `BP+0Ch`.
  std::uint32_t blit_entry;
  std::uint32_t blit_exit;

  /// The blitter's line loop, where it goes on to the next scanline: the
  /// one at `BP-4` is drawn, every byte column of it on every plane.
  std::uint32_t blit_next_line;
  std::uint16_t blit_line_below_bp;

  /// The overlay compositor's entry: the flush of a masked picture.
  std::uint32_t compositor_entry;

  /// The game mode on the program's start menu, where a save is loaded
  /// or a new party begun: the way between the last place and the next
  /// is not a way the party went.
  std::uint8_t mode_start_menu;

  /// The loaded big picture's block id, and the overland's.
  std::uint16_t data_picture_block;
  std::uint8_t overland_picture;
  /// The last small picture loaded, and the one that keeps the mark down.
  std::uint16_t data_last_picture;
  std::uint8_t picture_without_mark;
  /// The loaded big picture's far pointer (offset, then segment).
  std::uint16_t data_picture_pointer;

  /// The mark's block and the block it saves the screen under the mark
  /// into, both far pointers, and the mark's cell: column word, row word.
  std::uint16_t data_mark_block;
  std::uint16_t data_mark_saved;
  std::uint16_t data_mark_column;
  std::uint16_t data_mark_row;

  /// The program's adapter byte, and the EGA's value of it.
  std::uint16_t data_adapter;
  std::uint8_t adapter_ega;

  /// The four map-mask values the blitter writes a byte column's four
  /// plane bytes through, in order.
  std::uint16_t data_plane_order;

  /// The current place: a word in the area record (the profile's far
  /// pointer), read as the program reads it.
  std::uint16_t area_current_place;

  /// The place table, a byte per place: the column and the row of its
  /// eight-pixel cell on the screen, where the mark is drawn.
  std::uint16_t data_place_columns;
  std::uint16_t data_place_rows;
  std::uint8_t place_count;

  /// A picture block's header: its height in scanlines, its width in byte
  /// columns, and where its plane bytes begin.
  std::uint16_t block_height;
  std::uint16_t block_width;
  std::uint16_t block_data;

  /// Where the picture goes, in cells, and the frame interior it must fit:
  /// rows 1..15 and columns 1..38, inside the wilderness frame.
  std::uint16_t picture_row;
  std::uint16_t picture_column;
  std::uint16_t interior_last_row;
  std::uint16_t interior_last_column;
};

constexpr explored_picture_facts curse_explored{
    .profile = curse_of_the_azure_bonds,
    .mark_test = 0x04AC,
    .blit_entry = 0x3347,
    .blit_exit = 0x3542,
    .blit_next_line = 0x343B,
    .blit_line_below_bp = 4,
    .compositor_entry = 0x3548,
    .mode_start_menu = 0,
    .data_picture_block = 0x72AA,
    .overland_picture = 0x79,
    .data_last_picture = 0x72A4,
    .picture_without_mark = 0x50,
    .data_picture_pointer = 0x72A6,
    .data_mark_block = 0x8C90,
    .data_mark_saved = 0x65E4,
    .data_mark_column = 0x8C8C,
    .data_mark_row = 0x8C8E,
    .data_adapter = 0x5000,
    .adapter_ega = 1,
    .data_plane_order = 0x2590,
    .area_current_place = 0x342,
    .data_place_columns = 0x0A4A,
    .data_place_rows = 0x0A6A,
    .place_count = 33,
    .block_height = 0,
    .block_width = 2,
    .block_data = 0x17,
    .picture_row = 1,
    .picture_column = 1,
    .interior_last_row = 15,
    .interior_last_column = 38};

static_assert(curse_explored.place_count <= automap_max_places,
              "the program's places fit in a places record");

/// The seam's words on Curse: the picture on the screen is the program's
/// map as it drew it (1) or something has been drawn over it since (0);
/// and the last place recorded, plus one (0 for none).
constexpr unsigned word_picture_clean = 0;
constexpr unsigned word_last_place = 1;

/// Eight pixels to a cell, both ways, in the program's graphics mode.
constexpr unsigned cell_pixels = 8;

/// The widest picture the frame's interior holds, in byte columns.
constexpr std::size_t picture_max_columns = 38;

/// A far pointer read out of the data segment, and whether `reach` bytes
/// from it are in conventional memory without wrapping its segment.
struct far_block {
  std::uint16_t segment{};
  std::uint16_t offset{};
};

[[nodiscard]] far_block far_at(cpu::processor& cpu, std::uint16_t ds,
                               std::uint16_t where) {
  return {.segment = cpu.read_word(ds, at(where, 2)),
          .offset = cpu.read_word(ds, where)};
}

[[nodiscard]] bool readable(const far_block& block,
                            std::uint32_t reach) noexcept {
  if (block.segment == 0 && block.offset == 0) {
    return false;
  }
  return std::uint32_t{block.offset} + reach <= 0x10000U &&
         cpu::physical_address(block.segment, block.offset) + reach <=
             conventional_ram_size;
}

/// A point on the screen, in pixels.
struct screen_point {
  int x{};
  int y{};
};

/// The clear pixels of one scanline of the picture: what the party has
/// seen of it, a bit a pixel from the picture's left edge.
///
/// **One line at a time**, because that is how the program draws the
/// picture: its blitter writes it a scanline at a time, and the fog is
/// laid on each line as the blitter finishes it, so no frame shows a line
/// of the picture without it. A disc or a corridor meets a line in one
/// run of pixels, so a line is cheap however many places there are.
///
/// Distances are measured from a pixel's centre, in doubled coordinates
/// so that the centre is a whole number and the arithmetic stays in
/// integers.
class clear_line {
 public:
  clear_line(int left, int width, int y) noexcept
      : left_(left), width_(width), y_(y) {}

  /// The pixels within `radius` of `c`, a cell's centre.
  void disc(screen_point c, int radius) noexcept {
    const std::int64_t dy =
        (std::int64_t{y_} * 2) + 1 - (std::int64_t{c.y} * 2);
    const std::int64_t reach = std::int64_t{radius} * 2;
    if (dy * dy > reach * reach) {
      return;
    }
    for (int x = c.x - radius; x <= c.x + radius; ++x) {
      const std::int64_t dx =
          (std::int64_t{x} * 2) + 1 - (std::int64_t{c.x} * 2);
      if ((dx * dx) + (dy * dy) <= reach * reach) {
        set(x);
      }
    }
  }

  /// The pixels within `half_width` of the segment from `a` to `b`.
  void corridor(screen_point a, screen_point b, int half_width) noexcept {
    if (y_ < (a.y < b.y ? a.y : b.y) - half_width ||
        y_ > (a.y < b.y ? b.y : a.y) + half_width) {
      return;
    }
    const std::int64_t vx = (std::int64_t{b.x} - a.x) * 2;
    const std::int64_t vy = (std::int64_t{b.y} - a.y) * 2;
    const std::int64_t length = (vx * vx) + (vy * vy);
    const std::int64_t reach = std::int64_t{half_width} * 2;
    const std::int64_t wy =
        (std::int64_t{y_} * 2) + 1 - (std::int64_t{a.y} * 2);
    const int first = (a.x < b.x ? a.x : b.x) - half_width;
    const int last = (a.x < b.x ? b.x : a.x) + half_width;
    for (int x = first; x <= last; ++x) {
      const std::int64_t wx =
          (std::int64_t{x} * 2) + 1 - (std::int64_t{a.x} * 2);
      const std::int64_t along = (wx * vx) + (wy * vy);
      bool inside = false;
      if (along <= 0 || length == 0) {
        // Before the start: the distance to the start.
        inside = (wx * wx) + (wy * wy) <= reach * reach;
      } else if (along >= length) {
        // Past the end: the distance to the end.
        const std::int64_t ux = wx - vx;
        const std::int64_t uy = wy - vy;
        inside = (ux * ux) + (uy * uy) <= reach * reach;
      } else {
        // Beside the segment: the distance from its line, squared and
        // times its squared length, which keeps it in integers.
        const std::int64_t across = (wx * vy) - (wy * vx);
        inside = across * across <= reach * reach * length;
      }
      if (inside) {
        set(x);
      }
    }
  }

  [[nodiscard]] bool clear(int x) const noexcept {
    const int at_x = x - left_;
    if (at_x < 0 || at_x >= width_) {
      return false;
    }
    const auto bit = static_cast<std::size_t>(at_x);
    return ((bits_[bit / 8] >> (bit % 8)) & 1U) != 0;
  }

 private:
  void set(int x) noexcept {
    const int at_x = x - left_;
    if (at_x < 0 || at_x >= width_) {
      return;
    }
    const auto bit = static_cast<std::size_t>(at_x);
    bits_[bit / 8] =
        static_cast<std::uint8_t>(bits_[bit / 8] | (1U << (bit % 8)));
  }

  int left_;
  int width_;
  int y_;
  std::array<std::uint8_t, picture_max_columns> bits_{};
};

/// A place's cell's centre on the screen, from the program's own table.
template <const explored_picture_facts& F>
[[nodiscard]] screen_point place_centre(cpu::processor& cpu, std::uint16_t ds,
                                        unsigned place) {
  const int column = cpu.read_byte(
      ds, at(F.data_place_columns, static_cast<std::uint16_t>(place)));
  const int row = cpu.read_byte(
      ds, at(F.data_place_rows, static_cast<std::uint16_t>(place)));
  constexpr int half = static_cast<int>(cell_pixels / 2);
  return {.x = (column * static_cast<int>(cell_pixels)) + half,
          .y = (row * static_cast<int>(cell_pixels)) + half};
}

/// The current place, or `place_count` when the area record cannot be
/// read or holds a place beyond the table.
template <const explored_picture_facts& F>
[[nodiscard]] unsigned current_place(cpu::processor& cpu, std::uint16_t ds) {
  const far_block record = far_at(cpu, ds, F.profile.data_area_record);
  const std::uint32_t reach = F.area_current_place + 2U;
  if (!readable(record, reach)) {
    return F.place_count;
  }
  const std::uint16_t place =
      cpu.read_word(record.segment, at(record.offset, F.area_current_place));
  return place < F.place_count ? place : F.place_count;
}

/// Whether the program would put the mark up now: its own three compares.
template <const explored_picture_facts& F>
[[nodiscard]] bool overland_up(cpu::processor& cpu, std::uint16_t ds) {
  return cpu.read_byte(ds, F.profile.data_game_mode) == F.profile.mode_travel &&
         cpu.read_byte(ds, F.data_picture_block) == F.overland_picture &&
         cpu.read_byte(ds, F.data_last_picture) != F.picture_without_mark;
}

/// The loaded picture, as the program's blitter reads it: where its
/// plane bytes are, how many lines and byte columns, and which plane each
/// of a byte column's four bytes is for. `ok` false when a fact does not
/// hold, and then nothing is to be drawn from it.
struct loaded_picture {
  bool ok{false};
  far_block block{};
  unsigned lines{};
  unsigned columns{};
  std::array<unsigned, ega::plane_count> plane_of{};
};

template <const explored_picture_facts& F>
[[nodiscard]] loaded_picture read_picture(cpu::processor& cpu,
                                          std::uint16_t ds) {
  loaded_picture found{};
  if (cpu.read_byte(ds, F.data_adapter) != F.adapter_ega) {
    return found;
  }
  found.block = far_at(cpu, ds, F.data_picture_pointer);
  if (!readable(found.block, F.block_data)) {
    return found;
  }
  found.lines = cpu.read_word(found.block.segment,
                              at(found.block.offset, F.block_height));
  found.columns =
      cpu.read_word(found.block.segment, at(found.block.offset, F.block_width));
  const unsigned top = F.picture_row * cell_pixels;
  if (found.lines == 0 || found.columns == 0 ||
      top + found.lines > (F.interior_last_row + 1U) * cell_pixels ||
      F.picture_column + found.columns > F.interior_last_column + 1U) {
    return found;
  }
  const std::uint32_t data_bytes = std::uint32_t{found.lines} * found.columns *
                                   static_cast<std::uint32_t>(ega::plane_count);
  if (!readable(found.block, F.block_data + data_bytes)) {
    return found;
  }

  // The program's own table, which must name each plane once.
  unsigned planes_named = 0;
  for (unsigned k = 0; k < ega::plane_count; ++k) {
    const std::uint8_t mask = cpu.read_byte(
        ds, at(F.data_plane_order, static_cast<std::uint16_t>(k)));
    unsigned plane = ega::plane_count;
    for (unsigned p = 0; p < ega::plane_count; ++p) {
      if (mask == (1U << p)) {
        plane = p;
      }
    }
    if (plane == ega::plane_count || (planes_named & (1U << plane)) != 0) {
      return found;
    }
    planes_named |= 1U << plane;
    found.plane_of[k] = plane;
  }
  found.ok = true;
  return found;
}

/// Lay the fog on lines `first` to `last` of the picture (its own line
/// numbers), each byte written as the program's copy of the picture has
/// it less the fog's pixels.
template <const explored_picture_facts& F>
void paint_lines(machine& box, std::uint16_t ds, const loaded_picture& picture,
                 unsigned place, unsigned first, unsigned last) {
  cpu::processor& cpu = box.processor();
  const automap_record* record = box.automap().find_places(
      cpu.read_byte(ds, F.profile.data_disk_number), F.overland_picture);

  // What the party has seen is the same on every line: the places, and
  // the ways between them.
  std::array<screen_point, automap_max_places> centre{};
  std::array<bool, automap_max_places> stood{};
  for (unsigned a = 0; a < F.place_count; ++a) {
    centre[a] = place_centre<F>(cpu, ds, a);
    stood[a] =
        a == place || (record != nullptr && automap_state::seen(*record, a, a));
  }

  // A plain write-mode-0 byte to one plane at a time: no set/reset, the
  // whole byte the CPU's, no rotation. Set rather than assumed.
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_enable_set_reset_index, no_planes);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_set_reset_index, no_planes);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_data_rotate_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_write_mode_index, 0);
  write_register(box, ega::graphics_index_port, ega::graphics_data_port,
                 gc_bit_mask_index, all_bits);

  const unsigned top = F.picture_row * cell_pixels;
  const unsigned left = F.picture_column;
  const auto data_offset =
      static_cast<std::uint16_t>(picture.block.offset + F.block_data);
  for (unsigned line = first; line <= last && line < picture.lines; ++line) {
    const unsigned screen_line = top + line;
    clear_line seen(static_cast<int>(left * cell_pixels),
                    static_cast<int>(picture.columns * cell_pixels),
                    static_cast<int>(screen_line));
    for (unsigned a = 0; a < F.place_count; ++a) {
      if (stood[a]) {
        seen.disc(centre[a], explored_place_radius);
      }
      if (record == nullptr) {
        continue;
      }
      for (unsigned b = a + 1; b < F.place_count; ++b) {
        if (automap_state::seen(*record, a, b)) {
          seen.corridor(centre[a], centre[b], explored_trail_half_width);
        }
      }
    }

    const std::uint8_t checker =
        (screen_line % 2) == 0 ? checker_even_line : checker_odd_line;
    std::array<std::uint8_t, picture_max_columns> fog{};
    for (unsigned column = 0; column < picture.columns; ++column) {
      for (unsigned bit = 0; bit < cell_pixels; ++bit) {
        const auto pixel = static_cast<std::uint8_t>(0x80U >> bit);
        const auto x = static_cast<int>(((left + column) * cell_pixels) + bit);
        if ((checker & pixel) != 0 && !seen.clear(x)) {
          fog[column] = static_cast<std::uint8_t>(fog[column] | pixel);
        }
      }
    }

    for (unsigned k = 0; k < ega::plane_count; ++k) {
      write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                     sequencer_map_mask_index,
                     static_cast<std::uint8_t>(1U << picture.plane_of[k]));
      for (unsigned column = 0; column < picture.columns; ++column) {
        const std::uint32_t index =
            (((std::uint32_t{line} * picture.columns) + column) *
             ega::plane_count) +
            k;
        const std::uint8_t drawn =
            cpu.read_byte(picture.block.segment,
                          static_cast<std::uint16_t>(data_offset + index));
        // Index 0 is every plane clear, so the fog's pixels are the drawn
        // ones with their bits taken out.
        cpu.write_byte(video_window_segment,
                       static_cast<std::uint16_t>(
                           (screen_line * plane_bytes_per_row) + left + column),
                       static_cast<std::uint8_t>(drawn & ~fog[column]));
      }
    }
  }

  // Handed back as the program's own blitter leaves it, and as its line
  // loop expects to find it: every plane enabled, the sequencer's index on
  // the map mask.
  write_register(box, ega::sequencer_index_port, ega::sequencer_data_port,
                 sequencer_map_mask_index, all_planes);
}

/// Lay the fog if anything has moved since it was last laid.
template <const explored_picture_facts& F>
void paint_if_moved(machine& box, seam_context& ctx, std::uint16_t ds,
                    bool picture_just_drawn) {
  cpu::processor& cpu = box.processor();
  automap_state& state = box.automap();
  const unsigned place = current_place<F>(cpu, ds);
  if (place >= F.place_count) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  std::uint32_t signature = mix(2166136261U, place);
  signature = mix(signature, state.serial());
  if (signature == 0) {
    signature = 1;
  }
  if (!picture_just_drawn && signature == state.explored_signature()) {
    return;
  }
  const loaded_picture picture = read_picture<F>(cpu, ds);
  if (!picture.ok) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  paint_lines<F>(box, ds, picture, place, 0, picture.lines - 1);
  state.set_explored_signature(signature);
}

/// The menu-bar routine is deciding whether to put the mark up. With the
/// program's own answer yes, the place is recorded and the fog laid.
template <const explored_picture_facts& F>
void at_mark_test(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  if (cpu.read_byte(ds, F.profile.data_game_mode) == F.mode_start_menu) {
    // A save is loaded or a party begun from here: the next place the
    // overland shows is not one the party went to from the last.
    ctx.set_scratch(word_last_place, 0);
    return;
  }
  if (!overland_up<F>(cpu, ds)) {
    return;
  }
  const unsigned place = current_place<F>(cpu, ds);
  if (place >= F.place_count) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }

  automap_state& state = box.automap();
  const std::uint32_t was = state.serial();
  automap_record& record = state.record_for_places(
      cpu.read_byte(ds, F.profile.data_disk_number), F.overland_picture);
  (void)state.reveal(record, place, place);
  const unsigned last = ctx.scratch(word_last_place);
  if (last != 0 && last - 1 != place) {
    (void)state.reveal(record, last - 1, place);
  }
  ctx.set_scratch(word_last_place, static_cast<std::uint16_t>(place + 1));
  if (state.serial() != was) {
    (void)ctx.call_host(seam_host_service::automap_update, state.serial());
  }

  if (ctx.scratch(word_picture_clean) != 0) {
    paint_if_moved<F>(box, ctx, ds, false);
  }
}

/// The blitter is about to draw something. Anything but the picture
/// itself, the mark or what it saved under the mark, drawn into the
/// picture's cells, means the picture is no longer the program's map.
template <const explored_picture_facts& F>
void at_blit_entry(machine& box, seam_context& ctx) {
  if (ctx.scratch(word_picture_clean) == 0) {
    return;
  }
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t sp = regs[cpu::reg16::sp];
  // At the entry the far return is on top, then the block's far pointer,
  // its row and its column.
  const far_block block{.segment = cpu.read_word(ss, at(sp, 6)),
                        .offset = cpu.read_word(ss, at(sp, 4))};
  const std::uint16_t row = cpu.read_word(ss, at(sp, 8));
  const std::uint16_t column = cpu.read_word(ss, at(sp, 10));

  const auto is = [&](std::uint16_t pointer) {
    const far_block known = far_at(cpu, ds, pointer);
    return known.segment == block.segment && known.offset == block.offset;
  };
  if (is(F.data_picture_pointer)) {
    return;
  }
  if ((is(F.data_mark_block) || is(F.data_mark_saved)) &&
      row == cpu.read_word(ds, F.data_mark_row) &&
      column == cpu.read_word(ds, F.data_mark_column)) {
    return;
  }

  if (!readable(block, F.block_width + 2U)) {
    // Nothing the blitter can draw from; and nothing to tell where it
    // would go. Take the picture as covered.
    ctx.set_scratch(word_picture_clean, 0);
    return;
  }
  const unsigned lines =
      cpu.read_word(block.segment, at(block.offset, F.block_height));
  const unsigned columns =
      cpu.read_word(block.segment, at(block.offset, F.block_width));
  const unsigned first_line = unsigned{row} * cell_pixels;
  const unsigned last_line = first_line + lines;  // one past
  const unsigned last_column = unsigned{column} + columns;
  const unsigned interior_top = F.picture_row * cell_pixels;
  const unsigned interior_bottom = (F.interior_last_row + 1U) * cell_pixels;
  const bool apart = lines == 0 || columns == 0 || last_line <= interior_top ||
                     first_line >= interior_bottom ||
                     last_column <= F.picture_column ||
                     column > F.interior_last_column;
  if (!apart) {
    ctx.set_scratch(word_picture_clean, 0);
  }
}

/// The blitter has drawn. If it was the overland's picture, at its cell,
/// the picture is the program's map again, and on the overland the fog
/// goes on now.
template <const explored_picture_facts& F>
void at_blit_exit(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const far_block picture = far_at(cpu, ds, F.data_picture_pointer);
  if (cpu.read_word(ss, at(bp, 6)) != picture.offset ||
      cpu.read_word(ss, at(bp, 8)) != picture.segment ||
      cpu.read_word(ss, at(bp, 10)) != F.picture_row ||
      cpu.read_word(ss, at(bp, 12)) != F.picture_column ||
      cpu.read_byte(ds, F.data_picture_block) != F.overland_picture) {
    return;
  }
  ctx.set_scratch(word_picture_clean, 1);
  box.automap().set_explored_signature(0);
  if (overland_up<F>(cpu, ds)) {
    paint_if_moved<F>(box, ctx, ds, true);
  }
}

/// The blitter has drawn a scanline and is going on to the next. If it is
/// drawing the overland's picture on the overland, the line it has drawn
/// gets its fog now, so no frame shows the picture's country uncovered
/// while the rest of it is still coming in.
template <const explored_picture_facts& F>
void at_blit_line(machine& box, seam_context& ctx) {
  cpu::processor& cpu = box.processor();
  const std::uint16_t ds = data_segment<F.profile>(cpu, ctx);
  if (ds == 0) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  const cpu::registers& regs = cpu.regs();
  const std::uint16_t ss = regs[cpu::sreg::ss];
  const std::uint16_t bp = regs[cpu::reg16::bp];
  const far_block drawing = far_at(cpu, ds, F.data_picture_pointer);
  if (cpu.read_word(ss, at(bp, 6)) != drawing.offset ||
      cpu.read_word(ss, at(bp, 8)) != drawing.segment ||
      cpu.read_word(ss, at(bp, 10)) != F.picture_row ||
      cpu.read_word(ss, at(bp, 12)) != F.picture_column ||
      !overland_up<F>(cpu, ds)) {
    return;
  }
  const unsigned place = current_place<F>(cpu, ds);
  const loaded_picture picture = read_picture<F>(cpu, ds);
  const std::uint16_t screen_line =
      cpu.read_word(ss, static_cast<std::uint16_t>(bp - F.blit_line_below_bp));
  const unsigned top = F.picture_row * cell_pixels;
  if (place >= F.place_count || !picture.ok || screen_line < top ||
      screen_line >= top + picture.lines) {
    ctx.decline(seam_reason::point_not_recognized);
    return;
  }
  paint_lines<F>(box, ds, picture, place, screen_line - top, screen_line - top);
}

/// A masked picture or a sprite is going onto the screen.
template <const explored_picture_facts& F>
void at_compositor(machine& /*box*/, seam_context& ctx) {
  if (ctx.scratch(word_picture_clean) != 0) {
    ctx.set_scratch(word_picture_clean, 0);
  }
}

// ---------------------------------------------------------------------------
// The definition
// ---------------------------------------------------------------------------

constexpr std::array<seam_point, 4> pool_of_radiance_explored_points{
    {{.module = resident_image,
      .offset = pool_of_radiance_explored.present_return,
      .run = &at_present_return<pool_of_radiance_explored>},
     {.module = resident_image,
      .offset = pool_of_radiance_explored.profile.key_pending_entry,
      .run = &at_key_pending<pool_of_radiance_explored>},
     {.module = resident_image,
      .offset = pool_of_radiance_explored.profile.command_bar_entry,
      .run = &at_command_bar<pool_of_radiance_explored>},
     {.module = resident_image,
      .offset = pool_of_radiance_explored.window_square_blit,
      .run = &at_window_square<pool_of_radiance_explored>}}};

/// On Curse the mark's test is in the menu overlay (the profile's), and
/// the blitter and the compositor are resident.
constexpr std::array<seam_point, 5> curse_explored_points{
    {{.module = curse_explored.profile.menu_overlay,
      .offset = curse_explored.mark_test,
      .run = &at_mark_test<curse_explored>},
     {.module = resident_image,
      .offset = curse_explored.blit_entry,
      .run = &at_blit_entry<curse_explored>},
     {.module = resident_image,
      .offset = curse_explored.blit_exit,
      .run = &at_blit_exit<curse_explored>},
     {.module = resident_image,
      .offset = curse_explored.compositor_entry,
      .run = &at_compositor<curse_explored>},
     {.module = resident_image,
      .offset = curse_explored.blit_next_line,
      .run = &at_blit_line<curse_explored>}}};

/// The points on every program but the baseline, whose points are the
/// definition's own (seam.h, schema 9).
constexpr std::array<seam_program_points, 1> explored_programs{
    {{.fingerprint = curse_of_the_azure_bonds.fingerprint,
      .points = curse_explored_points}}};

constexpr seam_definition explored_definition{
    .id = "explored",
    .about =
        "fog of war on the overworld map: the country the party has "
        "walked is the game's own, and the rest is covered",
    .fingerprints = explored_binaries,
    .points = pool_of_radiance_explored_points,
    .programs = explored_programs,
    .trigger = false,
    .gate = document_kind::none,
    .schema = seam_schema_version};

}  // namespace

const seam_definition& explored_seam() noexcept { return explored_definition; }

}  // namespace amberfolio::machine
