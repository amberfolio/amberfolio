// SPDX-License-Identifier: AGPL-3.0-only
//
// The SDL3 desktop host: one host for Windows, macOS and Linux
// (PLAN.md §4). It builds a machine, points it at a directory, loads a
// program, and gives it a screen, a speaker and a keyboard.
//
//     amberfolio [<dir> [<program.exe>]] [--headless] [--scale N]
//                                     [--verify] [--press KEY@FRAME]
//                                     [--steps N] [--until TICKS]
//                                     [--dump PREFIX] [--trace]
//                                     [--watch OFF[:N]]
//                                     [--seam ID] [--seams]
//                                     [--document PATH] [--vfs-list]
//                                     [--vfs-get PATH] [--vfs-remove PATH]
//                                     [--save-layer]
//                                     [--speed NAME]
//                                     [--config PATH] [--no-config]
//                                     [--remember] [--forget-config]
//                                     [--fast N|max] [-- ARGUMENTS...]
//
// The two arguments come from the config file when they are not given
// (#382), so a launch after the first needs neither. A launch with
// neither and no config is a **first run**: it says what it needs and
// where to point it, and exits successfully. No game directory yet is
// not an error.
//
// `--headless` opens no window and no audio device. That is what keeps
// the CI smoke test meaningful on a runner with neither, and it is the
// path M2-T1's host checks take.
//
// `--verify` and `--press` are the opposite: they exist so the *windowed*
// path can be run without a person in front of it. See "Checking the
// paths a headless run cannot" in window_checks.h.
//
// Every option is explained in options.h, next to the parser.
//
//
// Where the rest of the program is
// --------------------------------
//
// This file is `main()`: the order things happen in, from the command line
// to the exit code, and nothing that can be read on its own. Each piece
// below is its own unit (#472), and each header says what it is for.
//
//   options.h          every flag, what it is for, and the parser
//   launch_config.h    the config file; flag > config > default
//   user_files.h       the per-user directory and the stores in it
//   wiring.h           the machine and the diagnostics sink
//   disk_reports.h     the edition, the save layer and the VFS door
//   journal_host.h     documents the player holds, and the journal
//   recording.h        --record, --replay and --wall
//   edge_dump.h        --dump's edge list
//   seam_host.h        the seams: enabling, the listing, the closing account
//   audio_device.h     the audio callback and the listening level
//   desktop_window.h   the window; window_checks.h is --verify
//   overlays.h         the on-screen keyboard and the toggle panel
//   pacing.h           where a slice ends and how long to wait
//   run_reports.h      what a run says as it goes and as it ends
//
//
// The loop, and the one rule it exists to honour
// ----------------------------------------------
//
// PLAN.md §4: "host wall time only throttles presentation, outside
// machine state." So the loop is:
//
//     run the machine forward in *virtual* time to the next frame
//     boundary → present whatever frame that produced → sleep whatever
//     *wall* time is left over
//
// and never the other way round. If the host cannot keep up, the sleep
// is simply zero and presentation falls behind; virtual time is not
// slowed, not skipped, and not consulted about how long any of it took.
// A frame that was composed while the host was busy is dropped by the
// generation counter (platform.h) rather than delaying the machine.
//
// The corollary is that this host never asks the machine to catch up. A
// long stall on the host side does not become a burst of emulated
// instructions; the machine's clock is its own, and the only thing wall
// time decides is when we draw and how long we idle.
//
//
// What is deliberately not here
// -----------------------------
//
// The period-correct non-square-pixel option PLAN.md §4 lists is a
// `--scale` integer for now and an obvious place to grow an aspect mode;
// M4's polish is where that gets decided rather than guessed at here.
// Native controller support is a seam rather than a key mapping, and is
// M8's (#372).

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "amberfolio/host/code_wheel_store.h"
#include "amberfolio/host/held_keys.h"
#include "amberfolio/host/host_services.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/host/slot_store.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/launcher_view.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/save_layer.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "audio_device.h"
#include "desktop_window.h"
#include "directory_vfs.h"
#include "disk_reports.h"
#include "edge_dump.h"
#include "install.h"
#include "journal_host.h"
#include "keymap.h"
#include "launch_config.h"
#include "mounted_vfs.h"
#include "options.h"
#include "overlays.h"
#include "pacing.h"
#include "press_spec.h"
#include "recording.h"
#include "rehash.h"
#include "run_reports.h"
#include "seam_host.h"
#include "sound_report.h"
#include "user_files.h"
#include "window_checks.h"
#include "wiring.h"

// <cstdio> rather than std::format/std::print, and not only for the wasm
// host's reason (bundle size). libc++ gates std::format's floating-point
// path behind macOS 13.3 availability, so *any* std::format call fails to
// compile against a deployment target of 11.0 — which is what the macos
// preset asks for. Revisit if that floor ever rises.

namespace {

using namespace amberfolio;
using sdl::audio_bridge;
using sdl::door_rule_log;
using sdl::options;
using sdl::scripted_press;
using sdl::scripted_pull;
using sdl::stderr_diagnostics;
using sdl::verify_report;
using sdl::watch_log;
using sdl::wired_machine;

}  // namespace

