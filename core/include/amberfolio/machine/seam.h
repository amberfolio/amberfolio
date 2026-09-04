// SPDX-License-Identifier: AGPL-3.0-only
//
// Seams: the one mechanism by which anything other than the program's own
// instructions is allowed to touch this machine (PLAN.md §5). This is the
// engine M4-F2 (#96) asks for, grown out of the slice M3 landed (#119)
// rather than written beside it; every seam in this tree and every M5
// enhancement sits on the shape below and adds no mechanism of its own.
//
// A seam is an **opt-in runtime patch**: an identifier, a description, the
// binary fingerprints it applies to, and a set of interception points —
// CS:IP breakpoints, each qualified by the module it lives in, whose
// handlers are native C++ that reach into the emulated machine from
// outside. The bytes on the player's disk are never touched, nothing is
// injected into the emulated machine to execute there, and every seam is
// individually toggleable and **off by default**. With all seams off this
// file costs the machine one boolean test per step, and the machine is a
// plain one running an unmodified program.
//
//
// The fidelity boundary, as a test
// -------------------------------
//
// PLAN.md §4 says the seam engine is the only component that may alter
// the machine; #96 makes that a test rather than a sentence, and this
// header is written so the test can be stated in three lines:
//
//   * with every seam off, a run's machine-state hash (state.h, #100) is
//     the hash of the same run on a machine that was never asked about
//     seams at all — the engine is not consulted, so it cannot differ;
//   * a disabled seam's breakpoint is never consulted: `dispatch()` is
//     reached only when `armed()`, and only enabled seams arm points;
//   * seam state is configuration, not machine state: `reset()` clears
//     it, the serialization omits it, and a replay records it as an
//     initial condition (PLAN.md §4) rather than as something the machine
//     arrived at.
//
//
// What a handler is, and what it is handed
// ----------------------------------------
//
// A `seam_handler` is a plain function pointer — the same shape and the
// same reason as `cpu::handler` and `service_handler` (service_floor.h):
// core carries no `<functional>` and allocates nothing. Its whole world is
// the `machine&` it is handed and the `seam_context&` beside it.
//
// It runs **at a step boundary, before the instruction at CS:IP is
// fetched**, which is the same moment the BIOS callout runs and for the
// same reason: it is the only point where CS:IP is settled. A handler that
// wants to let the instruction happen simply returns; a handler that
// wants it not to happen moves IP.
//
// Where it may reach, and how — the **action primitives** #96 names:
//
//   * **Registers**, through `box.processor().regs()`. Plain state; a
//     handler edits it the way the code-wheel seam does.
//   * **Memory, as the program**, through `box.processor().read_byte()`
//     and `write_byte()` — through the bus, so a write into the video
//     window reaches the EGA's pipeline and a write into ROM is refused,
//     exactly as the program's own would be. Never `memory().ram()`: that
//     back door is for the machine's own writers (the loader, the BIOS
//     setup), and a seam is not the machine. It is the program's hand,
//     moved from outside.
//   * **Synthetic input**, through `seam_context::inject_keystroke()`. The
//     keystroke goes straight into the BIOS keystroke buffer at 40:1E —
//     the keyboard-service funnel — and not through `input_queue`: the
//     queue is the host's recordable stream (platform.h), and a seam's
//     keystrokes are a consequence of the seam set, which a replay records
//     as an initial condition. Nothing about how often the program polls
//     changes, because nothing is waited for: the key is simply there on
//     the next INT 16h, the way one a person typed a moment earlier would
//     be (PLAN.md §5, item 3 — the automap hotkey and the Encamp Fix both
//     want exactly this).
//   * **Control**, by moving IP. `seam_context::redirect()` spells it.
//   * **A call into the program**, through
//     `seam_context::call_program()` — the mechanism M5-D4 (#188) added,
//     and the one that lets a seam put text on the game's screen that
//     the *game* drew. See "Calling the program" below.
//   * **A host service**, through `seam_context::call_host()` — the
//     callout M5's journal and automap consume. Since M5-D1 (#169) both
//     hosts attach one (`hosts/common/host_services.h`), so a call
//     reaches an implementation that is handed this machine and reads it
//     at the moment of the call. A seam that calls out on a machine with
//     no host attached is still told so and still does nothing, which is
//     the answer a test with no host gets and the fail-closed direction
//     for a service nobody plugged in.
//
// It must not stop the machine. A seam is an enhancement above the
// fidelity boundary, and "the enhancement gave up" is not a machine state
// — a seam whose preconditions are not met stays inert and *says so*
// (PLAN.md §5's fail-closed rule), through `seam_event` on the diagnostics
// channel and through `status()` for a host that asks.
//
//
// Calling the program (#188)
// --------------------------
//
// Memory surgery puts pixels on the screen. It cannot make them look like
// the game's: the program draws text with **its own font**, a far pointer
// it keeps in its data segment, and this machine's font (font.h) is
// deliberately not that one — every glyph of it was drawn for this
// project, because shipping somebody else's bitmaps is what the
// clean-content rule forbids. A seam that rasterized its own glyphs would
// put visibly foreign lettering beside the game's on the same screen.
//
// So a seam that wants text on the game's screen **asks the game to draw
// it**. `call_program()` queues a call to one of the program's own
// routines; `place_bytes()` puts the arguments that are not numbers — a
// string — where the program can read them.
//
// It is a **batch**, and that is the design decision worth writing down.
// A handler is a plain function pointer with nowhere to keep "I am
// half-way through drawing a report", and a report is a frame and several
// lines. So a handler queues the whole sequence in one arrival and hands
// it over; the engine runs the calls one after another and the handler is
// never re-entered part-way. Nothing about which call is next lives
// outside the machine except the queue itself, which exists only while
// the batch does.
//
// What the engine does with a batch, in order:
//
//   1. **Snapshots the register file** — every word of it, IP and SP
//      included — at the moment the handler returns.
//   2. `place_bytes()` has already lowered SP over its bytes, so an
//      interrupt taken during the batch pushes *below* them and they are
//      the program's to read for as long as the batch lasts.
//   3. For each call: pushes its words in the order given (the first is
//      the deepest, which is the order the program's own callers push
//      in), then a far return address pointing at
//      `service::call_return_offset` — an address in the BIOS region that
//      is not a stub, is in no vector, and nothing ever executes — and
//      sets CS:IP to the routine.
//   4. Recognises that return address at a step boundary, and either sets
//      up the next call or **restores the snapshot**, at which point the
//      machine executes the instruction it was about to execute before
//      the handler ran.
//
// The program's routines are far and **clean their own arguments**
// (`retf N`), so the engine builds a frame and never tears one down.
// Everything the batch leaves behind is what the routines themselves did.
//
// Three properties this has to have, and does:
//
//   * **An interrupt during the batch is survivable**, because the frame
//     is one the program itself could have built, on the program's own
//     stack, and the machine's own INT 8 and INT 9 land below it.
//   * **A batch finishes even if the seam is switched off while it
//     runs.** You cannot un-call a call: the machine has a half-finished
//     frame on its stack and the only honest thing is to let it return.
//     `armed()` therefore stays true while a batch is outstanding.
//   * **It cannot hang the host.** A routine that never returns would
//     otherwise be an emulator that never comes back; the batch carries a
//     step budget, and exceeding it restores the snapshot and reports
//     `call_did_not_return` rather than waiting forever.
//
// A batch is *not* machine state: the snapshot and the queue are the
// engine's, `reset()` drops them, and the serialization never sees them.
// What is machine state is everything the batch did to the machine, which
// is the point of it.
//
//
// A seam's own few words (#189)
// -----------------------------
//
// Every seam in this tree until M5-E1b held **no state at all**, and that
// is the shape to reach for first: `seam_encamp_fix.cpp`'s three points
// know where they are in a sequence because they read it out of the
// machine — a field the program leaves in a known state, written with the
// value the seam actually wanted. A handler that remembers nothing cannot
// remember wrongly, and a run that ends mid-sequence leaves nothing behind
// to be stale.
//
// Some sequences cannot be read back. A command that says what it *did* —
// how much it restored, how much it spent — is comparing the machine now
// against the machine before, and "before" is not in the machine any more.
// So a seam may keep `scratch_words` words of its own, through
// `seam_context::scratch()` and `set_scratch()`.
//
// **It is configuration, exactly as the enable and the latch are**:
// `reset()` and `clear()` drop it, `enable()` starts it fresh, the
// serialization never sees it, and a run in which the seam is off never
// has any. It is not a back door for machine state — a seam that kept a
// *copy* of something the machine holds would have two of them, and the
// second one would be the wrong one the first time they disagreed. Keep
// in it only what the machine has stopped holding.
//
//
// The trigger: the host -> seam direction (#161)
// ----------------------------------------------
//
// Everything above is a seam acting on its own account: its point is
// reached, its handler runs, and the only thing a host decided was
// whether the seam was on at all. `call_host()` is the seam -> host
// direction. There was no host -> seam direction, and a cheat wants one:
// a debug cheat that fires on every visit to its point decides every
// fight from the moment it is switched on, which is a setting and not a
// cheat. A person wants to *pull* it.
//
// So a definition may say `trigger = true`, and then:
//
//   * `seam_engine::pull(id, now)` sets a **one-shot latch** on that
//     seam. It is refused, with a reason, for a seam that is off or that
//     does not take a trigger — a latch quietly waiting on a seam nobody
//     turned on is the silence #131 is about.
//   * the next time one of that seam's points is reached with the latch
//     set, the handler runs and the latch clears. One pull, one run.
//   * with the latch unset the point is reached and **nothing happens**:
//     the address is compared, `reached` counts the arrival, and the
//     handler is not called.
//
// The latch is *configuration*, exactly as the enable is: `reset()` and
// `clear()` drop it, the serialization never sees it, `enable()` starts
// it fresh, and a run in which nobody pulls is byte-for-byte the run the
// same program has with the seam off. A replay records a pull the way it
// records a keystroke — as an event with a tick (`replay.h`) — because it
// is one: something a person did to a running machine at a moment.
//
// **A trigger cannot mean "at this instant", and this file will not
// pretend it does.** A seam acts at a step boundary because that is the
// whole mechanism (PLAN.md §5): there is no such thing as acting
// half-way through an instruction, and editing a structure half-way
// through the routine that is walking it is precisely the corruption the
// breakpoint discipline exists to prevent. So the honest latency of a
// pull is "at the next step boundary at which acting is *safe*", and the
// question is only ever what a point knows about safety.
//
// An **address point** knows it from the address: a known instruction in
// known code is a place where the structures a handler edits are known
// not to be half-way through being edited. It pays for that with
// latency, which is "how often does the program go there" — a fact about
// the program, and one nobody in this tree has measured. For a
// once-a-round end check that is a round, and a person who pulls a cheat
// mid-fight wants it now.
//
// So a point may say `at_every_step` and have no address at all (#163).
// It is offered at every step boundary while the latch is set, and it
// has to buy its safety back from the machine: its handler opens with a
// guard it can defend and `decline()`s — keeping the latch — until the
// guard holds. That trades a fact about *where the program is* for a
// fact about *what the program's own structures say*, which is a weaker
// kind of evidence and has to be written down as such wherever it is
// used. `seam_point::at_every_step` and `seam_cheats.cpp` do that.
//
// Hence `seam_status::reached`, and the two numbers beside it. `reached`
// counts every arrival at an armed **address** point whether or not a
// handler ran, so `reached` minus `fired` is the chances a trigger had
// and did not need, and the *rate* of `reached` over a run is the
// granularity a pull can possibly be served at by that point. A point
// with no address has no arrivals to count — it is offered at every step
// — so it counts none, and leaves `reached` meaning what it has always
// meant. `pulled_at` and `waited` are the same question answered in
// ticks: when the outstanding pull was made, and how long the last
// served one waited. That is the instrument the question needs; the
// answer for any particular point is a measurement, not a claim this
// header can make.
//
//
// Qualified points
// ----------------
//
// A point is a module and an offset in it (`seam_point`). For the resident
// image — the program the loader placed — the module is `resident_image`
// and the offset is from the image segment, because where DOS puts a
// program is the loader's business and a seam's facts are about the
// program (PLAN.md §5: "a database of addresses and offsets"). For
// overlaid code the module names the read that loads it — which file,
// which offset, how many bytes, optionally which digest (overlay.h) — and
// the engine arms the point only while the tracker says that read is
// resident, at the address it landed. A module that is not resident
// leaves the seam enabled but inert, with `seam_reason::module_not_resident`
// for anyone who asks why.
//
// **Where the program says where a module is, that is what is used.**
// An overlay manager that shuffles modules around an arena has to keep
// a note of where each one went, and that note lives in the resident
// image, which does not move. `seam_module::load_segment_at` is the
// offset of that word, and a point qualified by it is resolved
// *per step, from the machine* — the word is read at the step boundary
// and the point's address is that segment plus its offset — rather than
// computed once at arming. That is the difference #131 was filed for:
// an address computed at arming is a claim about the fact table and goes
// stale the moment the manager moves the module, and a seam armed at a
// stale address reports `armed`, fires nothing, and reads exactly like
// one that works. An address resolved from the program's own record is a
// claim about the machine, and there is no window in which it can be
// wrong. Zero in that word means the module is not loaded, and the point
// simply does not match — the same inert-and-honest answer, arrived at
// one layer lower.
//
// The tracker's reading is still the fallback, and still what a module
// with no such record gets. `docs/seams.md` §"Where a point lives" has
// the choice written out.
//
// Every definition carries the schema version it was written against.
// The engine refuses one written against another (`schema_mismatch`), so
// a change to what a module or a point means cannot silently re-target a
// seam written before it (PLAN.md §5: "the seam schema ... versioned").
//
//
// The cost when it is off
// -----------------------
//
// `machine::step()` tests one `bool`. Nothing is scanned, nothing is
// hashed, no address is compared: `armed()` is false and the branch is
// not taken. When a seam *is* on, the check is a linear scan of at most
// `max_points` physical addresses, which is a handful of compares on a
// path that is already an interpreted instruction away from anything
// that matters — the same argument port_map.h makes for scanning its
// claims.
//
// A point resolved from the program's load-segment word costs one more
// thing on that scan: a two-byte read of RAM. It is behind a test that
// throws away fifteen steps in sixteen for free — a load segment is a
// paragraph, so a point can only be *here* if `at` and the point's
// offset agree in their low four bits — and it happens only for a seam
// that is on and only for the points that name such a word.
//
// A point with no address (`seam_point::at_every_step`) costs one bool
// on that scan — its seam's latch — and everything past it happens only
// while somebody's pull is outstanding. That is the whole of what it
// costs a run in which nobody pulled, and it is the same bool the
// address points already test one line later, so an enabled-and-unpulled
// trigger is exactly the machine it was before #163. A run with a pull
// outstanding pays a guard per step until the guard holds, by design:
// the guard is what the point has instead of an address.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/diagnostics.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/overlay.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"

