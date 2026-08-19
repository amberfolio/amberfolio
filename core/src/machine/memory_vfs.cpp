// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/memory_vfs.h"

namespace amberfolio::machine {
namespace {

/// Whether `candidate` is a direct child of `dir` — one component deeper,
/// and everything above that last component equal to `dir`.
[[nodiscard]] bool is_direct_child(const dos_path& dir,
                                   const dos_path& candidate) noexcept {
  return candidate.depth() == dir.depth() + 1 && candidate.parent() == dir;
}

}  // namespace

void memory_filesystem::clear() noexcept {
  // Field by field, not `entries_ = {}`: assigning a freshly
  // aggregate-initialized array would build a temporary holding all 8 MiB
  // of `entries_` before copying it over, on the stack, which is exactly
  // what this header's top comment says never to do. Nothing below reads
  // an entry whose `in_use` is false, so `data` does not need clearing.
  for (auto& e : entries_) {
    e.in_use = false;
    e.is_directory = false;
    e.path = dos_path{};
    e.length = 0;
  }
  handles_ = {};  // No large member here; a plain reset is cheap.
}

memory_filesystem::entry* memory_filesystem::find(
    const dos_path& path) noexcept {
  for (auto& e : entries_) {
    if (e.in_use && e.path == path) {
      return &e;
    }
  }
  return nullptr;
}

const memory_filesystem::entry* memory_filesystem::find(
    const dos_path& path) const noexcept {
  for (const auto& e : entries_) {
    if (e.in_use && e.path == path) {
      return &e;
    }
  }
  return nullptr;
}

bool memory_filesystem::parent_exists(const dos_path& path) const noexcept {
  const dos_path parent = path.parent();
  if (parent.is_root()) {
    return true;
  }
  const entry* p = find(parent);
  return p != nullptr && p->is_directory;
}

vfs_error memory_filesystem::missing_path_error(
    const dos_path& path) const noexcept {
  return parent_exists(path) ? vfs_error::file_not_found
                             : vfs_error::path_not_found;
}

std::size_t memory_filesystem::free_entry_slot() const noexcept {
  for (std::size_t i = 0; i < max_entries; ++i) {
    if (!entries_[i].in_use) {
      return i;
    }
  }
  return max_entries;
}

std::size_t memory_filesystem::free_handle_slot() const noexcept {
  for (std::size_t i = 0; i < max_open_handles; ++i) {
    if (!handles_[i].in_use) {
      return i;
    }
  }
  return max_open_handles;
}

vfs_result<file_handle> memory_filesystem::open(const dos_path& path,
                                                open_mode mode) {
  if (path.is_root()) {
    return {.error = vfs_error::access_denied};
  }
  entry* e = find(path);
  if (e == nullptr) {
    return {.error = missing_path_error(path)};
  }
  if (e->is_directory) {
    return {.error = vfs_error::access_denied};
  }

  const std::size_t slot = free_handle_slot();
  if (slot == max_open_handles) {
    return {.error = vfs_error::too_many_open_files};
  }

  handles_[slot].in_use = true;
  handles_[slot].entry_index = static_cast<std::size_t>(e - entries_.data());
  handles_[slot].position = 0;
  handles_[slot].mode = mode;

  return {.value = {.slot = static_cast<std::uint32_t>(slot)},
          .error = vfs_error::none};
}

vfs_result<file_handle> memory_filesystem::create(const dos_path& path) {
  if (path.is_root()) {
    return {.error = vfs_error::access_denied};
  }

  entry* e = find(path);
  if (e != nullptr) {
    if (e->is_directory) {
      return {.error = vfs_error::access_denied};
    }
    e->length = 0;  // DOS 3Ch: creating an existing file truncates it.
  } else {
    if (!parent_exists(path)) {
      return {.error = vfs_error::path_not_found};
    }
    const std::size_t idx = free_entry_slot();
    if (idx == max_entries) {
      return {.error = vfs_error::directory_full};
    }
    e = &entries_[idx];
    e->in_use = true;
    e->is_directory = false;
    e->path = path;
    e->length = 0;
  }

  const std::size_t slot = free_handle_slot();
  if (slot == max_open_handles) {
    return {.error = vfs_error::too_many_open_files};
  }

  handles_[slot].in_use = true;
  handles_[slot].entry_index = static_cast<std::size_t>(e - entries_.data());
  handles_[slot].position = 0;
  handles_[slot].mode = open_mode::read_write;

  return {.value = {.slot = static_cast<std::uint32_t>(slot)},
          .error = vfs_error::none};
}

vfs_result<std::size_t> memory_filesystem::read(file_handle handle,
                                                std::span<std::uint8_t> out) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return {.error = vfs_error::invalid_handle};
  }
  open_file& h = handles_[handle.slot];
  if (h.mode == open_mode::write_only) {
    return {.error = vfs_error::access_denied};
  }

  entry& e = entries_[h.entry_index];
  const std::uint32_t remaining =
      (h.position < e.length) ? e.length - h.position : 0;
  const std::size_t count =
      (out.size() < remaining) ? out.size() : std::size_t{remaining};

  for (std::size_t i = 0; i < count; ++i) {
    out[i] = e.data[h.position + i];
  }
  h.position += static_cast<std::uint32_t>(count);

  return {.value = count, .error = vfs_error::none};
}

