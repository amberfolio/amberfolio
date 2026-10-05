// SPDX-License-Identifier: AGPL-3.0-only
//
// What a run says while it goes and when it ends (#472, split from main.cpp):
// the console, the stills, the `--watch` and `--trace` instruments, and the
// closing reports -- the stop line, `--dump`'s files and the speaker's
// pacing symptoms. Every line is one a script or a person reads
// (docs/hosts.md).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/report.h"
#include "audio_device.h"
#include "edge_dump.h"
#include "options.h"

namespace amberfolio::sdl {

/// Write one `--dump-every` still: `PREFIX-NNNNNN.ppm`, six digits so a
/// directory listing sorts into the order the frames happened in for any
/// run short of three virtual hours.
///
/// Failures are silent on purpose. A still is an observation aid, and a
/// run that stopped to complain about a full disk in the middle of the
/// thing being observed would have destroyed what it was there to show;
/// the missing file is the report.
void write_still(const std::string& prefix, std::uint64_t frame,
                 const machine::machine& box);

/// Drain whatever DOS console output has accumulated to stdout.
///
/// Pulled, not pushed: `console_output` is a buffer the host empties, not
/// a sink the core writes through, because nothing in core ever calls out
/// (platform.h). There is no text mode to render to and none is planned,
/// so stdout is the whole of what a program's console output means here.
void drain_console(machine::machine& box);

/// Print every overlay-tracker record newer than `printed`, and move it
/// on (machine/overlay.h). Once per slice, so a read that was replaced
/// inside one slice is not seen — a trace, not a log — which is plenty
/// for the thing it is for: reading the facts of a load off the program
/// rather than guessing them.
void print_overlay_loads(const machine::machine& box, std::uint64_t& printed);

/// What the last `--trace` line said about the automap's door rule, so
/// the next one is printed only if something moved.
struct door_rule_log {
  bool said{false};
  std::uint8_t disk{};
  std::uint8_t area{};
  std::uint8_t geo{};
  std::uint16_t seen{};
  std::uint16_t from_table{};
  machine::automap_door_tally drawn;

  friend bool operator==(const door_rule_log&, const door_rule_log&) = default;
};

/// Which evidence made the automap's door leaves on this map (#268).
///
/// The panel draws a leaf where a wall face's *kind* has been seen shut —
/// on this map, or in the seam's table of every shut face in the shipped
/// data — and the two are the same yellow pixels. Every driven leg up to
/// #268 was over New Phlan, which has no shut face on it and whose wall
/// set is not in the table, so the panel there fell all the way through
/// to the older "every passable face is a door" rule and no still could
/// show otherwise. This line is what makes a still evidence: `seen` is
/// the map's own shut kinds and `table` is the shipped table's, and
/// `drawn` counts the leaves actually on the panel by which of the four
/// rules put each one there — a shut face, a way through whose kind was
/// seen shut on this map, a way through only the table names, and a way
/// through on a map where nothing is known at all.
///
/// `machine::automap()` and not a bus cycle, for `print_watch`'s reason:
/// a `--trace` that changed what the machine did could not be used to
/// say a run was clean. It is observation the seam already keeps
/// (`automap.h`) — nothing here is machine state, and with the automap
/// seam off there is nothing to print.
void print_door_rule(const machine::machine& box, door_rule_log& log,
                     std::uint64_t frame_index);

/// What a watch has already said, per segment.
///
/// Per segment rather than one running value, because DS at a frame
/// boundary is not always the program's data segment and a watch that
/// forgot which segment it last read would alternate: the bytes under
/// some other segment differ from the program's, so they print, and then
/// the program's differ from those and print again — two lines a frame,
/// neither of them about the thing being watched. Remembering per
/// segment turns each of those into one line the first time it is seen
/// and silence after, which leaves the log saying exactly what a watch is
/// for: when the watched values moved, and to what.
///
/// Bounded because nothing here should be able to grow without limit on
/// what a program does; a run that ends frames in more segments than this
/// starts forgetting the oldest, and the only cost of forgetting is a
/// line that says again what it said before.
struct watch_log {
  struct entry {
    std::uint16_t segment{};
    std::vector<std::uint16_t> values;
  };
  static constexpr std::size_t max_segments = 64;
  std::vector<entry> seen;
};

/// Read the watched values out of RAM and print them if they moved.
///
/// Offsets are resolved against the processor's DS, and not against
/// `seam_engine::image_base()`, because a global is where the program's
/// own code says it is: this program unpacks itself, and its data
/// segment is nowhere near the image the loader placed. That is the same
/// anchor the seams read their globals through (seam_cheats.cpp), so an
/// offset that means something to one means the same thing to the other.
///
/// `memory_map::ram()` and not `machine::read_memory()`: this is not a
/// bus cycle. A watch that took one would latch an EGA plane or make an
/// open-bus notice of its own, and a run whose log a watch had written
/// into could not be used to say the run was clean.
void print_watch(machine::machine& box, const std::vector<watch_point>& watches,
                 watch_log& log, std::uint64_t frame_index);

/// The stop report: the whole point of this host in M3, formatted in core so
/// that the browser prints the same sentence (machine/report.h). Called after
/// SDL is down, so that nothing SDL writes on its way out can land in the
/// middle of it; and the trace ring after it, when `--trace` kept one.
void report_stop(const machine::machine& box, machine::run_end ended,
                 bool trace);

/// `--dump`'s files, written once the run is over: the last frame and its
/// text, the WAV, and the edge list's trailer.
void report_dump(const options& opts, const machine::machine& box,
                 const audio_bridge& bridge, edge_dump& edges);

/// The speaker's two host-pacing symptoms and the Tandy chip's traffic.
void report_audio(bool verify, const machine::machine& box,
                  const listening_level& listening);

}  // namespace amberfolio::sdl
