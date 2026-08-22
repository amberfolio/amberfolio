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
//     amberfolio-recording 1 state=1
//
// Then the **initial conditions**, in this order:
//
//     program NAME SHA256          the program loaded, and its fingerprint
//     tail HEX                     the command tail's bytes (may be empty)
//     speed SUBTICKS               the step cost, in 1/256ths of a tick
//     seam ID                      each seam that was on, registry order
//     file NAME SIZE SHA256        each file in the root, pinned order
//
// and then the **stream**, in tick order:
//
//     wall TICK YYYY-MM-DD HH:MM:SS.CC   a wall-clock seed, at that tick
//     key TICK SCANCODE down|up          a host key event, at that tick
//     checkpoint TICK STEPS WHOLE [SECTION=HEX16 ...]
//     end TICK STEPS
//
// A checkpoint carries the whole-state hash (state.h) and, optionally,
// the first eight bytes of each section's — enough to say *which* section
// first disagreed. `end` is where the recording stopped; a player that
// reaches it verified everything before it.
//
// The tail is hex because it can begin with a space, and a format whose
// fields are space-separated cannot carry a leading space as a field.
//
//
// Why a player in core, and what it does not do
// ---------------------------------------------
//
// Two hosts and a test harness all have to verify the *same* recording
// the same way, or "verified on all four targets" means four readings of
// it. So the parsing, the initial-condition check, the event delivery
// and the checkpoint comparison are here, once, and each host is the
// loop around them: run the machine to the next event's tick (or its own
// frame boundary, whichever is first), then `apply()`. The player never
// runs the machine itself — that is the host's loop, and a player that
// ran it would be a third host.
//
// It also never allocates (PLAN.md §4): it reads the text the host holds,
// through a cursor, and keeps the initial conditions in fixed storage
// sized for the filesystem this machine has (`memory_filesystem::
// max_entries` files). A host with the text in memory hands it over;
// reading the file is the host's.
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
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

class machine;
class filesystem;

/// The recording format's version — the first line's number. Bump when
/// the line grammar changes; a player refuses another version.
inline constexpr std::uint32_t recording_format_version = 1;

/// What one line of a recording says.
enum class replay_line : std::uint8_t {
  /// `amberfolio-recording` — the first line.
  header,
  program,
  tail,
  speed,
  seam,
  file,
  wall,
  key,
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

/// One parsed line. A plain aggregate with every kind's fields in it,
/// because the alternative is a variant core cannot spell without
/// <variant>; a reader looks at `kind` and at the fields that kind uses.
struct replay_event {
  replay_line kind{replay_line::nothing};

  /// `header`: the format and the state-layout versions.
  std::uint32_t format_version{};
  std::uint32_t state_version{};

  /// `program`, `file`: the name, and the digest. `file`: the size too.
  dos_name name{};
  std::uint32_t size{};
  sha256_digest digest{};

  /// `tail`: the bytes, `tail_length` of them.
  std::array<char, 126> tail{};
  std::size_t tail_length{};

  /// `speed`: the step cost, in subticks.
  std::uint32_t subticks{};

  /// `seam`: the id, `id_length` of it.
  std::array<char, replay_max_id + 1> id{};
  std::size_t id_length{};

  /// `wall`, `key`, `checkpoint`, `end`: the tick.
  ticks at{};
  /// `checkpoint`, `end`: the step count.
  std::uint64_t steps{};

  /// `wall`: the seed.
  wall_time when{};

  /// `key`: the event.
  std::uint8_t scancode{};
  key_action action{key_action::down};

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
/// manifest could not be taken.
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

  struct file_entry {
    dos_name name{};
    std::uint32_t size{};
    sha256_digest digest{};
  };
  static constexpr std::size_t max_files = memory_filesystem::max_entries;
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
  /// loaded, the speed set and the seams enabled — and to `fs`'s root,
  /// when `fs` is given. `ok`, or `malformed` with `report()` naming the
  /// first condition that did not hold. A host applies the preamble's
  /// speed and seams first if it means the recording to be the run.
  replay_status check_initial(const machine& box, filesystem* fs);

  /// The tick of the next event not yet applied, or `never` once the
  /// recording is exhausted. A host runs the machine to this (or to its
  /// own boundary, whichever is first) and then calls `apply()`.
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

  /// The report: one line, `amberfolio: replay ...`, saying what held or
  /// what differed first and where. NUL-terminated into `out`; answers
  /// the length.
  std::size_t report(std::span<char> out) const noexcept;

 private:
  /// Read the next non-empty line into `event`. False at the end of the
  /// text (or on a malformed line, which sets the status).
  bool next_line(replay_event& event);

  /// Peek: the next event, parsed and held until `apply()` consumes it.
  bool peek();

  void fail(replay_status status, std::string_view what, std::uint64_t at);
  void fail_hash(std::string_view section, const sha256_digest& expected,
                 const sha256_digest& actual);

  std::span<const char> text_{};
  std::size_t cursor_{};
  std::size_t line_number_{};

  replay_preamble preamble_{};
  replay_event pending_{};
  bool have_pending_{false};

  replay_status status_{replay_status::ok};
  std::size_t checkpoints_{};
  std::size_t keys_{};

  /// The report's pieces: a message, the line it is about, the tick, and
  /// for a hash divergence the two digests.
  std::array<char, 96> what_{};
  std::size_t what_length_{};
  std::size_t failed_line_{};
  std::uint64_t failed_at_{};
  bool have_digests_{false};
  sha256_digest expected_{};
  sha256_digest actual_{};
};

}  // namespace amberfolio::machine
