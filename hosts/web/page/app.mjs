// SPDX-License-Identifier: AGPL-3.0-only
//
// Browser-only glue for the dev page: canvas presentation, keyboard
// input, the AudioWorklet wiring, the console <pre> sink, and — since
// M3-F2 (#84) — the player's own directory.
//
// Deliberately separate from host.mjs, which has to stay DOM-free so
// tests/smoke.mjs can import it under node (host.mjs's own top comment).
// The directory picker is separate again, in picker.mjs, for the same
// reason one layer down: reading a `File` is browser work and putting the
// bytes in the machine is not.
//
// This is still the bare dev page PLAN.md §7 asks for, not the M6
// reference shell: no onboarding, no persistence, no touch, no styling
// worth the name. What #84 added is one affordance — getting a directory
// into the machine — because M3's exit criterion is "verified locally on
// desktop **and** web" and there was previously no way to put a player's
// files in front of the browser at all.
//
//
// Two things it can run
// ---------------------
//
// The embedded demo program (hosts/web/src/demo_program.cpp), which is
// what M2-H2 built this page to prove, and a program out of a directory
// the player chose. One run per page load either way: this page has no
// business tearing a machine down and standing another one up, and the
// ABI has one machine per module anyway (abi.h).
//
//
// Why everything waits for a gesture
// -----------------------------------
//
// Browsers refuse to start an AudioContext without a user gesture, and a
// page that ran the machine before the button existed to explain why
// nothing is audible yet would be confusing about the one thing this page
// exists to demonstrate. Choosing a directory is a gesture too, which is
// why the module is instantiated there: by the time Boot is pressed, the
// files are already in the machine and the audio context may start.

import {
  loadAmberfolio,
  Machine,
  loadDemoProgram,
  scancodeFor,
  HeldKeys,
  decodeConsoleBytes,
  SPEED_PRESETS,
  AF_OK,
  AF_INVALID,
  AF_NO_ROOM,
  describeSkip,
  AF_SEAM_ON,
  formatSeamFired,
  AF_RUN_END_STOPPED,
  AF_RUN_END_STEP_BUDGET,
  AF_RUN_END_HOST_QUIT,
  pacedAdvance,
  MAX_CATCH_UP_SECONDS,
  wallClockFields,
  readScreenKeyboard,
  commitKey,
  moveFocus,
  releaseLatched,
  AF_NAV_LEFT,
  AF_NAV_RIGHT,
  AF_NAV_UP,
  AF_NAV_DOWN,
} from './host.mjs';
import { wireDirectoryPicker } from './picker.mjs';
import { describeMatch, loadEditions, matchEdition } from './editions.mjs';
import {
  ingestJournal,
  journalKind,
  journalNumber,
  loadEngine,
  keepStore,
  keepLog,
  restoreStore,
  restoreLog,
  citeAllJournal,
  forgetStore,
  forgetLog,
  clearStore,
  browserStorage,
  JOURNAL_STORE_KEY,
  JOURNAL_LOG_KEY,
} from './journal.mjs';
import {
  open as openDatabase,
  forgetEverything,
  textDrawer,
  cacheDrawer,
  describeRefusal,
  estimateStorage,
  partitionDisk,
  readBack,
  sized,
  DISK_STORE,
  PLAY_STORE,
  TEXT_STORE,
  SETTINGS_STORE,
  PROGRAM_SETTING,
  SEAMS_SETTING,
} from './persist.mjs';
import {
  applyStoredSeams,
  panelRows,
  seamsToStore,
  storedSeams,
  PANEL_COLUMNS,
} from './toggle-panel.mjs';
import {
  SIDECARS_SETTING,
  SIDECARS_ASK,
  SIDECARS_NOWHERE_TO_REMEMBER,
  SIDECARS_NOTHING_TO_WRITE_BESIDE,
  readSidecarAnswer,
  shouldAskAboutSidecars,
  sidecarQuestion,
  sidecarStatus,
} from './sidecars.mjs';

const CANVAS_ID = 'screen';
const KEYBOARD_ID = 'keyboard';
const KEYBOARD_SHOW_ID = 'keyboard-show';
const KEYBOARD_LAYOUT_ID = 'keyboard-layout';
const KEYBOARD_ABOUT_ID = 'keyboard-about';
const START_BUTTON_ID = 'start';
const BOOT_BUTTON_ID = 'boot';
const STATUS_ID = 'status';
const CONSOLE_ID = 'console';
const DIRECTORY_INPUT_ID = 'directory';
const DROP_ZONE_ID = 'drop';
const PROGRAM_SELECT_ID = 'program';
const TAIL_INPUT_ID = 'tail';
const STEPS_INPUT_ID = 'steps';
const TRACE_CHECKBOX_ID = 'trace';
const EDITION_ID = 'edition';
const SEAMS_ID = 'seams';
const SPEED_SELECT_ID = 'speed';
const VOLUME_INPUT_ID = 'volume';
const MUTE_CHECKBOX_ID = 'mute';
const HEALTH_ID = 'health';
const JOURNAL_INPUT_ID = 'journal';
const JOURNAL_STATUS_ID = 'journal-status';
const JOURNAL_FORGET_ID = 'journal-forget';
const JOURNAL_CITE_ALL_ID = 'journal-cite-all';
const CODE_WHEEL_STATUS_ID = 'code-wheel-status';
const CODE_WHEEL_FORGET_ID = 'code-wheel-forget';
const KEPT_STATUS_ID = 'kept-status';
const KEPT_FORGET_ID = 'kept-forget';
const SIDECARS_STATUS_ID = 'sidecars-status';
const SIDECARS_YES_ID = 'sidecars-yes';
const SIDECARS_NO_ID = 'sidecars-no';
const SIDECARS_ABOUT_ID = 'sidecars-about';

/// Where this browser remembers the copies whose code-wheel challenge
/// has been answered (M6-C1b, #292).
///
/// Versioned in the key for `JOURNAL_STORE_KEY`'s reason: a future
/// format this build could not parse is a different drawer rather than a
/// puzzle. What goes in it is the store's own text — a header line and a
/// digest per copy, and nothing else about anybody.
///
/// A record in the database's `text` store since #381, under the name it
/// had in `localStorage`: a drawer that moved house kept its label, which
/// is what lets a player's answer come across with it.
const CODE_WHEEL_STORE_KEY = 'amberfolio.code-wheel.store.v1';

/// The three strings this page keeps on behalf of a module that owns
/// them: the journal's transcription, the journal's read log and the code
/// wheel's answered copies. The order is the order they are migrated in
/// and means nothing else.
const TEXT_RECORDS = [
  JOURNAL_STORE_KEY,
  JOURNAL_LOG_KEY,
  CODE_WHEEL_STORE_KEY,
];

// --- What this browser keeps between visits (M6, #381) ------------------
//
// `persist.mjs` has the reasoning and the schema. What is here is the
// page's half: one open database, one drawer over its small strings, and
// the two sets that say what the two file stores are holding, so a
// write-back can tell a file that went away from one that never existed.
//
// At module scope for `el`'s reason — `ensureMachine()` and the frame
// loop both reach them, and neither is inside `runDevPage()`'s scope.
let kept = null;
let drawer = cacheDrawer();
let diskPaths = new Set();
let playPaths = new Set();

/// Whether this player said their progress may be kept beside their
/// saves (#385): `true`, `false`, or `null` for a question nobody has
/// answered yet. Read out of the `settings` drawer once the database is
/// open and written back the instant somebody presses one of the two
/// buttons, so the boot has an answer in hand without awaiting anything.
///
/// `null` is not a `false`, and the difference is the whole feature: a
/// refusal is remembered and never asked about again, while an unanswered
/// question keeps the sidecars off *and comes back*.
let sidecarAnswer = null;

/// Whatever this browser kept, opened once, awaited by everything that
/// might look at it. Answers what the page has to say about it.
async function openKeptStore() {
  const { kept: opened, why } = await openDatabase();
  kept = opened;
  const counts = { disk: 0, play: 0 };
  try {
    // `browserStorage()` is where an older visit of this page put the
    // journal and the code wheel; `textDrawer` brings them across once
    // and leaves them where they were.
    const { drawer: made, migrated } = await textDrawer(
      kept,
      TEXT_RECORDS,
      browserStorage(),
    );
    drawer = made;
    if (kept) {
      counts.disk = await kept.count(DISK_STORE);
      counts.play = await kept.count(PLAY_STORE);
      // And the one permission this page asks for (#385). Read here so
      // the boot has it without awaiting a database: a boot that had to
      // wait would be a boot that could arrive before the answer did,
      // and the answer has to be in hand *before* `loadFromVfs`.
      sidecarAnswer = readSidecarAnswer(
        await kept.get(SETTINGS_STORE, SIDECARS_SETTING),
      );
    }
    return { why, migrated, counts };
  } catch (problem) {
    return {
      why: describeRefusal(problem, {
        what: 'what an older visit left in this browser',
        estimate: await estimateStorage(),
      }),
      migrated: [],
      counts,
    };
  }
}

/// Whatever the drawer owes the database, written.
///
/// Answers `{ ok, why }`, and `why` is a whole sentence: this is the one
/// place a player's transcription or their answered copy can be refused,
/// and a quota is not an exception a page may swallow (CLAUDE.md). A
/// refused key stays owed, so the next change tries again.
async function flushText() {
  const changes = drawer.changes();
  if (changes.length === 0) return { ok: true, why: null };
  if (!kept) {
    return { ok: false, why: 'this browser keeps nothing, so nothing was kept' };
  }
  const puts = changes.filter(([, value]) => value !== null);
  const removes = changes
    .filter(([, value]) => value === null)
    .map(([key]) => key);
  let bytes = 0;
  for (const [, value] of puts) bytes += value.length;
  try {
    await kept.write(TEXT_STORE, puts, removes);
    drawer.settled(changes.map(([key]) => key));
    return { ok: true, why: null };
  } catch (problem) {
    return {
      ok: false,
      why: describeRefusal(problem, {
        what: 'what this browser read out of your own documents',
        bytes,
        estimate: await estimateStorage(),
      }),
    };
  }
}

/// One value in the `settings` store — this page's own choices, and
/// nothing the machine can see. Answers whether it landed.
///
/// It throws nothing and says nothing, deliberately: most of what is in
/// there is a convenience, and a page that made a fuss about failing to
/// remember which program you picked last time would be spending a
/// player's attention on the wrong thing. The **answer** is for the one
/// setting that is not a convenience — the permission in #385, where a
/// player told their answer was remembered and then asked again next
/// visit has been told something untrue — and the caller that cares says
/// so in its own words.
async function rememberSetting(key, value) {
  if (!kept) return false;
  try {
    if (value === null) await kept.write(SETTINGS_STORE, [], [key]);
    else await kept.write(SETTINGS_STORE, [[key, value]]);
    return true;
  } catch {
    return false;
  }
}

/// One value back out of the `settings` store, or null.
///
/// Null for a browser that keeps nothing, for a key nothing has written
/// and for a read that threw — three different things that mean the same
/// one thing to a caller, which is that nobody has chosen. What a
/// particular value *means* is the caller's; `storedSeams()` is the one
/// that matters and it is fail-closed on purpose.
async function readSetting(key) {
  if (!kept) return null;
  try {
    return await kept.get(SETTINGS_STORE, key);
  } catch {
    return null;
  }
}


/// The rate the speaker is rendered and played at. What a callback pulls
/// is not a fixed number of samples but however many this rate has in the
/// virtual time that callback advanced (#157) — audio and the machine
/// have to come off the same clock, and since that advance is now
/// measured against the wall rather than counted in callbacks, a fixed
/// chunk would be four times too much audio on a 240 Hz display.
const AUDIO_SAMPLE_RATE = 44100;

