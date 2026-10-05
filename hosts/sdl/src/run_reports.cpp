// SPDX-License-Identifier: AGPL-3.0-only

#include "run_reports.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/trace.h"
#include "amberfolio/sha256.h"
#include "dump.h"
#include "screen_text_dump.h"
#include "sound_report.h"

namespace amberfolio::sdl {

void write_still(const std::string& prefix, std::uint64_t frame,
                 const machine::machine& box) {
  std::array<char, 32> suffix{};
  std::snprintf(suffix.data(), suffix.size(), "-%06llu.ppm",
                static_cast<unsigned long long>(frame));
  (void)sdl::write_ppm(std::filesystem::path(prefix + suffix.data()),
                       box.display().pixels(), box.display().palette());
  std::memcpy(suffix.data() + 7, ".txt", 4);
  sdl::write_screen_text(prefix + suffix.data(), box);
}

void drain_console(machine::machine& box) {
  std::array<std::uint8_t, 256> buffer{};
  for (;;) {
    const std::size_t got = box.console().read(buffer);
    if (got == 0) {
      return;
    }
    std::fwrite(buffer.data(), 1, got, stdout);
    // Flushed here rather than left to exit. A terminal would line-buffer
    // this and a pipe will not, so without it a program that echoes what
    // you type shows nothing at all until it ends - which is exactly the
    // shape of the check docs/hosts.md asks a person to make, and it
    // would look like the keyboard was dead.
    std::fflush(stdout);
  }
}

void print_overlay_loads(const machine::machine& box, std::uint64_t& printed) {
  const machine::overlay_tracker& overlays = box.overlays();
  std::uint64_t newest = printed;
  for (std::size_t i = 0; i < overlays.count(); ++i) {
    const machine::overlay_load& load = overlays.at(i);
    if (load.generation <= printed) {
      continue;
    }
    std::array<char, sha256_digest::text_length + 1> hex{};
    static_cast<void>(format_hex(load.digest, hex));
    const std::span<const char> name = load.file.leaf().text();
    std::fprintf(stderr,
                 "amberfolio: overlay %.*s offset=%u length=%u at=%04X:%04X"
                 " sha256=%s\n",
                 static_cast<int>(name.size()), name.data(), load.file_offset,
                 load.length, load.segment, load.offset, hex.data());
    if (load.generation > newest) {
      newest = load.generation;
    }
  }
  printed = newest;
}

void print_door_rule(const machine::machine& box, door_rule_log& log,
                     std::uint64_t frame_index) {
  const machine::automap_state& map = box.automap();
  if (!map.appearance_learned()) {
    return;
  }
  const door_rule_log now{.said = true,
                          .disk = map.settled_disk(),
                          .area = map.settled_area(),
                          .geo = map.settled_geo(),
                          .seen = map.door_nibbles_seen(),
                          .from_table = map.door_nibbles_table(),
                          .drawn = map.doors_drawn()};
  if (log == now) {
    return;
  }
  log = now;
  std::fprintf(stderr,
               "amberfolio: automap doors frame=%06llu disk=%u area=%02X"
               " geo=%02X seen=%04X table=%04X drawn shut=%u kind-seen=%u"
               " kind-table=%u no-evidence=%u\n",
               static_cast<unsigned long long>(frame_index), now.disk, now.area,
               now.geo, now.seen, now.from_table, now.drawn.shut,
               now.drawn.seen_kind, now.drawn.table_kind,
               now.drawn.no_evidence);
}

void print_watch(machine::machine& box, const std::vector<watch_point>& watches,
                 watch_log& log, std::uint64_t frame_index) {
  const std::span<const std::uint8_t> ram = box.memory().ram();
  const std::uint16_t ds = box.processor().regs()[cpu::sreg::ds];
  const auto base = static_cast<std::uint32_t>(ds) * 16U;

  std::vector<std::uint16_t> values;
  values.reserve(watches.size());
  for (const watch_point& point : watches) {
    std::uint16_t value = 0;
    for (unsigned byte = 0; byte < point.width; ++byte) {
      const std::uint32_t at = base + point.offset + byte;
      const std::uint8_t got = at < ram.size() ? ram[at] : 0U;
      value = static_cast<std::uint16_t>(
          value | (static_cast<unsigned>(got) << (8U * byte)));
    }
    values.push_back(value);
  }

  const auto found =
      std::ranges::find(log.seen, ds, &watch_log::entry::segment);
  if (found != log.seen.end()) {
    if (found->values == values) {
      return;
    }
    found->values = values;
  } else {
    if (log.seen.size() >= watch_log::max_segments) {
      log.seen.erase(log.seen.begin());
    }
    log.seen.push_back({.segment = ds, .values = values});
  }

  std::printf("amberfolio: watch frame=%06llu ds=%04X",
              static_cast<unsigned long long>(frame_index), ds);
  for (std::size_t i = 0; i < watches.size(); ++i) {
    std::printf(watches[i].width == 2 ? " %04X=%04X" : " %04X=%02X",
                watches[i].offset, values[i]);
  }
  std::printf("\n");
  std::fflush(stdout);
}

void report_stop(const machine::machine& box, machine::run_end ended,
                 bool trace) {
  {
    std::array<char, machine::stop_report_capacity> text{};
    machine::format_stop_report(box, ended, text);
    std::fputs(text.data(), stderr);
  }
  if (trace) {
    std::vector<char> text(machine::trace_report_capacity);
    machine::format_trace_report(box, text);
    std::fputs(text.data(), stderr);
  }
}

void report_dump(const options& opts, const machine::machine& box,
                 const audio_bridge& bridge, edge_dump& edges) {
  if (!opts.dump_prefix.empty()) {
    const std::filesystem::path ppm(opts.dump_prefix + ".ppm");
    sdl::write_screen_text(opts.dump_prefix + ".txt", box);
    if (sdl::write_ppm(ppm, box.display().pixels(), box.display().palette())) {
      std::fprintf(stderr, "amberfolio: dump frame=%s generation=%llu\n",
                   ppm.string().c_str(),
                   static_cast<unsigned long long>(box.display().generation()));
    } else {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   ppm.string().c_str());
    }

    // Whatever the one consumer managed to put there, whichever thread it
    // was; the stream is destroyed by now, so the callback cannot still
    // be writing.
    const std::size_t captured =
        bridge.captured.load(std::memory_order_relaxed);
    const std::filesystem::path wav(opts.dump_prefix + ".wav");
    if (captured == 0) {
      // Told apart from a failed write on purpose: "the speaker made no
      // sound this run" and "this file could not be created" are two
      // different findings, and only one of them is about the machine.
      std::fprintf(stderr,
                   "amberfolio: dump no audio was captured (nothing pulled"
                   " the speaker)\n");
    } else if (sdl::write_wav(
                   wav, std::span<const float>(bridge.capture.data(), captured),
                   audio_sample_rate)) {
      std::fprintf(stderr, "amberfolio: dump audio=%s samples=%zu%s\n",
                   wav.string().c_str(), captured,
                   bridge.truncated.load(std::memory_order_relaxed)
                       ? " (truncated)"
                       : "");
    } else {
      std::fprintf(stderr, "amberfolio: dump could not write %s\n",
                   wav.string().c_str());
    }

    // And the edge list's trailer. The count is on the last line as well
    // as in this report so that the file answers "is this all of it?" on
    // its own — a truncated dump and a silent run look identical from the
    // top, and only one of them is a finding about the machine.
    edges.finish(box, opts.dump_prefix);
  }
}

