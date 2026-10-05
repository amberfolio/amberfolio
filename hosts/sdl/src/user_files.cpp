// SPDX-License-Identifier: AGPL-3.0-only

#include "user_files.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <string_view>

#include "amberfolio/host/code_wheel_store.h"
#include "amberfolio/host/journal_store.h"
#include "desktop_config.h"
#include "options.h"

namespace amberfolio::sdl {

[[nodiscard]] std::string per_user_path(std::string_view filename) {
  // SDL's own answer, which is the right one on all three desktops and
  // is one call rather than three `#ifdef`s that would each be wrong on
  // somebody's machine: `%APPDATA%\\amberfolio\\` on Windows,
  // `~/Library/Application Support/amberfolio/` on macOS, and
  // `$XDG_DATA_HOME/amberfolio/` on Linux. It creates the directory.
  //
  // No organization, because there is no organization: an empty one
  // leaves the application's own directory directly under the platform's
  // data root, which is what a single-application project should write.
  char* where = SDL_GetPrefPath("", "amberfolio");
  if (where == nullptr) {
    return {};
  }
  std::string path(where);
  SDL_free(where);
  path += filename;
  return path;
}

[[nodiscard]] std::string journal_store_path(const options& opts) {
  return opts.journal_store.empty()
             ? per_user_path(host::journal_store_filename)
             : opts.journal_store;
}

[[nodiscard]] std::string code_wheel_store_path(const options& opts) {
  return opts.code_wheel_store.empty()
             ? per_user_path(host::code_wheel_store_filename)
             : opts.code_wheel_store;
}

[[nodiscard]] std::string config_file_path(const options& opts) {
  return opts.config_path.empty() ? per_user_path(sdl::desktop_config_filename)
                                  : opts.config_path;
}

[[nodiscard]] std::string slurp_file(const std::string& path, bool& found) {
  std::ifstream file(path, std::ios::binary);
  found = static_cast<bool>(file);
  if (!found) {
    return {};
  }
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

void load_code_wheel_store(const options& opts, host::code_wheel_store& store) {
  const std::string path = code_wheel_store_path(opts);
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: code wheel this platform does not say where"
                 " per-user data lives; say --code-wheel-store PATH\n");
    return;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  if (const host::code_wheel_trouble why = store.parse(text);
      why != host::code_wheel_trouble::none) {
    // Left alone rather than used, and left on disk rather than
    // overwritten: whatever that file is, this build cannot read it, and
    // a store from a later one is somebody's answer.
    std::fprintf(stderr, "amberfolio: code wheel store %s - %s\n", path.c_str(),
                 host::code_wheel_trouble_name(why));
  }
}

void save_code_wheel_store(const options& opts,
                           const host::code_wheel_store& store) {
  const std::string path = code_wheel_store_path(opts);
  if (path.empty()) {
    return;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  const std::string text = store.serialize();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  if (!file) {
    std::fprintf(stderr,
                 "amberfolio: code wheel store %s could not be written - the"
                 " next launch will ask again\n",
                 path.c_str());
  }
}

void load_journal_store(const options& opts, host::journal_store& store) {
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: journal this platform does not say where"
                 " per-user data lives; say --journal-store PATH\n");
    return;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fprintf(stderr,
                 "amberfolio: journal store %s is not there yet - the"
                 " reader has nothing to show until --journal reads one\n",
                 path.c_str());
    return;
  }
  const std::string text((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
  if (const host::journal_trouble why = store.parse(text);
      why != host::journal_trouble::none) {
    // Left alone rather than used: whatever that file is, it is not this
    // build's, and half a transcription is worse than none.
    store.clear();
    std::fprintf(stderr, "amberfolio: journal store %s - %s\n", path.c_str(),
                 host::journal_trouble_name(why));
    return;
  }
  // What the *file* held. The read log is not in it since #351 and is
  // reported on its own line below, after the sidecar beside the save has
  // been read over whatever a version 4 store carried.
  std::fprintf(stderr,
               "amberfolio: journal store %s entries=%zu corrections=%zu"
               " pictures=%zu\n",
               path.c_str(), store.size(), store.corrections(),
               store.picture_count());
}

void save_journal_store(const options& opts, const host::journal_store& store) {
  const std::string path = journal_store_path(opts);
  if (path.empty()) {
    return;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  const std::string text = store.serialize();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  if (!file) {
    std::fprintf(stderr, "amberfolio: journal store %s could not be written\n",
                 path.c_str());
  }
}

void settle_code_wheel(const options& opts, machine::machine& box,
                       host::code_wheel_store& code_wheel) {
  // And the one thing a person shows this machine that is not a file:
  // that they have already answered the code-wheel challenge (#291).
  // Before the seams, for the reason above — a run that knows it, and
  // turns the seam on, never reaches the challenge at all.
  //
  // Three things in order, and the order is the whole of what they mean:
  // a player asking to be asked again empties the store first; then the
  // store says whether *this copy* has answered, which is a question that
  // needs the program's fingerprint and so cannot be asked any earlier;
  // then `--code-wheel-answered` states the condition regardless, for a
  // driven run with nobody at the keyboard.
  //
  // The store is read either way, and **forgetting reads it first**: a
  // player asking to be asked again is asking about the file, and a run
  // that emptied an object it had never filled would say "forgotten" and
  // leave every copy in the file exactly where it was. It did, once, and
  // the driven check below is what caught it.
  load_code_wheel_store(opts, code_wheel);
  if (opts.forget_code_wheel) {
    if (code_wheel.forget()) {
      save_code_wheel_store(opts, code_wheel);
      code_wheel.clear_changed();
    }
    std::fprintf(stderr,
                 "amberfolio: code wheel forgotten - the challenge will be"
                 " asked again\n");
  } else if (host::apply_code_wheel_store(box, code_wheel)) {
    std::fprintf(stderr,
                 "amberfolio: code wheel answered on this copy already -"
                 " the challenge will not be drawn\n");
  }
  if (opts.code_wheel_answered) {
    box.seams().set_code_wheel_answered(true);
    std::fprintf(stderr,
                 "amberfolio: code wheel answered - the challenge will not be"
                 " drawn\n");
  }
}
}  // namespace amberfolio::sdl
