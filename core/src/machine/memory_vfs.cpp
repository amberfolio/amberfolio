// SPDX-License-Identifier: AGPL-3.0-only

#include "amberfolio/machine/memory_vfs.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace amberfolio::machine {
namespace {

/// Whether `candidate` is a direct child of `dir` — one component deeper,
/// and everything above that last component equal to `dir`.
[[nodiscard]] bool is_direct_child(const dos_path& dir,
                                   const dos_path& candidate) noexcept {
  return candidate.depth() == dir.depth() + 1 && candidate.parent() == dir;
}

/// Move `count` bytes within `arena` from `from` to `to`, correctly when
/// the two ranges overlap — which, in this file, they always do.
///
/// Written out rather than `std::memmove` for the reason every other loop
/// in core is written out: the direction rule is the whole content of the
/// function and it is worth being able to read it. Right-shifts copy from
/// the end back, left-shifts copy from the start forward; either way, no
/// byte is read after it has been overwritten.
void move_bytes(std::span<std::uint8_t> arena, std::size_t to, std::size_t from,
                std::size_t count) noexcept {
  if (count == 0 || to == from) {
    return;
  }
  if (to > from) {
    for (std::size_t i = count; i > 0; --i) {
      arena[to + i - 1] = arena[from + i - 1];
    }
  } else {
    for (std::size_t i = 0; i < count; ++i) {
      arena[to + i] = arena[from + i];
    }
  }
}

}  // namespace

void memory_filesystem::clear() noexcept {
  // Field by field, not `entries_ = {}`: assigning a freshly
  // aggregate-initialized array would build a temporary of the whole
  // table on the stack. Nothing below reads an entry whose `in_use` is
  // false, so the offsets do not need clearing either.
  for (auto& e : entries_) {
    e.in_use = false;
    e.is_directory = false;
    e.path = dos_path{};
    e.offset = 0;
    e.length = 0;
  }
  handles_ = {};  // No large member here; a plain reset is cheap.

  // The arena's contents are not blanked, for the same reason: nothing
  // can read a byte outside a live file's range, and zeroing 8 MiB to
  // prove it would only cost time. What makes the *next* file's bytes
  // defined is that `write()` fills every gap it opens.
  used_ = 0;

  // An emptied filesystem is a changed one (memory_vfs.h). This is the
  // one route in this tree that changes what a filesystem holds without
  // going through one of the five vfs.h wraps, so it is the one place
  // that has to say so for itself.
  note_change();
}

std::uint32_t memory_filesystem::growth_ceiling(const entry& e) const noexcept {
  const std::size_t room = arena_bytes - used_;
  const std::size_t by_arena = std::size_t{e.length} + room;
  return static_cast<std::uint32_t>((by_arena < max_file_size) ? by_arena
                                                               : max_file_size);
}

