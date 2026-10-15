// SPDX-License-Identifier: AGPL-3.0-only

#include "launch_config.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "amberfolio/machine/seam.h"
#include "desktop_config.h"
#include "options.h"
#include "sidecar_consent.h"
#include "user_files.h"

namespace amberfolio::sdl {

namespace {

/// A config file, written. Every outcome is a sentence: a player who
/// asked to be remembered and was not has to be told, or they will find
/// out on the launch after this one.
void write_config(const std::string& path, const sdl::desktop_config& what,
                  const char* what_happened) {
  if (path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: config this platform does not say where"
                 " per-user data lives; say --config PATH\n");
    return;
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  const std::string text = what.serialize();
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!file) {
    std::fprintf(stderr, "amberfolio: config %s could not be written\n",
                 path.c_str());
    return;
  }
  std::fprintf(stderr, "amberfolio: config %s %s\n", what_happened,
               path.c_str());
}

/// What this host needs and where to point it: a first run (#382).
///
/// Not an error and not the usage block. A person who has just unpacked
/// this has one question -- what do I give it -- and a wall of syntax
/// answers a different one. The three shapes below are the three ways a
/// launch can be short of what it needs, and each says only the part
/// that is missing.
void report_first_run(const options& opts, const std::string& config_path) {
  const bool have_root = !opts.root.empty();
  const bool have_program = !opts.program.empty();
  if (!have_root) {
    std::fprintf(stderr,
                 "amberfolio: no game directory yet, which is not a"
                 " problem\n");
    std::fprintf(stderr,
                 "amberfolio: point this at a directory holding your own"
                 " copy of the game, and the program in it to run:\n");
    std::fprintf(stderr,
                 "amberfolio:     amberfolio <dir> <program.exe>"
                 " --remember\n");
  } else if (!have_program) {
    std::fprintf(stderr,
                 "amberfolio: %s is the directory, and no program was"
                 " named\n",
                 opts.root.string().c_str());
    std::fprintf(stderr,
                 "amberfolio: name the program in it to run, and"
                 " --vfs-list says what is on the disk:\n");
    std::fprintf(stderr,
                 "amberfolio:     amberfolio %s <program.exe>"
                 " --remember\n",
                 opts.root.string().c_str());
  }
  if (opts.no_config) {
    std::fprintf(stderr,
                 "amberfolio: --no-config, so nothing was read from a"
                 " settings file\n");
    return;
  }
  if (config_path.empty()) {
    return;
  }
  std::fprintf(stderr,
               "amberfolio: --remember writes %s, and a launch after that"
               " needs no arguments\n",
               config_path.c_str());
}

/// The question, printed, and the line that came back (#385).
///
/// stdin and stderr and nothing else: this host has no window yet when
/// this runs — `settle_config` happens before SDL comes up — and the
/// terminal it was launched from is the only surface it has. The
/// question's text and the reading of an answer are
/// `sidecar_consent.h`'s, so this is the two lines of plumbing between
/// them and nothing that needs deciding.
///
/// `std::nullopt` for anything that is not a yes or a no, **the end of a
/// closed stdin included**. A caller writes nothing at all for that.
[[nodiscard]] std::optional<bool> ask_about_sidecars() {
  for (const std::string_view line : sdl::sidecar_question()) {
    std::fprintf(stderr, "amberfolio: %.*s\n", static_cast<int>(line.size()),
                 line.data());
  }
  const std::string_view prompt = sdl::sidecar_prompt();
  std::fprintf(stderr, "amberfolio: %.*s", static_cast<int>(prompt.size()),
               prompt.data());
  std::fflush(stderr);

  std::array<char, 64> typed{};
  if (std::fgets(typed.data(), static_cast<int>(typed.size()), stdin) ==
      nullptr) {
    std::fprintf(stderr, "\n");
    return std::nullopt;
  }
  return sdl::read_sidecar_answer(std::string_view(typed.data()));
}

/// One answered question, written down, and **the one place this host
/// writes a config without `--remember`** (#385).
///
/// #382's rule is that a config is written only when a player asks for
/// one, and its reason is that a driving script's `--seam` must never
/// become somebody's remembered choice. That reason is untouched here: a
/// person was asked a question in so many words and answered it, and
/// "asked once" is not a thing this host can deliver without writing the
/// answer somewhere.
///
/// So it writes `from_file` — whatever was already in that file, refused
/// files excluded by the caller — with that **one key** replaced, and
/// never `config_of(opts)`. A run that named a seam and answered this
/// question must not leave the seam behind in the file, and the only way
/// to be sure of that is not to look at the seams at all.
///
/// Its own function so it can be read, argued with, and taken back out
/// on its own.
void remember_the_sidecar_answer(const std::string& path,
                                 sdl::desktop_config from_file, bool answer) {
  from_file.save_sidecars = answer;
  write_config(path, from_file, "remembered your answer in");
}

}  // namespace