vfs_result<std::size_t> memory_filesystem::write(
    file_handle handle, std::span<const std::uint8_t> in) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return {.error = vfs_error::invalid_handle};
  }
  open_file& h = handles_[handle.slot];
  if (h.mode == open_mode::read_only) {
    return {.error = vfs_error::access_denied};
  }

  entry& e = entries_[h.entry_index];

  // A seek past the old end of file leaves a gap; fill it with zero
  // before writing, so a later read of that gap sees defined bytes and
  // never whatever a previous occupant of this slot left behind
  // (PLAN.md §4: determinism covers what a backend was never told to
  // write, not just what it was).
  if (h.position > e.length) {
    for (std::uint32_t i = e.length; i < h.position; ++i) {
      e.data[i] = 0;
    }
    e.length = h.position;
  }

  const std::uint32_t available =
      (h.position < max_file_size)
          ? static_cast<std::uint32_t>(max_file_size) - h.position
          : 0;
  const std::size_t count =
      (in.size() < available) ? in.size() : std::size_t{available};

  for (std::size_t i = 0; i < count; ++i) {
    e.data[h.position + i] = in[i];
  }
  h.position += static_cast<std::uint32_t>(count);
  if (h.position > e.length) {
    e.length = h.position;
  }

  return {.value = count, .error = vfs_error::none};
}

vfs_result<std::uint32_t> memory_filesystem::seek(file_handle handle,
                                                  seek_origin origin,
                                                  std::int32_t offset) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return {.error = vfs_error::invalid_handle};
  }
  open_file& h = handles_[handle.slot];
  const entry& e = entries_[h.entry_index];

  std::int64_t base = 0;
  switch (origin) {
    case seek_origin::begin:
      base = 0;
      break;
    case seek_origin::current:
      base = h.position;
      break;
    case seek_origin::end:
      base = e.length;
      break;
  }

  constexpr auto capacity = static_cast<std::int64_t>(max_file_size);

  std::int64_t target = base + static_cast<std::int64_t>(offset);
  if (target < 0) {
    target = 0;
  }
  if (target > capacity) {
    target = capacity;
  }
  h.position = static_cast<std::uint32_t>(target);

  return {.value = h.position, .error = vfs_error::none};
}

vfs_error memory_filesystem::truncate(file_handle handle) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return vfs_error::invalid_handle;
  }
  open_file& h = handles_[handle.slot];
  if (h.mode == open_mode::read_only) {
    return vfs_error::access_denied;
  }

  entry& e = entries_[h.entry_index];
  if (h.position > e.length) {
    // The same gap rule write() uses for a seek past the old end of
    // file: a later read of the extended region must see defined zero
    // bytes, never whatever a previous occupant of this slot left behind.
    for (std::uint32_t i = e.length; i < h.position; ++i) {
      e.data[i] = 0;
    }
  }
  e.length = h.position;

  return vfs_error::none;
}

vfs_error memory_filesystem::close(file_handle handle) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return vfs_error::invalid_handle;
  }
  handles_[handle.slot] = open_file{};
  return vfs_error::none;
}

