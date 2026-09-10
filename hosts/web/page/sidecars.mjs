// SPDX-License-Identifier: AGPL-3.0-only
//
// The one question this page asks a player (#385).
//
// `saveSidecars(on)` writes `\SAVE\AFMAP.DAT` and `\SAVE\AFSEEN.DAT`
// into the filesystem holding the copy somebody dropped — the same two
// files the desktop host's `--save-sidecars` writes, and the same
// sentence being asked for: *may this build keep its own files beside
// your saves*. It is the one surface in M6 that changes something of the
// player's, so it is asked for rather than assumed, once, and the answer
// is kept with everything else this browser remembers (`persist.mjs`).
//
// DOM-free, like `persist.mjs` and `journal.mjs` and for the same
// reason: `ctest --preset wasm` imports this under node, where there is
// neither a `document` nor an `indexedDB`. The buttons and the panel are
// `app.mjs`'s; what is here is the two decisions — *what the question
// says*, and *what an answer to it was* — plus the guard that says
// whether this visit is one to ask in at all.
//
//
// Why a page and a terminal say the same thing
// -------------------------------------------
//
// The desktop host's `sidecar_consent.h` is this file's other half, and
// the two texts are deliberately the same claim in the same order: what
// is being kept, where it lands, whose directory that is, and what does
// *not* happen. A player who reads one and then meets the other should
// not have to work out whether they are being asked two things.
//
// One difference, and it is the browser's own: what the page writes into
// is a filesystem inside this browser and not a directory on a disk, so
// the sentence about "your game directory" is the sentence about the copy
// this browser is keeping. The files are still theirs and still
// deletable — *Forget everything* is where they go.
//
//
// Silence is neither a yes nor a no
// --------------------------------
//
// `readSidecarAnswer` refuses everything that is not exactly `true` or
// `false`: nothing kept yet, a record from a later build, a value some
// other tab put there. A page that read an absent setting as consent
// would be writing files on the strength of a click that never happened,
// and one that read it as a refusal would write down an answer nobody
// gave and stop the question ever being asked again.
//
//
// Once, at the boot, and never twice
// ---------------------------------
//
// The *applying* is not here, and where it happens is a rule rather than
// a convenience: `saveSidecars(true)` turns the store on **and attaches
// it**, and the attach reads the working exploration table with
// `read_sidecar`, which replaces every record in the machine. So a second
// call part-way through a visit would hand a player an older map than the
// one they are looking at. `app.mjs` calls it once, at the boot, after
// the files are in and before `loadFromVfs()` — which is also the only
// moment there is a filesystem to read and nothing has been drawn yet.
// A click on the panel therefore records an answer and takes effect on
// the next boot, and the panel says so.

/// Where the answer is kept in the `settings` store (`persist.mjs`).
/// This page's own choice, and nothing the machine can see.
export const SIDECARS_SETTING = 'save-sidecars';

/// What this visit should do about the question, and why. The desktop's
/// `sidecar_ask` without the four kinds of driven run, which a page does
/// not have: `tools/drive.mjs` is the driven path in this host and it
/// says `--save-sidecars` on its own command line.
export const SIDECARS_ASK = 'ask';
export const SIDECARS_ALREADY_ANSWERED = 'already-answered';
export const SIDECARS_NOWHERE_TO_REMEMBER = 'nowhere-to-remember';
export const SIDECARS_NOTHING_TO_WRITE_BESIDE = 'nothing-to-write-beside';

/// What somebody chose, out of whatever the drawer had: `true`, `false`,
/// or `null` for **everything else**, an absent record and a record this
/// build does not understand included.
export function readSidecarAnswer(kept) {
  return kept === true || kept === false ? kept : null;
}

/// Whether this visit is the one that asks.
///
/// The order is the desktop's and is the precedence this page already
/// keeps: what was remembered first, then whether there is anywhere to
/// remember an answer, then whether there is anything for a sidecar to go
/// beside. A question whose answer cannot be written down is a question
/// asked again every visit, which is not asking once — so a browser
/// keeping nothing is told what the setting is rather than asked to
/// choose it.
export function shouldAskAboutSidecars({ answered, canRemember, haveDisk }) {
  if (readSidecarAnswer(answered) !== null) return SIDECARS_ALREADY_ANSWERED;
  if (!canRemember) return SIDECARS_NOWHERE_TO_REMEMBER;
  if (!haveDisk) return SIDECARS_NOTHING_TO_WRITE_BESIDE;
  return SIDECARS_ASK;
}

/// The question, as the lines a panel renders.
///
/// Here rather than in the page so a test can hold down the two things
/// this text has to say, both of which are facts a player acts on: the
/// **names of the files** that will appear, and that the answer is
/// **remembered**. The third — that nothing is written until there is
/// something to put in it — is true because `host/slot_store.h` makes it
/// true, and saying it here is what keeps the two in step.
export function sidecarQuestion() {
  return [
    'Two of the enhancements learn something as you play: which streets ' +
      'the automap has drawn for you, and which journal entries the game ' +
      'has sent you to. Neither survives the machine stopping.',
    'Kept, they go in \\SAVE\\AFMAP.DAT and \\SAVE\\AFSEEN.DAT - files of ' +
      "this project's own, beside your saved games in the copy this " +
      'browser is keeping and never inside one. The files you dropped are ' +
      'never written to, and neither of these appears at all until there ' +
      'is something to put in it.',
    'This browser remembers what you answer, and you can change it here ' +
      'whenever you like. A change takes effect the next time you boot.',
  ];
}

/// The line above the two buttons: what is chosen now, in a sentence.
///
/// Every state says what will happen rather than what was stored, because
/// "not answered" is a thing a player can act on and `null` is not.
export function sidecarStatus(answer) {
  const chosen = readSidecarAnswer(answer);
  if (chosen === true) {
    return 'kept beside your saves - your map and your read journal ' +
      'entries come back next visit';
  }
  if (chosen === false) {
    return 'not kept - your map and your read journal entries go when the ' +
      'machine stops';
  }
  return 'not answered yet - nothing is written beside your saves until ' +
    'you say so';
}
