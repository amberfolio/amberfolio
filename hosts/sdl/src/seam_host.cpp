// SPDX-License-Identifier: AGPL-3.0-only

#include "seam_host.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "amberfolio/host/slot_store.h"
#include "amberfolio/machine/document.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"
#include "amberfolio/machine/text_entry.h"

namespace amberfolio::sdl {

[[nodiscard]] const char* seam_refusal(machine::seam_error why) noexcept {
  switch (why) {
    case machine::seam_error::none:
      return "no reason";
    case machine::seam_error::unknown_seam:
      return "no seam by that name";
    case machine::seam_error::wrong_binary:
      return "this seam's addresses are facts about a different binary";
    case machine::seam_error::no_program:
      return "no program was loaded to key it on";
    case machine::seam_error::schema_mismatch:
      return "this seam was written against another schema version";
    case machine::seam_error::module_not_resident:
      return "the module this seam lives in is not resident";
    case machine::seam_error::document_not_presented:
      // The one refusal a *person* can do something about (#171), so it
      // says what to do rather than only what is wrong.
      return "this seam needs a document you have not presented - show it"
             " with --document";
    case machine::seam_error::call_did_not_return:
      // Also never an answer to `enable()`: the engine produces it when a
      // seam called into the program and the call did not come back
      // (#188), and it means a fact table naming an address that is not
      // the routine it says it is.
      return "this seam called into the program and the call did not come"
             " back";
    case machine::seam_error::point_not_recognized:
      // Never an answer to `enable()` - a handler produces it, at a
      // point, and the host renders it through the seam-event line. Here
      // because the enumeration is one and a switch over it has to be
      // whole.
      return "what is at one of this seam's points is not what its facts"
             " describe";
    case machine::seam_error::too_many_points:
      return "too many interception points for this build";
    case machine::seam_error::no_room:
      return "the seam registry is full";
    case machine::seam_error::not_triggered:
      return "this seam is not one you pull; it acts whenever it is on";
    case machine::seam_error::not_enabled:
      return "this seam is off - turn it on with --seam before pulling it";
  }
  return "unknown";
}

bool enable_requested_seams(machine::machine& box, const options& opts) {
  for (const std::string& id : opts.seams) {
    const machine::seam_error why = box.seams().enable(id);
    if (why == machine::seam_error::none) {
      continue;
    }
    std::fprintf(stderr, "amberfolio: seam %s refused (%s)\n", id.c_str(),
                 seam_refusal(why));
    // **A remembered choice that no longer fits does not stop the
    // launch** (#383). A `--seam` this host cannot honour is a command
    // line to fix and still does; but a seam a player turned on last
    // week, against a program they are not running today, is a row in
    // the panel with a reason on it — and a config that killed the
    // launch over one would be persistence taking the game away. #382
    // could already produce exactly that file through `--remember`.
    if (opts.seams_from_config) {
      std::fprintf(stderr,
                   "amberfolio: seam %s was remembered, not asked for on"
                   " this command line - carrying on without it\n",
                   id.c_str());
      continue;
    }
    return false;
  }
  return true;
}

void report_seam_listing(const machine::machine& box) {
  const machine::seam_engine& seams = box.seams();
  for (std::size_t i = 0; i < seams.count(); ++i) {
    const machine::seam_status row = seams.status(i);
    std::fprintf(stderr, "amberfolio: seams %.*s %s%s%s%s - %.*s\n",
                 static_cast<int>(row.id.size()), row.id.data(),
                 machine::seam_state_name(row.state),
                 row.state == machine::seam_state::on
                     ? (row.armed ? " armed" : " inert")
                     : "",
                 row.reason == machine::seam_reason::none ? "" : " ",
                 row.reason == machine::seam_reason::none
                     ? ""
                     : machine::seam_reason_name(row.reason),
                 static_cast<int>(row.about.size()), row.about.data());
    // What the seam is gated on, on a line of its own and only when
    // there is one (#171). A person reading a listing needs to know
    // that a seam is waiting on a document *before* they wonder why it
    // is inert — and every seam in this build says `no document`
    // today, so the line would otherwise be noise on every row.
    if (const machine::seam_definition* definition = seams.find(row.id);
        definition != nullptr &&
        definition->gate != machine::document_kind::none) {
      std::fprintf(stderr, "amberfolio: seams %.*s needs the %s (--document)\n",
                   static_cast<int>(row.id.size()), row.id.data(),
                   machine::document_kind_name(definition->gate));
    }
  }
  // And what has been shown to it, so a listing says the whole state
  // it is a listing of.
  for (std::size_t i = 0; i < seams.document_count(); ++i) {
    const machine::document_edition* held = seams.document_at(i);
    std::fprintf(stderr, "amberfolio: seams holding %.*s (%s)\n",
                 static_cast<int>(held->name.size()), held->name.data(),
                 machine::document_kind_name(held->kind));
  }
}

void report_seam_outcomes(const machine::machine& box) {
  // What each enabled seam actually did (#131). `armed` says an address
  // was computed; this says a handler ran there. A seam that is on and
  // armed and fired nothing is the failure that reads exactly like
  // success, and the only place a reader can be told about it for free is
  // here, once, at the end of the run it belongs to.
  //
  // A triggered seam (#161) carries two more numbers, and only it does:
  // `reached` is how many times its **addressed** point was arrived at
  // whether or not anybody had asked — the one measurement of how
  // promptly a pull could be served there — and `waited`/`waiting` is
  // what the last pull cost or is still costing.
  //
  // What the row *means* is `machine::seam_reading_of`, in core, and not
  // a decision made here (#163). It used to be made here and again in
  // `host.mjs`, and both got it wrong the same way the moment a seam
  // could act at a point with no address: `fired=1 reached=0` is a
  // success, and both printed "armed and never reached; its point may
  // not be where its facts say" over it. One decision, one spelling, and
  // the page is handed the finished sentence through the ABI — so a
  // reader comparing a browser run with a desktop one is comparing two
  // runs and not two spellings.
  for (std::size_t i = 0; i < box.seams().count(); ++i) {
    const machine::seam_status row = box.seams().status(i);
    if (row.state != machine::seam_state::on) {
      continue;
    }
    std::string extra;
    if (row.trigger) {
      extra += " reached=" + std::to_string(row.reached);
      if (row.waiting) {
        extra += " waiting";
      } else if (row.fired != 0) {
        extra += " waited=" + std::to_string(row.waited);
      }
    }
    // What the row means is core's answer, not this host's (#163).
    const char* say = machine::seam_reading_text(machine::seam_reading_of(row));
    std::fprintf(stderr, "amberfolio: seam %.*s %s fired=%llu%s%s\n",
                 static_cast<int>(row.id.size()), row.id.data(),
                 row.armed ? "armed" : "inert",
                 static_cast<unsigned long long>(row.fired), extra.c_str(),
                 say);
  }

  // Whether the run ended inside the program's line editor (#504), when
  // something is watching: the answer a page polls every frame, once.
  const machine::text_entry reading = machine::text_entry_now(box);
  if (reading != machine::text_entry::unknown) {
    std::fprintf(
        stderr, "amberfolio: text-entry %s\n",
        reading == machine::text_entry::reading ? "reading" : "not-reading");
  }
}

void report_host_services(const machine::machine& box,
                          const host::host_services& services) {
  // And what the seams asked of the host (M5-D1, #169). A line per
  // service that was called, and none for one that was not — which is
  // the whole of what the polled count is for: "it never asked" is a
  // finding, and the difference between it and "it asked and nobody
  // answered" is invisible in any stream (#153). The count is the
  // engine's (machine/seam.h); the tick beside it is this host's own
  // object's, and is the fact only a synchronous implementation can
  // have — the machine's virtual time at the instant of the call.
  for (std::size_t i = 0; i < machine::seam_host_service_count; ++i) {
    const auto which = static_cast<machine::seam_host_service>(i);
    const std::uint64_t calls = box.seams().host_calls(which);
    if (calls == 0) {
      continue;
    }
    std::fprintf(stderr,
                 "amberfolio: host-service %s calls=%llu last=%lu at=%llu\n",
                 machine::seam_host_service_name(which),
                 static_cast<unsigned long long>(calls),
                 static_cast<unsigned long>(box.seams().host_argument(which)),
                 static_cast<unsigned long long>(services.record(which).at));
  }
}

void report_sidecars(const host::host_services& services) {
  // And what the sidecars did with them (M5-E2c #173, #351). Printed
  // only when they were asked for, and printed even when they did
  // nothing: a store that wrote no file is either a run that explored
  // nothing and was cited nothing or a run whose writes were failing, and
  // those are not the same thing.
  if (services.slots().enabled()) {
    const host::slot_store& store = services.slots();
    const char slot = store.slot();
    std::fprintf(stderr,
                 "amberfolio: save-sidecars writes=%lu reads=%lu slot=%c"
                 " trouble=%s\n",
                 static_cast<unsigned long>(store.writes()),
                 static_cast<unsigned long>(store.reads()),
                 slot != 0 ? slot : '-',
                 host::slot_trouble_name(store.trouble()));
  }
}

}  // namespace amberfolio::sdl
