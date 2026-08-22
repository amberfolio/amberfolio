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
//   * **A host service**, through `seam_context::call_host()` — the
//     callout slot M5's journal, automap and save management consume.
//     Interface only, here: no host has attached one, and a seam that
//     calls one on a machine without one is told so and does nothing.
//
// It must not stop the machine. A seam is an enhancement above the
// fidelity boundary, and "the enhancement gave up" is not a machine state
// — a seam whose preconditions are not met stays inert and *says so*
// (PLAN.md §5's fail-closed rule), through `seam_event` on the diagnostics
// channel and through `status()` for a host that asks.
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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "amberfolio/machine/diagnostics.h"
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
inline constexpr std::uint16_t seam_schema_version = 1;

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
  /// More points than the engine has room for. A build-time mistake, not
  /// something a caller can recover from.
  too_many_points,
  /// The registry is full. The same kind of mistake.
  no_room,
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
};

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
};

struct seam_event {
  std::string_view id;
  seam_event_kind kind{};
  seam_reason reason{seam_reason::none};
};

/// The host services a seam may call out to — the slot M4 defines and M5
/// fills (PLAN.md §5 items 2, 3 and 5; #96). Named here so that the
/// consumers exist as names before any of them exists as code; a seam
/// calling one on a machine with no host attached is told so and does
/// nothing.
enum class seam_host_service : std::uint8_t {
  /// The journal reader: open the entry the argument names.
  journal_open,
  /// The automap: the party moved or the map changed; the argument is
  /// whatever the seam chose to encode.
  automap_update,
  /// Save and roster management: an in-game state change the host's
  /// save manager has to stay consistent with.
  save_state_changed,
};

/// What a host plugs in to answer those calls. Attached with
/// `seam_engine::set_host()`; a plain interface in the shape of
/// `diagnostics` and `filesystem`: held by reference, never owned.
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
};

/// The registry, the toggles, and the armed interception points.
///
/// Owned by the machine, like `dos_services` and for the same reason: a
/// handler is a plain function pointer with nowhere to keep state, so its
/// world is what `machine` hands out.
class seam_engine {
 public:
  /// Definitions the registry holds. The v1 seam set is six (PLAN.md §5)
  /// plus the cheats' two; sixteen leaves room for a test's own and for
  /// the fast-follow fixes without making this a data structure.
  static constexpr std::size_t max_seams = 16;

  /// Points armed at once, across every enabled seam.
  static constexpr std::size_t max_points = 32;

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
  [[nodiscard]] bool armed() const noexcept { return armed_ != 0; }

  /// Run whatever is armed at `at`, if anything. Called from
  /// `machine::step()` at the boundary, only when `armed()`.
  void dispatch(machine& box, std::uint32_t at);

  /// The overlay table changed (overlay.h): re-evaluate every enabled
  /// seam's points against it. `machine` calls this after a read the
  /// tracker recorded, only when `any_enabled()`.
  void rearm(const overlay_tracker& overlays);

  // --- The host service slot ----------------------------------------------

  /// Attach the host's services, or detach with null. A setting, like an
  /// attached device: it survives `reset()`.
  void set_host(seam_host_services* host) noexcept { host_ = host; }
  [[nodiscard]] seam_host_services* host() const noexcept { return host_; }

 private:
  friend class seam_context;

  struct slot {
    const seam_definition* seam{nullptr};
    bool enabled{false};
    /// Why an enabled seam is not (fully) armed; `none` when it is.
    seam_reason reason{seam_reason::none};
    bool armed{false};
  };

  struct armed_point {
    std::uint32_t at{};
    std::uint32_t module_base{};
    seam_handler run{nullptr};
    std::size_t owner{};
  };

  [[nodiscard]] std::size_t index_of(std::string_view id) const noexcept;

  /// Whether `seam` names the loaded program's digest.
  [[nodiscard]] bool applies(const seam_definition& seam) const noexcept;

  /// Rebuild the armed table from the enabled slots against `overlays`.
  void arm_all(const overlay_tracker* overlays);

  void report(std::string_view id, seam_event_kind kind,
              seam_reason reason) noexcept;

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

  /// The overlay table the points were last armed against, kept so that
  /// `enable()` — which has no tracker argument — can arm against the
  /// one `rearm()` last saw.
  const overlay_tracker* overlays_{nullptr};
};

}  // namespace amberfolio::machine
