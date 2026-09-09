// SPDX-License-Identifier: AGPL-3.0-only
//
// The playthrough's sidecars, written and read. `slot_store.h` has the
// reasoning; this is the file handling under it.

#include "amberfolio/host/slot_store.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::host {
namespace {

/// The stem of the program's own save slots. A path is one of them when
/// it is `SAVE\SAVGAM<L>.DAT` — the letter is what this is here to read,
/// and everything else about the traffic is the program's business.
///
/// The same fact `machine::save_layer` states as a pattern (#208), in
/// the form this file needs it: that table is keyed on the loaded
/// edition and is read by a host deciding what to persist, and this is a
/// letter read off a file event as it happens, before anything here
/// knows what was loaded.
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
  for (const char ch : slot_store_directory) {
    raw[used++] = ch;
  }
  raw[used++] = '\\';
  for (const char ch : leaf) {
    raw[used++] = ch;
  }
  return machine::canonicalize_host_path({raw.data(), used});
}

/// One of the two working tables, as a path. Its name is a constant and
/// canonicalizing it cannot fail, but the result is checked all the same
/// — a path this build could not build is a reason to write nothing, not
/// a reason to write somewhere else.
[[nodiscard]] machine::dos_path working_path(std::string_view text) noexcept {
  const machine::vfs_result<machine::dos_path> where =
      machine::canonicalize_host_path({text.data(), text.size()});
  return where.ok() ? where.value : machine::dos_path{};
}

/// One reason, refused by the filesystem, recorded with what it said.
void refused(slot_trouble& trouble, machine::vfs_error& refusal,
             machine::vfs_error what) noexcept {
  trouble = slot_trouble::file_refused;
  refusal = what;
}

}  // namespace

const char* slot_trouble_name(slot_trouble what) noexcept {
  switch (what) {
    case slot_trouble::file_refused:
      return "file-refused";
    case slot_trouble::out_of_room:
      return "out-of-room";
    case slot_trouble::not_a_sidecar:
      return "not-a-sidecar";
    case slot_trouble::none:
      break;
  }
  return "none";
}

char slot_store::slot_of(const machine::dos_path& path) noexcept {
  if (path.depth() != 2) {
    return 0;
  }
  if (text_of(path.component(0)) != slot_store_directory) {
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

machine::dos_path slot_store::automap_slot_path(char letter) noexcept {
  // `AFMAP<L>.DAT`: the working table's name with the letter spliced in,
  // which keeps every one of these files sorting together in a directory
  // listing beside the saves they belong to.
  const std::array<char, 12> leaf{'A',    'F', 'M', 'A', 'P',
                                  letter, '.', 'D', 'A', 'T'};
  const machine::vfs_result<machine::dos_path> where =
      under_save({leaf.data(), 10});
  return where.ok() ? where.value : machine::dos_path{};
}

machine::dos_path slot_store::journal_slot_path(char letter) noexcept {
  // `AFSEEN<L>.DAT`, on the same rule. Six and a letter is seven, which
  // is inside eight-three with a character to spare.
  const std::array<char, 12> leaf{'A',    'F', 'S', 'E', 'E', 'N',
                                  letter, '.', 'D', 'A', 'T'};
  const machine::vfs_result<machine::dos_path> where =
      under_save({leaf.data(), 11});
  return where.ok() ? where.value : machine::dos_path{};
}

void slot_store::attach(machine::machine& box) {
  box_ = &box;
  if (!enabled_) {
    return;
  }
  read_automap_from(working_path(slot_store_automap_working));
}

void slot_store::read_journal_log() {
  if (!enabled_ || box_ == nullptr || journal_ == nullptr) {
    return;
  }
  read_journal_from(working_path(slot_store_journal_working));
}

void slot_store::changed() {
  if (!enabled_ || box_ == nullptr) {
    return;
  }
  write_automap_to(working_path(slot_store_automap_working));
}

void slot_store::journal_changed() {
  if (!enabled_ || box_ == nullptr || journal_ == nullptr ||
      !journal_->log_changed()) {
    return;
  }
  write_journal_to(working_path(slot_store_journal_working));
  // Down, because the bytes are on the disk now. The flag is what stops
  // this being written again by the save that follows a citation, and
  // what lets a load refresh the working file through this same call.
  journal_->clear_log_changed();
}

void slot_store::saw(const machine::file_event& event) {
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
        // The working tables follow the save, and the slot gets its own
        // snapshot of each.
        changed();
        journal_changed();
        write_automap_to(automap_slot_path(letter));
        if (journal_ != nullptr) {
          // Written even when the log is empty, which is eight bytes of
          // header and no rows: a slot saved by a party that has been
          // cited nothing has to replace whatever snapshot was there, or
          // loading it would hand this party the last one's list.
          write_journal_to(journal_slot_path(letter));
        }
      } else {
        // A slot's snapshots **replace** the tables, because the party
        // now in the machine is that slot's party and nothing it walked
        // and nothing it was told has anything to do with what the last
        // one did. A slot with no snapshot beside it — a save made
        // before this was ever switched on — replaces them with nothing,
        // for the same reason: an empty map and an empty log are the
        // truth about a playthrough nobody recorded one for, and keeping
        // the previous party's would draw streets this one has never
        // seen and cite entries it has never been sent to.
        box_->automap().forget_records();
        read_automap_from(automap_slot_path(letter));
        // And the working table follows, so that it holds what this
        // machine now holds. Without this a run that loaded a slot and
        // then stopped without exploring anything would leave the
        // *previous* party's table as the working one, and the next run
        // would start by reading it back in.
        changed();
        if (journal_ != nullptr) {
          journal_->forget_seen();
          read_journal_from(journal_slot_path(letter));
          // The log's working file, for the same reason — and through
          // the flag, which both of the two lines above raise.
          journal_changed();
          // And into the machine the reader draws from, which is the one
          // step the exploration does not need: the automap seam reads
          // `box.automap()` itself, while the journal's log is *copied*
          // into `machine::journal_state` and a reader shown the store
          // alone would still be listing the last party's citations
          // (`journal_store.h`, `restore_journal_log`).
          box_->journal().clear_seen();
          restore_journal_log(box_->journal(), *journal_);
        }
      }
      break;
    }
    case machine::file_action::mkdir:
    case machine::file_action::unlink:
      break;
  }
}