/// One element of the page, by id. At module scope because the functions
/// below `runDevPage()` use it too — `restoreCodeWheelStore()` and the
/// frame loop's code-wheel line, both added with the code wheel's drawer
/// (M6-C1b, #292) — and a `const` inside `runDevPage()` is not in their
/// scope. So the first `ensureMachine()` threw `el is not defined`, which
/// is what the deployed page answered a player who picked their own
/// journal: *the ingestion failed: el is not defined*, before an engine
/// was so much as looked for (#306). The second pick worked, because the
/// machine had been made before the throw.
const el = (id) => document.getElementById(id);

/// Wires the page up. Called once, from index.html's own inline module
/// script.
export function runDevPage() {
  const canvas = el(CANVAS_ID);
  const startButton = el(START_BUTTON_ID);
  const bootButton = el(BOOT_BUTTON_ID);
  const statusEl = el(STATUS_ID);
  const consoleEl = el(CONSOLE_ID);
  const programSelect = el(PROGRAM_SELECT_ID);
  const speedSelect = el(SPEED_SELECT_ID);
  const volumeInput = el(VOLUME_INPUT_ID);
  const muteCheckbox = el(MUTE_CHECKBOX_ID);
  const healthEl = el(HEALTH_ID);

  const setStatus = (text) => {
    if (statusEl) statusEl.textContent = text;
  };

  const appendConsole = (text) => {
    if (consoleEl) {
      consoleEl.textContent += text;
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }
    if (text.length > 0) console.log('[amberfolio]', text);
  };

  // The one machine, made on whichever gesture comes first. `Machine`
  // throws if a second one is asked for (abi.h: one machine per module),
  // so this is also what keeps the two entry points from colliding.
  let loaded = null;
  let machine = null;
  let started = false;

  // What this browser kept, opened before anything asks for it (#381).
  // Started here rather than lazily because the answer decides whether
  // the page has a disk to put back, and a player who dropped one last
  // week should not have to press anything to get it.
  let keptReady = openKeptStore();

  const ensureMachine = async () => {
    if (machine) return machine;
    // The database first, always: the journal, the code wheel and the
    // disk are all read out of the drawer this settles, and a machine
    // made before it would come up with an empty one. What it had to say
    // was said at page load, below.
    await keptReady;
    setStatus('loading the wasm module...');
    loaded = await loadAmberfolio({ print: appendConsole, printErr: appendConsole });
    for (const line of loaded.output) appendConsole(`${line}\n`);

    machine = new Machine(loaded.module);
    const attached = machine.attachReferenceDevices();
    if (attached !== AF_OK) {
      throw new Error(`af_machine_attach_reference_devices() answered ${attached}`);
    }
    // The RESET line, once the devices are on the bus — the same thing
    // the SDL host's own wiring does before it loads anything
    // (hosts/sdl/src/main.cpp's `wired_machine`).
    //
    // It is not decoration. `reset()` blanks the frame and republishes
    // it, which advances the generation counter, so a machine that was
    // reset and one that was not are one frame apart forever after. That
    // difference shows up in the `frames=` field of the stop report, and
    // M3's exit criterion is that this host and the desktop one print the
    // same line at the same step (#84) — so the two have to power on the
    // same way, not merely run the same way.
    machine.reset();
    const { major, minor, patch } = loaded.version;
    appendConsole(`[host] amberfolio ${major}.${minor}.${patch}\n`);
    if (speedSelect && speedSelect.value !== 'xt') applySpeed();

    // And what day it is out in the world, before the machine has taken
    // a step (#320). Said again the moment it starts running, which is
    // `seedWallClock`'s own comment and #343.
    //
    // Here as well as there because this one needs no program and no run:
    // the journal panel's *Cite them all* stamps every row off this
    // clock, and a person may press it on a tab that has never booted
    // anything. That is #352 in the browser, and a seed taken only at
    // the run would leave it reading 1 January 1980.
    seedWallClock(machine, appendConsole);

    // The journal this browser already read, back into the module's store
    // (M5-E3f). Here rather than at an ingestion because the point of it
    // is the visit where there *is* no ingestion: the in-game reader asks
    // the store, and a player who read their journal last week should
    // find it there without picking a file at all.
    //
    // After the module exists and before anything can look at the store,
    // which is the one moment that is both. It cannot land on top of an
    // ingestion — `restoreStore` declines when the store already holds
    // something — and a module that comes up twice is not a thing this
    // page does.
    //
    // Out of the database's `text` store since #381, through the drawer
    // `openKeptStore()` filled: `restoreStore` wants an answer at the
    // moment it is called and IndexedDB cannot give one, so the records
    // are read once and this is the drawer over them (`persist.mjs`).
    reportRestoredJournal(restoreStore(loaded.module, { storage: drawer }));
    // And the *read log*, out of its own drawer (#351) and then into the
    // machine, which is a second call because the store is the module's
    // and the log is the machine's (#237). Without the second a player's
    // `*` marks came back on the desktop and not here, which was a gap
    // rather than a decision; without the first there would be nothing
    // for it to put there, because the log left the store's own file
    // when it went beside the save it belongs to.
    restoreLog(loaded.module, { storage: drawer });
    machine.journalSeenRestore();

    // And what this browser remembers about the code wheel (M6-C1b,
    // #292): the copies whose challenge has been answered. Here for the
    // journal's reason — after the module exists and before anything can
    // look at the store — and the *applying* is later, at the load, when
    // there is a program with a fingerprint to look up.
    restoreCodeWheelStore(machine);

    // And the copy the player dropped, with their own files on top of it
    // (#381). Last, because it is the only one of the four that needs the
    // filesystem, and because a disk that could not be put back is a
    // sentence about this visit rather than about the machine.
    await restoreDisk(machine);
    return machine;
  };

  /// What to offer as bootable, out of `files` (`{ path, bytes }`).
  ///
  /// What went in, not `vfsList()`: the root listing is the root's, and
  /// since #146 an entry in it may be a directory `vfsPut` made on the
  /// way to a file below (abi.h). This page knows which of the things it
  /// handed over were files, because it handed them over — and since
  /// #381 the other caller is the restore, which knows for the same
  /// reason.
  ///
  /// Programs first, everything else after: a player wants the .EXE and
  /// should not have to hunt for it, and the rest is still offered
  /// because nothing here should be deciding what is and is not bootable.
  /// Ordering is the whole of what this looks at — the name itself goes
  /// to `loadFromVfs` as the player spelled it, for core to canonicalize.
  function offerPrograms(files) {
    const isProgram = (path) => /\.(exe|com)$/i.test(path);
    const ordered = [
      ...files.filter((file) => isProgram(file.path)),
      ...files.filter((file) => !isProgram(file.path)),
    ];
    programSelect.replaceChildren(
      ...ordered.map((file) => {
        const option = document.createElement('option');
        option.value = file.path;
        option.textContent = `${file.path} (${file.bytes.length} bytes)`;
        return option;
      }),
    );
    programSelect.disabled = ordered.length === 0;
    bootButton.disabled = ordered.length === 0;
    return ordered;
  }

  // --- The disk, kept between visits (M6, #381) --------------------------
  //
  // `persist.mjs` argues the schema; this is the page's use of it. Three
  // moments: the drop writes the copy, a reload puts it back, and a run
  // writes back whatever the *player* made — which is the save layer's
  // question and not this page's (docs/hosts.md §6).

  /// The files the machine holds, into the `disk` store, replacing
  /// whatever was there.
  ///
  /// Read back off the machine rather than out of the list the picker
  /// handed over, which is the point of `vfsList()`/`vfsGet()` and the
  /// reason #170 opened them: what goes in the database is what actually
  /// landed on the filesystem, spelled the way core canonicalized it, so
  /// putting it back next visit reaches the same paths.
  async function keepDisk(box) {
    if (!kept) return;
    const paths = box.vfsList().map((entry) => entry.path);
    const { records, unreadable, bytes } = readBack(box, paths);
    if (unreadable.length > 0) {
      appendConsole(
        `[host] ${unreadable.length} file(s) would not read back off the ` +
          'machine and are not being kept: ' +
          `${unreadable.join(', ')}\n`,
      );
    }
    try {
      await kept.write(DISK_STORE, records, [], { emptyFirst: true });
      diskPaths = new Set(records.map(([path]) => path));
      appendConsole(
        `[host] kept ${records.length} file(s) (${sized(bytes)}) in this ` +
          'browser - this copy comes back on its own next visit\n',
      );
      sayWhatIsKept();
    } catch (problem) {
      const why = describeRefusal(problem, {
        what: 'the copy you dropped',
        bytes,
        estimate: await estimateStorage(),
      });
      appendConsole(`[host] ${why}\n`);
      sayWhatIsKept(why);
    }
  }

  /// The `play` store back onto the machine's filesystem.
  ///
  /// After the copy and never before it: a saved game is written over the
  /// slot the copy shipped with, and the other order would put the
  /// publisher's empty slot over the player's party.
  async function restorePlayFiles(box) {
    if (!kept) return 0;
    const records = await kept.all(PLAY_STORE);
    let put = 0;
    for (const [path, bytes] of records) {
      if (box.vfsPut(path, bytes) === AF_OK) put += 1;
      else {
        appendConsole(
          `[host] ${path} was kept in this browser and would not go back ` +
            'onto the machine\n',
        );
      }
    }
    playPaths = new Set(records.keys());
    return put;
  }

  /// Everything this browser holds, back onto a fresh machine.
  ///
  /// A `function` rather than a `const` because `ensureMachine` calls it
  /// and is written above it, which is the same hoisting
  /// `reportRestoredJournal` relies on.
  async function restoreDisk(box) {
    if (!kept) return;
    let files;
    try {
      files = await kept.all(DISK_STORE);
    } catch (problem) {
      appendConsole(
        `[host] this browser would not hand back the copy it kept ` +
          `(${problem?.name ?? problem})\n`,
      );
      return;
    }
    if (files.size === 0) {
      // Still worth doing: a player may have saves kept from a copy they
      // have since dropped again in another tab, and the honest thing is
      // to put them where the game will look.
      const alone = await restorePlayFiles(box);
      if (alone > 0) {
        appendConsole(
          `[host] ${alone} file(s) of your own came back, but the copy they ` +
            'belong to did not - drop your game directory again\n',
        );
      }
      sayWhatIsKept();
      return;
    }
    let bytes = 0;
    const taken = [];
    const refused = [];
    for (const [path, content] of files) {
      const status = box.vfsPut(path, content);
      if (status === AF_OK) {
        taken.push({ path, bytes: content });
        bytes += content.length;
      } else {
        refused.push(`${path} (${describeSkip(status)})`);
      }
    }
    diskPaths = new Set(taken.map((file) => file.path));
    const own = await restorePlayFiles(box);
    appendConsole(
      `[host] this browser had your copy: ${taken.length} file(s), ` +
        `${sized(bytes)}` +
        (own > 0 ? `, and ${own} file(s) of your own on top of it` : '') +
        (refused.length > 0
          ? `; ${refused.length} would not go back: ${refused.join(', ')}`
          : '') +
        '\n',
    );
    if (refused.length > 0) {
      // The same sentence a drop gets for the same reason (#158): a disk
      // with holes in it is about to be booted, and that is not a thing
      // to leave in a console line nobody opened.
      appendConsole(
        '[host] INCOMPLETE: the disk this page will boot is missing files ' +
          'this browser was keeping. Drop your game directory again.\n',
      );
    }
    offerPrograms(taken);
    // The program last booted, chosen again if it is still there.
    //
    // The *browser* decides whether it is still there: a `<select>` given
    // a value that matches no option answers the empty string, and that
    // is the whole of the matching. A page comparing the two names itself
    // would be deciding whether `Start.exe` and `\START.EXE` are one
    // file, which is core's rule and not a thing to have a second
    // implementation of (abi.h, #146).
    const last = await kept.get(SETTINGS_STORE, PROGRAM_SETTING);
    if (typeof last === 'string') programSelect.value = last;
    if (programSelect.value === '') programSelect.selectedIndex = 0;
    setStatus(
      `${taken.length} files came back from this browser - choose a program` +
        ' and press boot.',
    );
    sayWhatIsKept();
  }

  /// The write-back, armed for a loaded program, or null.
  ///
  /// **Armed only where there is a save layer to arm it with.** A program
  /// this build has no table for answers null from `saveLayer()`, and
  /// `docs/hosts.md` §6's rule for that answer is to persist nothing
  /// rather than persist a guess: with no table there is no line between
  /// the publisher's bytes and the player's, and a page that drew one
  /// from a filename would be drawing it at the one place a guess is
  /// worst. So an unrecognized program runs, and this says so and keeps
  /// nothing it writes.
  ///
  /// Answers a function the run loop calls on its readout cadence. It is
  /// cheap when nothing has happened: one integer off the ABI
  /// (`vfsGeneration()`), which is what #228 opened that door for.
  function armWriteBack(box) {
    const layer = box.saveLayer();
    if (layer === null) {
      appendConsole(
        '[host] this build has no save layer for this program, so nothing ' +
          'it writes to the disk will be kept between visits ' +
          '(docs/hosts.md §6)\n',
      );
      return null;
    }
    if (!kept) return null;

    let seen = box.vfsGeneration();
    let busy = false;
    const told = new Set();

    const writeBack = async () => {
      const { play, config, strangers } = partitionDisk(box, diskPaths);

      // §6's honest failure to watch for, said once per path: a file that
      // appeared during a run and that the table does not name is a gap
      // in the table, and an issue to file rather than a file to quietly
      // keep. It is not persisted, because whose bytes those are is the
      // one thing this must not guess about.
      for (const path of strangers) {
        if (told.has(path)) continue;
        told.add(path);
        appendConsole(
          `[host] ${path} appeared during this run and the save layer does ` +
            'not name it, so it is not being kept. That is a gap in the ' +
            'table (docs/hosts.md §6) and an issue to file.\n',
        );
      }

      const { records, unreadable, bytes } = readBack(box, play);
      if (unreadable.length > 0) {
        appendConsole(
          `[host] ${unreadable.length} of your own file(s) would not read ` +
            `back off the machine and are not being kept: ${unreadable.join(', ')}\n`,
        );
      }
      const now = new Set(records.map(([path]) => path));
      // What the store holds and the disk no longer does. A save over a
      // smaller party unlinks the items file of a member who now carries
      // nothing (§6), and a record left behind would hand that file back
      // next visit and contradict the save that removed it.
      const gone = [...playPaths].filter((path) => !now.has(path));

      try {
        await kept.write(PLAY_STORE, records, gone);
        playPaths = now;
        appendConsole(
          `[host] kept ${records.length} file(s) of your own (${sized(bytes)})` +
            (gone.length > 0 ? `, ${gone.length} removed` : '') +
            ' in this browser\n',
        );
      } catch (problem) {
        const why = describeRefusal(problem, {
          what: 'the game you just saved',
          bytes,
          estimate: await estimateStorage(),
        });
        appendConsole(`[host] ${why}\n`);
        sayWhatIsKept(why);
        return false;
      }

      // `POOL.CFG` is the program's settings, so §6 puts it on the game's
      // side of the boundary — but it is the one file on that side that a
      // player changes, so it goes back into the copy's own store rather
      // than being left to go stale.
      const changed = readBack(box, config);
      if (changed.records.length > 0) {
        try {
          await kept.write(DISK_STORE, changed.records);
          for (const [path] of changed.records) diskPaths.add(path);
        } catch (problem) {
          appendConsole(
            `[host] ${describeRefusal(problem, {
              what: "the program's own settings",
              bytes: changed.bytes,
              estimate: await estimateStorage(),
            })}\n`,
          );
          return false;
        }
      }
      sayWhatIsKept();
      return true;
    };

    return () => {
      if (busy) return;
      const now = box.vfsGeneration();
      if (now === seen) return;
      busy = true;
      void writeBack()
        // `seen` moves only on a write that landed, so a refusal is tried
        // again at the next look rather than being counted as done.
        .then((ok) => {
          if (ok) seen = now;
        })
        .catch((problem) => {
          appendConsole(`[host] the write-back failed: ${problem}\n`);
        })
        .finally(() => {
          busy = false;
        });
    };
  }

  /// The one line under *Forget everything* that says what there is to
  /// forget. A `why` replaces it, because a refusal is the more
  /// interesting fact.
  function sayWhatIsKept(why = null) {
    const status = el(KEPT_STATUS_ID);
    if (!status) return;
    if (why) {
      status.textContent = why;
      return;
    }
    if (!kept) {
      status.textContent =
        'this browser keeps nothing, so every visit starts over';
      return;
    }
    const parts = [];
    if (diskPaths.size > 0) parts.push(`${diskPaths.size} file(s) of a copy`);
    if (playPaths.size > 0) parts.push(`${playPaths.size} file(s) of your own`);
    const text = drawer.characters();
    if (text > 0) parts.push(`${sized(text)} of read documents`);
    status.textContent =
      parts.length === 0
        ? 'nothing kept in this browser yet'
        : `kept in this browser: ${parts.join(', ')}`;
  }


  // --- Speed: #107's presets, this page's half (#108) --------------------
  //
  // The same four names the SDL host's `--speed` takes and the web
  // driver's `--speed` takes; host.mjs owns the mapping onto abi.h's
  // numbers so that no host spells a preset its own way.
  //
  // A governor and not a fast-forward: virtual time still decides every
  // deadline, tone and tick, so a run at `at` is exactly as deterministic
  // as one at `xt` — what changes is how much of it fits into a second of
  // yours, which on this host is the question #108 exists to ask. It is
  // applied whenever the control changes and not only at boot, because
  // the useful thing to do with it here is to turn it up while watching
  // the readout below and find where the browser stops keeping up.
  function applySpeed() {
    if (!machine || !speedSelect) return;
    const preset = SPEED_PRESETS[speedSelect.value];
    if (preset === undefined) return;
    machine.setSpeed(preset);
    appendConsole(`[host] speed ${speedSelect.value}\n`);
  }
  if (speedSelect) speedSelect.addEventListener('change', applySpeed);

  const claimTheRun = () => {
    if (started) return false;
    started = true;
    startButton.disabled = true;
    bootButton.disabled = true;
    if (programSelect) programSelect.disabled = true;
    return true;
  };

  const fail = (error) => {
    setStatus(`failed: ${error}`);
    appendConsole(`[host] ${error}\n`);
    console.error(error);
  };

  // --- The embedded demo -------------------------------------------------

  startButton.addEventListener('click', () => {
    if (!claimTheRun()) return;
    startButton.textContent = 'running...';
    (async () => {
      const box = await ensureMachine();
      const { writeStatus, entryStatus, size } = loadDemoProgram(box);
      if (writeStatus !== AF_OK || entryStatus !== AF_OK) {
        throw new Error(
          `loading the demo program failed (write=${writeStatus}, entry=${entryStatus})`,
        );
      }
      appendConsole(`[host] embedded demo program loaded: ${size} bytes\n`);
      await run(box, {
        canvas,
        setStatus,
        appendConsole,
        healthEl,
        volumeInput,
        muteCheckbox,
        stepBudget: 0,
        message:
          'running - the machine draws a pattern, plays a tone, and echoes ' +
          'whatever you type into the console below.',
      });
    })().catch(fail);
  });

  // --- The player's own directory (#84) ----------------------------------

  // --- The journal, read once (M5-E3, #174) -----------------------------
  //
  // Nothing to do with the machine: this is onboarding, and it happens
  // whether or not a game has been loaded. The module does all the
  // reading — it hashes the document, finds its edition and follows each
  // entry's offset — and this is the file input and the sentence
  // afterwards.
  //
  // What that sentence says matters more than usual, because there are
  // three ways an ingestion ends and a player can act on each one: an
  // edition nobody has fingerprinted (here is its hash), an engine that
  // is not installed (here is what to do), and entries that were located
  // and read. `known_journals()` is empty today, so the first is what
  // every real journal gets — and saying so plainly, with the
  // fingerprint, is the whole of PLAN.md §9's friendly path.
  //
  // **Read once, now, rather than once per visit** (M5-E3f). What comes
  // out of an ingestion is a store, and the store goes into the browser's
  // own key-value drawer the moment it is made and comes back the moment
  // the module next loads (`journal.mjs`'s own section on it says why the
  // drawer and not M6's database). So this input is a thing a player uses
  // on their first visit and then does not think about again — which is
  // the whole feature, because a real edition through a wasm OCR engine
  // is minutes rather than moments.
  const journalInput = el(JOURNAL_INPUT_ID);
  const journalStatusEl = el(JOURNAL_STATUS_ID);
  const journalForgetButton = el(JOURNAL_FORGET_ID);
  const journalCiteAllButton = el(JOURNAL_CITE_ALL_ID);
  const setJournalStatus = (text) => {
    if (journalStatusEl) journalStatusEl.textContent = text;
    appendConsole(`[journal] ${text}\n`);
  };

  // What a restore has to say, if anything.
  //
  // A `function` rather than a `const` on purpose: `ensureMachine` calls
  // it and `ensureMachine` is written above this block, so hoisting is
  // what lets the wiring stay in the order a reader wants it in.
  //
  // Silence is the common case and the right one — a player with no
  // stored journal is not owed a line about a feature they have not used.
  // Two cases speak: a store that came back, and one that could not be
  // read back at all.
  function reportRestoredJournal(restored) {
    if (restored.why) {
      setJournalStatus(
        `${restored.why} - it is still in this browser, untouched;` +
          ' reading your journal again replaces it',
      );
      return;
    }
    if (!restored.restored) return;
    setJournalStatus(
      `${restored.entries} entries kept in this browser` +
        ` (${restored.recognized} read` +
        (restored.corrections > 0
          ? `, ${restored.corrections} corrected`
          : '') +
        (restored.pictures > 0 ? `, ${restored.pictures} pictures` : '') +
        `) - sha256=${restored.fingerprint}`,
    );
  }
  if (journalInput) {
    journalInput.addEventListener('change', async () => {
      const file = journalInput.files?.[0];
      if (!file) return;
      try {
        setJournalStatus(`reading ${file.name}...`);
        const box = await ensureMachine();
        const bytes = new Uint8Array(await file.arrayBuffer());

        // A journal is a document, so it is presented to the gate as
        // well as read inside — the same thing the desktop host's
        // `--journal` does, and what a journal-gated seam waits for
        // (#171). Its answer is not this ingestion's business: an
        // edition the gate does not know may still be one the fact table
        // knows, and the other way round, and each says so on its own.
        box.presentDocument(bytes);

        const { engine, why } = await loadEngine();
        if (!engine) setJournalStatus(why);

        const report = await ingestJournal(loaded.module, bytes, {
          engine,
          onProgress: ({ index, count, citation }) =>
            setJournalStatus(
              `${journalKind(citation)} ${journalNumber(citation)}` +
                ` (${index + 1} of ${count})...`,
            ),
        });
        if (engine?.close) await engine.close();

        if (!report.ok) {
          setJournalStatus(
            `${report.trouble} - sha256=${report.fingerprint}`,
          );
          return;
        }
        // Into the drawer before the sentence, so that the sentence can
        // say whether it got there (M5-E3f). A store that would not fit
        // is not a failed ingestion — the text is in this tab and the
        // reader will show it — but it *is* the difference between doing
        // this once and doing it every visit, so it is said out loud
        // rather than left for a player to discover next week.
        const stored = keepStore(loaded.module, { storage: drawer });
        keepLog(loaded.module, { storage: drawer });
        const wrote = await flushText();
        sayWhatIsKept();
        setJournalStatus(
          `${report.edition}: ${report.recognized} of ${report.entries} entries` +
            ` read by ${report.engine}` +
            // The drawings, and only when the edition has any: an entry
            // that is a map has no words, so the count above says nothing
            // about whether its picture arrived (#345).
            (report.art > 0
              ? `, ${report.pictures} of ${report.art} pictures`
              : '') +
            (report.firstTrouble
              ? ` (${journalKind(report.firstTrouble.citation)}` +
                ` ${journalNumber(report.firstTrouble.citation)}:` +
                ` ${report.firstTrouble.what})`
              : '') +
            (!wrote.ok
              ? ` - ${wrote.why}`
              : stored.kept
                ? ' - kept in this browser for next time'
                : ''),
        );
      } catch (problem) {
        setJournalStatus(`the ingestion failed: ${problem.message ?? problem}`);
      }
    });
  }

  // Reset: the drawer emptied and the tab's own copy with it (M5-E3f).
  //
  // Both halves, because a button that emptied only the drawer would
  // leave the reader showing text the page had just said it had
  // forgotten. The module's store is cleared through the ABI rather than
  // by reloading, so the reset is done when the sentence appears.
  //
  // No confirmation prompt, and that is a decision: what is destroyed is
  // a transcription of a document the player still has, and the way to
  // get it back is the file input immediately above. Corrections are the
  // one thing that is genuinely theirs, so the sentence counts them.
  if (journalForgetButton) {
    journalForgetButton.addEventListener('click', () => {
      // The module is *not* loaded to do this. A player who pressed this
      // before touching anything else wants the drawer emptied, and
      // fetching two megabytes of wasm to clear a store that does not yet
      // exist would be this page doing work on their behalf that they can
      // see and did not ask for. When there is no module there is no
      // tab-side copy either, so the drawer is the whole of it.
      //
      // The *database* is waited for, though, which it was not before
      // #381: the drawer is only filled once it is open, and forgetting
      // an empty drawer would leave the record where it was and hand the
      // journal back on the next reload.
      void (async () => {
        await keptReady;
        const had = loaded ? loaded.module._af_web_journal_store_size() : 0;
        const corrections = loaded
          ? loaded.module._af_web_journal_store_corrections()
          : 0;
        const { forgotten } = forgetStore({ storage: drawer });
        forgetLog({ storage: drawer });
        if (loaded) clearStore(loaded.module);
        const wrote = await flushText();
        sayWhatIsKept();
        if (!wrote.ok) {
          setJournalStatus(wrote.why);
          return;
        }
        if (!forgotten && had === 0) {
          setJournalStatus('there was no journal to forget');
          return;
        }
        setJournalStatus(
          `forgotten${had > 0 ? `: ${had} entries` : ''}` +
            (corrections > 0
              ? `, including ${corrections} you corrected`
              : '') +
            ' - read your journal again to put it back',
        );
      })().catch(fail);
    });
  }

  // The debug cheat (#301): everything this browser's journal holds onto
  // the machine's `Notes` log, so a person can open each entry in turn
  // on the game's own screen and read it against the scan.
  //
  // The module *is* loaded to do this, unlike the two forget buttons: the
  // log lives in the machine, so there has to be one, and `ensureMachine`
  // is also what restores the store this cites from. What it cites goes
  // into the store's own log the way a real citation does, and is kept
  // in the drawer right here — so the log stays filled across reloads,
  // for good, until *Forget it*. The sentence says so, because a game
  // that appears to have said everything already is otherwise a mystery.
  //
  // A page that has never read a journal cites nothing and says so; the
  // log is left as it was.
  if (journalCiteAllButton) {
    journalCiteAllButton.addEventListener('click', async () => {
      try {
        const box = await ensureMachine();
        const cited = citeAllJournal(loaded.module, box.handle);
        if (cited === 0) {
          setJournalStatus(
            'nothing to cite - no journal has been read in this browser',
          );
          return;
        }
        keepStore(loaded.module, { storage: drawer });
        // And the log, which is where the citing actually landed (#351):
        // the store's text did not move, so without this the cheat would
        // be forgotten on the next reload.
        keepLog(loaded.module, { storage: drawer });
        const wrote = await flushText();
        // The flag is lowered only once the bytes are somewhere, never
        // before: a store cleared on a database that refused it would
        // lose the citations at the next write.
        if (wrote.ok) loaded.module._af_web_journal_store_clear_changed();
        sayWhatIsKept();
        setJournalStatus(
          `cited all ${cited} entries onto the Notes log (cheat)` +
            ' - it stays that way until you press Forget it' +
            (wrote.ok ? '' : ` - ${wrote.why}`),
        );
      } catch (problem) {
        setJournalStatus(`citing failed: ${problem.message ?? problem}`);
      }
    });
  }

  // "Ask me again" (M6-C1b, #292): the drawer emptied, and the tab's own
  // copy with it.
  //
  // Both halves for the journal button's reason — a page that emptied only
  // the drawer would go on skipping the challenge for the rest of this
  // session while saying it had forgotten. The *machine* keeps its latch:
  // a person who has already answered in this session answered, and
  // un-asking a question after the fact is not something this can honestly
  // do. So the sentence says which launch it takes effect on.
  //
  // No confirmation prompt, and that is a decision: what is destroyed is
  // one line saying a copy has answered, and the way to get it back is to
  // answer the game's own question once.
  const codeWheelForgetButton = el(CODE_WHEEL_FORGET_ID);
  if (codeWheelForgetButton) {
    codeWheelForgetButton.addEventListener('click', () => {
      void (async () => {
        await keptReady;
        const status = el(CODE_WHEEL_STATUS_ID);
        drawer.removeItem(CODE_WHEEL_STORE_KEY);
        // The module's own store, when there is a module. Not fetched to
        // do this, for the journal button's reason: a player pressing
        // this before anything is loaded wants the drawer emptied and
        // nothing else downloaded on their behalf.
        if (loaded) loaded.module._af_web_code_wheel_store_clear();
        const wrote = await flushText();
        sayWhatIsKept();
        if (status) {
          status.textContent = wrote.ok
            ? 'forgotten - the game will ask again on the next launch'
            : wrote.why;
        }
      })().catch(fail);
    });
  }

  // --- May this build keep your progress beside your saves? (#385) -------
  //
  // The one permission this page asks for. `sidecars.mjs` has the
  // question, what an answer to it is, and why an unanswered one is
  // neither a yes nor a no; this is the panel over it.
  //
  // Two buttons rather than a checkbox, and that is the decision worth
  // stating: a checkbox has a state before anybody has touched it, so an
  // unanswered question would be indistinguishable from a refusal, and
  // *asked once* would quietly become *assumed once*. Both stay live
  // afterwards, because a player who changes their mind is entitled to.
  // What the panel says about itself when nothing has just happened.
  // Through `shouldAskAboutSidecars` so that the page and the terminal
  // answer the same question the same way, including the two states that
  // are not a choice at all: a browser keeping nothing has nowhere to
  // remember an answer, and a visit with no copy in it has nothing for a
  // sidecar to go beside.
  const sidecarSentence = () => {
    switch (
      shouldAskAboutSidecars({
        answered: sidecarAnswer,
        canRemember: kept !== null,
        haveDisk: diskPaths.size > 0,
      })
    ) {
      case SIDECARS_NOWHERE_TO_REMEMBER:
        return 'this browser is keeping nothing, so an answer here could ' +
          'not be remembered';
      case SIDECARS_NOTHING_TO_WRITE_BESIDE:
        return 'drop your game directory first - there is nothing for ' +
          'these to go beside yet';
      case SIDECARS_ASK:
      default:
        return sidecarStatus(sidecarAnswer);
    }
  };

  const paintSidecars = (why = null) => {
    const status = el(SIDECARS_STATUS_ID);
    if (status) status.textContent = why ?? sidecarSentence();
    const about = el(SIDECARS_ABOUT_ID);
    if (about) about.textContent = sidecarQuestion().join(' ');
    const yes = el(SIDECARS_YES_ID);
    const no = el(SIDECARS_NO_ID);
    // Which of the two is the one that would change something. A pressed
    // look rather than a disabled one: disabling the chosen button would
    // hide the answer from anybody reading the panel with a screen
    // reader, and there is nothing wrong with pressing it again.
    if (yes) yes.setAttribute('aria-pressed', String(sidecarAnswer === true));
    if (no) no.setAttribute('aria-pressed', String(sidecarAnswer === false));
  };

  const answerSidecars = (answer) => {
    void (async () => {
      await keptReady;
      sidecarAnswer = answer;
      // Written down straight away, because "asked once" is a promise
      // about the *next* visit and this is the only moment it can be
      // kept. A refusal to write is said out loud rather than swallowed:
      // a player told their answer was remembered and then asked again
      // has been lied to.
      const wrote = await rememberSetting(SIDECARS_SETTING, answer);
      paintSidecars(
        wrote
          ? `${sidecarStatus(sidecarAnswer)}${
              machine ? ' - from the next boot' : ''
            }`
          : 'this browser would not remember that, so you will be asked' +
              ' again next visit',
      );
      sayWhatIsKept();
    })().catch(fail);
  };

  const sidecarsYes = el(SIDECARS_YES_ID);
  if (sidecarsYes) {
    sidecarsYes.addEventListener('click', () => answerSidecars(true));
  }
  const sidecarsNo = el(SIDECARS_NO_ID);
  if (sidecarsNo) {
    sidecarsNo.addEventListener('click', () => answerSidecars(false));
  }
  paintSidecars();

  // --- Forget everything (#381) ------------------------------------------
  //
  // The per-thing forgets stay where they are — *Forget it* for the
  // journal, *Ask me again* for the code wheel — because the thing a
  // player usually wants is one of them and not all of them. This is the
  // other one: the whole database gone, the copy and the saves with it.
  //
  // No confirmation prompt, which is a decision and a different one from
  // the two above. What goes is a copy of files the player still has on
  // their own machine, a transcription of a document they still hold, and
  // one line saying a copy has answered a question. What it cannot give
  // back is a *saved game*, so the sentence says how many there were
  // rather than pretending nothing of consequence happened.
  const forgetAllButton = el(KEPT_FORGET_ID);
  if (forgetAllButton) {
    forgetAllButton.addEventListener('click', () => {
      void (async () => {
        await keptReady;
        const had = { disk: diskPaths.size, play: playPaths.size };
        const { forgotten, why } = kept
          ? await forgetEverything({ kept })
          : { forgotten: true, why: null };
        if (!forgotten) {
          sayWhatIsKept(why ?? 'this browser would not forget it');
          return;
        }
        // And what an older visit left in `localStorage`, which
        // `textDrawer` copied across rather than moved — so this is the
        // one place both copies go.
        const legacy = browserStorage();
        if (legacy) {
          for (const record of TEXT_RECORDS) {
            try {
              legacy.removeItem(record);
            } catch {
              /* a drawer that will not be emptied is one that is closed */
            }
          }
        }
        // The tab's own copies, for the two per-thing buttons' reason: a
        // page that emptied only the database would go on showing what it
        // had just said it had forgotten.
        if (loaded) {
          clearStore(loaded.module);
          loaded.module._af_web_code_wheel_store_clear();
        }
        // The machine's filesystem too, but only while nothing is
        // running: a page does not pull the disk out from under a program
        // that is reading it.
        if (machine && !started) {
          machine.vfsClear();
          offerPrograms([]);
        }
        diskPaths = new Set();
        playPaths = new Set();
        // Reopened, empty, so the rest of this session still keeps what
        // the player does next.
        keptReady = openKeptStore();
        await keptReady;
        sayWhatIsKept();
        const parts = [];
        if (had.disk > 0) parts.push(`${had.disk} file(s) of a copy`);
        if (had.play > 0) parts.push(`${had.play} file(s) of your own`);
        appendConsole(
          `[host] forgotten${parts.length > 0 ? `: ${parts.join(', ')}` : ''}` +
            (started
              ? ' - the machine is still running on the disk it has\n'
              : '\n'),
        );
        setStatus(
          started
            ? 'this browser has forgotten everything; the run keeps the disk' +
              ' it is on.'
            : 'this browser has forgotten everything - drop a directory to' +
              ' start again.',
        );
      })().catch(fail);
    });
  }

  wireDirectoryPicker({
    input: el(DIRECTORY_INPUT_ID),
    dropZone: el(DROP_ZONE_ID),
    onError: fail,
    onFiles: async (files) => {
      if (started) return;
      const box = await ensureMachine();
      setStatus(`reading ${files.length} files...`);

      // A second directory replaces the first rather than merging with
      // it: two installations' files in one filesystem is not a state
      // any real machine has, and this page keeps nothing between
      // reloads anyway.
      box.vfsClear();

      // Two skipped lists, not one (#158). A path DOS could never have
      // named is the machine working and the player needs no more than
      // the name; a file the filesystem had no room for is a *hole in
      // the disk they are about to boot*, and saying those two in one
      // sentence is how a browser came to run an installation with seven
      // data files missing from it.
      const unnameable = [];
      const noRoom = [];
      const otherwise = [];
      const taken = [];
      for (const file of files) {
        // The path goes across as the player's own text and core decides
        // what it means, separators included (abi.h, #146).
        const status = box.vfsPut(file.path, file.bytes);
        if (status === AF_OK) taken.push(file);
        else if (status === AF_NO_ROOM) noRoom.push(file.path);
        else if (status === AF_INVALID) unnameable.push(file.path);
        else otherwise.push(`${file.path} (${describeSkip(status)})`);
      }

      appendConsole(
        `[host] filesystem: ${taken.length} files, ` +
          `${Math.round(box.vfsBytesUsed() / 1024)} KiB` +
          (unnameable.length > 0
            ? `, ${unnameable.length} ignored (not DOS-nameable, which is ` +
              `expected): ${unnameable.join(', ')}`
            : '') +
          (otherwise.length > 0
            ? `, ${otherwise.length} refused: ${otherwise.join(', ')}`
            : '') +
          '\n',
      );

      if (noRoom.length > 0) {
        // Its own line and its own sentence, because everything below
        // this point — the boot, the frames, a save — is happening on a
        // disk that is not the one the player chose.
        appendConsole(
          `[host] OUT OF ROOM: ${noRoom.length} file(s) did not fit in the ` +
            "machine's filesystem and are missing from the disk this page " +
            'will boot. The game will not find them, and a save may have ' +
            'nowhere to go. Choose a smaller directory.\n' +
            `[host]   ${noRoom.join(', ')}\n`,
        );
      }

      const ordered = offerPrograms(taken);

      // Into this browser, so the next visit needs no drop (#381). After
      // the puts and before the boot: what is kept is what landed on the
      // filesystem, read back off it.
      //
      // The `play` store is deliberately *not* emptied here. A second
      // directory replaces the first — two installations in one
      // filesystem is not a state any real machine has — but a player's
      // own saves are theirs whichever copy they were made on, and
      // throwing a party away because somebody re-dropped a directory to
      // fix one missing file is the wrong way round of that trade.
      // *Forget everything* is how they go.
      await keepDisk(box);
      await restorePlayFiles(box);

      // The one line a player who did not open the console will read, so
      // "the disk is incomplete" has to survive into it (#158). A boot
      // is still offered — it is their disk and their decision — but not
      // silently.
      setStatus(
        ordered.length === 0
          ? 'nothing in that directory has a DOS-legal name.'
          : noRoom.length > 0
            ? `${taken.length} files loaded, but ${noRoom.length} did not ` +
              'fit - this disk is incomplete; see the console.'
            : `${taken.length} files loaded - choose a program and press boot.`,
      );
      // There is somewhere for a sidecar to go now, so the panel stops
      // saying there is not (#385).
      paintSidecars();
    },
  });

  bootButton.addEventListener('click', () => {
    const program = programSelect.value;
    if (!program || !claimTheRun()) return;
    bootButton.textContent = 'running...';

    (async () => {
      const box = await ensureMachine();

      // The identity of the player's file, before anything runs — a fact
      // about it, never anything out of it (PLAN.md §2, CONTRIBUTING.md),
      // and the same digest the desktop host prints at load.
      const digest = box.vfsFingerprint(program);
      appendConsole(`[host] load ${program} sha256=${digest ?? 'unreadable'}\n`);

      box.setTrace(el(TRACE_CHECKBOX_ID)?.checked === true);

      // The playthrough's sidecars, if this player said yes (#385, #351).
      //
      // **Here, once, and nowhere else.** `saveSidecars(true)` turns the
      // store on *and attaches it*, and the attach reads the working
      // exploration table with `read_sidecar`, which replaces every
      // record in the machine — so a second call later in a visit would
      // hand a player an older map than the one on their screen. This is
      // the one moment that is both after the files are in (there is a
      // filesystem to read) and before `loadFromVfs()` (nothing has been
      // drawn yet), which is why the panel above records an answer
      // rather than applying one.
      //
      // Only `true` turns it on. `false` and *not answered yet* are
      // different facts about a player and the same instruction to this
      // page, which is to write nothing into the copy they dropped.
      if (sidecarAnswer === true) {
        box.saveSidecars(true);
        // And the read log out of its own sidecar beside the save. A
        // second call, on purpose: `ensureMachine()` makes this one
        // before the disk is put back, when there is no `\SAVE\` to read
        // — so without this the log beside a save could never be picked
        // up on the page at all. Twice is harmless (`host.mjs`), and the
        // rows do not double.
        box.journalSeenRestore();
        appendConsole(
          '[host] save-sidecars on - your map and the entries you have ' +
            'been sent to are kept beside your saved games\n',
        );
      }

      const tail = el(TAIL_INPUT_ID)?.value ?? '';
      const status = box.loadFromVfs(program, tail === '' ? '' : ` ${tail}`);
      if (status !== AF_OK) {
        throw new Error(
          `${program} did not load (status ${status}, loader error ${box.loadError()})`,
        );
      }

      // The identity the load established, and the seams it makes
      // available (M4-F1 #95, M4-F4 #98). An unrecognized edition is an
      // answer, not a fault: the game runs as a plain machine and every
      // seam is listed as unavailable with the reason.
      const edition = box.edition();
      appendConsole(
        `[host] edition ${edition ?? 'unrecognized - no seams are available for this program'}\n`,
      );
      const editionEl = el(EDITION_ID);
      if (editionEl) editionEl.textContent = `edition: ${edition ?? 'unrecognized'}`;
      if (edition === null) {
        await reportUnrecognizedEdition(box, appendConsole);
      }
      // What this browser remembers about *this copy* (M6-C1b, #292).
      // After the load, because the answer is keyed by the program's own
      // fingerprint and there is nothing to look up before one is
      // loaded; before the run, because a machine told after its first
      // step would have drawn the challenge already.
      if (box.codeWheelApply()) {
        appendConsole(
          '[host] code wheel answered on this copy already - the challenge ' +
            'will not be drawn\n',
        );
        const status = el(CODE_WHEEL_STATUS_ID);
        if (status) status.textContent = 'this copy has answered; it will not be asked';
      }

      // The seams this player chose in the panel on a previous visit
      // (#383). After the load, because a seam is keyed on the program's
      // own fingerprint and there is nothing to enable before one is
      // there; before the run, for `--seam`'s reason on the desktop —
      // a seam turned on after the first step is a seam that missed the
      // points the boot went through.
      //
      // **Off is what a browser with no record gives**, and that is the
      // fidelity invariant rather than a preference: `storedSeams()`
      // answers null for a record that is missing, empty, or anything
      // but a list of names, and null turns nothing on.
      const chosen = storedSeams(await readSetting(SEAMS_SETTING));
      if (chosen !== null && chosen.length !== 0) {
        // Through the same `seamEnable()` a click takes, never by
        // assuming it took: a remembered seam this program has no
        // addresses for is a row with a reason on it, and the run goes
        // on without it.
        const { on, refused } = applyStoredSeams(box, chosen);
        if (on.length !== 0) {
          appendConsole(`[host] seams remembered here: ${on.join(', ')}\n`);
        }
        for (const { id, reason } of refused) {
          appendConsole(
            `[host] seam ${id} was remembered here and this program refuses ` +
              `it (${reason}) - the panel says so and the run goes on\n`,
          );
        }
      }

      const refreshSeams = renderSeams(box, el(SEAMS_ID), appendConsole, (ids) =>
        rememberSetting(SEAMS_SETTING, ids),
      );

      // Which program, so a returning player finds it chosen (#381).
      // This page's own choice and nothing the machine sees, which is
      // what the `settings` store is for.
      await rememberSetting(PROGRAM_SETTING, program);

      const budget = Number.parseInt(el(STEPS_INPUT_ID)?.value ?? '', 10);
      await run(box, {
        canvas,
        setStatus,
        appendConsole,
        healthEl,
        refreshSeams,
        volumeInput,
        muteCheckbox,
        writeBack: armWriteBack(box),
        stepBudget: Number.isFinite(budget) && budget > 0 ? budget : 0,
        message: `running ${program} - the console below is what it says and ` +
          'what the machine refuses.',
      });
    })().catch(fail);
  });

  // --- The visit after the first one (#381) ------------------------------
  //
  // A player who dropped a directory last week should find it here, and
  // not have to press anything to be told so. So: as soon as the database
  // says it is holding files, the module is loaded and the disk goes
  // back, which is what `ensureMachine()` ends with.
  //
  // **Only when there is something to put back.** A first-time visitor
  // fetches nothing extra and the page is exactly as it was: this is the
  // one thing on the page that does work before a gesture, and it does it
  // because the player already made the gesture — on their last visit.
  // Nothing here starts audio or steps the machine; both still wait for
  // **start** or **boot**, as the autoplay policy and PLAN.md §4 require.
  //
  // This is also where the database's own two sentences are said, rather
  // than in `ensureMachine()`: a browser that keeps nothing, and records
  // that came across from `localStorage`, are both facts about *this
  // visit* and are true whether or not anybody ever makes a machine. Said
  // there, a player whose journal moved house and who then read it in the
  // game would never have been told anything had happened.
  void keptReady
    .then((store) => {
      if (store.why) appendConsole(`[host] ${store.why}\n`);
      if (store.migrated.length > 0) {
        appendConsole(
          `[host] moved ${store.migrated.length} thing(s) this browser was ` +
            'keeping in localStorage into its database - the same bytes, and ' +
            'the old copies are left where they were\n',
        );
      }
      sayWhatIsKept();
      // And what this browser remembers about the one permission (#385).
      // Here rather than at the panel's own setup because the answer is
      // read out of the database, and the panel is painted before there
      // is one.
      paintSidecars();
      if (store.counts.disk === 0 && store.counts.play === 0) return null;
      setStatus('putting back the copy this browser kept...');
      return ensureMachine();
    })
    .catch(fail);
}