namespace amberfolio::machine {

class machine;
class seam_context;
class seam_engine;
class filesystem;
struct edition;

/// The shape a seam definition is written in. Bump when `seam_definition`,
/// `seam_point` or `seam_module` change meaning; a definition that names
/// another version is refused rather than misread.
///
/// 2: a module may name the word the program keeps its load segment in
/// (`seam_module::load_segment_at`, overlay.h), and a point in such a
/// module is resolved from that word at every step instead of from the
/// tracker's record of a read (#131).
///
/// 3: a definition may say `trigger`, and then its handlers run only
/// when a host has pulled its latch (`seam_engine::pull`, #161). A
/// definition written before this version means "run at every arrival",
/// which is what every seam meant then — but the field decides whether a
/// point does anything at all, so a stale definition is refused rather
/// than read as one or the other.
///
/// 4: a point may say `at_every_step`, and then it has no address at all
/// — it is offered at every step boundary while its seam's latch is set,
/// and its handler decides from the machine whether acting is safe
/// (#163). A definition written before this version means "an address
/// point", which is what every point meant then; the field decides
/// *where* a handler runs, which is the one thing about a seam that must
/// never be inferred.
///
/// 5: a definition may name a **document gate** (`seam_definition::gate`,
/// #171), and then it is inert until the player has presented the
/// document it names. A definition written before this version means "no
/// gate", which is what every seam meant then — and that is exactly why
/// the version moves: a stale definition read as ungated would be a
/// possession gate silently not applied, which is the one failure a gate
/// has (PLAN.md §5).
inline constexpr std::uint16_t seam_schema_version = 5;

/// What runs when execution reaches an armed interception point. Native
/// C++, called from outside the emulated machine — see this file's top
/// comment for what it may do and how.
using seam_handler = void (*)(machine& box, seam_context& ctx);

/// One interception point: which module, where in it, and what to do
/// there. `offset` is from the module's base — the image segment for the
/// resident image, the address the read landed at for an overlay.
struct seam_point {
  seam_module module{};
  std::uint32_t offset{};
  seam_handler run{nullptr};

