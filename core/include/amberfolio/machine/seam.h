// SPDX-License-Identifier: AGPL-3.0-only
//
// Seams: the one mechanism by which anything other than the program's own
// instructions is allowed to touch this machine (PLAN.md §5).
//
// A seam is an **opt-in runtime patch**: an identifier, a description,
// the binary fingerprint it applies to, and a set of interception points
// — CS:IP breakpoints whose handlers are native C++ that reach into the
// emulated machine's registers and memory from outside. The bytes on the
// player's disk are never touched, nothing is injected into the emulated
// machine to execute there, and every seam is individually toggleable and
// **off by default**. With all seams off this file costs the machine one
// boolean test per step, and the machine is a plain one running an
// unmodified program.
//
//
// Why this is here in M3
// ----------------------
//
// The seam engine is M4's (#94 lists it as out of M3 by decision), and
// this is deliberately not that engine — it is the smallest slice of it
// that M3's boot driver could not do without, landed early and named so
// that M4 grows it rather than replaces it.
//
// The forcing case: PLAN.md §7's M3 exit criterion is "title → party
// roster". Between the two sits the code-wheel challenge, which cannot be
// answered by a program, and past which the boot log has nothing more to
// say. Every service #85–#90 might still owe is on the other side of it.
//
// What M4 owes on top of this, and what this deliberately does not
// pretend to have:
//
//   * **Overlay qualification.** PLAN.md §5 requires an interception
//     point to be qualified by which module is resident, because the
//     program swaps overlaid code through shared memory and a raw
//     address therefore does not identify code. Every point here is in
//     the *resident* image and cannot be overlaid, so the question does
//     not arise yet — but a seam pointing into overlaid code must not be
//     written against this file until that qualification exists.
//   * **A fingerprint database.** One seam, one fingerprint, compiled
//     in. M4 makes it a table.
//   * **Config and UI.** `enable()` is called by a host that was told to
//     (a `--seam` flag today). M6 is where a person toggles one.
//   * **The possession gate.** PLAN.md §5's code-wheel seam is gated on a
//     fingerprint-verified code wheel PDF — it demonstrates that the
//     player holds the document. That gate is M5's and is **not here**:
//     what ships today is a bypass a maintainer turns on by hand, on
//     their own copy, and that is the whole of its intended audience
//     until the gate lands.
//
//
// Where a handler may reach, and where it may not
// -----------------------------------------------
//
// A `seam_handler` is a plain function pointer — the same shape and the
// same reason as `cpu::handler` and `service_handler` (service_floor.h):
// core carries no `<functional>` and allocates nothing. Its whole world
// is the `machine&` it is handed, which is registers, memory, ports and
// the platform interface.
//
// It runs **at a step boundary, before the instruction at CS:IP is
// fetched**, which is the same moment the BIOS callout runs and for the
// same reason: it is the only point where CS:IP is settled. A handler
// that wants to let the instruction happen simply returns; a handler that
// wants it not to happen moves IP.
//
// It must not stop the machine. A seam is an enhancement above the
// fidelity boundary, and "the enhancement gave up" is not a machine
// state — a seam whose preconditions are not met stays inert and says so
// (PLAN.md §5's fail-closed rule), which here means reporting through
// `diagnostics` and disarming itself rather than halting a run the player
// asked for.
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

#include "amberfolio/sha256.h"

namespace amberfolio::machine {

class machine;

/// What runs when execution reaches an armed interception point. Native
/// C++, called from outside the emulated machine, with the machine as its
/// only argument — see this file's top comment for what it may do.
using seam_handler = void (*)(machine& box);

/// One interception point: a physical address, and what to do there.
///
/// The address is an **offset from the loaded program's image segment**,
/// not an absolute physical address, because where DOS places a program
/// is the loader's business and a seam's facts are about the program
/// (PLAN.md §5: "a database of addresses and offsets"). `seam_engine`
/// adds the load base when it arms.
struct seam_point {
  std::uint32_t image_offset{};
  seam_handler run{nullptr};
};

/// A seam, as a fact table: what it is, what it applies to, and where it
/// intercepts.
struct seam_definition {
  /// The config key and the name a host's `--seam` takes. Kebab-case.
  std::string_view id;

  /// One line, for a listing.
  std::string_view about;

  /// The SHA-256 of the program image this seam's addresses are facts
  /// about, as 64 lowercase hex characters. A seam is refused against
  /// any other binary — PLAN.md §5's per-binary rule, which is what
  /// keeps a set of addresses from being applied to something they do
  /// not describe.
  std::string_view fingerprint;

  std::span<const seam_point> points;
};

/// Every seam this build carries. One, today.
[[nodiscard]] std::span<const seam_definition> all_seams();

/// Why `seam_engine::enable()` refused.
enum class seam_error : std::uint8_t {
  none,
  /// No seam has that id.
  unknown_seam,
  /// The program loaded is not the one this seam's addresses describe.
  wrong_binary,
  /// No program has been loaded yet, so there is nothing to key on and
  /// no image segment to place the points against.
  no_program,
  /// More points than the engine has room for. A build-time mistake, not
  /// something a caller can recover from.
  too_many_points,
};

/// The armed interception points, and the toggles over them.
///
/// Owned by the machine, like `dos_services` and for the same reason: a
/// handler is a plain function pointer with nowhere to keep state, so its
/// world is what `machine` hands out.
class seam_engine {
 public:
  /// Points armed at once, across every enabled seam. Eight is more than
  /// the whole v1 seam set's code-wheel entry needs and small enough that
  /// the scan is not worth thinking about.
  static constexpr std::size_t max_points = 8;

  /// Seams that can be on at once.
  static constexpr std::size_t max_enabled = 4;

  /// Tell the engine what program is running: the digest of its image and
  /// the segment it was loaded at. A host calls this once after
  /// `load_program()`; nothing can be enabled before it.
  ///
  /// Clears everything already enabled, because a different program makes
  /// every armed address meaningless.
  void loaded(const sha256_digest& digest, std::uint16_t image_segment);

  /// Turn `id` on. `seam_error::none` on success.
  seam_error enable(std::string_view id);

  /// Turn everything off. `machine::reset()` calls this — an enabled seam
  /// is a setting about a program, and a reset machine has no program.
  void clear() noexcept;

  /// Whether any point is armed. The whole of what a step costs when
  /// nothing is on.
  [[nodiscard]] bool armed() const noexcept { return armed_ != 0; }

  /// Run whatever is armed at `at`, if anything. Called from
  /// `machine::step()` at the boundary, only when `armed()`.
  void dispatch(machine& box, std::uint32_t at);

  /// Where the loader put the program, as a physical address. A seam's
  /// offsets are facts about the program (`seam_point`), so a handler
  /// that has to check an address against one of them adds this — the
  /// same arithmetic `enable()` does for the points themselves.
  [[nodiscard]] std::uint32_t image_base() const noexcept {
    return static_cast<std::uint32_t>(image_segment_) * 16U;
  }

  /// The ids currently on, in the order they were enabled — what a host
  /// prints back so a run says what was done to it.
  [[nodiscard]] std::span<const std::string_view> enabled() const noexcept {
    return {enabled_.data(), enabled_count_};
  }

 private:
  struct armed_point {
    std::uint32_t at{};
    seam_handler run{nullptr};
  };

  std::array<armed_point, max_points> points_{};
  std::size_t armed_{};

  std::array<std::string_view, max_enabled> enabled_{};
  std::size_t enabled_count_{};

  sha256_digest digest_{};
  std::uint16_t image_segment_{};
  bool have_program_{false};
};

}  // namespace amberfolio::machine
