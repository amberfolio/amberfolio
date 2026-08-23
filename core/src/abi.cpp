// SPDX-License-Identifier: AGPL-3.0-only
//
// The C ABI implementation. It is a translation of the C++ API and holds
// no logic of its own — that is the deal: the boundary stays thin enough
// that nothing can be true on one side of it and false on the other.
//
// Which means the rules are: every function here is a null check, a range
// check, a cast, and one call into machine/ or platform.h. Anything that
// needed a decision was a decision for platform.h to make, and if a
// question can only be answered by reading this file then the C++ side
// has a hole in it.

#include "amberfolio/abi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>

#include "amberfolio/abi_bridge.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/clock.h"
#include "amberfolio/machine/dos.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/ega.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/int10.h"
#include "amberfolio/machine/loader.h"
#include "amberfolio/machine/log.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/memory_vfs.h"
#include "amberfolio/machine/pic.h"
#include "amberfolio/machine/pit.h"
#include "amberfolio/machine/platform.h"
#include "amberfolio/machine/renderer.h"
#include "amberfolio/machine/replay.h"
#include "amberfolio/machine/report.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/speaker.h"
#include "amberfolio/machine/vfs.h"
#include "amberfolio/sha256.h"
#include "amberfolio/version.h"

/// The handle's definition: a machine, and the account of what it says.
///
/// A wrapper struct rather than `typedef machine af_machine`, because the
/// C header declares `struct af_machine` and C has no idea what a
/// namespace is; this is the one place the two spellings meet.
///
/// **Deliberately bare.** The M2 devices (#46-#53) all landed as classes
/// a caller wires up with `machine::attach()`/`schedule()`, and nothing
/// here does that on `af_machine_create()`'s behalf — a fresh machine is
/// CPU and RAM, exactly as `tests/core/abi_test.cpp`'s
/// `AStoppedMachineStillAnswersEveryPull` depends on: it posts a plain
/// `INT 21h` and asserts the machine *stops*, because nothing backs the
/// vector. That is PLAN.md §3's "log, don't fake" rule, demonstrated at
/// the ABI boundary, and it would go silently false the moment this
/// struct grew a DOS handler by default. A host that wants the M2
/// device set — the wasm dev page (#55) does — asks for it explicitly,
/// below.
///
/// The **diagnostic log** (machine/log.h, M4-W1 #108) is the one thing
/// beside the machine, and it does not make the struct less bare in the
/// sense above: it answers nothing, backs no vector, and cannot change
/// what a step does. It only writes down what the machine already said,
/// so that a JS host can read the account a C++ host gets handed as
/// records. It is a member rather than something a host installs because
/// a sink is held by reference for the machine's whole life and there is
/// nowhere else on this side of the boundary for it to live — and it is
/// declared first so that it outlives what points at it.
struct af_machine {
  amberfolio::machine::diagnostic_log log;
  amberfolio::machine::machine box{amberfolio::machine::memory_layout::pc,
                                   &log};
};

