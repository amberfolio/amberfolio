// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/launcher_view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

namespace {

/// The configuration `path` names, in whatever directory, or null.
[[nodiscard]] const launcher_configuration* configuration_of(
    const dos_path& path) noexcept {
  if (path.is_root()) {
    return nullptr;
  }
  const std::span<const char> leaf = path.leaf().text();
  return find_configuration(std::string_view(leaf.data(), leaf.size()));
}

}  // namespace

std::optional<std::size_t> configured_line_offset(
    std::span<const std::uint8_t> bytes, std::size_t line) noexcept {
  if (line == 0) {
    return std::nullopt;
  }
  std::size_t at = 0;
  std::size_t breaks = line - 1;
  for (std::size_t i = 0; i < bytes.size() && breaks > 0; ++i) {
    if (bytes[i] == '\n') {
      --breaks;
      at = i + 1;
    }
  }
  if (breaks > 0 || at >= bytes.size() || bytes[at] == '\r' ||
      bytes[at] == '\n') {
    return std::nullopt;
  }
  return at;
}

found_configuration find_configuration_in(const filesystem& fs,
                                          const dos_path& directory) {
  for (const edition& known : known_editions()) {
    const std::string_view name = known.configuration.file;
    const vfs_result<dos_path> path = canonicalize(
        directory, std::span<const char>(name.data(), name.size()));
    if (path.ok() && fs.exists(path.value)) {
      return {.layout = &known.configuration, .path = path.value};
    }
  }
  return {};
}

std::optional<configured_sound> read_configured_sound(
    filesystem& fs, const dos_path& current_directory) {
  const found_configuration config =
      find_configuration_in(fs, current_directory);
  if (!config.found()) {
    return std::nullopt;
  }
  const vfs_result<file_handle> file =
      fs.open(config.path, open_mode::read_only);
  if (!file.ok()) {
    return std::nullopt;
  }
  std::array<std::uint8_t, launcher_view::max_shown_size> bytes{};
  const vfs_result<std::size_t> got = fs.read(file.value, bytes);
  static_cast<void>(fs.close(file.value));
  if (!got.ok()) {
    return std::nullopt;
  }
  const std::optional<std::size_t> at = configured_line_offset(
      std::span(bytes.data(), got.value), config.layout->sound_line);
  if (!at) {
    return std::nullopt;
  }
  return configured_sound{.file = config.layout->file, .letter = bytes[*at]};
}

launcher_view::shown_file* launcher_view::shown_of(
    file_handle handle) noexcept {
  if (handle.slot < first_shown_slot ||
      handle.slot - first_shown_slot >= max_shown_files) {
    return nullptr;
  }
  shown_file& file = shown_files_[handle.slot - first_shown_slot];
  return file.open ? &file : nullptr;
}

vfs_result<file_handle> launcher_view::open(const dos_path& path,
                                            open_mode mode) {
  const vfs_result<file_handle> inner = inner_->open(path, mode);
  const launcher_configuration* layout = configuration_of(path);
  if (!inner.ok() || mode != open_mode::read_only || layout == nullptr) {
    return inner;
  }

  // Read it whole, one byte past the largest size this answers for, so a
  // file that fills the buffer is known to be too big rather than cut.
  std::array<std::uint8_t, max_shown_size + 1> bytes{};
  const vfs_result<std::size_t> got = inner_->read(inner.value, bytes);
  if (!got.ok() || got.value > max_shown_size) {
    static_cast<void>(inner_->seek(inner.value, seek_origin::begin, 0));
    return inner;
  }
  const std::span<const std::uint8_t> whole(bytes.data(), got.value);
  const std::optional<std::size_t> at =
      configured_line_offset(whole, layout->sound_line);
  if (!at || bytes[*at] == sound_for_this_machine) {
    static_cast<void>(inner_->seek(inner.value, seek_origin::begin, 0));
    return inner;
  }

  for (std::size_t i = 0; i < max_shown_files; ++i) {
    shown_file& file = shown_files_[i];
    if (file.open) {
      continue;
    }
    static_cast<void>(inner_->close(inner.value));
    file.open = true;
    file.size = got.value;
    file.position = 0;
    for (std::size_t b = 0; b < got.value; ++b) {
      file.bytes[b] = bytes[b];
    }
    file.bytes[*at] = sound_for_this_machine;
    ++shown_count_;
    return {
        .value = {.slot = first_shown_slot + static_cast<std::uint32_t>(i)}};
  }
  static_cast<void>(inner_->close(inner.value));
  return {.error = vfs_error::too_many_open_files};
}

vfs_result<std::size_t> launcher_view::read(file_handle handle,
                                            std::span<std::uint8_t> out) {
  shown_file* file = shown_of(handle);
  if (file == nullptr) {
    return inner_->read(handle, out);
  }
  std::size_t taken = 0;
  while (taken < out.size() && file->position < file->size) {
    out[taken] = file->bytes[file->position];
    ++file->position;
    ++taken;
  }
  return {.value = taken};
}

vfs_result<std::uint32_t> launcher_view::seek(file_handle handle,
                                              seek_origin origin,
                                              std::int32_t offset) {
  shown_file* file = shown_of(handle);
  if (file == nullptr) {
    return inner_->seek(handle, origin, offset);
  }
  std::int64_t base = 0;
  if (origin == seek_origin::current) {
    base = file->position;
  } else if (origin == seek_origin::end) {
    base = static_cast<std::int64_t>(file->size);
  }
  // Clamped at zero (vfs.h) and at the file's end: this copy has no room
  // past it, and nothing can be written through a read-only handle to
  // make any.
  const auto end = static_cast<std::int64_t>(file->size);
  file->position = static_cast<std::uint32_t>(
      std::clamp(base + offset, std::int64_t{0}, end));
  return {.value = file->position};
}

vfs_error launcher_view::close(file_handle handle) {
  shown_file* file = shown_of(handle);
  if (file == nullptr) {
    return inner_->close(handle);
  }
  file->open = false;
  return vfs_error::none;
}

bool launcher_view::exists(const dos_path& path) const {
  return inner_->exists(path);
}

vfs_result<file_stat> launcher_view::stat(const dos_path& path) const {
  return inner_->stat(path);
}

vfs_result<std::size_t> launcher_view::entry_count(const dos_path& dir) const {
  return inner_->entry_count(dir);
}

vfs_result<directory_entry> launcher_view::entry_at(const dos_path& dir,
                                                    std::size_t index) const {
  return inner_->entry_at(dir, index);
}

vfs_result<file_handle> launcher_view::create_file(const dos_path& path) {
  return inner_->create(path);
}

vfs_result<std::size_t> launcher_view::write_file(
    file_handle handle, std::span<const std::uint8_t> in) {
  if (shown_of(handle) != nullptr) {
    return {.error = vfs_error::access_denied};
  }
  return inner_->write(handle, in);
}

vfs_error launcher_view::truncate_file(file_handle handle) {
  if (shown_of(handle) != nullptr) {
    return vfs_error::access_denied;
  }
  return inner_->truncate(handle);
}

vfs_error launcher_view::unlink_file(const dos_path& path) {
  return inner_->unlink(path);
}

vfs_error launcher_view::make_directory(const dos_path& path) {
  return inner_->mkdir(path);
}

}  // namespace amberfolio::machine
