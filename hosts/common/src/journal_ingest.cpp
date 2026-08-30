// SPDX-License-Identifier: AGPL-3.0-only
//
// Ingestion. journal_ingest.h has the reasoning and the order.

#include "amberfolio/host/journal_ingest.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ocr.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {

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
        } else if (!into.record_scan(fact.number, text)) {
          why = journal_trouble::too_large;
        } else {
          ++report.recognized;
        }
      }
    }
    // The first thing that went wrong, kept whole. A host that printed
    // only a count would leave a player with nothing to look up.
    if (why != journal_trouble::none &&
        report.first_trouble == journal_trouble::none) {
      report.first_trouble = why;
      report.first_failure = fact.number;
    }
  }
  return report;
}

}  // namespace amberfolio::host
