// SPDX-License-Identifier: AGPL-3.0-only
//
// seam.h has the design; this is the registry, the arming and the
// dispatch it promises.

#include "amberfolio/machine/seam.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "amberfolio/cpu/address.h"
#include "amberfolio/cpu/registers.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"

namespace amberfolio::machine {

seam_reading seam_reading_of(const seam_status& row) noexcept {
  // `armed` first, because every sentence below is about a seam that is
  // placed and could act. An inert one already says `inert` and why.
  if (!row.armed) {
    return seam_reading::nothing_to_say;
  }
  // An outstanding pull is what the person is waiting on, so it outranks
  // whatever the seam did earlier in the run — and the two ways of
  // waiting mean opposite things. Declined is the seam working and
  // refusing; not-reached is the program not having been there.
  if (row.waiting) {
    return row.declined != 0 ? seam_reading::pulled_and_declined
                             : seam_reading::pulled_and_not_served;
  }
  // It acted, and nothing is outstanding. **No warning may be printed
  // past this line** — that is the whole of #163's defect: a seam served
  // by a point with no address reports `fired=1 reached=0`, and every
  // reading keyed on `reached == 0` called that a broken address table.
  if (row.fired != 0) {
    return row.trigger ? seam_reading::served : seam_reading::nothing_to_say;
  }
  if (row.trigger && row.reached != 0) {
    return seam_reading::reached_and_never_pulled;
  }
  // #131's warning, and it is a claim about an address: only for a seam
  // that has one, and only when the program never went to it.
  if (row.addressed && row.reached == 0) {
    return seam_reading::never_reached;
  }
  return seam_reading::nothing_to_say;
}

const char* seam_reading_text(seam_reading reading) noexcept {
  switch (reading) {
    case seam_reading::nothing_to_say:
      return "";
    case seam_reading::served:
      return " - pulled, and served";
    case seam_reading::pulled_and_not_served:
      return " - pulled, and its point not reached since";
    case seam_reading::pulled_and_declined:
      return " - pulled, and not served: what it was offered is not what its"
             " facts describe";
    case seam_reading::reached_and_never_pulled:
      return " - reached, and never pulled; this seam acts only when asked";
    case seam_reading::never_reached:
      return " - armed and never reached; its point may not be where its facts"
             " say";
  }
  return "";
}

const char* seam_reason_name(seam_reason reason) noexcept {
  switch (reason) {
    case seam_reason::none:
      return "none";
    case seam_reason::unknown_seam:
      return "unknown_seam";
    case seam_reason::no_program:
      return "no_program";
    case seam_reason::wrong_binary:
      return "wrong_binary";
    case seam_reason::schema_mismatch:
      return "schema_mismatch";
    case seam_reason::module_not_resident:
      return "module_not_resident";
    case seam_reason::document_not_presented:
      return "document_not_presented";
    case seam_reason::point_not_recognized:
      return "point_not_recognized";
    case seam_reason::too_many_points:
      return "too_many_points";
    case seam_reason::no_room:
      return "no_room";
    case seam_reason::not_triggered:
      return "not_triggered";
    case seam_reason::not_enabled:
      return "not_enabled";
  }
  return "unknown";
}

const char* seam_state_name(seam_state state) noexcept {
  switch (state) {
    case seam_state::off:
      return "off";
    case seam_state::on:
      return "on";
    case seam_state::unavailable:
      return "unavailable";
  }
  return "unknown";
}

const char* seam_event_kind_name(seam_event_kind kind) noexcept {
  switch (kind) {
    case seam_event_kind::enabled:
      return "on";
    case seam_event_kind::disabled:
      return "off";
    case seam_event_kind::armed:
      return "armed";
    case seam_event_kind::inert:
      return "inert";
    case seam_event_kind::refused:
      return "refused";
    case seam_event_kind::pulled:
      return "pulled";
    case seam_event_kind::served:
      return "served";
  }
  return "unknown";
}

// --- seam_context -----------------------------------------------------------

bool seam_context::inject_keystroke(std::uint8_t scancode, std::uint8_t ascii) {
  return box_->inject_keystroke(static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(scancode) << 8U) | ascii));
}

