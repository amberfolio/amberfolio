// SPDX-License-Identifier: AGPL-3.0-only
//
// What this build recognises, before it has loaded anything (#207).
//
// `Machine.edition()` answers after a program is in, and answers with a
// name or with nothing. A page onboarding somebody (#265) has to answer
// earlier and say more: here is what you dropped, here is what it looks
// like, here is what is missing. That is the whole requirement table,
// not the one fingerprint the seam engine is keyed on, and it is a file
// rather than an ABI call for one reason — a page wants the checklist
// while the module is still downloading.
//
// The file is `editions.json`, attached to every release beside
// `manifest.json` and served from the same directory as this module
// (docs/hosts.md §5). It is the *same* table the C++ hosts carry:
// `hosts/common` compiles its arrays out of it at build time, so a
// browser and a desktop cannot disagree about what an edition is made
// of. Adding an edition is editing that file.
//
// Nothing here is content. A name, a size and a SHA-256 per file is the
// class of fact CONTRIBUTING.md permits — a digest names a file without
// carrying a byte of it.

/// Where the table sits: beside this module, which is where the release
/// puts it and where `serve-web.py` serves it from.
export const EDITIONS_URL = './editions.json';

/// The schema this reader speaks. A consumer pins it, so a table that
/// changed shape has to be refused rather than half-read.
export const EDITIONS_SCHEMA = 'amberfolio.editions/2';

/// The editions in `table`, checked. Throws on a table this reader does
/// not speak, which is the honest answer: a checklist rendered off a
/// misread table would be wrong about a player's own files.
export function readEditions(table) {
  if (table === null || typeof table !== 'object') {
    throw new Error('editions.json is not an object');
  }
  if (table.schema !== EDITIONS_SCHEMA) {
    throw new Error(
      `editions.json declares schema ${JSON.stringify(table.schema)}; ` +
        `this page reads ${EDITIONS_SCHEMA}`,
    );
  }
  if (!Array.isArray(table.editions) || table.editions.length === 0) {
    throw new Error('editions.json names no editions');
  }
  return table.editions;
}

/// Fetch the table. Separate from `readEditions` so that a caller with
/// the bytes already — a test, a site that bundles them — needs no
/// network at all.
export async function loadEditions(url = EDITIONS_URL) {
  const response = await fetch(url, { cache: 'no-cache' });
  if (!response.ok) {
    throw new Error(`${url}: ${response.status} ${response.statusText}`);
  }
  return readEditions(await response.json());
}

