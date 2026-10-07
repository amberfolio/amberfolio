// SPDX-License-Identifier: AGPL-3.0-only
//
// selection_margin.h has the argument; this is the bookkeeping.

#include "amberfolio/machine/selection_margin.h"

#include <cstddef>
#include <cstdint>

namespace amberfolio::machine {
namespace {

/// The leftmost pixel of a scanline: column 0 of a cell.
constexpr std::uint8_t column_zero = 0x80;
/// Every pixel of a scanline: row 7 of a cell under a block's top.
constexpr std::uint8_t whole_line = 0xFF;
/// The cell's bottom scanline, where the block below draws its top.
constexpr unsigned bottom_line = selection_margins::lines - 1;

[[nodiscard]] bool holds(const selection_margins::strip& s) noexcept {
  for (const std::uint8_t bits : s.bits) {
    if (bits != 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool selection_margins::change::empty() const noexcept {
  return !holds(erase) && !holds(paint);
}

bool selection_margins::inside(unsigned page, unsigned row,
                               unsigned column) noexcept {
  return page < pages && row < rows && column < columns;
}

const selection_margins::cell& selection_margins::at(
    unsigned page, unsigned row, unsigned column) const noexcept {
  return cells_[page][(std::size_t{row} * columns) + column];
}

selection_margins::cell& selection_margins::at(unsigned page, unsigned row,
                                               unsigned column) noexcept {
  return cells_[page][(std::size_t{row} * columns) + column];
}

namespace {

[[nodiscard]] bool live(std::uint8_t block,
                        const selection_margins::strip& owned) noexcept {
  return block != 0 || holds(owned);
}

}  // namespace

void selection_margins::drawn(unsigned page, unsigned row, unsigned column,
                              std::uint8_t colour) noexcept {
  if (!inside(page, row, column)) {
    return;
  }
  cell& c = at(page, row, column);
  const bool was = live(c.block, c.owned);
  c.block = colour;
  c.owned = {};
  const bool is = live(c.block, c.owned);
  if (was != is) {
    live_[page] = is ? live_[page] + 1 : live_[page] - 1;
  }
}

void selection_margins::cleared(unsigned page, unsigned top, unsigned left,
                                unsigned bottom, unsigned right) noexcept {
  for (unsigned row = top; row <= bottom && row < rows; ++row) {
    for (unsigned column = left; column <= right && column < columns;
         ++column) {
      drawn(page, row, column, 0);
    }
  }
}

std::uint8_t selection_margins::block(unsigned page, unsigned row,
                                      unsigned column) const noexcept {
  return inside(page, row, column) ? at(page, row, column).block : 0;
}

selection_margins::strip selection_margins::wanted(
    unsigned page, unsigned row, unsigned column) const noexcept {
  strip out;
  if (!inside(page, row, column) || block(page, row, column) != 0) {
    // A block's own column 0 and row 7 are its colour already.
    return out;
  }
  const std::uint8_t below = block(page, row + 1, column);
  const std::uint8_t left = column == 0 ? 0 : block(page, row, column - 1);
  const std::uint8_t below_left =
      column == 0 ? 0 : block(page, row + 1, column - 1);

  // Column 0, beside a block that ends on the left: its right margin.
  if (left != 0) {
    for (unsigned line = 0; line < bottom_line; ++line) {
      out.bits[line] = column_zero;
      out.colour[line] = left;
    }
  }
  // Row 7: the top of the block below, end to end; or the corner of a
  // block's right margin, from the block on the left or below it.
  if (below != 0) {
    out.bits[bottom_line] = whole_line;
    out.colour[bottom_line] = below;
  } else if (left != 0 || below_left != 0) {
    out.bits[bottom_line] = column_zero;
    out.colour[bottom_line] = left != 0 ? left : below_left;
  }
  return out;
}

const selection_margins::strip& selection_margins::owned(
    unsigned page, unsigned row, unsigned column) const noexcept {
  static const strip none{};
  return inside(page, row, column) ? at(page, row, column).owned : none;
}

selection_margins::change selection_margins::needed(
    unsigned page, unsigned row, unsigned column) const noexcept {
  change out;
  if (!inside(page, row, column)) {
    return out;
  }
  const strip want = wanted(page, row, column);
  const strip& have = owned(page, row, column);
  for (unsigned line = 0; line < lines; ++line) {
    // A pixel this machine holds in a colour no longer wanted goes back to
    // black first, and is painted again in the colour that is.
    const std::uint8_t kept =
        want.colour[line] == have.colour[line]
            ? static_cast<std::uint8_t>(have.bits[line] & want.bits[line])
            : std::uint8_t{0};
    out.erase.bits[line] = static_cast<std::uint8_t>(have.bits[line] & ~kept);
    out.erase.colour[line] = have.colour[line];
    out.paint.bits[line] = static_cast<std::uint8_t>(want.bits[line] & ~kept);
    out.paint.colour[line] = want.colour[line];
  }
  return out;
}

void selection_margins::done(unsigned page, unsigned row, unsigned column,
                             const strip& erased,
                             const strip& painted) noexcept {
  if (!inside(page, row, column)) {
    return;
  }
  cell& c = at(page, row, column);
  const bool was = live(c.block, c.owned);
  for (unsigned line = 0; line < lines; ++line) {
    c.owned.bits[line] =
        static_cast<std::uint8_t>(c.owned.bits[line] & ~erased.bits[line]);
    if (painted.bits[line] != 0) {
      c.owned.bits[line] |= painted.bits[line];
      c.owned.colour[line] = painted.colour[line];
    }
    if (c.owned.bits[line] == 0) {
      c.owned.colour[line] = 0;
    }
  }
  const bool is = live(c.block, c.owned);
  if (was != is) {
    live_[page] = is ? live_[page] + 1 : live_[page] - 1;
  }
}

void selection_margins::forget_block(unsigned page, unsigned row,
                                     unsigned column) noexcept {
  if (!inside(page, row, column)) {
    return;
  }
  cell& c = at(page, row, column);
  const bool was = live(c.block, c.owned);
  c.block = 0;
  const bool is = live(c.block, c.owned);
  if (was != is) {
    live_[page] = is ? live_[page] + 1 : live_[page] - 1;
  }
}

bool selection_margins::any(unsigned page) const noexcept {
  return page < pages && live_[page] != 0;
}

void selection_margins::clear() noexcept {
  cells_ = {};
  live_ = {};
}

}  // namespace amberfolio::machine
