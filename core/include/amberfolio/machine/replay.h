// SPDX-License-Identifier: AGPL-3.0-only
//
// The replay harness: a recording's format, the player that verifies one
// against a running machine, and the helpers a host records with
// (M4-R1, #100; closes #78).
//
// PLAN.md §4: "everything nondeterministic the machine consumes arrives
// through the platform interface and is therefore recordable and
// injectable; a replay that also captures the initial conditions
// reproduces a run exactly." PLAN.md §6: "a replay harness … frame/state
// hashes as goldens. Hashes are committable; screen content never is."
// This file is the harness. What it records is *keys, ticks and hashes*
// — nothing in a recording reproduces a byte of a program or a pixel of
// a screen, which is what lets the session library (#101) be committed.
//
//
// The recording
// -------------
//
// Plain text, one line per fact, fields separated by single spaces,
// numbers in decimal and digests in lowercase hex. Lines that begin with
// `#` and empty lines are ignored. The first line names the format:
//
//     amberfolio-recording 3 state=1
//
// Then the **initial conditions**, in this order:
//
//     program NAME SHA256          the program loaded, and its fingerprint
//     tail HEX                     the command tail's bytes (may be empty)
//     speed SUBTICKS               the step cost, in 1/256ths of a tick
//     seam ID                      each seam that was on, registry order
//     dir PATH                     each directory on the disk
//     file PATH SIZE SHA256        each file on the disk, with its digest
//
// and then the **stream**, in tick order:
//
//     wall TICK YYYY-MM-DD HH:MM:SS.CC   a wall-clock seed, at that tick
//     key TICK SCANCODE down|up          a host key event, at that tick
//     pull TICK ID                       a seam's trigger, pulled then
//     checkpoint TICK STEPS WHOLE [stopped] [SECTION=HEX16 ...]
//     end TICK STEPS
//
// A `pull` is a **stream** event and not an initial condition, unlike
// the `seam` lines above it, and the distinction is the whole of #161: a
// seam being *on* is a fact about how the run was set up, and a person
// pulling its trigger is something they did at a moment, exactly as a
// keystroke is. A recording that carried the seam and not the pull would
// replay a run in which the cheat never fired.
//
// A checkpoint carries the whole-state hash (state.h) and, optionally,
// the first eight bytes of each section's — enough to say *which* section
// first disagreed. `stopped` marks one taken of a machine that had
// stopped, which is a fact about the tick and not only about the state;
// the paragraph on `next_tick()` below says why it has to be on the line.
// `end` is where the recording stopped; a player that reaches it verified
// everything before it.
//
// The tail is hex because it can begin with a space, and a format whose
// fields are space-separated cannot carry a leading space as a field.
//
//
// The manifest (#155)
// -------------------
//
// The `dir` and `file` lines are the statement of **what disk the run
// started from**, and they name the whole of it — every directory and
// every file, at every depth, not only the root. A `PATH` is
// `\`-joined and relative to the root, with no leading `\`
// (`SAVE\CHARLIST.TXT`), which is the spelling `tests/sessions/*.session`
// already uses for the same facts.
//
// It recurses because a recording is not only about the program: since
// #105 a session loads a *saved game*, and every byte of `\SAVE\` decides
// what that run does. A manifest that stopped at the root let a replay
// begin from a different saved party and say nothing, diverging thousands
// of frames later at a checkpoint hash — a finding about the machine that
// was really a finding about a directory. Now the disk is refused up
// front, by name.
//
// **The order is depth-first, each directory's entries in the VFS's
// pinned name order (`dos_name_less`), a directory's own line before its
// contents.** That is a total order decided by nothing but what exists,
// which is what makes two recordings of the same disk the same bytes; it
// is also exactly lexicographic order over the component sequences, a
// path sorting before every path it is a prefix of.
//
// A directory keeps a line of its own — `dir PATH`, with no size and no
// digest, because a directory has neither (its size is a fiction and a
// digest of one is not a thing). It is not redundant with its contents:
// an *empty* directory is a fact about a disk that nothing else records,
// and a manifest that skipped one would call two different disks the
// same. One line per filesystem entry is also what makes the manifest's
// capacity a count of entries (`replay_max_manifest_entries`) rather
// than of files-plus-however-many-directories-there-happen-to-be.
//
// Depth is bounded by `dos_path::max_depth`, not by "one level". A disk
// with a directory deeper than a path this machine can name is refused,
// loudly, rather than walked as far as it goes — a truncated walk would
// pin a disk that is not the disk.
//
//
// Why a player in core, and what it does not do
// ---------------------------------------------
//
// Two hosts and a test harness all have to verify the *same* recording
// the same way, or "verified on all four targets" means four readings of
// it. So the parsing, the initial-condition check, the event delivery
// and the checkpoint comparison are here, once, and each host is the
// loop around them: run the machine to `next_tick()` or to its own frame
// boundary, whichever is first, then `apply()`. The player never runs
// the machine itself — that is the host's loop, and a player that ran it
// would be a third host.
//
// `next_tick()` is when the machine must next be *interrupted*, and that
// is not quite the same as when the next line of the recording is.
//
// A machine stops *inside* a step (`machine::run()`): the step that
// refuses or exits is not counted and its ticks are not spent. So a
// stopped machine and the machine one step short of stopping stand at
// the same tick and the same step count, differing only in having
// stopped — and the only way to get from the second to the first is to
// ask the machine to run *past* that tick. A recording's last checkpoint
// is almost always the stopped one. A host held at its tick would run to
// it, find nothing left to do, and compare a machine that was never
// given the chance to stop.
//
// So a checkpoint marked `stopped` does not hold the host back: it runs
// to its own frame boundary, the machine stops where it stopped before,
// and the tick agrees because stopping costs nothing. Every other line
// does hold it back — a key or a seed consumed a tick late is a
// different run, and `end` is where the recording stops being one. A
// host that runs past a checkpoint anyway, because its frame period is
// not the recorder's, is told that it did.
//
// It also never allocates (PLAN.md §4): it reads the text the host holds,
// through a cursor, and keeps the initial conditions in fixed storage
// sized for the largest disk a recording may describe
// (`replay_max_manifest_entries` files and directories, each a bounded
// `dos_path`). That makes a player some tens of kilobytes; the ABI keeps
// its one in static storage for the same reason `af_machine` is there
// (abi.cpp). A host with the text in memory hands it over; reading the
// file is the host's.
//
// Recording is the host's too, with `format_replay_line()` and
// `write_preamble()` as the one spelling of each line. The desktop host
// records (`--record`); the harness in tests/programs records the
// synthetic sessions; every host verifies.
//
//
// What "verified" means
// ---------------------
//
// Every initial condition matched, every key and seed was delivered at
// the tick it was recorded at, every checkpoint's hash was the machine's
// at that tick, and the recording's `end` was reached. Anything else is
// reported as the first thing that differed: which line, which tick,
// which field or section, expected against actual.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

