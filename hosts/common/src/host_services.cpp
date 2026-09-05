// SPDX-License-Identifier: AGPL-3.0-only
//
// The two services, served. host_services.h has the reasoning; this is
// the whole of what M5-D1 (#169) asked for above it.

#include "amberfolio/host/host_services.h"

#include <cstddef>
#include <cstdint>

#include "amberfolio/host/code_wheel_store.h"
#include "amberfolio/machine/journal.h"
#include "amberfolio/machine/machine.h"
#include "amberfolio/machine/seam.h"

namespace amberfolio::host {

void host_services::serve(machine::machine& box,
                          machine::seam_host_service which,
                          std::uint32_t argument) {
  // The one read, and the reason this runs where it runs: the machine's
  // own virtual clock, at the instant the seam called out. Everything
  // else a consumer will want — the party's position, the entry a
  // journal point named — is read the same way and by the enhancement
  // that wants it (#173, #175).
  host_service_record& seen = records_[static_cast<std::size_t>(which)];
  seen.seen = true;
  seen.argument = argument;
  seen.at = box.time();

  // And the consumers. The exploration store (M5-E2c, #173) is off unless
  // a host asked for it, so on every run that has not this is a branch and
  // nothing else.
  if (which == machine::seam_host_service::automap_update) {
    automap_.changed();
    return;
  }

  // A person answered the code-wheel challenge (M6-C1b, #292). What the
  // seam latched lives as long as the machine; this is where it starts
  // outliving it. The *copy* is the key, and the machine is holding it:
  // the fingerprint of the program that asked the question, which is the
  // fingerprint the seam is keyed to.
  //
  // A store that already knew answers false and nothing is raised, so a
  // second run that says the same thing does not make a host rewrite a
  // file.
  if (which == machine::seam_host_service::code_wheel_answered) {
    if (code_wheel_ != nullptr && box.seams().have_program()) {
      static_cast<void>(code_wheel_->remember(box.seams().program()));
    }
    return;
  }

  // The journal's log moved (M5-E4b, #222). What changed is in the
  // machine's own log, which is observation rather than machine state, so
  // this copies it into the store - the thing that outlives the machine -
  // and clears the flag that said there was something to copy.
  if (which == machine::seam_host_service::journal_seen) {
    if (journal_ != nullptr) {
      journal_->set_seen(box.journal().seen());
    }
    box.journal().set_seen_changed(false);
    return;
  }

  // The journal reader (M5-E4, #175). The answer goes into the machine's
  // own delivery buffer rather than back through `serve()`, which has no
  // way to carry it; `machine/journal.h` is why that buffer is not machine
  // state and why a host may write it.
  //
  // Three refusals and one answer, and each refusal is a different thing
  // for a player to do about it: nobody has read a journal, this journal
  // has no such entry, or the entry is there and the engine read nothing
  // off it (`journal_trouble` makes the same distinctions one layer down).
  if (which == machine::seam_host_service::journal_open) {
    machine::journal_state& page = box.journal();
    if (journal_ == nullptr || journal_->empty()) {
      page.refuse(machine::journal_delivery::no_journal);
      return;
    }
    // The argument is a *pair* since #218 — a section and a number packed
    // into the one word the callout carries. An argument that does not
    // decode to a citation this build knows is refused exactly like a
    // number that names nothing, because that is what it is.
    const machine::journal_citation what =
        machine::journal_open_citation(argument);
    if (!what) {
      page.refuse(machine::journal_delivery::no_entry);
      return;
    }
    const journal_text* found = journal_->find(what);
    if (found == nullptr) {
      page.refuse(machine::journal_delivery::no_entry);
      return;
    }
    // `deliver()` answers `no_text` for an entry with nothing in it, so
    // the empty case needs no branch of its own here.
    page.deliver(found->text());
  }
}

}  // namespace amberfolio::host
