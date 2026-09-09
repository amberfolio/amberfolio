# Contributing to Amber Folio

Contributions are welcome. The licensing part is load-bearing; read it
once.

## Licensing of contributions

*(In force since the repository's first commit, 2026-08-15.)*

- **Outbound**: Amber Folio is distributed under **AGPL-3.0-only**.
- **Inbound**: by submitting a contribution you license it under the
  [Apache License 2.0](LICENSES/Apache-2.0.txt) to the project maintainer
  and to all recipients of the software. You keep your copyright. No CLA;
  the pull-request template asks you to acknowledge this in one checkbox.
- **Sign-off**: every commit you author carries a `Signed-off-by:` line
  (`git commit -s`), certifying the
  [Developer Certificate of Origin](https://developercertificate.org/).
  Merge commits made by GitHub are exempt.

**What the Apache-2.0 inbound rule means.** The maintainer distributes
Amber Folio under the AGPL and also offers commercial license exceptions
([COMMERCIAL.md](COMMERCIAL.md)). Your contribution stays in the
AGPL-licensed project for everyone, forever; the maintainer may also
license the combined work under other terms to commercial licensees; you
retain full rights to your own work; and the project's releases will
always remain available under an OSI-approved open-source license. If
that trade isn't acceptable, please don't submit code; bug reports,
testing and ideas carry no licensing terms.

**Third-party work.** Dependencies must be compatible with AGPL-3.0-only
(zlib, MIT, BSD, Apache-2.0 are fine; GPL-2.0-only is not), and nothing
third-party is committed: it is fetched at build time. [NOTICE.md](NOTICE.md)
lists dependencies and the published reverse-engineering work the CPU
core has learned from. Taking a *fact* about the 8086 from such a source
is fine; copying its *expression* is not. If you rely on one, say so in
the file's header comment and add it to NOTICE.md in the same pull
request.

## The clean-content rule (non-negotiable)

Amber Folio must contain **no material from the original games**: no game
code (original, disassembled or translated), no data, no assets, no
copyrighted byte sequences. Contributions may rely on *facts* (addresses,
offsets, format descriptions, checksums) but never on *expression*. This
applies beyond git: keep game files out of issues, CI logs and
screenshots.

- **The player's documents are covered exactly as the binary is.** What
  may be written down about the code wheel or the Adventurer's Journal is
  a SHA-256, a name and the offsets a fact table needs; never a page, an
  image or the text on one. `core/include/amberfolio/machine/document.h`
  is the fingerprint table.
- **The text a journal ingestion produces is covered too.** No store, no
  fragment, no excerpt and no fixture resembling one may enter this
  repository, an issue or a commit message. What may be reported is an
  entry count and `journal_store::fingerprint()`. `docs/journal.md` §8 is
  the list.
- **The content guard** (`scripts/check-clean.sh`) scans every commit in
  history, the index, the working tree and untracked files beside it for
  denylisted names, files over 256 KiB, and anything that is not text
  whose path is not on its allowlist (one entry today). If your change
  needs a committed binary, add it to the allowlist in the same pull
  request with a one-line note saying where the bytes came from.
- **Never `git add -A`.** A stray dump beside the tree once reached
  public history (#134), and public history is never rewritten.

## The fidelity invariant (non-negotiable)

PLAN.md §4 and §5 state it; in a diff it means:

- **Nothing outside the seam engine mutates machine state.** A device
  answers bus cycles, a service answers an interrupt, the loader places a
  program, a host reads machine state and writes it only through a seam.
  A change that writes the machine from anywhere but `machine::step()`'s
  own mechanisms, a device's bus cycle, a service handler, the loader or
  a seam handler is a change to the fidelity boundary and needs the
  argument in the change. `docs/seams.md` §9 is the review rule.
- **Every seam is off by default, and with all of them off the machine
  is a plain machine.** A run's state hash with the engine present and
  idle equals the hash on a build with no engine; a disabled seam's
  breakpoint is never consulted; seam state, an outstanding pull
  included, is configuration and not machine state.
  `tests/core/machine/seam_test.cpp` asserts all three.
- **An observation is not part of the run.** A trace ring, an edge log
  or a diagnostics sink must not move a state hash.
- **Log, don't fake.** An unimplemented service, register or port is a
  loud log line and a clean stop, never a guessed answer. `docs/machine.md`
  §5 has the third answer, a notice, and when it applies.

**A seam pull request brings its pair** (#177). A change that adds or
alters a seam brings two recordings into `tests/sessions/`: one with the
seam **on and never triggered** (`identical <baseline>` in its
descriptor) and one **on and exercised** (`contrast <baseline>`). Both
are checked without a disk by `python3 scripts/sweep.py --targets
contrast`, which runs in CI. A seam that draws the moment it is on cannot
carry the first; say so in the descriptor and pair it with a `contrast`
(`tests/sessions/quiet-journal.session` is the example). Do not loosen
what `identical` means to fit.

## Checks and gates

Six scripted checks gate every push. CI runs exactly these scripts:

```sh
bash scripts/check-clean.sh      # content guard: every commit, index, worktree, strays
bash scripts/check-dco.sh        # every non-merge commit carries a sign-off
bash scripts/check-host-time.sh  # nothing under core/ reads the host's clock
bash scripts/check-format.sh     # clang-format over tracked C++
bash scripts/check-tidy.sh       # clang-tidy; needs a configured build tree
bash scripts/check-shell.sh      # shellcheck over scripts/
```

The first two look at **history**, not just the tip: fix a missing
sign-off with `git rebase --signoff` before it lands, because `main` is
never force-pushed.

**Formatting and linting.** Style is settled by `.clang-format` and
`.clang-tidy`; change the config rather than arguing in prose. Both tools
are pinned in `.llvm-version`:

```sh
pip install "clang-format==$(cat .llvm-version)" "clang-tidy==$(cat .llvm-version)"
```

`shellcheck` comes from your package manager. `clang-format -i` fixes
what the gate names. `check-tidy.sh` reads the compile database from
`build/linux-clang` by default (`cmake --preset linux-clang` produces one
without building) and takes another build directory as its argument.
`scripts/test-guards.sh` asserts each gate fails on the violation it
exists to catch; run it after editing one.

## Releases and tags

Every milestone from M3 on gets a **0.x pre-release** tag (PLAN.md §7).

- The tag is **annotated**, on `main`, named `vMAJOR.MINOR.PATCH`, and
  matches `project(VERSION ...)` in the top-level `CMakeLists.txt`
  exactly (`af_version` reports it across the ABI and the wasm smoke test
  asserts it).
- The version bump happens in the closeout pull request, so the tagged
  commit is the first that reports the new version.
- The tag's message is one paragraph on what the milestone means and what
  it does not; it becomes the release notes.
- **Nothing is ever retagged**, and **assets are never republished under
  an existing tag**: the bytes are not bit-reproducible, and a consumer
  pins them by hash. A release that must be redone is deleted by hand
  first, by someone who has checked who is pinning it.

**What a tag publishes.** Pushing a `v*` tag runs `ci.yml` and, past the
same gate the deploy job waits on, publishes a GitHub Release carrying the
web host's build: seven files (`amberfolio.wasm`, `amberfolio.mjs`,
`host.mjs`, `app.mjs`, `audio-worklet.mjs`, `picker.mjs`, `journal.mjs`)
plus `vendor-tesseract.tar.gz`, `SHA256SUMS`, `manifest.json` and the
notices, flattened into one namespace. `manifest.json`'s `sourceCommit`
is a full commit sha, never the tag. The seven filenames are lockfile
keys in a consuming site: renaming one is a breaking change. The tarball
is the OCR engine `page/journal.mjs` asks for by name (#287): the page
refuses a CDN and reads one library version's output shape, so which
tesseract.js a site serves is a fact about the bundle rather than a
choice the site makes. `manifest.json` describes it under `engine`:
digest, size, library and pinned version, `unpacksTo`, and a digest per
file, so a consumer can check what it is about to serve as well as what
it downloaded. `scripts/release-bundle.sh` is the bundle and
`scripts/test-release-bundle.sh` asserts its refusals (the list by name
and in order, a notices collision, an engine directory with no
`tesseract.min.js`, and `GITHUB_SHA` being the tag object rather than a
commit). A `0.x` tag is a pre-release, so
`/releases/latest` shows nothing until 1.0.

**A tag older than the release job** is released by `release.yml`,
dispatched with the tag's name. It builds and tests the wasm module from
the tag and publishes it; it does not re-run `ci.yml`'s gates over the
tag, since today's gates ask yesterday's question. It checks out the tag
for everything it reads and the dispatched ref for
`scripts/release-bundle.sh` alone.

## Practical bits

- [README.md](README.md#building-from-source) has the per-platform
  prerequisites and preset commands.
- `git commit -s`. New source files start with
  `// SPDX-License-Identifier: AGPL-3.0-only`.
- Please open an issue before starting a large change.