/// What a set of fingerprinted files turned out to be.
///
/// `offered` is `{ name, sha256 }` per file, in whatever order a host
/// found them — `Machine.vfsList()` and `Machine.vfsFingerprint()` are
/// where a page gets both.
///
/// **Files are matched on the digest, never on the name**, which is the
/// same rule `host::match_edition()` follows: a renamed file still
/// matches, and a file carrying a required artifact's name with different
/// bytes matches nothing — so it lands in `unclaimed` while the artifact
/// it is not lands in `missing`. Those two lines together are the fact a
/// player can act on, and no single list says it.
///
/// **A `configuration` row is matched on the name** — the last path
/// component, any case — because its bytes are whatever the player's
/// launcher wrote; and only once a row has been chosen by digest, so a
/// lone POOL.CFG names nothing. The closest row is the one the most
/// files belong to by digest, the earlier row on a tie: two releases of
/// one program share most of their files, and the ones only one of them
/// ships decide. A configuration row may carry the digest its seller
/// ships it with, and a file of its name with those bytes counts toward
/// its row: Curse's two releases differ in nothing else.
///
/// Answers `{ edition, matched, missing, unclaimed, complete }`.
/// `edition` is null when not one file belonged to any edition, which is
/// the honest answer for a directory holding something else — and for an
/// edition nobody has fingerprinted yet, in which case `unclaimed` is
/// everything and a report is the file names and those hashes.
export function matchEdition(editions, offered) {
  const digest = (value) => (typeof value === 'string' ? value.toLowerCase() : '');
  const have = new Set(offered.map((file) => digest(file.sha256)).filter(Boolean));
  const leaf = (name) =>
    typeof name === 'string' ? name.split(/[\\/]/).pop().toUpperCase() : '';
  const names = new Set(offered.map((file) => leaf(file.name)).filter(Boolean));
  const isConfiguration = (artifact) => artifact.kind === 'configuration';
  // A configuration row that carries its seller's digest counts toward
  // its row only when a file of that name has those bytes: the one thing
  // that tells two releases of byte-identical files apart.
  const shipped = new Set(
    offered.map((file) => `${leaf(file.name)}\n${digest(file.sha256)}`),
  );
  const counts = (artifact) =>
    isConfiguration(artifact)
      ? digest(artifact.sha256) !== '' &&
        shipped.has(`${leaf(artifact.name)}\n${digest(artifact.sha256)}`)
      : have.has(digest(artifact.sha256));

  let edition = null;
  let matched = [];
  for (const candidate of editions) {
    const artifacts = Array.isArray(candidate.artifacts) ? candidate.artifacts : [];
    const here = artifacts.filter(counts);
    if (here.length === 0) continue;
    // Strictly greater, so a tie keeps the earlier row: the table's own
    // order is the only tiebreak, and taking the later one would make
    // the answer depend on where somebody appended.
    if (edition === null || here.length > matched.length) {
      edition = candidate;
      matched = here;
    }
  }
  if (edition === null) {
    return { edition: null, matched: [], missing: [], unclaimed: [...offered], complete: false };
  }

  // The configuration rows, by name, now that a row is chosen.
  const configured = edition.artifacts.filter(
    (artifact) => isConfiguration(artifact) && names.has(leaf(artifact.name)),
  );
  matched = edition.artifacts.filter(
    (artifact) => matched.includes(artifact) || configured.includes(artifact),
  );
  const claimed = new Set(matched.map((artifact) => digest(artifact.sha256)).filter(Boolean));
  const claimedNames = new Set(configured.map((artifact) => leaf(artifact.name)));
  // A required artifact nobody offered.
  const missing = edition.artifacts.filter(
    (artifact) => artifact.required === true && !matched.includes(artifact),
  );
  const unclaimed = offered.filter(
    (file) => !claimed.has(digest(file.sha256)) && !claimedNames.has(leaf(file.name)),
  );
  return { edition, matched, missing, unclaimed, complete: missing.length === 0 };
}

/// Where a copy belongs on the machine's drive, and which of its files
/// boots it (#624, #626).
///
/// The storefront copies are laid out for a launcher that mounts their
/// folder at the edition row's `install` directory and changes into it,
/// and each one's configuration file names its save directory under
/// that (`docs/hosts.md` §2c). A page is that launcher. This is the
/// desktop host's own decision (`hosts/sdl/src/install.cpp`), made over
/// the same table: a copy is moved to its row's `install` only when
///
///   * a file **at the root of what was given** is a boot file some
///     edition names, by digest — the folder given is the copy's own;
///   * the files given hold **every artifact that edition requires**,
///     because a layout is a fact about one edition and a closest match
///     is not one;
///   * and the row's `install` is not the root.
///
/// So a copy given as the folder that holds `CURSE\` is left where it
/// is — its boot file is a directory down, which is already where its
/// launcher would have put it — and so is one whose row installs at the
/// root, and so is anything this build does not recognise.
///
/// `offered` is `matchEdition`'s: `{ name, sha256 }` per file, the name
/// with whatever directories the file sits in, either separator, with or
/// without a leading one.
///
/// Answers `{ edition, complete, boot, install }`: the row matched (or
/// null), whether the copy is whole, the name **as offered** of the file
/// that boots it (or null when none here carries the row's fingerprint),
/// and the `install` directory to lay the copy out under — **null for
/// "leave it where it was given"**.
export function settleInstall(editions, offered) {
  const digest = (value) => (typeof value === 'string' ? value.toLowerCase() : '');
  const leaf = (name) => String(name).split(/[\\/]/).pop().toUpperCase();
  const atRoot = (name) => !/[\\/]/.test(String(name).replace(/^[\\/]+/, ''));

  const match = matchEdition(editions, offered);
  const answer = {
    edition: match.edition,
    complete: match.complete,
    boot: null,
    install: null,
  };
  if (match.edition === null) return answer;

  const wanted = digest(match.edition.fingerprint);
  const boots = offered.filter(
    (file) => wanted !== '' && digest(file.sha256) === wanted,
  );
  if (boots.length === 0) return answer;
  // One that sits at the root and carries the row's own name for it,
  // then either alone, then whatever has the bytes: a renamed boot file
  // is still the boot file, which is `matchEdition`'s rule too.
  const named = (file) =>
    leaf(file.name) === String(match.edition.boot ?? '').toUpperCase();
  const boot =
    boots.find((file) => atRoot(file.name) && named(file)) ??
    boots.find((file) => atRoot(file.name)) ??
    boots.find(named) ??
    boots[0];
  answer.boot = boot.name;

  const install =
    typeof match.edition.install === 'string' ? match.edition.install : '\\';
  const installsAtRoot = install.replace(/[\\/]/g, '') === '';
  if (match.complete && atRoot(boot.name) && !installsAtRoot) {
    answer.install = install;
  }
  return answer;
}

