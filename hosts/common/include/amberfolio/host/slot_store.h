// SPDX-License-Identifier: AGPL-3.0-only
//
// What a playthrough accumulates, persisted beside the save it belongs
// to: M5-E2c (#173) for the exploration, #351 for the journal's read log.
//
// Two enhancements learn something as a party plays. The automap panel
// learns what the party has seen and keeps it in `machine::automap()`;
// the journal's reader learns what the game has cited and keeps it in
// `machine::journal()`. Both live there because both are *observation*
// and not machine state, so a `reset()` drops them and a serialization
// never sees them (`machine/automap.h`, `machine/journal.h`). That is the
// right answer for fidelity and the wrong one for a player, who would
// like the map they filled in last night and the entries the game sent
// them to to still be there tonight. This is the other half: a host reads
// the tables out and writes them into files, and reads them back when a
// machine starts.
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
// So a sidecar is a file of this project's own, with a name of its own,
// in the directory the saves are in.
//
// They go through the machine's `filesystem` rather than round the side
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
// So it is a flag: `--save-sidecars` on the desktop host,
// `af_web_save_sidecars` in the browser. **One flag for both sidecars**,
// because the sentence it is asking permission for is "may this build
// write its own files beside your saves" and that sentence is the same
// one for each. With the seams on and the flag off — which is what every
// session in `tests/sessions` is — nothing here reads or writes a byte.
//
//
// Scoped to the playthrough, which is what a slot is
// --------------------------------------------------
//
// What a party accumulates belongs to a *playthrough*, not to a machine
// and not to the person at the keyboard. Two saved games are two parties
// in two places: one fog table shared between them paints each with the
// other's streets, and one read log shared between them tells each that
// it has already been sent somewhere it has never been. So each sidecar
// comes in two kinds of file, which is the proven design's own
// arrangement carried over:
//
//   * `\SAVE\AFMAP.DAT`, `\SAVE\AFSEEN.DAT` — the working tables.
//     Written whenever the thing they hold moves, so a session that ends
//     without saving has still kept what it walked and what it was told;
//   * `\SAVE\AFMAP<L>.DAT`, `\SAVE\AFSEEN<L>.DAT` — a snapshot per save
//     slot. Written when the program writes slot `L`, and read back
//     **over** the working table when the program reads it — over it even
//     when there is no snapshot there, because an empty map and an empty
//     log are the truth about a playthrough nobody recorded one for.
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
//
//
// The wilderness comes for free, and the journal did not (#351)
// -------------------------------------------------------------
//
// Three enhancements accumulate something. Only two files are here,
// because the explored overlay (#179) keeps nothing of its own: it reads
// the automap's own records for the overworld (`machine/automap.h`), the
// sidecar's version 2 carries a kind byte so an overland record and a
// dungeon record cannot be mistaken for each other, and `write_sidecar`
// writes every used record whatever its kind. So the wilderness is in
// `AFMAP<L>.DAT` already and has been since M5-E5b (#254).
//
// The journal's read log was the one that was wrong. It lived in the
// per-user journal store, one file for every playthrough, beside the
// player's own transcription of their document — and those two are not
// the same kind of fact. A transcription is about the player's *copy*;
// which entries the game has sent them to, and when, is about a
// *party*. So the log moved here (`journal_store.h`'s format, version 5)
// and the text stayed where it was.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/automap.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {
class machine;
}  // namespace amberfolio::machine

namespace amberfolio::host {

/// The directory the sidecars live in, and the two working tables.
///
/// Eight-three, and prefixed so that nothing this project writes can
/// collide with anything the program ships or anything another
/// enhancement of somebody else's has left there.
inline constexpr std::string_view slot_store_directory = "SAVE";
inline constexpr std::string_view slot_store_automap_working =
    "SAVE\\AFMAP.DAT";
inline constexpr std::string_view slot_store_journal_working =
    "SAVE\\AFSEEN.DAT";

/// The largest the exploration sidecar can be: the header plus every
/// record the store can hold. A fixed buffer, because a host has no
/// business heap-allocating per write on a path the seam reaches from a
/// keyboard poll.
inline constexpr std::size_t slot_store_automap_capacity =
    machine::automap_sidecar_header_bytes +
    (machine::automap_state::max_records *
     machine::automap_sidecar_record_bytes);

/// And the largest the read log's is, for the same reason
/// (`journal_store.h`). Two kilobytes and a header.
inline constexpr std::size_t slot_store_journal_capacity =
    journal_log_sidecar_capacity;

/// What went wrong, if anything did.
///
/// Its own reasons rather than DOS codes: two of the three are not
/// filesystem failures at all, and a host that printed `access denied`
/// for "that file is not one of ours" would be saying something untrue.
enum class slot_trouble : std::uint8_t {
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
[[nodiscard]] const char* slot_trouble_name(slot_trouble what) noexcept;

/// The playthrough's sidecars, on both hosts.
///
/// Held by whoever built it, handed to `host_services` so the seams'
/// `automap_update` and `journal_seen` reach it, and shown the DOS
/// layer's file events so it can tell a save from a load. Off until
/// `enable()`.
class slot_store {
 public:
  slot_store() = default;

