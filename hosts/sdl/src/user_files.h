// SPDX-License-Identifier: AGPL-3.0-only
//
// The files this host keeps for the player, and where they are (#472, split
// from main.cpp): the per-user data directory, the three paths in it, and
// reading and writing the two stores that live there.
//
// All three are facts about **this player** rather than about their game
// directory -- the config (launch_config.h), the journal's text and the
// copies whose code-wheel challenge has been answered -- so they are found
// together.

#pragma once

#include <string>
#include <string_view>

#include "amberfolio/host/code_wheel_store.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/machine.h"
#include "options.h"

namespace amberfolio::sdl {

/// `filename` in the per-user data directory this platform keeps
/// application data in. Empty if this platform will not say where that
/// is, in which case each caller asks for its own flag rather than
/// picking somewhere.
///
/// Three files live here now and they are all facts about **this
/// player** rather than about their game directory: the journal's text
/// (#174), the copies whose code-wheel challenge has been answered
/// (#292), and the config (#382). One helper, so a player who goes
/// looking finds them together.
[[nodiscard]] std::string per_user_path(std::string_view filename);

/// Where the journal's text is, for this run: `--journal-store` if it was
/// given, and this platform's per-user data directory otherwise.
[[nodiscard]] std::string journal_store_path(const options& opts);

/// Where the copies that have answered the code-wheel challenge are
/// remembered, for this run (M6-C1b, #292): `--code-wheel-store` if it
/// was given, and this platform's per-user data directory otherwise.
[[nodiscard]] std::string code_wheel_store_path(const options& opts);

/// Where this player's settings are, for this run (#382): `--config` if
/// it was given, and this platform's per-user data directory otherwise.
[[nodiscard]] std::string config_file_path(const options& opts);

/// The whole file, or nothing when there is no file. `found` tells the
/// two apart: a config that is not there is the ordinary case for
/// somebody who has never saved one, and says nothing, where a config
/// that is there and unreadable is a sentence.
[[nodiscard]] std::string slurp_file(const std::string& path, bool& found);

/// Read it, and say what it turned out to be.
///
/// Every outcome is a sentence and none of them stops the run: a player
/// whose store could not be read still asked to play, and what it costs
/// them is being asked the game's own question again. A file that is not
/// there at all is the ordinary case for somebody who has never answered
/// it, and says nothing — there is nothing to report about a question
/// nobody has been asked yet.
void load_code_wheel_store(const options& opts, host::code_wheel_store& store);

/// Write it back. Called when the store says it moved — which happens at
/// most once a run, at the instant somebody answers the question.
void save_code_wheel_store(const options& opts,
                           const host::code_wheel_store& store);

/// Read a journal store that is already there, for a run that is not
/// ingesting one (M5-E4, #175).
///
/// An ingestion happens once; the reading happens for ever after. So a run
/// with `--seam journal` and no `--journal` still has to find the text a
/// previous run wrote, and this is where it does.
///
/// Every outcome is a sentence and none of them stops the run, because a
/// player whose journal could not be read still asked to play. A file that
/// is not there at all is the ordinary case for somebody who has never
/// ingested one, and it says so once rather than leaving the reader to
/// announce it from inside the game later.
void load_journal_store(const options& opts, host::journal_store& store);

/// Write the store back, for the log's sake (M5-E4b, #222).
///
/// Ingestion writes this file too, and writes it once at the end of a job
/// that has just done the expensive part. This is the other writer: the
/// log changes while a game is being played, so it is written when the
/// machine says it moved and not on a timer.
void save_journal_store(const options& opts, const host::journal_store& store);

/// The code wheel's challenge, and the one thing a person shows this machine
/// that is not a file: that they have already answered it (#291). Called
/// before the seams are enabled, so a run that knows it, and turns the seam
/// on, never reaches the challenge at all.
void settle_code_wheel(const options& opts, machine::machine& box,
                       host::code_wheel_store& code_wheel);
}  // namespace amberfolio::sdl
