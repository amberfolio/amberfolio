# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Amber Folio: a purpose-built low-level emulator for the machine the SSI
Gold Box CRPGs ran on (real-mode 8086, EGA, PC speaker), targeting
Windows/macOS/Linux (64-bit) and WebAssembly. v1 targets Pool of
Radiance. **PLAN.md is the plan of record** — scope, architecture,
milestones, and settled decisions live there; don't re-litigate them
here or in PRs.

**Status: M4 complete, tagged `v0.2.0`. M5 — player enhancements — is
the current milestone.**
The game plays. A player-supplied copy runs its own unpacker and overlay
manager, renders its title sequence, answers its menus, makes a party,
walks the city, plays its opening story event, fights an encounter to a
finish with and without the debug cheats seam, saves, loads, buys, heals,
sells, and walks off the edge of a map — on the desktop host and in a
browser, from the same core. That is PLAN.md §7's M4 exit criterion, met.
`docs/playable.md` is the procedure, leg by leg, with the keystrokes that
drive it and what each leg is evidence for; its last section is what it
has *not* covered, and the gaps there that are decisions rather than
debts say so. `docs/first-light.md` is the M3 sibling, the boot alone.
**No test in this repository runs the game, and none ever will.**

The 8086 interpreter underneath is still exact — all 323 vector files of
the pinned SingleStepTests/8088 v2 set pass in CI on every push,
undefined flag behaviour included — and stayed exact through every
device, service and seam that grew around it.

What M4 left in place:

