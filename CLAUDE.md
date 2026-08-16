# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Amber Folio: a purpose-built low-level emulator for the machine the SSI
Gold Box CRPGs ran on (real-mode 8086, EGA, PC speaker), targeting
Windows/macOS/Linux (64-bit) and WebAssembly. v1 targets Pool of
Radiance. **PLAN.md is the plan of record** — scope, architecture,
milestones, and settled decisions live there; don't re-litigate them
here or in PRs.

**Status: M0 in progress.** The skeleton builds and is proven on all four
targets in CI on every push — an empty core library, a stub SDL3 desktop
host, and a wasm module that reports its version. There is no machine
yet. Still open in M0: the unit-test rig and the format/lint/sanitizer
gates; update this file as each lands.

## Commands

```sh
cmake --preset linux-gcc      # or windows-msvc, macos, linux-clang, wasm
cmake --build --preset linux-gcc
ctest --preset linux-gcc      # smoke checks only, so far

bash scripts/check-clean.sh   # content guard — run before every commit
bash scripts/check-dco.sh     # DCO check — non-merge commits signed off
bash scripts/test-guards.sh   # guard self-test — run after editing a guard
```

The wasm preset needs an activated emsdk of the version pinned in
`.emscripten-version`; README.md has the setup and the build layout.

Both run in CI on every push. The content guard scans every commit in
history plus the working tree for denylisted game-artifact filenames
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
