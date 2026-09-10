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
export const EDITIONS_SCHEMA = 'amberfolio.editions/1';

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
/// **Matched on the digest, never on the name**, which is the same rule
/// `host::match_edition()` follows: a renamed file still matches, and a
/// file carrying a required artifact's name with different bytes matches
/// nothing — so it lands in `unclaimed` while the artifact it is not
/// lands in `missing`. Those two lines together are the fact a player
/// can act on, and no single list says it.
///
/// Answers `{ edition, matched, missing, unclaimed, complete }`.
/// `edition` is null when not one file belonged to any edition, which is
/// the honest answer for a directory holding something else — and for an
/// edition nobody has fingerprinted yet, in which case `unclaimed` is
/// everything and a report is the file names and those hashes.
export function matchEdition(editions, offered) {
  const digest = (value) => (typeof value === 'string' ? value.toLowerCase() : '');
  const have = new Set(offered.map((file) => digest(file.sha256)).filter(Boolean));

  let edition = null;
  let matched = [];
  for (const candidate of editions) {
    const artifacts = Array.isArray(candidate.artifacts) ? candidate.artifacts : [];
    const here = artifacts.filter((artifact) => have.has(digest(artifact.sha256)));
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

  const claimed = new Set(matched.map((artifact) => digest(artifact.sha256)));
  // A required artifact nobody offered — the directory rows included.
  // They have no digest and so can never match, and a copy with no
  // `SAVE\` is a copy that cannot save, so they are deliberately still
  // reported rather than quietly dropped.
  const missing = edition.artifacts.filter(
    (artifact) => artifact.required === true && !claimed.has(digest(artifact.sha256)),
  );
  const unclaimed = offered.filter((file) => !claimed.has(digest(file.sha256)));
  return { edition, matched, missing, unclaimed, complete: missing.length === 0 };
}

/// How many artifacts of `edition` a copy is incomplete without.
export function requiredCount(edition) {
  return (edition?.artifacts ?? []).filter((artifact) => artifact.required === true)
    .length;
}

/// What `artifact` is called on a player's disk: a filename, or the
/// words the table gives a document, which has no filename of its own.
export function artifactName(artifact) {
  if (artifact.kind === 'directory') return `${artifact.name} (a directory)`;
  return artifact.name || artifact.about || '(unnamed)';
}

/// The match as lines a host prints, in the same words and the same
/// order the desktop host uses (`hosts/sdl/src/main.cpp`), so a bug
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
