// SPDX-License-Identifier: AGPL-3.0-only
//
// The exploration sidecar, written and read. `automap_store.h` has the
// reasoning; this is the file handling under it.

#include "amberfolio/host/automap_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::host {
namespace {

/// The stem of the program's own save slots. A path is one of them when
/// it is `SAVE\SAVGAM<L>.DAT` — the letter is what this is here to read,
/// and everything else about the traffic is the program's business.
constexpr std::string_view slot_stem = "SAVGAM";
constexpr std::string_view slot_extension = ".DAT";

/// The whole of one component's text, as characters. A `dos_name` is
/// already canonical and upper-cased, so nothing here folds case.
[[nodiscard]] std::string_view text_of(const machine::dos_name& name) noexcept {
  const std::span<const char> chars = name.text();
  return {chars.data(), chars.size()};
}

/// Build a path under `SAVE\` from a leaf name. Through
/// `canonicalize_host_path` and not by hand, because DOS name semantics
/// are core's alone (#146) and a host with its own opinion about what a
/// path means is a second opinion.
[[nodiscard]] machine::vfs_result<machine::dos_path> under_save(
    std::span<const char> leaf) noexcept {
  std::array<char, machine::max_host_path_text> raw{};
  std::size_t used = 0;
  for (const char ch : automap_store_directory) {
    raw[used++] = ch;
  }
  raw[used++] = '\\';
  for (const char ch : leaf) {
    raw[used++] = ch;
  }
  return machine::canonicalize_host_path({raw.data(), used});
}

/// One reason, refused by the filesystem, recorded with what it said.
void refused(automap_trouble& trouble, machine::vfs_error& refusal,
             machine::vfs_error what) noexcept {
  trouble = automap_trouble::file_refused;
  refusal = what;
}

}  // namespace

const char* automap_trouble_name(automap_trouble what) noexcept {
  switch (what) {
    case automap_trouble::file_refused:
      return "file-refused";
    case automap_trouble::out_of_room:
      return "out-of-room";
    case automap_trouble::not_a_sidecar:
      return "not-a-sidecar";
    case automap_trouble::none:
      break;
  }
  return "none";
}

char automap_store::slot_of(const machine::dos_path& path) noexcept {
  if (path.depth() != 2) {
    return 0;
  }
  if (text_of(path.component(0)) != automap_store_directory) {
    return 0;
  }
  const std::string_view leaf = text_of(path.leaf());
  if (leaf.size() != slot_stem.size() + 1 + slot_extension.size()) {
    return 0;
  }
  if (!leaf.starts_with(slot_stem) || !leaf.ends_with(slot_extension)) {
    return 0;
  }
  const char letter = leaf[slot_stem.size()];
  return (letter >= 'A' && letter <= 'Z') ? letter : 0;
}

machine::dos_path automap_store::slot_path(char letter) noexcept {
  // `AFMAP<L>.DAT`: the working table's name with the letter spliced in,
  // which keeps every one of these files sorting together in a directory
  // listing beside the saves they belong to.
  const std::array<char, 12> leaf{'A',    'F', 'M', 'A', 'P',
                                  letter, '.', 'D', 'A', 'T'};
  const machine::vfs_result<machine::dos_path> where =
      under_save({leaf.data(), 10});
  return where.ok() ? where.value : machine::dos_path{};
}

void automap_store::attach(machine::machine& box) {
  box_ = &box;
  if (!enabled_) {
    return;
  }
  const machine::vfs_result<machine::dos_path> working =
      machine::canonicalize_host_path(
          {automap_store_working.data(), automap_store_working.size()});
  if (working.ok()) {
    read_from(working.value);
  }
}

void automap_store::changed() {
  if (!enabled_ || box_ == nullptr) {
    return;
  }
  const machine::vfs_result<machine::dos_path> working =
      machine::canonicalize_host_path(
          {automap_store_working.data(), automap_store_working.size()});
  if (working.ok()) {
    write_to(working.value);
  }
}