  /// This point has **no address**: while its seam's latch is set, it is
  /// offered at every step boundary, and `offset` means nothing (#163).
  ///
  /// Only for a `trigger` definition, and only ever consulted while that
  /// seam is *waiting*: a point like this on a seam nobody pulled costs
  /// exactly the one bool the latch is, which is what keeps an unpulled
  /// run byte-identical to the run with the seam off (§"The fidelity
  /// boundary"). On a definition that is not a trigger nothing can ever
  /// set the latch, so such a point never runs at all — refused as a
  /// definition would be too strong, since it is a mistake in this tree
  /// rather than something a caller can hit, and `all_seams()` is
  /// checked for it in the unit suite.
  ///
  /// **The address is what a seam usually gets its safety from**, and a
  /// point without one gives that up: the whole reason the mechanism is
  /// a CS:IP breakpoint is that a known instruction boundary in known
  /// code is a place where the structures the handler edits are known
  /// not to be half-way through being edited by the program. A point
  /// with no address has to buy that back from the machine itself,
  /// which means its handler must open with a guard it can defend and
  /// `decline()` — keeping the latch — whenever the guard does not hold.
  /// `seam_cheats.cpp` is the worked example and says what its guard can
  /// and cannot rule out.
  ///
  /// What it buys is latency. "At the next arrival at the point" is as
  /// good as an address point can do, and for a once-a-round point that
  /// is a round; a person who pulls a cheat mid-fight wants it now. This
  /// is the honest reading of "now" at a step boundary: the first step
  /// at which acting is provably safe.
  bool at_every_step{false};
};

/// A seam, as a fact table: what it is, what it applies to, and where it
/// intercepts.
struct seam_definition {
  /// The config key and the name a host's `--seam` takes. Kebab-case.
  std::string_view id;

  /// One line, for a listing.
  std::string_view about;

  /// The SHA-256s of the program images this seam's addresses are facts
  /// about, each as 64 lowercase hex characters. A seam is unavailable
  /// against any other binary — PLAN.md §5's per-binary rule, which is
  /// what keeps a set of addresses from being applied to something they
  /// do not describe.
  std::span<const std::string_view> fingerprints;

  std::span<const seam_point> points;

  /// Whether this seam is **pulled** rather than left on: its handlers
  /// run only when a host has set its latch (`seam_engine::pull`, #161),
  /// once per pull, and its points are reached and do nothing the rest
  /// of the time.
  ///
  /// False — run at every arrival — is what every seam meant before
  /// schema 3 and what most of them still want: the code-wheel seam
  /// answers a challenge whenever the challenge is asked, and
  /// invulnerability is a property of a party rather than an act. A
  /// cheat that *ends a fight* is an act, and this is the field that
  /// says so.
  bool trigger{false};

  /// The document the player must hold for this seam to do anything
  /// (PLAN.md §5, #171), or `document_kind::none` for a seam that is not
  /// gated. **No seam in this build names one.** The code-wheel bypass
  /// did, on the wheel itself (#115), until the releases sold today
  /// turned out to ship a code generator application rather than a PDF
  /// of it (#290): what it waits for now is a person answering the
  /// program's own challenge, once (#291, seam_code_wheel.cpp). The
  /// mechanism stays because the argument for it did — the journal's own
  /// gate is a field away, and a table with a row and no user is a table
  /// that still describes what a gate is.
  ///
  /// **A possession gate: it demonstrates the player holds the document,
  /// no more.** PLAN.md §5's sentence, and the whole of what this field
  /// does. It is one more condition on `armed()`, in the same place
  /// "is the module resident" is, so an unsatisfied gate is a seam that
  /// is *on and inert with a reason* rather than one that was refused —
  /// the same shape a seam waiting for its overlay has, because it is
  /// the same situation: the seam took, and something it needs is not
  /// here yet.
  ///
  /// Fail-closed by construction and by nothing else. There is no code
  /// path that arms a gated seam without a satisfied gate, because the
  /// gate is tested where residency is tested and both answers are
  /// computed by the one function `status()` and `arm_all()` share
  /// (`modules_resident`'s own argument, applied again).
  document_kind gate{document_kind::none};

  /// The schema this definition was written against — `seam_schema_version`
  /// at the time. Spelled in the definition rather than assumed, so the
  /// engine can refuse a stale one.
  std::uint16_t schema{seam_schema_version};
};

/// Every seam this build carries. Registered into every engine at
/// construction; a host or a test may add more (`seam_engine::add`).
[[nodiscard]] std::span<const seam_definition> all_seams();

/// Why `seam_engine::enable()` or `disable()` refused, or why an enabled
/// seam is not armed.
enum class seam_reason : std::uint8_t {
  none,
  /// No seam has that id.
  unknown_seam,
  /// No program has been loaded yet, so there is nothing to key on and
  /// no image segment to place the points against.
  no_program,
  /// The program loaded is not one this seam's addresses describe.
  wrong_binary,
  /// The definition was written against another schema version.
  schema_mismatch,
  /// The seam is on, but the module one of its points lives in is not
  /// resident, so nothing is armed (overlay.h).
  module_not_resident,
  /// The seam is on, and the document it is gated on has not been
  /// presented (`seam_definition::gate`, document.h, #171). Nothing is
  /// armed and nothing was refused: the seam took, and the player has
  /// not shown the thing PLAN.md §5 requires them to hold.
  ///
  /// Its own reason rather than `module_not_resident`'s, because the two
  /// are answered by different people. A module arrives when the program
  /// loads it; a document arrives when a person presents one, and a host
  /// that could not tell them apart would be telling a player to wait
  /// for the game.
  document_not_presented,
  /// A point fired, and what the machine held there is not what the
  /// seam's facts describe — a stack frame whose argument is not the
  /// pointer it is supposed to be, a record where there is no record.
  /// The handler did nothing and said so.
  ///
  /// The one reason a *running* seam produces, and the reason the
  /// fail-closed rule needs most. Every other one here is answered
  /// before a single instruction is intercepted; this one is what keeps
  /// a seam whose address turned out to be wrong from writing a word
  /// into whatever happens to be at that offset instead. A cheat that
  /// silently corrupted a frame would show up as a wrong number on a
  /// character sheet, three layers from its cause.
  point_not_recognized,
  /// A seam called into the program and the call did not come back
  /// inside the batch's step budget (#188). The engine put the machine
  /// back the way it found it and said so, because the alternative is an
  /// emulator that never returns — and a fact table that named the wrong
  /// address is exactly how that would happen.
  call_did_not_return,
  /// More points than the engine has room for. A build-time mistake, not
  /// something a caller can recover from.
  too_many_points,
  /// The registry is full. The same kind of mistake.
  no_room,
  /// A trigger was pulled on a seam that does not take one (#161). Its
  /// handlers run at every arrival at its points and there is nothing to
  /// latch; answering "done" would be a lie a host would then show.
  not_triggered,
  /// A trigger was pulled on a seam that is off. Refused rather than
  /// remembered: a latch waiting on a seam nobody turned on would fire
  /// at some unrelated later moment, which is exactly the "it did
  /// something and I do not know when" this trigger exists to remove.
  not_enabled,
};

/// Kept as its own name because `enable()` has always answered one and
/// every host prints it: the reasons a toggle can be refused are the
/// `seam_reason`s, and this is the same enumeration under the name the
/// toggle surface uses.
using seam_error = seam_reason;

/// Where a seam stands, as a host shows it: off, on, or unavailable —
/// PLAN.md §5's "an unrecognized binary runs with no seams available".
enum class seam_state : std::uint8_t {
  off,
  on,
  unavailable,
};

/// One row of a listing (M4-F4, #98).
struct seam_status {
  std::string_view id;
  std::string_view about;
  seam_state state{seam_state::unavailable};
  /// Why it is unavailable, or why an enabled seam is not armed. `none`
  /// for a seam that is off, or on and armed.
  seam_reason reason{seam_reason::none};
  /// Whether any of its points is armed right now. On-and-unarmed is a
  /// seam waiting for its module.
  bool armed{false};