[[nodiscard]] unsigned volume_percent(float gain) {
  return static_cast<unsigned>(std::lround(static_cast<double>(gain) * 100.0));
}

[[nodiscard]] sdl::desktop_config config_of(const options& opts) {
  sdl::desktop_config out;
  if (!opts.root.empty()) {
    out.game_directory = opts.root.string();
  }
  if (!opts.program.empty()) {
    out.program = opts.program;
  }
  if (!opts.seams.empty()) {
    out.seams = opts.seams;
  }
  // Only when somebody named one. A discovered engine is deliberately
  // not written down: discovery is the answer that stays right when a
  // player upgrades their Tesseract or their distribution moves it, and
  // a path frozen into a config would be the answer that stops being
  // true without saying so (`ocr_discovery.h`).
  if (!opts.journal_ocr.empty()) {
    out.journal_ocr = opts.journal_ocr;
  }
  out.volume_percent = volume_percent(opts.volume);
  out.muted = opts.muted;
  out.speed = speed_word(opts.speed);
  out.scale = opts.scale;
  out.save_sidecars = opts.save_sidecars;
  return out;
}

void remember_panel_seams(const options& opts,
                          const machine::seam_engine& seams) {
  if (opts.no_config) {
    std::fprintf(stderr,
                 "amberfolio: --no-config, so this choice is not"
                 " remembered\n");
    return;
  }
  const std::string path = config_file_path(opts);
  sdl::desktop_config what;
  bool found = false;
  const std::string text = slurp_file(path, found);
  if (found) {
    // A file this build cannot read is left exactly where it is, here as
    // everywhere else: rewriting one to save a checkbox would throw away
    // somebody's settings to keep a preference (CLAUDE.md's "log, don't
    // fake").
    if (const sdl::config_reading read = what.parse(text); !read.ok()) {
      std::fprintf(stderr,
                   "amberfolio: config %s line %zu - %s: %s; this choice"
                   " is not remembered\n",
                   path.c_str(), read.line, sdl::config_trouble_name(read.why),
                   read.text.c_str());
      return;
    }
  }
  std::vector<std::string> on;
  for (std::size_t i = 0; i < seams.count(); ++i) {
    if (const machine::seam_status row = seams.status(i);
        row.state == machine::seam_state::on) {
      on.emplace_back(row.id);
    }
  }
  what.seams = on.empty() ? std::optional<std::vector<std::string>>{}
                          : std::optional<std::vector<std::string>>{on};
  write_config(path, what, "seams remembered in");
}

[[nodiscard]] bool nobody_is_at_the_keyboard(const options& opts) {
  return opts.headless || !opts.presses.empty() || !opts.pulls.empty() ||
         !opts.record_path.empty() || !opts.dump_prefix.empty() || opts.verify;
}