- **The seam engine** (`machine/seam.h`, `docs/seams.md`), which is
  PLAN.md §5's mechanism and the only way anything but the program may
  touch this machine. Fingerprint-keyed, overlay-qualified, four action
  primitives plus a host-service slot, a **call into the program** (M5-D4,
  #188 — so text a seam puts on the game's screen is drawn by the game, in
  the game's font), a host→seam trigger a person pulls, and a toggle
  surface on both hosts. **Every seam is off by
  default**, and the fidelity invariant is a test: with all of them off a
  run's state hash equals the same run's on a build with no engine at
  all, a disabled seam's breakpoint is never consulted, and seam state —
  an outstanding pull included — is configuration and not machine state.
  M5's five enhancements add no mechanism; they add handlers.
- **Seven seams this build carries**: `code-wheel` (ungated; its
  possession gate is M5's, #115), `encamp-fix` (M5-E1 #172, M5-E1a #186,
  M5-E1b #189 and M5-E1c #194 — the first M5 enhancement; it puts a `FIX`
  command on the camp screen's own bar by splicing four characters into
  the string the program draws that bar from, and when the player presses
  its letter it spends the cures the party already holds through the
  game's own cast driver — queueing one back for every one spent — then
  dials the game's own rest to the days that did not close and presses
  Rest, and on the next pass of the menu says what it did in a framed
  report the *game* draws, in the game's font, over a live bar that is
  the way out of it; a rest the game **interrupts** never gives it that
  pass, so a fourth point on the camp loop's own exit draws the report
  there instead — `Fix: Interrupted!`, held by the program's own message
  delay — and drops it on any other way out; any key the player types
  stops it),
  `automap` (M5-E2 #173 — the second M5 enhancement, and the first seam
  that *draws*: a map of the squares the party has walked, over the party
  roster on the game's own screen, shown and taken away on **Tab**. The
  key is claimed by removing it from the BIOS keystroke buffer before the
  program's own key routine looks, so the program observes exactly what it
  would have observed had it never been typed; the panel is rendered into
  the EGA planes a plane at a time, which is what added **port surgery**
  as `docs/seams.md` §3's eighth primitive; what has been explored lives
  in `machine::automap()` beside the overlay tracker, as observation and
  not machine state; and closing it clears the panel's own rect and calls
  the program's roster drawer to put the party list back. M5-E2a adds the
  colours: a wall is the modal non-black pixel of the very tiles the 3D
  view blits for it, so
  the buildings are the colour of the buildings, and a door leaf is drawn
  where a wall face's *kind* has been seen shut — on this map or in the
  table of every shut face in the shipped data. M5-E2b puts the zone's
  name in the band the panel's geometry leaves for it, in the program's
  own glyphs read out of its own font, off a table of (disk, area) to a
  short label — because the program holds no such string to read. M5-E2c
  makes what it has walked outlive the machine: the seam calls
  `automap_update` when a reveal changes something, and a host writes the
  table into `\SAVE\AFMAP.DAT` beside the program's saves and never
  inside one, with a snapshot per save slot so two playthroughs do not
  share a map. It is off unless a host is asked (`--automap-store`,
  `af_web_automap_store`), because a file appearing in a player's game
  directory changes it and every recorded session pins its disk. Telling
  a slot the program *loaded* from one the load menu merely looked at is
  what added the two traffic flags to a DOS file-close event. M5-E2d is
  what a player's first hours found: the panel gives back the roster and
  no more of the screen than that, because the composer it used to call
  repainted the viewport too and wiped a vendor's NPC out from under a
  live question; it comes down on its own, and its key goes quiet,
  whenever the bar on the screen is not the adventuring screen's own —
  a sixth point, at the thunk every menu bar in the game goes up
  through, because the party's bar is a string in the data segment and
  every vendor's is a copy on the stack, and neither the mode byte nor
  the three drawing points can tell them apart; and while it is up it
  takes the two keys that step the roster cursor, whose whole visible
  effect is a repaint of the cells it is sitting on. The obvious cheaper
  gate — the program's own "a script has the message area" byte — was
  measured with `--watch` and thrown away: it oscillates on every step.
  #173 is closed),
  `journal` (M5-E4 #175 — the third M5 enhancement and the reader half of
  the journal: when the game cites an entry, the entry opens on the
  game's own screen, and **F1** opens any other. It adds no address —
  five of its six points are the automap's and the sixth is the string
  drawer the Encamp Fix already calls, watched at its entry, so the
  citation watch is a point on the program's own text output and not a
  reader of a host's console ring (#165). What it matches is the
  citation's *shape* — the word and a number within reach of it, over a
  rolling window, because a sentence wrapped across two lines of a
  message panel reaches that routine as two calls — and never a word of
  the program's prose. It draws in the automap's own rect, in the
  program's own glyphs, and is modal over the map because they are the
  same pixels. Its text comes from a host through `journal_open`, and
  that is the first service that had to hand something *back*:
  `machine::journal()` is the buffer it comes back in, observation on
  `automap.h`'s three terms and not machine state. **No journal has been
  ingested by anybody**, so the one thing never driven is a citation in
  the program's own words — the path itself was, by building once with
  the pattern's word set to a letter the status line draws, which is
  `docs/playable.md` Leg 9),
  `cheat-invulnerable`, `cheat-kill-all`, and `cheat-wound-party` (M5-E1d
  #196 — pulled at the camp screen, it leaves every party member on one
  hit point, through the same write the program's own damage routine
  makes for that damage on a record it would accept; PLAN.md §5 item 6's
  third switch, built for that item's own stated reason, because the
  Encamp Fix's days arithmetic and its report's exception list had no
  other way to be driven). `docs/seams.md` §8 is the house style for the
  next one and §10 is the worked example.
- **The journal's ingestion** (`hosts/common/.../journal_*.h`,
  `docs/journal.md`) — M5-E3 (#174), and host work rather than seam work.
  A player's own Adventurer's Journal, located entry by entry inside
  their own PDF off a fact table of offsets, inflated, cropped, read once
  by an OCR engine, and kept as text on their own machine: a file beside
  the config on the desktop, this tab's memory in a browser (IndexedDB is
  M6's and the page says so). Corrections are a second field per entry
  and survive re-ingestion. The OCR engine is Tesseract on both hosts and
  is linked on neither — the desktop runs the player's own installed one
  as a program, the page loads the pinned tesseract.js
  (`.tesseract-version`, `.tesseract-js-version`) from its own origin and
  **never a CDN**. A page this build cannot decode is **carried rather
  than decoded** (M5-E3a #212): the first real journal anybody produced is
  `/DCTDecode` on every page, so its stream goes to the engine as its own
  bytes and the entry's rectangle filters the engine's *output* — which
  both engines can do, because both say where each word was. Nothing here
  learns what a JPEG is. **The edition table has one row**
  (M5-E3b #214), which is the first document anybody sat down with: the
  archive release's own journal, fifty-eight entries in seventy-eight
  pieces across nine two-page scans, every rectangle measured off the
  scans and every number checked against the printed headings. An entry is
  a *list* of fragments because the entries **flow** — out of a column,
  onto the facing page — and eighteen of the fifty-eight are in more than
  one piece; a row of one rectangle could describe none of those.
  `docs/journal.md` §3 is the method, so the next edition is a procedure
  rather than an archaeology. What has still never happened is a real
  engine reading a real page: the pipeline is proven in CI on all four
  targets against a synthetic PDF this project generates
  (`journal_probe.h`) — three entries, the third a real baseline JPEG this
  project encodes and the only one in two pieces — with a fixture engine
  that answers only for the scan the extraction was supposed to produce. The in-game reader that
  consumes it is M5-E4 (#175), above, and is a seam rather than host
  work; `docs/journal.md` §9 is the door between the two halves.
- **The replay harness** (`machine/replay.h`, `docs/replay.md`): a
  canonical machine-state serialization, a recording that is keys, ticks
  and hashes and no content at all, and verification on all four targets
  from one recording. A desktop recording of a **real game run** verifies
  on the wasm module — 101 checkpoints of a 139-million-step run, every
  hash equal. Before it the cross-target claim rested on four frames of
  `JMP $` (#142).
- **The session library** (`tests/sessions/`, `scripts/sweep.py`). Eleven
  sessions; one has its disk committed and ten pin a disk that cannot be
  (PLAN.md §6), so the runner is told where a copy is and **skips loudly**
  when it is not. A sweep that verified nothing must never read as a
  sweep that passed.
- **The instruments phase 3 needed.** `--watch OFF[:N]` on the SDL host
  prints a data-segment word every time it changes, which is how a run
  becomes a trail of where the party went; `--dump-every` says what the
  screen did and `--trace` what the program asked DOS for, and neither
  said where anything was. `hosts/web/tools/drive.mjs` is the SDL host's
  driving surface for the wasm module — a directory, a program,
  `--press KEY@FRAME`, `--pull`, `--seam`, `--dump`, a throughput line.
- **The speaker is measured** rather than described. `--dump` writes the
  edge list the machine published beside the PPM and the WAV; so does
  `drive.mjs`, and so does the host-free `amberfolio-dump`, in one
  format, so two hosts' runs are diffed rather than described. The
  underrun and resync counters reach every run's report, and the box
  filter's DC offset and its agreement across the two hosts' sample rates
  are numbers in the unit suite (`docs/hosts.md` §4).

What M4 did **not** settle, and is honest about: the dungeon and two city
services were closed as decisions rather than debts (#144, #145); nobody
has opened a browser on the dev page (#147); the game's own tones have
been heard but not measured, and a resync has never been produced on
purpose (#148). `docs/playable.md`'s last section and `docs/hosts.md` §3
and §4 carry those lists, above a sentence saying whether anyone is
coming.

What M3 left in place:

- **A machine that powers on like a PC.** `service_floor::reset()` is
  the self test, and it has two halves now: the vector table, the stubs
  and the BDA in memory, and then the PIT and the 8259 programmed
  through real bus cycles, to whichever of them is attached. M2 had only
  the first half, and the shape of that gap is the one to remember —
  nothing refused anything, nothing was logged, and the boot simply
  stopped making progress. Log-don't-fake cannot catch a program that
  never asked.
- **The surface a real boot asks for**, each item driven by a stop line
  and recorded on its issue: INT 21h `AH=25h/35h/44h`; INT 10h
  `AH=00h/05h/08h/0Fh/11h`; the BDA's video block; interrupts enabled at
  DOS entry. What the boot never asked for is written down too — the DOS
  memory functions, the keyboard hardware path, EXEC — and none of it
  was built on spec.
- **A raster at 3DAh.** The status register's timing bits are a formula
  against `machine::time()` rather than a constant, so a program that
  polls for vertical retrace terminates. Nothing in the boot polls it;
  this closed a hang before anything hit it.
- **A third answer beside "stop" and "fake":** a request the machine can
  honestly record but not honestly perform, reported as a notice.
  `docs/machine.md` §5 has the rule and the test for when it applies.
- **`synthetic_boot`** in `tests/programs` — the CI-runnable shape of a
  boot: it unpacks itself, loads a module off the filesystem, far-calls
  into it through a relocated pointer, and calls every service M3 added.
  **A service that closes a boot-log line adds its call there in the
  same change.**
- **The first seam** (`machine/seam.h`), which is deliberately the
  smallest slice of PLAN.md §5's engine and not the engine: off by
  default, keyed by binary fingerprint, one `bool` per step when
  nothing is on. Its own header lists what M4 owes on top of it.

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
bash scripts/check-host-time.sh  # nothing under core/ reads the host's clock
bash scripts/check-format.sh  # clang-format over tracked C++
bash scripts/check-tidy.sh    # clang-tidy; needs a configured build tree
bash scripts/check-shell.sh   # shellcheck over scripts/
bash scripts/test-guards.sh   # guard self-test — run after editing a guard
bash scripts/test-sweep.sh    # session-runner self-test — after editing sweep.py
bash scripts/test-release-bundle.sh  # release-bundler self-test — after editing it
python3 scripts/sweep.py      # every committed session, on every target

python3 scripts/fetch-ocr-engine.py --into <the served web directory>
                              # the browser's OCR engine (#174), pinned by
                              # .tesseract-js-version, never committed
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
pass. The content guard scans every commit in history, the staged index
and the working tree for denylisted game-artifact filenames, files over
256 KiB, and — since #134 — anything that is not text whose path is not
on its allowlist of committed binaries, which has one entry. It refuses
an untracked binary lying beside the tree too: a stray dump was exactly
what a `git add -A` swept into a commit in #134, and a denylist can only
refuse names somebody thought of in advance. All of it is an auditable
tripwire against obvious artifacts, not proof by itself; the deeper
clean-content claim rests on the full public history being open to
inspection. Public history must never be rewritten (branch protection
blocks force pushes to main), so both guards must stay green on every
commit, not just at the tip.

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
