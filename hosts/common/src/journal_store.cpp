// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal text store. journal_store.h has the reasoning and the
// format.

#include "amberfolio/host/journal_store.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/host/journal_facts.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

/// A decimal number, and where it ended. No sign, no space, no leading
/// plus: this parser is deliberately unforgiving, because everything it
/// reads is something this project wrote.
[[nodiscard]] bool take_number(std::string_view text, std::size_t& at,
                               std::uint64_t limit,
                               std::uint64_t& out) noexcept {
  const std::size_t start = at;
  std::uint64_t value = 0;
  while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
    value = (value * 10U) + static_cast<std::uint64_t>(text[at] - '0');
    if (value > limit) {
      return false;
    }
    ++at;
  }
  if (at == start) {
    return false;
  }
  out = value;
  return true;
}

[[nodiscard]] bool take_literal(std::string_view text, std::size_t& at,
                                std::string_view what) noexcept {
  if (text.substr(at, what.size()) != what) {
    return false;
  }
  at += what.size();
  return true;
}

/// The rest of the line, and past its newline. False at the end of the
/// text: every line this format has ends with one.
[[nodiscard]] bool take_line(std::string_view text, std::size_t& at,
                             std::string_view& out) noexcept {
  const std::size_t end = text.find('\n', at);
  if (end == std::string_view::npos) {
    return false;
  }
  out = text.substr(at, end - at);
  at = end + 1U;
  return true;
}

void append_number(std::string& out, std::uint64_t value) {
  char digits[24];
  std::size_t at = sizeof(digits);
  do {
    digits[--at] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U);
  out.append(digits + at, sizeof(digits) - at);
}

void append_record(std::string& out, std::string_view keyword,
                   std::uint16_t number, std::string_view body) {
  if (body.empty()) {
    return;
  }
  out.append(keyword);
  out.push_back(' ');
  append_number(out, number);
  out.push_back(' ');
  append_number(out, body.size());
  out.push_back('\n');
  out.append(body);
  out.push_back('\n');
}

}  // namespace

const journal_text* journal_store::find(std::uint16_t number) const noexcept {
  const auto found = std::ranges::lower_bound(
      entries_, number, {},
      [](const journal_text& entry) { return entry.number; });
  if (found == entries_.end() || found->number != number) {
    return nullptr;
  }
  return &*found;
}

std::string_view journal_store::text(std::uint16_t number) const noexcept {
  const journal_text* entry = find(number);
  return entry == nullptr ? std::string_view{} : entry->text();
}

journal_text* journal_store::entry_for(std::uint16_t number) {
  const auto at = std::ranges::lower_bound(
      entries_, number, {},
      [](const journal_text& entry) { return entry.number; });
  if (at != entries_.end() && at->number == number) {
    return &*at;
  }
  if (entries_.size() >= journal_max_entries) {
    return nullptr;
  }
  return &*entries_.insert(
      at, journal_text{.number = number, .scanned = {}, .corrected = {}});
}

bool journal_store::record_scan(std::uint16_t number, std::string_view what) {
  if (what.size() > journal_max_entry_bytes) {
    return false;
  }
  journal_text* entry = entry_for(number);
  if (entry == nullptr) {
    return false;
  }
  entry->scanned.assign(what);
  return true;
}

bool journal_store::correct(std::uint16_t number, std::string_view what) {
  if (what.size() > journal_max_entry_bytes) {
    return false;
  }
  journal_text* entry = entry_for(number);
  if (entry == nullptr) {
    return false;
  }
  entry->corrected.assign(what);
  return true;
}

std::size_t journal_store::recognized() const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      entries_, [](const journal_text& e) { return !e.text().empty(); }));
}

std::size_t journal_store::corrections() const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      entries_, [](const journal_text& e) { return !e.corrected.empty(); }));
}

void journal_store::clear() {
  edition_.clear();
  engine_.clear();
  entries_.clear();
}

