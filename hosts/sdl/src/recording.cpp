// SPDX-License-Identifier: AGPL-3.0-only

#include "recording.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/state.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::sdl {

namespace {

/// What the operating system says the date and the time are, in the local
/// calendar the person in front of this host keeps (#320).
///
/// This is a host reading the host's clock, which is the one place it is
/// allowed: `scripts/check-host-time.sh` refuses these calls under
/// `core/` and leaves the hosts — which are supposed to know what time it
/// is — alone. What crosses into the machine is one instant at one tick
/// and never a callout, so nothing downstream of it can observe *when*
/// this was called (`machine/platform.h`).
///
/// Local and not UTC, for the same reason the browser reads local fields:
/// the date a journal row is stamped with is the player's own, and a
/// machine that told somebody in Auckland it was yesterday would be
/// answering a question nobody asked.
///
/// False if the clock cannot be broken down at all, or reads a year
/// outside DOS's own 1980-2099 — the caller says so and leaves the
/// machine unseeded rather than inventing a date that fits.
[[nodiscard]] bool host_wall_time(machine::wall_time& out) {
  // Milliseconds since the Unix epoch, which C++20 pins `system_clock`
  // to, and then floor division — rather than `to_time_t`, whose
  // rounding is the implementation's business and can hand back the
  // second *after* the one the sub-second remainder belongs to.
  const auto since_epoch =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch());
  if (since_epoch.count() < 0) {
    return false;
  }
  const auto seconds = static_cast<std::time_t>(since_epoch.count() / 1000);
  const auto centisecond =
      static_cast<std::uint8_t>((since_epoch.count() % 1000) / 10);

  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &seconds) != 0) {
    return false;
  }
#else
  if (localtime_r(&seconds, &local) == nullptr) {
    return false;
  }
#endif

  out = machine::wall_time{
      .year = static_cast<std::uint16_t>(local.tm_year + 1900),
      .month = static_cast<std::uint8_t>(local.tm_mon + 1),
      .day = static_cast<std::uint8_t>(local.tm_mday),
      .hour = static_cast<std::uint8_t>(local.tm_hour),
      .minute = static_cast<std::uint8_t>(local.tm_min),
      // A leap second reads 60 here and there is no such second in DOS,
      // which counts hundredths off a day of exactly 8,640,000 of them.
      // Clamped rather than refused: one second a decade is not a reason
      // to hand a player a machine with no date.
      .second =
          static_cast<std::uint8_t>(local.tm_sec > 59 ? 59 : local.tm_sec),
      .centisecond = centisecond,
  };
  machine::wall_clock probe;
  return probe.set(out, 0);
}

}  // namespace

bool recorder::open(const options& opts, machine::machine& box,
                    machine::filesystem& files) {
  if (opts.record_path.empty()) {
    return true;
  }
  file_.open(opts.record_path, std::ios::binary | std::ios::trunc);
  if (!file_) {
    std::fprintf(stderr, "amberfolio: cannot write %s\n",
                 opts.record_path.c_str());
    return false;
  }
  // Sized by core rather than guessed at: the manifest names the whole
  // disk since #155, so how long a preamble gets is a fact about
  // `dos_path` and how many entries a recording may name, not about
  // this host. The number this used to carry was already too small.
  std::vector<char> preamble(machine::replay_preamble_capacity, '\0');
  const std::size_t n = machine::write_preamble(
      box, files, opts.program,
      std::span<const char>(opts.command_tail.data(), opts.command_tail.size()),
      preamble);
  if (n == 0) {
    std::fprintf(stderr,
                 "amberfolio: could not record this run's initial"
                 " conditions\n");
    return false;
  }
  file_.write(preamble.data(), static_cast<std::streamsize>(n));
  return true;
}

void recorder::line(const machine::replay_event& event) {
  if (!file_.is_open()) {
    return;
  }
  std::array<char, machine::replay_max_line> text{};
  const std::size_t n = machine::format_replay_line(event, text);
  file_.write(text.data(), static_cast<std::streamsize>(n));
}

void recorder::end_run(const machine::machine& box,
                       machine::ticks checkpointed) {
  if (!file_.is_open()) {
    return;
  }
  // The frame the run ends on is checkpointed whatever the cadence
  // says, and it is the checkpoint that matters most: it is the one a
  // machine that stopped is described by, and `stopped` on its line is
  // what lets a replaying host run *past* the tick to arrive at it
  // (machine/replay.h). A cadence that happened not to divide the last
  // frame number would otherwise drop it.
  if (box.time() != checkpointed) {
    line(machine::checkpoint_of(box));
  }
  machine::replay_event last{};
  last.kind = machine::replay_line::end;
  last.at = box.time();
  last.steps = box.steps();
  line(last);
  file_.flush();
}

