# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Amber Folio: a purpose-built low-level emulator for the machine the SSI
Gold Box CRPGs ran on (real-mode 8086, EGA, PC speaker), targeting
Windows/macOS/Linux (64-bit) and WebAssembly. v1 targets Pool of
Radiance. **PLAN.md is the plan of record** — scope, architecture,
milestones, and settled decisions live there; don't re-litigate them
here or in PRs.

**Status: M2 complete. M3 — first light — is the current milestone.**
There is a machine now, and self-written real-mode programs run on it
correctly on all four targets: that is PLAN.md §7's M2 exit criterion,
met. The 8086 interpreter underneath it is still exact — all 323 vector
files of the pinned SingleStepTests/8088 v2 set pass in CI on every push,
undefined flag behaviour included — and stayed exact through every device
that grew around it.

What is *not* here is a game. M3 is where the player's own copy boots:
its unpacker and overlay manager run as-is, the title sequence renders,
menus respond. Expect it to stop, loudly, on services this machine does
not have yet — that is the design, and the log line is the worklist.
PLAN.md §7 has M3's exit criterion.

What M2 left in place:

- The machine layer — `core/include/amberfolio/machine/` and
  `core/src/machine/`. The memory map (RAM, ROM, device windows, open
  bus that reports a first touch rather than inventing an answer), the
  port map, the device contract, the virtual clock and its deadline
  scheduler, the BIOS/DOS callout, and the platform interface the hosts
  consume. `docs/machine.md` is the tour and the house style for adding
  a device or a service — read it before extending the service surface.
- The devices: 8253 PIT and a minimal 8259, EGA (planes, latches, the
  full write pipeline, palette, renderer, INT 10h), PC speaker, and the
  BIOS keyboard services over the real BDA buffer.
- The DOS floor: a virtual filesystem with DOS name semantics settled in
  core, an MZ loader with relocations and a PSP, and PLAN.md §3's INT 21h
  subset — file I/O, date/time, console output, exit.
- **Virtual time is the only clock.** Counted in PIT input ticks
  (1,193,182 Hz); nothing under `core/` reads host time. Devices do not
  tick, they compute: a channel's count is a formula, its next edge is a
  deadline.
- Both hosts run the machine. The SDL3 host takes a directory and a
  program and returns the exit code the program chose; `--headless`
  makes that checkable in CI. The wasm dev page puts it in a browser —
  canvas, AudioWorklet, keyboard — with a headless smoke test asserting
  the same run. Since #80 the *windowed* path is checked too, on all
  three desktop targets: `--verify` reads each presented frame back off
  the render target and compares it pixel for pixel with what was
  uploaded, `--press KEY@FRAME` puts a real SDL key event through the
  real mapping, and both run under SDL's `dummy` video and audio
  drivers. `docs/hosts.md` says what that settles and what is left for
  a person with a display and a speaker.
- The exit-criterion suite — seven self-written programs under
  `tests/programs`, driven through the whole machine, answers asserted
  case by case. The M1 flat-bus programs still run beside them
  unchanged, and the whole apparatus stays free of GoogleTest so it
  builds under Emscripten.

What M1 left in place:

- The CPU core — `core/include/amberfolio/cpu/` and `core/src/cpu/`.
  A register file with normalized flags, one ALU kernel that owns flag
  semantics, a decoder (prefixes, ModRM, effective addresses), the
  dispatch tables (236 primary handlers — 256 less the prefixes and the
  group opcodes — and 90 group entries, one sorted line each), sixteen
  instruction files under `instructions/`, and interrupt delivery: one
  sequence for all four sources, plus the three timing windows a vector
  suite cannot catch (TF fires one instruction late, STI takes effect
  one instruction late, a segment-register load holds recognition off).
  `step()` runs one instruction or one REP iteration; there is no
  prefetch queue and no cycle counting, by decision (PLAN.md §3).
- The conformance suite, under the `conformance` label — one CTest case
  per vector file, fetched and condensed into a cache outside the tree.
  It is exhaustive: every stem the pin has runs and is expected to pass,
  the manifest's length is checked against the pin at configure time,
  and the only skip left is "the vectors are not on this machine",
  which CI turns into a failure. Adding an instruction now means
  keeping 323 green files green.
- `tests/programs` — self-written 8086 programs (a counted loop, a sieve
  of Eratosthenes to 100,000, and the string instructions over two
  32 KiB buffers) run to HLT against a flat megabyte of RAM. Their
  answers and their exact step counts are asserted case by case in the
  unit suite; `amberfolio-bench` runs the same list and times it, under
  the `bench` label. It is the one piece of test apparatus that builds
  under Emscripten, so `ctest --preset wasm` runs the interpreter rather
  than only building it.
- `docs/cpu-implementation.md` — the architecture tour and the
  house style for an instruction handler, written for M1's wide phase
  and still the guide for touching CPU code. `docs/machine.md` is its
  sibling for everything around the CPU: adding a device, adding a BIOS
  or DOS service, and what "log, don't fake" means at that layer.

What M0 left in place, all of it running in CI on every push:

- The unit-test rig — GoogleTest (fetched, never vendored) under CTest,
  tests in `tests/`, one CTest case per test, on the native targets.
- The format and lint gates — clang-format, clang-tidy and shellcheck,
  with the clang tools pinned in `.llvm-version`.
- An ASan+UBSan job on the `linux-asan-ubsan` preset.
- The content guard and the DCO check, over every commit in history.
- The skeleton on all four targets: the core library, a stub SDL3
  desktop host, and a wasm module that reports its version.
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
ctest --preset linux-gcc -L smoke   # the hosts, headless
ctest --preset wasm                 # the machine programs under node

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
