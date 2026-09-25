// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal edition fact table. journal_facts.h has the reasoning; the
// hex comparison is `machine::digest_is`, because a fingerprint has to
// mean the same thing whichever of this project's three tables it is in.

#include "amberfolio/host/journal_facts.h"

#include <array>
#include <cstdint>
#include <span>

#include "amberfolio/machine/edition.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

/// The Adventurer's Journal as the release sold on GOG ships
/// it: **all three of its numbered sections** — fifty-eight journal
/// entries, twenty-three tavern tales and eighteen proclamations, in
/// ninety-nine rows and a hundred and seventeen pieces, across eleven
/// two-page scans (M5-E3b #214, M5-E3d #218).
///
/// **Measured, never transcribed.** Every number below is a fact about
/// the file — the byte offset of a stream, its `/Length`, an image's
/// shape, a rectangle of it — which is what CONTRIBUTING.md permits to
/// be written down about an artifact. No pixel and no word of the
/// document is here, and none ever will be.
///
/// **The entries flow, and that is why a row is a list.** They are set
/// two columns to a printed page and two printed pages to a scan, and
/// an entry runs out of its column and resumes at the top of the next;
/// four of them resume on the facing page, which is a different stream
/// altogether. Sixteen of the fifty-eight are in more than one
/// piece. `journal_fragment` is what that costs and what it buys.
///
/// A piece with no ink in it is not here: an entry that happened to end
/// exactly at the foot of its column would otherwise carry an empty
/// rectangle, and asking an engine to read a blank is asking it for an
/// answer nobody wants.
///
/// **And a boundary is usually a continuation, six times out of
/// eighteen it is not** (#361). A paragraph can end exactly where its
/// column does, and since #357 a piece can resume under its entry's own
/// drawing; either way the hosts' single newline runs the sentence
/// before the break into the sentence after it, on one line. Which
/// boundary is which is ink: a paragraph of this edition opens with an
/// indent, and the first line of a resuming piece is either within two
/// samples of that piece's own left margin — twelve of them, and the
/// widest is two — or 18 to 22 right of it, which is the six below.
/// Nothing lands between. `journal_fragment::begins_paragraph` carries
/// the answer to whoever joins the pieces.
///
/// **How the rectangles were found**, because the next person needs to
/// know whether to trust them. The column geometry was measured off the
/// scans, and an item was taken to run from its heading to the next one
/// wherever that fell. Finding the headings took two methods, and which
/// one a section needed is a fact about how it is set:
///
/// - The **entries** open with a long phrase in a display face at the
///   column margin, so the phrase's own bitmap was matched down each
///   column. The numbering that comes out is a chain, so it was checked
///   against the printed numbers on every scan — including the two places
///   a chain would have drifted silently: the maps scan, whose single
///   entry covers it end to end, and the last, which has to land on
///   fifty-eight.
/// - The **tales** and the **proclamations** are set in the body face at
///   the body size, and "Tale" is four characters indented into its own
///   paragraph. A bitmap of either correlates as well with any line of
///   prose, so instead each column was cut into lines and an engine was
///   asked what each line opened with. That is only possible since #216;
///   before it there was no engine on the machine this was measured on.
///   A gap cannot separate the proclamations either — they sit closer
///   together than the gap inside one of them — so the block breaks are
///   the measured heading positions rather than a distance.
///
/// Read back out of their own rectangles by that engine, twenty-three of
/// twenty-three tales and eighteen of eighteen proclamations begin with
/// their own printed heading. Both sections ascend in reading order,
/// which is what caught the one misreading: an italic `CIX` whose `I`
/// carries a swash, called `CLIX` by a plain run and a whitelisted one
/// alike, and settled by eye against the scan.
///
/// **The proclamations are not contiguous and do not start at one.** They
/// are printed in Roman numerals and run 59 to 214 with gaps; what is
/// stored is the value, because a numeral is a way of writing a number
/// and the reader does the writing.

// The scans these are printed on: the byte offset of each stream's
// first data byte, and its `/Length`. Measured off the document,
// not transcribed from it.
constexpr std::uint64_t scan07_at = 1861904;
constexpr std::uint32_t scan07_bytes = 312593;
constexpr std::uint64_t scan08_at = 2175041;
constexpr std::uint32_t scan08_bytes = 294838;
constexpr std::uint64_t scan09_at = 2470423;
constexpr std::uint32_t scan09_bytes = 311899;
constexpr std::uint64_t scan10_at = 2782866;
constexpr std::uint32_t scan10_bytes = 326513;
constexpr std::uint64_t scan11_at = 3109923;
constexpr std::uint32_t scan11_bytes = 330739;
constexpr std::uint64_t scan12_at = 3441206;
constexpr std::uint32_t scan12_bytes = 312119;
constexpr std::uint64_t scan13_at = 3753869;
constexpr std::uint32_t scan13_bytes = 281465;
constexpr std::uint64_t scan14_at = 4035878;
constexpr std::uint32_t scan14_bytes = 329844;
constexpr std::uint64_t scan15_at = 4366266;
constexpr std::uint32_t scan15_bytes = 344515;
constexpr std::uint64_t scan16_at = 4711325;
constexpr std::uint32_t scan16_bytes = 265755;
constexpr std::uint64_t scan17_at = 4977624;
constexpr std::uint32_t scan17_bytes = 238368;

/// Every entry scan is one shape: a two-page spread, RGB, eight bits a
/// component, `/DCTDecode` — which this build carries to the engine
/// rather than decoding (#212).
constexpr journal_image spread{
    .width = 1328,
    .height = 1003,
    .bits_per_component = 8,
    .components = 3,
    .predictor = 1,
    .filter = journal_filter::dct,
    .inverted = false,
};

