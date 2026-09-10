// SPDX-License-Identifier: AGPL-3.0-only
//
// The toggle panel's columns and its pixels. seam_panel.h says why they
// are here and not in main.cpp.

#include "seam_panel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/font.h"
#include "amberfolio/machine/seam.h"
#include "glyph_atlas.h"

namespace amberfolio::sdl {
namespace {

/// The columns, as offsets into a line and widths in characters.
///
/// Fixed rather than sized to the content, and that is the point of the
/// panel: `fired` is a number a reader has to be able to see a zero in
/// among the others (#131), and a column that moved with the longest id
/// would put every run's zeros somewhere else.
///
/// Each width is the longest thing core can put in it plus a space —
/// `cheat-invulnerable` is eighteen characters, `unavailable` eleven,
/// `document_not_presented` twenty-two, `no document` eleven.
constexpr std::size_t column_mark = 0;
constexpr std::size_t column_id = 4;
constexpr std::size_t column_state = 23;
constexpr std::size_t column_fired = 35;
constexpr std::size_t width_fired = 6;
constexpr std::size_t column_reason = 43;
constexpr std::size_t column_gate = 66;
constexpr std::size_t panel_columns = 78;

/// What a panel says about itself. A person who has just opened it has
/// two questions — how do I work it, and does it stick — and the answer
/// to both is shorter than a row.
constexpr std::string_view panel_title =
    "seams - up/down, Return toggles and remembers, right button closes";

/// `text` written into `line` at `at`, cut to `width` characters so a
/// long value cannot walk into the column beside it.
void place(std::string& line, std::size_t at, std::string_view text,
           std::size_t width) {
  const std::size_t take = std::min(text.size(), width);
  for (std::size_t i = 0; i < take; ++i) {
    line[at + i] = text[i];
  }
}

/// A line of the panel's width, all spaces.
///
/// Built by `resize` rather than returned as `{panel_columns, ' '}`: a
/// braced list of two constants picks `std::string`'s
/// `initializer_list<char>` constructor and would make a two-character
/// string out of a width and a space.
[[nodiscard]] std::string blank_line() {
  std::string line;
  line.resize(panel_columns, ' ');
  return line;
}

/// `text` broken into lines of at most `panel_columns` characters, on
/// spaces.
///
/// A word longer than the panel is wide — which is what a 64-character
/// SHA-256 is not, deliberately: the panel is 78 columns and a hash is
/// 64, so a wrap on spaces always leaves one whole — is put on a line of
/// its own and left to `place()` to cut. Nothing here hyphenates,
/// because the words being wrapped are a filename's worth of English and
/// a digest, and a broken digest is a wrong digest.
[[nodiscard]] std::vector<std::string> wrapped(std::string_view text) {
  std::vector<std::string> out;
  std::string line;
  while (!text.empty()) {
    const std::size_t space = text.find(' ');
    const std::string_view word = text.substr(0, space);
    if (!line.empty() && line.size() + 1 + word.size() > panel_columns) {
      out.push_back(line);
      line.clear();
    }
    if (!line.empty()) {
      line += ' ';
    }
    line += word;
    if (space == std::string_view::npos) {
      break;
    }
    text.remove_prefix(space + 1);
  }
  if (!line.empty()) {
    out.push_back(line);
  }
  return out;
}

/// The colours, the on-screen keyboard's (`screen_keyboard_view.cpp`) so
/// that this host's two overlays look like one host's furniture.
constexpr SDL_Color panel_colour{.r = 20, .g = 16, .b = 12, .a = 232};
constexpr SDL_Color edge_colour{.r = 90, .g = 74, .b = 48, .a = 255};
constexpr SDL_Color text_colour{.r = 232, .g = 217, .b = 189, .a = 255};
constexpr SDL_Color amber{.r = 224, .g = 163, .b = 60, .a = 255};
constexpr SDL_Color focus_colour{.r = 42, .g = 33, .b = 21, .a = 255};

void set_colour(SDL_Renderer* renderer, SDL_Color colour) {
  SDL_SetRenderDrawColor(renderer, colour.r, colour.g, colour.b, colour.a);
}

/// How much of the window the panel may take.
constexpr float width_share = 0.94F;
constexpr float height_share = 0.9F;

}  // namespace

panel_row panel_row_of(const machine::seam_status& row,
                       machine::document_kind gate) {
  panel_row out;
  out.id = std::string(row.id);
  out.about = std::string(row.about);
  out.state = machine::seam_state_name(row.state);
  if (row.state == machine::seam_state::on) {
    // The two claims a seam that is on can make, and they are different
    // things: `armed` says an address was computed out of the fact
    // table, `inert` says the module it lives in is not resident yet.
    out.state += row.armed ? " armed" : " inert";
  }
  out.fired = row.fired;
  out.reason = row.reason == machine::seam_reason::none
                   ? "-"
                   : machine::seam_reason_name(row.reason);
  out.gate = gate == machine::document_kind::none
                 ? "-"
                 : machine::document_kind_name(gate);
  // Core's sentence about what the numbers on this row mean (#163),
  // without the " - " it is handed over with: the panel supplies its own
  // separator.
  std::string_view reading =
      machine::seam_reading_text(machine::seam_reading_of(row));
  if (reading.starts_with(" - ")) {
    reading.remove_prefix(3);
  }
  out.reading = std::string(reading);
  out.on = row.state == machine::seam_state::on;
  out.available = row.state != machine::seam_state::unavailable;
  return out;
}

std::vector<panel_row> panel_rows(const machine::seam_engine& seams) {
  std::vector<panel_row> rows;
  rows.reserve(seams.count());
  for (std::size_t i = 0; i < seams.count(); ++i) {
    const machine::seam_status row = seams.status(i);
    const machine::seam_definition* definition = seams.find(row.id);
    rows.push_back(panel_row_of(row, definition == nullptr
                                         ? machine::document_kind::none
                                         : definition->gate));
  }
  return rows;
}

std::vector<std::string> panel_lines(const std::vector<panel_row>& rows,
                                     std::size_t focus,
                                     const std::vector<std::string>& notice) {
  std::vector<std::string> lines;
  lines.reserve(rows.size() + notice.size() + 5);

  std::string title = blank_line();
  place(title, 0, panel_title, panel_columns);
  lines.push_back(title);

  std::string header = blank_line();
  place(header, column_id, "seam", panel_columns - column_id);
  place(header, column_state, "state", panel_columns - column_state);
  // Right-aligned over the numbers under it, like they are.
  place(header, column_fired + width_fired - 5, "fired", 5);
  place(header, column_reason, "reason", panel_columns - column_reason);
  place(header, column_gate, "waits for", panel_columns - column_gate);
  lines.push_back(header);

  for (const panel_row& row : rows) {
    std::string line = blank_line();
    // A box that cannot be ticked is drawn as one that cannot: a seam
    // that is not for this program is not a choice a player has.
    place(line, column_mark,
          !row.available ? "  - " : (row.on ? "[x] " : "[ ] "), 4);
    place(line, column_id, row.id, column_state - column_id - 1);
    place(line, column_state, row.state, column_fired - column_state - 1);
    const std::string fired = std::to_string(row.fired);
    place(line,
          fired.size() >= width_fired
              ? column_fired
              : column_fired + width_fired - fired.size(),
          fired, width_fired);
    place(line, column_reason, row.reason, column_gate - column_reason - 1);
    place(line, column_gate, row.gate, panel_columns - column_gate);
    lines.push_back(line);
  }
  if (rows.empty()) {
    std::string line = blank_line();
    place(line, column_id, "this build carries no seams", panel_columns);
    lines.push_back(line);
  }

  // And under the table, the two sentences that are too long to be
  // columns: what the focused seam is for, and what its numbers mean.
  std::string about = blank_line();
  std::string reading = blank_line();
  if (focus < rows.size()) {
    place(about, column_mark, rows[focus].id + " - " + rows[focus].about,
          panel_columns);
    place(reading, column_id, rows[focus].reading, panel_columns - column_id);
  } else {
    place(about, column_mark, "no row picked - click one, or press up or down",
          panel_columns);
  }
  lines.push_back(about);
  lines.push_back(reading);

  // And last, what the document control had to say (#384) — the outcome
  // of the file a player dropped on this window, which is the one thing
  // the panel says that is not about a row. Under the table, so the row
  // arithmetic above it is untouched.
  for (const std::string& said : notice) {
    for (const std::string& piece : wrapped(said)) {
      std::string line = blank_line();
      place(line, column_mark, piece, panel_columns);
      lines.push_back(line);
    }
  }
  return lines;
}

panel_box fit_panel(const std::vector<std::string>& lines, int window_width,
                    int window_height) noexcept {
  panel_box box{};
  if (lines.empty()) {
    return box;
  }
  std::size_t columns = 1;
  for (const std::string& line : lines) {
    columns = std::max(columns, line.size());
  }
  const auto glyph = static_cast<float>(machine::font::glyph_height);
  // Two cells of margin each way, so the text is not against the edge of
  // its own panel.
  const float across = (static_cast<float>(window_width) * width_share) /
                       (glyph * static_cast<float>(columns + 2));
  const float down = (static_cast<float>(window_height) * height_share) /
                     (glyph * static_cast<float>(lines.size() + 2));
  box.scale = std::max(1.0F, std::min(across, down));
  box.width = box.scale * glyph * static_cast<float>(columns);
  box.height = box.scale * glyph * static_cast<float>(lines.size());
  box.left = (static_cast<float>(window_width) - box.width) / 2.0F;
  box.top = (static_cast<float>(window_height) - box.height) / 2.0F;
  return box;
}

std::size_t row_under(const panel_box& box, std::size_t rows, float x,
                      float y) noexcept {
  if (box.scale <= 0.0F || x < box.left || y < box.top ||
      x >= box.left + box.width || y >= box.top + box.height) {
    return panel_no_row;
  }
  const float line_height =
      box.scale * static_cast<float>(machine::font::glyph_height);
  // Non-negative, which the bounds above are what says.
  const auto line = static_cast<std::size_t>((y - box.top) / line_height);
  if (line < panel_first_row || line - panel_first_row >= rows) {
    return panel_no_row;
  }
  return line - panel_first_row;
}

void panel_painter::draw(SDL_Renderer* renderer,
                         const std::vector<std::string>& lines,
                         std::size_t focus) {
  if (renderer == nullptr || lines.empty()) {
    return;
  }
  int window_width = 0;
  int window_height = 0;
  if (!SDL_GetRenderOutputSize(renderer, &window_width, &window_height)) {
    return;
  }
  const panel_box box = fit_panel(lines, window_width, window_height);
  if (box.scale <= 0.0F) {
    return;
  }
  const float line_height =
      box.scale * static_cast<float>(machine::font::glyph_height);

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  const SDL_FRect panel{.x = box.left - line_height,
                        .y = box.top - line_height,
                        .w = box.width + (2.0F * line_height),
                        .h = box.height + (2.0F * line_height)};
  set_colour(renderer, panel_colour);
  SDL_RenderFillRect(renderer, &panel);
  set_colour(renderer, edge_colour);
  SDL_RenderRect(renderer, &panel);

  if (focus != panel_no_row && focus + panel_first_row < lines.size()) {
    const SDL_FRect picked{
        .x = box.left,
        .y = box.top +
             (line_height * static_cast<float>(focus + panel_first_row)),
        .w = box.width,
        .h = line_height};
    set_colour(renderer, focus_colour);
    SDL_RenderFillRect(renderer, &picked);
  }

  for (std::size_t i = 0; i < lines.size(); ++i) {
    // The title and the column header in amber, the rows in the
    // machine's own paper white: a panel with one accent reads as a
    // table, and one with none reads as a wall.
    const SDL_Color colour = i < panel_first_row ? amber : text_colour;
    atlas_.draw(renderer, lines[i], box.left,
                box.top + (line_height * static_cast<float>(i)), box.scale,
                colour);
  }
}

}  // namespace amberfolio::sdl
