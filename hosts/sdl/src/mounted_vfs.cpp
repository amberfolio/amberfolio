// SPDX-License-Identifier: AGPL-3.0-only
//
// The wrapper mounted_vfs.h describes.

#include "mounted_vfs.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {

using machine::directory_entry;
using machine::dos_path;
using machine::file_handle;
using machine::file_stat;
using machine::vfs_error;
using machine::vfs_result;

mounted_filesystem::mounted_filesystem(machine::filesystem& inner,
                                       const dos_path& at)
    : inner_(&inner), at_(at) {}

std::optional<dos_path> mounted_filesystem::inside(
    const dos_path& path) const noexcept {
  if (path.depth() < at_.depth()) {
    return std::nullopt;
  }
  for (std::size_t i = 0; i < at_.depth(); ++i) {
    if (!(path.component(i) == at_.component(i))) {
      return std::nullopt;
    }
  }
  dos_path rest;
  for (std::size_t i = at_.depth(); i < path.depth(); ++i) {
    static_cast<void>(rest.push(path.component(i)));
  }
  return rest;
}

bool mounted_filesystem::above(const dos_path& path) const noexcept {
  if (path.depth() >= at_.depth()) {
    return false;
  }
  for (std::size_t i = 0; i < path.depth(); ++i) {
    if (!(path.component(i) == at_.component(i))) {
      return false;
    }
  }
  return true;
}

vfs_error mounted_filesystem::missing(const dos_path& path) const noexcept {
  return !path.is_root() && above(path.parent()) ? vfs_error::file_not_found
                                                 : vfs_error::path_not_found;
}

vfs_result<file_handle> mounted_filesystem::open(const dos_path& path,
                                                 machine::open_mode mode) {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->open(*rest, mode);
  }
  // A directory has no handle to open (vfs.h).
  return {.error = above(path) ? vfs_error::access_denied : missing(path)};
}

vfs_result<std::size_t> mounted_filesystem::read(file_handle handle,
                                                 std::span<std::uint8_t> out) {
  return inner_->read(handle, out);
}

vfs_result<std::uint32_t> mounted_filesystem::seek(file_handle handle,
                                                   machine::seek_origin origin,
                                                   std::int32_t offset) {
  return inner_->seek(handle, origin, offset);
}

vfs_error mounted_filesystem::close(file_handle handle) {
  return inner_->close(handle);
}

bool mounted_filesystem::exists(const dos_path& path) const {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->exists(*rest);
  }
  return above(path);
}

vfs_result<file_stat> mounted_filesystem::stat(const dos_path& path) const {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->stat(*rest);
  }
  if (above(path)) {
    return {.value = {.size = 0, .is_directory = true}};
  }
  return {.error = missing(path)};
}

vfs_result<std::size_t> mounted_filesystem::entry_count(
    const dos_path& dir) const {
  if (const std::optional<dos_path> rest = inside(dir)) {
    return inner_->entry_count(*rest);
  }
  if (above(dir)) {
    return {.value = 1};
  }
  return {.error = vfs_error::path_not_found};
}

vfs_result<directory_entry> mounted_filesystem::entry_at(
    const dos_path& dir, std::size_t index) const {
  if (const std::optional<dos_path> rest = inside(dir)) {
    return inner_->entry_at(*rest, index);
  }
  if (!above(dir)) {
    return {.error = vfs_error::path_not_found};
  }
  if (index != 0) {
    return {.error = vfs_error::no_more_files};
  }
  return {.value = {.name = at_.component(dir.depth()),
                    .size = 0,
                    .is_directory = true}};
}

vfs_result<file_handle> mounted_filesystem::create_file(const dos_path& path) {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->create(*rest);
  }
  // Beside the mount there is nothing behind a name to write into.
  return {.error = above(path) || above(path.parent())
                       ? vfs_error::access_denied
                       : vfs_error::path_not_found};
}

vfs_result<std::size_t> mounted_filesystem::write_file(
    file_handle handle, std::span<const std::uint8_t> in) {
  return inner_->write(handle, in);
}

vfs_error mounted_filesystem::truncate_file(file_handle handle) {
  return inner_->truncate(handle);
}

vfs_error mounted_filesystem::unlink_file(const dos_path& path) {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->unlink(*rest);
  }
  return above(path) ? vfs_error::access_denied : missing(path);
}

vfs_error mounted_filesystem::make_directory(const dos_path& path) {
  if (const std::optional<dos_path> rest = inside(path)) {
    return inner_->mkdir(*rest);
  }
  return above(path) || above(path.parent()) ? vfs_error::access_denied
                                             : vfs_error::path_not_found;
}

}  // namespace amberfolio::sdl