void seam_context::redirect(std::uint16_t cs, std::uint16_t ip) {
  cpu::registers& regs = box_->processor().regs();
  regs[cpu::sreg::cs] = cs;
  regs.ip = ip;
}

bool seam_context::call_host(seam_host_service which, std::uint32_t argument) {
  seam_host_services* host = engine_->host();
  if (host == nullptr) {
    return false;
  }
  host->serve(*box_, which, argument);
  return true;
}

void seam_context::decline(seam_reason why) {
  declined_ = true;
  engine_->note_decline(id_, why);
}

// --- seam_engine -------------------------------------------------------------

seam_engine::seam_engine(diagnostics* log) noexcept : log_(log) {
  for (const seam_definition& seam : all_seams()) {
    // The build's own table cannot overflow the registry or repeat an id
    // — that would be a mistake in this tree, caught by the unit suite
    // rather than by a refusal nobody reads — so the answer is dropped.
    static_cast<void>(add(seam));
  }
}

bool seam_engine::add(const seam_definition& seam) noexcept {
  if (registered_ == max_seams || index_of(seam.id) != max_seams) {
    return false;
  }
  slots_[registered_] = {.seam = &seam};
  ++registered_;
  return true;
}

std::size_t seam_engine::index_of(std::string_view id) const noexcept {
  for (std::size_t i = 0; i < registered_; ++i) {
    if (slots_[i].seam->id == id) {
      return i;
    }
  }
  return max_seams;
}

const seam_definition* seam_engine::find(std::string_view id) const noexcept {
  const std::size_t index = index_of(id);
  return index == max_seams ? nullptr : slots_[index].seam;
}

bool seam_engine::applies(const seam_definition& seam) const noexcept {
  if (!have_program_) {
    return false;
  }
  for (const std::string_view fingerprint : seam.fingerprints) {
    if (digest_is(digest_, fingerprint)) {
      return true;
    }
  }
  return false;
}

bool seam_engine::modules_resident(const seam_definition& seam) const noexcept {
  for (const seam_point& point : seam.points) {
    if (point.module.is_resident_image()) {
      continue;
    }
    if (point.module.has_load_segment()) {
      if (word_at(image_base() + point.module.load_segment_at) == 0) {
        return false;
      }
      continue;
    }
    if (overlays_ == nullptr || overlays_->resident(point.module) == nullptr) {
      return false;
    }
  }
  return true;
}

seam_reason seam_engine::blocking_reason(
    const seam_definition& seam) const noexcept {
  // The gate first, and seam.h says why: it is the condition a *person*
  // can do something about.
  if (!holds_document(seam.gate)) {
    return seam_reason::document_not_presented;
  }
  if (!modules_resident(seam)) {
    return seam_reason::module_not_resident;
  }
  return seam_reason::none;
}

const document_edition* seam_engine::present_document(
    const sha256_digest& digest) {
  const document_edition* known = find_document(digest);
  if (known == nullptr) {
    for (std::size_t i = 0; i < extra_documents_count_; ++i) {
      if (digest_is(digest, extra_documents_[i]->fingerprint)) {
        known = extra_documents_[i];
        break;
      }
    }
  }
  if (known == nullptr) {
    // Unrecognized, and that is an answer rather than a failure
    // (document.h). Nothing is satisfied and nothing is remembered: a
    // gate that armed on a document this build cannot name would be a
    // gate that armed on anything.
    return nullptr;
  }
  for (std::size_t i = 0; i < documents_; ++i) {
    if (presented_[i] == known) {
      return known;  // Presenting the same document twice is presenting it.
    }
  }
  if (documents_ < max_documents) {
    presented_[documents_++] = known;
  }
  // A document arriving changes what can be armed, exactly as `enable()`
  // does — so the armed table is rebuilt here for the same reason it is
  // rebuilt there. Without this a gated seam would report `armed` from
  // `status()` (which asks the machine afresh) and have no point in the
  // table for `dispatch()` to compare against, which is #131's
  // disagreement between a claim and a fact, arranged on purpose.
  //
  // Configuration, like every other caller of this: a host presents a
  // document between `run()` calls and never from inside one.
  arm_all(overlays_);
  return known;
}

