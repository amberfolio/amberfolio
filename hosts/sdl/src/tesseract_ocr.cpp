// SPDX-License-Identifier: AGPL-3.0-only
//
// The desktop host's OCR engine. tesseract_ocr.h has the reasoning.

#include "tesseract_ocr.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>

#include "amberfolio/host/journal_extract.h"

namespace amberfolio::sdl {
namespace {

/// One argument, quoted for the shell `std::system` hands this to.
///
/// `shell_quoted` and not `quoted`: `std::quoted` is an exact match for a
/// `std::string` argument and this is not, so argument-dependent lookup
/// would quietly hand every call to <iomanip>'s stream manipulator.
///
/// Everything this quotes is either a path this host made in its own
/// scratch directory or the program name the player gave on the command
/// line, so the job is to survive spaces rather than to defend against a
/// hostile string — but a path with a quote in it should still fail
/// loudly rather than becoming two arguments, so the quote is dropped
/// rather than escaped: a filename this host cannot spell safely is a
/// filename it does not use.
[[nodiscard]] std::string shell_quoted(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2U);
  out.push_back('"');
  for (const char c : text) {
    if (c != '"') {
      out.push_back(c);
    }
  }
  out.push_back('"');
  return out;
}

/// Run `command`, with its output going to `log`, and answer whether it
/// exited zero.
///
/// `std::system` rather than a per-platform process API: this runs twice
/// per ingestion and once per entry, during onboarding, and a
/// `CreateProcess`/`posix_spawn` pair would be sixty lines of platform
/// difference to save a shell that is already running.
[[nodiscard]] bool run(const std::string& command, const std::string& log) {
  std::string line = command;
  line += " > ";
  line += shell_quoted(log);
  line += " 2>&1";
#ifdef _WIN32
  // cmd.exe strips the first and last quote of a command line that begins
  // with one, so a quoted program path needs the whole line wrapped
  // again. This is the documented behaviour of `cmd /c`, not a
  // workaround for it.
  line = "\"" + line + "\"";
#endif
  // NOLINTNEXTLINE(bugprone-command-processor) - see below.
  return std::system(line.c_str()) == 0;
}

// Why the suppression above, since this project's rule is to change the
// config rather than argue with it: `bugprone-command-processor` is about
// a command string an attacker can influence, and the whole design here
// is that this one cannot be. Two of the three pieces are paths this host
// invented inside its own scratch directory; the third is the program
// name the *player typed on their own command line*, which is a person
// asking this process to run a program they already could have run
// themselves. `shell_quoted` drops quote characters rather than escaping
// them, so a path this host cannot spell safely is a path it does not
// use.
//
// It is one line rather than a check turned off for the whole tree,
// because everywhere else in this repository a command processor would
// still be wrong.

/// A whole file, or an empty string.
[[nodiscard]] std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return {};
  }
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The engine's `--version` output, cut down to its first line.
[[nodiscard]] std::string first_line(std::string_view text) {
  const std::size_t end = text.find_first_of("\r\n");
  return std::string(end == std::string_view::npos ? text
                                                   : text.substr(0, end));
}

}  // namespace

tesseract_ocr::tesseract_ocr(std::string program)
    : program_(std::move(program)) {}

tesseract_ocr::~tesseract_ocr() {
  // The whole body inside a catch: `remove_all` takes an `error_code` and
  // so reports the filesystem's own refusals without throwing, but
  // building a `path` from a string allocates, and a destructor that let
  // that escape would end the process rather than leave a temporary
  // directory behind. Leaving it behind is the better of the two.
  try {
    if (scratch_.empty()) {
      return;
    }
    std::error_code ignored;
    std::filesystem::remove_all(scratch_, ignored);
  } catch (...) {  // NOLINT(bugprone-empty-catch) - said above.
  }
}

bool tesseract_ocr::scratch(std::string& out) {
  if (!scratch_.empty()) {
    out = scratch_;
    return true;
  }
  std::error_code why;
  std::filesystem::path where = std::filesystem::temp_directory_path(why);
  if (why) {
    return false;
  }
  // Unique per run, so two ingestions at once cannot read each other's
  // pages. A clock reading is fine here and only here: PLAN.md §4's rule
  // that nothing reads the host's clock is core's, and this is a host
  // naming a temporary directory.
  where /= "amberfolio-ocr-" +
           std::to_string(static_cast<unsigned long long>(
               std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(where, why);
  if (why) {
    return false;
  }
  scratch_ = where.string();
  out = scratch_;
  return true;
}

bool tesseract_ocr::available() {
  std::string where;
  if (!scratch(where)) {
    return false;
  }
  const std::string log =
      (std::filesystem::path(where) / "version.txt").string();
  if (!run(shell_quoted(program_) + " --version", log)) {
    return false;
  }
  engine_ = first_line(slurp(log));
  if (engine_.empty()) {
    return false;
  }
  return true;
}

bool tesseract_ocr::recognize(const host::journal_bitmap& page,
                              std::string& out) {
  out.clear();
  if (page.empty()) {
    return false;
  }
  std::string where;
  if (!scratch(where)) {
    return false;
  }

  const std::filesystem::path root(where);
  const std::string image = (root / "entry.pgm").string();
  const std::string stem = (root / "entry").string();
  const std::string text = stem + ".txt";
  const std::string log = (root / "engine.txt").string();

  {
    // P5: the binary greymap. Eight lines, and Leptonica reads it
    // natively (tesseract_ocr.h).
    std::ofstream file(image, std::ios::binary | std::ios::trunc);
    if (!file) {
      return false;
    }
    file << "P5\n" << page.width << " " << page.height << "\n255\n";
    file.write(reinterpret_cast<const char*>(page.pixels.data()),
               static_cast<std::streamsize>(page.pixels.size()));
    if (!file) {
      return false;
    }
  }

  // `--psm 6` — one uniform block of text, which is what a journal entry
  // is. Tesseract writes its answer to <stem>.txt and says nothing on
  // stdout, so the log above catches only its complaints.
  const bool ok = run(shell_quoted(program_) + " " + shell_quoted(image) + " " +
                          shell_quoted(stem) + " --psm 6",
                      log);
  std::error_code ignored;
  std::filesystem::remove(image, ignored);
  if (!ok) {
    std::filesystem::remove(text, ignored);
    return false;
  }

  out = slurp(text);
  std::filesystem::remove(text, ignored);
  // Trailing blank lines are the engine's page break and not the entry's
  // text; a store that kept them would diff badly against a corrected
  // copy of the same entry for no reason at all.
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' ||
                          out.back() == ' ' || out.back() == '\f')) {
    out.pop_back();
  }
  return !out.empty();
}

}  // namespace amberfolio::sdl
