// SPDX-License-Identifier: AGPL-3.0-only
//
// The text route: a journal edition whose words are text, read out of the
// document itself with no OCR engine anywhere (#398).
//
// Every other edition this build knows is a scan: a page is a picture of
// words, and an engine reads the picture (`journal_ocr.h`). Some editions
// were typeset again instead, and their pages carry the words as
// character codes in a content stream. Reading one of those through an
// engine would mean rendering a page this build has no renderer for, in
// order to guess, badly, at text that is sitting right there — so this
// reads the text.
//
//
// Still an extractor, and not a PDF reader
// ----------------------------------------
//
// `journal_facts.h`'s argument holds unchanged. Nothing here finds an
// object, reads a cross-reference table or walks a page tree: the table
// names each page's content stream and each font's `/ToUnicode` map by
// offset, length and decoded size, exactly as it names an image, and a
// row that is wrong inflates to the wrong size and says so. What is new is
// one step past the inflate: the content stream is **interpreted**, and
// only as far as text needs —
//
//   * the graphics state's matrix (`q`, `Q`, `cm`), because a run's
//     position is on the page and not in its text object;
//   * the text operators (`BT`, `ET`, `Tm`, `Td`, `TD`, `T*`, `TL`, `Tf`,
//     `Ts`, `Tj`, `TJ`, `'`, `"`);
//   * everything else is an operator whose operands are dropped. Colour,
//     paths, images and marked content change no word, and an inline
//     image's bytes are stepped over whole.
//
// **A run is where it starts.** Nothing here reads a glyph's width, so
// the position this route knows is a run's origin — the point its first
// glyph sits on — and that is the position a fact table's box is about. A
// run shown straight after another in the same text object, with no
// positioning between them, is placed where that line began; ordering is
// by position and then by stream order, so it still lands after the run
// it followed.
//
//
// From runs to the text the store keeps
// -------------------------------------
//
// The text a store holds is what an engine would hand back if it were
// perfect (`journal_ocr.h`'s whitespace table), and this route writes it
// that way:
//
//   1. **Runs in the box.** A run belongs to a fragment when its origin is
//      inside the fragment's box (`journal_text_box`). A run inside a box,
//      in a font the page's facts do not name or with a code the font's
//      map does not, is `text_unreadable`: never a guessed letter.
//   2. **Lines.** Runs whose baselines are within half a point are one
//      line; a line reads left to right.
//   3. **The discretionary hyphen.** A typesetter that breaks a word
//      across lines draws the hyphen it added as a run of its own at the
//      end of the line, apart from the word it split. That run is
//      dropped and the word joined. A hyphen *inside* a run is the
//      writer's and is kept — `self-` and `taught` join as
//      `self-taught`, which is what was printed.
//   4. **Paragraphs are indents.** A line opens a paragraph when it starts
//      more than `journal_text_indent` points right of its box's left
//      edge, which is the column's margin. The first line of an item
//      always opens one. A fragment boundary is a continuation unless the
//      line after it is indented — measured, not tabled, which is the
//      thing `journal_fragment::begins_paragraph` had to be for a scan.
//   5. **Joining.** Within a paragraph a line joins the one before it with
//      a space, unless that line already ended in one, or in a hyphen, or
//      in a dropped discretionary hyphen. Runs of spaces become one; a
//      paragraph is trimmed; paragraphs are separated by a blank line.
//      **One line per paragraph**, then: the reader's reflow (#316) is a
//      guess made for an engine's lines, and there is nothing here for it
//      to guess about.
//   6. **Look-alikes are folded.** A font may map a glyph drawn as a Latin
//      letter to the Cyrillic letter it shares a shape with, and the one
//      edition this was written for does, five times. Such a code point is
//      the Latin letter it was printed as; the reader draws sixty-four
//      glyphs and would otherwise draw a substitute where the page has a
//      `T`.
//
// And then the digest (`journal_entry_fact::text_sha256`): an item whose
// text is a byte different from the one the table was measured against is
// `text_mismatch`, and nothing is stored for it.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"

namespace amberfolio::host {

/// How far right of its box's left edge a line has to start, in points,
/// to open a paragraph.
///
/// The one edition measured indents its paragraphs by eight and a half
/// points and sets every other line within a point of the margin, so any
/// threshold from two to seven reads it the same way; three is the middle
/// of what is safe on both sides.
inline constexpr double journal_text_indent = 3.0;

/// What one font's one-byte codes read as: each code's UTF-8, and whether
/// the font's map said anything about it at all.
struct journal_font_map {
  std::array<std::string, 256> text{};
  std::array<bool, 256> mapped{};
};

/// A `/ToUnicode` CMap's `bfchar` and `bfrange` entries, into `out`.
///
/// Only codes below 256 are kept, because only a simple font's are read.
/// Destinations are UTF-16BE and come out as UTF-8, with look-alikes
/// folded (step 6 above). `text_unreadable` for a CMap that is not one.
[[nodiscard]] journal_trouble read_to_unicode(
    std::span<const std::uint8_t> cmap, journal_font_map& out);

/// One run of text as a content stream shows it: where it starts, which
/// font resource it is set in, and its codes, still undecoded.
struct journal_text_run {
  double x{};
  double y{};
  std::string font{};
  std::vector<std::uint8_t> codes{};
};

/// Every run `content` shows, in stream order.
///
/// `text_unreadable` for a stream this cannot tokenize at all — a string
/// that never closes, say. An operator this does not interpret is not a
/// failure: it changes no word.
[[nodiscard]] journal_trouble read_text_runs(
    std::span<const std::uint8_t> content, std::vector<journal_text_run>& out);

/// The text of `fragments` of `edition`, by steps 1 to 6 above, and
/// **without** the digest check. What the measuring of a new edition
/// wants, and what `read_item_text()` checks.
[[nodiscard]] journal_trouble read_text_fragments(
    std::span<const std::uint8_t> document, const journal_edition& edition,
    std::span<const journal_text_fragment> fragments, std::string& out);

/// One item's text, checked against its row's digest.
///
/// `no_such_entry` for a row with no text fragments; `text_mismatch`,
/// with `out` cleared, for text the digest does not name.
[[nodiscard]] journal_trouble read_item_text(
    std::span<const std::uint8_t> document, const journal_edition& edition,
    const journal_entry_fact& fact, std::string& out);

/// What a store's `engine` line says about text read this way. Not an
/// engine and not a version: the text came out of the player's own
/// document, and that is the whole of what a later reader of the store
/// needs to know about how.
inline constexpr std::string_view journal_text_engine = "document text";

}  // namespace amberfolio::host
