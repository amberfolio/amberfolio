// SPDX-License-Identifier: AGPL-3.0-only

#include "options.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/screen_keyboard.h"
#include "press_spec.h"
#include "wall_spec.h"

namespace amberfolio::sdl {

namespace {

/// Every option this host takes.
///
/// Printed on a **mis-invocation** and nowhere else since #382: a launch
/// with nothing to run is a first run and gets `report_first_run()`
/// instead, which is a sentence about what this needs rather than a wall
/// of syntax about everything it can be told.
void print_usage() {
  std::fprintf(
      stderr,
      "usage: amberfolio [<dir> [<program.exe>]] [--install DIR] [--headless]"
      " [--scale N] [--verify] [--press KEY@FRAME]\n"
      "                                      [--pull ID@FRAME]\n"
      "                                      [--steps N]"
      " [--until TICKS] [--dump PREFIX] [--dump-every N]\n"
      "                                      [--trace]"
      " [--watch OFF[:N]]\n"
      "                                      [--seam ID] [--seams]"
      " [--seam-panel]\n"
      "                                      [--vfs-list]"
      " [--vfs-get PATH] [--vfs-remove PATH]\n"
      "                                      [--save-layer]\n"
      "                                      [--keyboard prompt|name|full]"
      "\n"
      "                                      [--document PATH]\n"
      "                                      [--record FILE]"
      " [--record-every N] [--replay FILE] [--rehash FILE]\n"
      "                                      [--wall now|none|"
      "YYYY-MM-DD[THH:MM[:SS[.CC]]]]\n"
      "                                      [--speed xt|turbo|at|386]\n"
      "                                      [--fast N|max]\n"
      "                                      [--volume 0-100] [--mute]\n"
      "                                      [--config PATH] [--no-config]"
      " [--remember] [--forget-config]\n"
      "                                      [-- ARGUMENTS...]\n");
}

}  // namespace

[[nodiscard]] const char* speed_name(machine::speed_preset preset) noexcept {
  switch (preset) {
    case machine::speed_preset::pc_xt:
      return "xt (4.77 MHz 8088)";
    case machine::speed_preset::turbo_xt:
      return "turbo (8-10 MHz XT clone)";
    case machine::speed_preset::at:
      return "at";
    case machine::speed_preset::pc_386:
      return "386 (33 MHz 386DX)";
  }
  return "unknown";
}

[[nodiscard]] const char* speed_word(machine::speed_preset preset) noexcept {
  switch (preset) {
    case machine::speed_preset::pc_xt:
      return "xt";
    case machine::speed_preset::turbo_xt:
      return "turbo";
    case machine::speed_preset::at:
      return "at";
    case machine::speed_preset::pc_386:
      return "386";
  }
  return "xt";
}

[[nodiscard]] bool speed_named(std::string_view word,
                               machine::speed_preset& out) noexcept {
  if (word == "xt") {
    out = machine::speed_preset::pc_xt;
  } else if (word == "turbo") {
    out = machine::speed_preset::turbo_xt;
  } else if (word == "at") {
    out = machine::speed_preset::at;
  } else if (word == "386") {
    out = machine::speed_preset::pc_386;
  } else {
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_count(const char* text, std::uint64_t& out) {
  if (text == nullptr || *text == '\0' || *text == '-') {
    return false;
  }
  char* end = nullptr;
  const unsigned long long value = std::strtoull(text, &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  out = static_cast<std::uint64_t>(value);
  return true;
}

[[nodiscard]] bool parse_pull(std::string_view spec, scripted_pull& out) {
  const std::size_t at = spec.rfind('@');
  if (at == std::string_view::npos || at == 0 || at + 1 == spec.size()) {
    return false;
  }
  const std::string_view digits = spec.substr(at + 1);
  std::uint64_t frame = 0;
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed = std::from_chars(first, last, frame);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.id = std::string(spec.substr(0, at));
  out.frame = frame;
  return true;
}

[[nodiscard]] bool parse_watch(std::string_view spec, watch_point& out) {
  std::string_view digits = spec;
  unsigned width = 1;
  const std::size_t colon = spec.rfind(':');
  if (colon != std::string_view::npos) {
    const std::string_view tail = spec.substr(colon + 1);
    if (tail == "1") {
      width = 1;
    } else if (tail == "2") {
      width = 2;
    } else {
      return false;
    }
    digits = spec.substr(0, colon);
  }
  if (digits.empty() || digits.size() > 4) {
    return false;
  }
  std::uint16_t offset = 0;
  const char* const first = digits.data();
  const char* const last = first + digits.size();
  const std::from_chars_result parsed =
      std::from_chars(first, last, offset, 16);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return false;
  }
  out.offset = offset;
  out.width = width;
  return true;
}

[[nodiscard]] options parse(int argc, char** argv) {
  options opts;
  std::vector<std::string_view> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--") {
      // Everything past here belongs to the program, not to this host —
      // including anything that looks like one of our own options, which
      // is the entire point of the separator. The leading space is what
      // COMMAND.COM leaves between the program name and its tail, and a
      // program that counts characters at PSP:80h expects it.
      for (int j = i + 1; j < argc; ++j) {
        opts.command_tail += ' ';
        opts.command_tail += argv[j];
      }
      break;
    }
    if (arg == "--headless") {
      opts.headless = true;
    } else if (arg == "--verify") {
      opts.verify = true;
    } else if (arg == "--press" && i + 1 < argc) {
      scripted_press press;
      if (!parse_press(argv[++i], press)) {
        std::fprintf(stderr,
                     "amberfolio: --press wants KEY@FRAME[:down|:up],"
                     " as in A@60\n");
        return opts;
      }
      opts.presses.push_back(std::move(press));
    } else if (arg == "--pull" && i + 1 < argc) {
      scripted_pull pull;
      if (!parse_pull(argv[++i], pull)) {
        std::fprintf(stderr,
                     "amberfolio: --pull wants ID@FRAME, as in"
                     " cheat-kill-all@600\n");
        return opts;
      }
      opts.pulls.push_back(std::move(pull));
    } else if (arg == "--watch" && i + 1 < argc) {
      watch_point point;
      if (!parse_watch(argv[++i], point)) {
        std::fprintf(stderr,
                     "amberfolio: --watch wants a hexadecimal data-segment "
                     "offset, optionally :1 or :2 for its width, as in 6AAD "
                     "or 6AAD:2\n");
        return opts;
      }
      opts.watches.push_back(point);
    } else if (arg == "--volume" && i + 1 < argc) {
      // Percent, because that is what a person means by a volume and
      // because an integer 0-100 has no rounding to argue about. Refused
      // rather than clamped: 150 is a request this host will not honour
      // (it never amplifies), and quietly turning it into 100 would be a
      // wrong answer given silently.
      std::uint64_t percent = 0;
      if (!parse_count(argv[++i], percent) || percent > 100) {
        std::fprintf(stderr,
                     "amberfolio: --volume wants a percentage from 0 to"
                     " 100\n");
        return opts;
      }
      opts.volume = static_cast<float>(percent) / 100.0F;
      opts.given.volume = true;
    } else if (arg == "--mute") {
      opts.muted = true;
      opts.given.muted = true;
    } else if (arg == "--seam" && i + 1 < argc) {
      opts.seams.emplace_back(argv[++i]);
      opts.given.seams = true;
    } else if (arg == "--install" && i + 1 < argc) {
      opts.install = argv[++i];
    } else if (arg == "--vfs-list") {
      opts.list_vfs = true;
    } else if (arg == "--vfs-get" && i + 1 < argc) {
      opts.vfs_gets.emplace_back(argv[++i]);
    } else if (arg == "--vfs-remove" && i + 1 < argc) {
      opts.vfs_removes.emplace_back(argv[++i]);
    } else if (arg == "--save-layer") {
      opts.save_layer = true;
    } else if (arg == "--keyboard" && i + 1 < argc) {
      opts.keyboard = argv[++i];
      if (machine::screen_keyboard::layout_named(opts.keyboard) == nullptr) {
        std::fprintf(stderr,
                     "amberfolio: --keyboard wants prompt, name or full\n");
        return opts;
      }
    } else if (arg == "--document" && i + 1 < argc) {
      opts.documents.emplace_back(argv[++i]);
    } else if (arg == "--code-wheel-answered") {
      opts.code_wheel_answered = true;
    } else if (arg == "--code-wheel-store" && i + 1 < argc) {
      opts.code_wheel_store = argv[++i];
    } else if (arg == "--forget-code-wheel") {
      opts.forget_code_wheel = true;
    } else if (arg == "--journal" && i + 1 < argc) {
      opts.journal = argv[++i];
    } else if (arg == "--journal-store" && i + 1 < argc) {
      opts.journal_store = argv[++i];
    } else if (arg == "--journal-ocr" && i + 1 < argc) {
      opts.journal_ocr = argv[++i];
      opts.given.journal_ocr = true;
    } else if (arg == "--journal-probe") {
      opts.journal_probe = true;
    } else if (arg == "--cite-all-journal") {
      opts.cite_all_journal = true;
    } else if (arg == "--seams") {
      opts.list_seams = true;
    } else if (arg == "--seam-panel") {
      opts.seam_panel = true;
    } else if (arg == "--trace") {
      opts.trace = true;
    } else if (arg == "--save-sidecars") {
      opts.save_sidecars = true;
      opts.given.save_sidecars = true;
    } else if (arg == "--no-save-sidecars") {
      // The other side of the same answer (#385). A player who said yes
      // once and wants one launch that writes nothing needs a way to say
      // so, and a driving script that wants to be explicit about the
      // disk it is about to replay over needs the same one.
      opts.save_sidecars = false;
      opts.given.save_sidecars = true;
    } else if (arg == "--config" && i + 1 < argc) {
      opts.config_path = argv[++i];
    } else if (arg == "--no-config") {
      opts.no_config = true;
    } else if (arg == "--remember") {
      opts.remember = true;
    } else if (arg == "--forget-config") {
      opts.forget_config = true;
    } else if (arg == "--dump" && i + 1 < argc) {
      opts.dump_prefix = argv[++i];
    } else if (arg == "--dump-every" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.dump_every) || opts.dump_every == 0) {
        std::fprintf(stderr,
                     "amberfolio: --dump-every wants a positive frame "
                     "count\n");
        return opts;
      }
    } else if (arg == "--record" && i + 1 < argc) {
      opts.record_path = argv[++i];
    } else if (arg == "--record-every" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.record_every) ||
          opts.record_every == 0) {
        std::fprintf(stderr,
                     "amberfolio: --record-every wants a positive frame"
                     " count\n");
        return opts;
      }
    } else if (arg == "--replay" && i + 1 < argc) {
      opts.replay_path = argv[++i];
    } else if (arg == "--rehash" && i + 1 < argc) {
      opts.rehash_path = argv[++i];
    } else if (arg == "--wall" && i + 1 < argc) {
      const std::string_view spec = argv[++i];
      if (spec == "now") {
        opts.wall = wall_source::host_clock;
      } else if (spec == "none") {
        opts.wall = wall_source::unseeded;
      } else if (sdl::parse_wall(spec, opts.wall_stated)) {
        opts.wall = wall_source::stated;
      } else {
        std::fprintf(stderr,
                     "amberfolio: --wall wants now, none, or a real"
                     " YYYY-MM-DD[THH:MM[:SS[.CC]]] between 1980 and"
                     " 2099\n");
        return opts;
      }
    } else if (arg == "--steps" && i + 1 < argc) {
      if (!parse_count(argv[++i], opts.step_budget) || opts.step_budget == 0) {
        std::fprintf(stderr, "amberfolio: --steps wants a positive count\n");
        return opts;
      }
    } else if (arg == "--until" && i + 1 < argc) {
      std::uint64_t ticks = 0;
      if (!parse_count(argv[++i], ticks) || ticks == 0) {
        std::fprintf(stderr, "amberfolio: --until wants a positive tick\n");
        return opts;
      }
      opts.tick_budget = static_cast<machine::ticks>(ticks);
    } else if (arg == "--fast" && i + 1 < argc) {
      const std::string_view rate = argv[++i];
      if (rate == "max") {
        opts.fast = 0.0;
      } else {
        char* end = nullptr;
        const double value = std::strtod(std::string(rate).c_str(), &end);
        if (end == nullptr || *end != '\0' || !(value > 0.0)) {
          std::fprintf(stderr,
                       "amberfolio: --fast wants a positive number, or max\n");
          return opts;
        }
        opts.fast = value;
      }
    } else if (arg == "--speed" && i + 1 < argc) {
      if (!speed_named(argv[++i], opts.speed)) {
        std::fprintf(stderr,
                     "amberfolio: --speed wants xt, turbo, at or 386\n");
        return opts;
      }
      opts.given.speed = true;
    } else if (arg == "--scale" && i + 1 < argc) {
      // strtol rather than atoi, which cannot tell "0" from "not a
      // number" - a distinction worth having when the answer decides
      // how big a window is.
      char* end = nullptr;
      const long value = std::strtol(argv[++i], &end, 10);
      opts.scale = (end != nullptr && *end == '\0' && value > 0)
                       ? static_cast<unsigned>(value)
                       : default_scale;
      opts.given.scale = true;
    } else if (arg.starts_with("--")) {
      std::fprintf(stderr, "amberfolio: unknown option %.*s\n",
                   static_cast<int>(arg.size()), arg.data());
      return opts;
    } else {
      positional.push_back(arg);
    }
  }

  // Two arguments, one, or none. Two is the whole invocation; one is a
  // directory whose program the config names; none is a launch that
  // takes everything from the config, which is what a second launch
  // looks like (#382). Three is a mis-invocation and gets the usage
  // block, because nothing here has ever taken three.
  if (positional.size() > 2) {
    print_usage();
    return opts;
  }
  if (!positional.empty()) {
    opts.root = std::filesystem::path(positional[0]);
    opts.given.root = true;
  }
  if (positional.size() == 2) {
    opts.program = std::string(positional[1]);
    opts.given.program = true;
  }

  // Both diagnostics need the window and the event queue that
  // `--headless` is defined as not opening. Refused rather than
  // quietly ignored: a check that reports nothing because its own
  // arguments cancelled out is worse than one that never ran.
  // `--headless` never sleeps, so it is already running as fast as this
  // machine can be run. Saying so beats accepting a number that would
  // change nothing.
  if (opts.headless && opts.fast != 1.0) {
    std::fprintf(stderr,
                 "amberfolio: --fast needs a window; --headless already"
                 " runs unpaced\n");
    return opts;
  }

  // `--headless` opens no audio device, so there is no level for either
  // of these to be the level of. Refused on the same reasoning as
  // `--fast` above, and with one extra: `--dump`'s WAV is written before
  // the gain in any case, so a headless run that accepted `--mute` would
  // still write a tone — an option that appeared to do nothing at all.
  if (opts.headless && (opts.given.muted || opts.given.volume)) {
    std::fprintf(stderr,
                 "amberfolio: --volume and --mute need an audio device;"
                 " --headless opens none\n");
    return opts;
  }

  if (opts.headless && (opts.verify || !opts.presses.empty())) {
    std::fprintf(stderr,
                 "amberfolio: --verify and --press need a window;"
                 " they cannot be combined with --headless\n");
    return opts;
  }
  // A keyboard nobody can see is a keyboard nobody can press: it is
  // drawn over the window and driven by the pointer at it. Refused on
  // the same reasoning as the two above.
  if (opts.headless && !opts.keyboard.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --keyboard needs a window; --headless opens"
                 " none\n");
    return opts;
  }
  // And a replay's keys are the recording's. A keyboard a person could
  // press during one would be an input the recorded run never had —
  // which is why a keystroke at the window is dropped during a replay and
  // why `--pull` is refused, and this is neither more nor less than that.
  if (!opts.keyboard.empty() && !opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: a replay's keys are the recording's;"
                 " --keyboard cannot be combined with --replay\n");
    return opts;
  }
  // The toggle panel is furniture over the window and is worked with
  // the pointer, so it wants one for exactly the on-screen keyboard's
  // reason; and a replay's seams are the recording's (docs/replay.md),
  // so a panel that could turn one on during one would be changing the
  // machine the recording describes.
  if (opts.headless && opts.seam_panel) {
    std::fprintf(stderr,
                 "amberfolio: --seam-panel needs a window; --headless"
                 " opens none\n");
    return opts;
  }
  if (opts.seam_panel && !opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: a replay's seams are the recording's;"
                 " --seam-panel cannot be combined with --replay\n");
    return opts;
  }

  // The stills share `--dump`'s prefix, so without one there is nowhere
  // to put them. Refused for the reason above: an option that silently
  // did nothing is worse than one that says why it cannot.
  if (opts.dump_every != 0 && opts.dump_prefix.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --dump-every needs --dump, whose prefix it"
                 " writes under\n");
    return opts;
  }

  // Two of the journal's three companions are about an *ingestion*, so
  // without one there is nothing for them to be about. Refused rather
  // than ignored, on the same reasoning as `--dump-every` below.
  //
  // `--journal-store` used to be the third, and M5-E4 (#175) is why it is
  // not any more: the reader reads that file on every run, so saying
  // where it is means something with no ingestion in sight. The note that
  // used to be here said reading was #175's; it is, and this is it.
  if (opts.journal.empty() && (opts.journal_probe || opts.given.journal_ocr)) {
    std::fprintf(stderr,
                 "amberfolio: --journal-ocr and"
                 " --journal-probe need --journal, whose ingestion they"
                 " are about\n");
    return opts;
  }

  // The cadence is a property of the recording being made, so without a
  // recording there is nothing for it to be a property of. Refused rather
  // than ignored, on the same reasoning as `--dump-every` above.
  if (opts.record_every != 1 && opts.record_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --record-every needs --record, whose"
                 " checkpoints it spaces\n");
    return opts;
  }

  // A run records or it replays; it does not do both. The recording of a
  // replay would be a copy of its own input with the checks folded in,
  // and a file that is neither the run nor the verification of one.
  if (!opts.record_path.empty() && !opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --record and --replay are the two halves of"
                 " one thing; ask for one\n");
    return opts;
  }

  if (!opts.rehash_path.empty() && opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --rehash rewrites a recording; name it with"
                 " --replay\n");
    return opts;
  }

  // The recording names the speed, the seams and every key, and a player
  // applies all three before it checks them (machine/replay.h). A command
  // line that also named one of them would be either agreeing silently or
  // disagreeing silently, and the second is a divergence reported as a
  // mismatched initial condition — true, but three steps from the cause.
  // Said here instead.
  //
  // `--wall` joins them for the same reason and a sharper one (#320): the
  // wall clock is machine state, so a replaying host that seeded one the
  // recording does not carry would diverge at the first checkpoint, on a
  // hash, having been told to. The recording's own `wall` line is what
  // seeds a replay, and a recording without one replays the unseeded
  // machine it was made on.
  if (!opts.replay_path.empty()) {
    const char* also = nullptr;
    if (opts.given.seams) {
      also = "--seam";
    } else if (opts.given.speed) {
      also = "--speed";
    } else if (!opts.presses.empty()) {
      also = "--press";
    } else if (!opts.pulls.empty()) {
      also = "--pull";
    } else if (opts.wall != wall_source::host_clock) {
      also = "--wall";
    }
    if (also != nullptr) {
      std::fprintf(stderr,
                   "amberfolio: the recording decides the seams, the speed,"
                   " the keys and the date; %s cannot be given with"
                   " --replay\n",
                   also);
      return opts;
    }
  }

  // The config's own three, and each of them is a pair of options that
  // ask for opposite things (#382). Refused rather than resolved by
  // precedence, because there is no sensible precedence between "ignore
  // the file" and "write the file".
  if (opts.remember && opts.no_config) {
    std::fprintf(stderr,
                 "amberfolio: --remember writes the config --no-config"
                 " says to ignore; ask for one\n");
    return opts;
  }
  if (opts.remember && opts.forget_config) {
    std::fprintf(stderr,
                 "amberfolio: --forget-config empties the config"
                 " --remember writes; ask for one\n");
    return opts;
  }
  if (opts.no_config && !opts.config_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: --config names a file --no-config says to"
                 " ignore; ask for one\n");
    return opts;
  }
  // And a replay's settings are the recording's, which is the same rule
  // the five options above it follow: a run whose seams and speed came
  // out of a recording has nothing of the player's to remember.
  if (opts.remember && !opts.replay_path.empty()) {
    std::fprintf(stderr,
                 "amberfolio: the recording decides the seams and the"
                 " speed; --remember cannot be given with --replay\n");
    return opts;
  }

  opts.valid = true;
  return opts;
}

}  // namespace amberfolio::sdl