bool memory_filesystem::resize(entry& e, std::uint32_t new_length) noexcept {
  if (new_length == e.length) {
    return true;
  }

  // Where the rest of the arena begins today. Everything at or past it
  // belongs to some other file and moves with the change; everything
  // below it is untouched.
  const std::size_t tail_start = std::size_t{e.offset} + e.length;
  const std::size_t tail_length = used_ - tail_start;
  const std::span<std::uint8_t> arena(arena_);

  if (new_length > e.length) {
    const std::size_t grow = new_length - e.length;
    if (grow > arena_bytes - used_) {
      return false;
    }
    move_bytes(arena, tail_start + grow, tail_start, tail_length);
    used_ += grow;
    for (auto& other : entries_) {
      // `&other != &e` matters and is not defensive: a zero-length file
      // has `offset == tail_start` too, so without it a file being grown
      // from empty would move itself along with the tail.
      if (other.in_use && !other.is_directory && &other != &e &&
          other.offset >= tail_start) {
        other.offset += static_cast<std::uint32_t>(grow);
      }
    }
  } else {
    const std::size_t shrink = e.length - new_length;
    move_bytes(arena, tail_start - shrink, tail_start, tail_length);
    used_ -= shrink;
    for (auto& other : entries_) {
      if (other.in_use && !other.is_directory && &other != &e &&
          other.offset >= tail_start) {
        other.offset -= static_cast<std::uint32_t>(shrink);
      }
    }
  }

  e.length = new_length;
  return true;
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

vfs_result<file_handle> memory_filesystem::create_file(const dos_path& path) {
  if (path.is_root()) {
    return {.error = vfs_error::access_denied};
  }

  entry* e = find(path);
  if (e != nullptr && e->is_directory) {
    return {.error = vfs_error::access_denied};
  }
  if (e == nullptr && !parent_exists(path)) {
    return {.error = vfs_error::path_not_found};
  }

  // Every refusal is decided before anything is written, which it was
  // not until M5-C1 (#228): the handle check used to come last, so a
  // create that ran out of handles had already truncated the file it
  // found — or, worse, already claimed an entry, leaving a zero-length
  // file under a name whose creation had just been refused. Nothing
  // could see it, because the caller had an error in hand and no reason
  // to look. `generation()` is what made it worth finding: a failed
  // create moves the counter by nothing, so a failure that changed the
  // disk would be a change no host could be told about.
  //
  // The one visible consequence is which exhaustion is reported when
  // both are exhausted at once — handles now, entries before. Neither
  // caller can act on the difference: DOS reports one error code, and
  // abi.h calls both AF_NO_ROOM.
  const std::size_t slot = free_handle_slot();
  if (slot == max_open_handles) {
    return {.error = vfs_error::too_many_open_files};
  }
  std::size_t idx = max_entries;
  if (e == nullptr) {
    idx = free_entry_slot();
    if (idx == max_entries) {
      return {.error = vfs_error::directory_full};
    }
  }

  if (e != nullptr) {
    // DOS 3Ch: creating an existing file truncates it. Shrinking always
    // fits, so the answer is not checked.
    static_cast<void>(resize(*e, 0));
  } else {
    e = &entries_[idx];
    e->in_use = true;
    e->is_directory = false;
    e->path = path;
    // At the end of the packed region, which is what makes writing to a
    // freshly created file free: there is no tail to move.
    e->offset = static_cast<std::uint32_t>(used_);
    e->length = 0;
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
    out[i] = arena_[e.offset + h.position + i];
  }
  h.position += static_cast<std::uint32_t>(count);

  return {.value = count, .error = vfs_error::none};
}

vfs_result<std::size_t> memory_filesystem::write_file(
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
  // never whatever a previous occupant of the arena left behind
  // (PLAN.md §4: determinism covers what a backend was never told to
  // write, not just what it was). A gap the arena cannot hold is a write
  // of nothing, which is the same short-count answer a full file gives.
  if (h.position > e.length) {
    if (h.position > growth_ceiling(e)) {
      return {.value = 0, .error = vfs_error::none};
    }
    const std::uint32_t gap_from = e.length;
    static_cast<void>(resize(e, h.position));
    for (std::uint32_t i = gap_from; i < h.position; ++i) {
      arena_[e.offset + i] = 0;
    }
  }

  // After the gap, because filling one consumed arena the write itself
  // can no longer have.
  const std::uint32_t ceiling = growth_ceiling(e);
  const std::uint32_t available =
      (h.position < ceiling) ? ceiling - h.position : 0;
  const std::size_t count =
      (in.size() < available) ? in.size() : std::size_t{available};

  const auto needed = static_cast<std::uint32_t>(h.position + count);
  if (needed > e.length && !resize(e, needed)) {
    // Unreachable while `growth_ceiling()` is what bounds `count`, and
    // answered rather than asserted away: a short count is this
    // backend's honest reply to running out of room anywhere else.
    return {.value = 0, .error = vfs_error::none};
  }

  for (std::size_t i = 0; i < count; ++i) {
    arena_[e.offset + h.position + i] = in[i];
  }
  h.position += static_cast<std::uint32_t>(count);

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

vfs_error memory_filesystem::truncate_file(file_handle handle) {
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
    // bytes, never whatever a previous occupant of the arena left behind.
    if (h.position > growth_ceiling(e)) {
      return vfs_error::access_denied;
    }
    const std::uint32_t gap_from = e.length;
    static_cast<void>(resize(e, h.position));
    for (std::uint32_t i = gap_from; i < h.position; ++i) {
      arena_[e.offset + i] = 0;
    }
    return vfs_error::none;
  }

  // Shrinking always fits.
  static_cast<void>(resize(e, h.position));
  return vfs_error::none;
}

vfs_error memory_filesystem::close(file_handle handle) {
  if (handle.slot >= max_open_handles || !handles_[handle.slot].in_use) {
    return vfs_error::invalid_handle;
  }
  handles_[handle.slot] = open_file{};
  return vfs_error::none;
}

vfs_error memory_filesystem::unlink_file(const dos_path& path) {
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

  // The bytes go back to the arena before the entry goes: `resize()`
  // works from `e`'s own offset and length, so an entry cleared first
  // would leave its range stranded inside the packed region forever.
  static_cast<void>(resize(*e, 0));
  e->in_use = false;
  e->is_directory = false;
  e->path = dos_path{};
  e->offset = 0;

  return vfs_error::none;
}

vfs_error memory_filesystem::make_directory(const dos_path& path) {
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
  // A directory owns no arena bytes and is never resized; the offset is
  // set to nothing in particular and read by nothing.
  e.offset = 0;
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