  /// How many times one of its handlers has actually **acted** since it
  /// was enabled — run, and not declined.
  ///
  /// A decline does not count (#163). It used to, and that was the one
  /// reading of this number that defeated its own purpose: a handler
  /// that arrives at a point which is not the point and says so is
  /// precisely the failure below, and a `fired` that went up for it made
  /// the failure look like success. A point with no address is offered
  /// at every step and declines at most of them, which is what turned an
  /// arguable choice into a wrong one.
  ///
  /// Here because `armed` turned out to be a claim about the *fact
  /// table* rather than about the machine (#131): a point is armed at an
  /// address computed from where a module was recorded, and a seam whose
  /// module has since moved — or whose offset was never right — reports
  /// `armed`, fires nothing, and is indistinguishable from one that
  /// works. It cost this milestone three wrong claims before a
  /// with-and-without comparison caught it.
  ///
  /// A count cannot make a wrong address right. What it does is make the
  /// wrongness *visible*, which is the half of fail-closed that was
  /// missing: `on armed fired=0` after a run that should have fired is a
  /// defect a reader can see, where silence is not.
  ///
  /// Bookkeeping, not machine state: above the fidelity boundary, never
  /// serialized, and it can only move when a seam is on — so a run with
  /// everything off is the run it always was.
  std::uint64_t fired{};

  /// Whether this seam is pulled rather than left on
  /// (`seam_definition::trigger`). A host shows a trigger-driven seam a
  /// button and not only a switch, because they are different things.
  bool trigger{false};

  /// A pull is outstanding: somebody asked, and the point has not been
  /// reached since.
  ///
  /// The state that turns "the button did nothing" into "the button is
  /// armed and the program has not been there yet", which is #131's
  /// lesson one layer up: the failure that reads as success is the one
  /// with nothing to show.
  bool waiting{false};

  /// How many times one of this seam's armed points was **reached** —
  /// the address matched at a step boundary — whether or not a handler
  /// ran there.
  ///
  /// For an ordinary seam this is `fired` (every arrival runs the
  /// handler). For a triggered one the difference is the whole point:
  /// `reached - fired` is the arrivals nobody had asked for, and the
  /// rate of `reached` over a run is the granularity at which a pull can
  /// possibly be served. Nobody has measured that for the cheats' end
  /// check, and this is the instrument that measures it.
  ///
  /// **Address points only**, and deliberately so (#163). A point with
  /// no address (`seam_point::at_every_step`) is offered at every step
  /// boundary while a pull is outstanding, so counting its offers would
  /// be counting steps, and counting steps is not a measurement of
  /// anything.
  ///
  /// Keeping it address-only is what makes it still worth having, and
  /// there is a measurement to show that. Driven against the real
  /// program, the cheats' end check is arrived at **exactly once per
  /// encounter**: a pull made before a round began used to cost
  /// `waited=22110288` — 18.5 virtual seconds — and now costs
  /// `waited=8327644` when the guard has to wait for the roster and
  /// `waited=0` when it does not. `reached=1` is the number that makes
  /// those three comparable, and a `reached` that also counted every
  /// step of the wait would have destroyed it.
  ///
  /// What it must *not* be read as is "this seam did nothing". A seam
  /// that acts at an address-free point reports `fired=1 reached=0`, and
  /// that pair is a success. `seam_reading_of()` is what a host says out
  /// loud, precisely so no host has to re-derive it.
  std::uint64_t reached{};

  /// How many times one of this seam's handlers **declined** since it
  /// was enabled (`seam_context::decline`) — arrived, found what the
  /// machine held was not what its facts describe, and touched nothing.
  ///
  /// The diagnostics channel says this once per enable, because a point
  /// in a tight loop would otherwise bury the first line. A count is the
  /// same fact in the form a status row can carry, and it is what tells
  /// "pulled, and the program has not been there yet" apart from
  /// "pulled, and every offer was refused" — which look identical
  /// through `waiting` alone and mean opposite things to whoever is
  /// waiting for the cheat to go off (#163).
  std::uint64_t declined{};

  /// Whether any of this seam's points **has an address**, as opposed to
  /// being offered at every step boundary (`seam_point::at_every_step`).
  ///
  /// Only here because one sentence a host says depends on it: "armed
  /// and never reached; its point may not be where its facts say" is a
  /// claim about an address table, and a seam with no addresses in it
  /// cannot have a wrong one. Saying that of a seam nobody pulled would
  /// send a reader to doubt a table that is not there.
  bool addressed{};

  /// The virtual tick the outstanding pull was made at. Meaningful only
  /// while `waiting`; a host showing how long a pull has been waiting
  /// subtracts it from `machine::time()`.
  ticks pulled_at{};

