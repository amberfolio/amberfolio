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
// the save, in the save directory, through #170's door, and **never inside the
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
// opinion about what `\SAVE\AFMAPA.DAT` means.
//
//
// Which directory is the save directory (#397)
// --------------------------------------------
//
// The program's own, read the way it reads it: `POOL.CFG` in the
// directory it was started in, line 4 against the same
// (`machine::read_save_directory`). `\SAVE` on the archive release,
// `\POOLRAD` on the copy that saves beside its game files, `\POOLRAD\SAVE`
// on the other — and the sidecars go wherever that is, because they
// belong beside the saves they are about. It is read once, at `attach()`,
// which is why a host makes the current directory current before it.
//
// A copy that does not say — no configuration file, or one that does
// not name a directory — gets **no sidecars**, and `trouble()` says
// `no-save-directory`. Writing them under a likely directory instead
// would be writing into a player's copy on a guess, and the saves they
// are meant to follow would not be there.
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
// And a host with a person in front of it **asks** for that flag rather
// than assuming it (#385): `hosts/sdl/src/sidecar_consent.h` on the
// desktop, `hosts/web/page/sidecars.mjs` in the browser, once each and
// remembered. A run nobody is watching is asked nothing and keeps it
// off, which is what stops a verification run changing the disk every
// recorded session pins.
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
// is a file per save slot, and nothing else:
//
//   * `AFMAP<L>.DAT`, `AFSEEN<L>.DAT` — written when the program writes
//     slot `L`, and read back **over** what the machine holds when the
//     program reads it — over it even when there is no file there,
//     because an empty map and an empty log are the truth about a
//     playthrough nobody recorded one for.
//
// **There is no file that follows the machine between saves.** Every run
// starts at the program's main menu with no party in it, and the only two
// ways out of that menu are a load, which replaces the tables with the
// slot's own, and a new party, whose tables are empty. A file read back
// at startup would be overwritten by the first and wrong for the second
// — a new party would walk into the last one's map. What a session
// explores and does not save is lost with the session, exactly as the
// party's own progress is.
//
// **A file appears only when there is something to put in it** (#385).
// A sidecar with no records in it is its header and nothing else, and a
// save made by a party that has walked nowhere and been cited nothing
// would otherwise put two eight-byte files into somebody's save directory —
// a directory of theirs, changed by this build, saying nothing. So a
// header-only sidecar is written only *over* a file that is already
// there, never as a new one, which is what the host asking permission
// promises a player in so many words (`hosts/sdl/src/sidecar_consent.h`).
// The replacing half is untouched, and has to be: a snapshot that is
// empty is the truth about this party and must still replace the last
// one's list.
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
// `SAVGAMA.DAT` in the save directory was *created* — a save — or
// *opened* — a load. No
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
#include "amberfolio/machine/save_layer.h"
#include "amberfolio/machine/vfs.h"

namespace amberfolio::machine {
class machine;
}  // namespace amberfolio::machine

namespace amberfolio::host {

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
  /// The copy does not say where it saves, so there is nowhere beside
  /// its saves to put anything (this file's "Which directory is the save
  /// directory"). Nothing is read or written.
  no_save_directory,
};

/// The printable name of one, for a host's end-of-run line. Never null.
[[nodiscard]] const char* slot_trouble_name(slot_trouble what) noexcept;

/// The playthrough's sidecars, on both hosts.
///
/// Held by whoever built it, reached through `host_services`, and shown
/// the DOS layer's file events so it can tell a save from a load. Off
/// until `enable()`.
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

  /// The machine to persist for. Called once, when a host has a machine
  /// with a filesystem attached **and its current directory set**: this
  /// is where the save directory is read (this file's "Which directory is
  /// the save directory"), and a copy that does not say leaves this
  /// reading and writing nothing.
  ///
  /// Reads nothing into the machine — a run starts with no party, and
  /// the first load is what fills the tables. It does take away the two
  /// files an earlier build kept between saves (`AFMAP.DAT`,
  /// `AFSEEN.DAT`), which were read back at startup and handed a new
  /// party the last one's map; they are this project's own, and a
  /// player who asked for sidecars asked for this build's.
  void attach(machine::machine& box);

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
  /// The retired files `attach()` takes away are not counted.
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

  /// The directory the sidecars go in, as `attach()` read it, or null
  /// before an attach that was on and for a copy that did not say — and
  /// then `save_directory_trouble()` is why.
  [[nodiscard]] const machine::dos_path* save_directory() const noexcept {
    return located_ ? &save_directory_ : nullptr;
  }
  [[nodiscard]] machine::save_directory_trouble save_directory_trouble()
      const noexcept {
    return save_directory_trouble_;
  }

 private:
  /// Whether this is on, attached, and knows where the saves are: every
  /// read and write asks this first.
  [[nodiscard]] bool live() const noexcept {
    return enabled_ && box_ != nullptr && located_;
  }

  /// `leaf` in the save directory, or the root when it cannot be named —
  /// which every reader and writer below takes as "do nothing".
  [[nodiscard]] machine::dos_path in_save_directory(
      std::string_view leaf) const noexcept;

  /// `AFMAP<L>.DAT` and `AFSEEN<L>.DAT` in the save directory for a slot
  /// letter.
  [[nodiscard]] machine::dos_path automap_slot_path(char letter) const noexcept;
  [[nodiscard]] machine::dos_path journal_slot_path(char letter) const noexcept;

  /// The exploration table out of the machine and into `path`, and back.
  void write_automap_to(const machine::dos_path& path);
  void read_automap_from(const machine::dos_path& path);

  /// The read log out of the journal store and into `path`, and back.
  void write_journal_to(const machine::dos_path& path);
  void read_journal_from(const machine::dos_path& path);

  /// Whether writing `size` bytes to `path` would create a file that
  /// says nothing (#385): a sidecar that is its header alone, going
  /// somewhere there is no file yet. False for a header-only write over
  /// a file that exists, which is a snapshot replacing the last party's
  /// and is the whole reason this is a question about the *path* rather
  /// than about the bytes.
  [[nodiscard]] bool nothing_to_put_in_it(const machine::dos_path& path,
                                          std::size_t size,
                                          std::size_t header_bytes);

  /// The bytes of one whole file, into `into`, and how many into `got`.
  /// False for a file that is not there or would not open — the first of
  /// which is the ordinary case on a first run and is not trouble.
  [[nodiscard]] bool read_whole(const machine::dos_path& path,
                                std::span<std::uint8_t> into, std::size_t& got);

  /// `bytes` into `path`, whole. The two writers' shared half.
  void write_whole(const machine::dos_path& path,
                   std::span<const std::uint8_t> bytes);

  /// The slot letter `path` names, if it is a save slot in the save
  /// directory at all, and zero otherwise.
  [[nodiscard]] char slot_of(const machine::dos_path& path) const noexcept;

  bool enabled_{false};
  machine::machine* box_{nullptr};
  journal_store* journal_{nullptr};

  /// The save directory, read at `attach()`.
  bool located_{false};
  machine::dos_path save_directory_{};
  machine::save_directory_trouble save_directory_trouble_{
      machine::save_directory_trouble::none};

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
