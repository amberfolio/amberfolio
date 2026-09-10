// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop config file. `desktop_config.h` has the reasoning and the
// format; the parsing is deliberately unforgiving, for
// `code_wheel_store.cpp`'s reason and one more — a player edits this one
// by hand, so a line it cannot read has to come back as that line and
// not as a shrug.

#include "desktop_config.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace amberfolio::sdl {
namespace {

constexpr std::string_view key_game_directory = "game-directory";
constexpr std::string_view key_program = "program";
constexpr std::string_view key_seam = "seam";
constexpr std::string_view key_journal_ocr = "journal-ocr";
constexpr std::string_view key_volume = "volume";
constexpr std::string_view key_mute = "mute";
constexpr std::string_view key_speed = "speed";
constexpr std::string_view key_scale = "scale";
constexpr std::string_view key_save_sidecars = "save-sidecars";

/// A line, and past its newline. False at the end of the text: every
/// line this format has ends with one, and a file whose last line does
/// not is a file that was cut off.
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

/// `amberfolio-config <version>`, and the version it named.
[[nodiscard]] bool take_header(std::string_view line,
                               std::uint32_t& version) noexcept {
  if (!line.starts_with(desktop_config_magic)) {
    return false;
  }
  std::string_view rest = line.substr(desktop_config_magic.size());
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

/// The key and the rest of the line. A line with no space is all key and
/// an empty value, which every key here refuses — there is no setting
/// whose answer is nothing.
void split_line(std::string_view line, std::string_view& key,
                std::string_view& value) noexcept {
  const std::size_t space = line.find(' ');
  if (space == std::string_view::npos) {
    key = line;
    value = {};
    return;
  }
  key = line.substr(0, space);
  value = line.substr(space + 1);
}

/// A whole non-negative number, and nothing else. `strtoul` is not used
/// for `code_wheel_store.cpp`'s reason: it accepts leading spaces, a
/// sign and a trailing anything, and every one of those would be this
/// parser guessing.
[[nodiscard]] bool take_number(std::string_view text, unsigned& out) noexcept {
  if (text.empty() || text.size() > 9) {
    return false;
  }
  unsigned value = 0;
  for (const char digit : text) {
    if (digit < '0' || digit > '9') {
      return false;
    }
    value = (value * 10U) + static_cast<unsigned>(digit - '0');
  }
  out = value;
  return true;
}

/// `on` or `off`, and nothing that reads like either without being one.
[[nodiscard]] bool take_switch(std::string_view text, bool& out) noexcept {
  if (text == "on") {
    out = true;
    return true;
  }
  if (text == "off") {
    out = false;
    return true;
  }
  return false;
}

/// The speed names `--speed` takes. Checked here rather than left to the
/// host so that a config naming a machine that does not exist is refused
/// by the reading that found it, with the line in hand.
[[nodiscard]] bool known_speed(std::string_view text) noexcept {
  return text == "xt" || text == "turbo" || text == "at" || text == "386";
}

/// A `key value` line into `into`. `config_trouble::none` for a line
/// that was understood.
[[nodiscard]] config_trouble take_setting(std::string_view key,
                                          std::string_view value,
                                          desktop_config& into) {
  if (value.empty()) {
    // Every key here answers something. A bare key is a line somebody
    // meant to finish.
    return config_trouble::bad_value;
  }
  if (key == key_game_directory) {
    into.game_directory = std::string(value);
  } else if (key == key_program) {
    into.program = std::string(value);
  } else if (key == key_seam) {
    if (!into.seams.has_value()) {
      into.seams.emplace();
    }
    into.seams->emplace_back(value);
  } else if (key == key_journal_ocr) {
    into.journal_ocr = std::string(value);
  } else if (key == key_volume) {
    unsigned percent = 0;
    if (!take_number(value, percent) || percent > 100) {
      return config_trouble::bad_value;
    }
    into.volume_percent = percent;
  } else if (key == key_mute) {
    bool on = false;
    if (!take_switch(value, on)) {
      return config_trouble::bad_value;
    }
    into.muted = on;
  } else if (key == key_speed) {
    if (!known_speed(value)) {
      return config_trouble::bad_value;
    }
    into.speed = std::string(value);
  } else if (key == key_scale) {
    unsigned scale = 0;
    if (!take_number(value, scale) || scale == 0) {
      return config_trouble::bad_value;
    }
    into.scale = scale;
  } else if (key == key_save_sidecars) {
    bool on = false;
    if (!take_switch(value, on)) {
      return config_trouble::bad_value;
    }
    into.save_sidecars = on;
  } else {
    return config_trouble::unknown_key;
  }
  return config_trouble::none;
}

/// CRLF out, in one pass. A player may have opened this in whatever
/// their platform calls Notepad, and a carriage return on the end of
/// `speed at` would otherwise be a speed this build does not know.
[[nodiscard]] std::string without_carriage_returns(std::string_view whole) {
  std::string out;
  out.reserve(whole.size());
  for (const char one : whole) {
    if (one != '\r') {
      out += one;
    }
  }
  return out;
}

void put(std::string& out, std::string_view key, std::string_view value) {
  out += key;
  out += ' ';
  out += value;
  out += '\n';
}

}  // namespace

const char* config_trouble_name(config_trouble why) noexcept {
  switch (why) {
    case config_trouble::none:
      return "ok";
    case config_trouble::not_a_config:
      return "not-a-config";
    case config_trouble::later_version:
      return "later-version";
    case config_trouble::unknown_key:
      return "unknown-key";
    case config_trouble::bad_value:
      return "bad-value";
  }
  return "unknown";
}

bool desktop_config::empty() const noexcept {
  return !game_directory.has_value() && !program.has_value() &&
         !seams.has_value() && !journal_ocr.has_value() &&
         !volume_percent.has_value() && !muted.has_value() &&
         !speed.has_value() && !scale.has_value() && !save_sidecars.has_value();
}

std::string desktop_config::serialize() const {
  std::string out(desktop_config_magic);
  out += ' ';
  out += std::to_string(desktop_config_version);
  out += '\n';
  if (game_directory.has_value()) {
    put(out, key_game_directory, *game_directory);
  }
  if (program.has_value()) {
    put(out, key_program, *program);
  }
  if (seams.has_value()) {
    for (const std::string& one : *seams) {
      put(out, key_seam, one);
    }
  }
  if (journal_ocr.has_value()) {
    put(out, key_journal_ocr, *journal_ocr);
  }
  if (volume_percent.has_value()) {
    put(out, key_volume, std::to_string(*volume_percent));
  }
  if (muted.has_value()) {
    put(out, key_mute, *muted ? "on" : "off");
  }
  if (speed.has_value()) {
    put(out, key_speed, *speed);
  }
  if (scale.has_value()) {
    put(out, key_scale, std::to_string(*scale));
  }
  if (save_sidecars.has_value()) {
    put(out, key_save_sidecars, *save_sidecars ? "on" : "off");
  }
  return out;
}

config_reading desktop_config::parse(std::string_view whole) {
  const std::string text = without_carriage_returns(whole);
  std::size_t at = 0;
  std::string_view line;
  if (!take_line(text, at, line)) {
    return {.why = config_trouble::not_a_config, .line = 1, .text = {}};
  }
  std::uint32_t version = 0;
  if (!take_header(line, version)) {
    return {.why = config_trouble::not_a_config,
            .line = 1,
            .text = std::string(line)};
  }
  if (version > desktop_config_version) {
    return {.why = config_trouble::later_version,
            .line = 1,
            .text = std::string(line)};
  }
  if (version < desktop_config_oldest_version) {
    return {.why = config_trouble::not_a_config,
            .line = 1,
            .text = std::string(line)};
  }

  // Read into a fresh config and swap at the end: a file that turned out
  // to be malformed half-way must leave this object exactly as it was,
  // which is what makes a refusal a clean start on the defaults rather
  // than a start on half of somebody's answers.
  desktop_config found;
  std::size_t number = 1;
  while (at < text.size()) {
    ++number;
    if (!take_line(text, at, line)) {
      return {.why = config_trouble::bad_value,
              .line = number,
              .text = std::string(text.substr(at))};
    }
    std::string_view key;
    std::string_view value;
    split_line(line, key, value);
    if (const config_trouble why = take_setting(key, value, found);
        why != config_trouble::none) {
      return {.why = why, .line = number, .text = std::string(line)};
    }
  }

  *this = std::move(found);
  return {};
}

}  // namespace amberfolio::sdl