/// `path`, as given, under the directory `install`: `SAVE/SAVGAMA.DAT`
/// under `\CURSE` is `\CURSE\SAVE/SAVGAMA.DAT`. Joined and nothing more —
/// what either separator means, and every other rule about a name, is
/// core's when the path crosses the ABI (`picker.mjs`'s own top comment).
export function placeUnder(install, path) {
  return `${String(install).replace(/[\\/]+$/, '')}\\${String(path).replace(/^[\\/]+/, '')}`;
}

/// Which program the page's select opens on (#626): one of `options`'
/// paths, or null when there are none.
///
/// `options` are `{ path, sha256 }` in the order the select lists them.
/// In order of preference:
///
///   1. the program **remembered** from the last boot, when it is one of
///      the options as spelled;
///   2. the option holding the bytes the remembered name reads as
///      (`rememberedSha256`, core's own answer for that name), when
///      exactly one does — a setting written in another spelling of the
///      same path names the same file, and it is core that says which
///      file a name is, never this page;
///   3. the edition's **boot** file (`settleInstall`'s `boot`), so a
///      recognised copy opens on the program its launcher runs and not
///      on whichever `.EXE` sorts first;
///   4. the first option.
export function programChoice(
  options,
  { remembered = null, rememberedSha256 = null, boot = null } = {},
) {
  const digest = (value) => (typeof value === 'string' ? value.toLowerCase() : '');
  const paths = options.map((option) => option.path);
  if (typeof remembered === 'string' && paths.includes(remembered)) {
    return remembered;
  }
  if (digest(rememberedSha256) !== '') {
    const same = options.filter(
      (option) => digest(option.sha256) === digest(rememberedSha256),
    );
    if (same.length === 1) return same[0].path;
  }
  if (typeof boot === 'string' && paths.includes(boot)) return boot;
  return paths[0] ?? null;
}

/// How many artifacts of `edition` a copy is incomplete without.
export function requiredCount(edition) {
  return (edition?.artifacts ?? []).filter((artifact) => artifact.required === true)
    .length;
}

/// What `artifact` is called on a player's disk: a filename, or the
/// words the table gives a document, which has no filename of its own.
export function artifactName(artifact) {
  if (artifact.kind === 'configuration') {
    return `${artifact.name} (a configuration file, any contents)`;
  }
  return artifact.name || artifact.about || '(unnamed)';
}

/// The match as lines a host prints, in the same words and the same
/// order the desktop host uses (`hosts/sdl/src/disk_reports.cpp`), so a bug
/// report from either one reads the same.
export function describeMatch(match) {
  const lines = [];
  if (match.edition === null) {
    lines.push(
      `no file here belongs to any edition this build knows ` +
        `(${match.unclaimed.length} looked at)`,
    );
  } else {
    const required = requiredCount(match.edition);
    lines.push(
      `closest ${match.edition.name} - ${required - match.missing.length} of ` +
        `${required} required artifact(s) here`,
    );
    for (const artifact of match.missing) {
      lines.push(`missing ${artifactName(artifact)}`);
    }
  }
  for (const file of match.unclaimed) {
    lines.push(`looked at ${file.name} sha256=${file.sha256}`);
  }
  return lines;
}