bool seam_engine::add_document(const document_edition& document) noexcept {
  if (extra_documents_count_ == max_documents) {
    return false;
  }
  extra_documents_[extra_documents_count_++] = &document;
  return true;
}

bool seam_engine::holds_document(document_kind kind) const noexcept {
  if (kind == document_kind::none) {
    return true;  // Nothing is needed, so nothing is missing.
  }
  for (std::size_t i = 0; i < documents_; ++i) {
    if (presented_[i]->kind == kind) {
      return true;
    }
  }
  return false;
}

const document_edition* seam_engine::document_at(
    std::size_t nth) const noexcept {
  return nth < documents_ ? presented_[nth] : nullptr;
}

seam_status seam_engine::status(std::size_t index) const noexcept {
  if (index >= registered_) {
    return {};
  }
  const slot& s = slots_[index];
  seam_status out{.id = s.seam->id, .about = s.seam->about};

  if (s.seam->schema != seam_schema_version) {
    out.state = seam_state::unavailable;
    out.reason = seam_reason::schema_mismatch;
    return out;
  }
  if (!have_program_) {
    out.state = seam_state::unavailable;
    out.reason = seam_reason::no_program;
    return out;
  }
  if (!applies(*s.seam)) {
    out.state = seam_state::unavailable;
    out.reason = seam_reason::wrong_binary;
    return out;
  }
  out.state = s.enabled ? seam_state::on : seam_state::off;
  out.trigger = s.seam->trigger;
  out.waiting = s.waiting;
  out.pulled_at = s.pulled_at;
  out.waited = s.waited;
  out.reached = s.reached;
  // Asked of the machine rather than read off `slot`, because a module
  // qualified by the program's own record can come and go without any
  // event this engine is told about (#131). A host that prints a listing
  // gets the answer as of the moment it asked.
  const seam_reason blocked = blocking_reason(*s.seam);
  const bool armed = s.enabled && blocked == seam_reason::none;
  out.armed = armed;
  out.reason = s.enabled ? blocked : seam_reason::none;
  out.fired = s.fired;
  out.declined = s.declined;
  // A fact about the definition rather than about the run, and worked
  // out here so that no host has to walk a point table to say one
  // sentence (#163).
  out.addressed = false;
  for (const seam_point& point : s.seam->points) {
    if (!point.at_every_step) {
      out.addressed = true;
      break;
    }
  }
  return out;
}

seam_status seam_engine::status(std::string_view id) const noexcept {
  return status(index_of(id));
}

void seam_engine::loaded(const sha256_digest& digest,
                         std::uint16_t image_segment) {
  clear();
  digest_ = digest;
  edition_ = find_edition(digest);
  image_segment_ = image_segment;
  have_program_ = true;
}

bool seam_engine::identify(filesystem& fs, const dos_path& path,
                           std::uint16_t image_segment) {
  const vfs_result<sha256_digest> digest = fingerprint_file(fs, path);
  if (!digest.ok()) {
    clear();
    return false;
  }
  loaded(digest.value, image_segment);
  return true;
}

void seam_engine::clear() noexcept {
  for (std::size_t i = 0; i < registered_; ++i) {
    slots_[i].enabled = false;
    slots_[i].reason = seam_reason::none;
    slots_[i].armed = false;
    // The latch goes with the enable it belongs to: a machine with no
    // program has nothing to pull a trigger on (#161).
    slots_[i].waiting = false;
    slots_[i].pulled_at = ticks{};
    slots_[i].waited = ticks{};
  }
  enabled_ = 0;
  points_ = {};
  armed_ = 0;
  have_program_ = false;
  edition_ = nullptr;
}