/// The rest of the unrecognized answer (#207).
///
/// `edition: unrecognized` is true and is nothing a player can act on.
/// This says what they actually dropped: the closest edition, how much
/// of it is here, which required artifacts are not, and every file that
/// was looked at with its hash — which is exactly what a request to add
/// an edition has to carry (`machine/edition.h`).
///
/// The table is fetched here, at the one place that asks for it. A shell
/// that renders the checklist before a program is chosen fetches it
/// earlier and calls the same two functions; the file is beside this
/// module either way (`editions.mjs`). A table that cannot be fetched is
/// said and nothing else happens: a missing checklist is not a reason to
/// refuse somebody a run.
async function reportUnrecognizedEdition(box, appendConsole) {
  let editions;
  try {
    editions = await loadEditions();
  } catch (why) {
    appendConsole(`[host] edition checklist unavailable (${why.message})\n`);
    return;
  }
  const offered = box.vfsList().map((entry) => ({
    name: entry.path,
    sha256: box.vfsFingerprint(entry.path) ?? '',
  }));
  for (const line of describeMatch(matchEdition(editions, offered))) {
    appendConsole(`[host] edition ${line}\n`);
  }
}

/// The toggle panel, as a table (#383).
///
/// One row per seam carrying the five facts `toggle-panel.mjs` builds —
/// name, state, **`fired` as a number**, the refusal reason in core's own
/// word, and the document the row waits for — with a checkbox in front of
/// it. The desktop host paints the same five in the same order and the
/// same spellings (`hosts/sdl/src/seam_panel.h`); a player who reads
/// about one is reading about the other.
///
/// It used to be a line of text per checkbox, and the two columns it did
/// not have are the two #383 was filed over: a refusal that showed `off`
/// where core said `document_not_presented` threw away the part a player
/// can act on, and the document a row waits for crossed the ABI and was
/// rendered nowhere.
///
/// **`fired` is a number and not a tick** (#131, #163). A seam that armed
/// and fired nothing reads exactly like one that worked, and the count is
/// the only thing on the row that makes the difference visible — so the
/// column is right-aligned, and core's own sentence about what the
/// numbers mean goes under the row whenever it has one to say.
///
/// Toggling is a configuration call between frames (host.mjs) — the
/// change handler runs between two rAF callbacks and so never from
/// inside `runUntil()`. The listing is re-read after every toggle so an
/// on-but-inert seam (its module is not resident yet) shows as such, and
/// **a choice that core refused is never written down**: the box goes
/// back the way it was and `remember` is not called.
///
/// A seam that is **pulled** rather than left on keeps its button (#161),
/// because a trigger is a different affordance from a toggle and one
/// shown as the other is a promise the seam does not keep. It is live
/// only while the seam is on.
///
/// Answers a `refresh()` the run loop calls on its own readout cadence,
/// which is how `fired` stays a live number instead of one taken once
/// before the machine had run a step. It rewrites the cells only —
/// rebuilding the table under a person's pointer while they were aiming
/// at a checkbox would be worse than a number being half a second old.
function renderSeams(machine, container, appendConsole, remember = async () => {}) {
  if (!container) return () => {};
  const rows = panelRows(machine);
  if (rows.length === 0) {
    container.replaceChildren('this build carries no seams');
    return () => {};
  }

  const table = document.createElement('table');
  table.className = 'seams';
  const head = document.createElement('tr');
  // The empty leading cell is the checkbox column; the trailing one is
  // the pull button's, and both are headed by nothing because a heading
  // over a control says less than the control does.
  head.append(document.createElement('th'));
  for (const column of PANEL_COLUMNS) {
    const cell = document.createElement('th');
    cell.textContent = column.heading;
    if (column.numeric) cell.className = 'num';
    head.append(cell);
  }
  head.append(document.createElement('th'));
  const header = document.createElement('thead');
  header.append(head);
  const body = document.createElement('tbody');

  /// The cells one seam's facts are written into, so `refresh()` can
  /// rewrite them without touching the controls beside them.
  const cells = new Map();
  const buttons = new Map();

  /// Everything about a row that can change while the machine runs.
  const writeRow = (row) => {
    const held = cells.get(row.id);
    if (!held) return;
    for (const column of PANEL_COLUMNS) {
      if (column.key === 'id') continue;
      held.facts[column.key].textContent = String(row[column.key]);
    }
    // Core's sentence about what this row's numbers mean (#163), shown
    // only when there is one: an empty reading is core saying the
    // numbers speak for themselves.
    held.reading.textContent = row.reading;
    held.reading.hidden = row.reading === '';
    const trigger = buttons.get(row.id);
    if (trigger) trigger.disabled = !row.on;
  };

  for (const row of rows) {
    const line = document.createElement('tr');

    const tick = document.createElement('td');
    const box = document.createElement('input');
    box.type = 'checkbox';
    box.checked = row.on;
    box.disabled = !row.available;
    box.title = row.available ? row.about : `unavailable: ${row.reason}`;
    tick.append(box);
    line.append(tick);

    const facts = {};
    for (const column of PANEL_COLUMNS) {
      const cell = document.createElement('td');
      if (column.numeric) cell.className = 'num';
      if (column.key === 'id') {
        // The name alone. What the seam is *for* goes on the line under
        // the row rather than in this cell: a sentence in a column makes
        // the column as wide as the sentence, and then `fired` is
        // somewhere different on every build — which is the one thing
        // the fixed columns exist to prevent (#131).
        cell.textContent = row.id;
      } else {
        cell.textContent = String(row[column.key]);
        facts[column.key] = cell;
      }
      line.append(cell);
    }

    const pull = document.createElement('td');
    let trigger = null;
    if (row.trigger) {
      trigger = document.createElement('button');
      trigger.type = 'button';
      trigger.textContent = 'pull';
      trigger.title =
        'act once, at the next time the program reaches this seam’s point';
      trigger.disabled = !row.on;
      buttons.set(row.id, trigger);
      trigger.addEventListener('click', () => {
        const answer = machine.seamPull(row.id);
        const after = panelRows(machine).find((seam) => seam.id === row.id);
        // What the pull is waiting for, said at the moment it is made. A
        // trigger acts at a CS:IP breakpoint, so "immediately" means "at
        // the next arrival at the point" and nothing else can; a person
        // told neither has a button that did nothing.
        appendConsole(
          `[host] seam ${row.id} pulled` +
            (answer !== AF_OK
              ? ' refused'
              : after && after.state === 'on inert'
                ? ` - inert (${after.reason}); it will act when its module is back`
                : ' - acts at the next arrival at its point') +
            '\n',
        );
        if (after) writeRow(after);
      });
      pull.append(trigger);
    }
    line.append(pull);

    // What the seam is for, and — when core has one to give — its
    // sentence about the numbers, on a line of their own under the row
    // and indented past the checkbox. The desktop panel says the same
    // two things under the table for whichever row has the focus; a page
    // has room to say them on every row.
    const readingRow = document.createElement('tr');
    readingRow.className = 'reading';
    readingRow.append(document.createElement('td'));
    const under = document.createElement('td');
    under.colSpan = PANEL_COLUMNS.length + 1;
    const about = document.createElement('span');
    about.className = 'about';
    about.textContent = row.about;
    const reading = document.createElement('span');
    reading.className = 'says';
    reading.textContent = row.reading;
    reading.hidden = row.reading === '';
    under.append(about, reading);
    readingRow.append(under);

    cells.set(row.id, { facts, reading });

    box.addEventListener('change', () => {
      const wanted = box.checked;
      const answer = wanted ? machine.seamEnable(row.id) : machine.seamDisable(row.id);
      const after = panelRows(machine).find((seam) => seam.id === row.id);
      appendConsole(
        `[host] seam ${row.id} ${wanted ? 'on' : 'off'}` +
          (answer === AF_OK ? '' : ` refused (${after?.reason ?? '?'})`) +
          (after && after.state === 'on inert' ? ` (inert: ${after.reason})` : '') +
          '\n',
      );
      // **A choice core refused is not a choice**, so the box goes back
      // and nothing is remembered: a page that wrote one down would turn
      // on a seam at the next visit that this visit could not.
      if (answer !== AF_OK) {
        box.checked = !wanted;
        if (after) writeRow(after);
        return;
      }
      if (after) writeRow(after);
      // And a toggle **in the panel** is a player choosing, which is the
      // only gesture on this page that is. `persist.mjs`'s `seams` key.
      void remember(seamsToStore(panelRows(machine)));
    });

    body.append(line, readingRow);
  }

  table.append(header, body);
  container.replaceChildren(table);

  return () => {
    for (const row of panelRows(machine)) writeRow(row);
  };
}