void automap_store::saw(const machine::file_event& event) {
  if (!enabled_ || box_ == nullptr || !event.ok()) {
    return;
  }

  switch (event.what) {
    case machine::file_action::create:
    case machine::file_action::open: {
      const char letter = slot_of(event.path);
      if (letter != 0) {
        pending_slot_ = letter;
        // Created means written: the program makes the slot file to save
        // into it and finds an existing one to load from.
        pending_is_save_ = event.what == machine::file_action::create;
      }
      break;
    }
    case machine::file_action::close: {
      if (pending_slot_ == 0 || slot_of(event.path) != pending_slot_) {
        break;
      }
      const char letter = pending_slot_;
      const bool saving = pending_is_save_;
      pending_slot_ = 0;
      if (!(saving ? event.written_through : event.read_through)) {
        // Opened and given back with nothing moved through it. That is
        // the load menu asking which slots exist — it opens every one of
        // them in turn to build its list — and it is not a load. Acting
        // on it would hand the player the last slot in the directory's
        // map instead of the one they chose.
        break;
      }
      slot_ = letter;
      if (saving) {
        // The working table follows the save, and the slot gets its own
        // snapshot of it.
        changed();
        write_to(slot_path(letter));
      } else {
        // A slot's snapshot **replaces** the table, because the party now
        // in the machine is that slot's party and nothing it walked has
        // anything to do with what the last one did. A slot with no
        // snapshot beside it — a save made before this was ever switched
        // on — replaces it with nothing, for the same reason: an empty
        // map is the truth about a playthrough nobody recorded one for,
        // and keeping the previous party's would draw streets this one
        // has never seen.
        box_->automap().forget_records();
        read_from(slot_path(letter));
      }
      break;
    }
    case machine::file_action::mkdir:
    case machine::file_action::unlink:
      break;
  }
}

void automap_store::write_to(const machine::dos_path& path) {
  machine::filesystem* fs = box_->vfs();
  if (fs == nullptr || path.is_root()) {
    return;
  }

  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const std::size_t size = box_->automap().write_sidecar(bytes);
  if (size == 0) {
    return;
  }

  // The directory the saves are in is the program's own and is there by
  // the time anything has been explored; making it is for the case where
  // a player pointed the host at an installation that has never been
  // saved in. An error is not one — it already exists.
  (void)fs->mkdir(path.parent());

  const machine::vfs_result<machine::file_handle> file = fs->create(path);
  if (!file.ok()) {
    refused(trouble_, refusal_, file.error);
    return;
  }
  const machine::vfs_result<std::size_t> wrote =
      fs->write(file.value, {bytes.data(), size});
  const machine::vfs_error closed = fs->close(file.value);
  if (!wrote.ok()) {
    refused(trouble_, refusal_, wrote.error);
    return;
  }
  if (closed != machine::vfs_error::none) {
    refused(trouble_, refusal_, closed);
    return;
  }
  if (wrote.value != size) {
    // A short count is what `write()` answers on a backend that has run
    // out of room, exactly as AH=40h does on a full disk.
    trouble_ = automap_trouble::out_of_room;
    return;
  }
  ++writes_;
}

void automap_store::read_from(const machine::dos_path& path) {
  machine::filesystem* fs = box_->vfs();
  if (fs == nullptr || path.is_root() || !fs->exists(path)) {
    // Not being there is the ordinary case on a first run and is not
    // trouble: there is nothing to say about a map nobody has drawn.
    return;
  }

  const machine::vfs_result<machine::file_handle> file =
      fs->open(path, machine::open_mode::read_only);
  if (!file.ok()) {
    refused(trouble_, refusal_, file.error);
    return;
  }
  std::array<std::uint8_t, automap_store_capacity> bytes{};
  const machine::vfs_result<std::size_t> got = fs->read(file.value, bytes);
  (void)fs->close(file.value);
  if (!got.ok()) {
    refused(trouble_, refusal_, got.error);
    return;
  }
  if (!box_->automap().read_sidecar({bytes.data(), got.value})) {
    // A file that is there and is not one of ours. Refused rather than
    // guessed at, and said out loud: this is the one failure a player
    // would otherwise meet as an empty map with no explanation.
    trouble_ = automap_trouble::not_a_sidecar;
    return;
  }
  ++reads_;
}

}  // namespace amberfolio::host