namespace {

using amberfolio::machine::hash_state;
using amberfolio::machine::key_action;
using amberfolio::machine::machine;
using amberfolio::machine::replay_player;
using amberfolio::machine::run_end;
using amberfolio::machine::seam_state;
using amberfolio::machine::speed_preset;
using amberfolio::machine::state_hashes;
using amberfolio::machine::stop_reason;
using amberfolio::machine::verify_recording;
using amberfolio::machine::verify_result;
using amberfolio::machine::wall_time;

// The packing in abi.h gives each component 8 bits. Nothing enforces that
// on the project version, so state it here rather than silently truncating
// at 0.0.256.
constexpr bool fits_in_a_byte(int n) noexcept { return n >= 0 && n <= 0xFF; }

static_assert(fits_in_a_byte(amberfolio::core_version.major) &&
                  fits_in_a_byte(amberfolio::core_version.minor) &&
                  fits_in_a_byte(amberfolio::core_version.patch),
              "af_version() packs each version component into 8 bits; this "
              "project version does not fit. Widen the packing in abi.h.");

constexpr uint32_t byte_of(int n) noexcept {
  return static_cast<uint32_t>(n) & 0xFFu;
}

// The C macros against the C++ enums they restate. A host reading abi.h
// and a handler reading clock.h have to mean the same thing by "turbo
// XT", and there is no compiler that would otherwise notice.
static_assert(AF_SPEED_PC_XT == static_cast<uint32_t>(speed_preset::pc_xt));
static_assert(AF_SPEED_TURBO_XT ==
              static_cast<uint32_t>(speed_preset::turbo_xt));
static_assert(AF_SPEED_AT == static_cast<uint32_t>(speed_preset::at));
static_assert(AF_SPEED_PC_386 == static_cast<uint32_t>(speed_preset::pc_386));
static_assert(AF_OK == static_cast<uint32_t>(stop_reason::none),
              "af_machine_stop_reason() returns machine::stop_reason "
              "directly, so a running machine's reason and AF_OK have to be "
              "the same number.");

// And the seam states (machine/seam.h), which `af_machine_seam_state`
// answers with directly.
static_assert(AF_SEAM_OFF == static_cast<uint32_t>(seam_state::off));
static_assert(AF_SEAM_ON == static_cast<uint32_t>(seam_state::on));
static_assert(AF_SEAM_UNAVAILABLE ==
              static_cast<uint32_t>(seam_state::unavailable));

// The same for the run-end values: a host passes one of these straight
// into `format_stop_report`, so the two spellings have to agree.
static_assert(AF_RUN_END_STOPPED == static_cast<uint32_t>(run_end::stopped));
static_assert(AF_RUN_END_STEP_BUDGET ==
              static_cast<uint32_t>(run_end::step_budget));
static_assert(AF_RUN_END_TICK_BUDGET ==
              static_cast<uint32_t>(run_end::tick_budget));
static_assert(AF_RUN_END_HOST_QUIT ==
              static_cast<uint32_t>(run_end::host_quit));

/// The one machine, in static storage.
///
/// core/ does not allocate (PLAN.md §4), and `af_machine_create` has to
/// produce a megabyte-sized object; the way to have both is a buffer that
/// is already there and a placement new into it. See abi.h's "The handle"
/// section for why one is the right number.
///
/// Uninitialized on purpose: it is .bss, so it costs the wasm module no
/// bytes on the wire, and constructing it before `create` is asked for
/// would mean a machine existing that nobody made.
alignas(af_machine) std::array<std::byte, sizeof(af_machine)> storage;
af_machine* live = nullptr;

/// The one replay player, for the same reason and in the same place.
///
/// A player holds the recording's whole manifest in fixed storage
/// (`replay_max_manifest_entries` bounded paths since #155) — tens of
/// kilobytes, which is not a thing to put on a wasm module's stack. It is
/// .bss, it has a trivial destructor, and `replay_player::load()` resets
/// it at the top of every `af_machine_verify_recording`, so nothing
/// carries from one call to the next.
replay_player verifier;

/// The one accessor. Every entry point below starts by calling it, which
/// is what makes "a null handle answers rather than traps" true by
/// construction rather than by remembering.
machine* box_of(af_machine* handle) noexcept {
  return handle == nullptr ? nullptr : &handle->box;
}

const machine* box_of(const af_machine* handle) noexcept {
  return handle == nullptr ? nullptr : &handle->box;
}

/// A DOS path's own worst case: `max_depth` components of
/// `dos_name::max_length` plus their separators, and a drive. A name
/// longer than this could not canonicalize anyway, so refusing it early
/// is the same answer arrived at sooner.
constexpr std::size_t max_name_text =
    (amberfolio::machine::dos_path::max_depth *
     (amberfolio::machine::dos_name::max_length + 1)) +
    2;

/// The length of a NUL-terminated C string, refused past `bound`.
///
/// Bounded because the string comes from the other side of an ABI and
/// nothing here can promise it is terminated at all. Looking at `bound +
/// 1` characters is what makes "longer than anything this can accept"
/// and "not a string" one safely detected answer.
[[nodiscard]] bool text_length(const char* text, std::size_t bound,
                               std::size_t& out) noexcept {
  if (text == nullptr) {
    return false;
  }
  for (std::size_t i = 0; i <= bound; ++i) {
    if (text[i] == '\0') {
      out = i;
      return true;
    }
  }
  return false;
}

/// `text`, canonicalized against the root — the one place a raw path
/// becomes a `dos_path`, for every door in the VFS surface (abi.h's
/// "Names are normalized in core").
///
/// `/` is folded to `\` on the way in, and that fold is here rather than
/// in a host for exactly the reason the rest of the rule is (#146): the
/// two spellings a host can hand over are a browser's
/// `webkitRelativePath`, `SAVE/SAVE1.DAT`, and DOS's own,
/// `SAVE\SAVE1.DAT`, and a page that decided for itself which one it was
/// holding would be deciding what a path is. It stops at this boundary —
/// `canonicalize()` keeps DOS's one separator, so what the emulated
/// program may write is untouched by it.
[[nodiscard]] bool path_of(const char* text,
                           amberfolio::machine::dos_path& out) noexcept {
  std::size_t length = 0;
  if (!text_length(text, max_name_text, length)) {
    return false;
  }
  std::array<char, max_name_text> raw{};
  for (std::size_t i = 0; i < length; ++i) {
    raw[i] = (text[i] == '/') ? '\\' : text[i];
  }
  const auto where = amberfolio::machine::canonicalize(
      amberfolio::machine::dos_path{},
      std::span<const char>(raw.data(), length));
  if (!where.ok()) {
    return false;
  }
  out = where.value;
  return true;
}

/// Make every directory `path` names above its leaf, so that a file can
/// then be created at it. `AF_OK`, or the status
/// `af_machine_vfs_put` should answer with: `AF_INVALID` when `path` is
/// one no file can live at, `AF_NO_ROOM` when the backend had no entry
/// left to make a directory in.
///
/// A host hands an installation over one file at a time (abi.h), so
/// `\SAVE\SAVE1.DAT` may arrive before anything has made `\SAVE` — and
/// `machine::filesystem::create()` deliberately will not make it, because
/// a missing parent is what tells `path_not_found` from `file_not_found`
/// for a program that asks (machine/vfs.h). Somebody has to, and abi.h's
/// whole argument is that it is not the host.
///
/// **Everything is decided before anything is made.** A component above
/// the leaf that already exists as a file, and a leaf that already exists
/// as a directory, are both paths no file can be written at; they are
/// found in the first pass, so a refusal never leaves a half-built tree
/// behind. What is left after that is capacity, which is the backend
/// running out rather than the caller being wrong — that is the whole of
/// why this answers a status and not a `bool` (#158), and
/// `af_machine_vfs_put` says what happens then.
[[nodiscard]] uint32_t make_parents(
    amberfolio::machine::filesystem& fs,
    const amberfolio::machine::dos_path& path) noexcept {
  amberfolio::machine::dos_path so_far;
  for (std::size_t i = 0; i + 1 < path.depth(); ++i) {
    static_cast<void>(so_far.push(path.component(i)));
    const auto seen = fs.stat(so_far);
    if (seen.ok() && !seen.value.is_directory) {
      return AF_INVALID;
    }
  }
  const auto leaf = fs.stat(path);
  if (leaf.ok() && leaf.value.is_directory) {
    return AF_INVALID;
  }

  so_far = amberfolio::machine::dos_path{};
  for (std::size_t i = 0; i + 1 < path.depth(); ++i) {
    static_cast<void>(so_far.push(path.component(i)));
    if (fs.exists(so_far)) {
      continue;
    }
    const auto made = fs.mkdir(so_far);
    if (made == amberfolio::machine::vfs_error::directory_full) {
      return AF_NO_ROOM;
    }
    if (made != amberfolio::machine::vfs_error::none) {
      return AF_INVALID;
    }
  }
  return AF_OK;
}

/// Copy `text` into `out` with a terminator, and answer how many
/// characters were copied. Zero, and nothing written, if it will not fit
/// — a truncated name is a different name.
[[nodiscard]] uint32_t copy_out(std::span<const char> text, char* out,
                                uint32_t max) noexcept {
  if (out == nullptr || text.size() + 1 > max) {
    return 0;
  }
  const std::span<char> destination(out, max);
  for (std::size_t i = 0; i < text.size(); ++i) {
    destination[i] = text[i];
  }
  destination[text.size()] = '\0';
  return static_cast<uint32_t>(text.size());
}

// --- The reference device set (M2-H2, #55) -----------------------------
//
// PLAN.md §3's whole device list — PIT, 8259, EGA, its renderer, the
// speaker — plus the video and DOS service handlers, wired up exactly the
// way every device test's own `rig` wires it by hand
// (int10_test.cpp, dos_test.cpp, pit_test.cpp...). Nothing below is new
// machinery; it is `machine::attach()`/`schedule()` and two `install_*()`
// calls, composed once.
//
// This is **opt-in**, through `af_machine_attach_reference_devices()`
// below, and not something `af_machine_create()` does automatically —
// `af_machine`'s own comment says why: a bare machine with an unbacked
// INT 21h is a documented, tested fact
// (`abi_test.cpp::AStoppedMachineStillAnswersEveryPull`), and folding
// device wiring into `create()` would make it false for every caller,
// not only the ones that want a full PC. The wasm dev page (#55) is the
// first caller that does; a native host wanting the same shape through
// the ABI (rather than linking `amberfolio::core` directly, as
// `hosts/sdl` does) would call the identical function.
struct reference_devices {
  /// "The M2 dev page's backend until #55 lands IndexedDB" — memory_vfs.h
  /// describes this exact class for this exact purpose, written before
  /// this file used it. The embedded demo program does no file I/O, so
  /// this starts and stays empty; it exists so INT 21h's file functions
  /// have a real (if empty) filesystem to answer over instead of a null
  /// one, on the chance a future demo program opens a file.
  amberfolio::machine::memory_filesystem fs;