std::size_t seam_engine::enabled_count() const noexcept { return enabled_; }

std::string_view seam_engine::enabled_id(std::size_t nth) const noexcept {
  for (std::size_t i = 0; i < registered_; ++i) {
    if (!slots_[i].enabled) {
      continue;
    }
    if (nth == 0) {
      return slots_[i].seam->id;
    }
    --nth;
  }
  return {};
}

seam_error seam_engine::enable(std::string_view id) {
  const std::size_t index = index_of(id);
  if (index == max_seams) {
    report(id, seam_event_kind::refused, seam_reason::unknown_seam);
    return seam_reason::unknown_seam;
  }
  slot& s = slots_[index];
  if (s.seam->schema != seam_schema_version) {
    report(id, seam_event_kind::refused, seam_reason::schema_mismatch);
    return seam_reason::schema_mismatch;
  }
  if (!have_program_) {
    report(id, seam_event_kind::refused, seam_reason::no_program);
    return seam_reason::no_program;
  }
  if (!applies(*s.seam)) {
    report(id, seam_event_kind::refused, seam_reason::wrong_binary);
    return seam_reason::wrong_binary;
  }
  if (s.enabled) {
    return seam_reason::none;
  }

  // Room, before anything is changed: a seam that would not fit is
  // refused whole rather than half-armed.
  std::size_t wanted = s.seam->points.size();
  for (std::size_t i = 0; i < registered_; ++i) {
    if (slots_[i].enabled) {
      wanted += slots_[i].seam->points.size();
    }
  }
  if (wanted > max_points) {
    report(id, seam_event_kind::refused, seam_reason::too_many_points);
    return seam_reason::too_many_points;
  }

  s.enabled = true;
  s.declined = 0;  // A fresh enable asks the question again.
  s.fired = 0;
  s.reached = 0;
  s.waiting = false;
  s.pulled_at = ticks{};
  s.waited = ticks{};
  ++enabled_;
  report(id, seam_event_kind::enabled, seam_reason::none);
  arm_all(overlays_);
  return seam_reason::none;
}

seam_error seam_engine::disable(std::string_view id) {
  const std::size_t index = index_of(id);
  if (index == max_seams) {
    report(id, seam_event_kind::refused, seam_reason::unknown_seam);
    return seam_reason::unknown_seam;
  }
  slot& s = slots_[index];
  if (!s.enabled) {
    return seam_reason::none;
  }
  s.enabled = false;
  s.armed = false;
  s.reason = seam_reason::none;
  // An outstanding pull dies with the enable. A latch that survived
  // being switched off would fire on whatever fight happened to be
  // running when somebody switched the seam back on.
  s.waiting = false;
  --enabled_;
  report(id, seam_event_kind::disabled, seam_reason::none);
  arm_all(overlays_);
  return seam_reason::none;
}

seam_error seam_engine::pull(std::string_view id, ticks now) {
  const std::size_t index = index_of(id);
  if (index == max_seams) {
    report(id, seam_event_kind::refused, seam_reason::unknown_seam);
    return seam_reason::unknown_seam;
  }
  slot& s = slots_[index];
  if (!s.seam->trigger) {
    report(id, seam_event_kind::refused, seam_reason::not_triggered);
    return seam_reason::not_triggered;
  }
  if (!s.enabled) {
    report(id, seam_event_kind::refused, seam_reason::not_enabled);
    return seam_reason::not_enabled;
  }
  if (s.waiting) {
    // Already asked, not yet served. One-shot means the second press
    // changes nothing — including the tick, so that a host showing how
    // long the pull has waited shows the wait since the first ask.
    return seam_reason::none;
  }
  s.waiting = true;
  s.pulled_at = now;
  report(id, seam_event_kind::pulled, seam_reason::none);
  return seam_reason::none;
}

