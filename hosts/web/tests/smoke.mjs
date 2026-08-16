// SPDX-License-Identifier: AGPL-3.0-only
//
// Headless check that the wasm module loads and its one export answers —
// the same thing index.html shows a human, in a form CI can run (issue #3
// wires it into the build matrix). Registered with CTest by
// hosts/web/CMakeLists.txt and run from the build tree, beside the glue
// this imports.
//
//     node smoke.mjs --expect <major.minor.patch>
//
// Usable by hand too: pass no --expect and it just reports what it found.

import { loadAmberfolio, formatVersion } from './host.mjs';

function parseArgs(argv) {
  const expectIndex = argv.indexOf('--expect');
  if (expectIndex === -1) return { expected: null };

  const expected = argv[expectIndex + 1];
  if (!expected) {
    console.error('smoke: --expect needs a version argument');
    process.exit(2);
  }
  return { expected };
}

const { expected } = parseArgs(process.argv.slice(2));

// Keep the module's own stdout/stderr distinguishable from ours: a version
// that matches for the wrong reason is exactly what this is meant to catch.
const { version, output } = await loadAmberfolio({
  print: (text) => console.log(`  [module] ${text}`),
  printErr: (text) => console.error(`  [module] ${text}`),
});

const found = formatVersion(version);
console.log(`smoke: af_version() reports ${found}`);

const problems = [];
if (expected && found !== expected) {
  problems.push(`expected af_version() to report ${expected}, got ${found}`);
}
if (output.length === 0) {
  problems.push('the module produced no output, so main() did not run');
}

if (problems.length > 0) {
  for (const problem of problems) console.error(`smoke: FAIL: ${problem}`);
  process.exit(1);
}

console.log('smoke: OK');
