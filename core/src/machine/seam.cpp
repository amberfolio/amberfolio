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
#include "amberfolio/machine/edition.h"
#include "amberfolio/machine/fingerprint.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/overlay.h"

namespace amberfolio::machine {

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
    case seam_reason::too_many_points:
      return "too_many_points";
    case seam_reason::no_room:
      return "no_room";
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
  out.reason = s.enabled ? s.reason : seam_reason::none;
  out.armed = s.enabled && s.armed;
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
  --enabled_;
  report(id, seam_event_kind::disabled, seam_reason::none);
  arm_all(overlays_);
  return seam_reason::none;
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

    bool all_resident = true;
    for (const seam_point& point : s.seam->points) {
      std::uint32_t base = 0;
      if (point.module.is_resident_image()) {
        base = image_base();
      } else {
        const overlay_load* load =
            overlays == nullptr ? nullptr : overlays->resident(point.module);
        if (load == nullptr) {
          all_resident = false;
          continue;
        }
        base = load->first();
      }
      // `enable()` checked the total fits; this cannot overflow, and the
      // guard is what keeps that a property of the table rather than a
      // belief about the caller.
      if (armed_ < max_points) {
        points_[armed_] = {.at = base + point.offset,
                           .module_base = base,
                           .run = point.run,
                           .owner = i};
        ++armed_;
      }
    }

    s.armed = all_resident;
    s.reason =
        all_resident ? seam_reason::none : seam_reason::module_not_resident;

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

void seam_engine::dispatch(machine& box, std::uint32_t at) {
  for (std::size_t i = 0; i < armed_; ++i) {
    const armed_point& point = points_[i];
    if (point.at != at || point.run == nullptr) {
      continue;
    }
    seam_context ctx(box, *this, slots_[point.owner].seam->id, at,
                     point.module_base, image_base());
    point.run(box, ctx);
  }
}

void seam_engine::report(std::string_view id, seam_event_kind kind,
                         seam_reason reason) noexcept {
  if (log_ != nullptr) {
    log_->report(seam_event{.id = id, .kind = kind, .reason = reason});
  }
}

}  // namespace amberfolio::machine
