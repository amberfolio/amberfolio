// SPDX-License-Identifier: AGPL-3.0-only
//
// The decision install.h describes.

#include "install.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/host/edition_facts.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::sdl {

install_choice settle_install(const std::string& flag,
                              const std::string& program,
                              machine::filesystem& files) {
  install_choice choice;
  if (!flag.empty()) {
    const machine::vfs_result<machine::dos_path> given =
        machine::canonicalize_host_path(
            std::span<const char>(flag.data(), flag.size()));
    choice.ok = given.ok();
    choice.directory = given.value;
    choice.from = "--install";
    return choice;
  }

  const machine::vfs_result<machine::dos_path> boot_path =
      machine::canonicalize(
          machine::dos_path{},
          std::span<const char>(program.data(), program.size()));
  if (!boot_path.ok()) {
    return choice;  // the load says why
  }
  const machine::vfs_result<sha256_digest> boot =
      machine::fingerprint_file(files, boot_path.value);
  std::array<char, sha256_digest::text_length + 1> hex{};
  if (!boot.ok() || format_hex(boot.value, hex) == 0 ||
      host::find_requirements({hex.data(), sha256_digest::text_length}) ==
          nullptr) {
    return choice;
  }

  // A boot file an edition names: which edition, by every file here.
  std::vector<machine::tree_file> found(machine::tree_file_count(files));
  const std::size_t count =
      std::min(machine::tree_files(files, found), found.size());
  std::vector<std::string> names;
  std::vector<sha256_digest> digests;
  for (std::size_t i = 0; i < count; ++i) {
    const machine::vfs_result<sha256_digest> digest =
        machine::fingerprint_file(files, found[i].path);
    if (!digest.ok()) {
      continue;
    }
    std::array<char, machine::dos_path_capacity> name{};
    static_cast<void>(machine::format_dos_path(found[i].path, name));
    names.emplace_back(name.data());
    digests.push_back(digest.value);
  }
  // Built after the names are all in place: `offered_file` holds views.
  std::vector<host::offered_file> offered;
  offered.reserve(names.size());
  for (std::size_t i = 0; i < names.size(); ++i) {
    offered.push_back({.name = names[i], .digest = digests[i]});
  }
  const host::edition_match match = host::match_edition(offered);
  if (!match.complete()) {
    return choice;
  }
  const machine::vfs_result<machine::dos_path> row =
      machine::canonicalize_host_path(std::span<const char>(
          match.edition->install.data(), match.edition->install.size()));
  if (row.ok()) {
    choice.directory = row.value;
    choice.from = "the edition row";
  }
  return choice;
}

}  // namespace amberfolio::sdl