bool load_replay(const options& opts, machine::machine& box,
                 machine::filesystem& files, std::string& replay_text,
                 machine::replay_player& player) {
  std::ifstream in(opts.replay_path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "amberfolio: cannot read %s\n",
                 opts.replay_path.c_str());
    return false;
  }
  replay_text.assign(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
  player.set_rehash(!opts.rehash_path.empty());
  if (!player.load(
          std::span<const char>(replay_text.data(), replay_text.size()))) {
    std::array<char, machine::replay_report_capacity> line{};
    static_cast<void>(player.report(line));
    std::fputs(line.data(), stderr);
    return false;
  }
  // The recording's own speed and seams, applied before it is checked
  // against the machine: a replay is the run the recording names.
  box.set_step_cost_subticks(player.preamble().subticks);
  for (std::size_t i = 0; i < player.preamble().seam_count; ++i) {
    if (box.seams().enable(player.preamble().seam(i)) !=
        machine::seam_error::none) {
      std::fprintf(stderr,
                   "amberfolio: the recording's seam %.*s is not"
                   " available for this program\n",
                   static_cast<int>(player.preamble().seam(i).size()),
                   player.preamble().seam(i).data());
      return false;
    }
  }
  if (player.check_initial(box, &files) != machine::replay_status::ok) {
    std::array<char, machine::replay_report_capacity> line{};
    static_cast<void>(player.report(line));
    std::fputs(line.data(), stderr);
    return false;
  }
  return true;
}

void seed_wall_clock(const options& opts, machine::machine& box, bool replaying,
                     recorder& recording) {
  // --- The date, said once, before the first instruction (#320) ----------
  //
  // The machine's clock is a **seed** plus virtual time and never a
  // callout into this host (`machine/platform.h`), so a machine nobody
  // seeds is not one with an approximate date: it is a PC with no clock
  // card, counting hundredths from 1 January 1980. That is what this host
  // handed the game for four milestones — nothing ever called
  // `set_wall_time()` — and where it showed was the journal's own
  // listing, every entry stamped `01-01 00:04`, which is not a date but
  // four minutes of uptime.
  //
  // **Here, and in the same breath as the line that records it.** The
  // seed is machine state and so is in every checkpoint hash, which makes
  // a run that seeds and does not write the `wall` line a run its own
  // recording cannot reproduce — it would diverge at the first
  // checkpoint, against a replaying machine still counting from 1980.
  // One block, so the two cannot drift apart.
  //
  // Not while replaying: the recording seeds the machine, at the tick it
  // was seeded at, through the same `set_wall_time()` (`machine/
  // replay.h`), and `--wall` is refused alongside `--replay` up in the
  // option checks. Every recording committed before this change carries
  // no `wall` line at all, and that is the whole of why they still
  // verify — an unseeded run replays as the unseeded run it was.
  if (!replaying && opts.wall != wall_source::unseeded) {
    machine::wall_time when{};
    bool known = true;
    if (opts.wall == wall_source::stated) {
      when = opts.wall_stated;
    } else {
      known = host_wall_time(when);
    }
    if (known && box.set_wall_time(when)) {
      // Read back rather than echoed: `wall_clock::set()` derives the
      // weekday and this is the machine saying what it now believes,
      // which is the thing a state hash is a hash of.
      const machine::wall_time now = box.wall().at(box.time());
      std::fprintf(
          stderr,
          "amberfolio: wall clock %04u-%02u-%02u %02u:%02u:%02u"
          " (%s)\n",
          static_cast<unsigned>(now.year), static_cast<unsigned>(now.month),
          static_cast<unsigned>(now.day), static_cast<unsigned>(now.hour),
          static_cast<unsigned>(now.minute), static_cast<unsigned>(now.second),
          opts.wall == wall_source::stated ? "stated" : "this host");
      machine::replay_event line{};
      line.kind = machine::replay_line::wall;
      line.at = box.time();
      line.when = now;
      recording.line(line);
    } else {
      // Log, don't fake. A clock this host cannot break down, or a year
      // DOS has no room for, leaves the machine the one it has always
      // been rather than getting a date somebody here made up for it.
      std::fprintf(stderr,
                   "amberfolio: wall clock not set - this host's clock is"
                   " not a date DOS can hold; the machine counts from"
                   " 1980-01-01\n");
    }
  }
}

}  // namespace amberfolio::sdl