std::string journal_store::serialize() const {
  std::string out;
  out.append(journal_store_magic);
  out.push_back(' ');
  append_number(out, journal_store_version);
  out.push_back('\n');
  out.append("edition ");
  out.append(edition_);
  out.push_back('\n');
  out.append("engine ");
  out.append(engine_);
  out.push_back('\n');
  for (const journal_text& entry : entries_) {
    append_record(out, "scanned", entry.number, entry.scanned);
    append_record(out, "corrected", entry.number, entry.corrected);
  }
  return out;
}

journal_trouble journal_store::parse(std::string_view whole) {
  if (whole.size() > journal_max_entries * journal_max_entry_bytes * 2U) {
    return journal_trouble::too_large;
  }

  // A store is a file a player may edit, and an editor on Windows writes
  // CRLF. Every length in the format counts bytes, so a file an editor
  // has been through would otherwise disagree with its own counts on
  // every record and be refused whole -- which is a correct refusal and a
  // useless one. Normalizing first makes a round trip through Notepad
  // lossless, and `serialize()` writes LF whatever came in.
  //
  // Only when there is something to normalize: the common case is a file
  // this build wrote, and it pays nothing.
  std::string normalized;
  if (whole.find('\r') != std::string_view::npos) {
    normalized.reserve(whole.size());
    for (std::size_t i = 0; i < whole.size(); ++i) {
      if (whole[i] == '\r' && i + 1U < whole.size() && whole[i + 1U] == '\n') {
        continue;
      }
      normalized.push_back(whole[i]);
    }
  }
  const std::string_view text =
      normalized.empty() ? whole : std::string_view(normalized);

  std::size_t at = 0;
  if (!take_literal(text, at, journal_store_magic) ||
      !take_literal(text, at, " ")) {
    return journal_trouble::not_a_store;
  }
  std::uint64_t version = 0;
  if (!take_number(text, at, 0xFFFFU, version) ||
      !take_literal(text, at, "\n")) {
    return journal_trouble::not_a_store;
  }
  // A store from a later format is refused rather than read as far as it
  // parses: what a version number is for.
  if (version != journal_store_version) {
    return journal_trouble::not_a_store;
  }

  std::string_view edition;
  std::string_view engine;
  if (!take_literal(text, at, "edition ") || !take_line(text, at, edition) ||
      !take_literal(text, at, "engine ") || !take_line(text, at, engine)) {
    return journal_trouble::not_a_store;
  }

  // Built beside the live one and swapped in at the end, so a file that
  // goes wrong at its last record leaves the store exactly as it was.
  journal_store read;
  read.edition_.assign(edition);
  read.engine_.assign(engine);

  while (at < text.size()) {
    const bool scanned = text.compare(at, 8, "scanned ") == 0;
    const bool corrected = text.compare(at, 10, "corrected ") == 0;
    if (!scanned && !corrected) {
      return journal_trouble::not_a_store;
    }
    at += scanned ? 8U : 10U;

    std::uint64_t number = 0;
    std::uint64_t length = 0;
    if (!take_number(text, at, 0xFFFFU, number) ||
        !take_literal(text, at, " ") ||
        !take_number(text, at, journal_max_entry_bytes, length) ||
        !take_literal(text, at, "\n")) {
      return journal_trouble::not_a_store;
    }
    if (text.size() - at < length + 1U ||
        text[at + static_cast<std::size_t>(length)] != '\n') {
      return journal_trouble::not_a_store;
    }
    const std::string_view body =
        text.substr(at, static_cast<std::size_t>(length));
    at += static_cast<std::size_t>(length) + 1U;

    const bool stored =
        scanned ? read.record_scan(static_cast<std::uint16_t>(number), body)
                : read.correct(static_cast<std::uint16_t>(number), body);
    if (!stored) {
      return journal_trouble::too_large;
    }
  }

  *this = std::move(read);
  return journal_trouble::none;
}

sha256_digest journal_store::fingerprint() const {
  const std::string bytes = serialize();
  return sha256(std::span<const std::uint8_t>(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
}

}  // namespace amberfolio::host
