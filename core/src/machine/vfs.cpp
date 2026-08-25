// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {
namespace {

/// The DOS/FAT short-name character set: letters, digits, and the
/// documented set of special characters a short name may hold. Space and
/// the FAT-reserved punctuation (`" * + , / : ; < = > ? \ [ ] |`) are
/// excluded, matching the ordinary DOS short-name rule found in any DOS
/// technical reference.
[[nodiscard]] constexpr bool is_dos_name_char(char c) noexcept {
  if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9')) {
    return true;
  }
  switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '-':
    case '@':
    case '^':
    case '_':
    case '`':
    case '{':
    case '}':
    case '~':
      return true;
    default:
      return false;
  }
}

[[nodiscard]] constexpr char upper(char c) noexcept {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

}  // namespace

vfs_result<dos_name> dos_name::parse(std::span<const char> raw) noexcept {
  // Find the one dot a legal short name may have; a second one ends this
  // immediately; the DOS FCB name/extension split has no room for more.
  std::size_t dot = raw.size();
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '.') {
      if (dot != raw.size()) {
        return {.error = vfs_error::path_not_found};
      }
      dot = i;
    }
  }

  const std::size_t name_len = dot;
  const std::size_t ext_len = (dot == raw.size()) ? 0 : raw.size() - dot - 1;

  if (name_len == 0 || name_len > 8 || ext_len > 3) {
    return {.error = vfs_error::path_not_found};
  }

  dos_name result;
  std::size_t out = 0;
  for (std::size_t i = 0; i < name_len; ++i) {
    if (!is_dos_name_char(raw[i])) {
      return {.error = vfs_error::path_not_found};
    }
    result.chars_[out++] = upper(raw[i]);
  }
  if (ext_len > 0) {
    result.chars_[out++] = '.';
    for (std::size_t i = 0; i < ext_len; ++i) {
      const char c = raw[dot + 1 + i];
      if (!is_dos_name_char(c)) {
        return {.error = vfs_error::path_not_found};
      }
      result.chars_[out++] = upper(c);
    }
  }
  result.length_ = static_cast<std::uint8_t>(out);

  return {.value = result, .error = vfs_error::none};
}

vfs_result<dos_path> canonicalize(const dos_path& current_directory,
                                  std::span<const char> raw) noexcept {
  std::size_t i = 0;
  dos_path result = current_directory;

  // An optional "X:" drive prefix. There is one drive, so anything but a
  // case-insensitive C is refused outright rather than silently ignored.
  if (raw.size() >= 2 && raw[1] == ':') {
    if (upper(raw[0]) != 'C') {
      return {.error = vfs_error::invalid_drive};
    }
    i = 2;
  }

  // A leading "\" makes this absolute, from the root, whether or not a
  // drive prefix preceded it — "\GAME" and "C:\GAME" mean the same thing.
  if (i < raw.size() && raw[i] == '\\') {
    result = dos_path{};
    ++i;
  }

  while (i < raw.size()) {
    const std::size_t start = i;
    while (i < raw.size() && raw[i] != '\\') {
      ++i;
    }
    const std::size_t len = i - start;
    if (i < raw.size()) {
      ++i;  // Skip the separator; a trailing one produces no component.
    }
    if (len == 0) {
      continue;  // A repeated "\\" collapses rather than naming nothing.
    }

    if (len == 1 && raw[start] == '.') {
      continue;
    }
    if (len == 2 && raw[start] == '.' && raw[start + 1] == '.') {
      result.pop();  // No-op at the root, exactly as real DOS's is.
      continue;
    }

    const auto parsed = dos_name::parse(raw.subspan(start, len));
    if (!parsed.ok()) {
      return {.error = parsed.error};
    }
    if (!result.push(parsed.value)) {
      // Deeper than any Gold Box installation goes (dos_path::max_depth's
      // reasoning); the honest answer is "this does not resolve", the
      // same one an illegal component gets, not a silent truncation.
      return {.error = vfs_error::path_not_found};
    }
  }

  return {.value = result, .error = vfs_error::none};
}

vfs_result<dos_path> canonicalize_host_path(
    std::span<const char> raw) noexcept {
  if (raw.size() > max_host_path_text) {
    return {.error = vfs_error::path_not_found};
  }
  std::array<char, max_host_path_text> folded{};
  for (std::size_t i = 0; i < raw.size(); ++i) {
    folded[i] = raw[i] == '/' ? '\\' : raw[i];
  }
  return canonicalize(dos_path{},
                      std::span<const char>(folded.data(), raw.size()));
}

// --- The whole tree, and one file whole (M5-D2, #170) -------------------

namespace {

/// Walk `dir` depth-first, handing every file to `out` in turn, and
/// answer how many there were in total — including the ones `out` had no
/// room for, so a caller can tell a full listing from a truncated one.
///
/// Recursion, and bounded by construction: `dos_path::max_depth` is
/// eight and `push()` refuses past it, so a directory deeper than that is
/// simply not descended into. Nothing under it could be named by
/// anything this machine can resolve, so there is nothing there a caller
/// could open anyway.
std::size_t walk(const filesystem& fs, dos_path& dir, std::size_t seen,
                 std::span<tree_file> out) {
  const vfs_result<std::size_t> entries = fs.entry_count(dir);
  if (!entries.ok()) {
    return seen;
  }
  for (std::size_t i = 0; i < entries.value; ++i) {
    const vfs_result<directory_entry> entry = fs.entry_at(dir, i);
    if (!entry.ok()) {
      break;
    }
    if (!dir.push(entry.value.name)) {
      // Deeper than a path can be spelled. Nothing under here is
      // reachable by name, so there is nothing here to list.
      continue;
    }
    if (entry.value.is_directory) {
      seen = walk(fs, dir, seen, out);
    } else {
      if (seen < out.size()) {
        out[seen] = {.path = dir, .size = entry.value.size};
      }
      ++seen;
    }
    dir.pop();
  }
  return seen;
}

}  // namespace

std::size_t tree_files(const filesystem& fs, std::span<tree_file> out) {
  dos_path root;
  return walk(fs, root, 0, out);
}

std::size_t tree_file_count(const filesystem& fs) {
  return tree_files(fs, std::span<tree_file>{});
}

vfs_result<std::uint32_t> read_file(filesystem& fs, const dos_path& path,
                                    std::span<std::uint8_t> out) {
  const vfs_result<file_handle> handle = fs.open(path, open_mode::read_only);
  if (!handle.ok()) {
    return {.error = handle.error};
  }
  std::uint32_t total = 0;
  vfs_error failure = vfs_error::none;
  while (total < out.size()) {
    const vfs_result<std::size_t> read =
        fs.read(handle.value, out.subspan(total));
    if (!read.ok()) {
      failure = read.error;
      break;
    }
    if (read.value == 0) {
      // End of file. Zero at the end is success and not an absence
      // (vfs.h's `read()`), so this is the loop's ordinary way out.
      break;
    }
    total += static_cast<std::uint32_t>(read.value);
  }
  static_cast<void>(fs.close(handle.value));
  if (failure != vfs_error::none) {
    return {.error = failure};
  }
  return {.value = total, .error = vfs_error::none};
}

}  // namespace amberfolio::machine