void slot_store::write_automap_to(const machine::dos_path& path) {
  std::array<std::uint8_t, slot_store_automap_capacity> bytes{};
  const std::size_t size = box_->automap().write_sidecar(bytes);
  if (size == 0) {
    return;
  }
  write_whole(path, {bytes.data(), size});
}

void slot_store::write_journal_to(const machine::dos_path& path) {
  std::array<std::uint8_t, slot_store_journal_capacity> bytes{};
  const std::size_t size = journal_->write_log_sidecar(bytes);
  if (size == 0) {
    return;
  }
  write_whole(path, {bytes.data(), size});
}

void slot_store::read_automap_from(const machine::dos_path& path) {
  std::array<std::uint8_t, slot_store_automap_capacity> bytes{};
  std::size_t got = 0;
  if (!read_whole(path, bytes, got)) {
    return;
  }
  if (!box_->automap().read_sidecar({bytes.data(), got})) {
    // A file that is there and is not one of ours. Refused rather than
    // guessed at, and said out loud: this is the one failure a player
    // would otherwise meet as an empty map with no explanation.
    trouble_ = slot_trouble::not_a_sidecar;
    return;
  }
  ++reads_;
}

void slot_store::read_journal_from(const machine::dos_path& path) {
  std::array<std::uint8_t, slot_store_journal_capacity> bytes{};
  std::size_t got = 0;
  if (!read_whole(path, bytes, got)) {
    return;
  }
  if (!journal_->read_log_sidecar({bytes.data(), got})) {
    trouble_ = slot_trouble::not_a_sidecar;
    return;
  }
  ++reads_;
}

void slot_store::write_whole(const machine::dos_path& path,
                             std::span<const std::uint8_t> bytes) {
  machine::filesystem* fs = box_->vfs();
  if (fs == nullptr || path.is_root()) {
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
  const machine::vfs_result<std::size_t> wrote = fs->write(file.value, bytes);
  const machine::vfs_error closed = fs->close(file.value);
  if (!wrote.ok()) {
    refused(trouble_, refusal_, wrote.error);
    return;
  }
  if (closed != machine::vfs_error::none) {
    refused(trouble_, refusal_, closed);
    return;
  }
  if (wrote.value != bytes.size()) {
    // A short count is what `write()` answers on a backend that has run
    // out of room, exactly as AH=40h does on a full disk.
    trouble_ = slot_trouble::out_of_room;
    return;
  }
  ++writes_;
}

bool slot_store::read_whole(const machine::dos_path& path,
                            std::span<std::uint8_t> into, std::size_t& got) {
  got = 0;
  machine::filesystem* fs = box_->vfs();
  if (fs == nullptr || path.is_root() || !fs->exists(path)) {
    // Not being there is the ordinary case on a first run and is not
    // trouble: there is nothing to say about a map nobody has drawn or a
    // game nobody has been told to read anything by.
    return false;
  }

  const machine::vfs_result<machine::file_handle> file =
      fs->open(path, machine::open_mode::read_only);
  if (!file.ok()) {
    refused(trouble_, refusal_, file.error);
    return false;
  }
  const machine::vfs_result<std::size_t> read = fs->read(file.value, into);
  (void)fs->close(file.value);
  if (!read.ok()) {
    refused(trouble_, refusal_, read.error);
    return false;
  }
  // A file that is there and holds nothing is **not** the same as no
  // file, and it goes on to the format check like any other: an empty
  // sidecar is a truncated one, which is a file this build should say it
  // cannot read rather than pass over in silence.
  got = read.value;
  return true;
}

}  // namespace amberfolio::host