  /// How long the last **served** pull waited, in ticks: from the pull
  /// to the arrival that ran the handler. Zero until one has been
  /// served. The latency of the trigger, in the units the machine
  /// measures everything else in.
  ticks waited{};
};

/// What a seam's row *means*, as one of six sentences a person can read
/// at a glance (#163).
///
/// Here rather than in a host — the same argument
/// `seam_event_kind_name` makes one screen down, and it was not enough:
/// both hosts spelled this decision out for themselves, and when a seam
/// gained a point with no address they both started printing "armed and
/// never reached; its point may not be where its facts say" over a run
/// in which the seam had done exactly what it was asked. `fired=1` and
/// "never reached" cannot both be true, and the sentence sent readers to
/// doubt an address table that was working. That is #131's harm with the
/// sign flipped — a line that reads as a failure when the thing worked —
/// so the decision lives in one place, is tested in one place, and
/// reaches the browser through the ABI as text rather than as logic to
/// re-derive.
///
/// The question it answers is the one a person actually has: **did it
/// act, and if not, why not.**
enum class seam_reading : std::uint8_t {
  /// The numbers on the row say everything there is to say: an ordinary
  /// seam that fired, or a seam that is inert and already says so.
  nothing_to_say,
  /// A trigger was pulled and served. `waited` says what it cost.
  served,
  /// A pull is outstanding, and nothing has been offered to the handler
  /// since — the program has not been where this seam is.
  pulled_and_not_served,
  /// A pull is outstanding, and what *was* offered got declined. The
  /// seam is working, is being asked, and is refusing to act on a
  /// machine it does not recognize — which is the fail-closed rule doing
  /// its job, and is a different thing to be waiting on.
  pulled_and_declined,
  /// A trigger's point was arrived at and nobody had asked. `fired=0`
  /// means nothing about such a seam.
  reached_and_never_pulled,
  /// #131's warning: armed at an address the program never went to, so
  /// the address may not be where the fact table says. Only ever said of
  /// a seam that *has* an address and did not act.
  never_reached,
};

/// Which of those `row` is. A pure function of a status row, so a host
/// that shows a listing and a host that prints one at the end of a run
/// cannot disagree.
[[nodiscard]] seam_reading seam_reading_of(const seam_status& row) noexcept;

/// The sentence, ready to append to a row — leading " - " and all, or
/// empty for `nothing_to_say`. Never null.
[[nodiscard]] const char* seam_reading_text(seam_reading reading) noexcept;

/// The printable name of a `seam_reason`, for a host's listing. Never
/// null.
[[nodiscard]] const char* seam_reason_name(seam_reason reason) noexcept;

/// The printable name of a `seam_state`. Never null.
[[nodiscard]] const char* seam_state_name(seam_state state) noexcept;

/// Something the engine did or declined, reported through the diagnostics
/// channel: a seam went on or off, armed or disarmed, or was refused. The
/// "reports why" half of PLAN.md §5's fail-closed rule, in the same
/// channel as a notice so a boot log carries it.
enum class seam_event_kind : std::uint8_t {
  enabled,
  disabled,
  armed,
  /// Enabled but not armed — the module is not resident (`reason` says).
  inert,
  refused,
  /// A trigger's latch was set: somebody pulled it (#161).
  pulled,
  /// And consumed: the point was reached with the latch set, and the
  /// handler ran. Two events rather than one because the gap between
  /// them is the thing nobody has measured — a boot log that carries
  /// both carries the latency.
  served,
};

struct seam_event {
  std::string_view id;
  seam_event_kind kind{};
  seam_reason reason{seam_reason::none};
};

/// The printable name of a `seam_event_kind` — `on`, `off`, `armed`,
/// `inert`, `refused`. Never null.
///
/// The word rather than the enumerator's own spelling, unlike the two
/// names above, because this one is the middle of a sentence a person
/// reads at a glance in a boot log: `seam cheat-kill-all inert
/// module_not_resident`. Here rather than in a host so that both hosts
/// say it identically (machine/report.h says why that matters).
[[nodiscard]] const char* seam_event_kind_name(seam_event_kind kind) noexcept;

/// The host services a seam may call out to — the slot M4 defined and
/// M5-D1 (#169) filled (PLAN.md §5 items 2 and 3; #96).
///
/// **Two, since #169, and there were three.** `save_state_changed` was
/// the callout save & roster management would have made, and that
/// enhancement was withdrawn from the plan on 2026-08-24 (#176) by
/// decision rather than deferral — it was the one v1 item that was not
/// an in-game enhancement. A name in this enumeration is a promise that
/// something calls it, and a service with no seam is a surface built on
/// spec: exactly what PLAN.md §3's rule refuses one layer down. So it is
/// gone, and this paragraph is what it left behind. Whatever M6 wants
/// for persistence adds the name it actually needs.
enum class seam_host_service : std::uint8_t {
  /// The journal reader: open the entry the argument names (#175).
  journal_open,
  /// The automap: the party moved or the map changed; the argument is
  /// whatever the seam chose to encode. Two seams call it — the automap
  /// panel (#173) and the explored overlay (#179) — because they share
  /// one exploration state.
  automap_update,
  /// The journal's log changed: the game cited something, or the player
  /// opened something it had cited (M5-E4b, #222). The argument carries
  /// nothing — what changed is in `machine::journal()`'s own log, which
  /// is observation and not machine state, the same arrangement
  /// `automap_update` has and for the same reason.
  journal_seen,
  /// The code-wheel challenge has just been answered — by a person, at
  /// the program's own prompt, correctly (M6-C1a, #291). The argument
  /// carries nothing, for `journal_seen`'s reason: what changed is the
  /// engine's own latch, and a host that wants to know reads it.
  ///
  /// Called **once**, on the transition, so a host writes its file once
  /// and not once per attempt.
  code_wheel_answered,
};

/// How many there are, for an array indexed by one. Not an enumerator:
/// a `count` in the enumeration would be a value `serve()` could be
/// handed, and it is not a service.
inline constexpr std::size_t seam_host_service_count = 4;

/// The printable name of a `seam_host_service` — `journal-open`,
/// `automap-update`, `journal-seen`, `code-wheel-answered`. Never null.
///
/// Kebab-case, and here rather than in a host, for the reason
/// `seam_event_kind_name` next door gives: both hosts print this in
/// their end-of-run seam line, and a reader comparing a browser run
/// with a desktop one should be comparing two runs and not two
/// spellings.
[[nodiscard]] const char* seam_host_service_name(
    seam_host_service which) noexcept;

/// What a host plugs in to answer those calls. Attached with
/// `seam_engine::set_host()`; a plain interface in the shape of
/// `diagnostics` and `filesystem`: held by reference, never owned.
///
/// **It is C++ running inside the module on both targets**, and not a
/// queue a page drains later (#169). The reason is in `serve()`'s own
/// contract: it is handed the machine, and what it reads there is only
/// true at the moment of the call. The automap wants the party's
/// position *now*, not when a page next gets a turn — by then the
/// program has moved on, and a service that read the machine late would
/// be answering a different question and could not say so.
class seam_host_services {
 public:
  seam_host_services() = default;
  seam_host_services(const seam_host_services&) = delete;
  seam_host_services(seam_host_services&&) = delete;
  seam_host_services& operator=(const seam_host_services&) = delete;
  seam_host_services& operator=(seam_host_services&&) = delete;

  /// A seam asked for `which`, with `argument`. The host does what the
  /// service means — reads machine state, shows something, remembers
  /// something — and must not write machine state: that is the seam's
  /// job, through the machine, and the host's reading of the machine is
  /// exactly what PLAN.md §4 allows it.
  virtual void serve(machine& box, seam_host_service which,
                     std::uint32_t argument) = 0;

 protected:
  ~seam_host_services() = default;
};

/// What a handler is handed beside the machine: where it is, and the
/// primitives that need the engine behind them.
class seam_context {
 public:
  /// The seam whose point fired.
  [[nodiscard]] std::string_view seam_id() const noexcept { return id_; }

  /// The physical address the point was armed at — the CS:IP the handler
  /// is running before.
  [[nodiscard]] std::uint32_t at() const noexcept { return at_; }

  /// Where the point's module begins, as a physical address: the image
  /// base for the resident image, the read's landing address for an
  /// overlay. A handler that reads a fact-table offset against the
  /// module adds this.
  [[nodiscard]] std::uint32_t module_base() const noexcept {
    return module_base_;
  }

  /// Where the loader put the program, as a physical address — the base
  /// every resident-image offset is relative to (seam_point). The same
  /// arithmetic the engine does to arm a resident point.
  [[nodiscard]] std::uint32_t image_base() const noexcept {
    return image_base_;
  }

  /// Put a keystroke in the BIOS buffer, as INT 16h will hand it back: the
  /// scan code in AH, the character (or 0) in AL. See this file's top
  /// comment for why the buffer and not the input queue. False if the
  /// buffer is full — the same answer a typed key gets.
  bool inject_keystroke(std::uint8_t scancode, std::uint8_t ascii);

  /// Continue at `cs:ip` instead of at the instruction the point is on.
  void redirect(std::uint16_t cs, std::uint16_t ip);

  /// Ask the host for `which`. False, and nothing happened, if no host
  /// service is attached — the seam stays inert rather than guessing.
  bool call_host(seam_host_service which, std::uint32_t argument);

  /// Put `bytes` where the program can read them for the length of this
  /// batch of calls, and answer the far pointer that names them (#188).
  ///
  /// They go on the machine's own stack, below what the program is using
  /// and above where an interrupt taken during the batch would push, and
  /// they are gone when the batch ends. That is what a Pascal string a
  /// seam invented needs and what it must not have: somewhere the
  /// program can read it from, and nowhere permanent.
  ///
  /// False, and nothing written, if the batch has no room left or the
  /// stack has not got the space — a seam that cannot say what it wanted
  /// to say declines, like any other unmet precondition.
  bool place_bytes(std::span<const std::uint8_t> bytes, std::uint16_t& segment,
                   std::uint16_t& offset);

  /// Queue a call to the program's own routine at `segment:offset`, with
  /// `words` pushed in the order given — the first the deepest, which is
  /// the order the program's own callers push in (#188).
  ///
  /// The call does not happen here. It happens after this handler
  /// returns, with the others queued beside it, and this handler is not
  /// re-entered: see "Calling the program" at the top of this file for
  /// why a batch rather than a call.
  ///
  /// False, and nothing queued, if the batch is full or `words` is longer
  /// than a call may be.
  bool call_program(std::uint16_t segment, std::uint16_t offset,
                    std::span<const std::uint16_t> words);