class machine;
class filesystem;

/// The recording format a recorder **writes** — the first line's number.
/// Bump when the line grammar changes.
///
/// 2 (#155): the manifest recurses. `file` takes a `\`-joined path rather
/// than a bare name, and a directory is a `dir PATH` line rather than a
/// `file` line with a zero size and a zero digest.
///
/// 3 (#161): the stream may carry `pull TICK ID` — a seam's trigger,
/// pulled by a person at that tick. A version-2 recording never carries
/// one, so a version-2 player reading one of these would refuse the line
/// rather than misread it; the bump is so that the first line says what
/// the file may contain, which is what a version is for.
inline constexpr std::uint32_t recording_format_version = 3;

/// The oldest format a player still **reads**, and the rule: a version is
/// readable for as long as a recording of it may still exist.
///
/// The seven recordings in `tests/sessions/` are version 1, and six of
/// them are of a game whose disk is nobody's to re-record. So version 1
/// is not retired — it is read exactly as it was written, with the
/// manifest naming the root and nothing below it, and a version-1
/// recording keeps saying precisely what it always said about a disk. A
/// player that dropped it would not be reading an older format wrongly;
/// it would be refusing evidence that cannot be remade.
///
/// Bumping this — retiring a version — invalidates every golden recorded
/// under it and is the same deliberate act `state_format_version` is
/// (docs/replay.md §7). Bumping `recording_format_version` alone is not:
/// old recordings go on verifying, new ones say more.
inline constexpr std::uint32_t recording_format_oldest_read = 1;

/// The first format whose manifest recurses. Below it the manifest is the
/// root directory only.
inline constexpr std::uint32_t recording_format_recursive_manifest = 2;

/// The first format whose stream may carry a `pull` line (#161). A
/// recording that names an older version and carries one is refused: it
/// is not a recording anything wrote.
inline constexpr std::uint32_t recording_format_pull = 3;

/// What one line of a recording says.
enum class replay_line : std::uint8_t {
  /// `amberfolio-recording` — the first line.
  header,
  program,
  tail,
  speed,
  seam,
  file,
  /// `dir PATH` — a directory on the disk (format 2 and up).
  dir,
  wall,
  key,
  /// `pull TICK ID` — a seam's trigger, pulled at that tick (format 3
  /// and up).
  pull,
  checkpoint,
  end,
  /// A comment or a blank line: nothing.
  nothing,
};

