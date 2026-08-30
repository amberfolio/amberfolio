// SPDX-License-Identifier: AGPL-3.0-only
//
// The WebAssembly module's entry point. It runs on module instantiation,
// reports the core version through the C ABI, and returns.
//
// Calling af_version() here is not only for the log line: it is what pulls
// the ABI object out of the core's static archive and into the link, which
// is what makes -sEXPORTED_FUNCTIONS able to export it — and since M2-F4
// (#45) that object carries the whole platform interface, so this one call
// is what makes every af_machine_* export reachable.
//
// The module runs a machine now, and M2-H2 (#55) is the page that does:
// host.mjs drives create -> af_machine_attach_reference_devices() ->
// af_web_demo_program_bytes() -> run loop, over canvas, an AudioWorklet
// and the keyboard. tests/smoke.mjs drives the identical sequence
// headlessly.
//
// M3-F2 (#84) gave the page a second thing to run: a directory of the
// player's own, put into the machine's filesystem one file at a time
// (af_machine_vfs_put) and booted from there (af_machine_load_from_vfs).
// The embedded program below did not change and is not going anywhere —
// it is what proves the boundary works without anybody having a game.
//
// M4-F4 (#98) adds the seam probe: the self-written program tests/programs
// runs with its own seam on and off, staged here so the node smoke check
// can toggle a seam through the ABI and assert the difference — the same
// program, the same seam, the same two results the native suite asserts.
// The seam is registered into the machine's engine through this host's
// own export rather than being part of core's table: it is a test seam
// keyed to a test program, and a player's listing has no business
// carrying it.
//
// <cstdio> rather than <iostream>/<format>: this is the one target where
// the standard library we pull in becomes bytes the player downloads
// (PLAN.md §4 — keeping the wasm bundle lean is why there is a hand-written
// JS host at all). Emscripten routes stdout to the console.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "amberfolio/abi.h"
#include "amberfolio/abi_bridge.h"
#include "amberfolio/host/automap_store.h"
#include "amberfolio/host/host_services.h"
#include "amberfolio/host/journal_extract.h"
#include "amberfolio/host/journal_facts.h"
#include "amberfolio/host/journal_ingest.h"
#include "amberfolio/host/journal_probe.h"
#include "amberfolio/host/journal_store.h"
#include "amberfolio/machine/log.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/sha256.h"
#include "demo_program.h"
#include "programs/machine_programs.h"

namespace {

/// Assembled once, on first use, and never freed — the module's whole
/// life is one page load, so there is nothing to reclaim it for. A
/// function-local static rather than a namespace-scope one: construction
/// order between translation units is otherwise unspecified, and this
/// way the vector exists exactly when something first asks for it and
/// not a moment before.
const std::vector<std::uint8_t>& demo_program() {
  static const std::vector<std::uint8_t> program =
      amberfolio::web::demo_program_bytes();
  return program;
}

/// The host services this module attaches to whatever machine the page
/// asks it to (M5-D1, #169) — the same object the SDL host attaches
/// (hosts/common), because a callout has to mean the same thing on both.
///
/// One, and never freed: the module's whole life is one page load, and
/// the page's whole life is one machine. A function-local static for the
/// reason `demo_program()` above is one.
amberfolio::host::host_services& services() {
  static amberfolio::host::host_services one;
  return one;
}

/// The journal ingestion this module is in the middle of (M5-E3, #174).
///
/// One, and never freed, for the reason `services()` above is one: the
/// module's whole life is one page load. It holds the document *by
/// value* — a page hands in a pointer into linear memory and a length,
/// and a page that freed that buffer before the last entry was extracted
/// would be a use-after-free across the ABI. A copy of the file the
/// player just chose is the cheapest possible way for that not to be
/// something a host author has to remember.
///
/// The store lives here too, in memory, for the life of the tab. That is
/// #174's stated M5 state for the browser and the page says so out loud:
/// IndexedDB is M6's, and quietly losing a player's corrections would be
/// the one kind of failure they find out about by losing work.
struct journal_session {
  std::vector<std::uint8_t> document;
  amberfolio::host::journal_ingester ingester;
  amberfolio::host::journal_store store;
  /// Whether the *next* `af_web_journal_ingest` looks the document up in
  /// the synthetic probe table rather than the shipped one. Test
  /// apparatus, exactly like `af_web_probe_seam_register` next door: a
  /// player's build knows nothing about a document this project made up.
  bool probe{false};
};

journal_session& journal() {
  static journal_session one;
  return one;
}

/// A string out through the ABI: `text` into `out`, NUL-terminated,
/// answering how many characters that was (the terminator not counted),
/// or the length it would have needed when `out` is null or too small.
///
/// The shape the core ABI's own name-writers have
/// (`af_machine_vfs_name_at`), so a page reads every string in this
/// module the same way: ask with null, allocate, ask again.
std::uint32_t hand_out(std::string_view text, char* out, std::uint32_t cap) {
  if (out == nullptr || cap <= text.size()) {
    return static_cast<std::uint32_t>(text.size());
  }
  std::memcpy(out, text.data(), text.size());
  out[text.size()] = '\0';
  return static_cast<std::uint32_t>(text.size());
}

}  // namespace

