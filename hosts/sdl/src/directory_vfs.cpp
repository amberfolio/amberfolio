// SPDX-License-Identifier: AGPL-3.0-only

#include "directory_vfs.h"

#include <algorithm>
#include <ios>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace amberfolio::sdl {
namespace {

using machine::directory_entry;
using machine::dos_name;
using machine::dos_path;
using machine::file_handle;
using machine::file_stat;
using machine::open_mode;
using machine::seek_origin;
using machine::vfs_error;
using machine::vfs_result;

/// A `dos_name` as text the host can use. Always ASCII and always short:
/// `dos_name::max_length` is twelve.
[[nodiscard]] std::string to_string(const dos_name& name) {
  const std::span<const char> text = name.text();
  return {text.data(), text.size()};
}

/// The belt-and-braces check this file's top comment describes. A
/// `dos_name` cannot legally contain any of this; if one ever does, the
/// path stops here rather than reaching the host's filesystem.
[[nodiscard]] bool safe_component(const dos_name& name) {
  const std::span<const char> text = name.text();
  if (text.empty()) {
    return false;
  }
  if (text[0] == '.') {
    return false;
  }
  return std::ranges::none_of(text, [](char c) {
    return c == '/' || c == '\\' || c == ':' || c == '\0';
  });
}

/// Map what the host filesystem said onto the interface's error model.
/// Anything this does not recognise becomes `access_denied`, which is the
/// honest answer for "the host refused and DOS has no code for why".
[[nodiscard]] vfs_error from_host(const std::error_code& ec) {
  if (!ec) {
    return vfs_error::none;
  }
  if (ec == std::errc::no_such_file_or_directory) {
    return vfs_error::file_not_found;
  }
  if (ec == std::errc::not_a_directory) {
    return vfs_error::path_not_found;
  }
  return vfs_error::access_denied;
}

}  // namespace

directory_filesystem::directory_filesystem(std::filesystem::path root)
    : root_(std::move(root)) {
  std::error_code ec;
  usable_ = std::filesystem::is_directory(root_, ec) && !ec;
}

std::filesystem::path directory_filesystem::host_path(
    const dos_path& path) const {
  std::filesystem::path result = root_;
  for (std::size_t i = 0; i < path.depth(); ++i) {
    const dos_name& part = path.component(i);
    if (!safe_component(part)) {
      return {};
    }
    result /= to_string(part);
  }
  return result;
}

directory_filesystem::open_file* directory_filesystem::find(
    file_handle handle) noexcept {
  if (handle.slot >= handles_.size()) {
    return nullptr;
  }
  open_file& slot = handles_[handle.slot];
  return slot.in_use ? &slot : nullptr;
}

vfs_result<file_handle> directory_filesystem::open(const dos_path& path,
                                                   open_mode mode) {
  const std::filesystem::path where = host_path(path);
  if (where.empty() || path.depth() == 0) {
    // Depth zero is the root, which vfs.h says is `access_denied`: this
    // interface has no directory handles.
    return {.error = path.depth() == 0 ? vfs_error::access_denied
                                       : vfs_error::path_not_found};
  }

  std::error_code ec;
  if (std::filesystem::is_directory(where, ec)) {
    return {.error = vfs_error::access_denied};
  }
  if (!std::filesystem::exists(where, ec)) {
    return {.error = vfs_error::file_not_found};
  }

  for (std::size_t i = 0; i < handles_.size(); ++i) {
    if (handles_[i].in_use) {
      continue;
    }
    auto flags = std::ios::binary | std::ios::in;
    if (mode != open_mode::read_only) {
      flags |= std::ios::out;
    }
    handles_[i].stream.open(where, flags);
    if (!handles_[i].stream.is_open()) {
      return {.error = vfs_error::access_denied};
    }
    handles_[i].in_use = true;
    handles_[i].writable = mode != open_mode::read_only;
    handles_[i].where = where;
    return {.value = file_handle{.slot = static_cast<std::uint32_t>(i)}};
  }
  return {.error = vfs_error::too_many_open_files};
}

vfs_result<file_handle> directory_filesystem::create(const dos_path& path) {
  const std::filesystem::path where = host_path(path);
  if (path.depth() == 0) {
    return {.error = vfs_error::access_denied};
  }
  if (where.empty()) {
    return {.error = vfs_error::path_not_found};
  }

  std::error_code ec;
  if (std::filesystem::is_directory(where, ec)) {
    return {.error = vfs_error::access_denied};
  }
  if (!std::filesystem::is_directory(where.parent_path(), ec)) {
    return {.error = vfs_error::path_not_found};
  }

  for (std::size_t i = 0; i < handles_.size(); ++i) {
    if (handles_[i].in_use) {
      continue;
    }
    // trunc, because DOS 3Ch truncates an existing file rather than
    // failing (vfs.h): creation is not an error just because the name is
    // taken.
    handles_[i].stream.open(where, std::ios::binary | std::ios::in |
                                       std::ios::out | std::ios::trunc);
    if (!handles_[i].stream.is_open()) {
      return {.error = vfs_error::access_denied};
    }
    handles_[i].in_use = true;
    handles_[i].writable = true;
    handles_[i].where = where;
    return {.value = file_handle{.slot = static_cast<std::uint32_t>(i)}};
  }
  return {.error = vfs_error::too_many_open_files};
}

