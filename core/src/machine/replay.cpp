// SPDX-License-Identifier: AGPL-3.0-only
//
// replay.h has the design; this is the grammar, the player and the
// preamble writer. Everything works over caller-owned spans with a
// cursor, allocates nothing and throws nothing (PLAN.md §4).

#include "amberfolio/machine/replay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/state.h"

namespace amberfolio::machine {
namespace {

// --- A bounded writer -------------------------------------------------------
//
// The same shape report.cpp's writer has, for the same reason: one place
// that checks, and a buffer that is always terminated.

class writer {
 public:
  explicit writer(std::span<char> out) noexcept : out_(out) {}

  void put(char c) noexcept {
    if (out_.empty() || used_ + 1 >= out_.size()) {
      overflowed_ = true;
      return;
    }
    out_[used_++] = c;
  }
  void text(std::string_view s) noexcept {
    for (const char c : s) {
      put(c);
    }
  }
  void number(std::uint64_t value) noexcept {
    std::array<char, 20> digits{};
    std::size_t count = 0;
    do {
      digits[count++] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    } while (value != 0);
    while (count > 0) {
      put(digits[--count]);
    }
  }
  /// Exactly `digits` decimal digits, zero-padded: a date field.
  void padded(std::uint32_t value, unsigned digits) noexcept {
    std::array<char, 10> out{};
    for (unsigned i = digits; i > 0; --i) {
      out[i - 1] = static_cast<char>('0' + (value % 10U));
      value /= 10U;
    }
    for (unsigned i = 0; i < digits; ++i) {
      put(out[i]);
    }
  }
  void hex(std::uint64_t value, unsigned digits) noexcept {
    constexpr std::string_view table = "0123456789abcdef";
    for (unsigned i = digits; i > 0; --i) {
      put(table[(value >> ((i - 1) * 4U)) & 0x0FU]);
    }
  }
  void digest(const sha256_digest& d) noexcept {
    for (const std::uint8_t byte : d.bytes) {
      hex(byte, 2);
    }
  }
  [[nodiscard]] std::size_t finish() noexcept {
    if (!out_.empty()) {
      out_[used_] = '\0';
    }
    return overflowed_ ? 0 : used_;
  }
  [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

 private:
  std::span<char> out_;
  std::size_t used_{};
  bool overflowed_{false};
};

// --- A field reader -------------------------------------------------------------

/// Space-separated fields over one line. Each `next()` answers the next
/// field; the rest of the line is `rest()` for the one field that may
/// contain spaces (there is none today, but the shape costs nothing).
class fields {
 public:
  explicit fields(std::span<const char> line) noexcept : line_(line) {}

  [[nodiscard]] bool next(std::string_view& out) noexcept {
    while (at_ < line_.size() && line_[at_] == ' ') {
      ++at_;
    }
    if (at_ >= line_.size()) {
      return false;
    }
    const std::size_t start = at_;
    while (at_ < line_.size() && line_[at_] != ' ') {
      ++at_;
    }
    out = std::string_view(line_.data() + start, at_ - start);
    return true;
  }

  [[nodiscard]] bool exhausted() noexcept {
    std::string_view scratch;
    return !next(scratch);
  }