bool seam_engine::waiting(std::string_view id) const noexcept {
  const std::size_t index = index_of(id);
  return index != max_seams && slots_[index].waiting;
}

void seam_engine::rearm(const overlay_tracker& overlays) {
  overlays_ = &overlays;
  arm_all(overlays_);
}

void seam_engine::arm_all(const overlay_tracker* overlays) {
  points_ = {};
  armed_ = 0;

  for (std::size_t i = 0; i < registered_; ++i) {
    slot& s = slots_[i];
    if (!s.enabled) {
      continue;
    }
    const bool was_armed = s.armed;
    const seam_reason was_reason = s.reason;

    // A gated seam whose document has not been presented arms **no
    // points at all** (#171). Not "arms them and declines": a point in
    // the table is a point `dispatch()` compares an address against, and
    // fail-closed here means the address never enters the table. The
    // loop below is skipped entirely, and the reason falls out of
    // `blocking_reason()` a few lines down like any other.
    const bool gated_shut = !holds_document(s.seam->gate);

    for (const seam_point& point : s.seam->points) {
      if (gated_shut) {
        break;
      }
      std::uint32_t base = 0;
      std::uint32_t anchor = no_load_segment;
      if (point.module.is_resident_image()) {
        base = image_base();
      } else if (point.module.has_load_segment()) {
        // The program's own note of where this module is, which the
        // manager that moves it keeps up to date. Kept as an *address*
        // and read at every step (`dispatch`) rather than dereferenced
        // here: an address worked out at arming is precisely what #131
        // was filed about. The point stays in the table whether or not
        // the module is loaded right now, too, because a module can
        // become resident without a read — a manager may answer a call
        // from a copy it already holds — and nothing would tell this
        // function to run again if it did.
        anchor = image_base() + point.module.load_segment_at;
      } else {
        const overlay_load* load =
            overlays == nullptr ? nullptr : overlays->resident(point.module);
        if (load == nullptr) {
          continue;
        }
        base = load->first();
      }
      // `enable()` checked the total fits; this cannot overflow, and the
      // guard is what keeps that a property of the table rather than a
      // belief about the caller.
      if (armed_ < max_points) {
        points_[armed_] = {// A point with no address gets none: `at` is
                           // never compared for it, and a plausible-looking
                           // number in a field nothing reads is the kind of
                           // fact this tree does not keep.
                           .at = point.at_every_step ? 0 : base + point.offset,
                           .module_base = base,
                           .anchor = anchor,
                           .offset = point.offset,
                           .run = point.run,
                           .owner = i,
                           .at_every_step = point.at_every_step};
        ++armed_;
      }
    }

    const seam_reason blocked = blocking_reason(*s.seam);
    s.armed = blocked == seam_reason::none;
    s.reason = blocked;

    // Said once per transition, not once per read: a program that loads
    // and unloads an overlay produces one line each way, and a seam that
    // stays inert through a thousand data reads produces none.
    if (s.armed != was_armed || s.reason != was_reason) {
      report(s.seam->id,
             s.armed ? seam_event_kind::armed : seam_event_kind::inert,
             s.reason);
    }
  }
}

std::uint16_t seam_engine::word_at(std::uint32_t address) const noexcept {
  if (address + 1 >= ram_.size()) {
    return 0;
  }
  return static_cast<std::uint16_t>(
      static_cast<unsigned>(ram_[address]) |
      (static_cast<unsigned>(ram_[address + 1]) << 8U));
}

