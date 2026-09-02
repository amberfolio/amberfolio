# Contributing to Amber Folio

Thank you for your interest! Contributions are welcome. This document is
short, but the licensing part is load-bearing — please read it once.

## Licensing of contributions

*(In force since the repository's first commit, 2026-08-15.)*

- **Outbound**: Amber Folio is distributed under **AGPL-3.0-only**.
- **Inbound**: by submitting a contribution (pull request, patch, or
  otherwise) you license your contribution under the
  [Apache License 2.0](LICENSES/Apache-2.0.txt) to the
  project maintainer and to all recipients of the software. You keep your
  copyright. No CLA, no paperwork — the pull-request template asks you to
  acknowledge this in one checkbox.
- **Sign-off**: every commit you author must carry a `Signed-off-by:`
  line (`git commit -s`), certifying the
  [Developer Certificate of Origin](https://developercertificate.org/) —
  that you wrote the change or otherwise have the right to submit it.
  Merge commits are the one exemption: pull requests are merged through
  GitHub, whose generated merge commits carry no sign-off.

### Why Apache-2.0 inbound — what it means, honestly

The maintainer distributes Amber Folio under the AGPL and also offers
commercial license exceptions (see [COMMERCIAL.md](COMMERCIAL.md)). The
permissive inbound rule is what makes that possible without asking anyone
to sign a CLA. What you should know, plainly:

- Your contribution remains part of the AGPL-licensed project **for
  everyone, forever** — the open version never loses anything.
- The maintainer may also license the combined work, including your
  contribution, under other terms to commercial licensees.
- You retain full rights to your own work and can reuse it anywhere.
- The project's releases will always remain available under an
  OSI-approved open-source license — that is a standing commitment.

If that trade isn't acceptable to you, that's a legitimate position — in
that case please don't submit code, but bug reports, testing, and ideas
are just as valuable and carry no licensing terms.

### Third-party work

Dependencies must be compatible with AGPL-3.0-only outbound — zlib, MIT,
BSD and Apache-2.0 are fine, GPL-2.0-only is not — and nothing
third-party is committed to this repository: it is fetched at build time
instead. [NOTICE.md](NOTICE.md) lists what the project depends on and
under which licence.

It also acknowledges published reverse-engineering work the CPU core has
learned from, which is a distinction worth being clear about. Taking a
*fact* about the 8086 from somebody's article or emulator — what the
divide microcode's loop does to the flags, say — is fine, and is how the
undefined-flag behaviour in `core/src/cpu/instructions/` was arrived at.
Copying their *expression* is not. If you rely on such a source, say so in
the file's header comment and add it to NOTICE.md in the same pull
request.

## The clean-content rule (non-negotiable)

Amber Folio must contain **no material from the original games**: no game
code — original, disassembled, or translated — no game data, no assets,
no copyrighted byte sequences. Contributions may rely on *facts*
(addresses, offsets, format descriptions, checksums) but never on
*expression* from the games. A content guard
([`scripts/check-clean.sh`](scripts/check-clean.sh), run it yourself before
you push) scans every commit in the history in CI on every push — a
tripwire for obvious artifacts, while the
full public history keeps the deeper claim open to anyone's inspection.
Maintainers will reject anything that crosses this line, however useful
it would be. This applies beyond git, too: keep game files out of issues,
CI logs, and screenshots.

**The rule covers the player's documents exactly as it covers the
binary** (M5, #171). The code wheel and the Adventurer's Journal are
copyrighted material as much as the program is; what this repository may
write down about one is what it may write down about the other — a
SHA-256, a name, and the offsets a fact table needs. Never a page, never
an image, never the text on one. `core/include/amberfolio/machine/
document.h` is the table that keeps those fingerprints, and a fingerprint
names a file without carrying a byte of it.

**And it covers the text a journal's ingestion produces** (M5-E3, #174).
That text is the one thing this project *makes* that is content: it is a
transcription of a player's own copyrighted document, read on the
player's own machine, and it stays there. No store, no fragment of one,
no excerpt and no fixture resembling one may enter this repository, an
issue, or a commit message. What may be reported about a store is how
many entries it has and its SHA-256 — `journal_store::fingerprint()`
exists for exactly that. `docs/journal.md` §8 is the list.

Part of that tripwire is an allowlist, and it is the part you are most
likely to meet: **anything that is not text is refused unless its path is
named in the guard.** The repository has one such file today, a 34-byte
program written here by hand. A denylist of artifact names can only
refuse what somebody thought of in advance — #134 was a few kilobytes of
the running program dumped out of the emulator under an invented name,
comfortably under the size cap and on nobody's list. If your change needs
a committed binary, add it to the allowlist in the same pull request with
a one-line note saying where the bytes came from, and expect that line to
be the thing review talks about.

## The fidelity invariant (non-negotiable)

The second rule that is not a review topic. PLAN.md §4 and §5 state it;
this is what it means when you are reading a diff.

**Nothing outside the seam engine mutates machine state.** A device
answers bus cycles. A service answers an interrupt. The loader places a
program. A host *reads* machine state — the framebuffer, the console,
memory through the ABI — and writes it only through a seam. The seam
engine is the only component in this repository that alters memory or
registers from outside the program's own instructions, and the only one
that intercepts its execution.

**Every seam is off by default, and with all of them off the machine is
a plain machine.** Not "behaves like one": a run's machine-state hash
with the engine present and idle equals the hash of the same run on a
build with no engine at all, a disabled seam's breakpoint is never
consulted, and seam state — a trigger's outstanding latch included — is
configuration and not machine state.
`tests/core/machine/seam_test.cpp` asserts all three and
`tests/programs` runs a probe program with its seam on and off on all
four targets, so this is a test and not a sentence.

In review that means: a change that writes the machine from anywhere but
`machine::step()`'s own mechanisms, a device's bus cycle, a service
handler, the loader, or a seam handler is a change to the fidelity
boundary. It may still be right — but it needs the argument, in the
change, and [`docs/seams.md`](docs/seams.md) is the shape that argument
takes. §8 there is the house style for writing a seam and §9 is this
rule at length.

**A seam pull request is not mergeable without its pair** (M5-V1, #177).
The invariant above is a claim about *your* seam too, and the only way to
see it is to run the same script twice. So a change that adds or alters a
seam brings two recordings into `tests/sessions/`:

- one where the seam is **on and never triggered**, carrying
  `identical <baseline>` in its descriptor — every checkpoint equal to
  the run without it;
- one where it is **on and exercised**, carrying `contrast <baseline>` —
  agreeing until the seam first matters and differing from there to the
  end.

Both are checked on the files, with no disk, so CI checks them on every
push: `python3 scripts/sweep.py --targets contrast`. The second is not
optional politeness — a seam can be on, armed, and reporting itself while
doing nothing at all, which is what happened twice to the cheats (#129,
#130) with a green suite throughout.

If your seam genuinely cannot carry the first — because it *draws* the
moment it is on, the way a splice onto a menu the game keeps redrawing
does — then say so in the descriptor and pair it with a `contrast`
instead. `tests/sessions/quiet-journal.session` is the worked example.
What is not acceptable is loosening what `identical` means so that it
fits, because that costs every other seam its meaning.

Two corollaries that come up more often than the rule itself:

- **An observation is not part of the run.** A trace ring, an edge log, a
  diagnostics sink: switching one on must not move a state hash. If it
  does, every recording in `tests/sessions` has quietly become a
  statement about the observer.
- **Log, don't fake.** An unimplemented service, register or port is a
  loud log line and a clean stop, never a guessed answer.
  [`docs/machine.md`](docs/machine.md) §5 has the third answer beside
  those two — a notice, for a request the machine can honestly record
  but not honestly perform — and the test for when it applies.

## Checks and gates

Beside the build-and-test matrix, six scripted checks gate every push,
and each is a script in [`scripts/`](scripts) you can run yourself. CI runs
exactly these scripts — no CI-only variant, no extra flags — so a green run
locally is a green run there:

```sh
bash scripts/check-clean.sh      # content guard: every commit, index, worktree, strays
bash scripts/check-dco.sh        # every non-merge commit carries a sign-off
bash scripts/check-host-time.sh  # nothing under core/ reads the host's clock
bash scripts/check-format.sh     # clang-format over tracked C++
bash scripts/check-tidy.sh       # clang-tidy; needs a configured build tree
bash scripts/check-shell.sh      # shellcheck over scripts/
```

The host-time guard is what keeps a run replayable (`docs/replay.md`):
virtual time is the machine's only clock, and a `<chrono>` or a `time()`
under `core/` would put the host's wall clock into machine state.

The first two look at **history**, not just your tip commit — an artifact
or a missing sign-off four commits back fails the push even if the tree is
clean now, and the public history is never rewritten to paper over it
(force pushes to `main` are blocked). Fix it before it lands: `git commit
-s` from the start, and `git rebase --signoff` if you forgot.

### Formatting and linting

Style is not a review topic here — it is settled by
[`.clang-format`](.clang-format), and the check set in
[`.clang-tidy`](.clang-tidy) is settled the same way — don't argue
formatting in prose, change the config.

Both clang tools are pinned in [`.llvm-version`](.llvm-version), because
they disagree with themselves across major versions and a gate whose
verdict depends on which one you installed is not a gate. Install that
exact version:

```sh
pip install "clang-format==$(cat .llvm-version)" "clang-tidy==$(cat .llvm-version)"
```

(A virtualenv or `pipx` is fine, and on Linux may be necessary — many
distributions mark the system Python as externally managed.) `shellcheck`
comes from your package manager: `apt install shellcheck`,
`brew install shellcheck`.

To fix formatting rather than just be told about it, run `clang-format -i`
over the files the gate names. `check-tidy.sh` reads the compile database
from `build/linux-clang` by default — `cmake --preset linux-clang` is
enough to produce one, no build needed — and takes another build directory
as its argument. It says which files a given build tree does not cover
rather than passing over them quietly. That database is *parsed*, with
the same python that installed clang-tidy above, rather than matched a
spelling at a time — so `build/windows-msvc` serves as well as
`build/linux-clang`, whichever way the generator of the day writes a
path. Without a working python the gate stops and says so; it does not
quietly check less.

The scripts are checked by [`scripts/test-guards.sh`](scripts/test-guards.sh),
which asserts each gate *fails* on the violation it exists to catch. Run it
after editing one.

## Releases and tags

PLAN.md §7 gives every milestone from M3 on a **0.x pre-release**, so
that there is always a current, runnable tag while the work converges on
1.0. This is how that is done, decided once at M3's closeout and reused
by every milestone after.

- **The tag is annotated, on `main`, named `vMAJOR.MINOR.PATCH`.**
  Annotated rather than lightweight so the tag carries a message,
  an author and a date of its own — a milestone is a claim about the
  repository, and a claim should be signed and dated.
- **Its name matches `project(VERSION ...)` in the top-level
  `CMakeLists.txt`, exactly.** The version is not decoration: `af_version`
  reports it across the C ABI and the wasm smoke test asserts the module
  reports what CMake built. Bumping the one and not the other makes a
  binary that lies about which milestone it is.
- **The bump happens in the closeout pull request**, so that the commit
  the tag names is the first commit that reports the new version, and no
  commit on `main` ever reports a version that was never tagged.
- **The tag's message is one paragraph on what the milestone means** —
  the exit criterion in plain words — followed by what it does *not*
  mean. Someone checking out a tag deserves to know what they are
  getting before they build it.
- **Nothing is ever retagged.** Public history is not rewritten (see
  above); a tag that turns out to be wrong is followed by another tag,
  not moved.

So M3's closeout was: bump `VERSION` to `0.1.0` in the closeout PR, merge
it, and `git tag -a v0.1.0` on the resulting merge commit. M4's was the
same three steps with `0.2.0`, and M5's with `0.3.0`.

### What a tag publishes

Pushing a `v*` tag runs the whole of `ci.yml` and, past the same gate the
deploy job waits on, publishes a **GitHub Release** carrying the web
host's build output (issue #200). That is the forward path, and it can
hold the full gate because the workflow and the tree it gates are the
same commit. Seven files — `amberfolio.wasm`,
`amberfolio.mjs`, `host.mjs`, `app.mjs`, `audio-worklet.mjs`,
`picker.mjs`, `journal.mjs` — plus `SHA256SUMS`, a `manifest.json`, and
the notices (`LICENSE`, `NOTICE.md`, and `LICENSES/`) so they can be
rendered without cloning. **`journal.mjs` joined the list at M5's
closeout (#229) and was owed before it**: `app.mjs` has imported it by
name since #174 and it was never staged, so a consumer serving the
released `app.mjs` got a 404 for a file the page asks for. A seventh
filename is a lockfile key like the other six, and `v0.3.0` is the first
release that has it. The tag's own message becomes the release notes, which is why it
is worth writing one. A `0.x` tag is marked a pre-release.

Nothing here is for a player: there is no desktop binary in it (that is
M7, PLAN.md §7). It exists because a **site that hosts the wasm build
consumes it as a pinned build input** — it contains no emulator source,
it records the tag and a sha256 per asset in a lockfile, and its build
refuses to start when the bytes it downloads do not match. That is what
lets such a site be honest about serving an AGPL program: every page
running the emulator links to the exact commit it was built from, and the
hash is what makes the link a fact rather than a claim. The commit is
`manifest.json`'s `sourceCommit`, a full forty-character sha and never
the tag, because a tag can be moved.

Two consequences worth knowing before touching any of it:

- **Assets are never republished under an existing tag.** The bytes are
  not bit-reproducible — a different emsdk build of the same source is a
  different `.wasm` — so re-running the job over a release that exists
  would change hashes somebody has already pinned. The job refuses; a
  release that genuinely has to be redone is deleted by hand first, by
  someone who has checked who is pinning it.
- **A tag older than the release job is released by `release.yml`**, a
  workflow of its own, dispatched with the tag's name. Nothing is
  retagged; `v0.1.0` and `v0.2.0` simply predate the job.

  It builds and tests the wasm module from that tag — both
  configurations, as `ci.yml` does — and publishes it, but it does *not*
  re-run `ci.yml`'s gates over the tag, which is a decision and not a
  shortcut. Naming an old tag as an input to `ci.yml` was tried on
  `v0.2.0` and failed three ways, none of them a fact about the bytes: a
  workflow comes from the ref it is dispatched on while the tree comes
  from the tag, so the guards job ran a self-test script that does not
  exist at `v0.2.0`, and `sdl-host-records-and-replays` failed on two
  targets — a bug fixed *after* that tag, visible only under the
  `--parallel 4` `ci.yml` adopted after it too. Today's gates asked
  yesterday's question. The gates that mean something for an old tag are
  the ones that were green when it was tagged, plus the build and test of
  the module actually being published.

  `release.yml` also checks the repository out twice: the tag, for
  everything it reads, and the dispatched ref, for
  `scripts/release-bundle.sh` alone — which is newer than the tags it
  exists to release. Current tool, historical content.

The bundle's shape lives in
[`scripts/release-bundle.sh`](scripts/release-bundle.sh) rather than in
the workflow, so it can be run and tested from a laptop:
`scripts/test-release-bundle.sh` asserts its refusals, including the one
that is easy to get wrong — on a tag push `GITHUB_SHA` is the *annotated
tag object's* sha, forty hex characters that look exactly like a commit
and resolve to no source tree.

### Rules a release is bound by

Confirmed against a first real consumer, which read `v0.2.0` cold and
needed nothing changed. These are the parts that are somebody else's
problem the moment they move.

- **The seven filenames are keys in a consumer's lockfile.** Renaming one
  is a breaking change for everyone pinning it, not a refactor. The same
  goes for the hash spelling: `SHA256SUMS` is hex and `manifest.json` is
  whatever is natural, and a consumer normalises both, so a change there
  would be invisible in code and merely confusing to a person reading the
  two files side by side. `scripts/test-release-bundle.sh` asserts the
  list by name and in order, so it cannot move quietly.
- **Every pre-1.0 tag is a pre-release**, and this has a consequence
  worth stating rather than discovering: `/releases/latest` *excludes*
  pre-releases, so a consumer tracking "latest" instead of a tag sees
  nothing at all. That is the right answer while no release is one
  anybody should track blind, and it stops being true on its own at 1.0.
- **The notices are flattened, and a collision is refused.** Release
  assets are one flat namespace, so `LICENSES/x.txt` is attached as
  `x.txt` beside the build outputs. Three files today, no collision. If
  the licence set grows enough for that namespace to feel crowded, give
  them a prefix or a tarball — but the failure mode is already a loud one
  rather than a silent overwrite: `release-bundle.sh` refuses when two
  files would be published under one name, and the self-test proves it.
- **Assets are never republished under an existing tag** (above), which
  is the property a lockfile's whole premise rests on. GitHub's immutable
  releases would make that impossible rather than merely refused; the
  repository does not have it turned on yet.
- **A rebuild will not be byte-identical**, and that is expected. The
  retroactive `v0.2.0` build differed from the dev page's bytes for
  `amberfolio.wasm`, `amberfolio.mjs` and `host.mjs` while the three
  pure-JS page scripts were identical. It costs one lockfile bump
  downstream, which is the normal path.

## Practical bits

- Building and running the tests: [README.md](README.md#building-from-source)
  has the prerequisites per platform and the preset commands. The four
  targets and the sanitizer preset are all one `cmake --preset` away.
- Use `git commit -s` (DCO sign-off, see above).
- New source files start with `// SPDX-License-Identifier: AGPL-3.0-only`.
- Early days: please open an issue before starting large changes — the
  architecture is still settling.
