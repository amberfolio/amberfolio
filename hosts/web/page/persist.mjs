// SPDX-License-Identifier: AGPL-3.0-only
//
// What this browser remembers between visits (M6, #381).
//
// Until now the page kept two strings in `localStorage` and nothing else:
// a reload lost the disk, the saves and the sidecars, so a player dropped
// their copy every visit and could not carry a party from one to the
// next. M6's exit is a player going from artifacts-in-hand to playing
// without reading source code, and a shell that forgets the disk does not
// reach it.
//
// DOM-free, like host.mjs and journal.mjs and for the same reason:
// `ctest --preset wasm` imports it under node, where there is neither a
// `document` nor an `indexedDB`. Everything that touches the database
// takes the factory as an argument, and the three decisions worth arguing
// about — what belongs on which side of the save layer, what a refusal
// says, what a synchronous drawer owes an asynchronous database — are
// plain functions with no database anywhere near them.
//
//
// Four drawers, and the save layer draws the line between the first two
// ---------------------------------------------------------------------
//
// One database, `amberfolio`, with four object stores:
//
//   `disk`      the copy the player dropped: every file that landed on
//               the machine's filesystem, keyed by the path core
//               canonicalized it to. Written once, at the drop.
//   `play`      the player's own files — the saved games, the party
//               records, the roster and this build's own sidecars —
//               keyed the same way and written back while the game runs.
//   `text`      the small strings a module owns and this page merely
//               keeps: the journal's store, the journal's read log and
//               the code wheel's answered copies. Keyed by the names they
//               had in `localStorage`, because a drawer that moved house
//               kept its label.
//   `settings`  this page's own choices, as JSON. One today, the program
//               last booted. The toggle panel's are #383's, and this is
//               the empty drawer they go in.
//
// **Which of `disk` and `play` a file belongs in is not this file's
// decision.** `machine/save_layer.h` is the fact table for that — which
// of the files on the machine's filesystem are the player's and which are
// the publisher's — and `Machine.saveLayerOf(path)` is how a host reads
// it (`docs/hosts.md` §6). `partitionDisk()` below asks it about every
// file: a `config` row goes with the copy, everything else it claims goes
// with the playthrough, and a path it claims not at all is a game file. A
// page that kept its own list of which names are saves would be a second
// answer to the one question this project cannot have two answers to.
//
// Which is also why a write-back is **armed only when there is a save
// layer to arm it with**: a program this build has no table for answers
// null, and §6's rule for that answer is to persist nothing rather than
// persist a guess.
//
//
// What is not in here
// -------------------
//
// **Nothing persisted reaches the machine's serialization or a
// recording's checkpoints.** Everything above crosses the ABI through
// `vfsPut()`/`vfsGet()` and the two store doors, which is the road a
// dropped directory and an ingested journal already travel; the
// bookkeeping — which generation was last seen, which keys the drawer
// owes the database — stays on this side and is never machine state.
//
// **And nothing here turns a seam on.** Seam state is configuration and
// is safe to persist (#265), but *off by default* has to survive
// persistence too: a player who never opened a panel must not find a seam
// enabled because a previous visit's record said so. So the `settings`
// drawer holds no seam state today, and the rule for #383 when it does is
// that a stored choice is applied through the same `seamEnable()` a click
// takes, with the refusal reported — never by assuming it took.
//
//
// Refusals
// --------
//
// A quota refusal is a report, not a swallowed exception ("log, don't
// fake", CLAUDE.md). Every write here lands or throws, the caller says so
// in words, and the sentence `describeRefusal()` builds names the error,
// what was refused, how big it was and how much room the browser admits
// to having. The flags that say a store has moved
// (`journalStoreChanged`, `codeWheelStoreChanged`) are lowered **only
// once the bytes are somewhere**, so a refused write is retried at the
// next change rather than lost.

/// The one database, and its version. A version bump is a schema change
/// and runs `onupgradeneeded`; adding an object store is the only kind of
/// change made so far, and it is additive.
export const DATABASE_NAME = 'amberfolio';
export const DATABASE_VERSION = 1;

export const DISK_STORE = 'disk';
export const PLAY_STORE = 'play';
export const TEXT_STORE = 'text';
export const SETTINGS_STORE = 'settings';

/// Every store the schema has, in the order they were added. The upgrade
/// walks this, so a store added below is created for a browser that has
/// the database already as well as for one that does not.
export const STORES = Object.freeze([
  DISK_STORE,
  PLAY_STORE,
  TEXT_STORE,
  SETTINGS_STORE,
]);