 private:
  std::span<const char> line_;
  std::size_t at_{};
};

[[nodiscard]] bool parse_number(std::string_view text, std::uint64_t& out,
                                std::uint64_t limit = UINT64_MAX) noexcept {
  if (text.empty() || text.size() > 20) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    const auto digit = static_cast<std::uint64_t>(c - '0');
    if (value > (UINT64_MAX - digit) / 10U) {
      return false;
    }
    value = value * 10U + digit;
  }
  if (value > limit) {
    return false;
  }
  out = value;
  return true;
}

[[nodiscard]] std::uint8_t nibble(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  return 0xFF;
}

[[nodiscard]] bool parse_hex(std::string_view text, std::uint64_t& out,
                             std::size_t digits) noexcept {
  if (text.size() != digits) {
    return false;
  }
  std::uint64_t value = 0;
  for (const char c : text) {
    const std::uint8_t n = nibble(c);
    if (n == 0xFF) {
      return false;
    }
    value = (value << 4U) | n;
  }
  out = value;
  return true;
}

/// `YYYY-MM-DD HH:MM:SS.CC`, as two fields.
[[nodiscard]] bool parse_wall(std::string_view date, std::string_view time,
                              wall_time& out) noexcept {
  const auto digit = [](char c, unsigned& value) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10U + static_cast<unsigned>(c - '0');
    return true;
  };
  if (date.size() != 10 || date[4] != '-' || date[7] != '-' ||
      time.size() != 11 || time[2] != ':' || time[5] != ':' || time[8] != '.') {
    return false;
  }
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  unsigned centi = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    if (!digit(date[i], year)) {
      return false;
    }
  }
  if (!digit(date[5], month) || !digit(date[6], month) ||
      !digit(date[8], day) || !digit(date[9], day) || !digit(time[0], hour) ||
      !digit(time[1], hour) || !digit(time[3], minute) ||
      !digit(time[4], minute) || !digit(time[6], second) ||
      !digit(time[7], second) || !digit(time[9], centi) ||
      !digit(time[10], centi)) {
    return false;
  }
  out = wall_time{.year = static_cast<std::uint16_t>(year),
                  .month = static_cast<std::uint8_t>(month),
                  .day = static_cast<std::uint8_t>(day),
                  .weekday = 0,
                  .hour = static_cast<std::uint8_t>(hour),
                  .minute = static_cast<std::uint8_t>(minute),
                  .second = static_cast<std::uint8_t>(second),
                  .centisecond = static_cast<std::uint8_t>(centi)};
  return true;
}

[[nodiscard]] bool parse_name(std::string_view text, dos_name& out) noexcept {
  const vfs_result<dos_name> parsed =
      dos_name::parse(std::span<const char>(text.data(), text.size()));
  if (!parsed.ok()) {
    return false;
  }
  out = parsed.value;
  return true;
}

/// A line's first field says what it is.
[[nodiscard]] replay_line kind_of(std::string_view word) noexcept {
  if (word == "amberfolio-recording") {
    return replay_line::header;
  }
  if (word == "program") {
    return replay_line::program;
  }
  if (word == "tail") {
    return replay_line::tail;
  }
  if (word == "speed") {
    return replay_line::speed;
  }
  if (word == "seam") {
    return replay_line::seam;
  }
  if (word == "file") {
    return replay_line::file;
  }
  if (word == "wall") {
    return replay_line::wall;
  }
  if (word == "key") {
    return replay_line::key;
  }
  if (word == "checkpoint") {
    return replay_line::checkpoint;
  }
  if (word == "end") {
    return replay_line::end;
  }
  return replay_line::nothing;
}

[[nodiscard]] bool is_event(replay_line kind) noexcept {
  return kind == replay_line::wall || kind == replay_line::key ||
         kind == replay_line::checkpoint || kind == replay_line::end;
}

/// A section name to its index, or `state_section_count` for none.
[[nodiscard]] std::size_t section_index(std::string_view name) noexcept {
  for (std::size_t i = 0; i < state_section_count; ++i) {
    if (name == state_section_name(static_cast<state_section>(i))) {
      return i;
    }
  }
  return state_section_count;
}

}  // namespace

// --- The grammar -------------------------------------------------------------

std::uint64_t section_prefix(const sha256_digest& digest) noexcept {
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    value = (value << 8U) | digest.bytes[i];
  }
  return value;
}

