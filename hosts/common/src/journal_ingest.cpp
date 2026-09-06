// SPDX-License-Identifier: AGPL-3.0-only
//
// Ingestion. journal_ingest.h has the reasoning and the order.

#include "amberfolio/host/journal_ingest.h"

#include <algorithm>
#include <array>
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

journal_reading_quality journal_ingest_report::reading() const {
  journal_reading_quality out;
  double weighted = 0.0;
  for (const journal_item_quality& item : quality) {
    if (!item.reading.known) {
      continue;
    }
    out.known = true;
    out.words += item.reading.words;
    out.doubtful += item.reading.doubtful;
    weighted +=
        item.reading.confidence * static_cast<double>(item.reading.words);
  }
  if (out.words != 0) {
    out.confidence = weighted / static_cast<double>(out.words);
  }
  return out;
}

std::vector<journal_item_quality> journal_ingest_report::worst_first() const {
  std::vector<journal_item_quality> out = quality;
  // Stable, so two items the engine was equally sure of stay in the order
  // they were read -- a report whose tail reshuffled between two runs of
  // the same ingestion would look like the ingestion had changed.
  std::ranges::stable_sort(
      out, [](const journal_item_quality& a, const journal_item_quality& b) {
        return a.reading.confidence < b.reading.confidence;
      });
  return out;
}

journal_trouble journal_ingester::begin(
    std::span<const std::uint8_t> document) {
  document_ = document;
  edition_ = nullptr;
  scan_ = journal_scan{};
  fingerprint_ = sha256(document);
  edition_ = find_journal(table_, fingerprint_);
  return edition_ == nullptr ? journal_trouble::unrecognized_edition
                             : journal_trouble::none;
}

std::string journal_ingester::fingerprint_hex() const {
  std::array<char, sha256_digest::text_length + 1> hex{};
  const std::size_t written = format_hex(fingerprint_, hex);
  return {hex.data(), written};
}

std::size_t journal_ingester::entries() const noexcept {
  return edition_ == nullptr ? 0U : edition_->entries.size();
}

const journal_entry_fact* journal_ingester::entry_at(
    std::size_t index) const noexcept {
  if (edition_ == nullptr || index >= edition_->entries.size()) {
    return nullptr;
  }
  return &edition_->entries[index];
}

journal_trouble journal_ingester::extract(std::size_t index) {
  const journal_entry_fact* fact = entry_at(index);
  if (fact == nullptr) {
    scan_ = journal_scan{};
    return edition_ == nullptr ? journal_trouble::unrecognized_edition
                               : journal_trouble::no_such_entry;
  }
  return extract_scan(document_, *fact, scan_);
}

void journal_ingester::adopt(journal_store& into) const {
  const std::string hex = fingerprint_hex();
  // A store of another edition is cleared rather than merged: entry 12 of
  // one printing is not entry 12 of another (journal_ingest.h).
  if (into.edition() != hex) {
    into.clear();
    into.set_edition(hex);
  }
}

journal_ingest_report journal_ingester::run(journal_ocr* engine,
                                            journal_store& into) {
  journal_ingest_report report;
  report.fingerprint = fingerprint_;
  if (edition_ == nullptr) {
    report.trouble = journal_trouble::unrecognized_edition;
    return report;
  }

  adopt(into);
  into.set_engine(engine == nullptr ? std::string_view{"none"}
                                    : engine->engine());
  report.entries = static_cast<std::uint32_t>(entries());

  std::string text;
  for (std::size_t index = 0; index < entries(); ++index) {
    const journal_entry_fact& fact = *entry_at(index);
    journal_trouble why = extract(index);
    if (why == journal_trouble::none) {
      ++report.extracted;
      if (engine == nullptr) {
        why = journal_trouble::no_engine;
      } else {
        text.clear();
        if (!engine->recognize(scan_, text)) {
          why = journal_trouble::engine_failed;
        } else if (!into.record_scan({.kind = fact.kind, .number = fact.number},
                                     text)) {
          why = journal_trouble::too_large;
        } else {
          ++report.recognized;
          // What the engine knew about that reading, asked for while it
          // is still the last thing the engine did (`journal_ocr.h`).
          // Only kept when the engine reports it at all: a list of
          // unknowns would look like a list of readings nobody was sure
          // of (#315).
          if (const journal_reading_quality how = engine->quality();
              how.known) {
            report.quality.push_back(
                {.what = {.kind = fact.kind, .number = fact.number},
                 .reading = how});
          }
        }
      }
    }
    // The first thing that went wrong, kept whole. A host that printed
    // only a count would leave a player with nothing to look up.
    if (why != journal_trouble::none &&
        report.first_trouble == journal_trouble::none) {
      report.first_trouble = why;
      report.first_failure = {.kind = fact.kind, .number = fact.number};
    }
  }
  return report;
}

}  // namespace amberfolio::host
