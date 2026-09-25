// SPDX-License-Identifier: AGPL-3.0-only
//
// Where the player's directory appears on the machine's drive, and so the
// directory the program starts in (#397, `docs/hosts.md` §2c).
//
// The storefront copies are laid out for a launcher that mounts their
// folder at `C:\POOLRAD` and changes into it; the archive release sits at
// the root. This host is that launcher, and this is its one decision:
//
//   * `--install DIR` when it was given;
//   * otherwise the edition row's own `install` directory
//     (`host/edition_facts.h`) — but only for a folder whose program is a
//     boot file some edition names and that holds every file that edition
//     requires, because a layout is a fact about one edition and a
//     closest match is not one;
//   * otherwise the root, which is where every run before #397 put it.
//
// Its own unit for `desktop_config.cpp`'s reason: a decision is a thing a
// test can read without a window.

#pragma once

#include <string>

#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {

/// What `settle_install()` decided.
struct install_choice {
  /// The DOS directory the folder appears at; the root by default.
  machine::dos_path directory{};
  /// Which of the three rules above chose it: `--install`, `the edition
  /// row` or `the root`.
  const char* from{"the root"};
  /// False only when `--install` named something no DOS directory can be,
  /// and then nothing else is tried.
  bool ok{true};
};

/// The directory for `files`, the folder the host was pointed at, seen at
/// the root, whose program is `program`. `flag` is `--install`'s text, or
/// empty.
[[nodiscard]] install_choice settle_install(const std::string& flag,
                                            const std::string& program,
                                            machine::filesystem& files);

}  // namespace amberfolio::sdl