/// The longest seam id a line carries. Ids are short (`cheat-kill-all`);
/// thirty-one characters is generous.
inline constexpr std::size_t replay_max_id = 31;

/// The longest line a recording has: a checkpoint with every section
/// named, well under this.
inline constexpr std::size_t replay_max_line = 768;

/// Manifest lines a recording may carry — one per filesystem entry,
/// files and directories together. A disk with more than this cannot be
/// described, and `write_preamble()` refuses rather than describing part
/// of one.
///
/// Set against the disk, not against a backend. A recording is made on
/// the desktop host over a directory, and the largest disk this
/// repository's session library pins is 193 entries — a Gold Box
/// installation with its save slots filled, which is exactly the shape
/// #155 exists for. A cap set *at* the real high-water mark is a cap
/// that refuses the next disk; this one is set well clear of it.
///
/// `memory_filesystem::max_entries` is now the same number, and equal on
/// purpose (#158). It was 192 when this constant was written, and the
/// gap read as harmless because a recording is made on the host with the
/// directory VFS — but it meant a disk this file could describe was a
/// disk the browser could not load, which is the one direction a
/// recording exists to travel. Whichever of the two moves next, the
/// other follows.
inline constexpr std::size_t replay_max_manifest_entries = 512;

/// The longest manifest line: `file `, a full-depth path (`max_depth`
/// components of `dos_name::max_length` with a separator each), a size, a
/// digest, the newline and the NUL.
inline constexpr std::size_t replay_max_manifest_line =
    5 + (dos_path::max_depth * (dos_name::max_length + 1)) + 1 + 10 + 1 +
    sha256_digest::text_length + 2;

/// A buffer big enough for any preamble `write_preamble()` writes: the
/// header and the fixed lines, plus a manifest line per entry. A host
/// sizes its buffer with this rather than guessing at one — the manifest
/// grew with #155 and a guess made before it would have been too small.
inline constexpr std::size_t replay_preamble_capacity =
    1024 + (replay_max_manifest_entries * replay_max_manifest_line);

/// Big enough for any report `replay_player::report()` writes — the
/// sentence, the message, and two digests spelled out — so that a host
/// never has to think about a report that was cut in half (report.h has
/// the same two constants for the same reason).
inline constexpr std::size_t replay_report_capacity = 512;

/// One parsed line. A plain aggregate with every kind's fields in it,
/// because the alternative is a variant core cannot spell without
/// <variant>; a reader looks at `kind` and at the fields that kind uses.
struct replay_event {
  replay_line kind{replay_line::nothing};

  /// `header`: the format and the state-layout versions.
  std::uint32_t format_version{};
  std::uint32_t state_version{};

  /// `program`: the name it was loaded under, and its digest.
  dos_name name{};
  /// `program`, `file`: the digest. `file`: the size too.
  std::uint32_t size{};
  sha256_digest digest{};

  /// `file`, `dir`: where on the disk, relative to the root. A `dos_path`
  /// and not a `dos_name` since #155, because the manifest recurses; the
  /// type is bounded at `dos_path::max_depth` components, so an event is
  /// still fixed-capacity and a line still cannot describe a path this
  /// machine could not name.
  dos_path path{};

  /// `tail`: the bytes, `tail_length` of them.
  std::array<char, 126> tail{};
  std::size_t tail_length{};

  /// `speed`: the step cost, in subticks.
  std::uint32_t subticks{};

  /// `seam`, `pull`: the id, `id_length` of it.
  std::array<char, replay_max_id + 1> id{};
  std::size_t id_length{};

  /// `wall`, `key`, `pull`, `checkpoint`, `end`: the tick.
  ticks at{};
  /// `checkpoint`, `end`: the step count.
  std::uint64_t steps{};

  /// `wall`: the seed.
  wall_time when{};

  /// `key`: the event.
  std::uint8_t scancode{};
  key_action action{key_action::down};

  /// `checkpoint`: whether the machine had stopped at this tick. On the
  /// line rather than left to the state hash because a player has to know
  /// it *before* it compares — see this file's header.
  bool stopped{false};

  /// `checkpoint`: the whole-state hash, and — when the line carries them
  /// — the first eight bytes of each section's, as integers.
  bool have_sections{false};
  std::array<std::uint64_t, state_section_count> sections{};