  /// The minimal 8259: exists only so PIT channel 0's IRQ0 has somewhere
  /// to go (pic.h). The M2-H2 demo program never touches it — its tone
  /// drives PIT channel 2 directly — but a program that does is not
  /// stopped for touching hardware the rest of this set assumes is
  /// there.
  amberfolio::machine::pic::controller pic_ctrl;

  /// Channels 0 (system tick) and 2 (speaker tone), per PLAN.md §3.
  amberfolio::machine::pit pit_dev;

  /// 320x200x16 planar VRAM and its registers (ega.h).
  amberfolio::machine::ega video;

  /// Composes `video`'s planes into the machine's framebuffer on ega.h's
  /// 60 Hz virtual-time deadline. Not a `device` (renderer.h's own top
  /// comment) — it answers no bus cycle — so it is `schedule()`d, not
  /// `attach()`ed, and its `reset()` has to be called by hand, both here
  /// and after every `af_machine_reset()` on a machine this is attached
  /// to (see that function below).
  amberfolio::machine::renderer render;

  /// Port 61h, PIT channel 2's gate and tone, box-filtered into the
  /// machine's audio timeline (speaker.h).
  amberfolio::machine::speaker spk;

  /// Construction order is declaration order, not this list's order —
  /// worth stating because every later member's constructor takes a
  /// reference to an earlier one: `pic_ctrl` before `pit_dev` (the PIT
  /// raises IRQ0 through it), `pit_dev` before `spk` (the speaker reads
  /// channel 2 through it and registers as its one reprogram listener),
  /// `video` before `render` (the renderer reads its planes). `fs` has
  /// no dependents and sits wherever is convenient.
  explicit reference_devices(machine& box) noexcept
      : fs(),
        pic_ctrl(box),
        pit_dev(box, pic_ctrl),
        video(box),
        render(box, video),
        spk(box, pit_dev) {
    // Every attach() here claims a distinct, non-overlapping memory
    // window or port range (ega.h's 0xA0000 window; pic.h's 20h-21h;
    // pit.h's 40h-43h; speaker.h's 61h), so none of these can fail on a
    // freshly created machine — but the calls still report through
    // `machine::attach()`'s own
    // `stop_with(stop_reason::conflicting_claim, ...)` if that ever
    // stops being true, rather than being asserted away.
    //
    // The order is `hosts/sdl`'s and `tests/programs/machine_harness`'s,
    // and since #100 it has to be: the canonical state hashes attached
    // devices in attach order (machine/state.h), so a recording made on
    // one target verifies on another only if all three wire the same
    // list the same way. It is otherwise free — the claims below do not
    // overlap, so no dispatch depends on it — which is exactly why it
    // can be spent on this.
    box.set_filesystem(fs);
    box.attach(pic_ctrl);
    box.attach(pit_dev);
    box.attach(spk);
    box.attach(video);
    box.schedule(pit_dev.channel0_deadline());
    box.schedule(pit_dev.channel2_deadline());
    box.schedule(spk);
    // The renderer is `scheduled` and not a `device` (renderer.h's own
    // top comment — it answers no bus cycle), so it needs this call the
    // same way the PIT's two channels above do: `scheduler::arm()`
    // refuses a participant that was never `add()`ed, and `schedule()`
    // is `add()`'s door. Forgetting this line leaves `render.reset()`
    // below calling `arm()` on an unregistered participant, which is
    // silently a no-op — the frame deadline is never armed and no frame
    // is ever composed, with nothing anywhere saying so.
    box.schedule(render);

    // "Call once after construction and again after every
    // machine::reset()" (renderer.h). af_machine_reset() below is the
    // second half of that promise.
    render.reset();

    amberfolio::machine::install_int10(box.services());
    amberfolio::machine::install_dos_services(box.services());
  }
};

/// At most one reference device set, tied to the one machine
/// `af_machine_create()` can produce — the same "one of these, in static
/// storage, placement-new'd on demand" shape `storage`/`live` above uses,
/// for the same reason (PLAN.md §4: core does not allocate).
alignas(reference_devices)
    std::array<std::byte, sizeof(reference_devices)> devices_storage;
reference_devices* devices_live = nullptr;

/// Why the last `af_machine_load_from_vfs` failed — see
/// `af_machine_load_error()` in abi.h for why the reason is a second call
/// rather than part of the status. Reset on every attempt, so it is
/// always about the most recent one.
amberfolio::machine::loader_error last_load_error =
    amberfolio::machine::loader_error::none;

/// The filesystem the `af_machine_vfs_*` calls are about: the reference
/// device set's own `memory_filesystem`.
///
/// Not `machine::vfs()`, which answers a `filesystem&` — the abstract
/// interface has no `clear()` and no `bytes_used()`, deliberately, since
/// neither is something a DOS program can ask for (machine/vfs.h). These
/// are host affordances over the *one backend a wasm host has*, so this
/// reaches the concrete object rather than widening the interface every
/// backend would then have to implement.
amberfolio::machine::memory_filesystem* vfs_of(af_machine* handle) noexcept {
  if (handle == nullptr || devices_live == nullptr) {
    return nullptr;
  }
  return &devices_live->fs;
}

const amberfolio::machine::memory_filesystem* vfs_of(
    const af_machine* handle) noexcept {
  if (handle == nullptr || devices_live == nullptr) {
    return nullptr;
  }
  return &devices_live->fs;
}

}  // namespace

