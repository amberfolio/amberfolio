// SPDX-License-Identifier: AGPL-3.0-only
//
// The entries that are pictures: a printed drawing reduced to what the
// in-game reader can draw (#328). M5-E3, PLAN.md §5 item 2.
//
// Several of a journal's fifty-eight entries are drawings rather than
// prose — maps, mazes, a diagram, a row of scratched runes — and an OCR
// engine reads every word that is on such a page, which is the entry's
// heading and its one-line caption. The reader therefore showed two
// lines and nineteen empty rows, which is worse than the panel it
// replaced, because the blankness now fills the display.
//
// This file is the half that turns the page into something drawable. It
// runs **once, at ingestion, on the player's own machine**, out of the
// player's own document, exactly as the OCR does — and for the same
// reason: a resample and a quantization per page turn is work nobody
// should pay for twice, and the answer is a fact about the player's
// document and belongs beside their transcription of it
// (`journal_store.h`).
//
//
// Facts, and the same rule as everything else in this pipeline
// -----------------------------------------------------------
//
// A picture is *content* in the strongest sense `journal_store.h` means:
// it is a scan of somebody's book. No pixel of one is in this repository
// and none ever will be. What is here is the arithmetic that reduces
// one, and what is in `journal_facts.h` is where on the scan it is — a
// rectangle, which is a fact about a document in exactly the sense an
// offset is.
//
//
// The four decisions, and where each of them was made
// ---------------------------------------------------
//
// `docs/journal.md` §11 is the whole argument, measured off a real scan
// rather than derived. In short:
//
//   1. **Where the picture is** is a rectangle of the same stream the
//      entry's text comes from, on its own field
//      (`journal_entry_fact::art`), because a picture's rectangle is not
//      the entry's: four of the tabled edition's fourteen are inside no
//      text rectangle of their own entry at all.
//   2. **What it becomes** is `machine::journal_art_*`: the reader's box,
//      four levels, and a fit that allows for the display's own pixel
//      shape. Measured, and every candidate is in the document.
//   3. **Where it lives** is the store, as one more kind of record
//      (`journal_store.h`), rather than a sidecar of its own.
//   4. **How it is drawn** is the reader's business
//      (`core/src/machine/seam_journal.cpp`), and it is plane surgery
//      because the program has no routine that draws a picture.
//
//
// Levels rather than colours, which is the one thing worth arguing
// ---------------------------------------------------------------
//
// The obvious reduction is to the sixteen colours the screen has. It is
// the wrong one twice over. The pictures are one hue of ink on paper, so
// there is no colour in them to keep — tone is the whole content. And
// the palette the program has installed is a fact about a *running
// machine*, which an ingestion is not: quantizing here to sixteen
// nominal EGA colours would be quantizing to a palette this program may
// never have set.
//
// So a picture is stored as four tones and the reader chooses the ramp,
// where the palette registers are readable and the rest of the page's
// colours are already chosen. It also means the ramp can be re-chosen —
// after somebody looks at one on a display, which is the only way this
// kind of decision has ever been made here (#263, #299) — without
// invalidating a single player's ingestion.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/machine/journal.h"

namespace amberfolio::host {

/// The shape a picture of `width` x `height` printed samples is reduced
/// to: the largest that fits the reader's box and still looks like the
/// printed one on a 4:3 display.
///
/// **The aspect correction is why this is not one division.** A screen
/// pixel in the 320x200 mode is a fifth taller than it is wide, so a
/// square map drawn 160x160 comes out a fifth too tall. Correcting it
/// makes the picture *wider* in pixels rather than shorter, so it also
/// uses more of a box that is landscape and more of the pictures
/// portrait.
struct journal_picture_shape {
  std::uint16_t width{};
  std::uint16_t height{};

  [[nodiscard]] friend constexpr bool operator==(
      const journal_picture_shape&, const journal_picture_shape&) = default;
};

[[nodiscard]] journal_picture_shape fit_picture(std::uint32_t width,
                                                std::uint32_t height) noexcept;

/// One picture of one entry, reduced and packed.
///
/// `levels` is two bits a pixel, four pixels a byte, most significant
/// pair leftmost, each row padded to a whole byte — the shape the
/// reader's plane surgery walks and the shape the store keeps. Level
/// `machine::journal_art_ink` is the darkest and
/// `machine::journal_art_paper` the lightest.
///
/// **A row's tail padding is paper**, which matters because ink is
/// level *zero*: a picture packed into zeroed bytes has a bright edge
/// down its right for anything that walks the bytes rather than asking
/// `level_at`, and walking the bytes is what a reader does.
struct journal_picture {
  machine::journal_citation what{};
  /// Which of the entry's pictures this is, in the fact table's order,
  /// zero first. An entry with an atlas on it has three.
  std::uint8_t nth{};
  std::uint16_t width{};
  std::uint16_t height{};
  std::vector<std::uint8_t> levels;

