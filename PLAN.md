# Amber Folio — Project Plan

*v1 target: Pool of Radiance. Last revised 2026-08-16.*

This is the engineering plan for the first release. Governance (license,
contributions, clean-content rule, trademarks) is settled in the repository
root documents and is not restated here — but two of its rules shape
everything below: the emulator ships **none of the original games'
expression**, and enhancements are **strictly opt-in**.

## 1. Goal

Ship a v1 that plays **Pool of Radiance** end-to-end — party creation,
city and dungeon exploration, story events, combat, shops, save/load —
on four targets, from the player's own copy of the game:

- **Windows** (x86-64)
- **macOS** (universal: arm64 + x86-64)
- **Linux** (x86-64)
- **WebAssembly** (wasm32, running in the browser)

Native targets are 64-bit only. The codebase is modern C++ (C++23,
falling back to C++20 where a platform toolchain lags), built with CMake.

**Release gate:** the game is verifiably playable end-to-end — a real
playthrough sweep across all major systems, not just a booting title
screen — and the v1 enhancement set below works. Milestones are ordered
by dependency, not calendar; **1.0** ships when the gate is green, with
tagged 0.x pre-releases at milestone exits along the way (§7).

## 2. What the player supplies

Amber Folio ships no game content. The player provides up to three
artifacts, verified by cryptographic fingerprint (SHA-256), with a
graceful "unrecognized artifact" path for editions we don't know yet:

| Artifact | Required? | What it enables |
|---|---|---|
| Game binaries + data files | **Yes** — nothing runs without them | The game itself |
| Adventurer's Journal (PDF) | Optional | The in-game journal enhancement |
| Code wheel (PDF) | Optional | The copy-protection bypass |

The policy is progressive: missing PDFs simply leave their enhancement
unavailable — without the code wheel PDF, the wheel challenge appears
exactly as it did on the real machine. Fingerprints identify known
editions (e.g. the officially sold archive releases); they are facts
about the player's files, and the only thing the project ever stores
about the originals.

## 3. The machine (v1 scope)

A **targeted low-level emulator**: the hardware the game program touches
is emulated at the register level; the thin BIOS/DOS service layer
beneath it is provided by the emulator. The original program runs
unmodified — including its own runtime unpacker and overlay manager,
which execute on the emulated CPU like everything else and need no
special handling beyond working DOS file I/O.

- **CPU** — Intel 8086, real mode, interpreter. The target is
  **architectural correctness** (registers, flags, memory, interrupts);
  cycle- or bus-level 8088 timing is a non-goal — this game family ran
  across a decade of PC hardware and does not depend on it. No
  performance concerns at this scale on any 64-bit host or browser.
  Virtual time advances by a fixed cost per scheduling step, where a
  step is one ordinary instruction or **one iteration of a repeated
  string operation** — REP runs are interruptible between iterations on
  the real 8086, and treating a whole run as one step would stall the
  virtual clock and block timer interrupts during large copies. A
  deliberately simple, deterministic model (per-opcode cycle counting
  is part of the timing non-goal); a configurable speed governor scales
  the step cost, with period presets so pacing matches the machines the
  game was designed for.
- **Memory** — 1 MB real-mode address space, 640 KB conventional.
- **Video** — EGA, 320×200 16-color planar graphics: the sequencer /
  graphics-controller register subset the game uses (map mask,
  set/reset, data rotate/ALU, read map, write modes, bit mask, latches)
  and the palette registers. Output is an indexed framebuffer the host
  presents.
- **Timer** — 8253 PIT, channels 0 (system tick, delivered as the timer
  interrupt) and 2 (speaker tone).
- **Sound** — PC speaker only for v1: PIT channel 2 plus the port 61h
  gate, box-filtered into the host audio stream. Writes to any other
  sound hardware are ignored (logged, not faked).
- **Input** — BIOS keyboard services (poll / blocking read) and the
  Ctrl-Break path.
- **DOS/BIOS services** — the small INT 21h subset the game actually
  uses (file open/create/read/write/seek/close/unlink/mkdir, date/time,
  console output, exit) over a virtual filesystem, plus the equally
  small INT 10h video subset (mode set, palette register set).

The scope is deliberately the surface *this game* exercises — but the
Gold Box family shares its toolchain and engine lineage, so the machine
is built as a general small-PC core with the game-specific knowledge
confined to the seam layer (§5). Sibling titles later mean new seam
sets and incremental device modules, not a new machine.

**Discipline rule carried from the machine's design into code review:**
an unimplemented service or register is a loud log line and a clean
stop, never a silently faked answer. Guessing is how emulators end up
subtly wrong for decades.

