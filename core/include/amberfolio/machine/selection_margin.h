// SPDX-License-Identifier: AGPL-3.0-only
//
// The margin of a selection block, where it lies outside the block's own
// cells (#483).
//
//
// What the margin is
// ------------------
//
// With `modern-controls` on a selection is drawn inverted: a block of its
// colour with the letters cut out (seam_select_yellow.cpp). Every face lays
// a letter in columns 1 to 7 and rows 0 to 6 of its cell (text_face.h), so
// the block keeps a margin on its left and below inside its own cells, and
// its letters touch its top and its right. The margin there is drawn **in the
// neighbouring cells**: row 7 of the cell above each block cell, and column 0
// of the cell right of a block's last cell, with the corner above it. Those
// are paper in every text glyph but a descender or two, and the margin is
// painted **only over black**, so it never covers what the program drew.
//
// A cell the program draws repaints all of its own pixels, and knows nothing
// of a margin lying in it. So the margin is kept, cell by cell: which cells
// are blocks, and which margin pixels this machine painted. Each time the
// program draws a cell or clears a rectangle, the seam asks what the cells
// around it want now and paints or takes back only the difference. A pixel
// is taken back only if it is this machine's: one the program has drawn over
// since is not. The seam checks what it holds on the screen before it
// settles a cell, because the program draws some things (the frame's border
// row) through neither the blitter nor the fill.
//
//
// Not machine state
// -----------------
//
// On automap.h's three terms: `machine::reset()` drops it, the state
// serialization never sees it, and a replay rebuilds it by drawing the same
// cells again. The seam is its one writer.

#pragma once

#include <array>
#include <cstdint>

namespace amberfolio::machine {

class selection_margins {
 public:
  /// The program's two display pages, and its text grid on each.
  static constexpr unsigned pages = 2;
  static constexpr unsigned rows = 25;
  static constexpr unsigned columns = 40;
  static constexpr unsigned lines = 8;

  /// Pixels of one cell, a mask a scanline (bit 0x80 leftmost), and the
  /// colour each scanline's pixels are in.
  struct strip {
    std::array<std::uint8_t, lines> bits{};
    std::array<std::uint8_t, lines> colour{};
  };

  /// What one cell needs: pixels of this machine's to put back to black,
  /// and pixels to paint, once the erasing is done.
  struct change {
    strip erase;
    strip paint;
    [[nodiscard]] bool empty() const noexcept;
  };

  /// The program drew the cell at `row`, `column` of `page`: a block in
  /// `colour`, or no block when `colour` is zero. Every pixel of the cell is
  /// the program's again.
  void drawn(unsigned page, unsigned row, unsigned column,
             std::uint8_t colour) noexcept;

  /// The program filled the rectangle, every cell of it, on `page`.
  void cleared(unsigned page, unsigned top, unsigned left, unsigned bottom,
               unsigned right) noexcept;

  /// The block colour the cell holds, or zero.
  [[nodiscard]] std::uint8_t block(unsigned page, unsigned row,
                                   unsigned column) const noexcept;

  /// The margin pixels the cell wants, from the blocks around it.
  [[nodiscard]] strip wanted(unsigned page, unsigned row,
                             unsigned column) const noexcept;

  /// The margin pixels in the cell that are this machine's.
  [[nodiscard]] const strip& owned(unsigned page, unsigned row,
                                   unsigned column) const noexcept;

  /// What the cell needs to go from what it holds to what it wants.
  [[nodiscard]] change needed(unsigned page, unsigned row,
                              unsigned column) const noexcept;

  /// The seam did it: `erased` taken back, and of the pixels it was asked to
  /// paint, `painted` were black and are this machine's now.
  void done(unsigned page, unsigned row, unsigned column, const strip& erased,
            const strip& painted) noexcept;

  /// A block the program has since covered some other way: forget it.
  void forget_block(unsigned page, unsigned row, unsigned column) noexcept;

  /// Whether any cell of `page` is a block or holds a margin pixel.
  [[nodiscard]] bool any(unsigned page) const noexcept;

  void clear() noexcept;

 private:
  struct cell {
    std::uint8_t block = 0;
    strip owned;
  };
  [[nodiscard]] static bool inside(unsigned page, unsigned row,
                                   unsigned column) noexcept;
  [[nodiscard]] const cell& at(unsigned page, unsigned row,
                               unsigned column) const noexcept;
  [[nodiscard]] cell& at(unsigned page, unsigned row, unsigned column) noexcept;

  std::array<std::array<cell, std::size_t{rows} * columns>, pages> cells_{};
  std::array<unsigned, pages> live_{};
};

}  // namespace amberfolio::machine