extern "C" {

uint32_t af_version(void) {
  const amberfolio::version v = amberfolio::linked_version();
  return (byte_of(v.major) << 16) | (byte_of(v.minor) << 8) | byte_of(v.patch);
}

double af_ticks_per_second(void) {
  return static_cast<double>(amberfolio::machine::pit_input_hz);
}

uint32_t af_frame_width(void) { return amberfolio::machine::frame_width; }

uint32_t af_frame_height(void) { return amberfolio::machine::frame_height; }

uint32_t af_palette_entries(void) {
  return amberfolio::machine::palette_entries;
}

af_machine* af_machine_create(void) {
  if (live != nullptr) {
    return nullptr;
  }
  // Default-initialized, not `af_machine{}`: `machine` has an explicit
  // default constructor, which aggregate initialization may not call.
  live = ::new (static_cast<void*>(storage.data())) af_machine;
  return live;
}

void af_machine_destroy(af_machine* handle) {
  if (handle == nullptr || handle != live) {
    return;
  }
  // The reference device set, if this machine has one, is this handle's
  // own state exactly as `live` itself is — nothing ties its lifetime to
  // anything else, so nothing but this call is in a position to end it.
  // Torn down before the machine it points into, for the ordinary reason
  // a device must not outlive what it was given a reference to.
  if (devices_live != nullptr) {
    devices_live->~reference_devices();
    devices_live = nullptr;
  }
  live->~af_machine();
  live = nullptr;
}

uint32_t af_machine_attach_reference_devices(af_machine* handle) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  // Idempotent rather than an error: a host that attaches once at
  // startup and again defensively after a code path it is not sure ran
  // should not have to track whether it already did this, and there is
  // only ever one machine for this to be attached to twice over.
  if (devices_live == nullptr) {
    devices_live = ::new (static_cast<void*>(devices_storage.data()))
        reference_devices(*box);
  }
  return AF_OK;
}