vfs_result<std::size_t> directory_filesystem::read(
    file_handle handle, std::span<std::uint8_t> out) {
  open_file* file = find(handle);
  if (file == nullptr) {
    return {.error = vfs_error::invalid_handle};
  }
  if (out.empty()) {
    return {.value = 0};
  }

  // clear() first: a previous read that hit end of file leaves eofbit
  // set, and DOS lets a program read again at the end and get zero
  // rather than an error that sticks.
  file->stream.clear();
  file->stream.read(reinterpret_cast<char*>(out.data()),
                    static_cast<std::streamsize>(out.size()));
  const auto got = static_cast<std::size_t>(file->stream.gcount());
  file->stream.clear();
  return {.value = got};
}

vfs_result<std::size_t> directory_filesystem::write(
    file_handle handle, std::span<const std::uint8_t> in) {
  open_file* file = find(handle);
  if (file == nullptr) {
    return {.error = vfs_error::invalid_handle};
  }
  if (!file->writable) {
    return {.error = vfs_error::access_denied};
  }
  if (in.empty()) {
    return {.value = 0};
  }

  file->stream.clear();
  file->stream.write(reinterpret_cast<const char*>(in.data()),
                     static_cast<std::streamsize>(in.size()));
  if (!file->stream) {
    file->stream.clear();
    return {.value = 0};
  }
  file->stream.flush();
  return {.value = in.size()};
}

vfs_result<std::uint32_t> directory_filesystem::seek(file_handle handle,
                                                     seek_origin origin,
                                                     std::int32_t offset) {
  open_file* file = find(handle);
  if (file == nullptr) {
    return {.error = vfs_error::invalid_handle};
  }

  file->stream.clear();
  std::ios::seekdir from = std::ios::beg;
  switch (origin) {
    case seek_origin::begin:
      from = std::ios::beg;
      break;
    case seek_origin::current:
      from = std::ios::cur;
      break;
    case seek_origin::end:
      from = std::ios::end;
      break;
  }

  // Resolve by hand rather than trusting the stream, so the low-end clamp
  // vfs.h specifies is this backend's own decision and not libstdc++'s.
  file->stream.seekg(0, from);
  const std::streamoff base = file->stream.tellg();
  const std::streamoff wanted = base + offset;
  const std::streamoff clamped = wanted < 0 ? 0 : wanted;

  file->stream.clear();
  file->stream.seekg(clamped, std::ios::beg);
  file->stream.seekp(clamped, std::ios::beg);
  return {.value = static_cast<std::uint32_t>(clamped)};
}

vfs_error directory_filesystem::truncate(file_handle handle) {
  open_file* file = find(handle);
  if (file == nullptr) {
    return vfs_error::invalid_handle;
  }
  if (!file->writable) {
    return vfs_error::access_denied;
  }

  // std::fstream cannot resize a file, so the position is taken, the
  // stream closed, the file resized through the filesystem, and the
  // stream reopened at the same place. Ugly, and honest about what the
  // standard library gives us; the alternative is a platform ifdef per
  // desktop target for one call DOS makes rarely.
  const std::streamoff at = file->stream.tellp();
  file->stream.close();

  std::error_code ec;
  std::filesystem::resize_file(
      file->where, static_cast<std::uintmax_t>(at < 0 ? 0 : at), ec);

  file->stream.open(file->where,
                    std::ios::binary | std::ios::in | std::ios::out);
  if (!file->stream.is_open()) {
    file->in_use = false;
    return vfs_error::access_denied;
  }
  file->stream.seekg(at, std::ios::beg);
  file->stream.seekp(at, std::ios::beg);
  return from_host(ec);
}

vfs_error directory_filesystem::close(file_handle handle) {
  open_file* file = find(handle);
  if (file == nullptr) {
    return vfs_error::invalid_handle;
  }
  file->stream.close();
  file->stream.clear();
  file->in_use = false;
  file->writable = false;
  file->where.clear();
  return vfs_error::none;
}