void seam_engine::dispatch(machine& box, std::uint32_t at) {
  for (std::size_t i = 0; i < armed_; ++i) {
    const armed_point& point = points_[i];
    if (point.run == nullptr) {
      continue;
    }

    std::uint32_t base = point.module_base;
    if (point.at_every_step) {
      // A point with no address (#163). One bool decides whether
      // anything at all happens here, and it is the latch: nobody has
      // pulled, nothing is read, nothing is compared, and the step is
      // the step the machine would have taken with this seam off.
      //
      // `waiting` alone, without asking whether the definition is a
      // trigger: only `pull()` sets it and only a trigger may be
      // pulled, so a latch that is set is a trigger's by construction —
      // and a point like this on a definition that is not one simply
      // never runs, which is the fail-closed direction.
      if (!slots_[point.owner].waiting) {
        continue;
      }
      if (point.anchor != no_load_segment) {
        // Address-free is not module-free: the module still has to be
        // in memory, and the program's own note is what says so, read
        // at the one moment when that answer is certainly current.
        const std::uint16_t segment = word_at(point.anchor);
        if (segment == 0) {
          continue;
        }
        base = static_cast<std::uint32_t>(segment) * 16U;
      }
    } else if (point.anchor != no_load_segment) {
      // A module the program can move under us. Its address is whatever
      // the program's own record says at this instant, and nothing else
      // is trusted: not where the module last landed, not where it was
      // when the point was armed.
      //
      // The paragraph test first, and it is free. A load segment is a
      // paragraph, so if this point is here at all then `at` and the
      // point's offset agree in their low four bits — fifteen steps in
      // sixteen leave without touching memory.
      if ((at & 0x0FU) != (point.offset & 0x0FU)) {
        continue;
      }
      const std::uint16_t segment = word_at(point.anchor);
      if (segment == 0) {
        // The program says the module is not loaded. Inert, at the one
        // moment when that answer is certainly current.
        continue;
      }
      base = static_cast<std::uint32_t>(segment) * 16U;
      if (base + point.offset != at) {
        continue;
      }
    } else if (point.at != at) {
      continue;
    }

    slot& owner = slots_[point.owner];
    if (!point.at_every_step) {
      // The arrival, counted first and whatever happens next: `reached`
      // is a fact about where the program went, and it is the only thing
      // that can say how often a trigger *could* be served (#161). A
      // point with no address has no arrivals — it is offered at every
      // step — so it counts none and leaves the number meaning what it
      // means.
      ++owner.reached;
      if (owner.seam->trigger && !owner.waiting) {
        // Reached, and nobody asked. The whole of what a triggered seam
        // does when its latch is down — no read, no write, no register
        // touched, which is what makes an untriggered run the run it
        // would have been with the seam off.
        continue;
      }
    }

    seam_context ctx(box, *this, owner.seam->id, at, base, image_base());
    point.run(box, ctx);
    if (!ctx.declined_) {
      // Acted. A decline is a handler saying it did *not*, and counting
      // it here made the one failure this number exists to expose look
      // like success (#163).
      ++owner.fired;
    }

    if (owner.seam->trigger && owner.waiting && !ctx.declined_) {
      // Served. A handler that declined did not act, so its pull is
      // still outstanding and waits for a point that is the point.
      owner.waiting = false;
      owner.waited = box.time() >= owner.pulled_at
                         ? box.time() - owner.pulled_at
                         : ticks{};
      report(owner.seam->id, seam_event_kind::served, seam_reason::none);
    }
  }
}

void seam_engine::note_decline(std::string_view id, seam_reason why) noexcept {
  const std::size_t at = index_of(id);
  if (at == max_seams) {
    return;
  }
  const bool first = slots_[at].declined == 0;
  ++slots_[at].declined;
  if (!first) {
    // Counted every time, said once: a point offered at every step
    // declines at most of them, and a line each would bury the first.
    // The count is what reaches `seam_status`.
    return;
  }
  report(id, seam_event_kind::inert, why);
}

void seam_engine::report(std::string_view id, seam_event_kind kind,
                         seam_reason reason) noexcept {
  if (log_ != nullptr) {
    log_->report(seam_event{.id = id, .kind = kind, .reason = reason});
  }
}

}  // namespace amberfolio::machine
