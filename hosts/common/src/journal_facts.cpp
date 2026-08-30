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

/// The Adventurer's Journal as the currently sold archive release ships
/// it: **all three of its numbered sections** — fifty-eight journal
/// entries, twenty-three tavern tales and eighteen proclamations, in
/// ninety-nine rows and a hundred and twenty pieces, across eleven
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
/// altogether. Eighteen of the fifty-eight are in more than one
/// piece. `journal_fragment` is what that costs and what it buys.
///
/// A piece with no ink in it is not here: an entry that happened to end
/// exactly at the foot of its column would otherwise carry an empty
/// rectangle, and asking an engine to read a blank is asking it for an
/// answer nobody wants.
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
constexpr std::array<journal_fragment, 2> entry02{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 702, .top = 687, .width = 290, .height = 271}},
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 255}},
}};
constexpr std::array<journal_fragment, 1> entry03{{
    {.page = 8,
     .offset = scan08_at,
     .length = scan08_bytes,
     .image = spread,
     .region = {.left = 992, .top = 267, .width = 297, .height = 691}},
}};
constexpr std::array<journal_fragment, 1> entry04{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 23, .width = 290, .height = 344}},
}};
constexpr std::array<journal_fragment, 1> entry05{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 371, .width = 290, .height = 269}},
}};
constexpr std::array<journal_fragment, 2> entry06{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 50, .top = 644, .width = 290, .height = 314}},
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
     .region = {.left = 340, .top = 247, .width = 289, .height = 711}},
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
     .region = {.left = 992, .top = 8, .width = 297, .height = 206}},
}};
constexpr std::array<journal_fragment, 1> entry10{{
    {.page = 9,
     .offset = scan09_at,
     .length = scan09_bytes,
     .image = spread,
     .region = {.left = 992, .top = 218, .width = 297, .height = 311}},
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
     .region = {.left = 340, .top = 475, .width = 289, .height = 347}},
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
     .region = {.left = 50, .top = 518, .width = 290, .height = 271}},
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
     .region = {.left = 340, .top = 8, .width = 289, .height = 252}},
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
constexpr std::array<journal_fragment, 2> entry26{{
    {.page = 11,
     .offset = scan11_at,
     .length = scan11_bytes,
     .image = spread,
     .region = {.left = 702, .top = 469, .width = 290, .height = 489}},
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
     .region = {.left = 992, .top = 703, .width = 297, .height = 255}},
}};
constexpr std::array<journal_fragment, 1> entry29{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 50, .top = 32, .width = 290, .height = 344}},
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
     .region = {.left = 992, .top = 8, .width = 297, .height = 130}},
}};
constexpr std::array<journal_fragment, 1> entry36{{
    {.page = 12,
     .offset = scan12_at,
     .length = scan12_bytes,
     .image = spread,
     .region = {.left = 992, .top = 142, .width = 297, .height = 816}},
}};
constexpr std::array<journal_fragment, 4> entry37{{
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 50, .top = 22, .width = 290, .height = 890}},
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 340, .top = 8, .width = 289, .height = 904}},
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 702, .top = 8, .width = 290, .height = 950}},
    {.page = 13,
     .offset = scan13_at,
     .length = scan13_bytes,
     .image = spread,
     .region = {.left = 992, .top = 8, .width = 297, .height = 950}},
}};
constexpr std::array<journal_fragment, 1> entry38{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 50, .top = 21, .width = 290, .height = 470}},
}};
constexpr std::array<journal_fragment, 2> entry39{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 50, .top = 495, .width = 290, .height = 463}},
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
     .region = {.left = 340, .top = 352, .width = 289, .height = 349}},
}};
constexpr std::array<journal_fragment, 1> entry42{{
    {.page = 14,
     .offset = scan14_at,
     .length = scan14_bytes,
     .image = spread,
     .region = {.left = 340, .top = 705, .width = 289, .height = 253}},
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
     .region = {.left = 992, .top = 8, .width = 297, .height = 190}},
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
     .region = {.left = 702, .top = 8, .width = 290, .height = 256}},
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
     .region = {.left = 992, .top = 189, .width = 297, .height = 765}},
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