bool parse_replay_line(std::span<const char> line, replay_event& out) noexcept {
  // A trailing CR or LF is not part of the line.
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line = line.first(line.size() - 1);
  }

  out = replay_event{};
  fields f(line);
  std::string_view word;
  if (!f.next(word) || word.front() == '#') {
    out.kind = replay_line::nothing;
    return true;
  }
  out.kind = kind_of(word);

  std::uint64_t n = 0;
  switch (out.kind) {
    case replay_line::nothing:
      return false;

    case replay_line::header: {
      if (!f.next(word) || !parse_number(word, n, UINT32_MAX)) {
        return false;
      }
      out.format_version = static_cast<std::uint32_t>(n);
      if (!f.next(word) || !word.starts_with("state=") ||
          !parse_number(word.substr(6), n, UINT32_MAX)) {
        return false;
      }
      out.state_version = static_cast<std::uint32_t>(n);
      return f.exhausted();
    }

    case replay_line::program: {
      if (!f.next(word) || !parse_name(word, out.name)) {
        return false;
      }
      if (!f.next(word) || !parse_digest(word, out.digest)) {
        return false;
      }
      return f.exhausted();
    }

    case replay_line::tail: {
      // The hex may be absent entirely: an empty tail.
      if (!f.next(word)) {
        return true;
      }
      if (word.size() % 2 != 0 || word.size() / 2 > out.tail.size()) {
        return false;
      }
      for (std::size_t i = 0; i < word.size(); i += 2) {
        const std::uint8_t high = nibble(word[i]);
        const std::uint8_t low = nibble(word[i + 1]);
        if (high == 0xFF || low == 0xFF) {
          return false;
        }
        out.tail[i / 2] = static_cast<char>((high << 4U) | low);
      }
      out.tail_length = word.size() / 2;
      return f.exhausted();
    }

    case replay_line::speed: {
      if (!f.next(word) || !parse_number(word, n, UINT32_MAX) || n == 0) {
        return false;
      }
      out.subticks = static_cast<std::uint32_t>(n);
      return f.exhausted();
    }

    case replay_line::seam: {
      if (!f.next(word) || word.empty() || word.size() > replay_max_id) {
        return false;
      }
      for (std::size_t i = 0; i < word.size(); ++i) {
        out.id[i] = word[i];
      }
      out.id_length = word.size();
      return f.exhausted();
    }

    case replay_line::file: {
      if (!f.next(word) || !parse_name(word, out.name)) {
        return false;
      }
      if (!f.next(word) || !parse_number(word, n, UINT32_MAX)) {
        return false;
      }
      out.size = static_cast<std::uint32_t>(n);
      if (!f.next(word) || !parse_digest(word, out.digest)) {
        return false;
      }
      return f.exhausted();
    }

    case replay_line::wall: {
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.at = n;
      std::string_view date;
      std::string_view time;
      if (!f.next(date) || !f.next(time) || !parse_wall(date, time, out.when)) {
        return false;
      }
      return f.exhausted();
    }

    case replay_line::key: {
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.at = n;
      if (!f.next(word) || !parse_hex(word, n, 2) || n == 0 || n > 0x7F) {
        return false;
      }
      out.scancode = static_cast<std::uint8_t>(n);
      if (!f.next(word)) {
        return false;
      }
      if (word == "down") {
        out.action = key_action::down;
      } else if (word == "up") {
        out.action = key_action::up;
      } else {
        return false;
      }
      return f.exhausted();
    }

    case replay_line::checkpoint: {
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.at = n;
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.steps = n;
      if (!f.next(word) || !parse_digest(word, out.digest)) {
        return false;
      }
      // Optional section fields, each `name=hex16`, any subset, in order.
      while (f.next(word)) {
        const std::size_t eq = word.find('=');
        if (eq == std::string_view::npos) {
          return false;
        }
        const std::size_t index = section_index(word.substr(0, eq));
        if (index == state_section_count ||
            !parse_hex(word.substr(eq + 1), n, 16)) {
          return false;
        }
        out.sections[index] = n;
        out.have_sections = true;
      }
      return true;
    }

    case replay_line::end: {
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.at = n;
      if (!f.next(word) || !parse_number(word, n)) {
        return false;
      }
      out.steps = n;
      return f.exhausted();
    }
  }
  return false;
}