uint32_t af_machine_reset(af_machine* handle) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  box->reset();
  // machine::reset() walks every *attached* device (machine.h), and the
  // renderer is deliberately not one (renderer.h's own top comment) — so
  // it does not know to re-arm the renderer's frame deadline, and
  // nothing else will if this does not. Only relevant once
  // af_machine_attach_reference_devices() has been called; on a bare
  // machine there is no renderer to re-arm.
  if (devices_live != nullptr) {
    devices_live->render.reset();
  }
  return AF_OK;
}

uint32_t af_machine_run_until(af_machine* handle, double tick) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (tick < 0.0) {
    return AF_INVALID;
  }
  box->run(static_cast<amberfolio::machine::ticks>(tick));
  return box->stopped() ? AF_STOPPED : AF_OK;
}

double af_machine_time(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->time());
}

double af_machine_steps(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->steps());
}

uint32_t af_machine_set_trace(af_machine* handle, int32_t on) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  box->trace().enable(on != 0);
  // Both halves of the one facility — see abi.h.
  handle->log.set_tracing(on != 0);
  return AF_OK;
}

uint32_t af_machine_stop_report(const af_machine* handle, uint32_t how,
                                char* out, uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || out == nullptr || max == 0) {
    return 0;
  }
  // An unknown `how` is treated as "the machine stopped" rather than
  // refused: the report is the one thing that must always be printable,
  // and the machine's own reason is the honest default when the host has
  // not named one this file recognizes.
  const run_end ended = (how <= AF_RUN_END_HOST_QUIT)
                            ? static_cast<run_end>(how)
                            : run_end::stopped;
  return static_cast<uint32_t>(
      format_stop_report(*box, ended, std::span<char>(out, max)));
}

uint32_t af_machine_trace_report(const af_machine* handle, char* out,
                                 uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || out == nullptr || max == 0) {
    return 0;
  }
  return static_cast<uint32_t>(
      format_trace_report(*box, std::span<char>(out, max)));
}

int32_t af_machine_stopped(const af_machine* handle) {
  const machine* box = box_of(handle);
  return (box != nullptr && box->stopped()) ? 1 : 0;
}

uint32_t af_machine_stop_reason(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  return static_cast<uint32_t>(box->stop().reason);
}

uint32_t af_machine_read_log(af_machine* handle, char* out, uint32_t max) {
  if (handle == nullptr || out == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(handle->log.read(std::span<char>(out, max)));
}

uint32_t af_machine_log_pending(const af_machine* handle) {
  if (handle == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(handle->log.pending());
}

double af_machine_log_dropped(const af_machine* handle) {
  if (handle == nullptr) {
    return 0.0;
  }
  return static_cast<double>(handle->log.dropped());
}

uint32_t af_machine_clear_log(af_machine* handle) {
  if (handle == nullptr) {
    return AF_NO_MACHINE;
  }
  handle->log.clear();
  return AF_OK;
}

uint32_t af_machine_set_speed(af_machine* handle, uint32_t preset) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (preset > AF_SPEED_MAX) {
    return AF_INVALID;
  }
  box->set_speed(static_cast<speed_preset>(preset));
  return AF_OK;
}

const uint8_t* af_machine_framebuffer(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? nullptr : box->display().pixels().data();
}

const uint8_t* af_machine_palette(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return nullptr;
  }
  // Three bytes per entry, in memory order, which is what `rgb` already
  // is — the reinterpretation is a fact about the layout, asserted rather
  // than assumed.
  static_assert(sizeof(amberfolio::machine::rgb) == 3);
  static_assert(alignof(amberfolio::machine::rgb) == 1);
  return reinterpret_cast<const uint8_t*>(box->display().palette().data());
}

double af_machine_frame_generation(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0
                        : static_cast<double>(box->display().generation());
}

uint32_t af_machine_render_audio(af_machine* handle, float* out,
                                 uint32_t frames, uint32_t sample_rate) {
  machine* box = box_of(handle);
  if (box == nullptr || out == nullptr) {
    return 0;
  }
  const std::span<float> buffer(out, frames);
  return static_cast<uint32_t>(box->audio().render(buffer, sample_rate));
}

double af_machine_audio_underruns(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->audio().underruns());
}

double af_machine_audio_resyncs(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0.0 : static_cast<double>(box->audio().resyncs());
}