/// The settings key the program last booted is remembered under, so a
/// returning player finds it already chosen. This page's own choice and
/// not the machine's, which is what the `settings` store is for.
export const PROGRAM_SETTING = 'program';

/// The database factory, or null where there is none.
///
/// Behind a try for `journal.mjs`'s `browserStorage()` reason:
/// `indexedDB` is not merely absent under node, it *throws* on access in
/// a browser told to block site data, and a page that let that escape
/// would fail to come up at all for a player who keeps nothing.
export function databaseFactory() {
  try {
    return globalThis.indexedDB ?? null;
  } catch {
    return null;
  }
}

/// One `IDBRequest` as a promise.
function requested(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () =>
      reject(request.error ?? new Error('the request failed'));
  });
}

/// One `IDBTransaction` as a promise that settles when it does.
///
/// Both halves matter: a `put` that runs out of room fails its own
/// request *and* aborts the transaction, and it is the abort that carries
/// the `QuotaExceededError` a caller has to report. Waiting on the
/// requests alone would leave a write that never landed looking like one
/// that did.
function finished(transaction) {
  return new Promise((resolve, reject) => {
    transaction.oncomplete = () => resolve();
    transaction.onabort = () =>
      reject(transaction.error ?? new Error('the write was aborted'));
    transaction.onerror = () =>
      reject(transaction.error ?? new Error('the write failed'));
  });
}

/// An open database, with the operations this page needs on it.
///
/// Deliberately small: a `Map` out, `[key, value]` pairs in, one
/// transaction per call. Nothing here is clever about batching, because
/// the biggest thing it writes is a game directory, once.
export class Kept {
  constructor(database) {
    this.database = database;
  }

  /// Everything in one store, as a `Map` in key order.
  async all(store) {
    const transaction = this.database.transaction(store, 'readonly');
    const entries = new Map();
    const cursor = transaction.objectStore(store).openCursor();
    await new Promise((resolve, reject) => {
      cursor.onsuccess = () => {
        const at = cursor.result;
        if (!at) {
          resolve();
          return;
        }
        entries.set(at.key, at.value);
        at.continue();
      };
      cursor.onerror = () =>
        reject(cursor.error ?? new Error('the listing failed'));
    });
    await finished(transaction);
    return entries;
  }

  /// How many records one store holds, without reading a byte of them.
  async count(store) {
    const transaction = this.database.transaction(store, 'readonly');
    const answer = await requested(transaction.objectStore(store).count());
    await finished(transaction);
    return answer;
  }

  /// One record, or null.
  async get(store, key) {
    const transaction = this.database.transaction(store, 'readonly');
    const answer = await requested(transaction.objectStore(store).get(key));
    await finished(transaction);
    return answer === undefined ? null : answer;
  }

  /// `puts` (an iterable of `[key, value]`) written and `removes` (an
  /// iterable of keys) deleted, in one transaction, optionally over an
  /// emptied store.
  ///
  /// Throws what the browser threw. The caller reports it — see
  /// `describeRefusal()` — because only the caller knows what was being
  /// kept and what it is worth saying about it.
  async write(store, puts = [], removes = [], { emptyFirst = false } = {}) {
    const transaction = this.database.transaction(store, 'readwrite');
    const table = transaction.objectStore(store);
    if (emptyFirst) table.clear();
    for (const [key, value] of puts) table.put(value, key);
    for (const key of removes) table.delete(key);
    await finished(transaction);
  }

  /// Every record in `stores` gone, in one transaction.
  async clear(stores) {
    const names = [...stores];
    if (names.length === 0) return;
    const transaction = this.database.transaction(names, 'readwrite');
    for (const name of names) transaction.objectStore(name).clear();
    await finished(transaction);
  }

  close() {
    this.database.close();
  }
}

/// Open the database, making any store this browser does not have yet.
///
/// Answers `{ kept, why }`: a `Kept` and no `why`, or no `kept` and a
/// sentence. A browser that keeps nothing is not an error — a private
/// window is a thing a person is allowed to be in — but it *is* a
/// difference in what this page can promise, so it is said rather than
/// discovered.
export async function open({
  factory = databaseFactory(),
  name = DATABASE_NAME,
  version = DATABASE_VERSION,
} = {}) {
  if (!factory) {
    return {
      kept: null,
      why: 'this browser keeps nothing, so a reload starts over',
    };
  }
  try {
    const request = factory.open(name, version);
    request.onupgradeneeded = () => {
      const database = request.result;
      for (const store of STORES) {
        if (!database.objectStoreNames.contains(store)) {
          database.createObjectStore(store);
        }
      }
    };
    const database = await requested(request);
    return { kept: new Kept(database), why: null };
  } catch (problem) {
    return {
      kept: null,
      why:
        'this browser would not open its own storage' +
        ` (${problem?.name ?? problem}), so a reload starts over`,
    };
  }
}