std::size_t format_replay_line(const replay_event& event,
                               std::span<char> out) noexcept {
  writer w(out);
  switch (event.kind) {
    case replay_line::nothing:
      break;
    case replay_line::header:
      w.text("amberfolio-recording ");
      w.number(event.format_version);
      w.text(" state=");
      w.number(event.state_version);
      break;
    case replay_line::program: {
      w.text("program ");
      const std::span<const char> name = event.name.text();
      w.text(std::string_view(name.data(), name.size()));
      w.put(' ');
      w.digest(event.digest);
      break;
    }
    case replay_line::tail:
      w.text("tail");
      if (event.tail_length != 0) {
        w.put(' ');
        for (std::size_t i = 0; i < event.tail_length; ++i) {
          w.hex(static_cast<std::uint8_t>(event.tail[i]), 2);
        }
      }
      break;
    case replay_line::speed:
      w.text("speed ");
      w.number(event.subticks);
      break;
    case replay_line::seam:
      w.text("seam ");
      w.text(event.id_text());
      break;
    case replay_line::file: {
      w.text("file ");
      const std::span<const char> name = event.name.text();
      w.text(std::string_view(name.data(), name.size()));
      w.put(' ');
      w.number(event.size);
      w.put(' ');
      w.digest(event.digest);
      break;
    }
    case replay_line::wall:
      w.text("wall ");
      w.number(event.at);
      w.put(' ');
      w.padded(event.when.year, 4);
      w.put('-');
      w.padded(event.when.month, 2);
      w.put('-');
      w.padded(event.when.day, 2);
      w.put(' ');
      w.padded(event.when.hour, 2);
      w.put(':');
      w.padded(event.when.minute, 2);
      w.put(':');
      w.padded(event.when.second, 2);
      w.put('.');
      w.padded(event.when.centisecond, 2);
      break;
    case replay_line::key:
      w.text("key ");
      w.number(event.at);
      w.put(' ');
      w.hex(event.scancode, 2);
      w.text(event.action == key_action::down ? " down" : " up");
      break;
    case replay_line::checkpoint:
      w.text("checkpoint ");
      w.number(event.at);
      w.put(' ');
      w.number(event.steps);
      w.put(' ');
      w.digest(event.digest);
      if (event.have_sections) {
        for (std::size_t i = 0; i < state_section_count; ++i) {
          w.put(' ');
          w.text(state_section_name(static_cast<state_section>(i)));
          w.put('=');
          w.hex(event.sections[i], 16);
        }
      }
      break;
    case replay_line::end:
      w.text("end ");
      w.number(event.at);
      w.put(' ');
      w.number(event.steps);
      break;
  }
  w.put('\n');
  return w.finish();
}

replay_event checkpoint_of(const machine& box) {
  const state_hashes hashes = hash_state(box);
  replay_event event{};
  event.kind = replay_line::checkpoint;
  event.at = box.time();
  event.steps = box.steps();
  event.digest = hashes.whole;
  event.have_sections = true;
  for (std::size_t i = 0; i < state_section_count; ++i) {
    event.sections[i] = section_prefix(hashes.sections[i]);
  }
  return event;
}

std::size_t write_preamble(const machine& box, filesystem& fs,
                           std::string_view program, std::span<const char> tail,
                           std::span<char> out) {
  std::size_t used = 0;
  const auto emit = [&](const replay_event& event) {
    const std::size_t n =
        format_replay_line(event, out.subspan(used < out.size() ? used : 0));
    if (n == 0) {
      return false;
    }
    used += n;
    return true;
  };

  replay_event event{};
  event.kind = replay_line::header;
  event.format_version = recording_format_version;
  event.state_version = state_format_version;
  if (!emit(event)) {
    return 0;
  }

  if (!box.seams().have_program()) {
    return 0;
  }
  event = replay_event{};
  event.kind = replay_line::program;
  if (!parse_name(program, event.name)) {
    return 0;
  }
  event.digest = box.seams().program();
  if (!emit(event)) {
    return 0;
  }

  event = replay_event{};
  event.kind = replay_line::tail;
  if (tail.size() > event.tail.size()) {
    return 0;
  }
  for (std::size_t i = 0; i < tail.size(); ++i) {
    event.tail[i] = tail[i];
  }
  event.tail_length = tail.size();
  if (!emit(event)) {
    return 0;
  }

  event = replay_event{};
  event.kind = replay_line::speed;
  event.subticks = static_cast<std::uint32_t>(box.step_cost_subticks());
  if (!emit(event)) {
    return 0;
  }

  for (std::size_t i = 0; i < box.seams().enabled_count(); ++i) {
    const std::string_view id = box.seams().enabled_id(i);
    event = replay_event{};
    event.kind = replay_line::seam;
    if (id.size() > replay_max_id) {
      return 0;
    }
    for (std::size_t c = 0; c < id.size(); ++c) {
      event.id[c] = id[c];
    }
    event.id_length = id.size();
    if (!emit(event)) {
      return 0;
    }
  }

  // The manifest: the root, in the pinned order, each file's size and
  // fingerprint. Directories are listed by name and size alone — nothing
  // in this machine's scope puts a program in one, and a digest of a
  // directory is not a thing.
  const vfs_result<std::size_t> count = fs.entry_count(dos_path{});
  if (!count.ok()) {
    return 0;
  }
  for (std::size_t i = 0; i < count.value; ++i) {
    const vfs_result<directory_entry> entry = fs.entry_at(dos_path{}, i);
    if (!entry.ok()) {
      return 0;
    }
    event = replay_event{};
    event.kind = replay_line::file;
    event.name = entry.value.name;
    event.size = entry.value.size;
    if (!entry.value.is_directory) {
      dos_path path;
      path.push(entry.value.name);
      const vfs_result<sha256_digest> digest = fingerprint_file(fs, path);
      if (!digest.ok()) {
        return 0;
      }
      event.digest = digest.value;
    }
    if (!emit(event)) {
      return 0;
    }
  }
  return used;
}