  /// One of this seam's own words (#189). Zero until something is put
  /// there, and zero again after `enable()`. `slot` past the end reads
  /// zero and writes nothing.
  [[nodiscard]] std::uint16_t scratch(unsigned slot) const noexcept;
  void set_scratch(unsigned slot, std::uint16_t value) noexcept;

  /// "I was reached, and what is here is not what my facts describe."
  ///
  /// A handler calls this instead of acting when a precondition it can
  /// check does not hold, and then returns without touching the machine.
  /// Reported once per seam per enable — a point in a tight loop would
  /// otherwise produce a line per iteration and bury the first one, the
  /// same argument diagnostics.h makes for a notice's first touch.
  ///
  /// For a triggered seam it also **keeps the latch** (#161): a pull
  /// that arrived at a point which was not the point is not a pull that
  /// was served, and swallowing it would answer a person's request with
  /// nothing at all.
  void decline(seam_reason why);

 private:
  friend class seam_engine;
  seam_context(machine& box, seam_engine& engine, std::string_view id,
               std::uint32_t at, std::uint32_t module_base,
               std::uint32_t image_base) noexcept
      : box_(&box),
        engine_(&engine),
        id_(id),
        at_(at),
        module_base_(module_base),
        image_base_(image_base) {}

  machine* box_;
  seam_engine* engine_;
  std::string_view id_;
  std::uint32_t at_;
  std::uint32_t module_base_;
  std::uint32_t image_base_;
  /// Whether `decline()` was called during this one visit. Read by
  /// `seam_engine::dispatch()` to decide whether a trigger's latch was
  /// spent.
  bool declined_{false};
};

/// The registry, the toggles, and the armed interception points.
///
/// Owned by the machine, like `dos_services` and for the same reason: a
/// handler is a plain function pointer with nowhere to keep state, so its
/// world is what `machine` hands out.
class seam_engine {
 public:
  /// Definitions the registry holds. The v1 seam set is six (PLAN.md §5)
  /// plus the cheats' two, and a test registers a dozen-odd of its own
  /// beside them; twenty-four leaves room for the fast-follow fixes
  /// without making this a data structure.
  ///
  /// It was sixteen, which the seam suite's own set reached exactly when
  /// M5-D1 added a thirteenth (#169), and M5-D3 two more behind it
  /// (#171) — and a registry that is exactly full fails by *refusing the
  /// next definition*, which in a test rig
  /// is a seam quietly missing rather than a build that stops. The
  /// headroom is not for the seams this build carries; it is so that
  /// adding one is never that.
  static constexpr std::size_t max_seams = 24;

  /// Points armed at once, across every enabled seam.
  static constexpr std::size_t max_points = 32;

  /// What a batch of calls into the program may hold (#188). A report is
  /// a framed box and a handful of lines, so twelve calls of eight words
  /// each is that with room over; the byte arena is the strings those
  /// lines name, and a screen is forty columns.
  static constexpr std::size_t max_calls = 12;
  static constexpr std::size_t max_call_words = 8;
  static constexpr std::size_t max_call_bytes = 256;

  /// Words of its own a seam may keep between arrivals (#189). Eight is
  /// what the Encamp Fix's report needs — what it spent, what it
  /// restored, when it started — and small enough that a seam reaching
  /// for a ninth is a seam that should be reading the machine instead.
  static constexpr unsigned scratch_words = 8;

  /// How many step boundaries a batch may take before the engine decides
  /// the call is not coming back.
  ///
  /// **A bound against *never*, not against slow**, and the difference
  /// cost a day. The first cut said a quarter of a million, reasoning
  /// that drawing a box and six lines is a few thousand instructions.
  /// Then a seam called the program's own cast driver, which repaints
  /// the screen it is on — and a repaint reads art off the disk. The
  /// call was abandoned every time, the machine was put back every time,
  /// and the seam looked broken when the engine was the thing that was
  /// wrong. Measured afterwards, that one call costs between one and two
  /// million steps.
  ///
  /// So: sixteen million, an order of magnitude past the worst thing
  /// anything here has asked for, and still only some seconds of virtual
  /// time — far inside how long a person would wait before deciding the
  /// emulator had stopped.
  static constexpr std::uint32_t max_call_steps = 16'000'000;

  /// `log` may be null; the engine does the same thing either way, and
  /// `machine` hands it the sink it was built with.
  explicit seam_engine(diagnostics* log = nullptr) noexcept;

  // --- The registry ------------------------------------------------------

  /// Register `seam`. Every definition in `all_seams()` is registered by
  /// the constructor; this is the door for a host's or a test's own.
  /// `seam` must outlive the engine. False, and nothing registered, if the
  /// registry is full or the id is already taken.
  bool add(const seam_definition& seam) noexcept;

  [[nodiscard]] std::size_t count() const noexcept { return registered_; }

  /// Where `id` stands — off, on, unavailable, and why. `index` is a
  /// position in the registry, `count()` of them.
  [[nodiscard]] seam_status status(std::size_t index) const noexcept;
  [[nodiscard]] seam_status status(std::string_view id) const noexcept;

  /// The definition behind `id`, or null.
  [[nodiscard]] const seam_definition* find(std::string_view id) const noexcept;

  // --- The program ------------------------------------------------------

  /// Tell the engine what program is running: the digest of its image and
  /// the segment it was loaded at. A host calls this once after
  /// `load_program()`; nothing can be enabled before it.
  ///
  /// Clears everything already enabled, because a different program makes
  /// every armed address meaningless.
  void loaded(const sha256_digest& digest, std::uint16_t image_segment);

  /// `loaded()`, with the digest taken from `path` on `fs` — the
  /// fingerprint a host would otherwise compute itself. False, and the
  /// engine left knowing no program, if the file could not be read.
  bool identify(filesystem& fs, const dos_path& path,
                std::uint16_t image_segment);

  /// Whether a program is known, and which.
  [[nodiscard]] bool have_program() const noexcept { return have_program_; }
  [[nodiscard]] const sha256_digest& program() const noexcept {
    return digest_;
  }

  /// The edition the program is, or null for an unrecognized one
  /// (edition.h) — in which case no seam is available, by the per-binary
  /// rule rather than by any check of this table.
  [[nodiscard]] const edition* known_edition() const noexcept {
    return edition_;
  }

  /// Where the loader put the program, as a physical address.
  [[nodiscard]] std::uint32_t image_base() const noexcept {
    return static_cast<std::uint32_t>(image_segment_) * 16U;
  }

  // --- The toggles ------------------------------------------------------
  //
  // Configuration, applied before the program runs or between `run()`
  // calls, never from inside one: a handler that toggled a seam would be
  // rewriting the table the dispatch is walking.

  /// Turn `id` on. `seam_reason::none` on success. A seam that is on but
  /// whose module is not resident is on and inert, and this still answers
  /// `none` — the seam took; the module is the program's business.
  seam_error enable(std::string_view id);

  /// Turn `id` off. `seam_reason::none` on success, `unknown_seam`
  /// otherwise; a seam that was already off is simply off.
  seam_error disable(std::string_view id);

  /// Turn everything off. `machine::reset()` calls this — an enabled seam
  /// is a setting about a program, and a reset machine has no program.
  void clear() noexcept;

  /// Pull `id`'s trigger: the next arrival at one of its points runs its
  /// handler, once (#161). `now` is the machine's own virtual time, and
  /// is kept only so that `seam_status::waited` can say afterwards how
  /// long the pull took to be served.
  ///
  /// `seam_reason::none` if the latch took. `unknown_seam` for a name
  /// that is not a seam, `not_triggered` for one that does not take a
  /// trigger, `not_enabled` for one that is off. A second pull while one
  /// is outstanding is not an error and is not a second pull: the latch
  /// is one-shot, so it is already set, and the tick stays the first
  /// one's — the wait a host is watching is the wait since the person
  /// first asked.
  ///
  /// Configuration, like `enable()`: a host pulls between `run()` calls,
  /// never from inside one, and nothing about the latch is machine
  /// state.
  seam_error pull(std::string_view id, ticks now);