constexpr std::array<journal_entry_fact, 99> archive_entries{{
    {.kind = journal_kind::entry, .number = 1, .fragments = entry01},
    {.kind = journal_kind::entry, .number = 2, .fragments = entry02},
    {.kind = journal_kind::entry, .number = 3, .fragments = entry03},
    {.kind = journal_kind::entry, .number = 4, .fragments = entry04},
    {.kind = journal_kind::entry, .number = 5, .fragments = entry05},
    {.kind = journal_kind::entry, .number = 6, .fragments = entry06},
    {.kind = journal_kind::entry, .number = 7, .fragments = entry07},
    {.kind = journal_kind::entry, .number = 8, .fragments = entry08},
    {.kind = journal_kind::entry, .number = 9, .fragments = entry09},
    {.kind = journal_kind::entry, .number = 10, .fragments = entry10},
    {.kind = journal_kind::entry, .number = 11, .fragments = entry11},
    {.kind = journal_kind::entry, .number = 12, .fragments = entry12},
    {.kind = journal_kind::entry, .number = 13, .fragments = entry13},
    {.kind = journal_kind::entry, .number = 14, .fragments = entry14},
    {.kind = journal_kind::entry, .number = 15, .fragments = entry15},
    {.kind = journal_kind::entry, .number = 16, .fragments = entry16},
    {.kind = journal_kind::entry, .number = 17, .fragments = entry17},
    {.kind = journal_kind::entry, .number = 18, .fragments = entry18},
    {.kind = journal_kind::entry, .number = 19, .fragments = entry19},
    {.kind = journal_kind::entry, .number = 20, .fragments = entry20},
    {.kind = journal_kind::entry, .number = 21, .fragments = entry21},
    {.kind = journal_kind::entry, .number = 22, .fragments = entry22},
    {.kind = journal_kind::entry, .number = 23, .fragments = entry23},
    {.kind = journal_kind::entry, .number = 24, .fragments = entry24},
    {.kind = journal_kind::entry, .number = 25, .fragments = entry25},
    {.kind = journal_kind::entry, .number = 26, .fragments = entry26},
    {.kind = journal_kind::entry, .number = 27, .fragments = entry27},
    {.kind = journal_kind::entry, .number = 28, .fragments = entry28},
    {.kind = journal_kind::entry, .number = 29, .fragments = entry29},
    {.kind = journal_kind::entry, .number = 30, .fragments = entry30},
    {.kind = journal_kind::entry, .number = 31, .fragments = entry31},
    {.kind = journal_kind::entry, .number = 32, .fragments = entry32},
    {.kind = journal_kind::entry, .number = 33, .fragments = entry33},
    {.kind = journal_kind::entry, .number = 34, .fragments = entry34},
    {.kind = journal_kind::entry, .number = 35, .fragments = entry35},
    {.kind = journal_kind::entry, .number = 36, .fragments = entry36},
    {.kind = journal_kind::entry, .number = 37, .fragments = entry37},
    {.kind = journal_kind::entry, .number = 38, .fragments = entry38},
    {.kind = journal_kind::entry, .number = 39, .fragments = entry39},
    {.kind = journal_kind::entry, .number = 40, .fragments = entry40},
    {.kind = journal_kind::entry, .number = 41, .fragments = entry41},
    {.kind = journal_kind::entry, .number = 42, .fragments = entry42},
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
    {.kind = journal_kind::entry, .number = 58, .fragments = entry58},
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

/// The edition, and the one this build knows the insides of.
///
/// Its fingerprint is the same one `machine::known_documents()` carries
/// for the gate: one artifact seen twice, and the suite checks that the
/// two agree.
constexpr std::array<journal_edition, 1> table{{
    {.fingerprint =
         "67cbfc0c833b835494310680ad298bc4de1cdcc0168115cc3608c2f6074c737c",
     .name = "Pool of Radiance Adventurer's Journal, archive release",
     .entries = archive_entries},
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