int main(int argc, char** argv) try {
  options opts = sdl::parse(argc, argv);
  if (!opts.valid) {
    return EXIT_FAILURE;
  }
  // And then the settings file, which is the other half of the command
  // line (#382): flag > config > default, once, here. It can end the run
  // before it starts -- a launch with no game directory yet is a first
  // run, which says what it needs and is **not an error**.
  if (!sdl::settle_config(opts)) {
    return opts.first_run ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  sdl::directory_filesystem host_files(opts.root);
  if (!host_files.usable()) {
    std::fprintf(stderr, "amberfolio: %s is not a directory\n",
                 opts.root.string().c_str());
    return EXIT_FAILURE;
  }

  // Where that directory sits on the machine's drive, and the directory
  // the program starts in: the same one, as the copies' own launchers
  // have it (install.h, #397). The root unless something says otherwise.
  const sdl::install_choice placed =
      sdl::settle_install(opts.install, opts.program, host_files);
  if (!placed.ok) {
    std::fprintf(stderr, "amberfolio: --install %s is not a DOS directory\n",
                 opts.install.c_str());
    return EXIT_FAILURE;
  }
  const machine::dos_path& install = placed.directory;
  sdl::mounted_filesystem mounted(host_files, install);
  machine::filesystem& files =
      install.is_root() ? static_cast<machine::filesystem&>(host_files)
                        : static_cast<machine::filesystem&>(mounted);

  stderr_diagnostics log;
  wired_machine wired(&log);
  machine::machine& box = *wired.box;
  // The host services a seam may call out to (M5-D1, #169). Attached
  // before anything is loaded and never detached: it is wiring, like an
  // attached device, and it changes nothing at all about a run with
  // every seam off — no handler runs, so nothing calls out. The same
  // object the web host attaches (hosts/common), so a callout means the
  // same thing on both.
  host::host_services services;
  box.seams().set_host(&services);
  // And the sidecars beside the saves (M5-E2c #173, #351). Enabled here
  // and attached after the disk is mounted, below, because `attach()`
  // reads where the copy saves.
  //
  // **Once, here, and nowhere else** (#385). A host has one moment to
  // decide this and it is before the disk is read, which is why the
  // desktop's question is asked in `settle_config` and not from a panel
  // a player can click during a game.
  services.slots().enable(opts.save_sidecars);
  log.set_slot_store(&services.slots());
  // And the other thing that door drives (M5-E4, #175): the text the
  // journal reader is answered out of. It lives here, for the whole run,
  // because an ingestion is one moment and the reading is every moment
  // after it — `ingest_journal` fills this object rather than one of its
  // own, and a run with no `--journal` reads what a previous one wrote.
  host::journal_store journal_text;
  services.set_journal_store(&journal_text);
  // And the third (M6-C1b, #292): the copies whose code-wheel challenge
  // has been answered. Read below, once the program is loaded and there
  // is a fingerprint to look up; written when the seam says somebody has
  // just answered one.
  host::code_wheel_store code_wheel;
  services.set_code_wheel_store(&code_wheel);
  // The machine to be, before anything runs (machine/clock.h). Printed
  // whenever it is not the default, for the reason a seam is: a run at a
  // speed nobody expected is a different run, and a log that did not say
  // so would be describing the wrong machine.
  box.set_speed(opts.speed);
  if (opts.speed != machine::default_speed) {
    std::fprintf(
        stderr, "amberfolio: speed %s, about %llu steps a second\n",
        sdl::speed_name(opts.speed),
        static_cast<unsigned long long>(machine::steps_per_second(opts.speed)));
  }

  if (opts.fast == 0.0) {
    std::fprintf(stderr, "amberfolio: fast-forward unpaced\n");
  } else if (opts.fast != 1.0) {
    std::fprintf(stderr, "amberfolio: fast-forward %gx wall time\n", opts.fast);
  }

  // The program reads the disk through the launcher's view
  // (launcher_view.h, #404); this host reads and writes `files`.
  machine::launcher_view launched(files);
  box.set_filesystem(launched);

  // The directory the program starts in, before the sidecars below look
  // for the save directory in it (machine/dos.h). Said when it is not the
  // root, for a seam's reason: a run started elsewhere is another run.
  if (box.dos().set_current_directory(files, install) !=
      machine::vfs_error::none) {
    std::fprintf(stderr, "amberfolio: %s is not a directory here\n",
                 sdl::spell_vfs_path(install).c_str());
    return EXIT_FAILURE;
  }
  if (!install.is_root() || placed.from != std::string_view("the root")) {
    std::fprintf(stderr, "amberfolio: install %s (from %s) is current\n",
                 sdl::spell_vfs_path(install).c_str(), placed.from);
  }
  sdl::report_copy_facts(files, install);

  // And now there is a disk to read the exploration sidecar off (M5-E2c,
  // #173). Before the program is loaded, so a panel opened in the first
  // seconds of a run already has last night's map in it. A no-op unless
  // `--save-sidecars` asked for it. The read log is the other half and
  // waits for the journal store below, which is parsed wholesale and
  // would throw away anything put there first (`slot_store.h`).
  //
  // **Once** (#385), and this is the call the rule is about: the read it
  // does replaces every record in `box.automap()`, so calling it again
  // later would hand a player an older map than the one they are looking
  // at. One machine, one attach.
  services.slots().attach(box);

  // The program is named from the directory given, which is the current
  // one — `START.EXE` is `\POOLRAD\START.EXE` on a copy laid out there,
  // as it would be typed at `C:\POOLRAD>`.
  const machine::vfs_result<machine::dos_path> where = machine::canonicalize(
      box.dos().current_directory(),
      std::span<const char>(opts.program.data(), opts.program.size()));
  if (!where.ok()) {
    std::fprintf(stderr, "amberfolio: %s is not a usable DOS name\n",
                 opts.program.c_str());
    return EXIT_FAILURE;
  }

  // The identity of the player's file, printed before anything runs.
  //
  // A fact about the file, not content from it (CONTRIBUTING.md), and the
  // one M4's fingerprint table will key its seams on (PLAN.md §2, §5). It
  // is printed even when the load then fails, because "which file was
  // this" is the first question anybody asks of a boot log and a load
  // that failed is exactly when it matters.
  const machine::vfs_result<sha256_digest> identity =
      machine::fingerprint_file(files, where.value);
  if (identity.ok()) {
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(identity.value, hex));
    std::fprintf(stderr, "amberfolio: load %s sha256=%s\n",
                 opts.program.c_str(), hex.data());
  } else {
    std::fprintf(stderr,
                 "amberfolio: load %s could not be fingerprinted"
                 " (vfs error %u)\n",
                 opts.program.c_str(), static_cast<unsigned>(identity.error));
  }

  // Asked for before the program is loaded, so that the ring covers the
  // whole run rather than starting a few instructions into it. It is a
  // setting on the machine and survives every reset (trace.h).
  box.trace().enable(opts.trace);
  log.set_tracing(opts.trace);

  const machine::loader_result<machine::loaded_program> loaded =
      machine::load_program(box, files, where.value,
                            std::span<const char>(opts.command_tail.data(),
                                                  opts.command_tail.size()));
  if (!loaded.ok()) {
    std::fprintf(stderr, "amberfolio: cannot load %s (loader error %u)\n",
                 opts.program.c_str(), static_cast<unsigned>(loaded.error));
    return EXIT_FAILURE;
  }
  std::fprintf(stderr,
               "amberfolio: load psp=%04X image=%04X entry=%04X:%04X"
               " stack=%04X:%04X tail=%zu\n",
               loaded.value.psp_segment, loaded.value.load_segment,
               loaded.value.entry_cs, loaded.value.entry_ip,
               loaded.value.entry_ss, loaded.value.entry_sp,
               opts.command_tail.size());

  // The seams the run was asked for, now that there is a program to key
  // them on. Enabled after the load and before the first step, and each
  // one printed: a run with a seam on is not the same run as one without
  // it, and a log that did not say so would be describing the wrong
  // machine (machine/seam.h).
  //
  // First the identity: which known edition the fingerprint names, or
  // that it names none — in which case the game runs as a plain machine
  // and every seam is unavailable (machine/edition.h, PLAN.md §5). Said
  // either way, because "no seams for this file" is a finding and not a
  // silence.
  if (identity.ok()) {
    box.seams().loaded(identity.value, loaded.value.load_segment);
    if (const machine::edition* known = box.seams().known_edition();
        known != nullptr) {
      std::fprintf(stderr, "amberfolio: edition %.*s\n",
                   static_cast<int>(known->name.size()), known->name.data());
    } else {
      std::fprintf(stderr,
                   "amberfolio: edition unrecognized - no seams are"
                   " available for this program\n");
      sdl::report_unrecognized_edition(files);
    }
    sdl::report_save_layer_table(box, files, opts);
  }
  // The documents the player presented, before the seams that may be
  // gated on them (#171). Before, and not after, so that a gated seam's
  // very first `enable()` sees the gate satisfied and its startup line
  // says `armed` rather than saying `inert` and then quietly changing
  // its mind at the first overlay read.
  //
  // What each one turned out to be is kept as well as printed, because
  // the panel says it too (#384): a player who launched with
  // `--document` and never looks at a terminal reads the same two lines
  // under the table that a player who dropped the file on the window
  // does.
  std::vector<std::string> document_notice;
  for (const std::string& path : opts.documents) {
    document_notice = sdl::present_document(box, path);
  }

  // And the one thing a person shows this machine that is not a file: that
  // they have already answered the code-wheel challenge (#291). Before the
  // seams, so that a run that knows it, and turns the seam on, never reaches
  // the challenge at all (`settle_code_wheel()` has the order).
  sdl::settle_code_wheel(opts, box, code_wheel);

  // And the journal (#174): ingested, or read from where a previous run left
  // it, and its read log put where the machine draws it from.
  sdl::bring_up_journal(box, opts, journal_text);

  // The seams the run was asked for, now that everything they may be gated
  // on is in place.
  if (!sdl::enable_requested_seams(box, opts)) {
    return EXIT_FAILURE;
  }

  if (opts.list_seams) {
    // The listing #98 asks for, in the state the run would have started
    // in, and then nothing runs: a listing is a question, and the answer
    // is the whole of what was asked for.
    sdl::report_seam_listing(box);
    return EXIT_SUCCESS;
  }

  // --- Replay: load the recording and become the run it describes -------
  //
  // The recording decides the speed, the seams and every key (machine/
  // replay.h); this host's job is to be that machine and check each
  // checkpoint. Set up before SDL, so a mismatch of the initial
  // conditions is reported without a window ever opening.
  std::string replay_text;
  machine::replay_player player;
  const bool replaying = !opts.replay_path.empty();
  if (replaying && !sdl::load_replay(opts, box, files, replay_text, player)) {
    return EXIT_FAILURE;
  }

  // --- Record: the preamble now, the stream as the run goes -------------
  sdl::recorder recording;
  if (!recording.open(opts, box, files)) {
    return EXIT_FAILURE;
  }

  // --- Dump: the edge list, written as the run makes it (M4-A1, #106) ---
  // `edge_dump.h` has the argument.
  sdl::edge_dump edges;
  edges.open(opts, box);

  // --- The date, said once, before the first instruction (#320) ----------
  // `recording.h` and `seed_wall_clock()` have the argument.
  sdl::seed_wall_clock(opts, box, replaying, recording);

  // The debug cheat (#301), after the seeding above and not before it
  // (#343): `cite_everything()` has the argument.
  if (opts.cite_all_journal) {
    sdl::cite_everything(box, journal_text);
  }

  // The tick of the last checkpoint written, so that the one taken where
  // the run ends is not a second copy of the one the cadence had just
  // taken. Two checkpoints at one tick would verify, and would say the
  // same thing twice.
  machine::ticks checkpointed = machine::never;

  // Whether this frame delivered an input — a key, or a seam trigger
  // somebody pulled (#161). A sparse cadence must not thin out the
  // moments a recording exists to pin: what a game session is evidence
  // for is that the machine answered *this* keystroke the way it did,
  // and a checkpoint on the far side of the frame that carried one is
  // where that is visible. A pull is the same kind of moment and gets
  // the same treatment.
  bool input_this_frame = false;

  // Pull one seam's trigger, wherever the ask came from — the host key
  // below, or `--pull ID@FRAME` (#161). Recorded as a `pull` line at the
  // machine's own tick, exactly as a keystroke is, and for the same
  // reason: it is something a person did to a running machine at a
  // moment, and a replay that had the seam on but not the pull would
  // reproduce a run in which the cheat never fired (machine/replay.h).
  const auto pull_trigger = [&](std::string_view id) {
    const machine::seam_error why = box.seams().pull(id, box.time());
    if (why != machine::seam_error::none) {
      std::fprintf(stderr, "amberfolio: seam %.*s not pulled (%s)\n",
                   static_cast<int>(id.size()), id.data(),
                   sdl::seam_refusal(why));
      return false;
    }
    // What the pull is waiting for, said at the moment it is made. A
    // trigger acts at a CS:IP breakpoint, so "immediately" means "at the
    // next arrival at the point" and nothing else can (machine/seam.h);
    // an inert seam is not even that, and a person who is told neither
    // has a button that did nothing.
    const machine::seam_status row = box.seams().status(id);
    std::fprintf(stderr, "amberfolio: seam %.*s pulled - %s\n",
                 static_cast<int>(id.size()), id.data(),
                 row.armed ? "acts at the next arrival at its point"
                           : "inert; its module is not resident");
    machine::replay_event line{};
    line.kind = machine::replay_line::pull;
    line.at = box.time();
    if (id.size() <= machine::replay_max_id) {
      for (std::size_t i = 0; i < id.size(); ++i) {
        line.id[i] = id[i];
      }
      line.id_length = id.size();
      recording.line(line);
    } else if (recording.is_open()) {
      // A recording that quietly left a pull out would be a recording of
      // a run that did not happen. Said out loud instead; the id would
      // have to be longer than any seam in this tree for it.
      std::fprintf(stderr,
                   "amberfolio: seam %.*s pulled but NOT recorded - its id"
                   " is longer than a recording may name\n",
                   static_cast<int>(id.size()), id.data());
    }
    input_this_frame = true;
    return true;
  };

  // The host key's whole job: pull every trigger that is on. One key for
  // however many triggered seams a build carries, because a key per seam
  // is a key this host does not have to spend — an 83-key XT board has
  // only so many codes it never uses, and the toggle surface is where
  // seams are chosen (`--seam`, the page's checkboxes).
  const auto pull_every_trigger = [&]() {
    unsigned pulled = 0;
    for (std::size_t i = 0; i < box.seams().count(); ++i) {
      const machine::seam_status row = box.seams().status(i);
      if (row.state != machine::seam_state::on || !row.trigger) {
        continue;
      }
      if (pull_trigger(row.id)) {
        ++pulled;
      }
    }
    if (pulled == 0) {
      std::fprintf(stderr,
                   "amberfolio: nothing to pull - no seam that takes a"
                   " trigger is on\n");
    }
  };

  const std::uint32_t init_flags =
      opts.headless ? 0U : (SDL_INIT_VIDEO | SDL_INIT_AUDIO);
  if (!SDL_Init(init_flags)) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  // The parsed presses, with room to record which have gone. `opts` is
  // what the command line said and stays that way.
  std::vector<scripted_press> presses = opts.presses;
  std::vector<scripted_pull> pulls = opts.pulls;
  watch_log watch_seen;

  sdl::desktop_window screen;
  audio_bridge bridge;
  bridge.box = &box;

  // The listening level (#148). `opts` is what the command line said and
  // stays that way; these two are where it is *now*, because F11 and F12
  // move them while the run is going. Neither is machine state, neither
  // is recorded, and nothing in the machine can observe either — a run at
  // 25% is the same run as one at 100%, down to the last edge.
  sdl::listening_level level{.volume = opts.volume, .muted = opts.muted};

  const auto apply_level = [&bridge, &level]() {
    bridge.gain.set(level.gain());
  };

  // Before the device is opened, so that a run asked to start muted has
  // never played a sample at any other level.
  apply_level();
  if (level.muted || level.volume != 1.0F) {
    level.say();
  }

  // `--dump`'s WAV, sized once and never resized: the audio thread
  // appends to it and must not allocate. A minute of virtual time is
  // enough to hear a title sequence through and small enough to be free
  // on any machine that can run this at all; a run past it keeps going
  // and the file simply ends where the buffer did, which is said out
  // loud rather than left to be noticed.
  if (!opts.dump_prefix.empty()) {
    bridge.capture.assign(
        std::size_t{sdl::audio_sample_rate} * sdl::dump_audio_seconds, 0.0F);
  }

  if (!opts.headless && !screen.open(opts, presses, bridge)) {
    return EXIT_FAILURE;
  }

  // One virtual frame is the renderer's own period, so the loop and the
  // renderer agree by construction rather than by two constants matching.
  const machine::ticks frame_ticks = machine::renderer::frame_period;
  std::uint64_t presented = 0;
  std::uint64_t frame_index = 0;
  verify_report report;
  bool quit = false;

  // The keys the window has posted a make for and no break yet (#313),
  // so a loss of focus can let go of them. Empty during a replay, which
  // posts nothing from the window.
  host::held_keys held;

  // Post one key event to the machine, from the window or from the
  // host's own hand at focus loss. Recorded where it is posted and at
  // the tick it is posted at: the machine's clock is the only stamp a
  // key has, and the post is the only moment the machine can see one.
  const auto post_key = [&](std::uint8_t code, machine::key_action action) {
    box.post_key(code, action);
    held.note(code, action);
    ++report.keys;
    machine::replay_event line{};
    line.kind = machine::replay_line::key;
    line.at = box.time();
    line.scancode = code;
    line.action = action;
    recording.line(line);
    input_this_frame = true;
  };

  // --- The on-screen keyboard and the toggle panel (#377, #383) ---------
  //
  // `overlays.h` has both: what they are, which keys and buttons they take
  // and where a choice goes. A key the keyboard commits goes out through
  // `post_key` above, so it is counted, recorded and let go of at a focus
  // loss exactly like a key struck at the window.
  sdl::overlays furniture(box, opts, replaying, post_key,
                          std::move(document_notice));

  // How much of the speaker's timeline one frame of virtual time is
  // worth, in samples. Only pulled when there is no audio device doing
  // the pulling — see the call site.
  const auto frame_samples = static_cast<std::size_t>(
      (static_cast<std::uint64_t>(sdl::audio_sample_rate) * frame_ticks) /
      machine::pit_input_hz);
  std::vector<float> headless_audio(frame_samples);

  machine::run_end ended = machine::run_end::stopped;
  std::uint64_t overlays_printed = 0;
  door_rule_log doors_printed;

  // The player is primed before the first slice: `next_tick()` answers
  // only once `apply()` has looked at the recording, and an event
  // recorded at tick 0 — a key on the very first frame — has to be
  // delivered before the machine has taken a step, not after.
  sdl::rehash_collector rehashed;
  const auto apply_replay = [&]() { rehashed.apply(player, box); };

  if (replaying) {
    apply_replay();
  }

  // The schedule this run paces against: one fixed instant, taken once,
  // rather than a fresh "now" every time round the loop (#343). A frame
  // asks for `frame_ticks / pit_input_hz` of wall time and gets it from
  // `sleep_for` — but `sleep_for` is a *minimum*, never an exact amount,
  // and every extra fraction of a millisecond the OS scheduler hands
  // back late was, before this, gone for good: the next frame measured
  // its own budget from its own fresh start and so never knew the last
  // one had overrun. Over a boot that is a rounding error; over a
  // session played for an hour it is minutes, and it is one-directional
  // — virtual time can only fall behind this way, never catch up on its
  // own, which is exactly backwards from what "paces against the wall"
  // (docs/first-light.md) is supposed to mean.
  //
  // Comparing every frame's deadline against one fixed origin fixes
  // that without touching what this file's top comment forbids: the
  // machine still runs forward by exactly `frame_ticks` a call, once a
  // loop, whatever this measured. Only the *sleep* target changes, from
  // "however long is left of the frame that just ran" to "however long
  // until the schedule says this frame should end" — so a frame that
  // slept a hair too long borrows nothing back by running faster; it
  // simply sleeps a hair less next time, the same way a real clock
  // recovers from a delayed tick without its second hand ever moving
  // ahead of a second a tick early.
  const std::chrono::steady_clock::time_point pace_origin =
      std::chrono::steady_clock::now();

  for (;;) {
    // The store, if it has said it moved (M5-E4b, #222). Since M5-C1
    // (#229) the flag is the store's rather than the log's, so a
    // correction made any way at all is written. Once a frame rather than
    // once a write, because a write is one instruction and a file is not;
    // and a flag rather than a timer, because most frames have nothing to
    // say.
    //
    // **The log is not one of the things that raises it** (#351). It is
    // not in this file, so a citation leaves a player's transcription
    // alone; it reaches a file beside the saves when the program saves.
    if (journal_text.changed()) {
      sdl::save_journal_store(opts, journal_text);
      journal_text.clear_changed();
    }
    // And the code wheel's answer, the same way and for the same reason
    // (M6-C1b, #292) — except that this one moves at most once in a run,
    // at the instant a person gets the question right. Written then
    // rather than at the end, so that a session which crashes an hour
    // later has still kept it.
    if (code_wheel.changed()) {
      sdl::save_code_wheel_store(opts, code_wheel);
      code_wheel.clear_changed();
      std::fprintf(stderr,
                   "amberfolio: code wheel answered - remembered for this"
                   " copy\n");
    }
    if (box.stopped()) {
      ended = machine::run_end::stopped;
      break;
    }
    // A replay that reached the recording's `end` has verified all of it,
    // and one that diverged has nothing left worth running: every tick
    // after the first difference is about a machine the recording never
    // described. Either way this is where the loop ends, and the report
    // below says which it was.
    //
    // `host_quit` because that is what this is from the machine's side:
    // it was still running and something outside it said stop. The
    // machine's own ending is checked first, above, so a replay of a
    // program that exits still reports the exit.
    if (replaying && player.status() != machine::replay_status::ok) {
      ended = machine::run_end::host_quit;
      break;
    }
    if (quit) {
      ended = machine::run_end::host_quit;
      break;
    }
    if (opts.step_budget != 0 && box.steps() >= opts.step_budget) {
      ended = machine::run_end::step_budget;
      break;
    }
    if (opts.tick_budget != 0 && box.time() >= opts.tick_budget) {
      ended = machine::run_end::tick_budget;
      break;
    }

    // Before the frame is run rather than after: `frame_index` is the
    // number `--press KEY@FRAME` matches on, and a still named for a
    // frame should be the screen that frame's keystroke was answered
    // against, not the one after it.
    if (opts.dump_every != 0 && frame_index % opts.dump_every == 0) {
      sdl::write_still(opts.dump_prefix, frame_index, box);
    }

    // The scripted pulls this frame owes (#161). Outside the windowed
    // block on purpose: a pull is a call into the engine and not an SDL
    // event, so it needs no window, and a scripted run that wants one is
    // exactly the headless kind.
    for (scripted_pull& pull : pulls) {
      if (!pull.done && pull.frame == frame_index) {
        static_cast<void>(pull_trigger(pull.id));
        pull.done = true;
      }
    }

    if (!opts.headless) {
      // Pushed before the poll, so the events this frame owes are on the
      // queue by the time the queue is read - one loop iteration, not
      // two.
      for (scripted_press& press : presses) {
        if (!press.done && press.frame == frame_index) {
          if (press.edges != sdl::press_edges::up) {
            sdl::push_key_event(screen.window(), press.code, true);
          }
          if (press.edges != sdl::press_edges::down) {
            sdl::push_key_event(screen.window(), press.code, false);
          }
          press.done = true;
        }
      }

      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
          quit = true;
        } else if ((event.type == SDL_EVENT_KEY_DOWN) && !event.key.repeat &&
                   (event.key.scancode == SDL_SCANCODE_F11 ||
                    event.key.scancode == SDL_SCANCODE_F12)) {
          // The host's own two keys (#148), and the only two it takes.
          // An 83-key XT board has ten function keys, so `xt_scancode()`
          // answers 0 for both and the emulated program loses nothing by
          // this; `keymap_test.cpp` pins that assumption rather than
          // leaving it as a belief about a table.
          //
          // Handled during a replay as well, and deliberately: a
          // recording decides what the *machine* did, and how loudly the
          // person watching it wants that played back is not one of
          // those things. Nothing here is posted, recorded or hashed.
          if (event.key.scancode == SDL_SCANCODE_F11) {
            level.toggle_mute();
          } else {
            level.louder();
          }
          apply_level();
          level.say();
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                   event.key.scancode == SDL_SCANCODE_PAUSE) {
          // The host's third key, and the trigger a person pulls (#161).
          //
          // Pause/Break, on the same argument F11 and F12 were chosen
          // on: an 83-key XT board has no such key at all. Pausing on
          // one was Ctrl and the keypad's Num Lock; the dedicated
          // Pause/Break arrived with the 101-key Enhanced board, where
          // it is the one E1-prefixed sequence, and this machine's wire
          // is set 1 with no prefixes on it. `sdl::xt_scancode()`
          // answers 0 for it, so the emulated program loses nothing —
          // `keymap_test.cpp` pins that, as it does for the other two.
          //
          // Break is also the right word for it: a person interrupting
          // the program from outside is exactly what a debug trigger is.
          // The keypad's `/` and its Enter are the other two keys an XT
          // board has not got, and both were passed over for being
          // inside the cluster this game's movement keys are — a key you
          // can hit by accident mid-fight is the wrong key for a cheat.
          //
          // Unlike F11 and F12 this one reaches the machine, so it is
          // refused during a replay for the same reason a keystroke at
          // the window is: an input the recorded run never had.
          if (replaying) {
            std::fprintf(stderr,
                         "amberfolio: a replay's pulls are the"
                         " recording's\n");
          } else {
            pull_every_trigger();
          }
        } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
          // The keyboard has gone to another window, and so will the
          // break code of anything held at this moment (#313). Let go of
          // all of it here, through the same path a key-up from the
          // window takes — posted, counted, recorded — so the BDA shift
          // flags come down the way they would had the person released
          // the keys, and a replay of this run releases them at the same
          // tick. held_keys.h says why this is a host's job and why the
          // BDA is never written directly.
          for (const std::uint8_t code : held.release_all()) {
            post_key(code, machine::key_action::up);
          }
          furniture.focus_lost();
        } else if (event.type == SDL_EVENT_DROP_FILE) {
          // **The document control** (#384): a file dropped on this
          // window is hashed, presented, and answered for — the desktop's
          // half of "any PDF in, what it was recognised as out". A flag
          // is not a control a player finds, and M6's exit criterion is
          // that they do not have to read source code to use what this
          // build has.
          //
          // Whatever was dropped, not whatever is named `.pdf`: the
          // matching is on the bytes, so a document a player renamed
          // still matches, and a file that is something else entirely is
          // reported with its hash rather than guessed at. The panel is
          // opened on the answer, because a drop that printed to a
          // terminal nobody is looking at is a control that did nothing.
          if (event.drop.data == nullptr) {
            // A drop with no filename is SDL's begin/complete pair; the
            // file itself arrives on its own event.
          } else if (replaying) {
            // A recording's documents are its own initial condition, the
            // same rule the panel's toggles follow (docs/replay.md).
            std::fprintf(stderr,
                         "amberfolio: a replay's documents are the"
                         " recording's\n");
          } else {
            furniture.document_presented(
                sdl::present_document(box, event.drop.data));
          }

        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
          furniture.mouse_button_down(event, screen.renderer());
        } else if (event.type == SDL_EVENT_KEY_DOWN ||
                   event.type == SDL_EVENT_KEY_UP) {
          if (!furniture.take_key(event)) {
            const bool down = event.type == SDL_EVENT_KEY_DOWN;
            const std::uint8_t code = sdl::xt_scancode(event.key.scancode);
            // A replay's keys are the recording's, delivered by the player
            // at the ticks it names. A key struck at the window during one
            // would be an input the recorded run never had, so the window
            // still closes and nothing else gets through.
            //
            // The operating system's repeats of a held key go through as
            // the makes they are (#426): the original keyboard repeated
            // in its own hardware, the machine cannot tell a repeat from
            // a make (`key_action::down`), and the web host posts every
            // keydown the same way. Only the host's own controls above
            // ignore them.
            if (code != 0 && !replaying) {
              post_key(code, down ? machine::key_action::down
                                  : machine::key_action::up);
            }
          }
        }
      }
    }

    // Virtual time first, and to a boundary the machine chose. Nothing
    // about how long the last frame took on the wall gets to influence
    // how much machine time passes here. A budget may bring the boundary
    // closer; nothing may push it further out.
    box.run(sdl::slice_end(box, frame_ticks, opts.step_budget, opts.tick_budget,
                           replaying ? player.next_tick() : machine::never));
    sdl::drain_console(box);
    edges.drain(box);
    if (opts.trace) {
      sdl::print_overlay_loads(box, overlays_printed);
      sdl::print_door_rule(box, doors_printed, frame_index);
    }
    if (!opts.watches.empty()) {
      sdl::print_watch(box, opts.watches, watch_seen, frame_index);
    }

    // Then the events this slice ran up to, delivered and checked before
    // anything else looks at the machine: a checkpoint is a statement
    // about the machine at a tick, and the display and the speaker are
    // read from it just below.
    if (replaying) {
      apply_replay();
    }

    // A checkpoint every `--record-every` frames, and every frame that
    // posted a key. The boundary is the machine's own — frame ticks off
    // its clock, never the wall — so a recording made on one target names
    // ticks a replay reaches on every other. Taken after the slice and
    // before the frame is presented, which is the moment the run has just
    // finished being somewhere describable.
    if (recording.is_open() &&
        (input_this_frame || frame_index % opts.record_every == 0)) {
      recording.line(machine::checkpoint_of(box));
      checkpointed = box.time();
    }
    input_this_frame = false;

    // With no audio device there is nobody pulling the speaker, so a
    // `--dump` run has to pull it here, on the machine thread — which the
    // threading contract permits ("exactly one thread", and this is it).
    // Not done when a device is open: two consumers of one timeline would
    // each get half the samples (platform.h).
    if (!screen.has_audio_device() && !bridge.capture.empty()) {
      static_cast<void>(
          box.audio().render(headless_audio, sdl::audio_sample_rate));
      sdl::capture_samples(bridge, headless_audio);
    }

    // The overlay is a reason to present as much as a new frame is: a
    // focus that moved over a picture the game is not redrawing would
    // otherwise not appear until the game moved (#377).
    // And the panel is a live readout: while it is up, `fired` is a
    // number that has to move when a handler runs, whether or not the
    // game happened to redraw anything (#383).
    if (!opts.headless && (box.display().generation() != presented ||
                           furniture.wants_present())) {
      presented = box.display().generation();
      screen.present(box, furniture, opts.verify, report);
    }

    if (!opts.headless && opts.fast != 0.0) {
      sdl::wait_for_frame(pace_origin, frame_ticks, opts.fast, frame_index);
    }

    ++frame_index;
  }

  // The last slice's edges. The loop drains after each slice, and it
  // leaves by a `break` that is above that drain — so without this, every
  // edge the final slice published would be in the machine's log and in
  // no file, which for a run that ends *because* of what it just did is
  // the part worth reading.
  edges.drain(box);

  // Where the recording stops, written before SDL comes down so that a
  // run whose teardown goes wrong still leaves a recording saying how far
  // it got. A player that reaches this line verified everything before
  // it; one that runs out of text without it says so.
  recording.end_run(box, checkpointed);

  // The audio stream first, and before the counters below are read: it is
  // what stops the callback thread, and until it has returned the two
  // tallies are still being written to.
  screen.close();

  // The stop report: the whole point of this host in M3, formatted in
  // core so that the browser prints the same sentence (machine/report.h).
  // After SDL is down, so that nothing SDL writes on its way out can land
  // in the middle of it.
  sdl::report_stop(box, ended, opts.trace);

  // What each enabled seam did, what it asked of the host and what the
  // sidecars did with it (seam_reports.h).
  sdl::report_seam_outcomes(box);
  sdl::report_host_services(box, services);
  sdl::report_sidecars(services);

  // The VFS door (M5-D2, #170), over the directory this host was pointed
  // at — the same three operations the ABI gives a browser over its
  // in-memory filesystem, so the two hosts' answers about a disk can be
  // compared rather than described.
  //
  // After the run, because what they exist to answer is what the run
  // left behind. Removals last: a listing that happened after them would
  // be a listing of a disk nobody had.
  sdl::report_vfs(files, opts);
  sdl::report_save_layer_files(box, files, opts);

  sdl::report_dump(opts, box, bridge, edges);
  sdl::report_audio(opts.verify, box, level);

  // What the recording said, and whether this machine was it. Printed
  // before `--verify`'s tally and answered before the program's own exit
  // code, for the reason `--verify` is: a run asked to check itself
  // against a recording is answering the check's question, not the
  // program's. A replay that did not reach the recording's `end` failed,
  // whatever else it did — a run cut short verified a prefix, and a
  // prefix is not the run.
  if (replaying) {
    std::array<char, machine::replay_report_capacity> line{};
    static_cast<void>(player.report(line));
    std::fputs(line.data(), stderr);
    if (!player.done()) {
      std::fflush(stdout);
      return EXIT_FAILURE;
    }
    if (player.rehashing() &&
        !sdl::write_rehashed(opts.rehash_path, replay_text, rehashed.lines())) {
      return EXIT_FAILURE;
    }
  }

  // And where a recording went, so that the file's name and the tick it
  // stops at are in the same log as the run that made it.
  if (recording.is_open()) {
    std::fprintf(stderr, "amberfolio: record %s tick=%llu steps=%llu\n",
                 opts.record_path.c_str(),
                 static_cast<unsigned long long>(box.time()),
                 static_cast<unsigned long long>(box.steps()));
  }

  if (opts.verify && !sdl::report_verify(report, box, bridge)) {
    return EXIT_FAILURE;
  }

  const machine::stop_record& stop = box.stop();
  if (ended == machine::run_end::stopped &&
      stop.reason == machine::stop_reason::program_exited) {
    std::fflush(stdout);
    return static_cast<int>(stop.exit_code);
  }
  if (ended == machine::run_end::host_quit) {
    return EXIT_SUCCESS;
  }

  // Everything else is a run that did not finish: a machine that refused
  // something, or a budget that ran out with the program still going.
  // The report above has already said which and where; this is only the
  // process's answer, and it is failure either way because in neither
  // case did the program get to choose one.
  std::fflush(stdout);
  return EXIT_FAILURE;
} catch (const std::exception& e) {
  // A function-try-block on main, because everything above allocates -
  // the megabyte of machine, the frame buffer, the host's own strings -
  // and a host that lets an allocation failure escape as an unhandled
  // exception tells the player nothing at all.
  std::fprintf(stderr, "amberfolio: %s\n", e.what());
  return EXIT_FAILURE;
} catch (...) {
  std::fprintf(stderr, "amberfolio: unknown error\n");
  return EXIT_FAILURE;
}