vfs_error memory_filesystem::unlink(const dos_path& path) {
  if (path.is_root()) {
    return vfs_error::access_denied;
  }
  entry* e = find(path);
  if (e == nullptr) {
    return missing_path_error(path);
  }
  if (e->is_directory) {
    return vfs_error::access_denied;
  }

  const auto idx = static_cast<std::size_t>(e - entries_.data());
  for (const auto& h : handles_) {
    if (h.in_use && h.entry_index == idx) {
      // This backend has no "delete on last close"; a caller closes
      // first. Real DOS/FAT of the era did allow deleting an open file —
      // refusing it is a deliberate simplification of a fragile corner
      // no v1 seam (PLAN.md §5) needs.
      return vfs_error::access_denied;
    }
  }

  e->in_use = false;
  e->is_directory = false;
  e->path = dos_path{};
  e->length = 0;

  return vfs_error::none;
}

vfs_error memory_filesystem::mkdir(const dos_path& path) {
  if (path.is_root()) {
    return vfs_error::access_denied;
  }
  if (find(path) != nullptr) {
    return vfs_error::access_denied;
  }
  if (!parent_exists(path)) {
    return vfs_error::path_not_found;
  }
  const std::size_t idx = free_entry_slot();
  if (idx == max_entries) {
    return vfs_error::directory_full;
  }

  entry& e = entries_[idx];
  e.in_use = true;
  e.is_directory = true;
  e.path = path;
  e.length = 0;

  return vfs_error::none;
}

bool memory_filesystem::exists(const dos_path& path) const {
  return path.is_root() || find(path) != nullptr;
}

vfs_result<file_stat> memory_filesystem::stat(const dos_path& path) const {
  if (path.is_root()) {
    return {.value = {.size = 0, .is_directory = true},
            .error = vfs_error::none};
  }
  const entry* e = find(path);
  if (e == nullptr) {
    return {.error = missing_path_error(path)};
  }
  return {.value = {.size = e->length, .is_directory = e->is_directory},
          .error = vfs_error::none};
}

vfs_result<std::size_t> memory_filesystem::entry_count(
    const dos_path& dir) const {
  if (!dir.is_root()) {
    const entry* d = find(dir);
    if (d == nullptr) {
      // Unlike open()/create(), `dir` is never a file being looked up —
      // it is itself a directory in a path, so a missing `dir` is always
      // "the path does not resolve", whether it is the leaf that is
      // missing or something above it. missing_path_error()'s
      // file_not_found/path_not_found split exists for the file case.
      return {.error = vfs_error::path_not_found};
    }
    if (!d->is_directory) {
      return {.error = vfs_error::access_denied};
    }
  }

  std::size_t count = 0;
  for (const auto& e : entries_) {
    if (e.in_use && is_direct_child(dir, e.path)) {
      ++count;
    }
  }
  return {.value = count, .error = vfs_error::none};
}

vfs_result<directory_entry> memory_filesystem::entry_at(
    const dos_path& dir, std::size_t index) const {
  const auto count = entry_count(dir);
  if (!count.ok()) {
    return {.error = count.error};
  }
  if (index >= count.value) {
    return {.error = vfs_error::no_more_files};
  }

  // Selection over the unsorted table: find the smallest name strictly
  // after the last one chosen, `index + 1` times. `entry_count()` already
  // proved the answer exists, so `chosen` is never null by the last
  // iteration. See memory_vfs.h's top comment for why this backend does
  // not keep a maintained sorted index instead.
  const entry* chosen = nullptr;
  bool have_floor = false;
  dos_name floor;

  for (std::size_t step = 0; step <= index; ++step) {
    const entry* best = nullptr;
    for (const auto& e : entries_) {
      if (!e.in_use || !is_direct_child(dir, e.path)) {
        continue;
      }
      if (have_floor && !dos_name_less(floor, e.path.leaf())) {
        continue;  // Not strictly past the floor yet.
      }
      if (best == nullptr || dos_name_less(e.path.leaf(), best->path.leaf())) {
        best = &e;
      }
    }
    chosen = best;
    floor = chosen->path.leaf();
    have_floor = true;
  }

  return {.value = {.name = chosen->path.leaf(),
                    .size = chosen->length,
                    .is_directory = chosen->is_directory},
          .error = vfs_error::none};
}

}  // namespace amberfolio::machine