uint32_t af_machine_post_key(af_machine* handle, uint32_t scancode,
                             int32_t down) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  // The 0x80 bit is the release bit on the wire and `down` carries it
  // here, so a code with it set is a host that has not read the contract.
  if (scancode == 0 || scancode > 0x7Fu) {
    return AF_INVALID;
  }
  box->post_key(static_cast<std::uint8_t>(scancode),
                down != 0 ? key_action::down : key_action::up);
  return AF_OK;
}

uint32_t af_machine_set_wall_clock(af_machine* handle, uint32_t year,
                                   uint32_t month, uint32_t day, uint32_t hour,
                                   uint32_t minute, uint32_t second,
                                   uint32_t centisecond) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (year > 0xFFFFu || month > 0xFFu || day > 0xFFu || hour > 0xFFu ||
      minute > 0xFFu || second > 0xFFu || centisecond > 0xFFu) {
    return AF_INVALID;
  }

  const wall_time when{.year = static_cast<std::uint16_t>(year),
                       .month = static_cast<std::uint8_t>(month),
                       .day = static_cast<std::uint8_t>(day),
                       .weekday = 0,
                       .hour = static_cast<std::uint8_t>(hour),
                       .minute = static_cast<std::uint8_t>(minute),
                       .second = static_cast<std::uint8_t>(second),
                       .centisecond = static_cast<std::uint8_t>(centisecond)};
  return box->set_wall_time(when) ? AF_OK : AF_INVALID;
}

uint32_t af_machine_read_console(af_machine* handle, uint8_t* out,
                                 uint32_t max) {
  machine* box = box_of(handle);
  if (box == nullptr || out == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(
      box->console().read(std::span<uint8_t>(out, max)));
}

uint32_t af_machine_console_pending(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return 0;
  }
  return static_cast<uint32_t>(box->console().pending());
}

double af_machine_console_dropped(const af_machine* handle) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return 0.0;
  }
  return static_cast<double>(box->console().dropped());
}

uint32_t af_machine_vfs_clear(af_machine* handle) {
  if (box_of(handle) == nullptr) {
    return AF_NO_MACHINE;
  }
  amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return AF_NO_FILESYSTEM;
  }
  fs->clear();
  return AF_OK;
}

uint32_t af_machine_vfs_put(af_machine* handle, const char* path,
                            const uint8_t* bytes, uint32_t size) {
  if (box_of(handle) == nullptr) {
    return AF_NO_MACHINE;
  }
  amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return AF_NO_FILESYSTEM;
  }
  if (bytes == nullptr && size != 0) {
    return AF_INVALID;
  }

  amberfolio::machine::dos_path where;
  if (!path_of(path, where) || where.is_root()) {
    return AF_INVALID;
  }
  const uint32_t parents = make_parents(*fs, where);
  if (parents != AF_OK) {
    return parents;
  }

  const auto made = fs->create(where);
  if (!made.ok()) {
    // The line #158 draws: a refusal about what the *machine* has left
    // (no entry in the table, no handle to open the file with) is
    // AF_NO_ROOM, and a refusal about what the caller asked for (a
    // directory already under that name) stays AF_INVALID. A host can
    // act on the first and cannot on the second.
    const bool out_of_room =
        made.error == amberfolio::machine::vfs_error::directory_full ||
        made.error == amberfolio::machine::vfs_error::too_many_open_files;
    return out_of_room ? AF_NO_ROOM : AF_INVALID;
  }

  bool whole = true;
  if (size != 0) {
    const auto wrote =
        fs->write(made.value, std::span<const uint8_t>(bytes, size));
    // A short count is the backend's honest answer to running out of room
    // (machine/vfs.h). A partly-written file is not the file the host
    // asked for, though, so this refuses — and takes the fragment away
    // again, because a half-written `START.EXE` sitting there under the
    // right name is precisely the plausible wrong answer PLAN.md §3 is
    // about.
    whole = wrote.ok() && wrote.value == size;
  }
  static_cast<void>(fs->close(made.value));
  if (!whole) {
    static_cast<void>(fs->unlink(where));
    // Bytes rather than entries — the arena is full, or this one file is
    // larger than one file may be. Both are room, not a bad argument.
    return AF_NO_ROOM;
  }
  return AF_OK;
}

uint32_t af_machine_vfs_count(const af_machine* handle) {
  const amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return 0;
  }
  const auto count = fs->entry_count(amberfolio::machine::dos_path{});
  return count.ok() ? static_cast<uint32_t>(count.value) : 0;
}

uint32_t af_machine_vfs_name_at(const af_machine* handle, uint32_t index,
                                char* out, uint32_t max) {
  const amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return 0;
  }
  const auto entry = fs->entry_at(amberfolio::machine::dos_path{}, index);
  if (!entry.ok()) {
    return 0;
  }
  return copy_out(entry.value.name.text(), out, max);
}

uint32_t af_machine_vfs_size_at(const af_machine* handle, uint32_t index) {
  const amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return 0;
  }
  const auto entry = fs->entry_at(amberfolio::machine::dos_path{}, index);
  return entry.ok() ? entry.value.size : 0;
}

double af_machine_vfs_bytes_used(const af_machine* handle) {
  const amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  return fs == nullptr ? 0.0 : static_cast<double>(fs->bytes_used());
}

