// SPDX-License-Identifier: AGPL-3.0-only
//
// The document control, in the page (#384).
//
// One control on each host that takes any PDF, hashes it, and says
// either what edition it was recognised as — naming the seam rows that
// were waiting on it — or that nobody here has fingerprinted this one,
// **and here is its SHA-256**. The desktop half is
// `hosts/sdl/src/document_control.{h,cpp}`, which argues both outcomes
// at length; this is the browser's, and the two are deliberately spelled
// the same, word for word, so that a player who reports one line is
// reporting a line the other host would have printed.
//
// DOM-free, like host.mjs and toggle-panel.mjs and for their reason:
// `ctest --preset wasm` imports this under node, where there is no
// `document`. Reading the file and putting the answer on the page is
// `app.mjs`'s.
//
//
// The unrecognised outcome is the one that matters
// ------------------------------------------------
//
// `presentDocument()` returns the fingerprint whichever way it went, and
// that is the point of it (`machine/document.h`, PLAN.md §9): a player
// holding an edition nobody has fingerprinted can still be shown the
// hash, which is the part they can act on. A control that said "not
// recognised" and stopped there would have thrown the actionable half
// away, and one that guessed at which edition it might be would be a
// gate that armed on anything, one layer up (CLAUDE.md's "log, don't
// fake").
//
// Nothing here keeps the document. The bytes are hashed inside the
// module and dropped; this page never stores a PDF, never parses one,
// and a possession gate is over bytes and nothing else. So a document is
// shown once per visit, and the page says so rather than pretending to
// remember it.

import { AF_OK } from './host.mjs';

/// The gate a seam that waits on nothing reports — core's own words,
/// which `panelRow()` maps to `-` for its column and which is compared
/// against here rather than against that dash.
const NO_GATE = 'no document';

/// Which seams wait on `kind`, by id, in the listing's own order.
///
/// The rows this document lights: gated on the kind that just arrived,
/// **whether or not the player has turned them on**, because a gated
/// seam nobody has enabled yet is still a row this document is for.
/// Today the answer is always none — since #290 no seam in this build is
/// gated, the code-wheel bypass having become a question a person
/// answers — and saying that plainly is better than a feature that
/// looks broken.
export function seamsWaitingOn(rows, kind) {
  if (!kind || kind === NO_GATE) return [];
  return rows.filter((row) => row.gate === kind).map((row) => row.id);
}

/// Present `bytes` and say what they turned out to be.
///
/// Answers `{ status, recognized, fingerprint, name, kind, waiting }`.
/// `status` is the ABI's: `AF_OK`, `AF_UNRECOGNIZED`, or `AF_INVALID`
/// for a file with no bytes in it — which is not one of the control's
/// two outcomes, because nothing was hashed and there is no fingerprint
/// to report.
///
/// The name and the kind come from **core**, out of the machine that
/// just recognised the document (`documentsHeld()`), rather than from a
/// table this page keeps: two tables spelling the same document
/// differently is exactly what the ABI is here to prevent.
export function showDocument(machine, bytes) {
  const { status, fingerprint } = machine.presentDocument(bytes);
  const out = {
    status,
    recognized: status === AF_OK,
    fingerprint: fingerprint ?? '',
    name: '',
    kind: '',
    waiting: [],
  };
  if (status !== AF_OK) return out;
  // The one just presented is the last one held: a document already
  // shown is not held twice (`seam_engine::present_document`), so a
  // second showing of the same file leaves the list as it was and the
  // last entry is still this document.
  const held = machine.documentsHeld().at(-1) ?? { name: '', kind: '' };
  out.name = held.name;
  out.kind = held.kind;
  out.waiting = seamsWaitingOn(
    machine.seamList().map((seam) => ({ id: seam.id, gate: seam.gate })),
    held.kind,
  );
  return out;
}

/// `n seams`, or `1 seam`. A count in a sentence is read as a sentence,
/// and `1 seams` reads as a bug in the thing that printed it.
function seamsPlural(n) {
  return `${n} ${n === 1 ? 'seam' : 'seams'}`;
}

/// The outcome as the lines a player reads — **the desktop host's,
/// character for character** (`document_lines()` in
/// `hosts/sdl/src/document_control.cpp`; `docs/hosts.md` §9 writes the
/// wording down for both).
///
/// Two lines: what the document is, and what it is *for* here. Neither
/// carries a host's prefix; a caller puts the characters somewhere a
/// person can read them, and a status line and a console do that
/// differently.
export function describeDocument(outcome) {
  if (!outcome.recognized) {
    return [
      `document unrecognized sha256=${outcome.fingerprint}` +
        ' - no gate is satisfied by it',
      'nobody here has fingerprinted this one - that sha256 is what an' +
        ' entry in the table is made of',
    ];
  }
  const first =
    `document ${outcome.name} (${outcome.kind})` +
    ` sha256=${outcome.fingerprint}`;
  if (outcome.waiting.length === 0) {
    return [first, `nothing in this build waits on the ${outcome.kind}`];
  }
  return [
    first,
    `the ${outcome.kind} lights ${seamsPlural(outcome.waiting.length)}: ` +
      outcome.waiting.join(', '),
  ];
}

/// What a file with nothing in it gets: not an outcome, because nothing
/// was hashed. Spelled once here so the page and `tools/drive.mjs` say
/// it the same way.
export const DOCUMENT_NO_BYTES = 'that file has no bytes in it - nothing was hashed';
