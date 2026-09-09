# CLAUDE.md

Guidance for Claude Code working in this repository.

## What this is

Amber Folio is a purpose-built low-level emulator for the machine the SSI
Gold Box CRPGs ran on (real-mode 8086, EGA, PC speaker), for
Windows/macOS/Linux (64-bit) and WebAssembly. v1 targets Pool of
Radiance. **PLAN.md is the plan of record**: scope, architecture,
milestones and settled decisions live there. Don't re-litigate them.

**Status.** M0–M5 are done and `v0.5.0` is the current tag. The game
boots, plays end to end, and all six v1 enhancements work and toggle
independently on both hosts. The current milestone is **M6** (onboarding,
shells, gamepad); its worklist is #265. Open issues are the complete list
of known gaps; docs describe what *is*, not what is owed.

**No test in this repository runs the game, and none ever will.** The
maintainer's own copy drives the game locally; CI runs everything that
needs no disk.

## Non-negotiable rules

- **Clean content.** Nothing from the original games, ever: no code
  (original, disassembled or translated), no data, no assets, no byte
  sequences, no page or text of the player's documents, no journal store
  or excerpt of one. *Facts* are fine: addresses, offsets, formats,
  SHA-256s. Full rule in CONTRIBUTING.md. Run `bash scripts/check-clean.sh`
  before every commit. **Never `git add -A`**: a stray dump beside the
  tree once reached public history (#134), and history is never rewritten.
- **Fidelity invariant.** Every seam is off by default. Nothing outside
  the seam engine mutates machine state. With all seams off the machine
  is a plain machine, and that is a test, not a sentence. Seam state is
  configuration, not machine state.
- **Log, don't fake.** An unimplemented service, register or port is a
  loud log line and a clean stop, never a guessed answer. `docs/machine.md`
  §5 has the third option, a notice, and when it applies.
- **Virtual time is the only clock.** Nothing under `core/` reads host
  time (`scripts/check-host-time.sh`).
- **Every non-merge commit is DCO-signed** (`git commit -s`). New source
  files start with `// SPDX-License-Identifier: AGPL-3.0-only`.
- **Licences.** Outbound AGPL-3.0-only, inbound Apache-2.0. Dependencies
  must be AGPL-compatible (zlib/MIT/BSD/Apache-2.0 yes, GPL-2.0-only no).
  Nothing third-party is committed; it is fetched at build time.
- **Naming.** Game and franchise titles appear only nominatively
  (TRADEMARK.md).
- **A seam PR brings its pair** into `tests/sessions/`: one recording with
  the seam on and never triggered (`identical`), one on and exercised
  (`contrast`). CONTRIBUTING.md explains; `tests/sessions/README.md` has
  the grammar.

## Architecture in brief

- **Targeted LLE.** Hardware the game touches is emulated at register
  level; a thin DOS/BIOS service layer sits under it over a virtual
  filesystem. The original program runs unmodified.
- **Core/host split.** Freestanding C++23 core (`core/`), a narrow
  platform interface, two hosts: SDL3 (`hosts/sdl/`) and a hand-written
  JS page (`hosts/web/`). Shared host code in `hosts/common/`.
- **Seams** (`core/include/amberfolio/machine/seam.h`) are the only
  enhancement mechanism: fingerprint-keyed, overlay-qualified CS:IP
  points with native C++ handlers. Never injected code.
- **Enhancement designs are settled.** Implement the mechanism, don't
  redesign the feature (PLAN.md §5).
- **Everything is deterministic and replayable.** A recording is keys,
  ticks and hashes (`docs/replay.md`).

## Commands

```sh
cmake --preset linux-gcc      # or windows-msvc, macos, linux-clang, wasm
cmake --build --preset linux-gcc
ctest --preset linux-gcc      # unit + programs + host smoke checks
ctest --preset linux-gcc -L conformance   # the 8088 vectors (fetch first)
python3 scripts/fetch-conformance-vectors.py
ctest --preset wasm           # the machine programs under node

bash scripts/check-clean.sh   # content guard, before every commit
bash scripts/check-dco.sh
bash scripts/check-host-time.sh
bash scripts/check-format.sh  # clang-format, pinned in .llvm-version
bash scripts/check-tidy.sh    # needs a configured build tree
bash scripts/check-shell.sh
bash scripts/test-guards.sh   # after editing a guard
bash scripts/test-sweep.sh    # after editing sweep.py
bash scripts/test-frames.sh   # after editing frames.py
bash scripts/test-visual-legs.sh
bash scripts/test-release-bundle.sh

python3 scripts/sweep.py --targets contrast   # the session relations, no disk
python3 scripts/sweep.py                      # every session; skips loudly without a disk
python3 scripts/visual-legs.py --game-disk DIR  # on/off confinement legs
python3 scripts/frames.py ...                 # look at, crop, diff --dump stills
```

Style is decided by `.clang-format` and `.clang-tidy`, not in review.
Windows: build from a VS developer shell on a short path. README.md has
per-platform prerequisites; the wasm preset needs the emsdk pinned in
`.emscripten-version`.

## Where to look

| Doing | Read |
|---|---|
| Anything CPU | `docs/cpu-implementation.md` |
| Adding a device or a DOS/BIOS service | `docs/machine.md` |
| Writing or changing a seam | `docs/seams.md` (§8 house style, §8.4 traps, §10 per-seam facts) |
| Recording or verifying a run | `docs/replay.md`, `tests/sessions/README.md` |
| Driving the game headlessly | `docs/playable.md` (legs and keystrokes), `docs/first-light.md` (the boot) |
| Hosts, flags, the wasm ABI, the speaker | `docs/hosts.md` |
| The journal (ingestion, store, reader) | `docs/journal.md`, `docs/journal-test-plan.md` |
| The explored overlay's facts and decisions | `docs/explored-overlay.md` |
| What each enhancement does for a player | `docs/enhancements.md` |
| Releases, tags, the bundle | CONTRIBUTING.md "Releases and tags" |

Facts about the original program (addresses, offsets, screen geometry)
belong in the doc for the thing that uses them, stated once. Don't
duplicate them here.

## Working here

- Branch per issue, PR to `main`, merge through GitHub. The PR template's
  two checkboxes are required by a CI job; `gh pr create --body` must
  include them ticked.
- A doc says what is true now. History lives in git and closed issues;
  don't write changelogs into docs or CLAUDE.md.
- Prefer a small, well-named test over a paragraph explaining a rule.
