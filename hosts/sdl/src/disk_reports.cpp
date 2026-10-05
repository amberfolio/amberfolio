// SPDX-License-Identifier: AGPL-3.0-only

#include "disk_reports.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/host/edition_facts.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/launcher_view.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/save_layer.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "options.h"
#include "sound_report.h"

namespace amberfolio::sdl {

namespace {

/// Resolve a raw path the way a program's own INT 21h call would, and
/// say so when it does not resolve.
///
/// The host never decides what a path means: `canonicalize_host_path()`
/// is the one place DOS short-name rules live, separator included
/// (machine/vfs.h), and a host that folded case or translated a
/// separator itself would be a second implementation of the rule that
/// says whether two callers are looking at the same file. The ABI's own
/// VFS door makes the identical call for the identical reason — and the
/// separator half of that rule moved into core precisely because this
/// flag became its second caller.
[[nodiscard]] bool resolve_vfs_path(const std::string& raw,
                                    machine::dos_path& out) {
  const machine::vfs_result<machine::dos_path> where =
      machine::canonicalize_host_path(
          std::span<const char>(raw.data(), raw.size()));
  if (!where.ok() || where.value.is_root()) {
    std::fprintf(stderr,
                 "amberfolio: vfs %s is not a path a file can live"
                 " at\n",
                 raw.c_str());
    return false;
  }
  out = where.value;
  return true;
}

/// The loaded program's layer and where its rows are on this disk, or
/// null with the reason printed: no table for the program, or a copy
/// that does not say where it saves (machine/save_layer.h, #397).
[[nodiscard]] const machine::save_layer* save_layer_here(
    const machine::machine& box, machine::filesystem& files,
    machine::save_layer_places& places) {
  const machine::save_layer* layer =
      machine::save_layer_for(box.seams().program());
  if (layer == nullptr) {
    std::fprintf(stderr,
                 "amberfolio: save-layer unrecognized - this build has no"
                 " table for this program\n");
    return nullptr;
  }
  const machine::save_directory_answer saves =
      machine::read_save_directory(files, box.dos().current_directory());
  if (!saves.ok()) {
    std::fprintf(stderr,
                 "amberfolio: save-layer none - the copy does not say where"
                 " it saves (%s)\n",
                 machine::save_directory_trouble_name(saves.trouble));
    return nullptr;
  }
  places.save_directory = saves.directory;
  places.current_directory = box.dos().current_directory();
  return layer;
}

}  // namespace

[[nodiscard]] std::string spell_vfs_path(const machine::dos_path& path) {
  std::array<char, machine::dos_path_capacity> text{};
  static_cast<void>(machine::format_dos_path(path, text));
  return {text.data()};
}

void report_copy_facts(machine::filesystem& files,
                       const machine::dos_path& install) {
  {
    const machine::save_directory_answer saves =
        machine::read_save_directory(files, install);
    if (saves.ok()) {
      std::fprintf(stderr, "amberfolio: save directory %s\n",
                   spell_vfs_path(saves.directory).c_str());
    } else if (!install.is_root()) {
      // Quiet at the root, where every test program lives with no
      // configuration file; a copy laid out on purpose gets a line.
      std::fprintf(stderr, "amberfolio: save directory unknown (%s)\n",
                   machine::save_directory_trouble_name(saves.trouble));
    }
  }
  // The copy's own sound line, when it is not what the program will
  // read (sound_report.h, #404).
  std::fputs(
      sdl::started_sound_line(machine::read_configured_sound(files, install))
          .c_str(),
      stderr);
}

void report_save_layer_table(const machine::machine& box,
                             machine::filesystem& files, const options& opts) {
  if (!opts.save_layer) {
    return;
  }
  machine::save_layer_places places;
  const machine::save_layer* layer = save_layer_here(box, files, places);
  if (layer == nullptr) {
    return;
  }
  std::fprintf(stderr,
               "amberfolio: save-layer %llu row(s) slots=%.*s members=%u\n",
               static_cast<unsigned long long>(layer->files.size()),
               static_cast<int>(layer->slots.size()), layer->slots.data(),
               static_cast<unsigned>(layer->members));
  for (const machine::save_file& row : layer->files) {
    std::array<char, machine::save_pattern_capacity> spelled{};
    static_cast<void>(machine::spell_save_pattern(row, places, spelled));
    std::fprintf(stderr, "amberfolio: save-layer %s %s%s - %.*s\n",
                 spelled.data(), machine::save_file_kind_name(row.kind),
                 row.required ? " required" : "",
                 static_cast<int>(row.about.size()), row.about.data());
  }
}

void report_save_layer_files(const machine::machine& box,
                             machine::filesystem& files, const options& opts) {
  if (!opts.save_layer || !box.seams().have_program()) {
    return;
  }
  machine::save_layer_places places;
  const machine::save_layer* layer = save_layer_here(box, files, places);
  if (layer == nullptr) {
    return;
  }
  std::vector<machine::tree_file> found(machine::tree_file_count(files));
  const std::size_t count = machine::tree_files(files, found);
  std::size_t named = 0;
  std::size_t theirs = 0;
  for (std::size_t i = 0; i < count && i < found.size(); ++i) {
    const machine::save_layer_row row =
        machine::match_save_file(*layer, places, found[i].path);
    if (row.file == nullptr) {
      continue;
    }
    ++named;
    if (row.file->kind != machine::save_file_kind::config) {
      ++theirs;
    }
    std::array<char, 32> where{};
    int used = 0;
    if (row.slot != 0) {
      used = std::snprintf(where.data(), where.size(), " slot=%c", row.slot);
    }
    if (row.member != 0 && used >= 0 &&
        static_cast<std::size_t>(used) < where.size()) {
      std::snprintf(where.data() + used,
                    where.size() - static_cast<std::size_t>(used), " member=%u",
                    static_cast<unsigned>(row.member));
    }
    std::fprintf(stderr, "amberfolio: save-layer file %s %s%s\n",
                 spell_vfs_path(found[i].path).c_str(),
                 machine::save_file_kind_name(row.file->kind), where.data());
  }
  // Two numbers, because they are not the same claim. The table *names*
  // the program's configuration file so that a host is told to leave it
  // with the game's own (machine/save_layer.h), and a single count that
  // folded it in with the saves would be saying the opposite of what
  // that row is there to say.
  std::fprintf(stderr,
               "amberfolio: save-layer %llu of %llu file(s) named,"
               " %llu the playthrough's\n",
               static_cast<unsigned long long>(named),
               static_cast<unsigned long long>(count),
               static_cast<unsigned long long>(theirs));
}

void report_vfs(machine::filesystem& files, const options& opts) {
  if (opts.list_vfs) {
    // One walk, filling a listing, rather than an index at a time: a
    // tree walk per row is quadratic on top of an `entry_at()` that is
    // already quadratic, and on a real installation it does not finish
    // (machine/vfs.h has the measurement).
    std::vector<machine::tree_file> found(machine::tree_file_count(files));
    const std::size_t count = machine::tree_files(files, found);
    std::fprintf(stderr, "amberfolio: vfs %llu file(s)\n",
                 static_cast<unsigned long long>(count));
    for (std::size_t i = 0; i < count && i < found.size(); ++i) {
      std::fprintf(stderr, "amberfolio: vfs %s %u\n",
                   spell_vfs_path(found[i].path).c_str(), found[i].size);
    }
  }

  for (const std::string& raw : opts.vfs_gets) {
    machine::dos_path where;
    if (!resolve_vfs_path(raw, where)) {
      continue;
    }
    const machine::vfs_result<machine::file_stat> seen = files.stat(where);
    if (!seen.ok() || seen.value.is_directory) {
      std::fprintf(stderr, "amberfolio: vfs %s is not a file here\n",
                   spell_vfs_path(where).c_str());
      continue;
    }
    // Every byte through the read, hashed as it comes, and nothing kept:
    // a player's file has no business in a log, and the digest of what
    // came back is the claim worth making about a read anyway.
    std::vector<std::uint8_t> bytes(seen.value.size);
    const machine::vfs_result<std::uint32_t> read =
        machine::read_file(files, where, bytes);
    if (!read.ok() || read.value != seen.value.size) {
      std::fprintf(stderr, "amberfolio: vfs %s could not be read whole\n",
                   spell_vfs_path(where).c_str());
      continue;
    }
    const sha256_digest digest = sha256(bytes);
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(digest, hex));
    std::fprintf(stderr, "amberfolio: vfs %s %u sha256=%s\n",
                 spell_vfs_path(where).c_str(), read.value, hex.data());
  }