  [[nodiscard]] std::string_view id_text() const noexcept {
    return {id.data(), id_length};
  }
  [[nodiscard]] std::span<const char> tail_text() const noexcept {
    return {tail.data(), tail_length};
  }
};

/// Parse one line. False, and `out` unspecified, for anything that is
/// not a well-formed line of a known kind; a comment or a blank line is
/// well-formed and answers `replay_line::nothing`.
[[nodiscard]] bool parse_replay_line(std::span<const char> line,
                                     replay_event& out) noexcept;

/// Write one line for `event`, newline included, NUL-terminated, into
/// `out`; answers the length, or zero if it did not fit.
std::size_t format_replay_line(const replay_event& event,
                               std::span<char> out) noexcept;

/// The first eight bytes of a digest, as the integer a checkpoint's
/// section field carries.
[[nodiscard]] std::uint64_t section_prefix(
    const sha256_digest& digest) noexcept;

/// A checkpoint event for `box` right now: the tick, the step count, the
/// state hashes.
[[nodiscard]] replay_event checkpoint_of(const machine& box);

/// Write the preamble — the header line and every initial-condition line
/// — for `box` as it stands, into `out`. `program` is the name the
/// program was loaded under and `tail` its command tail; the fingerprint,
/// the speed, the seams and the file manifest are read off the machine
/// and `fs`. Answers the length, or zero if it did not fit or the
/// manifest could not be taken — a disk with more entries than
/// `replay_max_manifest_entries`, or a directory deeper than
/// `dos_path::max_depth`, is refused rather than half-described.
///
/// `out` wants `replay_preamble_capacity` bytes; anything less is a
/// guess, and the manifest is as long as the disk is deep.
std::size_t write_preamble(const machine& box, filesystem& fs,
                           std::string_view program, std::span<const char> tail,
                           std::span<char> out);

/// The initial conditions a recording names, as `replay_player::load()`
/// leaves them.
struct replay_preamble {
  std::uint32_t format_version{};
  std::uint32_t state_version{};
  dos_name program{};
  sha256_digest program_digest{};
  std::array<char, 126> tail{};
  std::size_t tail_length{};
  std::uint32_t subticks{};
  bool have_speed{false};

  static constexpr std::size_t max_seams = 16;
  std::array<std::array<char, replay_max_id + 1>, max_seams> seams{};
  std::array<std::size_t, max_seams> seam_lengths{};
  std::size_t seam_count{};

  /// One manifest line: a `dir` or a `file`, in the order the walk
  /// produced it (this file's header says what that order is).
  struct file_entry {
    dos_path path{};
    std::uint32_t size{};
    bool is_directory{false};
    sha256_digest digest{};
  };
  static constexpr std::size_t max_files = replay_max_manifest_entries;
  std::array<file_entry, max_files> files{};
  std::size_t file_count{};

  [[nodiscard]] std::string_view seam(std::size_t i) const noexcept {
    return {seams[i].data(), seam_lengths[i]};
  }
  [[nodiscard]] std::span<const char> tail_text() const noexcept {
    return {tail.data(), tail_length};
  }
};

/// How the player is doing.
enum class replay_status : std::uint8_t {
  /// Loaded, or an event applied; more to come.
  ok,
  /// The recording's `end` was reached and everything before it held.
  done,
  /// Something differed. `report()` says what.
  diverged,
  /// The text is not a recording this player reads, or the initial
  /// conditions do not match the machine. `report()` says what.
  malformed,
};

/// The player: the recording's cursor, the initial conditions, and the
/// first thing that differed.
class replay_player {
 public:
  /// Parse the preamble of `text` and stand at its first event. The text
  /// is the host's and must outlive the player; nothing is copied but
  /// the preamble. False, with `report()` saying which line, for a text
  /// that is not a recording.
  bool load(std::span<const char> text);

  [[nodiscard]] const replay_preamble& preamble() const noexcept {
    return preamble_;
  }

  /// Compare the initial conditions to `box` as it stands, the program
  /// loaded, the speed set and the seams enabled — and to `fs`'s whole
  /// tree, when `fs` is given. `ok`, or `malformed` with `report()`
  /// naming the first condition that did not hold, and the path it is
  /// about. A host applies the preamble's speed and seams first if it
  /// means the recording to be the run.
  ///
  /// How far the manifest reaches is the recording's own to say: a
  /// version-2 recording names the whole disk, and a version-1 one names
  /// the root, which is all it ever claimed to.
  replay_status check_initial(const machine& box, filesystem* fs);