## 4. Architecture

```
 ┌────────────────────────── core (C++) ──────────────────────────┐
 │  cpu (8086)   memory    devices (ega, pit, speaker)            │
 │  dos services + VFS     machine (wiring, scheduler)            │
 │  seam engine (fingerprint DB, breakpoints, toggles)            │
 └───────────────┬────────────────────────────────┬───────────────┘
                 │  narrow platform interface     │
        ┌────────┴────────┐              ┌────────┴────────┐
        │  SDL3 host      │              │  web host (JS)  │
        │  Win/mac/Linux  │              │  canvas, Web-   │
        │                 │              │  Audio, IndexedDB│
        └─────────────────┘              └─────────────────┘
```

- **Core** — freestanding C++ library, no host dependencies. Exposes a
  narrow platform interface: present a frame (320×200 indexed + 16-color
  palette), pull audio samples, receive input events, read/write the
  virtual filesystem, date/time access. Compiled to each native target
  and, via Emscripten, to wasm32 with a small C ABI for the JS host.
- **Deterministic core.** All machine-visible time derives from the
  virtual clock (§3): the PIT, interrupt delivery, and audio synthesis
  run on virtual time, never host time — host wall time only throttles
  presentation, outside machine state. Everything nondeterministic the
  machine consumes (input events with their timing, date/time reads)
  arrives through the platform interface and is therefore recordable
  and injectable; a replay that also captures the initial conditions —
  VFS contents and ordering, configuration, active seam set, artifact
  fingerprints — reproduces a run exactly. Replay goldens hash a
  canonical, versioned machine-state serialization; floating-point
  audio output is excluded from hashes.
- **Desktop host** — one SDL3 (zlib-licensed) host for all three
  desktop platforms: window, scaling/aspect (with period-correct
  non-square-pixel option), audio callback, keyboard, config file, and
  a directory-backed VFS pointed at the player's game files.
- **Gamepad + virtual keyboard** — the game is keyboard-driven (menu
  letters, Y/N prompts, movement, free-text names), so gamepad support
  is a mapping layer, not just button events: a host-agnostic mapping
  model (d-pad/stick → movement, face buttons → the common confirm/
  cancel/menu keys, remappable profiles) shared by both hosts, with
  SDL3's gamepad API on desktop and the browser Gamepad API on the
  web. Anything not covered by the mapping is reachable through an
  on-screen **virtual keyboard** overlay, navigable by gamepad (and by
  touch on the web), feeding the same synthetic-input path the seams
  use. Each host presents the overlay its own way (SDL-rendered on
  desktop, DOM on the web); the layout and navigation model are
  shared. This is a significant, deliberate chunk of v1 scope.
- **OCR backend** — Tesseract (Apache-2.0) behind a host service
  interface, used only at artifact-ingestion time (journal text
  extraction): libtesseract/leptonica natively, the Tesseract wasm
  build in the browser. Kept out of the core; the core sees extracted
  text, never PDFs.
- **Web host** — hand-written JS: canvas blit, WebAudio, keyboard and
  touch input, IndexedDB-backed VFS and artifact cache. Deliberately not
  SDL-through-Emscripten: the browser features that matter (file-picker
  onboarding, persistence, touch controls, the toggle overlay) are DOM
  work SDL cannot do, and skipping it keeps the wasm bundle lean. The
  repository ships a **reference shell** — a minimal self-hostable page
  (onboarding, canvas, toggle panel) that anyone can serve as-is. The
  amberfolio.org site itself is a separate initiative, out of scope here.
- **Fidelity boundary** — with every seam off, the core is a plain
  machine running an unmodified program; the seam engine is the *only*
  component that ever alters the machine's memory or intercepts its
  execution, and the host UI (automap overlay, journal viewer, save
  manager) reads machine state but never writes it except through a
  seam.

## 5. Enhancements as seams

A **seam** is an opt-in runtime patch applied to the emulated machine's
memory or execution — the bytes on the player's disk are never touched.
Concretely, a seam is: an identifier, a description, the binary
fingerprint(s) it applies to, and a set of interception points
(CS:IP breakpoints) with actions — register/memory surgery, synthetic
input, or a call out to a host service. The knowledge of *where* to
hook is a database of addresses and offsets: facts about the original
program, per the clean-content rule.

Design requirements:

- **Individually toggleable.** Every seam has its own config key and
  UI toggle; any subset can be active. Defaults are **off** —
  fidelity first, enhancement by choice.