// --- The player ----------------------------------------------------------------

bool replay_player::next_line(replay_event& event) {
  while (cursor_ < text_.size()) {
    std::size_t end = cursor_;
    while (end < text_.size() && text_[end] != '\n') {
      ++end;
    }
    const std::span<const char> line = text_.subspan(cursor_, end - cursor_);
    cursor_ = end < text_.size() ? end + 1 : end;
    ++line_number_;
    if (!parse_replay_line(line, event)) {
      fail(replay_status::malformed, "a line that is not a recording line", 0);
      return false;
    }
    if (event.kind != replay_line::nothing) {
      return true;
    }
  }
  return false;
}

bool replay_player::load(std::span<const char> text) {
  *this = replay_player{};
  text_ = text;

  replay_event event{};
  if (!next_line(event) || event.kind != replay_line::header) {
    fail(replay_status::malformed, "no amberfolio-recording header", 0);
    return false;
  }
  if (event.format_version != recording_format_version) {
    fail(replay_status::malformed,
         "a recording format this player does not read", event.format_version);
    return false;
  }
  if (event.state_version != state_format_version) {
    fail(replay_status::malformed,
         "a state layout this build does not hash; re-record",
         event.state_version);
    return false;
  }
  preamble_.format_version = event.format_version;
  preamble_.state_version = event.state_version;

  bool have_program = false;
  for (;;) {
    const std::size_t before = cursor_;
    const std::size_t before_line = line_number_;
    if (!next_line(event)) {
      if (status_ == replay_status::malformed) {
        return false;
      }
      break;  // A recording with no events: the preamble is all of it.
    }
    if (is_event(event.kind)) {
      // Put it back: the first event is `apply()`'s.
      cursor_ = before;
      line_number_ = before_line;
      break;
    }
    switch (event.kind) {
      case replay_line::program:
        preamble_.program = event.name;
        preamble_.program_digest = event.digest;
        have_program = true;
        break;
      case replay_line::tail:
        preamble_.tail = event.tail;
        preamble_.tail_length = event.tail_length;
        break;
      case replay_line::speed:
        preamble_.subticks = event.subticks;
        preamble_.have_speed = true;
        break;
      case replay_line::seam:
        if (preamble_.seam_count == replay_preamble::max_seams) {
          fail(replay_status::malformed, "more seams than a recording may name",
               0);
          return false;
        }
        preamble_.seams[preamble_.seam_count] = event.id;
        preamble_.seam_lengths[preamble_.seam_count] = event.id_length;
        ++preamble_.seam_count;
        break;
      case replay_line::file:
        if (preamble_.file_count == replay_preamble::max_files) {
          fail(replay_status::malformed, "more files than a recording may name",
               0);
          return false;
        }
        preamble_.files[preamble_.file_count] = {
            .name = event.name, .size = event.size, .digest = event.digest};
        ++preamble_.file_count;
        break;
      case replay_line::header:
        fail(replay_status::malformed, "a second header", 0);
        return false;
      case replay_line::nothing:
      case replay_line::wall:
      case replay_line::key:
      case replay_line::checkpoint:
      case replay_line::end:
        break;
    }
  }
  if (!have_program) {
    fail(replay_status::malformed, "no program line", 0);
    return false;
  }
  status_ = replay_status::ok;
  return true;
}

