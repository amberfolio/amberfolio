# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Amber Folio: a purpose-built low-level emulator for the machine the SSI
Gold Box CRPGs ran on (real-mode 8086, EGA, PC speaker), targeting
Windows/macOS/Linux (64-bit) and WebAssembly. v1 targets Pool of
Radiance. **PLAN.md is the plan of record** — scope, architecture,
milestones, and settled decisions live there; don't re-litigate them
here or in PRs.

**Status: M0 complete. M1 — the CPU core — is the current milestone.**
The skeleton builds and is proven on all four targets in CI on every
push — an empty core library, a stub SDL3 desktop host, and a wasm
module that reports its version. There is no machine yet: M1 is where
the 8086 interpreter starts.

What M0 left in place, all of it running in CI on every push:

- The unit-test rig — GoogleTest (fetched, never vendored) under CTest,
  tests in `tests/`, one CTest case per test, on the native targets.
  M1 added the CPU conformance suite beside it under the `conformance`
  label: one CTest case per SingleStepTests/8088 vector file, fetched and
  condensed into a cache outside the tree, skipping until its instruction
  family lands (`tests/conformance/registry.cpp` is the enabled list).
  M1 also added `tests/programs` — self-written 8086 programs (a counted
  loop, a sieve of Eratosthenes to 100,000, and the string instructions
  over two 32 KiB buffers) run to HLT against a flat megabyte of RAM.
  Their answers and their exact step counts are asserted case by case in
  the unit suite; `amberfolio-bench` runs the same list and times it,
  under the `bench` label. It is the one piece of test apparatus that
  builds under Emscripten, so `ctest --preset wasm` now runs the
  interpreter rather than only building it.
- The format and lint gates — clang-format, clang-tidy and shellcheck,
  with the clang tools pinned in `.llvm-version`.
- An ASan+UBSan job on the `linux-asan-ubsan` preset.
- The content guard and the DCO check, over every commit in history.
- Deployment of the wasm host to https://amberfolio.vercel.app on every
  push to `main` (and to a preview URL for every same-repo PR) by the
  `deploy` job — built in Actions with the pinned emsdk, shipped
  prebuilt; see `deploy/vercel/README.md`.

## Commands

```sh
cmake --preset linux-gcc      # or windows-msvc, macos, linux-clang, wasm
cmake --build --preset linux-gcc
ctest --preset linux-gcc      # unit + programs + host smoke checks
ctest --preset linux-gcc -L bench   # just the 8086 programs, timed

cmake --preset linux-asan-ubsan   # the tests under ASan + UBSan, no host

python3 scripts/fetch-conformance-vectors.py   # the CPU oracle, ~726 MB once
ctest --preset linux-gcc -L conformance        # the 8088 vector suite

bash scripts/check-clean.sh   # content guard — run before every commit
bash scripts/check-dco.sh     # DCO check — non-merge commits signed off
bash scripts/check-format.sh  # clang-format over tracked C++
bash scripts/check-tidy.sh    # clang-tidy; needs a configured build tree
bash scripts/check-shell.sh   # shellcheck over scripts/
bash scripts/test-guards.sh   # guard self-test — run after editing a guard
```

The clang tools are pinned in `.llvm-version` and installed from PyPI
(`pip install "clang-format==$(cat .llvm-version)"`); CONTRIBUTING.md has
the details. Style is decided by `.clang-format` and `.clang-tidy`, not
in review — don't argue formatting in prose, change the config.

The wasm preset needs an activated emsdk of the version pinned in
`.emscripten-version`; README.md has the setup and the build layout.

Windows: build from a VS developer shell, and keep the checkout on a short
path — the fetched dependencies nest deeply enough that a long one trips
the 260-character limit while configuring. README.md has the per-platform
prerequisites.

The two guards run in CI on every push, and nothing deploys unless they
pass. The content guard scans every commit in history plus the working
tree for denylisted game-artifact filenames
and files over 256 KiB — an auditable tripwire against obvious
artifacts, not proof by itself; the deeper clean-content claim rests on
the full public history being open to inspection. Public history must
never be rewritten (branch protection blocks force pushes to main), so
both guards must stay green on every commit, not just at the tip.

## Non-negotiable rules

- **Clean content.** No material from the original games, ever: no game
  code (original, disassembled, or translated), no game data or assets,
  no original byte sequences. *Facts* are fine — addresses, offsets,
  format descriptions, SHA fingerprints. Full rule: CONTRIBUTING.md.
- **Every non-merge commit is DCO-signed**: `git commit -s`. (Merge
  commits are exempt; PRs merge through GitHub.)
- **New source files start with** `// SPDX-License-Identifier: AGPL-3.0-only`.
- **License compatibility.** Outbound is AGPL-3.0-only; inbound is
  Apache-2.0. Dependencies must be AGPL-3.0-compatible — zlib/MIT/BSD/
  Apache-2.0 are fine; GPL-2.0-only is not.
- **Naming.** Game and franchise titles appear only nominatively (to
  describe compatibility), per TRADEMARK.md.

## Architecture (see PLAN.md for the full picture)

- **Targeted LLE.** Hardware the game touches is emulated at register
  level (8086 interpreter, EGA planar subset, 8253 PIT, speaker); the
  thin DOS/BIOS service layer beneath it (small INT 21h/16h/10h
  subsets) is provided over a virtual filesystem. The original program
  runs unmodified — its own unpacker and overlay manager execute on the
  emulated CPU.
- **Core/host split.** A freestanding C++23 core exposes a narrow
  platform interface (frame out, audio pull, input in, VFS, clock).
  Two hosts: one SDL3 host for all desktop targets, and a hand-written
  JS host (canvas/WebAudio/IndexedDB) for wasm — deliberately not
  SDL-through-Emscripten.
- **Seams** are the only enhancement mechanism: opt-in runtime patches
  (CS:IP breakpoints + memory/register surgery + host services) keyed
  by binary SHA-256 fingerprint. Seam handlers are native C++ compiled
  into the emulator — never code injected into the emulated machine.
  Every seam is individually toggleable and **off by default**; with
  all seams off, the core is a plain machine running an unmodified
  program, and nothing else may mutate machine state.
- **Log, don't fake.** An unimplemented service, register, or port is
  a loud log line and a clean stop — never a silently guessed answer.
- **Enhancement designs are settled.** The v1 enhancements (automap
  panel drawn into the emulated EGA planes, journal with OCR at
  ingestion + in-game reader, Encamp Fix, code-wheel bypass, save
  management, debug cheats) re-express proven designs as-is; implement
  the mechanism, don't redesign the feature.