[[nodiscard]] bool settle_config(options& opts) {
  const std::string path = config_file_path(opts);

  // **A replay reads no config at all**, and that is a rule rather than a
  // tidiness. A recording is keys, ticks and hashes over a stated set of
  // seams at a stated speed (`docs/replay.md`), and it is verified by
  // exact comparison -- by `tests/sessions/`, by `scripts/sweep.py`, on
  // whatever machine happens to run them. A host that let a settings file
  // on that machine reach the run would make a replay's answer depend on
  // whose desk it ran at, which is the one thing a recording is for.
  //
  // Said out loud when there was a file to ignore, because a player
  // whose seams did not come on during a replay is owed the reason.
  if (!opts.replay_path.empty()) {
    std::error_code why;
    if (!path.empty() && std::filesystem::exists(path, why)) {
      std::fprintf(stderr,
                   "amberfolio: config not read for --replay - a"
                   " recording's seams, speed and keys are its own\n");
    }
    // And a replay that was given neither is not a first run: it is a
    // replay missing the disk it was recorded against, which is a
    // command line to fix rather than an invitation to point this
    // somewhere. The config would have answered it and deliberately did
    // not.
    if (opts.root.empty() || opts.program.empty()) {
      std::fprintf(stderr,
                   "amberfolio: a replay needs the directory and the"
                   " program it was recorded against; the config is not"
                   " read for one\n");
      return false;
    }
    return true;
  }

  // A player asking to start over, before anything is read: the file is
  // emptied to its header, so this launch is a first run and every
  // launch after it is too, until somebody says --remember. The whole of
  // "forget it", and `--forget-code-wheel` next door is its sibling.
  if (opts.forget_config) {
    write_config(path, sdl::desktop_config{}, "forgotten");
  }

  // Hoisted out of the block below because the question at the end of
  // this function needs both of them: what the file already said, so an
  // answer can be written back beside it rather than over it, and
  // whether there is a file here this build is allowed to write at all.
  // A config it *refused* is left exactly where it is (CLAUDE.md's "log,
  // don't fake"), which means there is nowhere to put an answer, which
  // means nothing is asked.
  sdl::desktop_config from_file;
  bool config_writable = !opts.no_config;

  if (!opts.no_config && !opts.forget_config) {
    bool found = false;
    const std::string text = slurp_file(path, found);
    if (found) {
      if (const sdl::config_reading read = from_file.parse(text); !read.ok()) {
        // Loud, and then the defaults -- never a guess, and never half a
        // file (CLAUDE.md's "log, don't fake"). Left where it is rather
        // than repaired: whatever that file is, this build cannot read
        // it, and a config from a later build is somebody's answer.
        std::fprintf(stderr, "amberfolio: config %s line %zu - %s: %s\n",
                     path.c_str(), read.line,
                     sdl::config_trouble_name(read.why), read.text.c_str());
        std::fprintf(stderr,
                     "amberfolio: config starting on the defaults; the"
                     " file was left where it is\n");
        config_writable = false;
      } else {
        // **flag > config > default**, nine times, one rule
        // (`desktop_config.h`).
        std::string root = opts.root.string();
        sdl::prefer(opts.given.root, from_file.game_directory, root);
        opts.root = std::filesystem::path(root);
        sdl::prefer(opts.given.program, from_file.program, opts.program);
        // The one setting whose *origin* outlives the settling, because
        // a refusal costs a remembered choice something different from
        // what it costs a flag (#383, `options::seams_from_config`).
        opts.seams_from_config =
            !opts.given.seams && from_file.seams.has_value();
        sdl::prefer(opts.given.seams, from_file.seams, opts.seams);
        sdl::prefer(opts.given.journal_ocr, from_file.journal_ocr,
                    opts.journal_ocr);
        unsigned percent = volume_percent(opts.volume);
        sdl::prefer(opts.given.volume, from_file.volume_percent, percent);
        opts.volume = static_cast<float>(percent) / 100.0F;
        sdl::prefer(opts.given.muted, from_file.muted, opts.muted);
        std::string speed = speed_word(opts.speed);
        sdl::prefer(opts.given.speed, from_file.speed, speed);
        // The reading already refused every word but the four, so this
        // cannot fail; checked anyway, because a cast that assumes is
        // exactly the shape of a bug nobody finds.
        static_cast<void>(speed_named(speed, opts.speed));
        sdl::prefer(opts.given.scale, from_file.scale, opts.scale);
        sdl::prefer(opts.given.save_sidecars, from_file.save_sidecars,
                    opts.save_sidecars);
        std::fprintf(stderr, "amberfolio: config read %s\n", path.c_str());
      }
    }
  }

  // The one question this host asks a person (#385). After the settling,
  // because a config that already answered it is the "once" in "asked
  // once" and because `--save-sidecars` on the command line has to beat
  // both; before the writing below, so that a run that was asked *and*
  // said `--remember` writes one file holding both answers.
  const sdl::sidecar_ask why = sdl::should_ask_about_sidecars(
      {.named_on_the_command_line = opts.given.save_sidecars,
       .config_answered = from_file.save_sidecars.has_value(),
       .can_remember = config_writable && !path.empty(),
       .driven = nobody_is_at_the_keyboard(opts),
       .have_game = !opts.root.empty() && !opts.program.empty()});
  if (why == sdl::sidecar_ask::ask) {
    if (const std::optional<bool> answer = ask_about_sidecars();
        answer.has_value()) {
      opts.save_sidecars = *answer;
      // Never `opts.given`: the command line did not say this, and a
      // later launch must be free to remember something else.
      if (!opts.remember) {
        remember_the_sidecar_answer(path, from_file, *answer);
      }
    } else {
      // Silence is neither a yes nor a no. Off for this run, because off
      // is what everything of this project's own is until somebody asks
      // for it — and nothing written down, so the next person at this
      // keyboard is asked again.
      std::fprintf(stderr,
                   "amberfolio: no answer, so nothing is written beside"
                   " your saves and nothing is remembered\n");
    }
  }

  // Written after the settling and before the run, so what lands in the
  // file is what this run is about to do -- and so a run that ends badly
  // has still remembered where the game is.
  if (opts.remember) {
    write_config(path, config_of(opts), "remembered");
  }

  if (opts.root.empty() || opts.program.empty()) {
    report_first_run(opts, path);
    opts.first_run = true;
    return false;
  }
  return true;
}

}  // namespace amberfolio::sdl
