// SPDX-License-Identifier: AGPL-3.0-only

#include "screen_text_dump.h"

#include <cstddef>
#include <fstream>
#include <initializer_list>
#include <string>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/screen_text.h"

namespace amberfolio::sdl {

namespace {

[[nodiscard]] char hex_digit(unsigned value) {
  return "0123456789ABCDEF"[value & 0x0FU];
}

}  // namespace

std::string screen_text_dump(const machine::machine& box) {
  machine::text_grid grid{};
  const machine::screen_text_trouble trouble =
      machine::read_screen_text(box, grid);
  std::string out = "screen-text ";
  out += machine::screen_text_trouble_name(trouble);
  out += '\n';
  if (trouble != machine::screen_text_trouble::none) {
    return out;
  }
  for (unsigned row = 0; row < machine::text_rows; ++row) {
    std::string text = "text  |";
    std::string ink = "ink   |";
    std::string paper = "paper |";
    for (unsigned column = 0; column < machine::text_columns; ++column) {
      const machine::text_cell& cell =
          grid[(std::size_t{row} * machine::text_columns) + column];
      text += cell.code == machine::cell_not_text ? '#'
              : cell.code == machine::cell_ambiguous
                  ? '%'
                  : static_cast<char>(cell.code);
      ink += cell.code == machine::cell_not_text ? '.' : hex_digit(cell.ink);
      paper +=
          cell.code == machine::cell_not_text ? '.' : hex_digit(cell.paper);
    }
    for (const std::string* line : {&text, &ink, &paper}) {
      out += *line;
      out += "|\n";
    }
  }
  return out;
}

void write_screen_text(const std::filesystem::path& where,
                       const machine::machine& box) {
  std::ofstream file(where, std::ios::binary);
  file << screen_text_dump(box);
}

}  // namespace amberfolio::sdl
