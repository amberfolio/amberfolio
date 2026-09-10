// SPDX-License-Identifier: AGPL-3.0-only
//
// The one question this host asks a person (#385).
//
// `--save-sidecars` writes `\SAVE\AFMAP.DAT` and `\SAVE\AFSEEN.DAT` into
// the player's own game directory (`host/slot_store.h`). It is the one
// surface in M6 that changes a file somebody else owns, so this build
// asks before it does, once, and keeps the answer with the rest of the
// settings (`desktop_config.h`).
//
// A question and its answer, and nothing else: the printing and the
// reading of a line are `main.cpp`'s, because a unit that owned stdin
// could not be tested without one. What is here is the three decisions —
// *whether* this launch is a moment to ask anybody anything, *what* the
// question says, and *what* an answer to it was — and every one of them
// is a plain function (`sidecar_consent_test.cpp`).
//
//
// When this host asks, and the three runs it must never ask in
// -----------------------------------------------------------
//
// A prompt waits for somebody, and a prompt on a run nobody is watching
// is a run that never finishes. Worse than that: a *sidecar written by a
// verification run changes the disk every recorded session pins*
// (`tests/sessions/README.md`), and `scripts/sweep.py` answers a disk it
// cannot match by skipping the session and naming it rather than failing
// — so a question answered the wrong way, or answered by a script that
// did not know it was answering one, turns the whole session library into
// skips without anything going red.
//
// So the rule is the other way round from a normal default: this asks
// only when it is certain a person is there, and every other launch keeps
// the sidecars off unless a flag said otherwise. `nobody_is_asked` below
// is that certainty, spelt out:
//
//   * a **replay** never gets here at all — `settle_config` reads no
//     config for one (`docs/hosts.md` §2a) and returns before the ask;
//   * a **headless** run has no window, so nobody is watching a game;
//   * a run with **`--press`** or **`--pull`** is being driven by a
//     script, and a script pressing keys at named frames is not a person
//     answering a question;
//   * a **`--record`** run is making the thing later runs are compared
//     against, and turning the sidecars on in the middle of one is how
//     the disk it pins stops matching;
//   * a **`--dump`** run is an instrument pointed at a problem;
//   * a **`--verify`** run is a check.
//
// And it asks only when there is somewhere to keep the answer. A config
// this build could not read is left exactly where it is (CLAUDE.md's
// "log, don't fake"), which means there is nowhere to write an answer, so
// nothing is asked: a question whose answer cannot be remembered is a
// question asked again every launch.
//
// **An unanswered question is not a no.** `read_sidecar_answer()` refuses
// anything but yes and no, including an empty line and a stdin that was
// closed, and `main.cpp` then writes nothing at all. The sidecars stay
// off for the run — off is what every seam and every file of this
// project's own is until somebody asks for it — but nothing is written
// down, so the question comes back the next time a person is there.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace amberfolio::sdl {

/// What this launch should do about the question, and why.
///
/// A reason rather than a bool because the reasons are the content: four
/// of the five are a launch that must be left alone, and each of them is
/// a different sentence about a different kind of run.
enum class sidecar_ask : std::uint8_t {
  /// Ask, and remember what comes back.
  ask,
  /// `--save-sidecars` or `--no-save-sidecars` was on the command line.
  /// A flag is an answer, for this run, and is never overruled.
  said_on_the_command_line,
  /// The config already carries the answer. This is the "once" in "asked
  /// once".
  already_answered,
  /// There is no config to write an answer into — `--no-config`, a
  /// platform that does not say where per-user data lives, or a file this
  /// build refused to read and will not overwrite.
  nowhere_to_remember,
  /// A replay, a headless run, a driven one, a recording, a dump or a
  /// verification: nobody is at the keyboard, and the sidecars stay off.
  nobody_is_asked,
  /// No game directory and no program yet, so there is nothing for a
  /// sidecar to go beside. A first run is asked what it needs instead
  /// (#382).
  nothing_to_write_beside,
};

/// The printable name of one, for a test and for a log line. Never null.
[[nodiscard]] const char* sidecar_ask_name(sidecar_ask why) noexcept;

/// What this host knows about the launch, as the question needs it.
struct sidecar_run {
  /// `--save-sidecars` or `--no-save-sidecars` was given.
  bool named_on_the_command_line{false};
  /// The config file carries a `save-sidecars` line.
  bool config_answered{false};
  /// There is a config file this build could read (or none yet) *and* a
  /// path to write one to. False for `--no-config`, for a platform with
  /// no per-user directory, and for a file that was refused.
  bool can_remember{false};
  /// A replay, headless, `--press`, `--pull`, `--record`, `--dump` or
  /// `--verify` — see the header comment for why each one of those is a
  /// run this host must not stop to ask a question in.
  bool driven{false};
  /// A directory and a program to run in it.
  bool have_game{false};
};

/// Whether this launch is the one that asks (#385).
///
/// The order matters and is the precedence the rest of this host already
/// keeps: a flag first, then what was remembered, then whether there is
/// anybody to ask at all.
[[nodiscard]] sidecar_ask should_ask_about_sidecars(
    const sidecar_run& run) noexcept;

/// The question, as the lines a host prints, without its `amberfolio: `
/// prefixes and without the trailing prompt.
///
/// Here rather than in `main.cpp` so that a test can hold down the two
/// things this text has to say, both of which are facts a player acts on:
/// the **names of the files** that will appear in their directory, and
/// that the answer is **remembered**. The third fact — that nothing is
/// written until there is something to put in it — is true because
/// `slot_store` makes it true, and saying it here is what keeps the two
/// in step.
[[nodiscard]] std::span<const std::string_view> sidecar_question() noexcept;

/// The prompt line, the last thing printed before a host waits.
[[nodiscard]] std::string_view sidecar_prompt() noexcept;

/// What somebody typed: yes, no, or neither.
///
/// `y`, `yes`, `n`, `no`, in any case, with any leading or trailing
/// spaces, tabs or a carriage return. **Everything else is neither** —
/// an empty line included, and the end of a closed stdin included. A
/// prompt that read silence as consent would be writing into a player's
/// directory on the strength of a keypress that never happened, and one
/// that read it as a refusal would write down an answer nobody gave.
[[nodiscard]] std::optional<bool> read_sidecar_answer(
    std::string_view line) noexcept;

}  // namespace amberfolio::sdl