  /// Turn it on. Nothing before this call and nothing after `enable(false)`
  /// reads or writes a byte.
  void enable(bool on) noexcept { enabled_ = on; }
  [[nodiscard]] bool enabled() const noexcept { return enabled_; }

  /// Where the read log lives between runs.
  ///
  /// A pointer and not a member, because a host already holds a journal
  /// store by the time this object exists and that store is the reader's
  /// one source of truth (`journal_store.h`). Null — the default — is a
  /// host that keeps no journal at all, and then `AFSEEN` is neither
  /// written nor read and everything else here works as it did.
  void set_journal_store(journal_store* store) noexcept { journal_ = store; }
  [[nodiscard]] const journal_store* journal() const noexcept {
    return journal_;
  }

  /// The machine to persist for, and the first read: the working
  /// exploration table, if there is one, into `box.automap()`. Called
  /// once, when a host has a machine with a filesystem attached.
  ///
  /// **The read log is not read here**, and that is the one asymmetry in
  /// this object. The exploration table is read into the machine, which
  /// exists by now; the log is read into the *journal store*, which a
  /// host has not filled from its own per-user file yet — and that file
  /// is parsed wholesale, so a log put there first would be thrown away
  /// by the parse that follows. `read_journal_log()` is the second half,
  /// called once a host has its store.
  void attach(machine::machine& box);

  /// The slot's read log, over whatever the per-user store held.
  ///
  /// Called after the journal store has been read from wherever a host
  /// keeps it and before `restore_journal_log()` puts it in the machine.
  /// A no-op while this is off, with no journal store set, or before
  /// `attach()`.
  void read_journal_log();

  /// The seam says the exploration moved. Writes the working table.
  void changed();

  /// The seam says the read log moved. Writes the working log — and only
  /// if the store agrees it moved (`journal_store::log_changed()`), which
  /// is what keeps a save from rewriting a file that is already right and
  /// lets a load refresh one through this same call.
  void journal_changed();

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
    explicit observer(slot_store& store) noexcept : store_(&store) {}

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
    slot_store* store_;
  };

  // --- what happened, for the host's end-of-run line and for tests ----

  /// How many sidecar files were written and read, both kinds together.
  /// A host prints one line about this object and these are its two
  /// numbers; which of the two files a write was is not something a
  /// player has a use for.
  [[nodiscard]] std::uint32_t writes() const noexcept { return writes_; }
  [[nodiscard]] std::uint32_t reads() const noexcept { return reads_; }

  /// The last slot letter this saw the program save into or load from,
  /// or zero. Upper case, as the filenames are.
  [[nodiscard]] char slot() const noexcept { return slot_; }

  /// The last thing that went wrong, and — where it was the filesystem
  /// that refused — what it said. A host prints these rather than
  /// dropping them: a sidecar that silently is not being written is the
  /// failure a player finds out about last.
  [[nodiscard]] slot_trouble trouble() const noexcept { return trouble_; }
  [[nodiscard]] machine::vfs_error refusal() const noexcept { return refusal_; }

 private:
  /// `SAVE\AFMAP<L>.DAT` and `SAVE\AFSEEN<L>.DAT` for a slot letter.
  [[nodiscard]] static machine::dos_path automap_slot_path(
      char letter) noexcept;
  [[nodiscard]] static machine::dos_path journal_slot_path(
      char letter) noexcept;

  /// The exploration table out of the machine and into `path`, and back.
  void write_automap_to(const machine::dos_path& path);
  void read_automap_from(const machine::dos_path& path);

  /// The read log out of the journal store and into `path`, and back.
  void write_journal_to(const machine::dos_path& path);
  void read_journal_from(const machine::dos_path& path);

  /// The bytes of one whole file, into `into`, and how many into `got`.
  /// False for a file that is not there or would not open — the first of
  /// which is the ordinary case on a first run and is not trouble.
  [[nodiscard]] bool read_whole(const machine::dos_path& path,
                                std::span<std::uint8_t> into, std::size_t& got);

  /// `bytes` into `path`, whole. The two writers' shared half.
  void write_whole(const machine::dos_path& path,
                   std::span<const std::uint8_t> bytes);

  /// The slot letter `path` names, if it is a save slot at all, and zero
  /// otherwise.
  [[nodiscard]] static char slot_of(const machine::dos_path& path) noexcept;

  bool enabled_{false};
  machine::machine* box_{nullptr};
  journal_store* journal_{nullptr};

  /// The slot the program has a save file open on, and whether it made
  /// that file (a save) or found it (a load). Cleared at the close that
  /// acts on it.
  char pending_slot_{0};
  bool pending_is_save_{false};

  char slot_{0};
  std::uint32_t writes_{0};
  std::uint32_t reads_{0};
  slot_trouble trouble_{slot_trouble::none};
  machine::vfs_error refusal_{machine::vfs_error::none};
};

}  // namespace amberfolio::host