replay_status replay_player::check_initial(const machine& box, filesystem* fs) {
  if (status_ != replay_status::ok) {
    return status_;
  }
  if (!box.seams().have_program() ||
      !(box.seams().program() == preamble_.program_digest)) {
    fail(replay_status::malformed, "the program loaded is not the one recorded",
         0);
    return status_;
  }
  if (preamble_.have_speed && box.step_cost_subticks() != preamble_.subticks) {
    fail(replay_status::malformed, "the speed is not the one recorded",
         box.step_cost_subticks());
    return status_;
  }
  // The seam set, exactly: every recorded seam on, and nothing else on.
  if (box.seams().enabled_count() != preamble_.seam_count) {
    fail(replay_status::malformed, "the seams on are not the ones recorded",
         box.seams().enabled_count());
    return status_;
  }
  for (std::size_t i = 0; i < preamble_.seam_count; ++i) {
    const seam_status row = box.seams().status(preamble_.seam(i));
    if (row.state != seam_state::on) {
      fail(replay_status::malformed, "a recorded seam is not on", i);
      return status_;
    }
  }
  if (fs == nullptr) {
    return status_;
  }
  const vfs_result<std::size_t> count = fs->entry_count(dos_path{});
  if (!count.ok() || count.value != preamble_.file_count) {
    fail(replay_status::malformed,
         "the filesystem holds a different number of files",
         count.ok() ? count.value : 0);
    return status_;
  }
  for (std::size_t i = 0; i < preamble_.file_count; ++i) {
    const vfs_result<directory_entry> entry = fs->entry_at(dos_path{}, i);
    const replay_preamble::file_entry& want = preamble_.files[i];
    if (!entry.ok() || !(entry.value.name == want.name) ||
        entry.value.size != want.size) {
      fail(replay_status::malformed, "a file's name or size is not as recorded",
           i);
      return status_;
    }
    if (!entry.value.is_directory) {
      dos_path path;
      path.push(entry.value.name);
      const vfs_result<sha256_digest> digest = fingerprint_file(*fs, path);
      if (!digest.ok() || !(digest.value == want.digest)) {
        fail(replay_status::malformed,
             "a file's fingerprint is not as recorded", i);
        if (digest.ok()) {
          have_digests_ = true;
          expected_ = want.digest;
          actual_ = digest.value;
        }
        return status_;
      }
    }
  }
  return status_;
}

bool replay_player::peek() {
  if (have_pending_) {
    return true;
  }
  if (status_ != replay_status::ok) {
    return false;
  }
  if (!next_line(pending_)) {
    return false;
  }
  if (!is_event(pending_.kind)) {
    fail(replay_status::malformed,
         "an initial-condition line after the stream began", 0);
    return false;
  }
  have_pending_ = true;
  return true;
}

ticks replay_player::next_tick() const noexcept {
  if (status_ != replay_status::ok) {
    return never;
  }
  // `peek()` is not const; a player that has not peeked yet answers the
  // tick only after `apply()` has looked. Callers go through `apply()`
  // once at the start (at tick 0) to prime it, which every host's loop
  // does naturally: run to nothing, apply, ask.
  return have_pending_ ? pending_.at : never;
}