vfs_error directory_filesystem::unlink(const dos_path& path) {
  if (path.depth() == 0) {
    return vfs_error::access_denied;
  }
  const std::filesystem::path where = host_path(path);
  if (where.empty()) {
    return vfs_error::path_not_found;
  }

  std::error_code ec;
  if (std::filesystem::is_directory(where, ec)) {
    return vfs_error::access_denied;
  }
  if (!std::filesystem::exists(where, ec)) {
    return vfs_error::file_not_found;
  }
  // vfs.h: a file with a handle still open on it is refused. There is no
  // delete-on-last-close in this interface; a caller closes first.
  for (const open_file& slot : handles_) {
    if (slot.in_use && slot.where == where) {
      return vfs_error::access_denied;
    }
  }

  std::filesystem::remove(where, ec);
  return from_host(ec);
}

vfs_error directory_filesystem::mkdir(const dos_path& path) {
  if (path.depth() == 0) {
    return vfs_error::access_denied;
  }
  const std::filesystem::path where = host_path(path);
  if (where.empty()) {
    return vfs_error::path_not_found;
  }

  std::error_code ec;
  if (std::filesystem::exists(where, ec)) {
    return vfs_error::access_denied;
  }
  if (!std::filesystem::is_directory(where.parent_path(), ec)) {
    return vfs_error::path_not_found;
  }
  std::filesystem::create_directory(where, ec);
  return from_host(ec);
}

bool directory_filesystem::exists(const dos_path& path) const {
  if (path.depth() == 0) {
    return usable_;
  }
  const std::filesystem::path where = host_path(path);
  if (where.empty()) {
    return false;
  }
  std::error_code ec;
  return std::filesystem::exists(where, ec) && !ec;
}

vfs_result<file_stat> directory_filesystem::stat(const dos_path& path) const {
  if (path.depth() == 0) {
    return {.value = file_stat{.size = 0, .is_directory = true}};
  }
  const std::filesystem::path where = host_path(path);
  if (where.empty()) {
    return {.error = vfs_error::path_not_found};
  }

  std::error_code ec;
  if (std::filesystem::is_directory(where, ec)) {
    return {.value = file_stat{.size = 0, .is_directory = true}};
  }
  if (!std::filesystem::exists(where, ec)) {
    return {.error = vfs_error::file_not_found};
  }
  const std::uintmax_t size = std::filesystem::file_size(where, ec);
  if (ec) {
    return {.error = from_host(ec)};
  }
  return {.value = file_stat{.size = static_cast<std::uint32_t>(size),
                             .is_directory = false}};
}

vfs_result<std::vector<directory_entry>> directory_filesystem::listing(
    const dos_path& dir) const {
  const std::filesystem::path where = host_path(dir);
  if (where.empty()) {
    return {.error = vfs_error::path_not_found};
  }

  std::error_code ec;
  if (!std::filesystem::exists(where, ec)) {
    return {.error = vfs_error::path_not_found};
  }
  if (!std::filesystem::is_directory(where, ec)) {
    return {.error = vfs_error::access_denied};
  }

  std::vector<directory_entry> entries;
  for (const std::filesystem::directory_entry& item :
       std::filesystem::directory_iterator(where, ec)) {
    const std::string name = item.path().filename().string();
    const vfs_result<dos_name> parsed =
        dos_name::parse(std::span<const char>(name.data(), name.size()));
    if (!parsed.ok()) {
      // Not nameable by a program, so not listable either: reporting it
      // would produce a listing with an entry nothing can open.
      continue;
    }

    std::error_code item_ec;
    const bool is_dir = item.is_directory(item_ec);
    const std::uintmax_t size = is_dir ? 0 : item.file_size(item_ec);
    entries.push_back({.name = parsed.value,
                       .size = static_cast<std::uint32_t>(size),
                       .is_directory = is_dir});
  }

  // Pinned order (vfs.h, PLAN.md §4): what the host iterator happened to
  // give us is exactly what must not reach machine state.
  std::ranges::sort(entries,
                    [](const directory_entry& a, const directory_entry& b) {
                      return machine::dos_name_less(a.name, b.name);
                    });
  return {.value = std::move(entries)};
}

vfs_result<std::size_t> directory_filesystem::entry_count(
    const dos_path& dir) const {
  const vfs_result<std::vector<directory_entry>> entries = listing(dir);
  if (!entries.ok()) {
    return {.error = entries.error};
  }
  return {.value = entries.value.size()};
}

vfs_result<directory_entry> directory_filesystem::entry_at(
    const dos_path& dir, std::size_t index) const {
  const vfs_result<std::vector<directory_entry>> entries = listing(dir);
  if (!entries.ok()) {
    return {.error = entries.error};
  }
  if (index >= entries.value.size()) {
    return {.error = vfs_error::no_more_files};
  }
  return {.value = entries.value[index]};
}

}  // namespace amberfolio::sdl