// The pieces, entry by entry. A row of one is an entry that fitted in
// its column; a row of more is one that did not.
constexpr std::array<journal_fragment, 1> entry01{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 702, .top = 270, .width = 290, .height = 413}},
}};
// One piece, and it used to be two (#344). The second claimed rows 8 to
// 262 of the next column, which is where the entries' own section opens:
// this scan's right-hand page carries a display heading across rows 25 to
// 64 and a paragraph across rows 89 to 232, both the width of the page
// and above the two columns, with a printed rule at rows 244 and 245
// under them. Nothing there is a numbered item, and the reader showed all
// of it as the tail of this entry. The chain that measured the table runs
// an item from its heading to the next one down the columns, and on the
// one page where a section begins that walk steps over matter printed
// above the column grid; this entry ends at the foot of its own column,
// where its last inked row is 959.
constexpr std::array<journal_fragment, 1> entry02{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 702, .top = 687, .width = 290, .height = 271}},
}};
constexpr std::array<journal_fragment, 1> entry03{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 992, .top = 267, .width = 297, .height = 691}},
}};
// **A text piece stops at its entry's own picture**, and resumes under
// it if there is more prose there (#357). Twelve entries have a drawing
// set in the column their caption is in, and a rectangle measured to the
// whole column hands the engine the drawing as well: the hand lettering
// on a map, the label beside a maze's door, two runes and a path
// symbol, the place names of an atlas. None of it is prose, all of it
// arrives as the entry's own words, and the reader draws the picture
// anyway. So the
// column is cut around the art rectangle -- above it, and below it where
// prose continues, which in this edition is entry 26 alone -- and a
// piece with no ink left in it is dropped.
constexpr std::array<journal_fragment, 1> entry04{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 23, .width = 290, .height = 55}},
}};
constexpr std::array<journal_fragment, 1> entry05{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 371, .width = 290, .height = 269}},
}};
// The two pieces on this printed page stop at row 780 and not at the
// foot of the scan, and so does entry 7's first piece beside them: the
// page carries the edition's legend for its map symbols under a printed
// rule at rows 784 to 787, the width of the page and below its two
// columns. That is #344's finding upside down -- there the matter was
// printed *above* the grid, here below it -- and it costs the same
// thing, because the walk that measured this table runs an item from its
// heading to the next one down the columns and steps straight over
// anything set outside them. Both entries showed the legend spliced into
// their middle, at the seam between the column they end and the one they
// resume in.
constexpr std::array<journal_fragment, 2> entry06{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 644, .width = 290, .height = 137}},
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 340, .top = 8, .width = 289, .height = 235}},
}};
constexpr std::array<journal_fragment, 2> entry07{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 340, .top = 247, .width = 289, .height = 534}},
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 392}},
}};
constexpr std::array<journal_fragment, 1> entry08{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 702, .top = 404, .width = 290, .height = 330}},
}};
constexpr std::array<journal_fragment, 2> entry09{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 702, .top = 738, .width = 290, .height = 220}},
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 206},
     .begins_paragraph = true},
}};
constexpr std::array<journal_fragment, 1> entry10{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 992, .top = 218, .width = 297, .height = 84}},
}};
constexpr std::array<journal_fragment, 2> entry11{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 992, .top = 533, .width = 297, .height = 425}},
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 50, .top = 8, .width = 290, .height = 189}},
}};
constexpr std::array<journal_fragment, 1> entry12{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 50, .top = 201, .width = 290, .height = 208}},
}};
constexpr std::array<journal_fragment, 1> entry13{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 50, .top = 413, .width = 290, .height = 545}},
}};
constexpr std::array<journal_fragment, 1> entry14{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 340, .top = 20, .width = 289, .height = 451}},
}};
constexpr std::array<journal_fragment, 1> entry15{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 340, .top = 475, .width = 289, .height = 65}},
}};
constexpr std::array<journal_fragment, 2> entry16{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 340, .top = 826, .width = 289, .height = 132}},
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 352}},
}};
constexpr std::array<journal_fragment, 1> entry17{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 702, .top = 364, .width = 290, .height = 269}},
}};
constexpr std::array<journal_fragment, 2> entry18{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 702, .top = 637, .width = 290, .height = 321}},
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 149}},
}};
constexpr std::array<journal_fragment, 1> entry19{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 992, .top = 161, .width = 297, .height = 248}},
}};
constexpr std::array<journal_fragment, 1> entry20{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 992, .top = 413, .width = 297, .height = 545}},
}};
constexpr std::array<journal_fragment, 1> entry21{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 50, .top = 24, .width = 290, .height = 490}},
}};
constexpr std::array<journal_fragment, 1> entry22{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 50, .top = 518, .width = 290, .height = 76}},
}};
constexpr std::array<journal_fragment, 2> entry23{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 50, .top = 793, .width = 290, .height = 165}},
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 340, .top = 8, .width = 289, .height = 252},
     .begins_paragraph = true},
}};
constexpr std::array<journal_fragment, 1> entry24{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 340, .top = 264, .width = 289, .height = 575}},
}};
constexpr std::array<journal_fragment, 2> entry25{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 340, .top = 843, .width = 289, .height = 115}},
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 457}},
}};
// The one entry of the edition whose prose resumes *under* its drawing
// rather than ending at it, which is why the cut above is two pieces and
// not a shorter one. What resumes there opens a paragraph rather than
// carrying one on (#361), which is the reading the cut itself made
// possible to get wrong: the sentence over the maze ends, and the one
// under it starts a new indented quotation.
constexpr std::array<journal_fragment, 3> entry26{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 702, .top = 469, .width = 290, .height = 166}},
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 702, .top = 855, .width = 290, .height = 103},
     .begins_paragraph = true},
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 255}},
}};
constexpr std::array<journal_fragment, 1> entry27{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 992, .top = 267, .width = 297, .height = 432}},
}};
constexpr std::array<journal_fragment, 1> entry28{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 992, .top = 703, .width = 297, .height = 71}},
}};
constexpr std::array<journal_fragment, 1> entry29{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 50, .top = 32, .width = 290, .height = 64}},
}};
constexpr std::array<journal_fragment, 1> entry30{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 50, .top = 380, .width = 290, .height = 578}},
}};
constexpr std::array<journal_fragment, 1> entry31{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 340, .top = 29, .width = 289, .height = 471}},
}};
constexpr std::array<journal_fragment, 1> entry32{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 340, .top = 504, .width = 289, .height = 331}},
}};
constexpr std::array<journal_fragment, 2> entry33{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 340, .top = 839, .width = 289, .height = 119}},
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 233}},
}};
constexpr std::array<journal_fragment, 1> entry34{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 702, .top = 245, .width = 290, .height = 289}},
}};
constexpr std::array<journal_fragment, 2> entry35{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 702, .top = 538, .width = 290, .height = 420}},
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 68},
     .begins_paragraph = true},
}};
constexpr std::array<journal_fragment, 1> entry36{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 992, .top = 142, .width = 297, .height = 816}},
}};
// The atlas, and the one row whose rectangle is **not** a column: a
// heading and a caption set the width of the left printed page, and
// after them the whole spread is maps. Two column-shaped pieces cut the
// caption's own longest word in half, and each half was read as a word
// (#357). The right page held no prose at all once the maps were
// measured out of it, so it has no piece here.
constexpr std::array<journal_fragment, 1> entry37{{
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 50, .top = 22, .width = 579, .height = 78}},
}};
constexpr std::array<journal_fragment, 1> entry38{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 50, .top = 21, .width = 290, .height = 470}},
}};
// The same shape again, and the matter below the grid is a drawing:
// entry 42's sketch is printed the width of this page under a rule at
// rows 731 and 732, so both columns end above it -- this one at 728, and
// entry 42's own at 751, under the caption the sketch belongs to. Left
// at the foot of the scan, this entry carried half of somebody else's
// labels and entry 42 carried the other half, ahead of its own heading.
constexpr std::array<journal_fragment, 2> entry39{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 50, .top = 495, .width = 290, .height = 234}},
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 340, .top = 8, .width = 289, .height = 169}},
}};
constexpr std::array<journal_fragment, 1> entry40{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 340, .top = 181, .width = 289, .height = 167}},
}};
constexpr std::array<journal_fragment, 1> entry41{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 340, .top = 352, .width = 289, .height = 66}},
}};
constexpr std::array<journal_fragment, 1> entry42{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 340, .top = 705, .width = 289, .height = 66}},
}};
constexpr std::array<journal_fragment, 2> entry43{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 702, .top = 21, .width = 290, .height = 937}},
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 190},
     .begins_paragraph = true},
}};
constexpr std::array<journal_fragment, 1> entry44{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 992, .top = 202, .width = 297, .height = 756}},
}};
constexpr std::array<journal_fragment, 1> entry45{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 50, .top = 21, .width = 290, .height = 937}},
}};
constexpr std::array<journal_fragment, 1> entry46{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 340, .top = 19, .width = 289, .height = 716}},
}};
constexpr std::array<journal_fragment, 2> entry47{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 340, .top = 739, .width = 289, .height = 219}},
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 256},
     .begins_paragraph = true},
}};
constexpr std::array<journal_fragment, 1> entry48{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 702, .top = 268, .width = 290, .height = 489}},
}};
constexpr std::array<journal_fragment, 2> entry49{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 702, .top = 761, .width = 290, .height = 197}},
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 277}},
}};
constexpr std::array<journal_fragment, 1> entry50{{
    {.page = 15,
     .offset = scan15_at,
     .length = scan15_bytes,
     .image = spread,
     .region = {.left = 992, .top = 289, .width = 297, .height = 669}},
}};
constexpr std::array<journal_fragment, 1> entry51{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 50, .top = 24, .width = 290, .height = 450}},
}};
constexpr std::array<journal_fragment, 1> entry52{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 50, .top = 478, .width = 290, .height = 480}},
}};
constexpr std::array<journal_fragment, 1> entry53{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 340, .top = 24, .width = 289, .height = 205}},
}};
constexpr std::array<journal_fragment, 1> entry54{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 340, .top = 233, .width = 289, .height = 725}},
}};
constexpr std::array<journal_fragment, 1> entry55{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 702, .top = 28, .width = 290, .height = 306}},
}};
constexpr std::array<journal_fragment, 1> entry56{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 702, .top = 338, .width = 290, .height = 183}},
}};
constexpr std::array<journal_fragment, 2> entry57{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 702, .top = 525, .width = 290, .height = 433}},
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 177}},
}};
constexpr std::array<journal_fragment, 1> entry58{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 992, .top = 189, .width = 297, .height = 60}},
}};
constexpr std::array<journal_fragment, 1> tale01{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 64, .top = 432, .width = 273, .height = 66}},
}};
constexpr std::array<journal_fragment, 1> tale02{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 65, .top = 513, .width = 264, .height = 66}},
}};
constexpr std::array<journal_fragment, 1> tale03{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 63, .top = 595, .width = 274, .height = 66}},
}};
constexpr std::array<journal_fragment, 1> tale04{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 66, .top = 676, .width = 273, .height = 87}},
}};
constexpr std::array<journal_fragment, 1> tale05{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 67, .top = 778, .width = 271, .height = 63}},
}};
constexpr std::array<journal_fragment, 1> tale06{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 364, .top = 185, .width = 268, .height = 82}},
}};
constexpr std::array<journal_fragment, 1> tale07{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 366, .top = 287, .width = 276, .height = 62}},
}};
constexpr std::array<journal_fragment, 1> tale08{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 367, .top = 368, .width = 279, .height = 47}},
}};
constexpr std::array<journal_fragment, 1> tale09{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 368, .top = 429, .width = 270, .height = 107}},
}};
constexpr std::array<journal_fragment, 1> tale10{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 367, .top = 551, .width = 280, .height = 67}},
}};
constexpr std::array<journal_fragment, 1> tale11{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 370, .top = 633, .width = 276, .height = 62}},
}};
constexpr std::array<journal_fragment, 1> tale12{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 703, .top = 44, .width = 277, .height = 84}},
}};
constexpr std::array<journal_fragment, 1> tale13{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 704, .top = 143, .width = 269, .height = 65}},
}};
constexpr std::array<journal_fragment, 1> tale14{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 703, .top = 224, .width = 267, .height = 66}},
}};
constexpr std::array<journal_fragment, 1> tale15{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 701, .top = 305, .width = 271, .height = 86}},
}};
constexpr std::array<journal_fragment, 1> tale16{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 703, .top = 407, .width = 271, .height = 61}},
}};
constexpr std::array<journal_fragment, 1> tale17{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 703, .top = 488, .width = 278, .height = 86}},
}};
constexpr std::array<journal_fragment, 1> tale18{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 704, .top = 589, .width = 274, .height = 102}},
}};
constexpr std::array<journal_fragment, 1> tale19{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 704, .top = 712, .width = 270, .height = 86}},
}};
constexpr std::array<journal_fragment, 1> tale20{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 703, .top = 814, .width = 278, .height = 101}},
}};
constexpr std::array<journal_fragment, 1> tale21{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 1005, .top = 44, .width = 279, .height = 84}},
}};
constexpr std::array<journal_fragment, 1> tale22{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 1005, .top = 143, .width = 280, .height = 101}},
}};
constexpr std::array<journal_fragment, 1> tale23{{
    {.page = 17,
     .offset = scan17_at,
     .length = scan17_bytes,
     .image = spread,
     .region = {.left = 1007, .top = 266, .width = 268, .height = 82}},
}};
constexpr std::array<journal_fragment, 1> procl059{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 51, .top = 282, .width = 276, .height = 211}},
}};
constexpr std::array<journal_fragment, 1> procl064{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 52, .top = 509, .width = 279, .height = 189}},
}};
constexpr std::array<journal_fragment, 1> procl078{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 52, .top = 719, .width = 283, .height = 173}},
}};
constexpr std::array<journal_fragment, 1> procl101{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 356, .top = 281, .width = 282, .height = 213}},
}};
constexpr std::array<journal_fragment, 1> procl109{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 357, .top = 510, .width = 281, .height = 151}},
}};
constexpr std::array<journal_fragment, 1> procl110{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 357, .top = 678, .width = 277, .height = 173}},
}};
constexpr std::array<journal_fragment, 1> procl114{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 695, .top = 22, .width = 280, .height = 206}},
}};
constexpr std::array<journal_fragment, 1> procl120{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 697, .top = 249, .width = 284, .height = 233}},
}};
constexpr std::array<journal_fragment, 1> procl126{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 701, .top = 498, .width = 280, .height = 148}},
}};
constexpr std::array<journal_fragment, 1> procl129{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 703, .top = 667, .width = 283, .height = 210}},
}};
constexpr std::array<journal_fragment, 1> procl134{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 999, .top = 21, .width = 284, .height = 208}},
}};
constexpr std::array<journal_fragment, 1> procl154{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 1004, .top = 247, .width = 282, .height = 234}},
}};
constexpr std::array<journal_fragment, 1> procl156{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 1007, .top = 497, .width = 282, .height = 229}},
}};
constexpr std::array<journal_fragment, 2> procl170{{
    {.page = 7,
     .offset = scan07_at,
     .length = scan07_bytes,
     .image = spread,
     .region = {.left = 1009, .top = 747, .width = 285, .height = 192}},
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 48, .top = 20, .width = 277, .height = 64}},
}};
constexpr std::array<journal_fragment, 1> procl190{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 45, .top = 102, .width = 283, .height = 270}},
}};
constexpr std::array<journal_fragment, 1> procl201{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 49, .top = 391, .width = 280, .height = 229}},
}};
constexpr std::array<journal_fragment, 1> procl204{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 350, .top = 20, .width = 285, .height = 270}},
}};
constexpr std::array<journal_fragment, 1> procl214{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 352, .top = 309, .width = 287, .height = 210}},
}};