- **Per-binary.** Seam sets are keyed by binary fingerprint; an
  unrecognized binary runs with no seams available rather than
  misapplied ones.
- **Nothing original embedded.** Seams carry our code and addresses
  only — never byte sequences from the game.
- **Native execution.** Seam logic is modern C++ compiled into the
  emulator itself. When an interception point is hit, the handler runs
  natively, reaching into the emulated machine's memory and registers
  from outside — never as injected code executing on the emulated CPU.
  Enhancements therefore run at host speed with no interpreter
  overhead, and inactive seams cost nothing on the hot path (breakpoint
  checks exist only for seams that are switched on). The one deliberate
  exception is behavior that *is* game behavior by design — e.g. the
  Encamp (F)ix seam makes the game's own routines do the work so game
  time passes authentically — but the orchestration around them is
  native code.
- **Overlay-aware, fail-closed.** The original program swaps overlaid
  code through shared memory, so a raw address does not identify code;
  interception points are qualified by which module/overlay is
  currently resident (the machine tracks this as the program loads
  them). A seam whose preconditions aren't met stays inert and reports
  why, rather than firing on the wrong code. The seam schema and any
  seam-persisted data are versioned; disabling a seam restores
  untouched behavior.
- **Designs are settled, not reopened.** Each v1 enhancement
  re-expresses an already-proven design — screen layouts, integration
  points, behavior — as-is. The work in this project is carrying the
  mechanism onto the seam engine, not redesigning the features.

### The v1 seam set

1. **Code-wheel bypass** — gated on a fingerprint-verified code wheel
   PDF: a possession gate (it demonstrates the player holds the
   document, no more). Once verified, the seam skips the challenge —
   preferring to engage the program's own built-in skip (a launch
   parameter long documented in the game's cheat lore) over patching
   the check, so the unmodified program simply exercises its own
   functionality. (No OCR involved.)
