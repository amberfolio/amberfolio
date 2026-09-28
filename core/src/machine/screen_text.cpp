// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/screen_text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_map.h"

namespace amberfolio::machine {

namespace {

// ---------------------------------------------------------------------------
// The facts
// ---------------------------------------------------------------------------

/// Where one binary keeps its font: the data segment as an offset in the
/// loaded image, and the far pointer to the glyphs as an offset in that
/// segment. The same two facts the seams read the font through
/// (seams.md §10, the zone label), stated for this read's own binaries.
struct font_location {
  std::string_view fingerprint;
  std::uint32_t dgroup_offset;
  std::uint16_t font_pointer;
};

/// The baseline edition (edition.h), and only it.
constexpr std::array<font_location, 1> locations{{
    {.fingerprint =
         "d825df2b174675c9088ba1489488bdeebe66ad2a22943f17d3a198e60b6a07bd",
     .dgroup_offset = 0xC7C0,
     .font_pointer = 0x5E20},
}};

// ---------------------------------------------------------------------------
// The matcher
// ---------------------------------------------------------------------------

using glyph = std::array<std::uint8_t, text_cell_pixels>;

/// The character a program glyph is: its index is the character
/// upper-cased modulo sixty-four, so 0-31 are `@` to `_` and 32-63 are
/// themselves.
[[nodiscard]] constexpr std::uint8_t program_code(unsigned index) noexcept {
  return static_cast<std::uint8_t>(index < 32 ? index + 0x40 : index);
}

[[nodiscard]] bool same(std::span<const std::uint8_t> table, std::size_t at,
                        const glyph& bits) noexcept {
  for (std::size_t row = 0; row < bits.size(); ++row) {
    if (table[at + row] != bits[row]) {
      return false;
    }
  }
  return true;
}

/// Every character `bits` is in the program's font. A second character
/// that answers makes the answer ambiguous.
struct candidates {
  std::uint8_t code{cell_not_text};
  bool ambiguous{false};

  void add(std::uint8_t found) noexcept {
    if (code == cell_not_text) {
      code = found;
    } else if (code != found) {
      ambiguous = true;
    }
  }
};

[[nodiscard]] candidates lookup(std::span<const std::uint8_t> font,
                                const glyph& bits) noexcept {
  candidates found;
  for (unsigned index = 0; index < program_font_glyphs; ++index) {
    const std::size_t at = std::size_t{index} * text_cell_pixels;
    if (at + text_cell_pixels <= font.size() && same(font, at, bits)) {
      found.add(program_code(index));
    }
  }
  return found;
}

[[nodiscard]] text_cell read_cell(std::span<const std::uint8_t> pixels,
                                  std::span<const std::uint8_t> program,
                                  unsigned column, unsigned row) noexcept {
  const std::size_t x0 = std::size_t{column} * text_cell_pixels;
  const std::size_t y0 = std::size_t{row} * text_cell_pixels;

  // The two colours, and the bitmap of the first.
  std::uint8_t first = pixels[(y0 * frame_width) + x0];
  std::uint8_t second = first;
  bool two = false;
  glyph bits{};
  for (std::size_t y = 0; y < text_cell_pixels; ++y) {
    for (std::size_t x = 0; x < text_cell_pixels; ++x) {
      const std::uint8_t pixel = pixels[((y0 + y) * frame_width) + x0 + x];
      if (pixel == first) {
        bits[y] = static_cast<std::uint8_t>(bits[y] | (0x80U >> x));
      } else if (!two) {
        second = pixel;
        two = true;
      } else if (pixel != second) {
        return {};
      }
    }
  }
  if (!two) {
    return {.code = ' ', .ink = first, .paper = first};
  }

  // Either colour may be the ink.
  glyph inverse{};
  for (std::size_t y = 0; y < text_cell_pixels; ++y) {
    inverse[y] = static_cast<std::uint8_t>(~bits[y]);
  }
  const candidates as_first = lookup(program, bits);
  const candidates as_second = lookup(program, inverse);
  if (as_first.ambiguous || as_second.ambiguous ||
      (as_first.code != cell_not_text && as_second.code != cell_not_text)) {
    return {.code = cell_ambiguous, .ink = first, .paper = second};
  }
  if (as_first.code != cell_not_text) {
    return {.code = as_first.code, .ink = first, .paper = second};
  }
  if (as_second.code != cell_not_text) {
    return {.code = as_second.code, .ink = second, .paper = first};
  }
  return {};
}

}  // namespace

const char* screen_text_trouble_name(screen_text_trouble trouble) noexcept {
  switch (trouble) {
    case screen_text_trouble::none:
      return "none";
    case screen_text_trouble::no_program:
      return "no_program";
    case screen_text_trouble::wrong_binary:
      return "wrong_binary";
    case screen_text_trouble::no_font:
      return "no_font";
  }
  return "unknown";
}

void read_text_cells(std::span<const std::uint8_t> pixels,
                     std::span<const std::uint8_t> program_font,
                     text_grid& out) noexcept {
  out = {};
  if (pixels.size() < frame_pixels) {
    return;
  }
  for (unsigned row = 0; row < text_rows; ++row) {
    for (unsigned column = 0; column < text_columns; ++column) {
      out[(std::size_t{row} * text_columns) + column] =
          read_cell(pixels, program_font, column, row);
    }
  }
}

screen_text_trouble read_screen_text(const machine& box,
                                     text_grid& out) noexcept {
  out = {};
  const seam_engine& seams = box.seams();
  if (!seams.have_program()) {
    return screen_text_trouble::no_program;
  }
  const font_location* where = nullptr;
  for (const font_location& known : locations) {
    if (digest_is(seams.program(), known.fingerprint)) {
      where = &known;
    }
  }
  if (where == nullptr) {
    return screen_text_trouble::wrong_binary;
  }

  // The far pointer, and the glyphs it names, straight out of RAM: a read
  // of the machine's storage and not a bus cycle, so nothing the program
  // can see moves. A pointer the program has not set up — zero, or one
  // that would run off conventional memory — is its own "no font yet".
  const std::span<const std::uint8_t> ram = box.memory().ram();
  const std::uint32_t pointer_at =
      seams.image_base() + where->dgroup_offset + where->font_pointer;
  if (pointer_at + 4 > ram.size()) {
    return screen_text_trouble::no_font;
  }
  const auto word = [&ram](std::uint32_t at) {
    return static_cast<std::uint16_t>(ram[at] | (ram[at + 1] << 8U));
  };
  const std::uint16_t offset = word(pointer_at);
  const std::uint16_t segment = word(pointer_at + 2);
  const std::uint32_t glyphs = (std::uint32_t{segment} * 16U) + offset;
  if (segment == 0 || offset + program_font_bytes > 0x10000U ||
      glyphs + program_font_bytes > conventional_ram_size) {
    return screen_text_trouble::no_font;
  }

  read_text_cells(box.display().pixels(),
                  ram.subspan(glyphs, program_font_bytes), out);
  return screen_text_trouble::none;
}

}  // namespace amberfolio::machine