uint32_t af_machine_vfs_fingerprint(af_machine* handle, const char* name,
                                    char* out, uint32_t max) {
  amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return 0;
  }
  amberfolio::machine::dos_path where;
  if (!path_of(name, where) || where.is_root()) {
    return 0;
  }
  const auto digest = amberfolio::machine::fingerprint_file(*fs, where);
  if (!digest.ok()) {
    return 0;
  }
  std::array<char, amberfolio::sha256_digest::text_length + 1> text{};
  if (amberfolio::format_hex(digest.value, text) == 0) {
    return 0;
  }
  return copy_out(std::span<const char>(text.data(),
                                        amberfolio::sha256_digest::text_length),
                  out, max);
}

uint32_t af_machine_load_from_vfs(af_machine* handle, const char* name,
                                  const char* command_tail) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  amberfolio::machine::memory_filesystem* fs = vfs_of(handle);
  if (fs == nullptr) {
    return AF_NO_FILESYSTEM;
  }

  last_load_error = amberfolio::machine::loader_error::none;

  amberfolio::machine::dos_path where;
  if (!path_of(name, where) || where.is_root()) {
    return AF_INVALID;
  }

  // Bounded by what a PSP can hold (machine/loader.h): a longer tail is
  // `loader_error::command_tail_too_long` there, and refusing it here
  // means the same answer without reading past the end of whatever the
  // host actually passed.
  std::size_t tail_length = 0;
  if (command_tail != nullptr &&
      !text_length(command_tail,
                   amberfolio::machine::psp::command_tail_max_length,
                   tail_length)) {
    return AF_INVALID;
  }

  const auto loaded = amberfolio::machine::load_program(
      *box, *fs, where,
      std::span<const char>(command_tail == nullptr ? "" : command_tail,
                            tail_length));
  if (!loaded.ok()) {
    last_load_error = loaded.error;
    return AF_INVALID;
  }

  // The identity of what was just loaded, handed to the seam engine: the
  // file's SHA-256 and the segment the loader placed it at (machine/seam.h).
  // Here rather than left to the page, because a host that loaded a
  // program and forgot to say which is a host whose seams are all
  // unavailable for a reason nothing explains. A file that loaded cannot
  // fail to be read again a moment later; the answer is dropped because
  // the load has already succeeded and is the thing this call reports.
  static_cast<void>(
      box->seams().identify(*fs, where, loaded.value.load_segment));
  return AF_OK;
}

uint32_t af_machine_edition(const af_machine* handle, char* out, uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr) {
    return 0;
  }
  const amberfolio::machine::edition* known = box->seams().known_edition();
  if (known == nullptr) {
    return 0;
  }
  return copy_out(std::span<const char>(known->name.data(), known->name.size()),
                  out, max);
}

uint32_t af_machine_program_fingerprint(const af_machine* handle, char* out,
                                        uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || !box->seams().have_program()) {
    return 0;
  }
  std::array<char, amberfolio::sha256_digest::text_length + 1> text{};
  if (amberfolio::format_hex(box->seams().program(), text) == 0) {
    return 0;
  }
  return copy_out(std::span<const char>(text.data(),
                                        amberfolio::sha256_digest::text_length),
                  out, max);
}

uint32_t af_machine_seam_count(const af_machine* handle) {
  const machine* box = box_of(handle);
  return box == nullptr ? 0 : static_cast<uint32_t>(box->seams().count());
}

uint32_t af_machine_seam_id(const af_machine* handle, uint32_t index, char* out,
                            uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  const std::string_view id = box->seams().status(index).id;
  return copy_out(std::span<const char>(id.data(), id.size()), out, max);
}

uint32_t af_machine_seam_about(const af_machine* handle, uint32_t index,
                               char* out, uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  const std::string_view about = box->seams().status(index).about;
  return copy_out(std::span<const char>(about.data(), about.size()), out, max);
}

uint32_t af_machine_seam_state(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return AF_SEAM_NONE;
  }
  return static_cast<uint32_t>(box->seams().status(index).state);
}

uint32_t af_machine_seam_reason(const af_machine* handle, uint32_t index,
                                char* out, uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  const std::string_view why =
      amberfolio::machine::seam_reason_name(box->seams().status(index).reason);
  return copy_out(std::span<const char>(why.data(), why.size()), out, max);
}

int32_t af_machine_seam_armed(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  return box->seams().status(index).armed ? 1 : 0;
}

double af_machine_seam_fired(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0.0;
  }
  return static_cast<double>(box->seams().status(index).fired);
}

uint32_t af_machine_seam_enable(af_machine* handle, const char* id) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  std::size_t length = 0;
  if (!text_length(id, 64, length)) {
    return AF_INVALID;
  }
  return box->seams().enable(std::string_view(id, length)) ==
                 amberfolio::machine::seam_reason::none
             ? AF_OK
             : AF_INVALID;
}

uint32_t af_machine_seam_disable(af_machine* handle, const char* id) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  std::size_t length = 0;
  if (!text_length(id, 64, length)) {
    return AF_INVALID;
  }
  return box->seams().disable(std::string_view(id, length)) ==
                 amberfolio::machine::seam_reason::none
             ? AF_OK
             : AF_INVALID;
}