  [[nodiscard]] bool empty() const noexcept { return levels.empty(); }
  /// How many bytes a row of `width` takes.
  [[nodiscard]] std::size_t stride() const noexcept {
    return machine::journal_art_stride(width);
  }
  /// The level at `(x, y)`, or `journal_art_paper` outside the picture —
  /// which is what the reader wants for the margin around one anyway.
  [[nodiscard]] std::uint8_t level_at(std::uint32_t x,
                                      std::uint32_t y) const noexcept;
};

/// Reduce one decoded region to a picture.
///
/// `page` is eight bits of gray a sample, already cropped to the picture
/// (`journal_extract.h`'s `journal_bitmap`, which is what
/// `extract_fragment` answers). What comes back is the whole of what a
/// store keeps and a reader draws.
///
/// Three steps, each of them measured rather than assumed
/// (`docs/journal.md` §11):
///
///   * **a box filter** to the fitted shape — every source sample counted
///     once, which is what keeps a hairline from disappearing between two
///     sample points the way a nearest-neighbour reduction lets it;
///   * **a normalization onto the page's own extremes** — the scan's
///     paper is a cream and its ink never reaches black, so a fixed
///     threshold would either lose the lightest lines or fill the page;
///   * **a nearest quantization**, with no dither, because every dithered
///     candidate turned the paper into a mesh at this size and the paper
///     is most of the picture.
///
/// False for a region with no samples in it, which leaves `out` empty.
[[nodiscard]] bool reduce_picture(const journal_bitmap& page,
                                  journal_picture& out);

/// A decoder for pages this build does not decode itself.
///
/// **The one thing a picture needs that text does not.** A `/DCTDecode`
/// edition reaches an OCR engine as its own bytes and the engine does
/// the decoding (`docs/journal.md` §4a) — which is what let #212 refuse
/// to put a JPEG decoder in this project. A picture has no engine to
/// hand the work to: somebody has to produce samples.
///
/// So this is a door rather than a decoder, and it stays empty in this
/// tree. Who fills it is a host, out of something it already links for
/// another reason: the desktop's `AMBERFOLIO_LINK_TESSERACT` build has
/// Leptonica and libjpeg-turbo in it already, and a browser has had a
/// JPEG decoder since before this program was written. A build with
/// neither answers `filter_unsupported` and says so, which is the same
/// honest failure §4 gives a filter nobody has written code for.
class journal_page_decoder {
 public:
  journal_page_decoder() = default;
  journal_page_decoder(const journal_page_decoder&) = delete;
  journal_page_decoder& operator=(const journal_page_decoder&) = delete;
  journal_page_decoder(journal_page_decoder&&) = delete;
  journal_page_decoder& operator=(journal_page_decoder&&) = delete;
  virtual ~journal_page_decoder() = default;

  /// `stream` is the page's own encoded bytes; `out` is eight bits of
  /// gray a sample, the whole page, top row first. False for anything
  /// this decoder does not read, which is not an error worth a name of
  /// its own: the caller already knows which filter it handed over.
  [[nodiscard]] virtual bool decode(std::span<const std::uint8_t> stream,
                                    journal_bitmap& out) = 0;

  /// What a host prints beside the count. Never null.
  [[nodiscard]] virtual const char* name() const noexcept = 0;
};

/// Every picture one entry has, reduced.
///
/// `document` is the whole file and `fact` the entry's row. Fragments
/// this build decodes itself go straight through `extract_fragment`; the
/// rest need `decoder`, and a null one — or one that will not read the
/// page — leaves that picture out. What comes back is what was made, so
/// an entry whose pictures could not be decoded comes back empty rather
/// than half-made.
///
/// The trouble is the *first* thing that went wrong, `none` when
/// everything asked for was produced, so a caller can say which of the
/// two kinds of nothing it got.
[[nodiscard]] journal_trouble reduce_entry_pictures(
    std::span<const std::uint8_t> document, const journal_entry_fact& fact,
    journal_page_decoder* decoder, std::vector<journal_picture>& out);

/// The most pictures one store may hold, and the most base64 one
/// picture's record may be. Bounds rather than trust, exactly as
/// `journal_max_entries` and `journal_max_entry_bytes` are: the first
/// is a sanity limit on a fact table somebody edits by hand, the second
/// is what stops a store from somewhere else being read into unbounded
/// memory.
inline constexpr std::size_t journal_max_pictures = journal_max_entries;
inline constexpr std::size_t journal_max_picture_text =
    ((machine::journal_art_bytes + 2U) / 3U) * 4U;

/// The picture's bytes as text, and back — how a picture rides in a store
/// whose whole point is that a person can open it (`journal_store.h`).
///
/// Base64, the ordinary alphabet with `=` padding. Not because the
/// content is meant to be read: because the file it goes in is a text
/// file with a line-counted format, and a run of raw bytes in one would
/// make it a binary file that happens to start with words.
[[nodiscard]] std::string encode_base64(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool decode_base64(std::string_view text,
                                 std::vector<std::uint8_t>& out);

}  // namespace amberfolio::host
