// SPDX-License-Identifier: AGPL-3.0-only
//
// What this host says about the disk it was pointed at (#472, split from
// main.cpp): the edition it turned out to be, which of its files are the
// player's (#208), and the VFS door's three operations (M5-D2, #170).
// Every line is a sentence on stderr that the sweep, the session library
// and docs/hosts.md read.

#pragma once

#include <string>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/vfs.h"
#include "options.h"

namespace amberfolio::sdl {

/// `path`, spelled the way core spells it — so a listing line and a
/// `--trace` line about the same file are the same characters
/// (machine/report.h).
[[nodiscard]] std::string spell_vfs_path(const machine::dos_path& path);

/// Where the copy saves and which sound it is set up for, said once the
/// directory the program starts in is current. The save directory is quiet at
/// the root, where every test program lives with no configuration file; a
/// copy laid out on purpose gets a line.
void report_copy_facts(machine::filesystem& files,
                       const machine::dos_path& install);

/// `--save-layer`'s first half: the table itself, printed with the
/// edition line (#208).
///
/// Before the run and not after it, because it is a fact about the
/// program rather than about the disk — the same reason the edition line
/// is where it is. A pattern is spelled with its placeholders in it:
/// `<S>` a slot letter, `<N>` a party-member index, `<NAME>` a name the
/// player chose (`machine/save_layer.h`).
void report_save_layer_table(const machine::machine& box,
                             machine::filesystem& files, const options& opts);

/// `--save-layer`'s second half: which of the disk's files are the
/// player's, once the run has left what it left (#208).
///
/// The table itself is printed with the edition line, before the run,
/// because it is a fact about the program rather than about the disk.
/// This is the disk read against it — the same question a browser asks
/// before it writes `\SAVE\` into its own storage, answered here so the
/// two hosts can be compared rather than described.
///
/// Files the layer does not claim are simply absent: this is a listing
/// of the playthrough's, and `--vfs-list` is the listing of everything.
void report_save_layer_files(const machine::machine& box,
                             machine::filesystem& files, const options& opts);

/// `--vfs-list`, `--vfs-get` and `--vfs-remove`, in that order, after the
/// run. See options.h's notes for what each one is for and why
/// `--vfs-get` prints a digest rather than bytes.
void report_vfs(machine::filesystem& files, const options& opts);

/// What the player's directory turned out to be, when the program in it
/// is not one this build recognises (#207).
///
/// The identity line says `unrecognized` and stops, which is true and is
/// nothing a player can act on. This is the rest of that answer, off the
/// one table both hosts read (`host/edition_facts.h`, `data/editions.json`):
/// the closest edition, how much of it is here, which required artifacts
/// are not — and every file that was looked at with its hash, because an
/// edition nobody has fingerprinted yet is a first-class answer
/// (`machine/edition.h`) and that list is exactly what a report asking
/// for one has to carry.
void report_unrecognized_edition(machine::filesystem& files);

}  // namespace amberfolio::sdl