/// The code wheel's drawer, into the module's store (M6-C1b, #292).
///
/// Every outcome is a sentence on the page, because the failure this
/// guards against is silent: a player who answered the question last week
/// and is asked it again deserves to know their browser lost it rather
/// than to think the emulator did nothing.
function restoreCodeWheelStore(machine) {
  const status = el(CODE_WHEEL_STATUS_ID);
  const say = (text) => {
    if (status) status.textContent = text;
  };
  // The drawer, not the database, and the order matters: a browser with
  // no IndexedDB but with what an older visit left in `localStorage` has
  // the answer right there, and telling that player it keeps nothing
  // would be this page throwing away a fact it is holding.
  const text = drawer.getItem(CODE_WHEEL_STORE_KEY);
  if (text === null || text === '') {
    say(
      kept
        ? 'nothing answered in this browser yet'
        : 'this browser keeps nothing, so the game will ask every visit',
    );
    return;
  }
  const trouble = machine.codeWheelStoreRead(text);
  if (trouble !== 0) {
    // Left in the drawer rather than thrown away: whatever it is, this
    // build cannot read it, and a store from a later one is somebody's
    // own answer.
    say(`what this browser kept could not be read back (${trouble})`);
    return;
  }
  say('this browser remembers a copy that has answered');
}