  for (const std::string& raw : opts.vfs_removes) {
    machine::dos_path where;
    if (!resolve_vfs_path(raw, where)) {
      continue;
    }
    // Said before it is done, and said plainly. This host's filesystem
    // is a directory on the player's disk, not a sandbox, and a flag
    // that reads like a test fixture is exactly the one somebody runs on
    // a real installation.
    std::fprintf(stderr, "amberfolio: vfs deleting %s from %s\n",
                 spell_vfs_path(where).c_str(), opts.root.string().c_str());
    const machine::vfs_error why = files.unlink(where);
    if (why == machine::vfs_error::none) {
      std::fprintf(stderr, "amberfolio: vfs %s deleted\n",
                   spell_vfs_path(where).c_str());
    } else {
      std::fprintf(stderr, "amberfolio: vfs %s not deleted (%s)\n",
                   spell_vfs_path(where).c_str(), machine::vfs_error_name(why));
    }
  }
}

void report_unrecognized_edition(machine::filesystem& files) {
  struct looked_at {
    std::string name;
    std::uint32_t size{};
    sha256_digest digest;
  };

  std::vector<machine::tree_file> found(machine::tree_file_count(files));
  const std::size_t count =
      std::min(machine::tree_files(files, found), found.size());
  std::vector<looked_at> seen;
  seen.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const machine::vfs_result<sha256_digest> digest =
        machine::fingerprint_file(files, found[i].path);
    if (!digest.ok()) {
      std::fprintf(stderr, "amberfolio: edition %s could not be hashed\n",
                   spell_vfs_path(found[i].path).c_str());
      continue;
    }
    seen.push_back({.name = spell_vfs_path(found[i].path),
                    .size = found[i].size,
                    .digest = digest.value});
  }

  // Built after the names are all in place: `offered_file` holds views,
  // and a vector that grew while they were being taken would have left
  // every one of them pointing at freed characters.
  std::vector<host::offered_file> offered;
  offered.reserve(seen.size());
  for (const looked_at& file : seen) {
    offered.push_back({.name = file.name, .digest = file.digest});
  }

  const host::edition_match match = host::match_edition(offered);
  if (match.edition == nullptr) {
    std::fprintf(stderr,
                 "amberfolio: edition no file here belongs to any edition"
                 " this build knows (%zu looked at)\n",
                 seen.size());
  } else {
    std::size_t required = 0;
    for (const host::edition_artifact& artifact : match.edition->artifacts) {
      required += artifact.required ? 1U : 0U;
    }
    std::fprintf(stderr,
                 "amberfolio: edition closest %.*s - %zu of %zu required"
                 " artifact(s) here\n",
                 static_cast<int>(match.edition->name.size()),
                 match.edition->name.data(), required - match.missing.size(),
                 required);
    for (const std::size_t index : match.missing) {
      const host::edition_artifact& artifact = match.edition->artifacts[index];
      std::fprintf(stderr, "amberfolio: edition missing %.*s%s\n",
                   static_cast<int>(artifact.name.size()), artifact.name.data(),
                   artifact.kind == host::artifact_kind::configuration
                       ? " (a configuration file, any contents)"
                       : "");
    }
  }

  // The files nothing claimed, with their digests: all of them when
  // nothing matched at all, which is the directory that holds an edition
  // this build has never seen.
  const std::size_t unclaimed_count =
      match.edition == nullptr ? seen.size() : match.unclaimed.size();
  for (std::size_t i = 0; i < unclaimed_count; ++i) {
    const looked_at& file =
        match.edition == nullptr ? seen[i] : seen[match.unclaimed[i]];
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(file.digest, hex));
    std::fprintf(stderr, "amberfolio: edition looked at %s %u sha256=%s\n",
                 file.name.c_str(), file.size, hex.data());
  }
}

}  // namespace amberfolio::sdl
