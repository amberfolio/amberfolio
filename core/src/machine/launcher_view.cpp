// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/launcher_view.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/machine/save_layer.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {

namespace {

/// Whether `path` names the configuration file, in whatever directory.
[[nodiscard]] bool is_configuration(const dos_path& path) noexcept {
  if (path.is_root()) {
    return false;
  }
  const std::span<const char> leaf = path.leaf().text();
  if (leaf.size() != save_layer_config_file.size()) {
    return false;
  }
  for (std::size_t i = 0; i < leaf.size(); ++i) {
    if (leaf[i] != save_layer_config_file[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<std::size_t> configured_sound_offset(
    std::span<const std::uint8_t> bytes) noexcept {
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] != '\n') {
      continue;
    }
    const std::size_t at = i + 1;
    if (at >= bytes.size() || bytes[at] == '\r' || bytes[at] == '\n') {
      return std::nullopt;
    }
    return at;
  }
  return std::nullopt;
}

std::optional<std::uint8_t> read_configured_sound(
    filesystem& fs, const dos_path& current_directory) {
  const vfs_result<dos_path> config = canonicalize(
      current_directory, std::span<const char>(save_layer_config_file.data(),
                                               save_layer_config_file.size()));
  if (!config.ok()) {
    return std::nullopt;
  }
  const vfs_result<file_handle> file =
      fs.open(config.value, open_mode::read_only);
  if (!file.ok()) {
    return std::nullopt;
  }
  std::array<std::uint8_t, launcher_view::max_shown_size> bytes{};
  const vfs_result<std::size_t> got = fs.read(file.value, bytes);
  static_cast<void>(fs.close(file.value));
  if (!got.ok()) {
    return std::nullopt;
  }
  const std::optional<std::size_t> at =
      configured_sound_offset(std::span(bytes.data(), got.value));
  if (!at) {
    return std::nullopt;
  }
  return bytes[*at];
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
  if (!inner.ok() || mode != open_mode::read_only || !is_configuration(path)) {
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
  const std::optional<std::size_t> at = configured_sound_offset(whole);
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