/// Put the module's store in the drawer and on to the database, and say
/// whether it went.
///
/// Asynchronous since #381, which is why the frame loop below does not
/// wait for it: the answer arrives a moment later and is a sentence on
/// the page either way, and the flag that says a store has moved stays
/// raised until the bytes are somewhere.
async function keepCodeWheelStore(machine) {
  drawer.setItem(CODE_WHEEL_STORE_KEY, machine.codeWheelStoreWrite());
  const wrote = await flushText();
  return { kept: wrote.ok, why: wrote.why };
}

/// Present, run, and report — everything both entry points share.
/// Tell `machine` what the clock out here reads, at the tick it reads it
/// (#320, #343).
///
/// The clock inside is a **seed plus virtual time and never a callout**
/// into here (`host.mjs`'s `wallClockFields`, `machine/platform.h`), so a
/// machine nobody tells is not a machine with an approximate date — it is
/// one counting from the DOS epoch, and every INT 21h AH=2Ah answers 1
/// January 1980 plus its own uptime. The journal's listing is where that
/// surfaced: every entry stamped `01-01 00:04` (#320).
///
/// **And a seed is only as good as the tick it was taken at, which is why
/// this is called twice** (#343). `ensureMachine()` makes the machine on
/// whichever gesture comes first — a dropped directory, a journal picked
/// for ingestion — and virtual time does not begin until somebody presses
/// **start** or **boot**. Every second in between is wall time the
/// machine's clock never sees, and it is not recovered later: the seed is
/// an origin, so a stamp taken an hour into play is that origin plus an
/// hour of *virtual* time, and the whole listing reads that many seconds
/// stale for the rest of the session. Picking a journal and waiting out
/// its ingestion is minutes of it. So the run loop takes a fresh seed at
/// the tick it is about to start stepping from, which is where the
/// desktop host has always taken its one (`hosts/sdl/src/main.cpp`,
/// before the first instruction).
///
/// Re-seeding is not a second clock and not a jump: `wall_clock::set()`
/// records an instant *and* the tick it belongs to, so the machine's
/// answers stay a monotonic function of virtual time either way. It is
/// still a seed, so a run is still reproducible from what the host wrote
/// down.
function seedWallClock(machine, appendConsole) {
  const wall = wallClockFields(new Date());
  if (
    wall &&
    machine.setWallClock(
      wall.year,
      wall.month,
      wall.day,
      wall.hour,
      wall.minute,
      wall.second,
      wall.centisecond,
    ) === AF_OK
  ) {
    const two = (n) => String(n).padStart(2, '0');
    appendConsole(
      `[host] wall clock ${wall.year}-${two(wall.month)}-${two(wall.day)} ` +
        `${two(wall.hour)}:${two(wall.minute)}:${two(wall.second)}\n`,
    );
    return true;
  }
  // Log, don't fake: a browser whose year DOS has no room for gets the
  // machine this build has always had, and is told so, rather than a date
  // somebody here invented for it.
  appendConsole(
    '[host] wall clock not set - this browser reports a date outside ' +
      "DOS's own 1980-2099; the machine counts from 1980-01-01\n",
  );
  return false;
}