2. **Adventurer's Journal** — gated on a fingerprint-verified journal
   PDF. At onboarding, a fact-table (page regions and stream offsets
   keyed by the known PDF editions' fingerprints) locates each journal
   entry's scan image directly in the player's own PDF — an
   edition-specific extractor (stream offsets, filters, and image
   parameters are part of the fact-table), not a general-purpose PDF
   parser or renderer; unrecognized editions are already rejected by
   the fingerprint gate — decoded by the host's image decoder, and the
   text extracted once by OCR (Tesseract). In-game, the established journal reader layout is
   rendered on the game's own screen, and a seam watches the game's
   text output to auto-open the right entry when the game cites one.
3. **Automap** — an in-game map panel drawn by the seam directly into
   the emulated EGA planes (so it appears on the game's own screen,
   in captures and replays alike), rendered from live machine state:
   party position, facing, explored geometry, zone names. Toggled by
   hotkey — claimed at the keyboard-service funnel without altering
   the number of input polls the game observes, so replay timing is
   untouched. Map exploration state is persisted alongside the save.
4. **Encamp (F)ix** — the auto rest-and-heal camp command later Gold
   Box titles had: drives the game's *own* memorize → rest → heal loop
   through orchestrated input, so game time passes and random
   encounters still roll — automation, not cheating.
5. **Save & roster management** — delete/back up save games, delete
   characters from the roster (operations the original never offered).
   Host-level VFS operations surfaced in the shell UI; a seam is
   involved only where in-game state must be kept consistent.
6. **Debug cheats** — invulnerability and kill-all-enemies, built
   early because they double as test tooling for the playthrough
   sweeps.

Known engine bug-fix seams (roster and money-handling bugs, a map-edge
transition trap) are a documented fast-follow after v1, on the same
seam machinery.

## 6. Testing

The repository and CI contain **no game content, ever** (the content
guard enforces this on every push). The strategy splits cleanly along
that line:

**Public, in CI:**

- Build matrix: all four targets on every push.
- CPU conformance: the interpreter validated instruction-by-instruction
  against the public single-step 8088 test suites (MIT-licensed JSON
  vectors) — thousands of per-instruction register/flag/memory cases.
- Device unit tests: EGA register/latch/ALU behavior, PIT counting and
  reload semantics, speaker gate — driven by small self-written test
  programs assembled into fixtures.
- DOS layer tests over a synthetic VFS.
- Sanitizer (ASan/UBSan) jobs on the native build; format/lint gates.

**Local, with the maintainer's own game copy:**

- Boot and playthrough sweeps on all targets.
- A replay harness: scripted input (with timing) plus recorded
  date/time reads — every nondeterministic input the machine consumes —
  and frame/state *hashes* as goldens. Hashes are committable; screen
  content never is.
- Emulator-vs-original behavior spot checks for anything suspicious.

## 7. Milestones

Ordered by dependency; each has a crisp exit criterion. From M3 on,
every milestone exit is tagged as a **0.x pre-release** (M3 → 0.1
"boots", M4 → 0.2 "playable", M5 → 0.3 "enhanced", M6 → 0.4
"onboarded"), so there is always a current, runnable tag while the work
converges on **1.0** — the release the gate in §1 defines.

- **M0 — Bootstrap.** CMake + presets, CI matrix for all four targets,
  unit-test rig, format/lint/sanitizer gates, DCO check, and automatic
  deployment of the wasm host to a public URL on every push. *Exit: an
  empty core library + hosts build green on all targets in CI, and the
  wasm host is published from `main` with no human step.*
- **M1 — CPU core.** 8086 interpreter with exact flag semantics and
  interrupt delivery. *Exit: public single-step conformance suite
  passes in CI.*
- **M2 — Machine.** Memory map, MZ loader with relocations, PIT +
  timer interrupt, EGA device + renderer, speaker audio path, DOS
  service layer + VFS, keyboard services; a machine harness that runs
  small test programs; a bare dev page proving the wasm target. *Exit:
  self-written real-mode test programs run correctly on all targets.*
- **M3 — First light.** The game boots from a player-supplied copy:
  its unpacker and overlay manager run as-is, the title sequence
  renders, menus respond. *Exit: title → party roster reachable,
  verified locally on desktop + web.*
- **M4 — Playable + seam engine.** Full game loop: exploration, story
  events, combat, shops, save/load; audio; speed calibration. The seam
  engine lands here, and debug cheats land first to power the sweep.
  Replay harness + first goldens. *Exit: a full playthrough sweep
  passes on all targets; cheats seam toggleable end-to-end.*
- **M5 — Player enhancements.** Code-wheel bypass, journal (OCR
  ingestion + in-game reader + auto-open), automap panel, Encamp
  (F)ix, save & roster management — each
  individually toggleable, each off by default. *Exit: all six v1
  seams work and toggle independently on desktop + web.*
- **M6 — Onboarding, shells + gamepad.** The reference web shell
  (file-picker / drag-drop onboarding, artifact fingerprinting with a
  clear unrecognized-edition path, IndexedDB persistence, touch
  controls, toggle panel), desktop onboarding polish (first-run
  pointing at the game directory, config file), and the gamepad
  mapping layer + virtual keyboard on both hosts. *Exit: a new player
  goes from artifacts-in-hand to playing without reading source code,
  and the game is fully playable — including text entry — with only a
  gamepad in hand.*
- **M7 — Release 1.0.** Versioning, GitHub Releases with prebuilt
  binaries for the three desktop targets + the wasm bundle, README/docs
  refresh, a short "supplying your artifacts" guide. *Exit: tagged 1.0
  release, binaries downloadable, release gate (§1) green.*

## 8. Risks and mitigations

- **Emulation fidelity gaps** (CPU flag corner cases, EGA latch/ALU
  subtleties, timing). Mitigated by the conformance suites, the
  log-don't-fake rule, and the replay goldens; the game itself is the
  final integration test.
- **Speed/pacing feel.** The original's pacing depended on period
  hardware; the governor's presets need playtest tuning. Low risk,
  contained in one knob.
- **Artifact edition variance.** Players will hold editions we haven't
  fingerprinted (different releases, re-scanned PDFs). Mitigated by
  supporting the currently sold editions first, a friendly
  unrecognized-artifact message, and a process for adding editions.
- **macOS distribution.** Signing/notarization needs an Apple developer
  account; unsigned builds with instructions are acceptable for the
  0.x pre-releases. Open question below for 1.0.
- **Scope creep toward the family.** Sibling titles are tempting and
  the machine is family-shaped by design — but v1 is Pool of Radiance
  only; family work starts after 1.0 ships.

## 9. Out of scope for v1

- The amberfolio.org website and hosted player (separate initiative;
  the reference shell here is its future substrate).
- Other Gold Box titles (the design keeps the door open; the work
  starts post-v1).
- Other audio hardware (Ad Lib, Roland, Tandy), mouse support, other
  video modes (CGA/MCGA/Hercules), machine save-states, localization,
  32-bit builds.

## 10. Open questions

- macOS signing/notarization for 1.0: acquire the developer account
  during the 0.x run, or ship 1.0 unsigned with instructions too?
- Which game-binary editions to fingerprint at launch (the currently
  sold archive release is the baseline — which others?).
