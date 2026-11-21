// SPDX-License-Identifier: AGPL-3.0-only
//
// One edition of the journal fact table, Secret of the Silver Blades'
// GOG journal (#633), in a unit of its own: with it, journal_facts.cpp
// would be over the content guard's 256 KiB cap (scripts/check-clean.sh).
// journal_facts.h has the reasoning and journal_facts.cpp the table that
// names this edition.

#include "journal_facts_secret_gog.h"

#include <array>
#include <cstdint>

#include "amberfolio/host/journal_facts.h"

namespace amberfolio::host::journal_rows {
namespace {

/// The Adventurer's Journal of **Secret of the Silver Blades** as the GOG
/// release ships it (#633): the printed booklet scanned, thirty-three
/// pages, every one `/DCTDecode`, RGB, eight bits a component. Thirty-one
/// are two-page spreads of 1650 by 1275 samples; the first and the last
/// are single pages. The entries are on nine of the spreads, pages 16 to
/// 24. It is the booklet the Steam release typesets as text, entry for
/// entry: seventy-two entries, and nineteen pictures on the same nineteen
/// entries.
///
/// **Measured, never transcribed**: offsets, lengths, an image's shape
/// and rectangles of it. No pixel and no word of the document is here.
///
/// **A heading is a piece of its own, and so is its title.** Every
/// heading is a line of red display type after a printed checkbox, which
/// is an ornament and not a word of the entry. Under it, sixty-eight of
/// the seventy-two have a title in red capitals of two sizes, at the
/// margin the checkbox is at, and then the body, in blue. A rectangle
/// cannot leave the checkbox out of a column, so an entry's first piece is
/// its heading's line alone, from the middle of the gap right of the
/// checkbox to the end of the heading's ink; the title is the next piece
/// and the body the one after, each opening a paragraph. Seventy-two
/// entries are two hundred and twenty pieces: seventy-two headings,
/// sixty-eight titles and eighty pieces of body. Twelve entries are a
/// heading, a title and a map, and have no body.
///
/// How the rectangles were found:
///
/// - **Headings by their ink.** A line band whose ink is nearly all red
///   and a heading's height, opening with the checkbox, is a heading; the
///   red bands under it at the body's size are its title. Every heading
///   read back out of its rectangle came back with the number the chain
///   gives it.
/// - **Four column bands to a spread**, cut in the gutter between a
///   printed page's two columns, between samples 420 and 429 on the left
///   page and 1235 and 1247 on the right, and either side of the spine's
///   shadow at 818 and 846. A band runs from under the rule and ornament
///   printed across the head of every page to above its folio.
/// - **A piece hugs its own ink**, with four samples around it and never
///   past the middle of the gap to its neighbour.
/// - **The order is not the numbering** in two places. Entry 33 opens at
///   the foot of the first column of page 19 and is carried on in the
///   second, under entry 32's map, which is set at that column's head.
///   Entry 37 opens beside its picture at the foot of the third column,
///   under entry 35's map, and is carried on under entry 36's map at the
///   head of the fourth. The chain in reading order is 1 to 31, 33, 32,
///   34, 35, 37, 36 and 38 to 72.
/// - **Matter outside the grid ends a run**: the section's title above
///   entry 1, the folios, and a rule across the page under which a map
///   spans both of a printed page's columns, on both printed pages of
///   page 21 and the right-hand one of page 24. Above each rule the entry
///   before is carried on at the top of both columns: entry 44 above
///   entry 45's heading, entry 48 above entry 49's, and the end of entry
///   69 and entries 70 and 71 above entry 72's.
/// - **Seven drawings are set in their entry's column** with its prose
///   beside them, on entries 4, 15, 22, 25, 37, 55 and 63, and a body is
///   cut where it comes level with a drawing, eight samples above it or
///   less, and where it leaves it. Entry 37's heading, title and prose are
///   set in a frame of their own right of its picture.
///
/// **The boundaries** (#361). Besides the body's first piece, twenty
/// pieces carry a body on, in the next column, on the next page, or
/// beside or under a drawing. This edition sets its paragraphs apart
/// rather than indenting them, so which boundary is a paragraph break is
/// read as Secret's Steam edition reads it: a line is broken where the
/// next word does not fit, so a piece carries a paragraph on unless the
/// word it opens with, and a space of seven samples, would have fitted on
/// the line before it. The measure is that column's widest line on that
/// page, 333 to 343 samples, or a frame's own widest line. At three
/// boundaries the word fits with 30 to 105 samples to spare, in entries
/// 44, 50 and 53; at the other seventeen it is 2 to 61 samples too wide.
/// All twenty agree with the paragraphs of the Steam edition's text.
///
/// **The pictures** are the Steam edition's nineteen, each the bounding
/// box of its ink: twelve maps, with their keys and labels inside the
/// rectangle, and seven drawings. Two of the maps are not rectangles, and
/// a heading is set in the notch of each: entry 38's rectangle holds its
/// own heading and title and entry 40's, and entry 45's its own and entry
/// 47's. None of those eight text rectangles holds a sample of the map's
/// ink. No other text rectangle overlaps a picture, and no two overlap
/// each other.

/// A scan of the GOG edition: the byte offset of its stream's first data
/// byte, and its `/Length`. Indexed by page, from page 16.
struct secret_gog_scan {
  std::uint64_t at{};
  std::uint32_t bytes{};
};
constexpr std::array<secret_gog_scan, 9> secret_gog_scans{{
    {.at = 6560969, .bytes = 328895},  // page 16
    {.at = 6890394, .bytes = 500206},  // page 17
    {.at = 7391130, .bytes = 476203},  // page 18
    {.at = 7867863, .bytes = 375035},  // page 19
    {.at = 8243428, .bytes = 328429},  // page 20
    {.at = 8572389, .bytes = 388648},  // page 21
    {.at = 8961576, .bytes = 416260},  // page 22
    {.at = 9378375, .bytes = 424689},  // page 23
    {.at = 9803603, .bytes = 296959},  // page 24
}};

/// Every one of those scans is one shape: a two-page spread, carried to
/// the engine rather than decoded (#212).
constexpr journal_image secret_gog_spread{
    .width = 1650,
    .height = 1275,
    .bits_per_component = 8,
    .components = 3,
    .predictor = 1,
    .filter = journal_filter::dct,
    .inverted = false,
};

/// One rectangle of the spread on `page`. A page with no scan in the
/// table above does not compile.
constexpr journal_fragment secret_gog_piece(std::uint16_t page,
                                            journal_region region,
                                            bool begins_paragraph = false) {
  const secret_gog_scan& scan = secret_gog_scans.at(page - 16U);
  return {.page = page,
          .offset = scan.at,
          .length = scan.bytes,
          .image = secret_gog_spread,
          .region = region,
          .begins_paragraph = begins_paragraph};
}

constexpr std::array<journal_fragment, 2> secret_gog_entry001{{
    secret_gog_piece(16, {.left = 77, .top = 119, .width = 164, .height = 30}),
    secret_gog_piece(16, {.left = 55, .top = 149, .width = 297, .height = 25},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry001_art{{
    secret_gog_piece(16,
                     {.left = 70, .top = 185, .width = 689, .height = 1036}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry002{{
    secret_gog_piece(16, {.left = 896, .top = 103, .width = 166, .height = 30}),
    secret_gog_piece(16, {.left = 874, .top = 133, .width = 215, .height = 25},
                     true),
    secret_gog_piece(16, {.left = 873, .top = 169, .width = 345, .height = 321},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry003{{
    secret_gog_piece(16, {.left = 899, .top = 504, .width = 166, .height = 31}),
    secret_gog_piece(16, {.left = 876, .top = 535, .width = 158, .height = 24},
                     true),
    secret_gog_piece(16, {.left = 876, .top = 570, .width = 347, .height = 248},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry004{{
    secret_gog_piece(16, {.left = 901, .top = 833, .width = 166, .height = 30}),
    secret_gog_piece(16, {.left = 878, .top = 864, .width = 192, .height = 48},
                     true),
    secret_gog_piece(16, {.left = 879, .top = 923, .width = 314, .height = 25},
                     true),
    secret_gog_piece(16,
                     {.left = 878, .top = 951, .width = 137, .height = 218}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry004_art{{
    secret_gog_piece(16,
                     {.left = 1037, .top = 974, .width = 187, .height = 187}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry005{{
    secret_gog_piece(16,
                     {.left = 1277, .top = 102, .width = 165, .height = 30}),
    secret_gog_piece(16, {.left = 1254, .top = 133, .width = 181, .height = 47},
                     true),
    secret_gog_piece(
        16, {.left = 1254, .top = 192, .width = 344, .height = 271}, true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry006{{
    secret_gog_piece(16,
                     {.left = 1279, .top = 478, .width = 167, .height = 30}),
    secret_gog_piece(16, {.left = 1256, .top = 508, .width = 294, .height = 53},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry007{{
    secret_gog_piece(16,
                     {.left = 1280, .top = 576, .width = 167, .height = 30}),
    secret_gog_piece(16, {.left = 1256, .top = 606, .width = 294, .height = 25},
                     true),
    secret_gog_piece(
        16, {.left = 1257, .top = 642, .width = 345, .height = 296}, true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry008{{
    secret_gog_piece(17, {.left = 77, .top = 108, .width = 167, .height = 30}),
    secret_gog_piece(17, {.left = 54, .top = 138, .width = 334, .height = 52},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry009{{
    secret_gog_piece(17, {.left = 78, .top = 204, .width = 166, .height = 30}),
    secret_gog_piece(17, {.left = 53, .top = 235, .width = 187, .height = 48},
                     true),
    secret_gog_piece(17, {.left = 54, .top = 295, .width = 341, .height = 222},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry010{{
    secret_gog_piece(17, {.left = 78, .top = 532, .width = 180, .height = 30}),
    secret_gog_piece(17, {.left = 54, .top = 562, .width = 283, .height = 27},
                     true),
    secret_gog_piece(17, {.left = 54, .top = 599, .width = 349, .height = 256},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry011{{
    secret_gog_piece(17, {.left = 79, .top = 873, .width = 176, .height = 31}),
    secret_gog_piece(17, {.left = 56, .top = 904, .width = 220, .height = 24},
                     true),
    secret_gog_piece(17, {.left = 55, .top = 940, .width = 332, .height = 173},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry012{{
    secret_gog_piece(17, {.left = 460, .top = 107, .width = 178, .height = 30}),
    secret_gog_piece(17, {.left = 436, .top = 137, .width = 289, .height = 24},
                     true),
    secret_gog_piece(17, {.left = 436, .top = 173, .width = 348, .height = 242},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry013{{
    secret_gog_piece(17, {.left = 461, .top = 434, .width = 178, .height = 30}),
    secret_gog_piece(17, {.left = 438, .top = 465, .width = 191, .height = 25},
                     true),
    secret_gog_piece(17, {.left = 437, .top = 501, .width = 349, .height = 522},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry014{{
    secret_gog_piece(17,
                     {.left = 461, .top = 1041, .width = 180, .height = 31}),
    secret_gog_piece(17, {.left = 442, .top = 1072, .width = 180, .height = 24},
                     true),
    secret_gog_piece(
        17, {.left = 438, .top = 1107, .width = 343, .height = 101}, true),
    secret_gog_piece(17,
                     {.left = 881, .top = 104, .width = 344, .height = 149}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry015{{
    secret_gog_piece(17, {.left = 906, .top = 268, .width = 178, .height = 30}),
    secret_gog_piece(17, {.left = 883, .top = 298, .width = 304, .height = 27},
                     true),
    secret_gog_piece(17, {.left = 882, .top = 336, .width = 141, .height = 219},
                     true),
    secret_gog_piece(17, {.left = 883, .top = 555, .width = 345, .height = 74}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry015_art{{
    secret_gog_piece(17,
                     {.left = 1043, .top = 340, .width = 186, .height = 188}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry016{{
    secret_gog_piece(17, {.left = 906, .top = 644, .width = 180, .height = 31}),
    secret_gog_piece(17, {.left = 884, .top = 675, .width = 329, .height = 25},
                     true),
    secret_gog_piece(17, {.left = 883, .top = 711, .width = 348, .height = 436},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry017{{
    secret_gog_piece(17,
                     {.left = 1285, .top = 108, .width = 180, .height = 30}),
    secret_gog_piece(17, {.left = 1262, .top = 138, .width = 187, .height = 48},
                     true),
    secret_gog_piece(
        17, {.left = 1261, .top = 198, .width = 349, .height = 315}, true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry018{{
    secret_gog_piece(17,
                     {.left = 1286, .top = 532, .width = 181, .height = 30}),
    secret_gog_piece(17, {.left = 1263, .top = 562, .width = 255, .height = 25},
                     true),
    secret_gog_piece(
        17, {.left = 1263, .top = 598, .width = 351, .height = 610}, true),
    secret_gog_piece(18, {.left = 55, .top = 100, .width = 349, .height = 629}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry019{{
    secret_gog_piece(18, {.left = 78, .top = 748, .width = 181, .height = 31}),
    secret_gog_piece(18, {.left = 59, .top = 779, .width = 244, .height = 24},
                     true),
    secret_gog_piece(18, {.left = 53, .top = 814, .width = 345, .height = 261},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry020{{
    secret_gog_piece(18, {.left = 76, .top = 1087, .width = 180, .height = 31}),
    secret_gog_piece(18, {.left = 52, .top = 1118, .width = 335, .height = 53},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry021{{
    secret_gog_piece(18, {.left = 464, .top = 103, .width = 177, .height = 31}),
    secret_gog_piece(18, {.left = 442, .top = 134, .width = 243, .height = 26},
                     true),
    secret_gog_piece(18, {.left = 439, .top = 170, .width = 343, .height = 295},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry022{{
    secret_gog_piece(18, {.left = 462, .top = 479, .width = 180, .height = 32}),
    secret_gog_piece(18, {.left = 440, .top = 511, .width = 217, .height = 25},
                     true),
    secret_gog_piece(18, {.left = 437, .top = 546, .width = 149, .height = 219},
                     true),
    secret_gog_piece(18,
                     {.left = 437, .top = 765, .width = 325, .height = 113}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry022_art{{
    secret_gog_piece(18,
                     {.left = 596, .top = 555, .width = 187, .height = 191}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry023{{
    secret_gog_piece(18, {.left = 460, .top = 894, .width = 179, .height = 31}),
    secret_gog_piece(18, {.left = 440, .top = 925, .width = 234, .height = 26},
                     true),
    secret_gog_piece(18, {.left = 435, .top = 961, .width = 351, .height = 149},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry024{{
    secret_gog_piece(18, {.left = 902, .top = 110, .width = 179, .height = 30}),
    secret_gog_piece(18, {.left = 881, .top = 140, .width = 305, .height = 28},
                     true),
    secret_gog_piece(18, {.left = 879, .top = 176, .width = 348, .height = 171},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry025{{
    secret_gog_piece(18, {.left = 903, .top = 365, .width = 179, .height = 30}),
    secret_gog_piece(18, {.left = 880, .top = 396, .width = 245, .height = 26},
                     true),
    secret_gog_piece(18, {.left = 880, .top = 432, .width = 143, .height = 194},
                     true),
    secret_gog_piece(18,
                     {.left = 880, .top = 626, .width = 348, .height = 596}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry025_art{{
    secret_gog_piece(18,
                     {.left = 1042, .top = 433, .width = 184, .height = 185}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry026{{
    secret_gog_piece(18,
                     {.left = 1283, .top = 110, .width = 178, .height = 30}),
    secret_gog_piece(18, {.left = 1258, .top = 140, .width = 288, .height = 26},
                     true),
    secret_gog_piece(
        18, {.left = 1258, .top = 176, .width = 343, .height = 198}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry027{{
    secret_gog_piece(18,
                     {.left = 1283, .top = 389, .width = 180, .height = 31}),
    secret_gog_piece(18, {.left = 1260, .top = 420, .width = 240, .height = 24},
                     true),
    secret_gog_piece(
        18, {.left = 1260, .top = 456, .width = 336, .height = 235}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry028{{
    secret_gog_piece(18,
                     {.left = 1285, .top = 706, .width = 179, .height = 30}),
    secret_gog_piece(18, {.left = 1261, .top = 736, .width = 187, .height = 49},
                     true),
    secret_gog_piece(
        18, {.left = 1260, .top = 797, .width = 348, .height = 266}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry029{{
    secret_gog_piece(19, {.left = 84, .top = 104, .width = 180, .height = 30}),
    secret_gog_piece(19, {.left = 60, .top = 134, .width = 182, .height = 48},
                     true),
    secret_gog_piece(19, {.left = 59, .top = 194, .width = 347, .height = 317},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry030{{
    secret_gog_piece(19, {.left = 82, .top = 526, .width = 180, .height = 32}),
    secret_gog_piece(19, {.left = 59, .top = 558, .width = 341, .height = 52},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry031{{
    secret_gog_piece(19, {.left = 81, .top = 624, .width = 178, .height = 32}),
    secret_gog_piece(19, {.left = 59, .top = 656, .width = 208, .height = 24},
                     true),
    secret_gog_piece(19, {.left = 57, .top = 691, .width = 348, .height = 219},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry032{{
    secret_gog_piece(19, {.left = 469, .top = 104, .width = 179, .height = 31}),
    secret_gog_piece(19, {.left = 446, .top = 135, .width = 210, .height = 25},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry032_art{{
    secret_gog_piece(19,
                     {.left = 453, .top = 195, .width = 328, .height = 286}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry033{{
    secret_gog_piece(19, {.left = 80, .top = 928, .width = 179, .height = 32}),
    secret_gog_piece(19, {.left = 58, .top = 960, .width = 144, .height = 24},
                     true),
    secret_gog_piece(19, {.left = 56, .top = 995, .width = 347, .height = 127},
                     true),
    secret_gog_piece(19,
                     {.left = 440, .top = 544, .width = 338, .height = 223}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry034{{
    secret_gog_piece(19, {.left = 464, .top = 782, .width = 179, .height = 31}),
    secret_gog_piece(19, {.left = 439, .top = 813, .width = 198, .height = 50},
                     true),
    secret_gog_piece(19, {.left = 438, .top = 873, .width = 341, .height = 290},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry035{{
    secret_gog_piece(19, {.left = 899, .top = 107, .width = 179, .height = 31}),
    secret_gog_piece(19, {.left = 877, .top = 138, .width = 216, .height = 26},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry035_art{{
    secret_gog_piece(19,
                     {.left = 888, .top = 195, .width = 439, .height = 791}),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry036{{
    secret_gog_piece(19,
                     {.left = 1355, .top = 108, .width = 180, .height = 31}),
    secret_gog_piece(19, {.left = 1333, .top = 139, .width = 216, .height = 51},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry036_art{{
    secret_gog_piece(19,
                     {.left = 1341, .top = 214, .width = 258, .height = 239}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry037{{
    secret_gog_piece(19,
                     {.left = 1102, .top = 1018, .width = 180, .height = 29}),
    secret_gog_piece(
        19, {.left = 1079, .top = 1047, .width = 230, .height = 22}, true),
    secret_gog_piece(
        19, {.left = 1079, .top = 1073, .width = 231, .height = 120}, true),
    secret_gog_piece(19,
                     {.left = 1332, .top = 521, .width = 274, .height = 606}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry037_art{{
    secret_gog_piece(19,
                     {.left = 877, .top = 1017, .width = 186, .height = 188}),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry038{{
    secret_gog_piece(20, {.left = 91, .top = 105, .width = 179, .height = 30}),
    secret_gog_piece(20, {.left = 68, .top = 136, .width = 254, .height = 23},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry038_art{{
    secret_gog_piece(20,
                     {.left = 162, .top = 103, .width = 575, .height = 434}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry039{{
    secret_gog_piece(20, {.left = 85, .top = 567, .width = 181, .height = 31}),
    secret_gog_piece(20, {.left = 61, .top = 598, .width = 299, .height = 25},
                     true),
    secret_gog_piece(20, {.left = 58, .top = 634, .width = 350, .height = 476},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry040{{
    secret_gog_piece(20, {.left = 468, .top = 478, .width = 180, .height = 31}),
    secret_gog_piece(20, {.left = 445, .top = 509, .width = 177, .height = 50},
                     true),
    secret_gog_piece(20, {.left = 443, .top = 569, .width = 350, .height = 348},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry041{{
    secret_gog_piece(20, {.left = 902, .top = 106, .width = 177, .height = 31}),
    secret_gog_piece(20, {.left = 880, .top = 137, .width = 205, .height = 26},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry041_art{{
    secret_gog_piece(20,
                     {.left = 888, .top = 189, .width = 275, .height = 732}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry042{{
    secret_gog_piece(20, {.left = 901, .top = 962, .width = 178, .height = 31}),
    secret_gog_piece(20, {.left = 881, .top = 996, .width = 186, .height = 22},
                     true),
    secret_gog_piece(
        20, {.left = 877, .top = 1028, .width = 346, .height = 150}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry043{{
    secret_gog_piece(20,
                     {.left = 1281, .top = 108, .width = 179, .height = 30}),
    secret_gog_piece(20, {.left = 1258, .top = 138, .width = 204, .height = 48},
                     true),
    secret_gog_piece(
        20, {.left = 1257, .top = 198, .width = 349, .height = 296}, true),
}};
constexpr std::array<journal_fragment, 5> secret_gog_entry044{{
    secret_gog_piece(20,
                     {.left = 1280, .top = 508, .width = 180, .height = 31}),
    secret_gog_piece(20, {.left = 1258, .top = 539, .width = 188, .height = 24},
                     true),
    secret_gog_piece(
        20, {.left = 1256, .top = 575, .width = 350, .height = 345}, true),
    secret_gog_piece(21, {.left = 61, .top = 104, .width = 350, .height = 173},
                     true),
    secret_gog_piece(21,
                     {.left = 445, .top = 104, .width = 349, .height = 157}),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry045{{
    secret_gog_piece(21, {.left = 84, .top = 318, .width = 180, .height = 30}),
    secret_gog_piece(21, {.left = 61, .top = 349, .width = 207, .height = 48},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry045_art{{
    secret_gog_piece(21, {.left = 72, .top = 333, .width = 715, .height = 342}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry046{{
    secret_gog_piece(21, {.left = 85, .top = 699, .width = 180, .height = 31}),
    secret_gog_piece(21, {.left = 62, .top = 730, .width = 238, .height = 24},
                     true),
    secret_gog_piece(21, {.left = 62, .top = 766, .width = 346, .height = 294},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry047{{
    secret_gog_piece(21, {.left = 468, .top = 630, .width = 180, .height = 30}),
    secret_gog_piece(21, {.left = 445, .top = 660, .width = 338, .height = 25},
                     true),
    secret_gog_piece(21, {.left = 444, .top = 697, .width = 349, .height = 452},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry048{{
    secret_gog_piece(21, {.left = 899, .top = 110, .width = 179, .height = 32}),
    secret_gog_piece(21, {.left = 880, .top = 142, .width = 287, .height = 28},
                     true),
    secret_gog_piece(21, {.left = 876, .top = 176, .width = 348, .height = 76},
                     true),
    secret_gog_piece(21,
                     {.left = 1257, .top = 106, .width = 339, .height = 150}),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry049{{
    secret_gog_piece(21, {.left = 901, .top = 282, .width = 179, .height = 31}),
    secret_gog_piece(21, {.left = 878, .top = 313, .width = 234, .height = 50},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry049_art{{
    secret_gog_piece(21,
                     {.left = 888, .top = 386, .width = 712, .height = 320}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry050{{
    secret_gog_piece(21, {.left = 904, .top = 742, .width = 179, .height = 31}),
    secret_gog_piece(21, {.left = 882, .top = 773, .width = 187, .height = 27},
                     true),
    secret_gog_piece(21, {.left = 880, .top = 809, .width = 347, .height = 339},
                     true),
    secret_gog_piece(
        21, {.left = 1259, .top = 739, .width = 345, .height = 259}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry051{{
    secret_gog_piece(22, {.left = 81, .top = 108, .width = 177, .height = 30}),
    secret_gog_piece(22, {.left = 57, .top = 138, .width = 228, .height = 51},
                     true),
    secret_gog_piece(22, {.left = 56, .top = 197, .width = 342, .height = 259},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry052{{
    secret_gog_piece(22, {.left = 81, .top = 483, .width = 179, .height = 31}),
    secret_gog_piece(22, {.left = 58, .top = 514, .width = 298, .height = 25},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry052_art{{
    secret_gog_piece(22, {.left = 65, .top = 557, .width = 345, .height = 309}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry053{{
    secret_gog_piece(22, {.left = 463, .top = 107, .width = 179, .height = 30}),
    secret_gog_piece(22, {.left = 444, .top = 137, .width = 235, .height = 24},
                     true),
    secret_gog_piece(22, {.left = 438, .top = 173, .width = 351, .height = 910},
                     true),
    secret_gog_piece(22, {.left = 870, .top = 107, .width = 340, .height = 150},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry054{{
    secret_gog_piece(22, {.left = 894, .top = 271, .width = 178, .height = 31}),
    secret_gog_piece(22, {.left = 871, .top = 302, .width = 235, .height = 50},
                     true),
    secret_gog_piece(22, {.left = 870, .top = 362, .width = 350, .height = 222},
                     true),
}};
constexpr std::array<journal_fragment, 5> secret_gog_entry055{{
    secret_gog_piece(22, {.left = 894, .top = 599, .width = 179, .height = 30}),
    secret_gog_piece(22, {.left = 872, .top = 630, .width = 130, .height = 24},
                     true),
    secret_gog_piece(22, {.left = 871, .top = 666, .width = 137, .height = 184},
                     true),
    secret_gog_piece(22,
                     {.left = 871, .top = 850, .width = 348, .height = 280}),
    secret_gog_piece(22,
                     {.left = 1248, .top = 107, .width = 344, .height = 368}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry055_art{{
    secret_gog_piece(22,
                     {.left = 1032, .top = 637, .width = 187, .height = 188}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry056{{
    secret_gog_piece(22,
                     {.left = 1274, .top = 490, .width = 179, .height = 31}),
    secret_gog_piece(22, {.left = 1251, .top = 521, .width = 216, .height = 26},
                     true),
    secret_gog_piece(
        22, {.left = 1250, .top = 557, .width = 347, .height = 376}, true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry057{{
    secret_gog_piece(23, {.left = 90, .top = 106, .width = 180, .height = 30}),
    secret_gog_piece(23, {.left = 67, .top = 136, .width = 342, .height = 27},
                     true),
    secret_gog_piece(23, {.left = 64, .top = 172, .width = 343, .height = 515},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry058{{
    secret_gog_piece(23, {.left = 88, .top = 701, .width = 180, .height = 31}),
    secret_gog_piece(23, {.left = 64, .top = 732, .width = 194, .height = 20},
                     true),
    secret_gog_piece(23, {.left = 62, .top = 768, .width = 350, .height = 268},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry059{{
    secret_gog_piece(23, {.left = 472, .top = 108, .width = 180, .height = 31}),
    secret_gog_piece(23, {.left = 452, .top = 139, .width = 304, .height = 25},
                     true),
    secret_gog_piece(23, {.left = 447, .top = 174, .width = 345, .height = 319},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry060{{
    secret_gog_piece(23, {.left = 471, .top = 508, .width = 179, .height = 31}),
    secret_gog_piece(23, {.left = 448, .top = 539, .width = 249, .height = 26},
                     true),
    secret_gog_piece(23, {.left = 447, .top = 575, .width = 337, .height = 53},
                     true),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry061{{
    secret_gog_piece(23, {.left = 471, .top = 643, .width = 176, .height = 31}),
    secret_gog_piece(23, {.left = 447, .top = 674, .width = 309, .height = 25},
                     true),
    secret_gog_piece(23, {.left = 445, .top = 710, .width = 347, .height = 223},
                     true),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry062{{
    secret_gog_piece(23, {.left = 469, .top = 948, .width = 179, .height = 30}),
    secret_gog_piece(23, {.left = 446, .top = 978, .width = 145, .height = 25},
                     true),
    secret_gog_piece(
        23, {.left = 445, .top = 1014, .width = 342, .height = 126}, true),
    secret_gog_piece(23,
                     {.left = 877, .top = 105, .width = 348, .height = 260}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry063{{
    secret_gog_piece(23, {.left = 900, .top = 378, .width = 179, .height = 31}),
    secret_gog_piece(23, {.left = 878, .top = 411, .width = 123, .height = 23},
                     true),
    secret_gog_piece(23, {.left = 877, .top = 446, .width = 145, .height = 171},
                     true),
    secret_gog_piece(23,
                     {.left = 877, .top = 617, .width = 348, .height = 337}),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry063_art{{
    secret_gog_piece(23,
                     {.left = 1039, .top = 414, .width = 186, .height = 189}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry064{{
    secret_gog_piece(23, {.left = 901, .top = 972, .width = 178, .height = 31}),
    secret_gog_piece(23, {.left = 878, .top = 1003, .width = 238, .height = 27},
                     true),
    secret_gog_piece(
        23, {.left = 877, .top = 1037, .width = 349, .height = 152}, true),
    secret_gog_piece(23,
                     {.left = 1255, .top = 106, .width = 337, .height = 97}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry065{{
    secret_gog_piece(23,
                     {.left = 1279, .top = 221, .width = 179, .height = 31}),
    secret_gog_piece(23, {.left = 1257, .top = 252, .width = 155, .height = 24},
                     true),
    secret_gog_piece(
        23, {.left = 1256, .top = 288, .width = 342, .height = 221}, true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry066{{
    secret_gog_piece(23,
                     {.left = 1278, .top = 544, .width = 179, .height = 31}),
    secret_gog_piece(23, {.left = 1254, .top = 575, .width = 271, .height = 51},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry066_art{{
    secret_gog_piece(23,
                     {.left = 1259, .top = 646, .width = 337, .height = 378}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry067{{
    secret_gog_piece(24, {.left = 89, .top = 106, .width = 180, .height = 29}),
    secret_gog_piece(24, {.left = 66, .top = 136, .width = 234, .height = 25},
                     true),
    secret_gog_piece(24, {.left = 64, .top = 171, .width = 347, .height = 436},
                     true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry068{{
    secret_gog_piece(24, {.left = 470, .top = 106, .width = 179, .height = 30}),
    secret_gog_piece(24, {.left = 447, .top = 136, .width = 220, .height = 50},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry068_art{{
    secret_gog_piece(24,
                     {.left = 461, .top = 214, .width = 287, .height = 663}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry069{{
    secret_gog_piece(24, {.left = 468, .top = 923, .width = 180, .height = 31}),
    secret_gog_piece(24, {.left = 444, .top = 954, .width = 138, .height = 24},
                     true),
    secret_gog_piece(24, {.left = 444, .top = 990, .width = 347, .height = 210},
                     true),
    secret_gog_piece(24,
                     {.left = 873, .top = 104, .width = 347, .height = 255}),
}};
constexpr std::array<journal_fragment, 4> secret_gog_entry070{{
    secret_gog_piece(24, {.left = 896, .top = 376, .width = 179, .height = 31}),
    secret_gog_piece(24, {.left = 874, .top = 407, .width = 144, .height = 25},
                     true),
    secret_gog_piece(24, {.left = 873, .top = 443, .width = 332, .height = 78},
                     true),
    secret_gog_piece(24,
                     {.left = 1251, .top = 105, .width = 338, .height = 97}),
}};
constexpr std::array<journal_fragment, 3> secret_gog_entry071{{
    secret_gog_piece(24,
                     {.left = 1275, .top = 220, .width = 177, .height = 31}),
    secret_gog_piece(24, {.left = 1251, .top = 251, .width = 277, .height = 26},
                     true),
    secret_gog_piece(
        24, {.left = 1252, .top = 287, .width = 343, .height = 150}, true),
}};
constexpr std::array<journal_fragment, 2> secret_gog_entry072{{
    secret_gog_piece(24, {.left = 899, .top = 568, .width = 179, .height = 31}),
    secret_gog_piece(24, {.left = 877, .top = 599, .width = 262, .height = 27},
                     true),
}};
constexpr std::array<journal_fragment, 1> secret_gog_entry072_art{{
    secret_gog_piece(24,
                     {.left = 884, .top = 655, .width = 714, .height = 215}),
}};
}  // namespace

constexpr std::array<journal_entry_fact, 72> secret_gog_entries{{
    {.kind = journal_kind::entry,
     .number = 1,
     .fragments = secret_gog_entry001,
     .art = secret_gog_entry001_art},
    {.kind = journal_kind::entry,
     .number = 2,
     .fragments = secret_gog_entry002},
    {.kind = journal_kind::entry,
     .number = 3,
     .fragments = secret_gog_entry003},
    {.kind = journal_kind::entry,
     .number = 4,
     .fragments = secret_gog_entry004,
     .art = secret_gog_entry004_art},
    {.kind = journal_kind::entry,
     .number = 5,
     .fragments = secret_gog_entry005},
    {.kind = journal_kind::entry,
     .number = 6,
     .fragments = secret_gog_entry006},
    {.kind = journal_kind::entry,
     .number = 7,
     .fragments = secret_gog_entry007},
    {.kind = journal_kind::entry,
     .number = 8,
     .fragments = secret_gog_entry008},
    {.kind = journal_kind::entry,
     .number = 9,
     .fragments = secret_gog_entry009},
    {.kind = journal_kind::entry,
     .number = 10,
     .fragments = secret_gog_entry010},
    {.kind = journal_kind::entry,
     .number = 11,
     .fragments = secret_gog_entry011},
    {.kind = journal_kind::entry,
     .number = 12,
     .fragments = secret_gog_entry012},
    {.kind = journal_kind::entry,
     .number = 13,
     .fragments = secret_gog_entry013},
    {.kind = journal_kind::entry,
     .number = 14,
     .fragments = secret_gog_entry014},
    {.kind = journal_kind::entry,
     .number = 15,
     .fragments = secret_gog_entry015,
     .art = secret_gog_entry015_art},
    {.kind = journal_kind::entry,
     .number = 16,
     .fragments = secret_gog_entry016},
    {.kind = journal_kind::entry,
     .number = 17,
     .fragments = secret_gog_entry017},
    {.kind = journal_kind::entry,
     .number = 18,
     .fragments = secret_gog_entry018},
    {.kind = journal_kind::entry,
     .number = 19,
     .fragments = secret_gog_entry019},
    {.kind = journal_kind::entry,
     .number = 20,
     .fragments = secret_gog_entry020},
    {.kind = journal_kind::entry,
     .number = 21,
     .fragments = secret_gog_entry021},
    {.kind = journal_kind::entry,
     .number = 22,
     .fragments = secret_gog_entry022,
     .art = secret_gog_entry022_art},
    {.kind = journal_kind::entry,
     .number = 23,
     .fragments = secret_gog_entry023},
    {.kind = journal_kind::entry,
     .number = 24,
     .fragments = secret_gog_entry024},
    {.kind = journal_kind::entry,
     .number = 25,
     .fragments = secret_gog_entry025,
     .art = secret_gog_entry025_art},
    {.kind = journal_kind::entry,
     .number = 26,
     .fragments = secret_gog_entry026},
    {.kind = journal_kind::entry,
     .number = 27,
     .fragments = secret_gog_entry027},
    {.kind = journal_kind::entry,
     .number = 28,
     .fragments = secret_gog_entry028},
    {.kind = journal_kind::entry,
     .number = 29,
     .fragments = secret_gog_entry029},
    {.kind = journal_kind::entry,
     .number = 30,
     .fragments = secret_gog_entry030},
    {.kind = journal_kind::entry,
     .number = 31,
     .fragments = secret_gog_entry031},
    {.kind = journal_kind::entry,
     .number = 32,
     .fragments = secret_gog_entry032,
     .art = secret_gog_entry032_art},
    {.kind = journal_kind::entry,
     .number = 33,
     .fragments = secret_gog_entry033},
    {.kind = journal_kind::entry,
     .number = 34,
     .fragments = secret_gog_entry034},
    {.kind = journal_kind::entry,
     .number = 35,
     .fragments = secret_gog_entry035,
     .art = secret_gog_entry035_art},
    {.kind = journal_kind::entry,
     .number = 36,
     .fragments = secret_gog_entry036,
     .art = secret_gog_entry036_art},
    {.kind = journal_kind::entry,
     .number = 37,
     .fragments = secret_gog_entry037,
     .art = secret_gog_entry037_art},
    {.kind = journal_kind::entry,
     .number = 38,
     .fragments = secret_gog_entry038,
     .art = secret_gog_entry038_art},
    {.kind = journal_kind::entry,
     .number = 39,
     .fragments = secret_gog_entry039},
    {.kind = journal_kind::entry,
     .number = 40,
     .fragments = secret_gog_entry040},
    {.kind = journal_kind::entry,
     .number = 41,
     .fragments = secret_gog_entry041,
     .art = secret_gog_entry041_art},
    {.kind = journal_kind::entry,
     .number = 42,
     .fragments = secret_gog_entry042},
    {.kind = journal_kind::entry,
     .number = 43,
     .fragments = secret_gog_entry043},
    {.kind = journal_kind::entry,
     .number = 44,
     .fragments = secret_gog_entry044},
    {.kind = journal_kind::entry,
     .number = 45,
     .fragments = secret_gog_entry045,
     .art = secret_gog_entry045_art},
    {.kind = journal_kind::entry,
     .number = 46,
     .fragments = secret_gog_entry046},
    {.kind = journal_kind::entry,
     .number = 47,
     .fragments = secret_gog_entry047},
    {.kind = journal_kind::entry,
     .number = 48,
     .fragments = secret_gog_entry048},
    {.kind = journal_kind::entry,
     .number = 49,
     .fragments = secret_gog_entry049,
     .art = secret_gog_entry049_art},
    {.kind = journal_kind::entry,
     .number = 50,
     .fragments = secret_gog_entry050},
    {.kind = journal_kind::entry,
     .number = 51,
     .fragments = secret_gog_entry051},
    {.kind = journal_kind::entry,
     .number = 52,
     .fragments = secret_gog_entry052,
     .art = secret_gog_entry052_art},
    {.kind = journal_kind::entry,
     .number = 53,
     .fragments = secret_gog_entry053},
    {.kind = journal_kind::entry,
     .number = 54,
     .fragments = secret_gog_entry054},
    {.kind = journal_kind::entry,
     .number = 55,
     .fragments = secret_gog_entry055,
     .art = secret_gog_entry055_art},
    {.kind = journal_kind::entry,
     .number = 56,
     .fragments = secret_gog_entry056},
    {.kind = journal_kind::entry,
     .number = 57,
     .fragments = secret_gog_entry057},
    {.kind = journal_kind::entry,
     .number = 58,
     .fragments = secret_gog_entry058},
    {.kind = journal_kind::entry,
     .number = 59,
     .fragments = secret_gog_entry059},
    {.kind = journal_kind::entry,
     .number = 60,
     .fragments = secret_gog_entry060},
    {.kind = journal_kind::entry,
     .number = 61,
     .fragments = secret_gog_entry061},
    {.kind = journal_kind::entry,
     .number = 62,
     .fragments = secret_gog_entry062},
    {.kind = journal_kind::entry,
     .number = 63,
     .fragments = secret_gog_entry063,
     .art = secret_gog_entry063_art},
    {.kind = journal_kind::entry,
     .number = 64,
     .fragments = secret_gog_entry064},
    {.kind = journal_kind::entry,
     .number = 65,
     .fragments = secret_gog_entry065},
    {.kind = journal_kind::entry,
     .number = 66,
     .fragments = secret_gog_entry066,
     .art = secret_gog_entry066_art},
    {.kind = journal_kind::entry,
     .number = 67,
     .fragments = secret_gog_entry067},
    {.kind = journal_kind::entry,
     .number = 68,
     .fragments = secret_gog_entry068,
     .art = secret_gog_entry068_art},
    {.kind = journal_kind::entry,
     .number = 69,
     .fragments = secret_gog_entry069},
    {.kind = journal_kind::entry,
     .number = 70,
     .fragments = secret_gog_entry070},
    {.kind = journal_kind::entry,
     .number = 71,
     .fragments = secret_gog_entry071},
    {.kind = journal_kind::entry,
     .number = 72,
     .fragments = secret_gog_entry072,
     .art = secret_gog_entry072_art},
}};

}  // namespace amberfolio::host::journal_rows