  /// The tick the machine must not be run past: the next key, wall seed,
  /// `end` or plain checkpoint still to come. `never` when the next thing
  /// is a checkpoint marked `stopped`, which the machine has to be run
  /// past to reach (see this file's header), and once the recording is
  /// exhausted. A host runs the machine to this or to its own frame
  /// boundary, whichever is first, and then calls `apply()`.
  [[nodiscard]] ticks next_tick() const noexcept;

  /// Deliver every event whose tick is at or before `box.time()`: post
  /// the keys, seed the clock, compare the checkpoints. `ok` while more
  /// remain, `done` at `end`, `diverged` the moment anything differed —
  /// including an event whose tick the machine has already passed, which
  /// means the host ran too far.
  replay_status apply(machine& box);

  [[nodiscard]] bool done() const noexcept {
    return status_ == replay_status::done;
  }
  [[nodiscard]] replay_status status() const noexcept { return status_; }

  /// What was verified so far.
  [[nodiscard]] std::size_t checkpoints_verified() const noexcept {
    return checkpoints_;
  }
  [[nodiscard]] std::size_t keys_delivered() const noexcept { return keys_; }
  /// Seam triggers delivered (#161). A pull the engine refused — the
  /// seam is off, or is not one that takes a trigger — is a divergence
  /// and not a delivery: the recording says the run had one.
  [[nodiscard]] std::size_t pulls_delivered() const noexcept { return pulls_; }

  /// The report: one line, `amberfolio: replay ...`, saying what held or
  /// what differed first and where. NUL-terminated into `out`; answers
  /// the length.
  [[nodiscard]] std::size_t report(std::span<char> out) const noexcept;

 private:
  /// Read the next non-empty line into `event`. False at the end of the
  /// text (or on a malformed line, which sets the status).
  bool next_line(replay_event& event);

  /// Peek: the next event, parsed and held until `apply()` consumes it.
  bool peek();

  void fail(replay_status status, std::string_view what, std::uint64_t at);
  void fail_hash(std::string_view section, const sha256_digest& expected,
                 const sha256_digest& actual);
  /// A refusal about one entry of the manifest, which the report names by
  /// path — the whole point of #155 is that "this is not the disk this
  /// was recorded against" says *which file*.
  void fail_path(std::string_view what, const dos_path& where);

  /// The manifest, root only: what a version-1 recording pins, read the
  /// way version 1 wrote it.
  replay_status check_root_manifest(filesystem& fs);
  /// The manifest, the whole tree: version 2 and up.
  replay_status check_tree_manifest(filesystem& fs);

  std::span<const char> text_{};
  std::size_t cursor_{};
  std::size_t line_number_{};

  replay_preamble preamble_{};
  replay_event pending_{};
  bool have_pending_{false};

  replay_status status_{replay_status::ok};
  std::size_t checkpoints_{};
  std::size_t keys_{};
  std::size_t pulls_{};

  /// The report's pieces: a message, the line it is about, the tick, and
  /// for a hash divergence the two digests.
  std::array<char, 96> what_{};
  std::size_t what_length_{};
  std::size_t failed_line_{};
  std::uint64_t failed_at_{};
  bool have_digests_{false};
  sha256_digest expected_{};
  sha256_digest actual_{};
  /// The manifest entry a refusal is about, when it is about one.
  bool have_where_{false};
  dos_path where_{};
};

/// What `verify_recording()` found.
struct verify_result {
  replay_status status{replay_status::malformed};
  std::size_t checkpoints{};
  std::size_t keys{};
  /// Seam triggers delivered (#161).
  std::size_t pulls{};

  [[nodiscard]] bool ok() const noexcept {
    return status == replay_status::done;
  }
};

/// Drive `box` through the whole of `text` and answer what happened.
///
/// This is the loop the player will not do for itself, and it is allowed
/// to be here because it is a *loop* and not a host: nothing is
/// presented, nothing is pulled from the speaker, no input arrives from
/// anywhere but the recording, and no wall clock is consulted. A desktop
/// or browser host still writes its own, because a host has all of those
/// things to do between slices — but a checker has none of them, and
/// three targets writing the same twelve lines is three chances to write
/// them differently.
///
/// `slice` is the caller's frame period, the boundary it would have run
/// to anyway. It must be the one the recording was made at — the
/// recorder's checkpoints are at *its* boundaries, and a checker with a
/// coarser slice runs past them and is told so.
///
/// Everything the recording names as an initial condition is applied
/// first: the speed and the seams. Then `check_initial()`, then the
/// loop. `player` is the caller's so that `report()` can be read after,
/// whichever way it went.
verify_result verify_recording(machine& box, filesystem* fs,
                               std::span<const char> text, ticks slice,
                               replay_player& player);

}  // namespace amberfolio::machine
