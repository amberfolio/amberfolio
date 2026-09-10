// SPDX-License-Identifier: AGPL-3.0-only
//
// The toggle panel, painted over the window (#383).
//
// M5 built every seam and no face for one: on this host they were
// reachable from `--seam` and `--seams` and nowhere else, which is a
// person reading source code to turn on an enhancement — the thing M6's
// exit criterion says a new player must not have to do.
//
//
// The five facts, and why each is on the row
// ------------------------------------------
//
// A row is **name, state, fired, reason, gate**, and the panel is the
// same five in the same order on both hosts (`page/toggle-panel.mjs`).
// None of them is decoration:
//
//   * **state** is `off`, `on armed`, `on inert` or `unavailable`. The
//     middle two are different claims about a seam that is on and the
//     difference is `seam_status::armed` — an address was computed out
//     of the seam's fact table, or the module it lives in is not
//     resident yet.
//   * **fired** is a **number** and never a tick (#131, #163). A seam
//     that armed and fired nothing reads exactly like one that worked;
//     the count is the only thing on the row that makes that visible.
//   * **reason** is `seam_reason_name()` — core's word, not this host's
//     paraphrase. A panel that showed `off` where core said
//     `document_not_presented` would be throwing away the part a player
//     can act on.
//   * **gate** is the document the row waits for
//     (`seam_definition::gate`), which was carried through the ABI and
//     rendered nowhere. `-` for a seam that waits on nothing, which is
//     every seam in this build since #290; #384's document control is
//     what lights this column.
//
// And under the rows, for whichever one the focus is on: what the seam is
// for, and `seam_reading_text()` — core's sentence about what the numbers
// on that row *mean*, so that a browser run and a desktop run say the
// same thing about the same seam (#163).
//
//
// Nothing here touches the machine
// --------------------------------
//
// This file builds rows out of a `seam_engine` and paints them. Turning
// a seam on is `main()`'s, through the same `enable()`/`disable()` a
// `--seam` flag takes, with the refusal reported: a panel that recorded a
// choice it had not made would be the one failure a fail-closed toggle
// surface cannot have.
//
// It is drawn after the frame has been presented and verified, like the
// on-screen keyboard and for the same reason: `--verify` is a claim about
// the machine's own pixels, and this host's furniture in the target would
// make it a claim about the furniture.
//
// Split from main.cpp for `screen_keyboard_view.h`'s reason — the column
// arithmetic is not checkable by eye, and a lambda inside a `main()` is
// arithmetic no test can reach. `tests/seam_panel_test.cpp` reaches it.

#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "glyph_atlas.h"

namespace amberfolio::sdl {

/// One seam's five facts, in the words core says them.
///
/// Strings rather than the `seam_status` itself because a row outlives
/// the engine call that made it by exactly one paint, and because a test
/// that has to build a `seam_engine` to check a column width is a test
/// nobody writes.
struct panel_row {
  std::string id;
  std::string about;
  /// `off`, `on armed`, `on inert`, `unavailable`.
  std::string state;
  std::uint64_t fired{};
  /// `seam_reason_name()`, or `-` for `seam_reason::none`.
  std::string reason;
  /// The document this seam waits for, or `-` for one that waits on
  /// nothing.
  std::string gate;
  /// `seam_reading_text()` without its leading separator, or empty.
  std::string reading;
  /// Whether the box is ticked, and whether it may be.
  bool on{false};
  bool available{false};
};

/// One status row, plus the gate off its definition, as a panel row.
[[nodiscard]] panel_row panel_row_of(const machine::seam_status& row,
                                     machine::document_kind gate);

/// Every seam this engine carries, in registration order.
[[nodiscard]] std::vector<panel_row> panel_rows(
    const machine::seam_engine& seams);

/// The panel as lines of text: a title, a column header, one line per
/// seam, the focused seam's description, and last whatever `notice` was
/// handed in.
///
/// Fixed columns, so that the numbers under `fired` line up and a reader
/// can see a zero among them at a glance. A `focus` past the end is no
/// focus: the description line then says what to do rather than what a
/// row is.
///
/// `notice` is the document control's outcome (`document_control.h`,
/// #384) — what a dropped file turned out to be, which is the one thing
/// this host has to say to a player that is not about a row. It goes
/// **under** the table so the rows keep the line numbers `row_under()`
/// computes from, and it is **wrapped on spaces** rather than cut to the
/// panel's width: an unrecognised document's whole SHA-256 is the only
/// thing that player can act on, and a hash with its last six characters
/// off the edge is worse than no hash at all.
[[nodiscard]] std::vector<std::string> panel_lines(
    const std::vector<panel_row>& rows, std::size_t focus,
    const std::vector<std::string>& notice = {});

/// Which line of `panel_lines()` the first seam sits on. The title and
/// the header are above it.
inline constexpr std::size_t panel_first_row = 2;

/// The answer `row_under()` gives for a point on no row.
inline constexpr std::size_t panel_no_row = static_cast<std::size_t>(-1);

/// Where the panel goes in a window: what one glyph pixel is worth, and
/// the rectangle the text fills.
struct panel_box {
  float scale{};  ///< Window pixels per glyph pixel; never less than one.
  float left{};
  float top{};
  float width{};
  float height{};
};

/// Fit `lines` into a window `window_width` by `window_height` pixels,
/// centred. A window too small for the text answers a `scale` of one
/// rather than a degenerate box: a panel a person has to squint at is
/// still better than a division by zero.
[[nodiscard]] panel_box fit_panel(const std::vector<std::string>& lines,
                                  int window_width, int window_height) noexcept;

/// Which seam a window point is on, or `panel_no_row` for a point on the
/// title, the header, the description or outside the panel altogether.
[[nodiscard]] std::size_t row_under(const panel_box& box, std::size_t rows,
                                    float x, float y) noexcept;

/// Paints the panel. Owns nothing but the atlas it draws letters with.
class panel_painter {
 public:
  /// Draw `lines` over whatever the renderer already holds, with the
  /// seam at `focus` picked out.
  void draw(SDL_Renderer* renderer, const std::vector<std::string>& lines,
            std::size_t focus);

 private:
  glyph_atlas atlas_;
};

}  // namespace amberfolio::sdl
