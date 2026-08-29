// SPDX-License-Identifier: AGPL-3.0-only
//
// Ingestion: a player's document in, a text store out (M5-E3, #174).
//
// The four pieces this ties together each have their own file and their
// own reasoning — the fact table (`journal_facts.h`), the extractor
// (`journal_extract.h`), the engine (`journal_ocr.h`) and the store
// (`journal_store.h`). What is here is the order they go in and the
// decisions that only exist because they are in that order.
//
//
// Fingerprint first, and nothing before it
// ----------------------------------------
//
// The very first thing an ingestion does is hash the whole document and
// look it up. Not because the gate demands it — the gate is a seam's
// business (`machine/document.h`) — but because *the offsets are only
// true of one file*. A fact table followed into a document it was not
// gathered from does not produce a wrong transcription; it produces bytes
// that fail to inflate, or inflate to the wrong size, twenty times in a
// row. Refusing by fingerprint turns that into one sentence a player can
// act on: this is not an edition I know, here is its fingerprint, here is
// how an edition gets added.
//
// So an unrecognized document is `unrecognized_edition` and stops there.
// It is the same first-class "I do not know this file" answer the two
// tables one level out already give, and PLAN.md §9's mitigation for
// edition variance is exactly this and not a guess.
//
//
// One entry at a time, and only one image in memory
// ------------------------------------------------
//
// A journal is a hundred-odd entries and a decoded page is a megabyte or
// two, so extracting them all and then recognizing them all would hold
// something like a gigabyte for no reason. The loop is extract, read,
// store, drop — which also gives the browser the shape it needs, where
// the engine is on the far side of the ABI and drives the loop itself:
// the page asks for entry *i*'s pixels, recognizes them in JS, hands back
// the text, and asks for the next.
//
// That is why the class has both a `run()` for a host with a synchronous
// engine and the three calls a host without one drives by hand. They are
// the same loop; only who is holding it differs.
//
//
// An ingestion never destroys a correction
// ---------------------------------------
//
// The store handed in is the one already on the player's disk, read back
// first. `record_scan()` replaces what an engine read and leaves what a
// person wrote (`journal_store.h`), so re-ingesting with a better engine
// improves the transcriptions and keeps the fixes — which is #174's
// "correctable: a player can fix an OCR error and the fix survives
// re-ingestion", and is a property of this order rather than of any code
// here.
//
// A store of a *different* edition is cleared instead of merged. Entry 12
// of one printing is not entry 12 of another, and silently keeping text
// across editions would be the one failure that looks like it worked.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {

/// What one ingestion did — the line a host prints when it is over.
///
/// Four numbers rather than a bool, because "it worked" is not what a
/// player needs to know. An ingestion that read every image and
/// recognized none of them is a missing engine; one that recognized most
/// is a scan the engine found hard; one that extracted none is a fact
/// table that does not match this document. Those are three different
/// next moves and one bool cannot tell them apart.
struct journal_ingest_report {
  /// Why the ingestion as a whole stopped, or `none`. An entry that
  /// failed on its own does not set this — it is counted below and the
  /// run goes on.
  journal_trouble trouble{journal_trouble::none};
  /// The document's fingerprint, always — including when it was not
  /// recognized, which is the one case where it is the useful half of the
  /// message.
  sha256_digest fingerprint{};
  /// How many entries the edition's fact table has.
  std::uint32_t entries{0};
  /// How many of them decoded to an image.
  std::uint32_t extracted{0};
  /// How many of those the engine read text out of.
  std::uint32_t recognized{0};
  /// The first entry that failed, and how — zero and `none` if none did.
  std::uint16_t first_failure{0};
  journal_trouble first_trouble{journal_trouble::none};
};

/// One ingestion of one document.
///
/// Holds a borrowed view of the document for its life, and one entry's
/// scan at a time. Built, used, and dropped: it is onboarding, not a
/// resident of a running machine.
class journal_ingester {
 public:
  /// `table` is a parameter so that the probe (`journal_probe.h`) can be
  /// ingested without the shipped table knowing anything about it — the
  /// same argument `af_web_probe_seam_register` makes for a test seam
  /// that a player's listing has no business carrying.
  explicit journal_ingester(
      std::span<const journal_edition> table = known_journals()) noexcept
      : table_(table) {}

  /// Hash `document`, look it up, and be ready to extract from it.
  ///
  /// `document` must outlive this object. `unrecognized_edition` leaves
  /// the ingester with a fingerprint and nothing else, which is what a
  /// host prints.
  [[nodiscard]] journal_trouble begin(std::span<const std::uint8_t> document);

  [[nodiscard]] const journal_edition* edition() const noexcept {
    return edition_;
  }
  [[nodiscard]] const sha256_digest& fingerprint() const noexcept {
    return fingerprint_;
  }
  /// The fingerprint as 64 lowercase hex characters, for a message and
  /// for the store's header.
  [[nodiscard]] std::string fingerprint_hex() const;

  /// How many entries this edition's table has, and what they are.
  [[nodiscard]] std::size_t entries() const noexcept;
  [[nodiscard]] const journal_entry_fact* entry_at(
      std::size_t index) const noexcept;

  /// Get entry `index`'s scan into `scan()`, by whichever route its
  /// filter takes (`journal_extract.h`).
  [[nodiscard]] journal_trouble extract(std::size_t index);

  /// What the last `extract()` produced. Empty if it failed.
  [[nodiscard]] const journal_scan& scan() const noexcept { return scan_; }

  /// Its samples, for a caller that only handles decoded editions — a
  /// test comparing pixels, or a host preview. Empty for a scan that went
  /// through as its own bytes, which `scan()` is the way to reach.
  [[nodiscard]] const journal_bitmap& image() const noexcept {
    return scan_.gray;
  }

  /// The whole loop, for a host whose engine is a C++ object it can call.
  ///
  /// `engine` may be null: every entry is still extracted, nothing is
  /// recognized, and the report says both numbers. `into` is the store
  /// already on disk — read it back before calling this, so corrections
  /// survive.
  [[nodiscard]] journal_ingest_report run(journal_ocr* engine,
                                          journal_store& into);

  /// Point `into` at this document's edition, clearing it if it was a
  /// store of a different one. `run()` does this itself; a host driving
  /// the loop by hand calls it before its first `extract()`.
  void adopt(journal_store& into) const;

 private:
  std::span<const journal_edition> table_;
  std::span<const std::uint8_t> document_;
  const journal_edition* edition_{nullptr};
  sha256_digest fingerprint_{};
  journal_scan scan_;
};

}  // namespace amberfolio::host