// The pictures. Twelve of the fifty-eight entries are drawings rather
// than prose -- maps, mazes, a diagram, marks scratched in dirt -- and
// one of them, the atlas, is three maps across a whole spread; fourteen
// rectangles in all (#328). An OCR engine reads what words are on such a
// page, which is the caption, so before this the reader showed the
// caption and nineteen empty rows.
//
// **Measured to the ink, not to the column**, which is the one way these
// rectangles differ in kind from the text ones above. A text fragment is
// the column, because everything outside it is prose to throw away; a
// picture is its own bounding box, because everything outside it is
// paper to draw. Where the drawing sits inside a printed rule the rule
// is inside the rectangle, since it is part of what was printed.
//
// How they were found, so the next edition is a procedure rather than an
// archaeology (`docs/journal.md` section 11): type on these pages is an
// inked band about fourteen rows deep and then a blank one, so a run of
// consecutive inked rows much longer than that is a candidate -- a sieve
// and not an answer, because prose descenders bridge lines into runs of
// a hundred and forty rows and three of these are inside that. What it
// buys is about twenty candidates over eleven spreads, each of which was
// then looked at; the fourteenth is three runes at the top of a column
// and the sieve cannot see it at all. Each rectangle is then the
// bounding box of the candidate's ink, in a band whose top is below the
// entry's caption. Four of the fourteen fall outside every one of their
// own entry's text rectangles -- the atlas's three maps, each of which
// crosses the columns its caption is set in, and one drawing that runs
// the width of a printed page -- which is why art is a field of its own.
constexpr std::array<journal_fragment, 1> art04{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 53, .top = 78, .width = 270, .height = 269}},
}};
// Measured again: the box stopped at row 509 and the map's own bottom
// border line is rows 512 to 515, so the drawing was shown open at the
// foot. A picture is the bounding box of its ink (section 11.1) and this
// is now exactly that, 1014..1267 by 302..515.
constexpr std::array<journal_fragment, 1> art10{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 1014, .top = 302, .width = 254, .height = 214}},
}};
constexpr std::array<journal_fragment, 1> art15{{
    {.page = 10,
     .offset = scan10_at,
     .length = scan10_bytes,
     .image = spread,
     .region = {.left = 355, .top = 540, .width = 251, .height = 255}},
}};
constexpr std::array<journal_fragment, 1> art22{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 52, .top = 594, .width = 272, .height = 172}},
}};
constexpr std::array<journal_fragment, 1> art26{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 713, .top = 635, .width = 256, .height = 220}},
}};
constexpr std::array<journal_fragment, 1> art28{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 1015, .top = 774, .width = 268, .height = 137}},
}};
constexpr std::array<journal_fragment, 1> art29{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 50, .top = 96, .width = 247, .height = 255}},
}};
constexpr std::array<journal_fragment, 1> art35{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 1034, .top = 76, .width = 210, .height = 41}},
}};
// Three, and the only entry with more than one: an atlas printed as
// three maps across a two-page spread. They are three *pictures* and not
// one in pieces -- the reader turns a page between them -- which is the
// other way art differs from the fragments above, whose pieces are
// joined into one text.
//
// Each map's own title is inside its rectangle, and so are the labels
// printed outside the first one's frame: they are the drawing's words,
// not the entry's, and measured out of it they arrived in the prose as
// the first map's title, between the two halves of the caption (#357).
constexpr std::array<journal_fragment, 3> art37{{
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 55, .top = 100, .width = 577, .height = 359}},
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 55, .top = 500, .width = 579, .height = 421}},
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 708, .top = 103, .width = 584, .height = 814}},
}};
constexpr std::array<journal_fragment, 1> art41{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 366, .top = 418, .width = 254, .height = 253}},
}};
// Measured again from a band below the caption, which is the rule
// (section 11.1) and was not what the first measurement did: rows 748 to
// 752 of this band are the descenders of `drawing.` on the caption line,
// eighteen rows above the sketch's own first ink, and a rectangle that
// began there both carried a strip of somebody's prose and left the
// entry's caption nowhere to be measured.
constexpr std::array<journal_fragment, 1> art42{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 95, .top = 771, .width = 502, .height = 176}},
}};
constexpr std::array<journal_fragment, 1> art58{{
    {.page = 16,
     .offset = scan16_at,
     .length = scan16_bytes,
     .image = spread,
     .region = {.left = 1011, .top = 249, .width = 269, .height = 268}},
}};

constexpr std::array<journal_entry_fact, 99> archive_entries{{
    {.kind = journal_kind::entry, .number = 1, .fragments = entry01},
    {.kind = journal_kind::entry, .number = 2, .fragments = entry02},
    {.kind = journal_kind::entry, .number = 3, .fragments = entry03},
    {.kind = journal_kind::entry,
     .number = 4,
     .fragments = entry04,
     .art = art04},
    {.kind = journal_kind::entry, .number = 5, .fragments = entry05},
    {.kind = journal_kind::entry, .number = 6, .fragments = entry06},
    {.kind = journal_kind::entry, .number = 7, .fragments = entry07},
    {.kind = journal_kind::entry, .number = 8, .fragments = entry08},
    {.kind = journal_kind::entry, .number = 9, .fragments = entry09},
    {.kind = journal_kind::entry,
     .number = 10,
     .fragments = entry10,
     .art = art10},
    {.kind = journal_kind::entry, .number = 11, .fragments = entry11},
    {.kind = journal_kind::entry, .number = 12, .fragments = entry12},
    {.kind = journal_kind::entry, .number = 13, .fragments = entry13},
    {.kind = journal_kind::entry, .number = 14, .fragments = entry14},
    {.kind = journal_kind::entry,
     .number = 15,
     .fragments = entry15,
     .art = art15},
    {.kind = journal_kind::entry, .number = 16, .fragments = entry16},
    {.kind = journal_kind::entry, .number = 17, .fragments = entry17},
    {.kind = journal_kind::entry, .number = 18, .fragments = entry18},
    {.kind = journal_kind::entry, .number = 19, .fragments = entry19},
    {.kind = journal_kind::entry, .number = 20, .fragments = entry20},
    {.kind = journal_kind::entry, .number = 21, .fragments = entry21},
    {.kind = journal_kind::entry,
     .number = 22,
     .fragments = entry22,
     .art = art22},
    {.kind = journal_kind::entry, .number = 23, .fragments = entry23},
    {.kind = journal_kind::entry, .number = 24, .fragments = entry24},
    {.kind = journal_kind::entry, .number = 25, .fragments = entry25},
    {.kind = journal_kind::entry,
     .number = 26,
     .fragments = entry26,
     .art = art26},
    {.kind = journal_kind::entry, .number = 27, .fragments = entry27},
    {.kind = journal_kind::entry,
     .number = 28,
     .fragments = entry28,
     .art = art28},
    {.kind = journal_kind::entry,
     .number = 29,
     .fragments = entry29,
     .art = art29},
    {.kind = journal_kind::entry, .number = 30, .fragments = entry30},
    {.kind = journal_kind::entry, .number = 31, .fragments = entry31},
    {.kind = journal_kind::entry, .number = 32, .fragments = entry32},
    {.kind = journal_kind::entry, .number = 33, .fragments = entry33},
    {.kind = journal_kind::entry, .number = 34, .fragments = entry34},
    {.kind = journal_kind::entry,
     .number = 35,
     .fragments = entry35,
     .art = art35},
    {.kind = journal_kind::entry, .number = 36, .fragments = entry36},
    {.kind = journal_kind::entry,
     .number = 37,
     .fragments = entry37,
     .art = art37},
    {.kind = journal_kind::entry, .number = 38, .fragments = entry38},
    {.kind = journal_kind::entry, .number = 39, .fragments = entry39},
    {.kind = journal_kind::entry, .number = 40, .fragments = entry40},
    {.kind = journal_kind::entry,
     .number = 41,
     .fragments = entry41,
     .art = art41},
    {.kind = journal_kind::entry,
     .number = 42,
     .fragments = entry42,
     .art = art42},
    {.kind = journal_kind::entry, .number = 43, .fragments = entry43},
    {.kind = journal_kind::entry, .number = 44, .fragments = entry44},
    {.kind = journal_kind::entry, .number = 45, .fragments = entry45},
    {.kind = journal_kind::entry, .number = 46, .fragments = entry46},
    {.kind = journal_kind::entry, .number = 47, .fragments = entry47},
    {.kind = journal_kind::entry, .number = 48, .fragments = entry48},
    {.kind = journal_kind::entry, .number = 49, .fragments = entry49},
    {.kind = journal_kind::entry, .number = 50, .fragments = entry50},
    {.kind = journal_kind::entry, .number = 51, .fragments = entry51},
    {.kind = journal_kind::entry, .number = 52, .fragments = entry52},
    {.kind = journal_kind::entry, .number = 53, .fragments = entry53},
    {.kind = journal_kind::entry, .number = 54, .fragments = entry54},
    {.kind = journal_kind::entry, .number = 55, .fragments = entry55},
    {.kind = journal_kind::entry, .number = 56, .fragments = entry56},
    {.kind = journal_kind::entry, .number = 57, .fragments = entry57},
    {.kind = journal_kind::entry,
     .number = 58,
     .fragments = entry58,
     .art = art58},
    {.kind = journal_kind::tale, .number = 1, .fragments = tale01},
    {.kind = journal_kind::tale, .number = 2, .fragments = tale02},
    {.kind = journal_kind::tale, .number = 3, .fragments = tale03},
    {.kind = journal_kind::tale, .number = 4, .fragments = tale04},
    {.kind = journal_kind::tale, .number = 5, .fragments = tale05},
    {.kind = journal_kind::tale, .number = 6, .fragments = tale06},
    {.kind = journal_kind::tale, .number = 7, .fragments = tale07},
    {.kind = journal_kind::tale, .number = 8, .fragments = tale08},
    {.kind = journal_kind::tale, .number = 9, .fragments = tale09},
    {.kind = journal_kind::tale, .number = 10, .fragments = tale10},
    {.kind = journal_kind::tale, .number = 11, .fragments = tale11},
    {.kind = journal_kind::tale, .number = 12, .fragments = tale12},
    {.kind = journal_kind::tale, .number = 13, .fragments = tale13},
    {.kind = journal_kind::tale, .number = 14, .fragments = tale14},
    {.kind = journal_kind::tale, .number = 15, .fragments = tale15},
    {.kind = journal_kind::tale, .number = 16, .fragments = tale16},
    {.kind = journal_kind::tale, .number = 17, .fragments = tale17},
    {.kind = journal_kind::tale, .number = 18, .fragments = tale18},
    {.kind = journal_kind::tale, .number = 19, .fragments = tale19},
    {.kind = journal_kind::tale, .number = 20, .fragments = tale20},
    {.kind = journal_kind::tale, .number = 21, .fragments = tale21},
    {.kind = journal_kind::tale, .number = 22, .fragments = tale22},
    {.kind = journal_kind::tale, .number = 23, .fragments = tale23},
    {.kind = journal_kind::proclamation, .number = 59, .fragments = procl059},
    {.kind = journal_kind::proclamation, .number = 64, .fragments = procl064},
    {.kind = journal_kind::proclamation, .number = 78, .fragments = procl078},
    {.kind = journal_kind::proclamation, .number = 101, .fragments = procl101},
    {.kind = journal_kind::proclamation, .number = 109, .fragments = procl109},
    {.kind = journal_kind::proclamation, .number = 110, .fragments = procl110},
    {.kind = journal_kind::proclamation, .number = 114, .fragments = procl114},
    {.kind = journal_kind::proclamation, .number = 120, .fragments = procl120},
    {.kind = journal_kind::proclamation, .number = 126, .fragments = procl126},
    {.kind = journal_kind::proclamation, .number = 129, .fragments = procl129},
    {.kind = journal_kind::proclamation, .number = 134, .fragments = procl134},
    {.kind = journal_kind::proclamation, .number = 154, .fragments = procl154},
    {.kind = journal_kind::proclamation, .number = 156, .fragments = procl156},
    {.kind = journal_kind::proclamation, .number = 170, .fragments = procl170},
    {.kind = journal_kind::proclamation, .number = 190, .fragments = procl190},
    {.kind = journal_kind::proclamation, .number = 201, .fragments = procl201},
    {.kind = journal_kind::proclamation, .number = 204, .fragments = procl204},
    {.kind = journal_kind::proclamation, .number = 214, .fragments = procl214},
}};

