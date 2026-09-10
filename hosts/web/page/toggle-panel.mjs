// SPDX-License-Identifier: AGPL-3.0-only
//
// The toggle panel's model, in the page (#383).
//
// One panel on both hosts, the same five facts in the same order and in
// the same words: **name, state, fired, reason, gate**. The desktop half
// is `hosts/sdl/src/seam_panel.{h,cpp}`, which argues each of the five
// and why `fired` is a number; this is the browser's, and the two are
// deliberately spelled the same so that a player who reads a forum post
// about one is reading about the other.
//
// DOM-free, like host.mjs and persist.mjs and for their reason: `ctest
// --preset wasm` imports this under node, where there is no `document`.
// The row, the stored choice and what a refusal costs are plain
// functions here; `app.mjs` turns rows into a table and is the only part
// a test cannot reach.
//
//
// Off by default has to survive persistence
// -----------------------------------------
//
// `storedSeams()` is where that lives. A record that is not an array of
// strings — missing, `null`, a number, an object an older build wrote, a
// string somebody hand-edited into the database — is **no choice**, and
// no choice is every seam off. There is no reading of a stored value
// that means "leave the defaults alone and inherit whatever is on",
// because the default *is* off and a player who has never opened this
// panel must find it that way (CLAUDE.md's fidelity invariant).
//
// And a choice that is stored is applied through the same `seamEnable()`
// a click takes, with the refusal reported — never by assuming it took.
// `applyStoredSeams()` is that, and it answers what was refused so the
// page can say so: a remembered seam this program has no addresses for
// is a row with a reason on it, not a silent nothing and not a launch
// that stops.

import { AF_OK, AF_SEAM_ON, AF_SEAM_UNAVAILABLE } from './host.mjs';

/// The columns, in order, as `{ key, heading, numeric }`.
///
/// A table rather than five hand-written cells so that the page and this
/// file cannot disagree about how many there are, and `numeric` is what
/// puts `fired` under a right-aligned heading — the point of the number
/// (#131, #163) is that a reader spots a zero among the others, which a
/// ragged column defeats.
export const PANEL_COLUMNS = Object.freeze([
  { key: 'id', heading: 'seam', numeric: false },
  { key: 'state', heading: 'state', numeric: false },
  { key: 'fired', heading: 'fired', numeric: true },
  { key: 'reason', heading: 'reason', numeric: false },
  { key: 'gate', heading: 'waits for', numeric: false },
]);

/// What core says for a seam that waits on no document. Mapped to `-`
/// so the column reads the way the desktop panel's does; every seam in
/// this build says it, and will until #384's document control lands.
const NO_GATE = 'no document';

/// The five facts about one seam, out of a `Machine.seamList()` row.
///
/// The spellings are the desktop panel's, character for character:
/// `off` / `on armed` / `on inert` / `unavailable`, a `fired` that is a
/// number, core's own word for a reason and `-` for none.
///
/// **The reason is never paraphrased.** A panel that showed `off` where
/// core said `document_not_presented` would be throwing away the part a
/// player can act on, which is the whole complaint #383 was filed over.
export function panelRow(seam) {
  const on = seam.state === AF_SEAM_ON;
  const available = seam.state !== AF_SEAM_UNAVAILABLE;
  let state = 'off';
  if (seam.state === AF_SEAM_UNAVAILABLE) state = 'unavailable';
  // The two claims an enabled seam can make, and they are different:
  // `armed` says an address was computed out of the fact table, `inert`
  // says the module it lives in is not resident yet.
  else if (on) state = seam.armed ? 'on armed' : 'on inert';
  const reason = !seam.reason || seam.reason === 'none' ? '-' : seam.reason;
  const gate = !seam.gate || seam.gate === NO_GATE ? '-' : seam.gate;
  // `seam_reading_text()`'s sentence without the ` - ` it arrives with:
  // the panel supplies its own separator, exactly as the desktop one
  // does.
  const reading = (seam.reading ?? '').startsWith(' - ')
    ? seam.reading.slice(3)
    : (seam.reading ?? '');
  return {
    id: seam.id,
    about: seam.about,
    state,
    fired: Math.round(seam.fired ?? 0),
    reason,
    gate,
    reading,
    on,
    available,
    trigger: Boolean(seam.trigger),
  };
}

/// Every seam, as rows.
export function panelRows(machine) {
  return machine.seamList().map(panelRow);
}

/// What a stored record means, fail-closed.
///
/// Answers an array of ids, or **null for no choice at all**. Anything
/// that is not an array of non-empty strings is no choice: a record from
/// a build that kept something else, a value a person put there by hand,
/// a `null` from a browser that keeps nothing. Duplicates are dropped
/// and the order the record gives is kept, because the order a player
/// turned them on in is the only order there is.
///
/// Null and `[]` are different answers and the page prints different
/// sentences for them — "you have never chosen" and "you chose none" —
/// but they turn on exactly the same seams, which is none.
export function storedSeams(value) {
  if (!Array.isArray(value)) return null;
  const ids = [];
  for (const entry of value) {
    if (typeof entry !== 'string' || entry === '') return null;
    if (!ids.includes(entry)) ids.push(entry);
  }
  return ids;
}

/// The seams that are on right now, as the record to store, or null when
/// none is.
///
/// Null rather than `[]` for a player who has turned everything back
/// off, so that the record they leave behind is the record a player who
/// never opened the panel has. There is one meaning of "off" and it does
/// not depend on how you got there.
export function seamsToStore(rows) {
  const on = rows.filter((row) => row.on).map((row) => row.id);
  return on.length === 0 ? null : on;
}

/// A stored choice applied to a machine, through the same `seamEnable()`
/// a click takes.
///
/// Answers `{ on, refused }` — the ids that took, and `{ id, reason }`
/// for each that did not, with core's reason on it. A refusal is
/// reported and never fatal: a player who turned a seam on for one
/// program and has loaded another today gets a row with a reason, not a
/// page that will not come up. The desktop host makes the same
/// distinction between a remembered choice and a `--seam` flag
/// (hosts/sdl/src/main.cpp).
export function applyStoredSeams(machine, ids) {
  const on = [];
  const refused = [];
  for (const id of ids ?? []) {
    if (machine.seamEnable(id) === AF_OK) {
      on.push(id);
      continue;
    }
    const after = machine.seamList().find((seam) => seam.id === id);
    refused.push({ id, reason: after?.reason ?? 'unknown_seam' });
  }
  return { on, refused };
}
