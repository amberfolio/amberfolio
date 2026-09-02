// SPDX-License-Identifier: AGPL-3.0-only
//
// The journal text store. journal_store.h has the reasoning and the
// format.

#include "amberfolio/host/journal_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
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

/// One lower-case word, up to but not including the space after it.
///
/// The space is left for the caller's `take_literal`, so a record whose
/// word runs straight into its number is refused rather than read as a
/// word this build happens not to know.
[[nodiscard]] bool take_word(std::string_view text, std::size_t& at,
                             std::string_view& out) noexcept {
  std::size_t end = at;
  while (end < text.size() && text[end] >= 'a' && text[end] <= 'z') {
    ++end;
  }
  if (end == at) {
    return false;
  }
  out = text.substr(at, end - at);
  at = end;
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
                   machine::journal_citation what, std::string_view body) {
  if (body.empty()) {
    return;
  }
  out.append(keyword);
  out.push_back(' ');
  out.append(journal_kind_name(what.kind));
  out.push_back(' ');
  append_number(out, what.number);
  out.push_back(' ');
  append_number(out, body.size());
  out.push_back('\n');
  out.append(body);
  out.push_back('\n');
}

}  // namespace

namespace {

/// The order the store keeps: by section, then by number within it.
///
/// A pair and not a number, which is what #218 is about. `kind` first
/// rather than `number` first only because it makes a serialized store
/// readable by a person — the three sections come out in blocks — and any
/// total order would do for `fingerprint()`.
[[nodiscard]] constexpr auto key_of(machine::journal_citation what) noexcept {
  return std::pair{static_cast<std::uint8_t>(what.kind), what.number};
}

[[nodiscard]] constexpr auto key_of(const journal_text& entry) noexcept {
  return std::pair{static_cast<std::uint8_t>(entry.kind), entry.number};
}

}  // namespace

const journal_text* journal_store::find(
    machine::journal_citation what) const noexcept {
  const auto found = std::ranges::lower_bound(
      entries_, key_of(what), {},
      [](const journal_text& entry) { return key_of(entry); });
  if (found == entries_.end() || key_of(*found) != key_of(what)) {
    return nullptr;
  }
  return &*found;
}

std::string_view journal_store::text(
    machine::journal_citation what) const noexcept {
  const journal_text* entry = find(what);
  return entry == nullptr ? std::string_view{} : entry->text();
}

journal_text* journal_store::entry_for(machine::journal_citation what) {
  const auto at = std::ranges::lower_bound(
      entries_, key_of(what), {},
      [](const journal_text& entry) { return key_of(entry); });
  if (at != entries_.end() && key_of(*at) == key_of(what)) {
    return &*at;
  }
  if (entries_.size() >= journal_max_entries) {
    return nullptr;
  }
  return &*entries_.insert(at, journal_text{.kind = what.kind,
                                            .number = what.number,
                                            .scanned = {},
                                            .corrected = {}});
}

bool journal_store::record_scan(machine::journal_citation what,
                                std::string_view text) {
  if (text.size() > journal_max_entry_bytes) {
    return false;
  }
  journal_text* entry = entry_for(what);
  if (entry == nullptr) {
    return false;
  }
  entry->scanned.assign(text);
  return true;
}

bool journal_store::correct(machine::journal_citation what,
                            std::string_view text) {
  if (text.size() > journal_max_entry_bytes) {
    return false;
  }
  journal_text* entry = entry_for(what);
  if (entry == nullptr) {
    return false;
  }
  entry->corrected.assign(text);
  return true;
}

void journal_store::set_seen(std::span<const machine::journal_seen_row> rows) {
  seen_.assign(rows.begin(), rows.size() > machine::journal_log_rows
                                 ? rows.begin() + machine::journal_log_rows
                                 : rows.end());
  changed_ = true;
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
  seen_.clear();
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
    const machine::journal_citation what{.kind = entry.kind,
                                         .number = entry.number};
    append_record(out, "scanned", what, entry.scanned);
    append_record(out, "corrected", what, entry.corrected);
  }
  // The log last, so a store reads as its texts and then what the game has
  // said about them. No length and no body: a `seen` line carries facts
  // about an entry and not a word of one.
  for (const machine::journal_seen_row& row : seen_) {
    out.append("seen ");
    out.append(journal_kind_name(row.what.kind));
    out.push_back(' ');
    append_number(out, row.what.number);
    for (const std::uint8_t field :
         {row.month, row.day, row.hour, row.minute}) {
      out.push_back(' ');
      append_number(out, field);
    }
    out.push_back(' ');
    append_number(out, row.read ? 1U : 0U);
    out.push_back('\n');
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
  // A store from a *later* format is refused rather than read as far as
  // it parses: what a version number is for. An older one is read, which
  // is the other half of what it is for — see the format in the header.
  if (version > journal_store_version ||
      version < journal_store_oldest_version) {
    return journal_trouble::not_a_store;
  }
  // Version 1 predates the sections and so has no kind on its records.
  const bool kinded = version >= 2U;

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
    const bool seen = kinded && text.compare(at, 5, "seen ") == 0;
    if (!scanned && !corrected && !seen) {
      return journal_trouble::not_a_store;
    }
    if (seen) {
      at += 5U;
      machine::journal_seen_row row;
      std::uint64_t number = 0;
      std::array<std::uint64_t, 5> fields{};
      std::string_view word;
      if (!take_word(text, at, word) ||
          !journal_kind_from_name(word, row.what.kind) ||
          !take_literal(text, at, " ") ||
          !take_number(text, at, 0xFFFFU, number)) {
        return journal_trouble::not_a_store;
      }
      row.what.number = static_cast<std::uint16_t>(number);
      for (std::uint64_t& field : fields) {
        if (!take_literal(text, at, " ") ||
            !take_number(text, at, 0xFFU, field)) {
          return journal_trouble::not_a_store;
        }
      }
      if (!take_literal(text, at, "\n")) {
        return journal_trouble::not_a_store;
      }
      row.month = static_cast<std::uint8_t>(fields[0]);
      row.day = static_cast<std::uint8_t>(fields[1]);
      row.hour = static_cast<std::uint8_t>(fields[2]);
      row.minute = static_cast<std::uint8_t>(fields[3]);
      row.read = fields[4] != 0;
      if (!row.what) {
        return journal_trouble::not_a_store;
      }
      if (read.seen_.size() < machine::journal_log_rows) {
        read.seen_.push_back(row);
      }
      continue;
    }
    at += scanned ? 8U : 10U;

    journal_kind kind = journal_kind::entry;
    if (kinded) {
      std::string_view word;
      if (!take_word(text, at, word) || !journal_kind_from_name(word, kind) ||
          !take_literal(text, at, " ")) {
        return journal_trouble::not_a_store;
      }
    }

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

    const machine::journal_citation what{
        .kind = kind, .number = static_cast<std::uint16_t>(number)};
    const bool stored =
        scanned ? read.record_scan(what, body) : read.correct(what, body);
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

void restore_journal_log(machine::journal_state& into,
                         const journal_store& from) noexcept {
  const std::span<const machine::journal_seen_row> stored = from.seen();
  for (std::size_t i = stored.size(); i > 0; --i) {
    const machine::journal_seen_row& row = stored[i - 1];
    into.note_seen(row.what, row.month, row.day, row.hour, row.minute);
    if (row.read) {
      static_cast<void>(into.mark_seen_read(row.what));
    }
  }
  into.set_seen_changed(false);
}

}  // namespace amberfolio::host