  /// Whether `id` has a pull outstanding. `status()` says the same
  /// thing; this is the question a host's key handler asks of every
  /// seam without building a row.
  [[nodiscard]] bool waiting(std::string_view id) const noexcept;

  /// The ids currently on, in registry order — what a host prints back so
  /// a run says what was done to it, and what a replay records.
  [[nodiscard]] std::size_t enabled_count() const noexcept;
  [[nodiscard]] std::string_view enabled_id(std::size_t nth) const noexcept;

  /// Whether any seam is on at all — the test `machine` makes before
  /// bothering the engine about the overlay table.
  [[nodiscard]] bool any_enabled() const noexcept { return enabled_ != 0; }

  // --- The hot path -----------------------------------------------------

  /// Whether any point is armed. The whole of what a step costs when
  /// nothing is on.
  /// Whether the engine has anything to do at a step boundary.
  ///
  /// A batch of calls counts, and not only because it has to be driven:
  /// a seam switched off in the middle of one leaves a half-finished
  /// frame on the machine's stack, and the only honest thing is to let it
  /// return (#188). You cannot un-call a call.
  [[nodiscard]] bool armed() const noexcept {
    return armed_ != 0 || call_.active;
  }

  /// Run whatever is armed at `at`, if anything. Called from
  /// `machine::step()` at the boundary, only when `armed()`.
  void dispatch(machine& box, std::uint32_t at);

  /// The overlay table changed (overlay.h): re-evaluate every enabled
  /// seam's points against it. `machine` calls this after a read the
  /// tracker recorded, only when `any_enabled()`.
  void rearm(const overlay_tracker& overlays);

  /// The machine's RAM, for reading a module's load segment out of the
  /// program's own record (`seam_module::load_segment_at`, overlay.h).
  /// `machine` hands it over once; the engine only ever reads it, and
  /// only at the one word a seam's facts name.
  void watch_memory(std::span<const std::uint8_t> ram) noexcept { ram_ = ram; }

  // --- The host service slot ----------------------------------------------

  // --- Document gates (M5-D3, #171) --------------------------------------
  //
  // PLAN.md §5 gates two enhancements on a document the player holds.
  // Presenting one is a thing a *person* does, once, to a host — not a
  // thing the program does and not a thing the machine arrives at — so
  // it is configuration in exactly the sense the enables are, and it
  // survives `reset()` the way an attached device does. A reset machine
  // has no program; the player still has their code wheel.
  //
  // Nothing here reads inside a document. A gate is over bytes
  // (document.h), and the bytes never reach this file: a host hashes
  // what it was handed and presents the digest.

  /// The player presented a document with this digest. Answers the
  /// edition it is, or **null for one this build does not recognize** —
  /// in which case nothing is satisfied and the host says so (PLAN.md
  /// §9's friendly unrecognized-artifact path, which M6 puts a face on).
  ///
  /// Presenting the same document twice is presenting it. Presenting a
  /// second document of the same kind satisfies the same gate, because
  /// the gate is about what the player holds and they hold both.
  const document_edition* present_document(const sha256_digest& digest);

  /// Register `document` as an edition this engine recognizes, beside
  /// the ones `known_documents()` carries. `document` must outlive the
  /// engine. False, and nothing registered, if there is no room.
  ///
  /// The door `add()` is for a seam: a test's own fact table, so the
  /// gate mechanism can be driven against a synthetic document and a
  /// synthetic edition rather than against a file nobody may commit
  /// (PLAN.md §6).
  bool add_document(const document_edition& document) noexcept;

  /// Whether a document of `kind` has been presented. `document_kind::
  /// none` is satisfied by nothing and needs nothing, which is what
  /// makes an ungated seam ungated.
  [[nodiscard]] bool holds_document(document_kind kind) const noexcept;

  /// The documents presented so far, in the order they were presented —
  /// what a host prints back so a run says what was shown to it.
  [[nodiscard]] std::size_t document_count() const noexcept {
    return documents_;
  }

  // --- The code wheel, answered (M6-C1a, #291) ---------------------------
  //
  // The gate this replaced asked a player for a *file* — a PDF of the
  // code wheel — and the releases sold today do not ship one: they ship
  // a code generator application instead (#290). So the proof moved from
  // the artifact to the act. A player answers the program's own
  // challenge once, correctly, off whatever they own; the seam sees the
  // program's own comparison come out equal and latches it here; a host
  // remembers it; and from the next launch the challenge is not drawn.
  //
  // It is **configuration**, in exactly the sense a presented document
  // was and for the same reasons: `clear()` and `reset()` leave it
  // alone (a reset machine has no program, and the person running it
  // still answered the question last Tuesday), the serialization never
  // sees it, and a machine with it set and every seam off is
  // byte-for-byte a machine without it.
  //
  // *Which* copy this is, is not asked here. A host keys what it
  // remembers by the program's fingerprint (#292), and the seam refuses
  // any binary its own fingerprints do not name.

  /// Whether the code-wheel challenge has been answered on this machine
  /// — set by a host from what it remembered, before the run, or by the
  /// seam during one, at the moment a person gets it right.
  [[nodiscard]] bool code_wheel_answered() const noexcept {
    return code_wheel_answered_;
  }

  /// Say so, or unsay it. A host sets it from its store and clears it
  /// when a person asks to be asked again; the seam only ever sets it.
  void set_code_wheel_answered(bool answered) noexcept {
    code_wheel_answered_ = answered;
  }
  [[nodiscard]] const document_edition* document_at(
      std::size_t nth) const noexcept;

  /// Attach the host's services, or detach with null. A setting, like an
  /// attached device: it survives `reset()`.
  ///
  /// Attaching one changes **nothing** about a machine whose seams are
  /// all off, and that is a test rather than a sentence (#169): nothing
  /// here is consulted until a handler calls out, and no handler runs
  /// until a seam is on. The fidelity invariant is the same invariant
  /// with a host in the room.
  void set_host(seam_host_services* host) noexcept { host_ = host; }
  [[nodiscard]] seam_host_services* host() const noexcept { return host_; }

  /// How many calls of `which` a host has **served** since the last
  /// `clear()`, and what the last of them carried.
  ///
  /// The polled half of the pair #153 taught this project to build: a
  /// stream cannot express "it never asked". A page that only watched
  /// events could not tell a seam that called out and was answered from
  /// one that never called out at all, because both look like an empty
  /// stream — and "the callout never happened" is precisely the failure
  /// a new door has. So the count sits beside whatever a host does with
  /// the call, exactly as `seam_status::fired` sits beside the
  /// `seam_event` stream, and reaches a browser through
  /// `af_machine_seam_host_calls`.
  ///
  /// **Served, not asked.** A call made on a machine with no host
  /// attached does not count, because nothing happened: `call_host()`
  /// answers false, and the handler says so through the fail-closed path
  /// that question belongs in (`decline()`, `seam_event`). A non-zero
  /// count is therefore proof that an implementation was reached and
  /// not merely that a seam tried.
  ///
  /// Counted **here** rather than in each host's object, though the
  /// object is what serves: this is the engine's record of what it
  /// routed, the way `fired` is its record of what it dispatched, so the
  /// number cannot differ between a desktop run and a browser one, an
  /// implementation cannot forget to keep it, and the ABI has a door
  /// without a host-specific export behind it.
  ///
  /// Bookkeeping, not machine state — above the fidelity boundary, never
  /// serialized, and it can only move when a seam is on.
  [[nodiscard]] std::uint64_t host_calls(
      seam_host_service which) const noexcept;

  /// The argument of the most recent served call of `which`, or zero if
  /// there has not been one. What the callout *carried*, which is the
  /// other half of what a page has to learn (#169) — the count says a
  /// journal entry was asked for, this says which.
  [[nodiscard]] std::uint32_t host_argument(
      seam_host_service which) const noexcept;

 private:
  friend class seam_context;