extern "C" {

// Web-host-specific exports, alongside the core ABI's — see this file's
// own top comment and hosts/web/CMakeLists.txt's export list, which is the
// only place that decides whether a browser can actually reach these. Not
// part of core/include/amberfolio/abi.h: they hand out this *host's*
// embedded programs and register this host's test seam, not anything the
// core itself knows about, so they live beside the code that assembled
// them.

/// A pointer into the module's own linear memory, stable for the
/// module's life — the same "core-owned, read through a typed-array
/// view" shape abi.h's framebuffer/palette pointers already have.
const uint8_t* af_web_demo_program_bytes(void) { return demo_program().data(); }

uint32_t af_web_demo_program_size(void) {
  return static_cast<uint32_t>(demo_program().size());
}

/// The seam probe program (tests/programs/machine_programs.cpp), as the MZ
/// file a page puts on the filesystem and loads — same shape as the demo.
const uint8_t* af_web_probe_program_bytes(void) {
  return amberfolio::programs::seam_probe_file().data();
}

uint32_t af_web_probe_program_size(void) {
  return static_cast<uint32_t>(amberfolio::programs::seam_probe_file().size());
}

/// Register the probe's three seams with `box`'s engine, so
/// `af_machine_seam_*` can list and toggle them. AF_NO_MACHINE for a null
/// handle; AF_INVALID if the registry would not take one (already
/// registered, or full).
///
/// Two, since #147: `probe`, whose points the program runs through, and
/// `probe-unreached`, whose one point sits on an instruction it never
/// executes. Both are keyed to the same file, so both are equally
/// available and both arm — and the only thing that tells them apart is
/// `af_machine_seam_fired`, which is what a browser needed and did not
/// have. Registering them together rather than through two exports keeps
/// the pair inseparable: a smoke check cannot end up asserting the happy
/// one and quietly skipping the other.
///
/// Three, since #161: `probe-trigger` shares `probe`'s point and is
/// **pulled** rather than left on, so a browser can be asked the one
/// question the ABI could not answer before — does a trigger nobody
/// pulled leave the machine alone, and does one somebody pulled act
/// exactly once.
///
/// Four, since #163: `probe-pull` is a trigger whose point has **no
/// address**, so it is offered at every step boundary while the pull is
/// outstanding and decides for itself when acting is safe. A browser
/// gets to ask that one too, because "one bool per step when nobody
/// pulled" is a claim about a hot path and hot paths differ per target.
///
/// Five, since M5-D1 (#169): `probe-host` calls a host service at each of
/// two points that are reached exactly once, so a browser can be asked
/// whether the seam -> host direction works in *its* module — the one
/// question the whole issue is about, and one that can only be answered
/// where the implementation actually runs.
uint32_t af_web_probe_seam_register(af_machine* box) {
  amberfolio::machine::machine* pc = amberfolio::af_machine_unwrap(box);
  if (pc == nullptr) {
    return AF_NO_MACHINE;
  }
  const bool ok =
      pc->seams().add(amberfolio::programs::seam_probe_definition()) &&
      pc->seams().add(
          amberfolio::programs::seam_probe_unreached_definition()) &&
      pc->seams().add(amberfolio::programs::seam_probe_trigger_definition()) &&
      pc->seams().add(amberfolio::programs::seam_probe_pull_definition()) &&
      pc->seams().add(amberfolio::programs::seam_probe_host_definition());
  return ok ? AF_OK : AF_INVALID;
}

/// Attach this module's `seam_host_services` to `box` (M5-D1, #169), so
/// that a seam's `call_host()` reaches an implementation instead of
/// answering false.
///
/// A host export rather than part of `af_machine_attach_reference_devices`
/// for the reason abi.h's whole seam section gives: what a *host* plugs
/// into a machine is the host's decision, and core does not have one to
/// make. `host.mjs` calls it right after the devices, so every page and
/// every check that goes through the JS host has it; a caller that drives
/// the ABI by hand and does not is a machine whose seams call out into
/// nothing, which is the honest answer for a host that attached nothing.
///
/// Attaching changes nothing about a run with every seam off: no handler
/// runs, so nothing calls out. That is the fidelity invariant with a
/// host in the room, and it is asserted rather than asserted-about
/// (tests/core/machine/seam_test.cpp).
///
/// `AF_NO_MACHINE` for a null handle, `AF_OK` otherwise. Idempotent —
/// attaching the same object twice is attaching it.
uint32_t af_web_attach_host_services(af_machine* box) {
  amberfolio::machine::machine* pc = amberfolio::af_machine_unwrap(box);
  if (pc == nullptr) {
    return AF_NO_MACHINE;
  }
  pc->seams().set_host(&services());
  // And where `journal_open` looks an entry up (M5-E4, #175): this tab's
  // store, which lives for the life of the tab and is filled by
  // `af_web_journal_ingest`. Attached here rather than at an ingestion so
  // that the pointer is set once and stays set — the store object does not
  // move, only its contents change, and a page that ingested a journal
  // after loading the program should not have to attach anything again.
  services().set_journal_store(&journal().store);
  return AF_OK;
}

/// Keep the automap's exploration beside the save, in this module's own
/// filesystem (M5-E2c, #173).
///
/// The same object the desktop host's `--automap-store` turns on
/// (`hosts/common`), so a browser and a terminal write the same file with
/// the same bytes. Off unless a page asks, for the reasons
/// `automap_store.h` gives — and for one more that is the page's alone: a
/// browser's filesystem is whatever was dropped into it, and writing into
/// it is what makes an exploration outlive the tab.
///
/// **Call it after the files are in and before the program is loaded.**
/// Turning it on reads the working table off the filesystem, so a
/// filesystem that is still empty has nothing to give it.
///
/// The file events it needs — which save slot the program touched — reach
/// it through the diagnostic log's relay (`machine/log.h`), because in
/// this module that log *is* the machine's sink and there is nowhere else
/// for a second C++ consumer to stand.
///
/// `AF_NO_MACHINE` for a null handle, `AF_OK` otherwise.
uint32_t af_web_automap_store(af_machine* box, int32_t on) {
  amberfolio::machine::machine* pc = amberfolio::af_machine_unwrap(box);
  if (pc == nullptr) {
    return AF_NO_MACHINE;
  }
  static amberfolio::host::automap_store::observer watcher{
      services().automap()};
  if (amberfolio::machine::diagnostic_log* log =
          amberfolio::af_machine_log_unwrap(box);
      log != nullptr) {
    log->set_relay(on != 0 ? &watcher : nullptr);
  }
  services().automap().enable(on != 0);
  services().automap().attach(*pc);
  return AF_OK;
}

/// The virtual tick this module's host services last saw `which` called
/// at, or zero if it has not been called.
///
/// The count and the argument are core's (`af_machine_seam_host_calls`,
/// `af_machine_seam_host_argument`) because they are the engine's record
/// of what it routed. This one is the *implementation's*, and is the
/// fact that makes the call synchronous rather than queued: the machine's
/// own time at the instant the seam called out. A page that could only
/// read the count could not tell a callout served during the run from
/// one served after it.
// --- The journal's ingestion (M5-E3, #174) -----------------------------
//
// The desktop host does all of this in one call, because its OCR engine
// is a C++ object it can call (`hosts/sdl/src/tesseract_ocr.h`). A
// browser's engine is tesseract.js, which lives on the far side of this
// boundary and is asynchronous, so the loop is inverted: the page asks
// for entry `i`'s pixels, recognizes them in JS, hands the text back, and
// asks for the next. `host/journal_ingest.h` is written for both, and
// says so.
//
// These answer a `host::journal_trouble` directly rather than an `AF_*`
// code — zero is `none` and nothing else is — because every one of the
// dozen reasons is a different sentence a player needs, and folding them
// into `AF_INVALID` would be exactly the loss abi.h's `AF_UNRECOGNIZED`
// exists to avoid. `af_web_journal_trouble_name` is the words.

/// Ingest `size` bytes at `bytes`: hash them, look the edition up, and be
/// ready to extract. The bytes are copied, so the page may free its
/// buffer the moment this returns.
uint32_t af_web_journal_ingest(const uint8_t* bytes, uint32_t size) {
  journal_session& session = journal();
  if (bytes == nullptr) {
    return static_cast<uint32_t>(
        amberfolio::host::journal_trouble::unrecognized_edition);
  }
  session.document.assign(bytes, bytes + size);
  session.ingester = amberfolio::host::journal_ingester(
      session.probe ? amberfolio::host::journal_probe_table()
                    : amberfolio::host::known_journals());
  const amberfolio::host::journal_trouble why =
      session.ingester.begin(session.document);
  if (why == amberfolio::host::journal_trouble::none) {
    // Points the store at this edition, clearing it if it was a store of
    // another one — the same thing `run()` does for the desktop, and the
    // reason a page driving the loop by hand still cannot mix two
    // printings' entries together.
    session.ingester.adopt(session.store);
    session.store.set_engine("none");
  }
  return static_cast<uint32_t>(why);
}

/// The words for a trouble code — the same sentence the desktop host
/// prints, so two hosts do not describe one finding differently.
uint32_t af_web_journal_trouble_name(uint32_t code, char* out, uint32_t cap) {
  return hand_out(amberfolio::host::journal_trouble_name(
                      static_cast<amberfolio::host::journal_trouble>(code)),
                  out, cap);
}

/// The document's fingerprint, as 64 lowercase hex characters — the half
/// of an unrecognized answer a player can act on.
uint32_t af_web_journal_fingerprint(char* out, uint32_t cap) {
  return hand_out(journal().ingester.fingerprint_hex(), out, cap);
}

/// The recognized edition's name, or nothing.
uint32_t af_web_journal_edition_name(char* out, uint32_t cap) {
  const amberfolio::host::journal_edition* edition =
      journal().ingester.edition();
  return hand_out(edition == nullptr ? std::string_view{} : edition->name, out,
                  cap);
}

/// How many rows this edition's fact table has, and the *citation* the
/// game itself uses for row `index`.
///
/// A citation and not a number since #218: three of the journal's
/// sections are numbered and each starts from its own base, so the two
/// travel together. It comes across as the one packed word the seam's own
/// host callout uses (`machine/journal.h`), which keeps this ABI the
/// width it was and lets the page treat it as opaque — the page never
/// needs to take it apart, only to hand it back.
uint32_t af_web_journal_entry_count(void) {
  return static_cast<uint32_t>(journal().ingester.entries());
}

uint32_t af_web_journal_entry_citation(uint32_t index) {
  const amberfolio::host::journal_entry_fact* fact =
      journal().ingester.entry_at(index);
  if (fact == nullptr) {
    return 0U;
  }
  return amberfolio::machine::journal_open_argument(
      {.kind = fact->kind, .number = fact->number});
}

/// Get entry `index`'s scan: every piece of it, by whichever route its
/// filter takes.
///
/// `af_web_journal_encoding()` then says which of the two shapes the
/// pieces are (#212) and `af_web_journal_part_count()` how many there are
/// (#214); the accessors below take a piece's index. An entry is one piece
/// unless its edition's entries flow between columns, and the page joins
/// what it reads out of them in this order.
uint32_t af_web_journal_extract(uint32_t index) {
  return static_cast<uint32_t>(journal().ingester.extract(index));
}

/// Which shape the last `af_web_journal_extract` produced: 0 for gray
/// samples this module decoded, 1 for a stream it did not (#212).
///
/// A number rather than two "is it" predicates, because it is one
/// question with two answers and a page that asked it twice could get an
/// impossible pair. It is the scan's, not a piece's: every piece of one
/// entry is the same shape (`host/journal_extract.h`).
uint32_t af_web_journal_encoding(void) {
  return static_cast<uint32_t>(journal().ingester.scan().encoding);
}

/// How many pieces the last extraction produced (#214).
uint32_t af_web_journal_part_count(void) {
  return static_cast<uint32_t>(journal().ingester.scan().parts.size());
}

namespace {

/// Piece `which` of the last extraction, or null.
const amberfolio::host::journal_part* journal_part_at(uint32_t which) {
  const amberfolio::host::journal_scan& scan = journal().ingester.scan();
  return which < scan.parts.size() ? &scan.parts[which] : nullptr;
}

}  // namespace

/// A piece's pixels. Null and zero for an encoded scan, and for a piece
/// that is not there.
///
/// The same "core-owned, read through a typed array" shape abi.h's
/// framebuffer has, and the same warning: a view is detached when memory
/// grows, so re-derive rather than cache.
const uint8_t* af_web_journal_image_bytes(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? nullptr : part->gray.pixels.data();
}

uint32_t af_web_journal_image_width(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->gray.width;
}

uint32_t af_web_journal_image_height(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->gray.height;
}

/// A piece's stream, for an encoded scan. Null and zero for a decoded one.
const uint8_t* af_web_journal_encoded_bytes(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? nullptr : part->encoded.data();
}

uint32_t af_web_journal_encoded_size(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : static_cast<uint32_t>(part->encoded.size());
}

/// Which rectangle of an encoded piece's image that piece is, in that
/// image's own samples — what the page's engine filters its words by
/// (`host/journal_ocr.h`). All zero for a decoded piece, where the crop
/// already happened.
uint32_t af_web_journal_region_left(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->region.left;
}

uint32_t af_web_journal_region_top(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->region.top;
}

uint32_t af_web_journal_region_width(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->region.width;
}

uint32_t af_web_journal_region_height(uint32_t which) {
  const amberfolio::host::journal_part* part = journal_part_at(which);
  return part == nullptr ? 0U : part->region.height;
}

/// What the page's engine read for the item `citation` names. Replaces
/// the scan and leaves any correction alone, which is what makes a
/// correction survive a re-ingestion (`host/journal_store.h`).
///
/// `citation` is what `af_web_journal_entry_citation()` handed out. A
/// word that does not decode to one is refused rather than read as some
/// other item, which is the same answer the seam's callout gives (#218).
uint32_t af_web_journal_set_text(uint32_t citation, const char* text) {
  const amberfolio::machine::journal_citation what =
      amberfolio::machine::journal_open_citation(citation);
  if (text == nullptr || !what) {
    return AF_INVALID;
  }
  return journal().store.record_scan(what, text) ? AF_OK : AF_NO_ROOM;
}

/// A person's correction to one item.
uint32_t af_web_journal_correct(uint32_t citation, const char* text) {
  const amberfolio::machine::journal_citation what =
      amberfolio::machine::journal_open_citation(citation);
  if (text == nullptr || !what) {
    return AF_INVALID;
  }
  return journal().store.correct(what, text) ? AF_OK : AF_NO_ROOM;
}

/// What a reader shows for one item: the correction if there is one, the
/// scan otherwise. This is what #175 reads.
uint32_t af_web_journal_text(uint32_t citation, char* out, uint32_t cap) {
  const amberfolio::machine::journal_citation what =
      amberfolio::machine::journal_open_citation(citation);
  if (!what) {
    return 0U;
  }
  return hand_out(journal().store.text(what), out, cap);
}

/// What the engine that read this store was, as it named itself.
uint32_t af_web_journal_set_engine(const char* what) {
  if (what == nullptr) {
    return AF_INVALID;
  }
  journal().store.set_engine(what);
  return AF_OK;
}

/// How many entries the store holds, how many have text, and how many
/// carry a correction — the three numbers the page reports.
uint32_t af_web_journal_store_size(void) {
  return static_cast<uint32_t>(journal().store.size());
}

uint32_t af_web_journal_store_recognized(void) {
  return static_cast<uint32_t>(journal().store.recognized());
}

uint32_t af_web_journal_store_corrections(void) {
  return static_cast<uint32_t>(journal().store.corrections());
}

/// The store as its file would be, and the same bytes back in.
///
/// The browser keeps its store in memory in M5 (#174), so nothing here
/// writes a file — but a page that can serialize can offer a player the
/// text they just spent an hour correcting, and M6's IndexedDB is then
/// two lines rather than a format decision. `_read` answers a
/// `journal_trouble`, so a store from a later version is refused with a
/// reason rather than half-read.
uint32_t af_web_journal_store_write(char* out, uint32_t cap) {
  return hand_out(journal().store.serialize(), out, cap);
}

uint32_t af_web_journal_store_read(const char* text, uint32_t size) {
  if (text == nullptr) {
    return static_cast<uint32_t>(
        amberfolio::host::journal_trouble::not_a_store);
  }
  return static_cast<uint32_t>(
      journal().store.parse(std::string_view(text, size)));
}

/// The SHA-256 of the store's own bytes: what a maintainer reports about
/// an ingestion of their own document, and the only thing about a store
/// that may be written down anywhere (`host/journal_store.h`).
uint32_t af_web_journal_store_fingerprint(char* out, uint32_t cap) {
  std::array<char, amberfolio::sha256_digest::text_length + 1> hex{};
  const std::size_t written =
      amberfolio::format_hex(journal().store.fingerprint(), hex);
  return hand_out(std::string_view(hex.data(), written), out, cap);
}

/// Look the *next* ingestion up in the synthetic probe table, and hand
/// out the probe document itself.
///
/// Test apparatus, and a host export for exactly the reason
/// `af_web_probe_seam_register` is one: it is keyed to a document this
/// project generates, and a player's build has no business knowing it
/// exists. `host/journal_probe.h` says why there is a synthetic document
/// at all — no real edition may ever be in this tree, so without one the
/// whole pipeline would have no check that runs anywhere but on a
/// maintainer's desk.
uint32_t af_web_journal_probe(int32_t on) {
  journal().probe = on != 0;
  return AF_OK;
}

const uint8_t* af_web_journal_probe_bytes(void) {
  return amberfolio::host::journal_probe_pdf().data();
}

uint32_t af_web_journal_probe_size(void) {
  return static_cast<uint32_t>(amberfolio::host::journal_probe_pdf().size());
}

/// The text the probe's fixture engine answers for entry `index`, so a
/// check can drive the JS side of the loop without an OCR engine at all
/// — which is what CI has.
uint32_t af_web_journal_probe_text(uint32_t index, char* out, uint32_t cap) {
  return hand_out(amberfolio::host::journal_probe_text(index), out, cap);
}

/// FNV-1a, 32 bits, over what entry `index` is *supposed* to look like —
/// `journal_probe_expected()`, generated from the same description the
/// document was generated from and not from the extractor's output.
///
/// It exists so the page's fixture engine can be as unfoolable as the
/// desktop's: a browser cannot compare two bitmaps across the ABI without
/// a second buffer and a second copy, and one number it can compare
/// against the pixels it was handed is the whole check
/// (`page/journal.mjs`). FNV-1a because that is the hash `tests/smoke.mjs`
/// already carries.
uint32_t af_web_journal_probe_hash(uint32_t index) {
  // Whichever the entry is (#212): the pixels the probe's own generator
  // produces for a decoded entry, or the bytes its own encoder wrote for
  // the one that goes through undecoded. One function, because the page
  // asks one question — "is this the scan entry `index` should have
  // been" — and it hashes whichever shape it was handed.
  const amberfolio::host::journal_bitmap want =
      amberfolio::host::journal_probe_expected(index);
  const std::span<const std::uint8_t> encoded =
      amberfolio::host::journal_probe_encoded(index);
  std::uint32_t hash = 0x811C9DC5U;
  for (const std::uint8_t byte :
       want.pixels.empty() ? std::span<const std::uint8_t>(encoded)
                           : std::span<const std::uint8_t>(want.pixels)) {
    hash ^= byte;
    hash *= 0x01000193U;
  }
  return hash;
}

double af_web_host_service_at(uint32_t which) {
  if (which >= amberfolio::machine::seam_host_service_count) {
    return 0.0;
  }
  return static_cast<double>(
      services()
          .record(static_cast<amberfolio::machine::seam_host_service>(which))
          .at);
}

}  // extern "C"

int main() {
  const uint32_t v = af_version();

  std::printf("amberfolio %u.%u.%u\n", AF_VERSION_MAJOR(v), AF_VERSION_MINOR(v),
              AF_VERSION_PATCH(v));
  // ASCII only: this goes to a browser console and to CI logs.
  std::printf("  wasm module - the machine ABI is here, the page is not.\n");
  std::printf("  demo program embedded: %u bytes.\n",
              af_web_demo_program_size());

  return 0;
}
