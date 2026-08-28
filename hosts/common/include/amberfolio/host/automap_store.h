// SPDX-License-Identifier: AGPL-3.0-only
//
// The automap's exploration, persisted beside the save: M5-E2c, the last
// leg of #173.
//
// The panel learns what the party has seen and keeps it in
// `machine::automap()`, which a `reset()` drops and a serialization never
// sees, because it is observation and not machine state
// (`machine/automap.h`). That is the right answer for fidelity and the
// wrong one for a player, who would like the map they filled in last
// night to still be there tonight. This is the other half: a host reads
// the table out of the machine and writes it into a file, and reads it
// back when a machine starts.
//
//
// Why the host, and why through the VFS
// -------------------------------------
//
// PLAN.md §4: the core is freestanding and touches no filesystem of its
// own; a host owns files. #173 is specific about which files — *beside*
// the save, under `\SAVE\`, through #170's door, and **never inside the
// program's own files**. A save game the program wrote must still be a
// save game the program wrote, byte for byte, with the seam off or on.
// So the sidecar is a file of this project's own, with a name of its
// own, in the directory the saves are in.
//
// It goes through the machine's `filesystem` rather than round the side
// of it for two reasons. The first is that this has to work in a browser,
// where there is no directory to open — the page's VFS is the only
// filesystem there is. The second is that the DOS path semantics are
// core's alone (#146): a host that built its own path would be a second
// opinion about what `\SAVE\AFMAP.DAT` means.
//
//
// Off unless a host is asked, and why that is not timidity
// -------------------------------------------------------
//
// Writing here is writing into a real directory of the player's, on the
// desktop host, which `directory_vfs` maps onto real files. Two things
// follow, and they point the same way:
//
//   * a file that appears in a game directory changes it, and this
//     project's whole method is runs that can be compared. Every one of
//     the recorded sessions pins its disk by name, size and SHA-256, and
//     a sidecar written by a verification run would make the next run's
//     disk a different disk;
//   * a player who has not asked for their installation to be written to
//     has not asked.
//
// So it is a flag: `--automap-store` on the desktop host,
// `af_web_automap_store` in the browser. With the automap seam on and the
// flag off — which is what every session in `tests/sessions` is — nothing
// here reads or writes anything at all.
//
//
// Scoped to the playthrough, the way the proven design scopes it
// --------------------------------------------------------------
//
// Exploration belongs to a *playthrough*, not to a machine. Two saved
// games are two parties in two places, and one fog table shared between
// them paints each with the other's streets. So there are two kinds of
// file, which is the proven design's own arrangement carried over:
//
//   * `\SAVE\AFMAP.DAT` — the working table. Written whenever the seam
//     says the exploration moved, so a session that ends without saving
//     has still kept what it walked;
//   * `\SAVE\AFMAP<L>.DAT` — a snapshot per save slot. Written when the
//     program writes slot `L`, and read back **over** the working table
//     when the program reads it — over it even when there is no snapshot
//     there, because an empty map is the truth about a playthrough
//     nobody recorded one for.
//
// **A slot the program only looked at is not a slot it loaded.** The load
// menu opens every save file in the directory in turn to find out which
// slots exist, so the naming call alone would fire nine times for one
// load and leave the player looking at the last slot in the directory's
// map. What tells them apart is whether bytes actually moved through the
// handle, which is what `file_event`'s two traffic flags say and what the
// proven design's own file layer counted (`machine/dos.h`).
//
// **And the slot is learnt from the file traffic, not from the program.**
// The program keeps no slot letter anywhere in memory for a seam to read:
// it prompts for one, builds a filename out of it and lets it go. What
// there is instead is the DOS layer's own record of which files were
// named and what happened to them (`machine/diagnostics.h`), which says
// `SAVE\SAVGAMA.DAT` was *created* — a save — or *opened* — a load. No
// game code is involved and no new address fact is needed, which is
// exactly the argument the proven design made for hooking its own file
// layer rather than the save routine.
//
// A create and a close, rather than the create alone: the program writes
// the slot file and then moves each party member's character files in
// beside it, and a snapshot taken at the create would be taken before the
// save it is a snapshot of had finished happening.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {
class machine;
}  // namespace amberfolio::machine

