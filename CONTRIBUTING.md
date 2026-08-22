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

## Checks and gates

Beside the build-and-test matrix, six scripted checks gate every push,
and each is a script in [`scripts/`](scripts) you can run yourself. CI runs
exactly these scripts — no CI-only variant, no extra flags — so a green run
locally is a green run there:

```sh
bash scripts/check-clean.sh      # content guard: every commit, index, worktree
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
rather than passing over them quietly.

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
it, and `git tag -a v0.1.0` on the resulting merge commit.

## Practical bits

- Building and running the tests: [README.md](README.md#building-from-source)
  has the prerequisites per platform and the preset commands. The four
  targets and the sanitizer preset are all one `cmake --preset` away.
- Use `git commit -s` (DCO sign-off, see above).
- New source files start with `// SPDX-License-Identifier: AGPL-3.0-only`.
- Early days: please open an issue before starting large changes — the
  architecture is still settling.