/// The whole database gone.
///
/// Answers `{ forgotten, why }`. `kept` is closed first when one is
/// given: a delete waits on every open connection, and a page that did
/// not close its own would sit there blocked with nothing to show for it.
export async function forgetEverything({
  factory = databaseFactory(),
  name = DATABASE_NAME,
  kept = null,
} = {}) {
  if (kept) kept.close();
  if (!factory) return { forgotten: false, why: 'this browser keeps nothing' };
  try {
    const request = factory.deleteDatabase(name);
    await new Promise((resolve, reject) => {
      request.onsuccess = () => resolve();
      request.onerror = () =>
        reject(request.error ?? new Error('the delete failed'));
      // Another tab of this page still holds the database open. Nothing
      // is deleted until it lets go, so say which it is rather than hang.
      request.onblocked = () =>
        reject(new Error('another tab of this page still has it open'));
    });
    return { forgotten: true, why: null };
  } catch (problem) {
    return {
      forgotten: false,
      why:
        'this browser would not forget it: ' +
        `${problem?.message ?? problem?.name ?? problem}`,
    };
  }
}

/// How much room the browser admits to, or null. Never a number this page
/// relies on: the figures are advisory, engines round them, and a browser
/// is free to answer nothing at all.
export async function estimateStorage(storage = globalThis.navigator?.storage) {
  try {
    if (!storage?.estimate) return null;
    const { usage, quota } = await storage.estimate();
    if (!Number.isFinite(usage) || !Number.isFinite(quota)) return null;
    return { usage, quota };
  } catch {
    return null;
  }
}