int32_t af_machine_seam_triggered(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  return box->seams().status(index).trigger ? 1 : 0;
}

int32_t af_machine_seam_waiting(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0;
  }
  return box->seams().status(index).waiting ? 1 : 0;
}

double af_machine_seam_reached(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0.0;
  }
  return static_cast<double>(box->seams().status(index).reached);
}

double af_machine_seam_waited(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0.0;
  }
  return static_cast<double>(box->seams().status(index).waited);
}

double af_machine_seam_pulled_at(const af_machine* handle, uint32_t index) {
  const machine* box = box_of(handle);
  if (box == nullptr || index >= box->seams().count()) {
    return 0.0;
  }
  return static_cast<double>(box->seams().status(index).pulled_at);
}

uint32_t af_machine_seam_pull(af_machine* handle, const char* id) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  std::size_t length = 0;
  if (!text_length(id, 64, length)) {
    return AF_INVALID;
  }
  // The machine's own clock stamps the pull, never the caller's: a
  // latency measured against a tick a host chose would be a measurement
  // of the host (machine/clock.h).
  return box->seams().pull(std::string_view(id, length), box->time()) ==
                 amberfolio::machine::seam_reason::none
             ? AF_OK
             : AF_INVALID;
}

uint32_t af_machine_load_error(const af_machine* handle) {
  if (box_of(handle) == nullptr) {
    return AF_NO_MACHINE;
  }
  return static_cast<uint32_t>(last_load_error);
}

uint32_t af_machine_write_memory(af_machine* handle, uint32_t address,
                                 const uint8_t* bytes, uint32_t size) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (bytes == nullptr) {
    return AF_INVALID;
  }
  const std::span<std::uint8_t> ram = box->memory().ram();
  if (address > ram.size() || size > ram.size() - address) {
    return AF_INVALID;
  }

  const std::span<const std::uint8_t> source(bytes, size);
  for (std::size_t i = 0; i < source.size(); ++i) {
    ram[address + i] = source[i];
  }
  return AF_OK;
}

uint32_t af_machine_read_memory(af_machine* handle, uint32_t address,
                                uint8_t* out, uint32_t size) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (out == nullptr) {
    return AF_INVALID;
  }
  const std::span<const std::uint8_t> ram = box->memory().ram();
  if (address > ram.size() || size > ram.size() - address) {
    return AF_INVALID;
  }

  const std::span<std::uint8_t> destination(out, size);
  for (std::size_t i = 0; i < destination.size(); ++i) {
    destination[i] = ram[address + i];
  }
  return AF_OK;
}

uint32_t af_machine_set_entry(af_machine* handle, uint32_t cs, uint32_t ip,
                              uint32_t ss, uint32_t sp) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (cs > 0xFFFFu || ip > 0xFFFFu || ss > 0xFFFFu || sp > 0xFFFFu) {
    return AF_INVALID;
  }

  amberfolio::cpu::registers& regs = box->processor().regs();
  regs[amberfolio::cpu::sreg::cs] = static_cast<std::uint16_t>(cs);
  regs.ip = static_cast<std::uint16_t>(ip);
  regs[amberfolio::cpu::sreg::ss] = static_cast<std::uint16_t>(ss);
  regs[amberfolio::cpu::reg16::sp] = static_cast<std::uint16_t>(sp);
  return AF_OK;
}

uint32_t af_machine_state_hash(const af_machine* handle, char* out,
                               uint32_t max) {
  const machine* box = box_of(handle);
  if (box == nullptr || out == nullptr ||
      max < amberfolio::sha256_digest::text_length + 1) {
    return 0;
  }
  const state_hashes hashes = hash_state(*box);
  return static_cast<uint32_t>(
      amberfolio::format_hex(hashes.whole, std::span<char>(out, max)));
}

uint32_t af_machine_verify_recording(af_machine* handle, const char* text,
                                     uint32_t length, char* out, uint32_t max) {
  machine* box = box_of(handle);
  if (box == nullptr) {
    return AF_NO_MACHINE;
  }
  if (text == nullptr) {
    return AF_INVALID;
  }

  // The player is the caller's in core so that a host can read its
  // report whichever way it went; here it is this call's, because the
  // report is the only thing the ABI hands back and it is handed back
  // now.
  //
  // In static storage rather than on the stack, for the reason `storage`
  // above is: a player keeps the whole manifest in fixed storage
  // (`replay_max_manifest_entries` bounded paths since #155), which is
  // some tens of kilobytes, and the stack this is called on is a wasm
  // module's. `load()` is the first thing `verify_recording` does and it
  // resets the player, so one that outlives the call carries nothing
  // into the next.
  const verify_result result =
      verify_recording(*box, box->vfs(), std::span<const char>(text, length),
                       amberfolio::machine::renderer::frame_period, verifier);

  if (out != nullptr && max != 0) {
    static_cast<void>(verifier.report(std::span<char>(out, max)));
  }
  return result.ok() ? AF_OK : AF_INVALID;
}

}  // extern "C"

namespace amberfolio {

machine::machine* af_machine_unwrap(af_machine* box) noexcept {
  return box_of(box);
}

}  // namespace amberfolio
