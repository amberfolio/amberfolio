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
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/sha256.h"
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

[[nodiscard]] std::string journal_store_path(const options& opts,
                                             const machine::edition* program) {
  if (!opts.journal_store.empty()) {
    return opts.journal_store;
  }
  return program == nullptr
             ? per_user_path(host::journal_store_filename)
             : per_user_path(host::journal_store_filename_for(*program));
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

namespace {

/// The name of the edition `store` is of, for a sentence; empty for one
/// this build does not know.
[[nodiscard]] std::string_view edition_name_of(
    const host::journal_store& store) {
  sha256_digest digest;
  if (!machine::parse_digest(store.edition(), digest)) {
    return {};
  }
  const host::journal_edition* known = host::find_journal(digest);
  return known == nullptr ? std::string_view{} : known->name;
}

/// Whose journal `store` is, to the program `box` is running.
[[nodiscard]] host::journal_owner owner_of(const host::journal_store& store,
                                           machine::machine& box) {
  return box.seams().have_program()
             ? host::journal_owner_of(store.edition(), box.seams().program())
             : host::journal_owner::unknown;
}

void write_journal_store(const std::string& path,
                         const host::journal_store& store) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  const std::string text = store.serialize();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  file.flush();
  if (!file) {
    std::fprintf(stderr, "amberfolio: journal store %s could not be written\n",
                 path.c_str());
  }
}

/// The file a store was kept in before a store was a program's own
/// (#584), into `store`, if it is there, reads, and is the running
/// program's journal. False, and `store` empty, otherwise: another
/// program's text is never read in under this one's name.
[[nodiscard]] bool adopt_the_one_store(machine::machine& box,
                                       host::journal_store& store,
                                       const std::string& from) {
  bool found = false;
  const std::string text = slurp_file(from, found);
  if (!found) {
    return false;
  }
  if (store.parse(text) != host::journal_trouble::none ||
      owner_of(store, box) != host::journal_owner::this_program) {
    store.clear();
    store.clear_changed();
    return false;
  }
  return true;
}

}  // namespace

void load_journal_store(const options& opts, machine::machine& box,
                        host::journal_store& store) {
  const std::string path =
      journal_store_path(opts, box.seams().known_edition());
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: journal this platform does not say where"
                 " per-user data lives; say --journal-store PATH\n");
    return;
  }
  // A store somebody named is the whole of where to look.
  load_journal_store_from(path,
                          opts.journal_store.empty()
                              ? per_user_path(host::journal_store_filename)
                              : std::string{},
                          box, store);
}

void load_journal_store_from(const std::string& path, const std::string& before,
                             machine::machine& box,
                             host::journal_store& store) {
  bool found = false;
  const std::string text = slurp_file(path, found);
  if (!found) {
    // This program has no store of its own yet. The one file every store
    // was kept in before #584 may be its journal: taken if it is, kept
    // from here on under the program's own name, and left where it was.
    if (!before.empty() && before != path &&
        adopt_the_one_store(box, store, before)) {
      write_journal_store(path, store);
      store.clear_changed();
      std::fprintf(stderr,
                   "amberfolio: journal store %s entries=%zu corrections=%zu"
                   " pictures=%zu - read from %s, and kept here from now"
                   " on\n",
                   path.c_str(), store.size(), store.corrections(),
                   store.picture_count(), before.c_str());
      return;
    }
    std::fprintf(stderr,
                 "amberfolio: journal store %s is not there yet - the"
                 " reader has nothing to show until --journal reads one\n",
                 path.c_str());
    return;
  }
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
  if (owner_of(store, box) == host::journal_owner::another_program) {
    // A store somebody named, of the other game's journal (#584). Said,
    // and left as it is: the reader answers as it does with no journal
    // (`host_services.cpp`), and the file is that game's to keep.
    const std::string_view name = edition_name_of(store);
    std::fprintf(stderr,
                 "amberfolio: journal store %s is the %.*s - another"
                 " game's journal, so the reader will not open it here\n",
                 path.c_str(), static_cast<int>(name.size()), name.data());
  }
}

void save_journal_store(const options& opts, machine::machine& box,
                        const host::journal_store& store) {
  const std::string path =
      journal_store_path(opts, box.seams().known_edition());
  if (path.empty()) {
    return;
  }
  write_journal_store(path, store);
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
