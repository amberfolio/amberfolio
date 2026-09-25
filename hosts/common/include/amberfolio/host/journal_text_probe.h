// SPDX-License-Identifier: AGPL-3.0-only
//
// The text probe: a document this project made whose words are text, so
// the text route (`journal_text.h`, #398) can be driven end to end
// without one it did not.
//
// `journal_probe.h` is the same arrangement for the scans, and its
// argument is this one's: no page or word of a real journal may enter
// this tree, and without a document of our own the route would have no
// check that runs anywhere but on a maintainer's desk. So this file
// writes one — a two-page PDF, deterministic to the byte, set in an
// encoding of its own invention with a `/ToUnicode` map that says so —
// and its fact table is what the generator measured while writing it.
//
// The words are invented, and so are the rules they are set by. What is
// **not** invented is the expectation: each item's text is written down
// here as the text a reader should get, its digest is taken from that,
// and the route has to produce it from the page. Two derivations of one
// intention, as the scanned probe has.
//
//
// What the two pages exercise
// --------------------------
//
// Page one's content stream is Flate (stored blocks, for
// `journal_probe_zlib_stored()`'s reason), page two's is unfiltered, so
// both decodings run. Between them the pages carry every step of
// `journal_text.h` a real edition could take:
//
//   * a heading and an item that **flows** into a second column, which
//     continues the paragraph because its first line is not indented, and
//     a paragraph after it that opens with an indent;
//   * a **discretionary hyphen** drawn as a run of its own, which is
//     dropped; a writer's hyphen inside a run, which is kept; a line that
//     ends in a space, and one that does not and so is joined with one;
//     two spaces in a row, which become one;
//   * positioning by `Tm`, `Td`, `TD`, `T*`, `TL` and `'`, and a column
//     placed by `cm` inside `q`/`Q`;
//   * a `TJ` array with a kern in it; literal strings with octal
//     escapes, an escaped parenthesis and a backslash-newline; a hex
//     string; a comment; marked content whose properties carry a string;
//     an inline image whose data holds parentheses;
//   * a CMap with `bfchar`, and `bfrange` in both of its forms; codes that
//     read as curly quotes; a Cyrillic letter that is **folded** to the
//     Latin one it is drawn as;
//   * outside every box, and so harmless there: a run in a font the facts
//     do not name, and one with a code the map does not — which is what
//     the refusals are tested against, by rows that point a box at them;
//   * a picture, on the first item, so a text edition's drawings go the
//     scanned route's way (`journal_picture.h`).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_facts.h"

namespace amberfolio::host {

/// How many items the text probe has: entry one and tale one.
inline constexpr std::size_t journal_text_probe_items = 2;

/// The text probe document's bytes, the same on every target.
[[nodiscard]] const std::vector<std::uint8_t>& journal_text_probe_pdf();

/// The text probe's edition. The second row of `journal_probe_table()`.
[[nodiscard]] const journal_edition& journal_text_probe_edition();

/// The text item `index` of it should read as — what its digest is of.
[[nodiscard]] std::string_view journal_text_probe_text(std::size_t index);

/// Boxes on the probe's first page around things a row must be refused
/// for (#398): a run in a font the facts do not name, and a run with a
/// code the font's map does not. Nothing in the probe's own table points
/// at either; a test builds a row that does.
inline constexpr journal_text_box journal_text_probe_unnamed_font{
    .left = 20, .bottom = 280, .right = 200, .top = 295};
inline constexpr journal_text_box journal_text_probe_unmapped_code{
    .left = 20, .bottom = 0, .right = 200, .top = 15};

}  // namespace amberfolio::host