namespace amberfolio::host {

/// The directory the sidecars live in, and the stem they are named from.
///
/// Eight-three, and prefixed so that nothing this project writes can
/// collide with anything the program ships or anything another
/// enhancement of somebody else's has left there.
inline constexpr std::string_view automap_store_directory = "SAVE";
inline constexpr std::string_view automap_store_working = "SAVE\\AFMAP.DAT";

/// The largest a sidecar can be: the header plus every record the store
/// can hold. A fixed buffer, because a host has no business heap-
/// allocating per write on a path the seam reaches from a keyboard poll.
inline constexpr std::size_t automap_store_capacity =
    machine::automap_sidecar_header_bytes +
    (machine::automap_state::max_records *
     machine::automap_sidecar_record_bytes);

/// What went wrong, if anything did.
///
/// Its own reasons rather than DOS codes: two of the three are not
/// filesystem failures at all, and a host that printed `access denied`
/// for "that file is not one of ours" would be saying something untrue.
enum class automap_trouble : std::uint8_t {
  /// Nothing has gone wrong.
  none,
  /// The filesystem refused a create, an open, a write or a read.
  /// `refusal()` is what it said.
  file_refused,
  /// A write came up short, which is DOS's own answer for a full disk.
  out_of_room,
  /// Something is there under a sidecar's name and is not a sidecar —
  /// a file from a later version of this format, or somebody else's.
  /// Refused rather than guessed at.
  not_a_sidecar,
};

/// The printable name of one, for a host's end-of-run line. Never null.
[[nodiscard]] const char* automap_trouble_name(automap_trouble what) noexcept;

/// The exploration sidecar, on both hosts.
///
/// Held by whoever built it, handed to `host_services` so the seam's
/// `automap_update` reaches it, and shown the DOS layer's file events so
/// it can tell a save from a load. Off until `enable()`.
class automap_store {
 public:
  automap_store() = default;

  /// Turn it on. Nothing before this call and nothing after `enable(false)`
  /// reads or writes a byte.
  void enable(bool on) noexcept { enabled_ = on; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  /// The machine to persist for, and the first read: the working table,
  /// if there is one, into `box.automap()`. Called once, when a host has
  /// a machine with a filesystem attached.
  void attach(machine::machine& box);

  /// The seam says the exploration moved. Writes the working table.
  void changed();

  /// One naming call the DOS layer resolved. Save-slot traffic is the
  /// only thing this looks for; everything else is ignored.
  void saw(const machine::file_event& event);

  /// A `diagnostics` sink that feeds `saw()` and drops everything else.
  ///
  /// It is here because the two hosts reach the DOS layer's file events
  /// by different routes. The desktop host owns its own sink and calls
  /// `saw()` from it; the wasm module's sink is core's `diagnostic_log`,
  /// inside the ABI's own handle, and the only place a second C++
  /// consumer can stand is that log's relay (`machine/log.h`). This is
  /// the shape that relay wants.
  class observer final : public machine::diagnostics {
   public:
    explicit observer(automap_store& store) noexcept : store_(&store) {}

    void report(const machine::file_event& event) override {
      store_->saw(event);
    }

    // Everything else is somebody else's business. They are not dropped
    // on the floor — the log this relays from keeps them all.
    void report(const machine::notice&) override {}
    void report(const machine::service_call&) override {}
    void report(const machine::stop_record&) override {}
    void report(const cpu::stop_record&) override {}
    void report(const machine::device_stop&) override {}
    void report(const machine::seam_event&) override {}

   private:
    automap_store* store_;
  };

  // --- what happened, for the host's end-of-run line and for tests ----

  /// How many times a sidecar was written and read.
  [[nodiscard]] std::uint32_t writes() const noexcept { return writes_; }
  [[nodiscard]] std::uint32_t reads() const noexcept { return reads_; }

  /// The last slot letter this saw the program save into or load from,
  /// or zero. Upper case, as the filenames are.
  [[nodiscard]] char slot() const noexcept { return slot_; }

  /// The last thing that went wrong, and — where it was the filesystem
  /// that refused — what it said. A host prints these rather than
  /// dropping them: a sidecar that silently is not being written is the
  /// failure a player finds out about last.
  [[nodiscard]] automap_trouble trouble() const noexcept { return trouble_; }
  [[nodiscard]] machine::vfs_error refusal() const noexcept { return refusal_; }

 private:
  /// `SAVE\AFMAP<L>.DAT` for a slot letter, into `out`.
  [[nodiscard]] static machine::dos_path slot_path(char letter) noexcept;

  /// The table out of the machine and into `path`.
  void write_to(const machine::dos_path& path);

  /// `path` back into the machine, if it is there and is a sidecar.
  void read_from(const machine::dos_path& path);

  /// The slot letter `path` names, if it is a save slot at all, and zero
  /// otherwise.
  [[nodiscard]] static char slot_of(const machine::dos_path& path) noexcept;

  bool enabled_{false};
  machine::machine* box_{nullptr};

  /// The slot the program has a save file open on, and whether it made
  /// that file (a save) or found it (a load). Cleared at the close that
  /// acts on it.
  char pending_slot_{0};
  bool pending_is_save_{false};

  char slot_{0};
  std::uint32_t writes_{0};
  std::uint32_t reads_{0};
  automap_trouble trouble_{automap_trouble::none};
  machine::vfs_error refusal_{machine::vfs_error::none};
};

}  // namespace amberfolio::host