replay_status replay_player::apply(machine& box) {
  if (status_ != replay_status::ok) {
    return status_;
  }
  for (;;) {
    if (!peek()) {
      if (status_ == replay_status::ok) {
        // Text ran out without an `end`: a recording cut short. Not a
        // divergence of the machine's — the recording is incomplete —
        // and reported as malformed so the two are told apart.
        fail(replay_status::malformed, "the recording has no end line",
             box.time());
      }
      return status_;
    }
    const ticks now = box.time();
    if (pending_.at > now) {
      return status_;  // Not yet; the host runs on.
    }
    if (pending_.at < now) {
      // The host ran past an event's tick. Every event has to land on the
      // exact tick it was recorded at, or the run is not the run.
      fail(replay_status::diverged, "the machine ran past an event's tick",
           pending_.at);
      return status_;
    }

    switch (pending_.kind) {
      case replay_line::wall:
        if (!box.set_wall_time(pending_.when)) {
          fail(replay_status::malformed, "a wall seed that is not a real date",
               pending_.at);
          return status_;
        }
        break;
      case replay_line::key:
        box.post_key(pending_.scancode, pending_.action);
        ++keys_;
        break;
      case replay_line::checkpoint: {
        if (box.steps() != pending_.steps) {
          fail(replay_status::diverged,
               "the step count is not the one recorded", box.steps());
          return status_;
        }
        const state_hashes hashes = hash_state(box);
        if (!(hashes.whole == pending_.digest)) {
          // Name the section, when the line carried them.
          std::string_view which = "state";
          if (pending_.have_sections) {
            for (std::size_t i = 0; i < state_section_count; ++i) {
              if (section_prefix(hashes.sections[i]) != pending_.sections[i]) {
                which = state_section_name(static_cast<state_section>(i));
                break;
              }
            }
          }
          fail_hash(which, pending_.digest, hashes.whole);
          return status_;
        }
        ++checkpoints_;
        break;
      }
      case replay_line::end:
        if (box.steps() != pending_.steps) {
          fail(replay_status::diverged,
               "the step count at the end is not the one recorded",
               box.steps());
          return status_;
        }
        status_ = replay_status::done;
        have_pending_ = false;
        return status_;
      case replay_line::header:
      case replay_line::program:
      case replay_line::tail:
      case replay_line::speed:
      case replay_line::seam:
      case replay_line::file:
      case replay_line::nothing:
        break;
    }
    have_pending_ = false;
  }
}

void replay_player::fail(replay_status status, std::string_view what,
                         std::uint64_t at) {
  status_ = status;
  what_length_ = what.size() < what_.size() ? what.size() : what_.size() - 1;
  for (std::size_t i = 0; i < what_length_; ++i) {
    what_[i] = what[i];
  }
  failed_line_ = line_number_;
  failed_at_ = at;
  have_digests_ = false;
}

void replay_player::fail_hash(std::string_view section,
                              const sha256_digest& expected,
                              const sha256_digest& actual) {
  fail(replay_status::diverged, section, pending_.at);
  have_digests_ = true;
  expected_ = expected;
  actual_ = actual;
}

std::size_t replay_player::report(std::span<char> out) const noexcept {
  writer w(out);
  w.text("amberfolio: replay ");
  switch (status_) {
    case replay_status::ok:
      w.text("in progress checkpoints=");
      w.number(checkpoints_);
      w.text(" keys=");
      w.number(keys_);
      break;
    case replay_status::done:
      w.text("verified checkpoints=");
      w.number(checkpoints_);
      w.text(" keys=");
      w.number(keys_);
      break;
    case replay_status::diverged:
      if (have_digests_) {
        w.text("diverged line=");
        w.number(failed_line_);
        w.text(" tick=");
        w.number(failed_at_);
        w.text(" section=");
        w.text(std::string_view(what_.data(), what_length_));
        w.text(" expected=");
        w.digest(expected_);
        w.text(" actual=");
        w.digest(actual_);
      } else {
        w.text("diverged line=");
        w.number(failed_line_);
        w.text(" at=");
        w.number(failed_at_);
        w.text(" why=");
        w.text(std::string_view(what_.data(), what_length_));
      }
      w.text(" checkpoints=");
      w.number(checkpoints_);
      break;
    case replay_status::malformed:
      w.text("refused line=");
      w.number(failed_line_);
      w.text(" why=");
      w.text(std::string_view(what_.data(), what_length_));
      if (failed_at_ != 0) {
        w.text(" value=");
        w.number(failed_at_);
      }
      if (have_digests_) {
        w.text(" expected=");
        w.digest(expected_);
        w.text(" actual=");
        w.digest(actual_);
      }
      break;
  }
  w.put('\n');
  return w.finish();
}

}  // namespace amberfolio::machine