/// The Adventurer's Journal as the Steam release ships it (#398): **the
/// same ninety-nine items, typeset again as text** rather than scanned.
/// Its pages are two printed pages to a sheet, two columns to a printed
/// page, like the scans above; the words are character codes in each
/// page's content stream, in one font whose `/ToUnicode` map is the only
/// fact needed to read them (`journal_text.h`).
///
/// **Measured, never transcribed**, by the rule every row in this file
/// keeps: offsets, lengths, decoded sizes, boxes in points, and a SHA-256
/// per item of the text this build reads out of those boxes. No word of
/// the document is here.
///
/// How the boxes were found, because the next person needs to know
/// whether to trust them. Every run of the eleven pages was placed by its
/// origin and cut into the four column bands by the column margins
/// (19.84, 167.24, 340.16 and 487.56 points). An item runs from its
/// heading to the next heading in reading order: `Journal Entry N:`,
/// `Proclamation` and a numeral, `Tale N:`, each the whole of its line or
/// the start of it. **Matter outside the grid ends an item's run in that
/// column**, as it does for the scans (`docs/journal.md` §3): a section's
/// title and introduction, the map legend's note under entry 6, and the
/// atlas's three map titles, which are set in another font and in another
/// size. A box's top and bottom are the midpoints to the nearest line
/// outside the item, and no line of anything else is inside one.
///
/// What came out: fifty-eight entries, twenty-three tales and eighteen
/// proclamations — the archive edition's ninety-nine items, by the same
/// numbers — in a hundred and fifteen pieces; every section ascends in
/// reading order and every item begins with its own heading.
///
/// **The pictures** are the archive edition's fourteen, on the same
/// entries: each is an image XObject placed inside its entry's column, or
/// across the page under it (entry 42), and each rectangle is the part of
/// the image the page's clip shows. Entry 37's first two maps are one
/// image here, cut at the blank band between them; the three map titles
/// are type set above the images and so are in no picture.
constexpr std::array<journal_text_font, 1> steam_fonts{{
    {.resource = "T1_0", .offset = 15555, .length = 609, .decoded = 1335},
}};
constexpr std::array<journal_text_page, 11> steam_pages{{
    {.page = 8,
     .offset = 570438,
     .length = 5456,
     .decoded = 31945,
     .fonts = steam_fonts},
    {.page = 9,
     .offset = 576881,
     .length = 5651,
     .decoded = 31244,
     .fonts = steam_fonts},
    {.page = 10,
     .offset = 583571,
     .length = 5836,
     .decoded = 29910,
     .fonts = steam_fonts},
    {.page = 11,
     .offset = 899622,
     .length = 6352,
     .decoded = 33291,
     .fonts = steam_fonts},
    {.page = 12,
     .offset = 1038729,
     .length = 6295,
     .decoded = 32350,
     .fonts = steam_fonts},
    {.page = 13,
     .offset = 1398956,
     .length = 6384,
     .decoded = 33300,
     .fonts = steam_fonts},
    {.page = 14,
     .offset = 1516500,
     .length = 549,
     .decoded = 1404,
     .fonts = steam_fonts},
    {.page = 15,
     .offset = 2986009,
     .length = 5891,
     .decoded = 30833,
     .fonts = steam_fonts},
    {.page = 16,
     .offset = 3402647,
     .length = 7123,
     .decoded = 37946,
     .fonts = steam_fonts},
    {.page = 17,
     .offset = 3410788,
     .length = 4807,
     .decoded = 24882,
     .fonts = steam_fonts},
    {.page = 18,
     .offset = 3588833,
     .length = 4822,
     .decoded = 23794,
     .fonts = steam_fonts},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation059{{
    {.page = 8, .box = {.left = 19, .bottom = 239, .right = 166, .top = 352}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation064{{
    {.page = 8, .box = {.left = 19, .bottom = 140, .right = 166, .top = 238}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation078{{
    {.page = 8, .box = {.left = 19, .bottom = 52, .right = 166, .top = 139}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation101{{
    {.page = 8, .box = {.left = 167, .bottom = 239, .right = 339, .top = 378}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation109{{
    {.page = 8, .box = {.left = 167, .bottom = 159, .right = 339, .top = 238}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation110{{
    {.page = 8, .box = {.left = 167, .bottom = 72, .right = 339, .top = 158}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation114{{
    {.page = 8, .box = {.left = 340, .bottom = 356, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation120{{
    {.page = 8, .box = {.left = 340, .bottom = 237, .right = 486, .top = 355}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation126{{
    {.page = 8, .box = {.left = 340, .bottom = 157, .right = 486, .top = 236}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation129{{
    {.page = 8, .box = {.left = 340, .bottom = 49, .right = 486, .top = 156}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation134{{
    {.page = 8, .box = {.left = 487, .bottom = 356, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation154{{
    {.page = 8, .box = {.left = 487, .bottom = 237, .right = 640, .top = 355}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation156{{
    {.page = 8, .box = {.left = 487, .bottom = 117, .right = 640, .top = 236}},
}};
constexpr std::array<journal_text_fragment, 2> steam_proclamation170{{
    {.page = 8, .box = {.left = 487, .bottom = 40, .right = 640, .top = 116}},
    {.page = 9, .box = {.left = 19, .bottom = 407, .right = 166, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation190{{
    {.page = 9, .box = {.left = 19, .bottom = 269, .right = 166, .top = 406}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation201{{
    {.page = 9, .box = {.left = 19, .bottom = 152, .right = 166, .top = 268}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation204{{
    {.page = 9, .box = {.left = 167, .bottom = 326, .right = 339, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_proclamation214{{
    {.page = 9, .box = {.left = 167, .bottom = 219, .right = 339, .top = 325}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry001{{
    {.page = 9, .box = {.left = 340, .bottom = 157, .right = 486, .top = 359}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry002{{
    {.page = 9, .box = {.left = 340, .bottom = 20, .right = 486, .top = 156}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry003{{
    {.page = 9, .box = {.left = 487, .bottom = 22, .right = 640, .top = 382}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry004{{
    {.page = 10, .box = {.left = 19, .bottom = 364, .right = 166, .top = 461}},
}};
constexpr std::array<journal_fragment, 1> steam_entry004_art{{
    {.page = 10,
     .offset = 762137,
     .length = 136467,
     .image = {.width = 561,
               .height = 558,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 0, .width = 561, .height = 558}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry005{{
    {.page = 10, .box = {.left = 19, .bottom = 166, .right = 166, .top = 363}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry006{{
    {.page = 10, .box = {.left = 19, .bottom = 111, .right = 166, .top = 165}},
    {.page = 10, .box = {.left = 167, .bottom = 358, .right = 339, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry007{{
    {.page = 10, .box = {.left = 167, .bottom = 114, .right = 339, .top = 357}},
    {.page = 10, .box = {.left = 340, .bottom = 270, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry008{{
    {.page = 10, .box = {.left = 340, .bottom = 122, .right = 486, .top = 269}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry009{{
    {.page = 10, .box = {.left = 340, .bottom = 24, .right = 486, .top = 121}},
    {.page = 10, .box = {.left = 487, .bottom = 368, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry010{{
    {.page = 10, .box = {.left = 487, .bottom = 269, .right = 640, .top = 367}},
}};
constexpr std::array<journal_fragment, 1> steam_entry010_art{{
    {.page = 10,
     .offset = 591323,
     .length = 115464,
     .image = {.width = 537,
               .height = 455,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::flate},
     .region = {.left = 2, .top = 5, .width = 535, .height = 450}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry011{{
    {.page = 10, .box = {.left = 487, .bottom = 24, .right = 640, .top = 268}},
    {.page = 11, .box = {.left = 19, .bottom = 407, .right = 166, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry012{{
    {.page = 11, .box = {.left = 19, .bottom = 308, .right = 166, .top = 406}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry013{{
    {.page = 11, .box = {.left = 19, .bottom = 54, .right = 166, .top = 307}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry014{{
    {.page = 11, .box = {.left = 167, .bottom = 246, .right = 339, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry015{{
    {.page = 11, .box = {.left = 167, .bottom = 152, .right = 339, .top = 245}},
}};
constexpr std::array<journal_fragment, 1> steam_entry015_art{{
    {.page = 11,
     .offset = 907832,
     .length = 129857,
     .image = {.width = 532,
               .height = 552,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 3, .width = 532, .height = 546}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry016{{
    {.page = 11, .box = {.left = 167, .bottom = 30, .right = 339, .top = 151}},
    {.page = 11, .box = {.left = 340, .bottom = 290, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry017{{
    {.page = 11, .box = {.left = 340, .bottom = 161, .right = 486, .top = 289}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry018{{
    {.page = 11, .box = {.left = 340, .bottom = 24, .right = 486, .top = 160}},
    {.page = 11, .box = {.left = 487, .bottom = 388, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry019{{
    {.page = 11, .box = {.left = 487, .bottom = 269, .right = 640, .top = 387}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry020{{
    {.page = 11, .box = {.left = 487, .bottom = 44, .right = 640, .top = 268}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry021{{
    {.page = 12, .box = {.left = 19, .bottom = 227, .right = 166, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry022{{
    {.page = 12, .box = {.left = 19, .bottom = 142, .right = 166, .top = 226}},
}};
constexpr std::array<journal_fragment, 1> steam_entry022_art{{
    {.page = 12,
     .offset = 1046882,
     .length = 107151,
     .image = {.width = 571,
               .height = 361,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 4, .top = 4, .width = 567, .height = 353}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry023{{
    {.page = 12, .box = {.left = 19, .bottom = 30, .right = 166, .top = 141}},
    {.page = 12, .box = {.left = 167, .bottom = 349, .right = 339, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry024{{
    {.page = 12, .box = {.left = 167, .bottom = 73, .right = 339, .top = 348}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry025{{
    {.page = 12, .box = {.left = 167, .bottom = 24, .right = 339, .top = 72}},
    {.page = 12, .box = {.left = 340, .bottom = 241, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry026{{
    {.page = 12, .box = {.left = 340, .bottom = 26, .right = 486, .top = 240}},
    {.page = 12, .box = {.left = 487, .bottom = 339, .right = 640, .top = 463}},
}};
constexpr std::array<journal_fragment, 1> steam_entry026_art{{
    {.page = 12,
     .offset = 1155949,
     .length = 179137,
     .image = {.width = 549,
               .height = 461,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::flate},
     .region = {.left = 0, .top = 0, .width = 549, .height = 459}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry027{{
    {.page = 12, .box = {.left = 487, .bottom = 132, .right = 640, .top = 338}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry028{{
    {.page = 12, .box = {.left = 487, .bottom = 103, .right = 640, .top = 131}},
}};
constexpr std::array<journal_fragment, 1> steam_entry028_art{{
    {.page = 12,
     .offset = 1336943,
     .length = 60984,
     .image = {.width = 561,
               .height = 298,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 0, .width = 561, .height = 296}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry029{{
    {.page = 13, .box = {.left = 19, .bottom = 369, .right = 166, .top = 461}},
}};
constexpr std::array<journal_fragment, 1> steam_entry029_art{{
    {.page = 13,
     .offset = 1417808,
     .length = 97650,
     .image = {.width = 522,
               .height = 529,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 0, .width = 522, .height = 529}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry030{{
    {.page = 13, .box = {.left = 19, .bottom = 61, .right = 166, .top = 368}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry031{{
    {.page = 13, .box = {.left = 167, .bottom = 246, .right = 339, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry032{{
    {.page = 13, .box = {.left = 167, .bottom = 90, .right = 339, .top = 245}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry033{{
    {.page = 13, .box = {.left = 340, .bottom = 305, .right = 486, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry034{{
    {.page = 13, .box = {.left = 340, .bottom = 166, .right = 486, .top = 304}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry035{{
    {.page = 13, .box = {.left = 340, .bottom = 30, .right = 486, .top = 165}},
    {.page = 13, .box = {.left = 487, .bottom = 358, .right = 640, .top = 463}},
}};
constexpr std::array<journal_fragment, 1> steam_entry035_art{{
    {.page = 13,
     .offset = 1407195,
     .length = 8756,
     .image = {.width = 471,
               .height = 76,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 0, .width = 471, .height = 76}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry036{{
    {.page = 13, .box = {.left = 487, .bottom = 65, .right = 640, .top = 357}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry037{{
    {.page = 14, .box = {.left = 19, .bottom = 431, .right = 166, .top = 463}},
}};
constexpr std::array<journal_fragment, 3> steam_entry037_art{{
    {.page = 14,
     .offset = 1518909,
     .length = 819641,
     .image = {.width = 1213,
               .height = 1642,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 5, .width = 1213, .height = 694}},
    {.page = 14,
     .offset = 1518909,
     .length = 819641,
     .image = {.width = 1213,
               .height = 1642,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 815, .width = 1213, .height = 813}},
    {.page = 14,
     .offset = 2340410,
     .length = 644570,
     .image = {.width = 1197,
               .height = 1626,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 3, .top = 5, .width = 1190, .height = 1621}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry038{{
    {.page = 15, .box = {.left = 19, .bottom = 246, .right = 166, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry039{{
    {.page = 15, .box = {.left = 19, .bottom = 129, .right = 166, .top = 245}},
    {.page = 15, .box = {.left = 167, .bottom = 388, .right = 339, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry040{{
    {.page = 15, .box = {.left = 167, .bottom = 308, .right = 339, .top = 387}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry041{{
    {.page = 15, .box = {.left = 167, .bottom = 213, .right = 339, .top = 307}},
}};
constexpr std::array<journal_fragment, 1> steam_entry041_art{{
    {.page = 15,
     .offset = 3087968,
     .length = 313691,
     .image = {.width = 533,
               .height = 540,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::flate},
     .region = {.left = 0, .top = 5, .width = 533, .height = 530}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry042{{
    {.page = 15, .box = {.left = 167, .bottom = 121, .right = 339, .top = 212}},
}};
constexpr std::array<journal_fragment, 1> steam_entry042_art{{
    {.page = 15,
     .offset = 2993758,
     .length = 92294,
     .image = {.width = 1072,
               .height = 381,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 0, .width = 1072, .height = 381}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry043{{
    {.page = 15, .box = {.left = 340, .bottom = 23, .right = 486, .top = 461}},
    {.page = 15, .box = {.left = 487, .bottom = 388, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry044{{
    {.page = 15, .box = {.left = 487, .bottom = 46, .right = 640, .top = 387}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry045{{
    {.page = 16, .box = {.left = 19, .bottom = 23, .right = 166, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry046{{
    {.page = 16, .box = {.left = 167, .bottom = 119, .right = 339, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry047{{
    {.page = 16, .box = {.left = 167, .bottom = 31, .right = 339, .top = 118}},
    {.page = 16, .box = {.left = 340, .bottom = 368, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry048{{
    {.page = 16, .box = {.left = 340, .bottom = 132, .right = 486, .top = 367}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry049{{
    {.page = 16, .box = {.left = 340, .bottom = 24, .right = 486, .top = 131}},
    {.page = 16, .box = {.left = 487, .bottom = 349, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry050{{
    {.page = 16, .box = {.left = 487, .bottom = 85, .right = 640, .top = 348}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry051{{
    {.page = 17, .box = {.left = 19, .bottom = 237, .right = 166, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry052{{
    {.page = 17, .box = {.left = 19, .bottom = 90, .right = 166, .top = 236}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry053{{
    {.page = 17, .box = {.left = 167, .bottom = 364, .right = 339, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry054{{
    {.page = 17, .box = {.left = 167, .bottom = 149, .right = 339, .top = 363}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry055{{
    {.page = 17, .box = {.left = 340, .bottom = 305, .right = 486, .top = 461}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry056{{
    {.page = 17, .box = {.left = 340, .bottom = 215, .right = 486, .top = 304}},
}};
constexpr std::array<journal_text_fragment, 2> steam_entry057{{
    {.page = 17, .box = {.left = 340, .bottom = 108, .right = 486, .top = 214}},
    {.page = 17, .box = {.left = 487, .bottom = 358, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_entry058{{
    {.page = 17, .box = {.left = 487, .bottom = 330, .right = 640, .top = 357}},
}};
constexpr std::array<journal_fragment, 1> steam_entry058_art{{
    {.page = 17,
     .offset = 3417453,
     .length = 170392,
     .image = {.width = 557,
               .height = 556,
               .bits_per_component = 8,
               .components = 3,
               .filter = journal_filter::dct},
     .region = {.left = 0, .top = 5, .width = 557, .height = 546}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale001{{
    {.page = 18, .box = {.left = 19, .bottom = 240, .right = 166, .top = 277}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale002{{
    {.page = 18, .box = {.left = 19, .bottom = 202, .right = 166, .top = 239}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale003{{
    {.page = 18, .box = {.left = 19, .bottom = 164, .right = 166, .top = 201}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale004{{
    {.page = 18, .box = {.left = 19, .bottom = 116, .right = 166, .top = 163}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale005{{
    {.page = 18, .box = {.left = 19, .bottom = 81, .right = 166, .top = 115}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale006{{
    {.page = 18, .box = {.left = 167, .bottom = 345, .right = 339, .top = 408}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale007{{
    {.page = 18, .box = {.left = 167, .bottom = 307, .right = 339, .top = 344}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale008{{
    {.page = 18, .box = {.left = 167, .bottom = 279, .right = 339, .top = 306}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale009{{
    {.page = 18, .box = {.left = 167, .bottom = 221, .right = 339, .top = 278}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale010{{
    {.page = 18, .box = {.left = 167, .bottom = 184, .right = 339, .top = 220}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale011{{
    {.page = 18, .box = {.left = 167, .bottom = 148, .right = 339, .top = 183}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale012{{
    {.page = 18, .box = {.left = 340, .bottom = 417, .right = 486, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale013{{
    {.page = 18, .box = {.left = 340, .bottom = 379, .right = 486, .top = 416}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale014{{
    {.page = 18, .box = {.left = 340, .bottom = 341, .right = 486, .top = 378}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale015{{
    {.page = 18, .box = {.left = 340, .bottom = 294, .right = 486, .top = 340}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale016{{
    {.page = 18, .box = {.left = 340, .bottom = 256, .right = 486, .top = 293}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale017{{
    {.page = 18, .box = {.left = 340, .bottom = 208, .right = 486, .top = 255}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale018{{
    {.page = 18, .box = {.left = 340, .bottom = 151, .right = 486, .top = 207}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale019{{
    {.page = 18, .box = {.left = 340, .bottom = 105, .right = 486, .top = 150}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale020{{
    {.page = 18, .box = {.left = 487, .bottom = 407, .right = 640, .top = 463}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale021{{
    {.page = 18, .box = {.left = 487, .bottom = 360, .right = 640, .top = 406}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale022{{
    {.page = 18, .box = {.left = 487, .bottom = 302, .right = 640, .top = 359}},
}};
constexpr std::array<journal_text_fragment, 1> steam_tale023{{
    {.page = 18, .box = {.left = 487, .bottom = 257, .right = 640, .top = 301}},
}};
constexpr std::array<journal_entry_fact, 99> steam_entries{{
    {.kind = journal_kind::proclamation,
     .number = 59,
     .art = {},
     .text = steam_proclamation059,
     .text_sha256 =
         "8aac1c6502f2f8e9c46fac5001dbbcfd542f283c20e392a14d69c63edd306c24"},
    {.kind = journal_kind::proclamation,
     .number = 64,
     .art = {},
     .text = steam_proclamation064,
     .text_sha256 =
         "70bdaf2b484d537df8422451da246bcbe45280fa84e52fc41d3b25c2247f6a83"},
    {.kind = journal_kind::proclamation,
     .number = 78,
     .art = {},
     .text = steam_proclamation078,
     .text_sha256 =
         "60a7a261d4089eb669e1afc8dad2fda84889511e27adf1a9852e0b4c26deb5eb"},
    {.kind = journal_kind::proclamation,
     .number = 101,
     .art = {},
     .text = steam_proclamation101,
     .text_sha256 =
         "e716ea58b66e644ee2576cf5889b562951e512d3637e1fa7e1b01c5b62dd18fe"},
    {.kind = journal_kind::proclamation,
     .number = 109,
     .art = {},
     .text = steam_proclamation109,
     .text_sha256 =
         "5178f1940d82b869011afcbeb76afa6c9911ae0af00c448cbf3dfbcbbbc7e214"},
    {.kind = journal_kind::proclamation,
     .number = 110,
     .art = {},
     .text = steam_proclamation110,
     .text_sha256 =
         "2f770e9de80342b48af6cecd151185850215273d659d5c3accfd71df6f814d96"},
    {.kind = journal_kind::proclamation,
     .number = 114,
     .art = {},
     .text = steam_proclamation114,
     .text_sha256 =
         "601d15798bc322a36dced22473c68e59fe95cf153fbb078f3cb9ec6b3c562f29"},
    {.kind = journal_kind::proclamation,
     .number = 120,
     .art = {},
     .text = steam_proclamation120,
     .text_sha256 =
         "7873d72756263b6b79e91d3cce760096d3a1b501a9d91e349c8b6304e1120726"},
    {.kind = journal_kind::proclamation,
     .number = 126,
     .art = {},
     .text = steam_proclamation126,
     .text_sha256 =
         "da8da89f8525bc2fc3d6873b2c337a6b72aceded5b543c2c3ae831e37cb158e2"},
    {.kind = journal_kind::proclamation,
     .number = 129,
     .art = {},
     .text = steam_proclamation129,
     .text_sha256 =
         "499b9b756cb2aeccf8702ae451e2d4a11f3324afe94d47c592034d26c8f14c53"},
    {.kind = journal_kind::proclamation,
     .number = 134,
     .art = {},
     .text = steam_proclamation134,
     .text_sha256 =
         "dd6710646dd145916ccd1f0ba7a6bee2678dc6b2f089b8d48869406cc0845dfa"},
    {.kind = journal_kind::proclamation,
     .number = 154,
     .art = {},
     .text = steam_proclamation154,
     .text_sha256 =
         "8ba9dad4c84b8d4ddf6745398022c80fbfe27491fda801d263f5fcb48ba51688"},
    {.kind = journal_kind::proclamation,
     .number = 156,
     .art = {},
     .text = steam_proclamation156,
     .text_sha256 =
         "0b2dcdd8aa9c5d52b4cd25aab994ee85f68d85544b4e458783cd4776ac2ee182"},
    {.kind = journal_kind::proclamation,
     .number = 170,
     .art = {},
     .text = steam_proclamation170,
     .text_sha256 =
         "099f3f2dd99b543ba444bf2edaf29c7798f8fb21eac10a0526c2c9504d6f5df7"},
    {.kind = journal_kind::proclamation,
     .number = 190,
     .art = {},
     .text = steam_proclamation190,
     .text_sha256 =
         "ec7bfe423239586c55ea8d20f0edc519975be2b5c97f042e01957ad0eb69ca78"},
    {.kind = journal_kind::proclamation,
     .number = 201,
     .art = {},
     .text = steam_proclamation201,
     .text_sha256 =
         "d5aa7aafc5590e7d00d83839e77f4b6d41edc2770eb5360a319913f38786bf57"},
    {.kind = journal_kind::proclamation,
     .number = 204,
     .art = {},
     .text = steam_proclamation204,
     .text_sha256 =
         "60b65cdf8f3da60024ae86e07d956eaf59c786b227e9514903a19a0560dd5142"},
    {.kind = journal_kind::proclamation,
     .number = 214,
     .art = {},
     .text = steam_proclamation214,
     .text_sha256 =
         "21c3ab76af96d9bea8f86b0fbd42beafd52b5e7ce55849cb4f1579d7a7a117fe"},
    {.kind = journal_kind::entry,
     .number = 1,
     .art = {},
     .text = steam_entry001,
     .text_sha256 =
         "a5706c12d653738f5748f08e91c4cdb21fd70a4c4c3fa04ac9bf50d882978fe9"},
    {.kind = journal_kind::entry,
     .number = 2,
     .art = {},
     .text = steam_entry002,
     .text_sha256 =
         "b7dfb14070bb075486f116d6a2307498b755815835357ea493bf78ceb0423edd"},
    {.kind = journal_kind::entry,
     .number = 3,
     .art = {},
     .text = steam_entry003,
     .text_sha256 =
         "5a85a9cb57e42a234dda90c46d841e6723d632e87d980652f601dc2ba2b30de4"},
    {.kind = journal_kind::entry,
     .number = 4,
     .art = steam_entry004_art,
     .text = steam_entry004,
     .text_sha256 =
         "6e3b89f56a87aca876fe84ff55603beca6c52f944760dcf2b892f596e6f2867f"},
    {.kind = journal_kind::entry,
     .number = 5,
     .art = {},
     .text = steam_entry005,
     .text_sha256 =
         "8464933f831c2309eda415ae95dd214c18d85eef3ac659861eb447772c4f9c48"},
    {.kind = journal_kind::entry,
     .number = 6,
     .art = {},
     .text = steam_entry006,
     .text_sha256 =
         "529265903d64e1878e70954e484c4e74f584e8834a1a53c6d303fca295dd4f98"},
    {.kind = journal_kind::entry,
     .number = 7,
     .art = {},
     .text = steam_entry007,
     .text_sha256 =
         "5f4b0ca392fa659ebecbce5fe4863254ab4b93226fea6302cf69267fe9bcb549"},
    {.kind = journal_kind::entry,
     .number = 8,
     .art = {},
     .text = steam_entry008,
     .text_sha256 =
         "38cb207bfe6bdf5bdb5b699297d41ab542313d1170723fb3800bba22df084c8e"},
    {.kind = journal_kind::entry,
     .number = 9,
     .art = {},
     .text = steam_entry009,
     .text_sha256 =
         "70132a04d88c71fa6cc94decd3c2244d21b393c38b3ed49da7e258303ad8399a"},
    {.kind = journal_kind::entry,
     .number = 10,
     .art = steam_entry010_art,
     .text = steam_entry010,
     .text_sha256 =
         "bc30899093c9878aa22493d0581653f7a823d1260fe2cf0917beb9d0e56a7236"},
    {.kind = journal_kind::entry,
     .number = 11,
     .art = {},
     .text = steam_entry011,
     .text_sha256 =
         "d8cca90f5a1167a896dffd0814dea9985f9e84f0bc2c4349f53c407b0d03e7f4"},
    {.kind = journal_kind::entry,
     .number = 12,
     .art = {},
     .text = steam_entry012,
     .text_sha256 =
         "0057a81af2c823e695f5b4c186acef738a4309166f2edf28b3eb8df2aaaf3208"},
    {.kind = journal_kind::entry,
     .number = 13,
     .art = {},
     .text = steam_entry013,
     .text_sha256 =
         "606761492842a37c47926c16343b0a98495a200db39f5f3f47dea464519aa2f6"},
    {.kind = journal_kind::entry,
     .number = 14,
     .art = {},
     .text = steam_entry014,
     .text_sha256 =
         "e8a88ebd6bb289d9eba22e8eb220f1086141afb6a603c07c5287a7ee6318415e"},
    {.kind = journal_kind::entry,
     .number = 15,
     .art = steam_entry015_art,
     .text = steam_entry015,
     .text_sha256 =
         "5f4bbaf8e7fc022eb53887774a322e00a3a438fb0caa798c432a39907049fae5"},
    {.kind = journal_kind::entry,
     .number = 16,
     .art = {},
     .text = steam_entry016,
     .text_sha256 =
         "b0e6bfde0eba2f9437aaa03c5c9684c1090c0b0bec752cc7c93bea994cca0dfa"},
    {.kind = journal_kind::entry,
     .number = 17,
     .art = {},
     .text = steam_entry017,
     .text_sha256 =
         "d9f871dcbcbe764b5aae978a68214f0ae75a70c67041e4e8b2dc8baabf3cd7a3"},
    {.kind = journal_kind::entry,
     .number = 18,
     .art = {},
     .text = steam_entry018,
     .text_sha256 =
         "958853b27ab3089ff4ec8a0fbfb01f67634c137e5e98ab8957fc712cf7deaf39"},
    {.kind = journal_kind::entry,
     .number = 19,
     .art = {},
     .text = steam_entry019,
     .text_sha256 =
         "3b00a6e21d23ebe92be6724e65cbf0ecc662a86967bc7c586aabef6b16725d46"},
    {.kind = journal_kind::entry,
     .number = 20,
     .art = {},
     .text = steam_entry020,
     .text_sha256 =
         "de31d2f7f7d77c822b6072681384790ba9069e3fe8ff376f8c945c02cf88545b"},
    {.kind = journal_kind::entry,
     .number = 21,
     .art = {},
     .text = steam_entry021,
     .text_sha256 =
         "c0ba8d4111d873c1507d58cfe4d6925f81100272e5597df652215d29a03f10f8"},
    {.kind = journal_kind::entry,
     .number = 22,
     .art = steam_entry022_art,
     .text = steam_entry022,
     .text_sha256 =
         "ad392b7b59a9d09186e76a82f45546524f805c2104776d26f63a9dbcf1d676c2"},
    {.kind = journal_kind::entry,
     .number = 23,
     .art = {},
     .text = steam_entry023,
     .text_sha256 =
         "0d10dc557e9d4f036da1a5fe780986f5a0c44d9ab8d3f050b0745cfdd68cf19f"},
    {.kind = journal_kind::entry,
     .number = 24,
     .art = {},
     .text = steam_entry024,
     .text_sha256 =
         "6397528e841754a6a7ca0040b865b05ffe0fd68e4d01e74fea2fbe7593d649a9"},
    {.kind = journal_kind::entry,
     .number = 25,
     .art = {},
     .text = steam_entry025,
     .text_sha256 =
         "3b63728c0254138013257a5806686d8a4f6630dc2fbfd307d55ad49a7c408a14"},
    {.kind = journal_kind::entry,
     .number = 26,
     .art = steam_entry026_art,
     .text = steam_entry026,
     .text_sha256 =
         "e2dbd07b184f661e45b0a0aa1feb674001d686f0d9b7b299a701873048b9e3bc"},
    {.kind = journal_kind::entry,
     .number = 27,
     .art = {},
     .text = steam_entry027,
     .text_sha256 =
         "3986b5620999ff1d75cc225c3f38407b73de42f7d9bd08892fb235c6f725249e"},
    {.kind = journal_kind::entry,
     .number = 28,
     .art = steam_entry028_art,
     .text = steam_entry028,
     .text_sha256 =
         "a10c7fdd21dd2c381cddfdfa3e8eae132e0fe64973140307f42ed32e592189ee"},
    {.kind = journal_kind::entry,
     .number = 29,
     .art = steam_entry029_art,
     .text = steam_entry029,
     .text_sha256 =
         "d96bc586c832a5aba939a2cfa35b99476c2acc0e1ce29695e70118cfb66716bf"},
    {.kind = journal_kind::entry,
     .number = 30,
     .art = {},
     .text = steam_entry030,
     .text_sha256 =
         "8f3977514702836c2a04d85e218e1915743b0a0ef5fe74fff5af1d429a71f57f"},
    {.kind = journal_kind::entry,
     .number = 31,
     .art = {},
     .text = steam_entry031,
     .text_sha256 =
         "5bb2ff2f5c0f89c1eada6cc281178c1d66d256a1b3507256fa2369ad9f4b2b58"},
    {.kind = journal_kind::entry,
     .number = 32,
     .art = {},
     .text = steam_entry032,
     .text_sha256 =
         "52e588d8c1cede02af62dde7eda488497a3cfd0623337e82c639b43cf136a972"},
    {.kind = journal_kind::entry,
     .number = 33,
     .art = {},
     .text = steam_entry033,
     .text_sha256 =
         "362e7f00fb773f6889649e8ca08831c9f12fdde44b6a1dfab021d4aa56fcf662"},
    {.kind = journal_kind::entry,
     .number = 34,
     .art = {},
     .text = steam_entry034,
     .text_sha256 =
         "e104e3cef853ba243441f1a42c055f71261e4fbde6b85f31c789310da7aa4a7e"},
    {.kind = journal_kind::entry,
     .number = 35,
     .art = steam_entry035_art,
     .text = steam_entry035,
     .text_sha256 =
         "b8949a25d191d5395195c33dba065599a962a51fa6acc423f40cec1f3875ede3"},
    {.kind = journal_kind::entry,
     .number = 36,
     .art = {},
     .text = steam_entry036,
     .text_sha256 =
         "88ed23ecb73df96fce871d138fcd6921c90fa27d766477bccdd3c36928de4853"},
    {.kind = journal_kind::entry,
     .number = 37,
     .art = steam_entry037_art,
     .text = steam_entry037,
     .text_sha256 =
         "c98546b189555a2d46789a70dfdc4373187c135286954fe079d565b8c4960ce5"},
    {.kind = journal_kind::entry,
     .number = 38,
     .art = {},
     .text = steam_entry038,
     .text_sha256 =
         "ba6e25d7feb62d4cc6f2ea710b824c962bb72ea3545f9ea1589bf855e43d85f1"},
    {.kind = journal_kind::entry,
     .number = 39,
     .art = {},
     .text = steam_entry039,
     .text_sha256 =
         "4b77c38f29b59cc465f557a288447a32398274c4bcf603a265eff7486448c342"},
    {.kind = journal_kind::entry,
     .number = 40,
     .art = {},
     .text = steam_entry040,
     .text_sha256 =
         "2e0f6c62faa189365892dd8c93bfee1886b54f05c4596d9ace1c0929dbb6fff6"},
    {.kind = journal_kind::entry,
     .number = 41,
     .art = steam_entry041_art,
     .text = steam_entry041,
     .text_sha256 =
         "8c5f0cd4748b75b1a084e04c5556d0a1b86ceb083464c7aa46b63a475c8e8409"},
    {.kind = journal_kind::entry,
     .number = 42,
     .art = steam_entry042_art,
     .text = steam_entry042,
     .text_sha256 =
         "a003a8801e0e817c4ac9946acb0d896a62447ae558b0c216f33cdb9d47427ace"},
    {.kind = journal_kind::entry,
     .number = 43,
     .art = {},
     .text = steam_entry043,
     .text_sha256 =
         "322f5e5682938dd6a932b1f1e20ac4aec523cac0ca2a53c8f3d825a5e07a6aa0"},
    {.kind = journal_kind::entry,
     .number = 44,
     .art = {},
     .text = steam_entry044,
     .text_sha256 =
         "3af4f3fde3013eca895df8271185ede582fb307c02db1190dd8a8cab40638d1e"},
    {.kind = journal_kind::entry,
     .number = 45,
     .art = {},
     .text = steam_entry045,
     .text_sha256 =
         "14f673a0ed9864c5ca0ac99ce67c62e1d764aa0553d3b17ad800f0eaa7be4951"},
    {.kind = journal_kind::entry,
     .number = 46,
     .art = {},
     .text = steam_entry046,
     .text_sha256 =
         "7962302b8d6f1bd772216f2bb9ec226cd6f5748b4de31e240cc37fb16af2473b"},
    {.kind = journal_kind::entry,
     .number = 47,
     .art = {},
     .text = steam_entry047,
     .text_sha256 =
         "875c1c9a91eb4769c12505c29ec1e9ac70b5dd7f583a27622b01773a77ab3a33"},
    {.kind = journal_kind::entry,
     .number = 48,
     .art = {},
     .text = steam_entry048,
     .text_sha256 =
         "ba5bbdf91aa99a9eec6ad1152b100d11524b21640150e61cb49c6cc40ced606e"},
    {.kind = journal_kind::entry,
     .number = 49,
     .art = {},
     .text = steam_entry049,
     .text_sha256 =
         "b0fa01e447f7c33f28e8499fafa9eecd4b8b140cf85158a142065a5f9a0949f0"},
    {.kind = journal_kind::entry,
     .number = 50,
     .art = {},
     .text = steam_entry050,
     .text_sha256 =
         "011116e98190260438e2579fd6756cb169291baad17e54d8d2aad72b2a21f34a"},
    {.kind = journal_kind::entry,
     .number = 51,
     .art = {},
     .text = steam_entry051,
     .text_sha256 =
         "922ff1a6fd636475f287ab514d42d5e17fc44615e736335cabaea7788cc71189"},
    {.kind = journal_kind::entry,
     .number = 52,
     .art = {},
     .text = steam_entry052,
     .text_sha256 =
         "61084c38bdaa5703dfa50b6e29ca4cffc1fee33e6e5cf99996af94c4c80ed1be"},
    {.kind = journal_kind::entry,
     .number = 53,
     .art = {},
     .text = steam_entry053,
     .text_sha256 =
         "6aa44121868e87b34aa89e959641320a23653b93c87787ad16c925c6a30bf693"},
    {.kind = journal_kind::entry,
     .number = 54,
     .art = {},
     .text = steam_entry054,
     .text_sha256 =
         "36b78defdbc6f3a7ca53f8a4ae4af65689720c46a4cff2ae6ddf93597c23e77b"},
    {.kind = journal_kind::entry,
     .number = 55,
     .art = {},
     .text = steam_entry055,
     .text_sha256 =
         "8892b61d925219a4377309b730ac11eceb451618abeb80734b53513e2ba0914e"},
    {.kind = journal_kind::entry,
     .number = 56,
     .art = {},
     .text = steam_entry056,
     .text_sha256 =
         "66c686174ffce5c03e135b1fd565b7f2f4de1776a3c83213baabca4ff1ab8cf7"},
    {.kind = journal_kind::entry,
     .number = 57,
     .art = {},
     .text = steam_entry057,
     .text_sha256 =
         "ccebcb8f82b76d2e0cf384c03f5b0e296f2e098df997067a0d9b4a6ad5a02271"},
    {.kind = journal_kind::entry,
     .number = 58,
     .art = steam_entry058_art,
     .text = steam_entry058,
     .text_sha256 =
         "b81038a7ab8af419f5280d057ca9f90e3726877dab66228a5880d5a5e138f210"},
    {.kind = journal_kind::tale,
     .number = 1,
     .art = {},
     .text = steam_tale001,
     .text_sha256 =
         "57cdb0843d5301dd076feb0fc3bbba59196dd904df0d95368cc6057321da1c1d"},
    {.kind = journal_kind::tale,
     .number = 2,
     .art = {},
     .text = steam_tale002,
     .text_sha256 =
         "d0a1fa31052bc9b5e21c5a268a60fad6533b54dada15b662c9caf6b7b5d71c83"},
    {.kind = journal_kind::tale,
     .number = 3,
     .art = {},
     .text = steam_tale003,
     .text_sha256 =
         "be5c3304c0c18069595273f07e6e91bc79722451060036a1172fdc2019f9430e"},
    {.kind = journal_kind::tale,
     .number = 4,
     .art = {},
     .text = steam_tale004,
     .text_sha256 =
         "461e4a33c0ca73563329649167a8164c06675f1932b2cc3331830e3035b160d3"},
    {.kind = journal_kind::tale,
     .number = 5,
     .art = {},
     .text = steam_tale005,
     .text_sha256 =
         "0e18124fb28af61bb0de2d8f0685912b2589ee3f21d373a88249e570f9034085"},
    {.kind = journal_kind::tale,
     .number = 6,
     .art = {},
     .text = steam_tale006,
     .text_sha256 =
         "7d863ac8da3956cdbd0e3778f3aceaa15a249fc926352f4694ffd365a92d3906"},
    {.kind = journal_kind::tale,
     .number = 7,
     .art = {},
     .text = steam_tale007,
     .text_sha256 =
         "36f1d6592183911ce7a29d0df93c8d88af2bf1d8d25938e6027075139923ff40"},
    {.kind = journal_kind::tale,
     .number = 8,
     .art = {},
     .text = steam_tale008,
     .text_sha256 =
         "c4f3c7223ff07ee3a4e227c790a82e2b171a2fd967fee3fdc90b003872436d82"},
    {.kind = journal_kind::tale,
     .number = 9,
     .art = {},
     .text = steam_tale009,
     .text_sha256 =
         "43ef20f3d32a2143556112959eeecdf4228b270a3954d1bd25cf4d182edff86f"},
    {.kind = journal_kind::tale,
     .number = 10,
     .art = {},
     .text = steam_tale010,
     .text_sha256 =
         "28b80bc440dff59b1c684b5083aae08083b8d026bbe91898a31804d48d7d53b6"},
    {.kind = journal_kind::tale,
     .number = 11,
     .art = {},
     .text = steam_tale011,
     .text_sha256 =
         "e492dabf3fcd17da239d47a2c38629078ac6e98a235c27fcbd855a65e287af24"},
    {.kind = journal_kind::tale,
     .number = 12,
     .art = {},
     .text = steam_tale012,
     .text_sha256 =
         "2d77dad0f4db61aa3a2bb1ec7436e4a5a2598871203cdfcd28894914128b21a7"},
    {.kind = journal_kind::tale,
     .number = 13,
     .art = {},
     .text = steam_tale013,
     .text_sha256 =
         "e706f0ac7fdc57da0947812aca1166dd501dd3c09851605990577a445650eb50"},
    {.kind = journal_kind::tale,
     .number = 14,
     .art = {},
     .text = steam_tale014,
     .text_sha256 =
         "ebc0e681c3187ff3fdb67d0d1653f08904a2f418068ce7f246d1e82ac4b721e0"},
    {.kind = journal_kind::tale,
     .number = 15,
     .art = {},
     .text = steam_tale015,
     .text_sha256 =
         "df4235b4c5052a56bfb6515b6219f44d7a1abd24b3dc09eb7611f6d1077b1fb7"},
    {.kind = journal_kind::tale,
     .number = 16,
     .art = {},
     .text = steam_tale016,
     .text_sha256 =
         "ae9530e7c9f2ecfee8988ecce65b53d0bafdbe8dfc0ba7d7bdbfa13ee919d410"},
    {.kind = journal_kind::tale,
     .number = 17,
     .art = {},
     .text = steam_tale017,
     .text_sha256 =
         "2b725451f687625c6f32059573f93571a328d2413230321891a503adb776fd56"},
    {.kind = journal_kind::tale,
     .number = 18,
     .art = {},
     .text = steam_tale018,
     .text_sha256 =
         "4ea0e09e6632cb9a8a6fb1d4fc84657cb0b80d6f7b8dc4e1936f420baf6080fd"},
    {.kind = journal_kind::tale,
     .number = 19,
     .art = {},
     .text = steam_tale019,
     .text_sha256 =
         "4e0de4002ce820a176478c96a34ef163fbbf7ac31f659d5f5e62ed219efae8c8"},
    {.kind = journal_kind::tale,
     .number = 20,
     .art = {},
     .text = steam_tale020,
     .text_sha256 =
         "9887fa62fd3b26749716ade413e627fcb36e694d52438f0b7b966179cffe155a"},
    {.kind = journal_kind::tale,
     .number = 21,
     .art = {},
     .text = steam_tale021,
     .text_sha256 =
         "c5805608ffb086790551afa998148b19922c8f09705fe004336c7e88049db4c7"},
    {.kind = journal_kind::tale,
     .number = 22,
     .art = {},
     .text = steam_tale022,
     .text_sha256 =
         "db3f054a0466c306b66e47923736505e6e67e6639eb367cd6be659f5955fb4db"},
    {.kind = journal_kind::tale,
     .number = 23,
     .art = {},
     .text = steam_tale023,
     .text_sha256 =
         "577d8db41a73af2a061365e0f11cd1feabc9dca9488b244da094cf46005a6285"},
}};

/// The editions this build knows the insides of.
///
/// Each fingerprint is the same one `machine::known_documents()` carries
/// for the gate: one artifact seen twice, and the suite checks that the
/// two agree.
constexpr std::array<journal_edition, 2> table{{
    {.fingerprint =
         "67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c",
     .name = "Pool of Radiance Adventurer's Journal, archive release",
     .entries = archive_entries},
    {.fingerprint =
         "a31368c35c527ce3f760ffac0596ab96254c79b22f7e89be219c6238954ac1ee",
     .name = "Pool of Radiance Adventurer's Journal, Steam release",
     .entries = steam_entries,
     .pages = steam_pages},
}};

}  // namespace

const char* journal_filter_name(journal_filter which) noexcept {
  switch (which) {
    case journal_filter::none:
      return "none";
    case journal_filter::flate:
      return "FlateDecode";
    case journal_filter::dct:
      return "DCTDecode";
    case journal_filter::ccitt:
      return "CCITTFaxDecode";
    case journal_filter::jbig2:
      return "JBIG2Decode";
  }
  return "unknown";
}

bool journal_filter_supported(journal_filter which) noexcept {
  // `dct` is here and is **not** decoded (below): its stream goes to the
  // engine as its own bytes (#212). The two fax filters are still refused
  // by name, which is `docs/journal.md` §4's rule rather than an
  // exception to it — a filter is added the day a document in hand asks
  // for it, and neither has.
  return journal_filter_decoded(which) || which == journal_filter::dct;
}

bool journal_filter_decoded(journal_filter which) noexcept {
  return which == journal_filter::none || which == journal_filter::flate;
}

std::span<const journal_edition> known_journals() { return table; }

const journal_edition* find_journal(std::span<const journal_edition> editions,
                                    const sha256_digest& digest) noexcept {
  for (const journal_edition& known : editions) {
    if (machine::digest_is(digest, known.fingerprint)) {
      return &known;
    }
  }
  return nullptr;
}

const journal_edition* find_journal(const sha256_digest& digest) noexcept {
  return find_journal(known_journals(), digest);
}

}  // namespace amberfolio::host
