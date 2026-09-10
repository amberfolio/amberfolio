// SPDX-License-Identifier: AGPL-3.0-only
//
// Where the desktop host looks for an OCR engine (#382).
//
// Until M6 this host was *told*: `--journal-ocr` defaulted to the bare
// word `tesseract` and the shell was left to resolve it, so a player
// with an engine installed somewhere ordinary and no idea that a flag
// existed got "no engine" and a suggestion to read `--help`. The browser
// path never asked anybody anything — the page carries its engine — and
// that is the reference this catches up to.
//
// Two places, in this order, and the order is the argument:
//
//   1. **Beside the binary.** A packaged build that ships an engine puts
//      it there, and a player's copy must not depend on a path from the
//      machine it was built on — `linked_tessdata_path()` in `main.cpp`
//      already makes the same call for the same reason.
//   2. **Each directory of `PATH`, in order.** The player's own install,
//      wherever their platform's packaging put it. In order, because
//      `PATH` order is the answer the platform itself would give and a
//      host that reordered it would be answering a different question.
//
// And then a **report**, which is the third thing and not an absence of
// the first two: a miss says what was looked for and where, because
// "no engine" with nothing after it is the failure a player finds out
// about last (`tesseract_ocr.h` makes the same point about a store with
// no text in it). "Log, don't fake" (CLAUDE.md) is the rule; a host that
// quietly recognized nothing would be the fake.
//
// `--journal-ocr PATH` and `--journal-ocr none` keep working and win:
// discovery is what happens when nobody said, and a player who said is
// not overruled by a directory listing.
//
//
// What this unit does and does not do
// -----------------------------------
//
// It builds the **list of places**, in order, and stops there. Whether a
// file at one of them exists is `main.cpp`'s question, asked of the
// filesystem — which is what lets the list itself be checked without an
// engine installed, on a runner that has none, and without this unit
// growing an opinion about what "executable" means on three platforms.
//
// Deliberately not consulted: `PATHEXT` on Windows. A player's Tesseract
// is `tesseract.exe`; a `.bat` or a `.cmd` in front of one is a wrapper
// somebody wrote, and running a wrapper this host went looking for is a
// step past discovery into guessing. Such a player can say
// `--journal-ocr PATH`, which is the door that stays open for exactly
// this.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace amberfolio::sdl {

/// The engine's filename on this platform.
inline constexpr std::string_view ocr_engine_filename =
#ifdef _WIN32
    "tesseract.exe";
#else
    "tesseract";
#endif

/// Every place this host would look for `executable`, in the order it
/// looks: `beside` first when there is one, then each directory of
/// `path_variable` split on `separator`.
///
/// Empty entries are skipped. An empty `PATH` entry means the current
/// directory on Windows, and the directory a player happened to launch
/// from is not a place to go looking for a program to run — a host that
/// searched it would be reachable by anyone who could leave a file next
/// to a shortcut.
///
/// A place named twice is listed once, keeping the first: a `PATH` with
/// a repeat in it is ordinary, and a report that said the same directory
/// four times would read like a bug in the report.
[[nodiscard]] std::vector<std::string> ocr_candidates(
    std::string_view beside, std::string_view path_variable, char separator,
    std::string_view executable);

}  // namespace amberfolio::sdl