/// The on-screen keyboard (#377), drawn from core's own tables.
///
/// A phone has no keyboard and this game asks for a character's name, so
/// M6's exit needs one painted on the screen. What is *here* is only the
/// painting: the legends, the make codes, the widths, the rows, which
/// key the focus starts on and what a commit produces all come across
/// the ABI from `machine/screen_keyboard.h`, and the desktop host draws
/// the same three layouts from the same numbers. A layout change is a
/// change to that one file in core and to nothing in this page.
///
/// **This widget is a reference implementation and not an interface.** A
/// serving page is free to replace it outright: the model's four layers
/// (`docs/hosts.md` §7) are what a page builds on, a page with keys of
/// its own takes `commitScancode()` and none of the tables, and a page
/// that wants no painted keyboard at all posts scan codes with
/// `machine.postKey()` and imports none of this.
///
/// The keys are real buttons, positioned from the model's own columns and
/// widths, so the browser does the hit testing and a gap between two keys
/// is a gap in both hosts because both put the keys in the same places.
/// A host drawing into a framebuffer has no widget under the pointer and
/// asks core instead (`af_screen_keyboard_key_at`).
///
/// Returns `{ owns, press, dispose }`: while the keyboard is up it *owns*
/// the four arrows and Return, which move its focus and commit the key
/// under it rather than reaching the machine — the path a gamepad will
/// drive in M8 (#210), proven here with the only four-way control a
/// browser has today.
function wireScreenKeyboard(machine, postKey) {
  const container = document.getElementById(KEYBOARD_ID);
  const show = document.getElementById(KEYBOARD_SHOW_ID);
  const chooser = document.getElementById(KEYBOARD_LAYOUT_ID);
  const about = document.getElementById(KEYBOARD_ABOUT_ID);
  const { layouts } = readScreenKeyboard(machine.module);

  let index = 0;
  let focus = layouts[index].focus;
  let latched = 0;
  let buttons = [];

  /// A length in quarter units, as the stylesheet's own `--quarter`
  /// multiplied out. No pixel count reaches this file: what a key is
  /// worth on a screen is the page's to style and core's to know nothing
  /// about.
  const quarters = (n, inset = 0) =>
    `calc(var(--quarter) * ${n}${inset === 0 ? '' : ` - ${inset}px`})`;

  const paint = () => {
    for (const [at, button] of buttons.entries()) {
      const key = layouts[index].keys[at];
      button.classList.toggle('focused', at === focus);
      button.classList.toggle(
        'latched',
        key.latch !== 0 && (latched & key.latch) !== 0,
      );
    }
  };

  const commit = (at) => {
    focus = at;
    const made = commitKey(machine.module, index, at, latched);
    for (const event of made.events) postKey(event.scancode, event.down);
    latched = made.latched;
    paint();
  };

  /// Let go of whatever is latched, and post the breaks for it: a shift
  /// latched on one layout has no business surviving into the next one,
  /// or outliving the keyboard that latched it.
  const unlatch = () => {
    for (const event of releaseLatched(machine.module, latched)) {
      postKey(event.scancode, event.down);
    }
    latched = 0;
  };

  const render = () => {
    const layout = layouts[index];
    container.replaceChildren();
    container.style.width = quarters(layout.width);
    container.style.height = quarters(layout.rows * 4);
    buttons = layout.keys.map((key, at) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = key.label;
      button.style.left = quarters(key.column);
      button.style.top = quarters(key.row * 4);
      button.style.width = quarters(key.width, 2);
      button.style.height = quarters(4, 2);
      button.addEventListener('click', () => commit(at));
      container.append(button);
      return button;
    });
    about.textContent = layout.about;
    paint();
  };

  const onShow = () => {
    container.hidden = !show.checked;
    if (!show.checked) unlatch();
  };
  const onChoose = () => {
    unlatch();
    index = Number(chooser.value);
    focus = layouts[index].focus;
    render();
  };

  chooser.replaceChildren();
  for (const [at, layout] of layouts.entries()) {
    const option = document.createElement('option');
    option.value = String(at);
    option.textContent = layout.name;
    chooser.append(option);
  }
  chooser.value = String(index);
  chooser.disabled = false;
  show.disabled = false;
  show.addEventListener('change', onShow);
  chooser.addEventListener('change', onChoose);
  render();
  onShow();

  /// The four arrows and Return, while the keyboard is up. `true` means
  /// the page took the key and the machine must not also see it.
  const DIRECTIONS = {
    ArrowLeft: AF_NAV_LEFT,
    ArrowRight: AF_NAV_RIGHT,
    ArrowUp: AF_NAV_UP,
    ArrowDown: AF_NAV_DOWN,
  };

  const commits = (code) => code === 'Enter' || code === 'NumpadEnter';

  return {
    owns(code) {
      return show.checked && (code in DIRECTIONS || commits(code));
    },
    press(code) {
      if (commits(code)) {
        commit(focus);
        return;
      }
      focus = moveFocus(machine.module, index, focus, DIRECTIONS[code]);
      paint();
    },
    dispose() {
      unlatch();
      show.removeEventListener('change', onShow);
      chooser.removeEventListener('change', onChoose);
      chooser.disabled = true;
      show.disabled = true;
      container.replaceChildren();
      container.hidden = true;
      about.textContent = '';
      show.checked = false;
      buttons = [];
    },
  };
}

