// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal edition fact table: where each entry's scan lives inside a
// player's own PDF (M5-E3, #174).
//
// `machine/document.h` fingerprints a document the player holds and gates
// a seam on it, and it is careful to say what it is not: "a gate is over
// bytes". This is the other half, and it is the half that reads *inside*
// one — the page an entry is on, the byte offset of the image stream that
// carries its scan, how that stream is encoded, and which rectangle of
// the decoded image is the entry itself.
//
//
// Facts, and the same rule as every other fact in this tree
// --------------------------------------------------------
//
// CONTRIBUTING.md's clean-content rule permits *facts about* an artifact
// — addresses, offsets, format descriptions, fingerprints — and forbids
// any of the artifact. Everything below is the first kind. An offset is
// where something is, not what it is; a width and a height are the shape
// of a rectangle; a fingerprint names a file without carrying a byte of
// it. No page image, no glyph, no word of the journal's text is in this
// tree or ever will be — the text a player ends up with is OCRed on the
// player's own machine, out of the player's own document, and stays
// there (`journal_store.h`).
//
// This is the same standard the interception points in `docs/seams.md`
// are held to, and it is reviewed the same way.
//
//
// An extractor, not a PDF reader
// -----------------------------
//
// The table below is what makes that possible. A general-purpose PDF
// parser would have to find the objects, resolve the cross-reference
// table, walk a page tree and interpret a content stream — a large,
// input-driven surface pointed straight at a file this project did not
// make, for a benefit nobody needs: the editions are *known*, and an
// edition nobody recognized was already refused (`journal_ingest.h`
// resolves by fingerprint before it reads a byte). So the offsets are
// facts, gathered once, and the extractor follows them
// (`journal_extract.h`).
//
// The failure mode that leaves is the good one. A table entry that is
// wrong points at bytes that do not inflate, or inflate to the wrong
// size, and the extractor says so and stops. It cannot point at bytes
// that quietly mean something else, because the size and the shape are
// facts too and every one of them is checked.
//
//
// The table is empty, and that is the honest state
// -----------------------------------------------
//
// `machine/document.cpp` has no journal line, for a stated reason: "a
// fingerprint is a fact about a file somebody actually hashed, and nobody
// here has hashed that one". The same is true one level further in, and
// more so — these offsets are facts about a document somebody has to sit
// down with. So `known_journals()` is empty, every real journal is
// therefore an unrecognized edition, and the mechanism is proven against
// a synthetic document this project builds itself
// (`journal_probe.h`).
//
// **Adding an edition is adding data**: a fingerprint that also goes into
// `known_documents()` as a `journal` (the two tables are checked against
// each other in the suite), and one `journal_entry_fact` per entry.
// `docs/journal.md` §3 is the procedure.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/sha256.h"