void report_audio(bool verify, const machine::machine& box,
                  const listening_level& listening) {
  // The speaker's two host-pacing symptoms, which until now no host read
  // at all (M4-A1, #106). `platform.h` states the policy for each — an
  // underrun holds the last level and keeps its place, an overrun jumps
  // the cursor forward and throws the backlog away — and a policy no host
  // can report is not a tested policy: a stalled run and a smooth one
  // produced the same silence on stderr.
  //
  // Printed whenever there is something to say, and always under
  // `--verify`, whose job is to say what happened whether or not anything
  // did. A windowed run almost always underruns once at the start,
  // because SDL's device pulls before the machine has settled any virtual
  // time at all; that first one is the shape of a healthy run and not a
  // symptom.
  //
  // `dropped edges` is the third and the loudest: it is the *ring*
  // overflowing, which is sound the machine made and no host ever got.
  const std::uint64_t underruns = box.audio().underruns();
  const std::uint64_t resyncs = box.audio().resyncs();
  const std::uint64_t dropped = box.audio().dropped_edges();
  if (verify || underruns != 0 || resyncs != 0 || dropped != 0) {
    // The listening level joins the line only when it is not unity, so
    // that a default run's report is the line it has always been — and
    // so that a run whose sound was turned down says so where somebody
    // asking "why did I hear nothing" will read it (#148). It is
    // appended rather than inserted for the same reason: the three
    // counters in front of it are what cmake/run-verify-program.cmake
    // matches on.
    std::array<char, 32> level{};
    if (listening.muted) {
      std::snprintf(level.data(), level.size(), " volume=muted");
    } else if (listening.volume != 1.0F) {
      std::snprintf(level.data(), level.size(), " volume=%ld%%",
                    std::lround(listening.volume * 100.0F));
    }
    std::fprintf(stderr,
                 "amberfolio: audio underruns=%llu resyncs=%llu dropped"
                 " edges=%llu%s\n",
                 static_cast<unsigned long long>(underruns),
                 static_cast<unsigned long long>(resyncs),
                 static_cast<unsigned long long>(dropped), level.data());
  }
  // The Tandy chip's traffic, when there was any (sound_report.h).
  std::fputs(sdl::chip_traffic_line(box.audio().chip_published(),
                                    box.audio().dropped_chip_writes())
                 .c_str(),
             stderr);
}

}  // namespace amberfolio::sdl
