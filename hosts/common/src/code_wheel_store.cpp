// SPDX-License-Identifier: AGPL-3.0-only
//
// The answered-copies store. code_wheel_store.h has the reasoning and the
// format; the parsing is deliberately unforgiving, for journal_store.cpp's
// reason — everything it reads is something this project wrote.

#include "amberfolio/host/code_wheel_store.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"

namespace amberfolio::host {
namespace {

constexpr std::string_view answered_keyword = "answered ";

/// The rest of the line, and past its newline. False at the end of the
/// text: every line this format has ends with one.
[[nodiscard]] bool take_line(std::string_view text, std::size_t& at,
                             std::string_view& out) noexcept {
  const std::size_t end = text.find('\n', at);
  if (end == std::string_view::npos) {
    return false;
  }
  out = text.substr(at, end - at);
  at = end + 1;
  return true;
}

/// `amberfolio-code-wheel <version>`, and the version it named.
[[nodiscard]] bool take_header(std::string_view line,
                               std::uint32_t& version) noexcept {
  if (!line.starts_with(code_wheel_store_magic)) {
    return false;
  }
  std::string_view rest = line.substr(code_wheel_store_magic.size());
  if (rest.empty() || rest.front() != ' ') {
    return false;
  }
  rest.remove_prefix(1);
  if (rest.empty()) {
    return false;
  }
  std::uint32_t value = 0;
  for (const char digit : rest) {
    if (digit < '0' || digit > '9' || value > 0xFFFFU) {
      return false;
    }
    value = (value * 10U) + static_cast<std::uint32_t>(digit - '0');
  }
  version = value;
  return true;
}

}  // namespace

const char* code_wheel_trouble_name(code_wheel_trouble why) noexcept {
  switch (why) {
    case code_wheel_trouble::none:
      return "ok";
    case code_wheel_trouble::not_a_store:
      return "not-a-store";
    case code_wheel_trouble::later_version:
      return "later-version";
    case code_wheel_trouble::bad_line:
      return "bad-line";
  }
  return "unknown";
}

bool code_wheel_store::answered(const sha256_digest& program) const noexcept {
  return std::ranges::find(answered_, program) != answered_.end();
}

bool code_wheel_store::remember(const sha256_digest& program) {
  if (answered(program)) {
    // Told what it knew. Not news, and not a file rewritten.
    return false;
  }
  answered_.push_back(program);
  changed_ = true;
  return true;
}

bool code_wheel_store::forget() noexcept {
  if (answered_.empty()) {
    return false;
  }
  answered_.clear();
  changed_ = true;
  return true;
}

std::string code_wheel_store::serialize() const {
  std::string out(code_wheel_store_magic);
  out += ' ';
  out += std::to_string(code_wheel_store_version);
  out += '\n';
  for (const sha256_digest& one : answered_) {
    std::array<char, sha256_digest::text_length + 1> hex{};
    const std::size_t written = format_hex(one, hex);
    out += answered_keyword;
    out.append(hex.data(), written);
    out += '\n';
  }
  return out;
}

code_wheel_trouble code_wheel_store::parse(std::string_view whole) {
  std::size_t at = 0;
  std::string_view line;
  if (!take_line(whole, at, line)) {
    return code_wheel_trouble::not_a_store;
  }
  std::uint32_t version = 0;
  if (!take_header(line, version)) {
    return code_wheel_trouble::not_a_store;
  }
  if (version > code_wheel_store_version) {
    return code_wheel_trouble::later_version;
  }
  if (version < code_wheel_store_oldest_version) {
    return code_wheel_trouble::not_a_store;
  }

  // Read into a fresh list and swap at the end: a store that turned out
  // to be malformed half-way must leave this object exactly as it was.
  std::vector<sha256_digest> found;
  while (at < whole.size()) {
    if (!take_line(whole, at, line)) {
      return code_wheel_trouble::bad_line;
    }
    if (!line.starts_with(answered_keyword)) {
      return code_wheel_trouble::bad_line;
    }
    sha256_digest digest;
    if (!machine::parse_digest(line.substr(answered_keyword.size()), digest)) {
      return code_wheel_trouble::bad_line;
    }
    if (std::ranges::find(found, digest) == found.end()) {
      found.push_back(digest);
    }
  }

  answered_ = std::move(found);
  return code_wheel_trouble::none;
}

bool apply_code_wheel_store(machine::machine& box,
                            const code_wheel_store& store) {
  if (!box.seams().have_program() || !store.answered(box.seams().program())) {
    return false;
  }
  box.seams().set_code_wheel_answered(true);
  return true;
}

}  // namespace amberfolio::host