/// A byte count as a person reads one.
export function sized(bytes) {
  if (!Number.isFinite(bytes) || bytes < 0) return '? bytes';
  if (bytes < 1024) return `${Math.round(bytes)} bytes`;
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} KiB`;
  // Up to GiB, because a browser's own answer to "how much room is
  // there" is measured in them and `10240.0 MiB` is a number nobody
  // reads.
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MiB`;
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GiB`;
}

/// What a refused write says.
///
/// A quota refusal is the one failure this file has to get right in
/// words: a browser that will not keep an hour of somebody's play is not
/// an exception to swallow, and "it didn't save" with no reason attached
/// is not something a player can act on. So the sentence names the error,
/// what was refused, how big it was, how much room the browser admits to
/// — and, because it is the part that stops a person panicking, that
/// nothing already kept was touched.
///
/// Pure, which is why `estimate` is an argument rather than a lookup:
/// `hosts/web/tests/smoke.mjs` drives it.
export function describeRefusal(
  problem,
  { what, bytes = 0, estimate = null } = {},
) {
  const name = problem?.name ?? 'a failure with no name';
  const detail = problem?.message ? `: ${problem.message}` : '';
  const room = estimate
    ? ` This origin is using ${sized(estimate.usage)} of ${sized(estimate.quota)}.`
    : ' This browser would not say how much room it has.';
  const advice =
    name === 'QuotaExceededError'
      ? " Free some of this site's data in your browser, or drop a smaller" +
        ' directory, and try again.'
      : '';
  return (
    `NOT kept: ${what}` +
    (bytes > 0 ? ` (${sized(bytes)})` : '') +
    ` - ${name}${detail}.` +
    room +
    ' Nothing was written and what was already kept is untouched.' +
    advice
  );
}

/// Which of the machine's files belong to the playthrough, which to the
/// copy, and which are neither.
///
/// `machine` is anything with `vfsList()` and `saveLayerOf(path)` — the
/// `Machine` façade in host.mjs, or a stand-in in a test. `known` is the
/// paths the `disk` store already holds, which is what makes the last
/// answer mean anything.
///
/// - **`play`**: every path the save layer claims that is not a `config`
///   row — the saved games, the party records, the roster, characters
///   kept under a name, and this build's own sidecars. These go to the
///   `play` store, and they are what a write-back writes.
/// - **`config`**: `config` rows. `POOL.CFG` is the program's settings
///   and §6 puts it on the game's side of the boundary, so it belongs to
///   the copy — but it is the one file on that side that changes, so it
///   is named apart from the rest and written back into `disk`.
/// - **`game`**: a path the layer does not claim that was dropped. The
///   publisher's bytes, already in `disk` and never written again.
/// - **`strangers`**: a path the layer does not claim and nobody dropped.
///   §6 calls this the honest failure to watch for: a file that appeared
///   during a run and that the table does not name is a gap in the table,
///   an issue to file rather than a file to quietly keep. So it is
///   reported and **not** persisted — a guess about whose bytes they are
///   is the one thing this must not make.
export function partitionDisk(machine, known = new Set()) {
  const play = [];
  const config = [];
  const game = [];
  const strangers = [];
  for (const entry of machine.vfsList()) {
    const row = machine.saveLayerOf(entry.path);
    if (row === null || row === undefined) {
      if (known.has(entry.path)) game.push(entry.path);
      else strangers.push(entry.path);
      continue;
    }
    if (row.kind === 'config') config.push(entry.path);
    else play.push(entry.path);
  }
  return { play, config, game, strangers };
}

/// Read `paths` back off the machine as `[path, bytes]` pairs, skipping
/// anything that would not read whole.
///
/// `vfsGet()` answers null for a file it could not read, and a null in a
/// record would come back on the next visit as a file of no bytes — a
/// corrupt save wearing the shape of a real one. Skipped and counted
/// instead; the caller says how many.
export function readBack(machine, paths) {
  const records = [];
  const unreadable = [];
  let bytes = 0;
  for (const path of paths) {
    const content = machine.vfsGet(path);
    if (content === null || content === undefined) {
      unreadable.push(path);
      continue;
    }
    records.push([path, content]);
    bytes += content.length;
  }
  return { records, unreadable, bytes };
}

/// A synchronous drawer over an asynchronous database.
///
/// `journal.mjs`'s store functions take a drawer with `localStorage`'s
/// three methods and want an answer immediately — they are called at the
/// moment the module comes up, before anything can look at the store, and
/// making them asynchronous would spread `await` through code whose whole
/// contract is that a store is either there or it is not. IndexedDB
/// cannot answer immediately, so the records are read *once* into a `Map`
/// and this is the drawer over it.
///
/// Which leaves the interesting half: a `setItem` that cannot fail is a
/// `setItem` that cannot report a quota. So it does not pretend to.
/// Writes are collected here, `changes()` hands them to the caller, and
/// the caller writes them to the database and reports what happened
/// (`describeRefusal`). A key stays owed until `settled()` says it landed,
/// so a refused write is retried at the next change instead of vanishing.
export function cacheDrawer(entries = []) {
  const values = new Map(entries);
  const owed = new Set();
  return {
    getItem(key) {
      return values.has(key) ? values.get(key) : null;
    },
    setItem(key, value) {
      const text = String(value);
      if (values.get(key) === text) return;
      values.set(key, text);
      owed.add(key);
    },
    removeItem(key) {
      if (!values.has(key)) return;
      values.delete(key);
      owed.add(key);
    },
    /// What the database has not been told yet, as `[key, value]` with a
    /// null value for a key that was removed.
    changes() {
      return [...owed].map((key) => [
        key,
        values.has(key) ? values.get(key) : null,
      ]);
    },
    /// Those keys are in the database now.
    settled(keys) {
      for (const key of keys) owed.delete(key);
    },
    /// Whether anything is owed, and how much text is held. Both are for
    /// a sentence a page shows, never for a decision.
    owes() {
      return owed.size;
    },
    characters() {
      let total = 0;
      for (const value of values.values()) total += value.length;
      return total;
    },
  };
}

/// The `text` store's records into a drawer, and whatever an older visit
/// left in `localStorage` into the `text` store on the way.
///
/// The journal's transcription and the code wheel's answered copies were
/// in `localStorage` before this (M5-E3f, #292), and a player who spent an
/// hour of OCR there is not going to be told to do it again because the
/// drawer moved house. So a key the database does not have yet and the
/// old drawer does is copied across and **left where it was**. Left,
/// rather than moved, because bytes some later build cannot read are
/// bytes a player still has; *Forget everything* is what removes them,
/// and it removes both.
///
/// Answers `{ drawer, migrated }` — the keys that came across, for the
/// line a page prints.
export async function textDrawer(kept, keys, legacy = null) {
  const records = kept ? await kept.all(TEXT_STORE) : new Map();
  const migrated = [];
  if (legacy) {
    const arrived = [];
    for (const key of keys) {
      if (records.has(key)) continue;
      let text = null;
      try {
        text = legacy.getItem(key);
      } catch {
        text = null;
      }
      if (text === null || text === '') continue;
      records.set(key, text);
      arrived.push([key, text]);
      migrated.push(key);
    }
    if (kept && arrived.length > 0) await kept.write(TEXT_STORE, arrived);
  }
  return { drawer: cacheDrawer(records), migrated };
}