  struct slot {
    const seam_definition* seam{nullptr};
    bool enabled{false};
    /// Why an enabled seam is not (fully) armed; `none` when it is.
    seam_reason reason{seam_reason::none};
    bool armed{false};
    /// Handler runs since `enable()`. `seam_status::fired`.
    std::uint64_t fired{};
    /// Arrivals at one of this seam's points since `enable()`, handler
    /// run or not. `seam_status::reached`.
    std::uint64_t reached{};
    /// The one-shot latch (#161): set by `pull()`, cleared by the
    /// arrival that serves it. Only ever consulted for a seam whose
    /// definition says `trigger`.
    bool waiting{false};
    ticks pulled_at{};
    ticks waited{};
    /// The seam's own words (#189). Configuration like everything else
    /// here: cleared by `enable()`, dropped by `reset()`, never
    /// serialized.
    std::array<std::uint16_t, scratch_words> scratch{};
    /// How many times a handler of this seam has declined
    /// (`seam_context::decline`). Zero means it has not, which is what
    /// the diagnostics channel's report-once test reads; the count
    /// itself is `seam_status::declined`. Cleared when the seam is
    /// enabled, so turning it off and on again asks the question afresh.
    std::uint64_t declined{};
  };

  struct armed_point {
    /// The physical address the point is on, for a module that stays
    /// where it was put. Meaningless for a point whose module names a
    /// load-segment word: that one's address is `anchor`'s contents
    /// times sixteen, plus `offset`, worked out afresh at every step.
    /// Meaningless too — and zero — for one that has no address at all.
    std::uint32_t at{};
    std::uint32_t module_base{};
    /// The physical address of the word the program keeps this module's
    /// load segment in, or `no_load_segment` for a point that has none.
    std::uint32_t anchor{no_load_segment};
    /// The point's offset in its module — kept past arming only because
    /// an anchored point re-derives its address from it.
    std::uint32_t offset{};
    seam_handler run{nullptr};
    std::size_t owner{};
    /// `seam_point::at_every_step`: this point has no address and is
    /// offered at every step boundary while its seam's latch is set.
    bool at_every_step{false};
  };

  /// One queued call: where, and the words to push before it.
  struct queued_call {
    std::uint16_t segment{};
    std::uint16_t offset{};
    std::uint8_t words{};
    std::array<std::uint16_t, max_call_words> word{};
  };

  /// A batch of calls into the program, outstanding (#188).
  ///
  /// **Not machine state**, exactly as a seam's enable and its latch are
  /// not: `reset()` drops it, the serialization never sees it, and a run
  /// in which no seam calls anything never has one. What *is* machine
  /// state is everything the calls did, which is the point of them.
  struct call_batch {
    /// A handler has queued something, or a call is running.
    bool active{false};
    /// The first call has been set up, so the machine is inside one and
    /// the return address is worth comparing against.
    bool running{false};
    std::size_t count{};
    std::size_t next{};
    std::array<queued_call, max_calls> call{};
    /// The register file as it was when the handler returned — every
    /// word, IP and SP included. Restoring it is how the machine gets
    /// back to the instruction it was about to execute.
    cpu::registers saved{};
    /// Which seam's, for the diagnostics line if the batch has to be
    /// abandoned.
    std::string_view owner{};
    /// Bytes `place_bytes()` has put on the machine's stack for this
    /// batch, against `max_call_bytes`. Counted across the batch and not
    /// per call: the bound that matters is how much of the program's
    /// stack a seam may stand on, and ten small strings are as much of
    /// it as one large one.
    std::size_t placed{};
    /// Step boundaries seen since the batch began, against
    /// `max_call_steps`. A routine that never returns is a fact table
    /// that named the wrong address, and it must not be a host that
    /// never returns.
    std::uint32_t steps{};
  };
  call_batch call_{};

  /// Begin a batch if one is not already open: snapshot the register
  /// file, so that whatever the calls do, the machine can be put back.
  void open_batch(machine& box, std::string_view id) noexcept;

  /// Set up the next queued call, or — when there is none left — restore
  /// the snapshot and close the batch. True when the batch is finished,
  /// which is when the caller offers the point again (#189).
  bool advance_batch(machine& box) noexcept;

  /// Put the machine back and drop the batch, with a line saying why.
  void abandon_batch(machine& box, seam_reason why) noexcept;

  /// The word at `address`, or zero if it is not wholly inside the RAM
  /// the engine was handed. Zero is the same answer as "the module is
  /// not loaded", which is the fail-closed direction for a fact table
  /// that names an offset the machine does not have.
  [[nodiscard]] std::uint16_t word_at(std::uint32_t address) const noexcept;

  [[nodiscard]] std::size_t index_of(std::string_view id) const noexcept;

  /// Whether `seam` names the loaded program's digest.
  [[nodiscard]] bool applies(const seam_definition& seam) const noexcept;

  /// Why `seam` is not armable right now, or `none` if it is: the gate
  /// first, then the modules.
  ///
  /// One function, and one order, for the same reason `modules_resident`
  /// is one function: `status()` answers a host asking right now and
  /// `arm_all()` decides whether a transition is worth a line, and the
  /// two must never be able to disagree about why a seam is inert. The
  /// gate comes first because it is the condition a *person* can do
  /// something about — telling a player their overlay is not resident
  /// when what they actually need is to present a code wheel would send
  /// them to wait for the game.
  [[nodiscard]] seam_reason blocking_reason(
      const seam_definition& seam) const noexcept;

  /// Whether every module `seam`'s points live in is in memory *now* —
  /// asked of the program's own record where there is one, and of the
  /// tracker otherwise. The resident image always is.
  ///
  /// One function for both callers on purpose: `status()` answers a host
  /// asking right now, `arm_all()` decides whether a transition is worth
  /// a line, and the two must never be able to disagree about what
  /// resident means.
  [[nodiscard]] bool modules_resident(
      const seam_definition& seam) const noexcept;

  /// Rebuild the armed table from the enabled slots against `overlays`.
  void arm_all(const overlay_tracker* overlays);

  void report(std::string_view id, seam_event_kind kind,
              seam_reason reason) noexcept;

  /// `seam_context::decline`'s other half: report once per seam per
  /// enable, and do nothing else. Nothing here touches machine state —
  /// a decline is the seam saying it did not.
  void note_decline(std::string_view id, seam_reason why) noexcept;

  std::array<slot, max_seams> slots_{};
  std::size_t registered_{};
  std::size_t enabled_{};

  std::array<armed_point, max_points> points_{};
  std::size_t armed_{};

  sha256_digest digest_{};
  const edition* edition_{nullptr};
  std::uint16_t image_segment_{};
  bool have_program_{false};

  diagnostics* log_;
  seam_host_services* host_{nullptr};

  /// `host_calls()` / `host_argument()`, one entry per service. Cleared
  /// by `clear()` with everything else that is configuration.
  std::array<std::uint64_t, seam_host_service_count> host_calls_{};
  std::array<std::uint32_t, seam_host_service_count> host_arguments_{};

  /// Documents the player has presented (#171). Configuration, but of a
  /// different kind to the two above: `clear()` and `reset()` leave it
  /// alone, because a reset machine has no program and the player still
  /// holds what they hold.
  ///
  /// Room for the build's own table and a test's own, the way the seam
  /// registry has room for both.
  static constexpr std::size_t max_documents = 8;
  std::array<const document_edition*, max_documents> presented_{};
  std::size_t documents_{};
  std::array<const document_edition*, max_documents> extra_documents_{};
  std::size_t extra_documents_count_{};

  /// The code wheel's challenge, answered (#291). Configuration of the
  /// same kind as `presented_` above, and left alone by `clear()` and
  /// `reset()` for the same reason.
  bool code_wheel_answered_{false};

  /// The machine's RAM, read-only and read at one word at a time
  /// (`watch_memory`). Empty until `machine` hands it over, and an empty
  /// one answers every anchored point "not loaded".
  std::span<const std::uint8_t> ram_{};

  /// The overlay table the points were last armed against, kept so that
  /// `enable()` — which has no tracker argument — can arm against the
  /// one `rearm()` last saw.
  const overlay_tracker* overlays_{nullptr};
};

}  // namespace amberfolio::machine