namespace amberfolio::host {

/// How an entry's image stream is encoded — the PDF filter its object
/// names.
///
/// Two are implemented and the rest are named. That is deliberate and it
/// is CLAUDE.md's "log, don't fake" one level up: a filter this build
/// cannot decode is refused *by name*, so somebody fingerprinting a new
/// edition learns what their document needs rather than watching an
/// extraction produce noise. Nothing here was built on spec — an edition
/// that turns out to want `dct` gets the code the day somebody has the
/// document, and `docs/journal.md` §4 says what that code would look
/// like.
enum class journal_filter : std::uint8_t {
  /// The stream is the image's samples already.
  none,
  /// `/FlateDecode` — a zlib stream. Implemented.
  flate,
  /// `/DCTDecode` — JPEG. Not implemented.
  dct,
  /// `/CCITTFaxDecode` — group 3 or 4 fax. Not implemented.
  ccitt,
  /// `/JBIG2Decode`. Not implemented.
  jbig2,
};

/// The printable name of one, as the PDF spells it. Never null.
[[nodiscard]] const char* journal_filter_name(journal_filter which) noexcept;

/// Whether this build can get an entry under `which` as far as an engine
/// (`journal_extract.h`).
///
/// **Two different questions since #212**, and keeping them apart is what
/// lets a JPEG-paged edition work without a JPEG decoder in this tree.
/// This one is "can the pipeline carry it": `none` and `flate`, which are
/// decoded here, and `dct`, which is not — its stream goes to the engine
/// as its own bytes. `/CCITTFaxDecode` and `/JBIG2Decode` are still
/// refused by name, and still not built for on spec.
[[nodiscard]] bool journal_filter_supported(journal_filter which) noexcept;

/// Whether this build turns `which` into samples of its own.
///
/// The other half of the question above. False means the engine is handed
/// the stream unaltered and told which rectangle of it is the entry —
/// `journal_extract.h`'s `journal_scan`, and the reason the region became
/// the engine's business rather than the extractor's.
[[nodiscard]] bool journal_filter_decoded(journal_filter which) noexcept;

/// The shape of the image an entry's stream decodes to.
///
/// These are the PDF image dictionary's own entries, under this
/// project's names: `/Width`, `/Height`, `/BitsPerComponent`, the number
/// of components its `/ColorSpace` implies, and the `/DecodeParms`
/// predictor. Facts about a document's structure, in the same sense a
/// struct layout is a fact.
struct journal_image {
  /// `/Width` and `/Height`, in samples.
  std::uint32_t width{};
  std::uint32_t height{};
  /// `/BitsPerComponent`: 1 or 8. Anything else is refused.
  std::uint8_t bits_per_component{8};
  /// How many components a sample has: 1 for a gray or bilevel scan, 3
  /// for RGB. Anything else is refused.
  std::uint8_t components{1};
  /// `/DecodeParms /Predictor`: 1 (or 0) for none, 10–15 for the PNG
  /// predictors, which is what a Flate-compressed image almost always
  /// uses. TIFF's predictor 2 is refused rather than guessed at.
  ///
  /// Meaningless for a filter this build does not decode: a `/DCTDecode`
  /// stream carries no predictor and nothing here would apply one.
  std::uint8_t predictor{1};
  /// The filter the stream is under.
  journal_filter filter{journal_filter::flate};
  /// `/Decode [1 0]`, or an `/ImageMask` — the sample values run the
  /// other way, so a 1 bit is ink rather than paper. Recorded here
  /// because it is a fact about the document and applying it is not
  /// optional: OCR of an inverted page recognizes nothing.
  bool inverted{false};
};

/// A rectangle of the image, in its own samples.
///
/// Of the *decoded* image where this build decodes one, and of the image
/// as the document holds it where it does not (#212) — which is the same
/// rectangle either way, because a decoder does not move pixels around.
/// That is what lets it be handed to an engine as a fact about a picture
/// this build never looked at.
///
/// The entry, and not the page: a scanned page carries a heading, a
/// number, sometimes two entries. What is OCRed is the part that is the
/// entry, because everything else on the page is text the reader (#175)
/// would have to throw away afterwards, and throwing it away afterwards
/// means guessing.
struct journal_region {
  std::uint32_t left{};
  std::uint32_t top{};
  std::uint32_t width{};
  std::uint32_t height{};
};

/// One piece of one entry's scan: where its bytes are, what shape the
/// image holding them is, and which rectangle of that image this piece
/// is.
///
/// **An entry is a list of these, not one of them** (M5-E3b, #214), and
/// the reason is the first real edition. Its entries are set in columns
/// and they *flow*: an entry runs out of column and resumes at the top of
/// the next, and four of its fifty-eight resume on the facing page — a
/// different scan, a different stream. A fact table that gave an entry
/// one rectangle could describe neither: the bounding box of two columns
/// swallows the entries between them, and the first piece alone is half a
/// sentence.
///
/// So the offset and the shape live here rather than on the entry. Most
/// fragments of most editions will repeat their neighbour's, which is the
/// price of being able to say the thing that is true.
struct journal_fragment {
  /// The page of the PDF this piece's scan is on, one-based. Nothing
  /// reads it — the offset is what the extractor follows — and it is here
  /// because it is the fact a person needs to check the row by hand.
  std::uint16_t page{};
  /// The byte offset, in the file, of the first byte of the stream's
  /// data: past the `stream` keyword and its end-of-line, and not past
  /// anything else.
  std::uint64_t offset{};
  /// `/Length` — how many bytes of encoded data there are.
  std::uint32_t length{};
  journal_image image{};
  /// Which rectangle of that image this piece of the entry is.
  journal_region region{};
};

/// One entry of one edition: which entry it is, and where its scan is —
/// in as many pieces as the page it was printed on needed.
struct journal_entry_fact {
  /// The number the game itself uses when it tells a player to read an
  /// entry. The store is keyed by this, not by the order of this table,
  /// so an edition that prints its entries out of order is a table that
  /// is out of order and nothing else.
  std::uint16_t number{};
  /// Its pieces, **in reading order**: what an engine reads out of them
  /// is joined in this order, so a table whose fragments are out of order
  /// is an entry whose sentences are.
  std::span<const journal_fragment> fragments;
};

/// One journal edition this build knows the insides of.
struct journal_edition {
  /// The SHA-256 of the whole file, as 64 lowercase hex characters — the
  /// same spelling `machine::document_edition` uses, and for a recognized
  /// edition the *same value*: an edition here must be a `journal` there,
  /// which the suite checks.
  std::string_view fingerprint;
  /// The name a host prints.
  std::string_view name;
  /// Its entries. Not required to be sorted, and not required to be
  /// every entry the document has — an edition half-tabled is an edition
  /// whose other half a player has not got yet, which is a better answer
  /// than none.
  std::span<const journal_entry_fact> entries;
};

/// Every journal edition this build knows the insides of.
///
/// **Empty**, today, and the file's own top comment says why. An empty
/// table is not a broken one: it is the fail-closed direction, and the
/// consequence is exactly the one `machine/document.cpp` already
/// describes for the gate — every real journal is reported as an
/// unrecognized edition until somebody sits down with one.
[[nodiscard]] std::span<const journal_edition> known_journals();

/// The edition `digest` names within `table`, or null.
///
/// `table` is a parameter rather than always `known_journals()` because
/// the probe (`journal_probe.h`) is a synthetic edition of a synthetic
/// document, and a check that had to reach into the shipped table to run
/// would be a check that changed what a player's build knows.
[[nodiscard]] const journal_edition* find_journal(
    std::span<const journal_edition> table,
    const sha256_digest& digest) noexcept;

/// The same, over the shipped table.
[[nodiscard]] const journal_edition* find_journal(
    const sha256_digest& digest) noexcept;

/// The most entries one edition may have, and the most bytes of text one
/// entry's OCR may produce (`journal_store.h` enforces the second).
///
/// Bounds rather than trust: the first is a sanity limit on a fact table
/// somebody edits by hand, the second is what stops a store file from a
/// later version — or from somebody else — being read into unbounded
/// memory. Both are far above anything a real journal wants.
inline constexpr std::size_t journal_max_entries = 512;
inline constexpr std::size_t journal_max_entry_bytes = std::size_t{64} * 1024;

}  // namespace amberfolio::host