async function run(
  machine,
  {
    canvas,
    setStatus,
    appendConsole,
    healthEl,
    refreshSeams,
    volumeInput,
    muteCheckbox,
    stepBudget,
    message,
    // What to call when the disk may have moved (#381), or null when
    // there is nothing to write back to — no database, or a program this
    // build has no save layer for, which `docs/hosts.md` §6 says is the
    // answer to persist nothing on rather than to guess on.
    writeBack = null,
  },
) {
  // A keep of the code wheel's answer in flight, or null. The write is
  // asynchronous and the flag that asks for it stays raised until it
  // lands, so without this every frame after an answer would start
  // another one.
  let codeWheelKeep = null;
  const ctx = canvas.getContext('2d');
  const width = machine.frameWidth();
  const height = machine.frameHeight();
  canvas.width = width;
  canvas.height = height;
  const imageData = ctx.createImageData(width, height);
  // Every pixel is opaque; set once rather than every frame.
  const alpha = imageData.data;
  for (let i = 3; i < alpha.length; i += 4) alpha[i] = 255;

  const present = () => {
    const pixels = machine.framebufferView();
    const palette = machine.paletteView();
    const data = imageData.data;
    for (let i = 0; i < pixels.length; ++i) {
      const entry = pixels[i] * 3;
      const out = i * 4;
      data[out] = palette[entry];
      data[out + 1] = palette[entry + 1];
      data[out + 2] = palette[entry + 2];
    }
    ctx.putImageData(imageData, 0, 0);
  };

  // --- Keyboard: keydown/keyup -> ABI key events -------------------------
  //
  // `scancodeFor()` (host.mjs) is the whole of the translation; anything
  // it does not recognise is left alone (no preventDefault, no post) so
  // the browser's own shortcuts still work for keys this 83-key keyboard
  // never had. Recognised keys are prevented from their usual browser
  // effect — Space scrolling the page, Backspace navigating back, Tab
  // moving focus, and since #84 the arrows and Page keys scrolling —
  // because the dev page's whole surface is the machine while it runs.
  //
  // Every key the page posts is noted in `held` (HeldKeys, host.mjs,
  // #313), and when the page loses the keyboard — the window blurs, or
  // the tab is hidden — a break is posted for each key still held,
  // through the same call a `keyup` takes. The `keyup` for those keys
  // is going to another window; this is the release the machine would
  // otherwise never see, and it is input, not a write to the BDA.
  const held = new HeldKeys();
  const postKey = (scancode, down) => {
    machine.postKey(scancode, down);
    held.note(scancode, down);
  };
  // The on-screen keyboard (#377). While it is up it takes the four
  // arrows and Return for itself — a person with a keyboard in front of
  // them closes it and gets them back, and a person without one never had
  // them. The *up* of such a key is swallowed only if its down was:
  // toggling the keyboard between the two would otherwise post a break
  // for a make the machine never saw, or leave a key held down forever.
  const keyboard = wireScreenKeyboard(machine, postKey);
  const takenByKeyboard = new Set();
  const onKey = (down) => (event) => {
    const scancode = scancodeFor(event.code);
    if (scancode === undefined) return;
    event.preventDefault();
    if (down) {
      if (keyboard.owns(event.code)) {
        takenByKeyboard.add(event.code);
        keyboard.press(event.code);
        return;
      }
    } else if (takenByKeyboard.delete(event.code)) {
      return;
    }
    postKey(scancode, down);
  };
  const onKeyDown = onKey(true);
  const onKeyUp = onKey(false);
  const releaseHeld = () => {
    for (const scancode of held.releaseAll()) postKey(scancode, false);
  };
  const onVisibilityChange = () => {
    if (document.visibilityState === 'hidden') releaseHeld();
  };
  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);
  window.addEventListener('blur', releaseHeld);
  document.addEventListener('visibilitychange', onVisibilityChange);

  // --- Audio: an AudioWorklet fed from the main thread --------------------
  //
  // See audio-worklet.mjs's own top comment for the threading contract
  // this implements: `machine.renderAudio()` runs here, on the main
  // thread (the machine thread, since this build has no wasm pthreads),
  // and every pulled chunk crosses to the worklet by `postMessage()`
  // with its buffer transferred, never shared.
  const audioContext = new AudioContext({ sampleRate: AUDIO_SAMPLE_RATE });
  await audioContext.audioWorklet.addModule('./audio-worklet.mjs');
  const speakerNode = new AudioWorkletNode(audioContext, 'amberfolio-speaker', {
    numberOfInputs: 0,
    numberOfOutputs: 1,
    outputChannelCount: [1],
  });
  speakerNode.connect(audioContext.destination);
  // AudioContext is created suspended by some browsers even inside a
  // user-gesture handler; resume() is a no-op if it is already running.
  await audioContext.resume();

  // The worklet's own starvation count, which is a different number from
  // core's: `machine.audioUnderruns()` counts pulls that ran past settled
  // virtual time, and this counts quanta the audio thread had no chunk
  // for. Both are host-pacing symptoms and neither is machine state
  // (platform.h) — and until #108 the page showed neither, so "why does
  // it sound wrong" had no number attached on either side of the
  // boundary. The worklet posts this only when it changes, so the message
  // rate is bounded by the thing being counted.
  let workletUnderruns = 0;
  speakerNode.port.onmessage = (event) => {
    const count = event.data?.underruns;
    if (typeof count === 'number') workletUnderruns = count;
  };

  // --- Volume and mute: #148's half of #106's scope ----------------------
  //
  // Two controls and not one, for the reason every mixer ever built has
  // two: mute is a latch that can be lifted, and lifting it should give
  // back the level that was there rather than one the player has to find
  // again. The gain that crosses to the worklet is `muted ? 0 : volume`.
  //
  // It crosses to the *worklet* and is not applied to the chunks here,
  // which audio-worklet.mjs's own comment argues at length: the held
  // level and the fade an underrun makes are on that side of the
  // boundary, so a mute applied on this one would not silence a stalled
  // tab. And it is not in core at all — a gain in
  // `audio_timeline::render()` would stop a sample being the exact
  // integral of the edge list, which is the property every number in
  // docs/hosts.md §4 is a measurement of.
  //
  // Nothing here is machine state: a run at 25% is the same run as one
  // at 100%, down to the last edge in the list and the last bit of the
  // frame.
  const sendGain = () => {
    const muted = muteCheckbox ? muteCheckbox.checked : false;
    const volume = volumeInput ? Number(volumeInput.value) / 100 : 1;
    speakerNode.port.postMessage({ gain: muted ? 0 : volume });
  };
  if (volumeInput) volumeInput.addEventListener('input', sendGain);
  if (muteCheckbox) muteCheckbox.addEventListener('change', sendGain);
  // Whatever the controls say now, which is not necessarily what they
  // said when the page loaded: a browser restores a form control's value
  // across a reload, and a page that started at full volume because it
  // never asked would be a control that lies.
  sendGain();

  setStatus(message);

  // --- The run loop: requestAnimationFrame, paced against the wall ------
  //
  // abi.h's snippet is "next += af_ticks_per_second() / 60;
  // af_machine_run_until(box, next)", and this page used to run it
  // verbatim, one increment per callback. That is correct for a caller
  // invoked at 60 Hz and this is not one: **rAF fires at the display's
  // refresh rate**, so the loop turned the monitor into a speed control —
  // 2x on a 120 Hz panel, 4x on a 240 Hz one, compounding with whatever
  // preset the control above was set to. That is #157, and it is a
  // violation of PLAN.md §4's rule rather than a tension with it: the
  // refresh rate is wall time, and it was deciding virtual time.
  //
  // So the callback asks `pacedAdvance()` (host.mjs) where elapsed *real*
  // time says virtual time should be, and runs there. rAF is then back to
  // being what §4 allows it to be — the thing that paces presentation —
  // and the display's rate decides only how finely a second is chopped
  // up, not how many seconds there are.
  //
  // The clamp is the other half, and it is the SDL host's rule wearing
  // browser clothes (hosts/sdl/src/main.cpp's top comment): a host that
  // falls behind does not sleep, and never runs the machine faster to
  // compensate. A backgrounded tab hands the next callback a delta of
  // minutes; advancing by all of it would be exactly the burst of
  // emulated instructions that comment forbids. Past the clamp, virtual
  // time falls behind the wall and stays behind — the loss is dropped,
  // not banked — and the readout says how often that happened.
  //
  // Presentation is untouched by any of this and stays on the generation
  // counter (platform.h's pull contract): a frame is drawn when a new one
  // exists, which on a 240 Hz display is now every fourth callback rather
  // than every one.
  // What the clock out here reads, at the tick this run is about to step
  // from (#343). `ensureMachine()` took one too, and this one replaces
  // it: everything between the two — choosing a directory, ingesting a
  // journal, reading the code wheel — is wall time the machine's clock
  // would otherwise never have seen, and a seed is an origin, so it would
  // have left every journal row stamped that far behind for the whole
  // session. `seedWallClock`'s own comment has the rest.
  seedWallClock(machine, appendConsole);

  const ticksPerSecond = machine.ticksPerSecond();
  let next = machine.time();
  let lastTimestamp = null;
  let stalls = 0;
  let nextHealthTick = next;
  let lastGeneration = -1;

  /// The run is over: say why, in the one fixed format the desktop host
  /// prints (machine/report.h). Formatted in core precisely so that "the
  /// same stop line at the same step" (#84) is a comparison anybody can
  /// make by eye.
  const finish = (how) => {
    showHealth();
    // One last look at the disk before the loop stops (#381). A machine
    // that stopped on its own — a program that exited, a refusal — is
    // exactly the case where the last half-second's writes would
    // otherwise never be looked at again.
    if (writeBack) writeBack();
    setStatus(
      how === AF_RUN_END_STOPPED
        ? `the machine stopped - see the report below.`
        : `the run was cut short at ${machine.steps()} steps - see the report below.`,
    );
    drainLog();
    if (partialLine.length > 0) {
      appendConsole(`${partialLine}\n`);
      partialLine = '';
    }
    const lost = machine.logDropped();
    if (lost > 0) {
      appendConsole(`[host] ${lost} diagnostic line(s) dropped - the log ring overflowed\n`);
    }
    appendConsole(machine.stopReport(how));
    const trace = machine.traceReport();
    if (!trace.startsWith('amberfolio: stop trace=off')) appendConsole(trace);

    // What each enabled seam actually did, in the desktop host's own
    // words (#131, #147). `armed` says an address was computed; `fired`
    // says a handler ran there, and a seam that is on and armed and
    // fired nothing is the failure that reads exactly like success. The
    // end of the run it belongs to is the one place a reader gets told
    // for free, and until now a browser run could not say it at all.
    for (const seam of machine.seamList()) {
      if (seam.state !== AF_SEAM_ON) continue;
      appendConsole(`amberfolio: seam ${seam.id} ${formatSeamFired(seam)}\n`);
    }
    keyboard.dispose();
    window.removeEventListener('keydown', onKeyDown);
    window.removeEventListener('keyup', onKeyUp);
    window.removeEventListener('blur', releaseHeld);
    document.removeEventListener('visibilitychange', onVisibilityChange);
  };

  // The diagnostics stream, as the lines core renders from it
  // (machine/log.h, #108). Before this the page said nothing about what a
  // program did beyond the number it stopped with, while the same run on
  // the desktop host printed its notices, its file activity and every
  // seam transition - which made driving docs/playable.md's legs here
  // strictly worse than driving them there, for no reason but a missing
  // door.
  //
  // A drain can end mid-line (abi.h), so the tail of one is held back and
  // becomes the head of the next. Nothing is lost: the remainder is still
  // the ring's, not this page's.
  let partialLine = '';
  const drainLog = () => {
    const text = partialLine + machine.readLog();
    const lastBreak = text.lastIndexOf('\n');
    if (lastBreak < 0) {
      partialLine = text;
      return;
    }
    partialLine = text.slice(lastBreak + 1);
    appendConsole(text.slice(0, lastBreak + 1));
  };

  /// The health readout: how far the run has got, what the audio path
  /// had to paper over to keep up, and — since #147 — what each enabled
  /// seam has actually done. #106's policies have a visible half and
  /// this is it.
  ///
  /// Every half virtual second rather than every frame. It is a readout
  /// and not an instrument — the driver (`tools/drive.mjs`) is where a
  /// number gets measured — and writing the same three figures into the
  /// DOM sixty times a second is itself the sort of thing that causes
  /// the underruns it would be reporting. The seam rows ride the same
  /// cadence for the same reason.
  const showHealth = () => {
    if (refreshSeams) refreshSeams();
    if (!healthEl) return;
    // The listening level joins the line only when it is not unity, the
    // same rule and for the same reason as the SDL host's report line
    // (#148): a default run reads as it always has, and a run somebody
    // turned down says so where they will look when they wonder why
    // they heard nothing.
    const muted = muteCheckbox ? muteCheckbox.checked : false;
    const volume = volumeInput ? Number(volumeInput.value) : 100;
    const level = muted
      ? ' volume=muted'
      : volume === 100
        ? ''
        : ` volume=${volume}%`;
    // `stalls=` joins the line by the same rule and for the same reason
    // (#157): a run that kept up reads as it always has, and a run whose
    // virtual time was left behind — a backgrounded tab, a browser that
    // could not keep up at this preset — says so, where somebody
    // wondering why the clock inside disagrees with theirs will look.
    // Each one is up to MAX_CATCH_UP_SECONDS of wall time the machine
    // did not run, deliberately.
    const behind = stalls > 0 ? ` stalls=${stalls}` : '';
    healthEl.textContent =
      `frames=${machine.frameGeneration()} steps=${Math.round(machine.steps())} ` +
      `| audio underruns=${Math.round(machine.audioUnderruns())} ` +
      `resyncs=${Math.round(machine.audioResyncs())} ` +
      `starved=${workletUnderruns}${behind}${level}`;
  };
  showHealth();

  const frame = (timestamp) => {
    if (stepBudget !== 0 && machine.steps() >= stepBudget) {
      finish(AF_RUN_END_STEP_BUDGET);
      return;
    }

    // Where elapsed wall time says to run to, and how much of it that is.
    // The first callback anchors the clock and advances nothing; a
    // clamped one is counted and the excess is dropped rather than owed.
    const paced = pacedAdvance({
      tick: next,
      since: lastTimestamp,
      now: timestamp,
      ticksPerSecond,
    });
    lastTimestamp = timestamp;
    next = paced.tick;
    if (paced.clamped) {
      stalls += 1;
      if (stalls === 1) {
        appendConsole(
          `[host] the page fell more than ${MAX_CATCH_UP_SECONDS}s behind; ` +
            'virtual time is left behind rather than caught up on\n',
        );
      }
    }
    const status = machine.runUntil(next);

    const generation = machine.frameGeneration();
    if (generation !== lastGeneration) {
      present();
      lastGeneration = generation;
    }

    const consoleBytes = machine.readConsole();
    if (consoleBytes.length > 0) {
      appendConsole(decodeConsoleBytes(consoleBytes));
    }
    drainLog();

    // Somebody answered the code-wheel challenge (M6-C1b, #292). At most
    // once in a session, and the check is a boolean the module already
    // raised — the same arrangement the journal's store has, and the
    // reason #229 put a door on that flag rather than making a page hash
    // a store every frame.
    //
    // The write is asynchronous since #381, so the flag is left raised
    // and a second attempt is kept out with `codeWheelKeep` rather than
    // by lowering something early. A refusal is reported and not retried
    // in this run: the drawer still owes the key, so the next thing that
    // flushes it — an ingestion, a forget, the next answer — tries again.
    if (machine.codeWheelStoreChanged() && codeWheelKeep === null) {
      codeWheelKeep = keepCodeWheelStore(machine)
        .then(({ kept: went, why }) => {
          // Lowered only once the bytes are somewhere, never before: a
          // flag cleared on a database that refused them would lose the
          // answer.
          if (went) {
            machine.codeWheelStoreClearChanged();
            codeWheelKeep = null;
          }
          const status = el(CODE_WHEEL_STATUS_ID);
          if (status) {
            status.textContent = went
              ? 'answered, and remembered for this copy'
              : `answered, but ${why}`;
          }
          appendConsole(
            went
              ? '[host] code wheel answered - remembered for this copy\n'
              : `[host] code wheel answered. ${why}\n`,
          );
        })
        .catch((problem) => {
          appendConsole(`[host] keeping the code wheel's answer failed: ${problem}\n`);
        });
    }


    // Exactly the audio this callback's slice of virtual time contains —
    // not a fixed frame's worth, which on a 240 Hz display would be four
    // times more audio than the machine made and would leave the worklet
    // dropping most of it (#157). Handed over whether or not the port is
    // still connected: a detached one simply drops the message, which is
    // no worse than the underrun the worklet already plays as silence.
    const audioFrames = Math.round((paced.advance / ticksPerSecond) * AUDIO_SAMPLE_RATE);
    if (audioFrames > 0) {
      const { samples } = machine.renderAudio(audioFrames, AUDIO_SAMPLE_RATE);
      speakerNode.port.postMessage(samples, [samples.buffer]);
    }

    // Every half second of *virtual* time, which is what this cadence
    // always meant (its own comment says so) and could say by counting
    // callbacks only while a callback was a fixed frame.
    if (next >= nextHealthTick) {
      showHealth();
      // And whatever the player has written to the disk, back into this
      // browser (#381). On this cadence rather than every frame: what
      // moves the generation is a save, which writes a file at a time
      // over several frames, and a walk per frame would be thirteen
      // write-backs of a half-written slot to reach the one that
      // matters. Half a second of virtual time is late enough that the
      // save is done and early enough that a tab closed straight after
      // one still kept it.
      if (writeBack) writeBack();
      nextHealthTick = next + ticksPerSecond / 2;
    }

    if (status !== AF_OK) {
      // Keep presenting and draining the console after a stop
      // (platform.h: "the pulls keep working... it is the console and
      // the diagnostics that say why it stopped"), but there is nothing
      // left to run.
      finish(AF_RUN_END_STOPPED);
      return;
    }

    window.requestAnimationFrame(frame);
  };

  // A page being closed or navigated away from is a host quit, and the
  // report says so rather than nothing — the same distinction the desktop
  // host draws between a machine that refused something and a person who
  // walked away.
  window.addEventListener('pagehide', () => {
    if (!machine.stopped()) console.log(machine.stopReport(AF_RUN_END_HOST_QUIT));
  });

  window.requestAnimationFrame(frame);
}
