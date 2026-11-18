# Amber Folio — Project Plan

*Current milestone: M10, Secret of the Silver Blades. Last revised 2026-11-17.*

This is the engineering plan. v1, Pool of Radiance, shipped as 1.0 and
1.1, and Curse of the Azure Bonds as 1.2; M10 brings Secret of the
Silver Blades to the same bar (§1, §7).
Governance (license,
contributions, clean-content rule, trademarks) is settled in the repository
root documents and is not restated here — but two of its rules shape
everything below: the emulator ships **none of the original games'
expression**, and enhancements are **strictly opt-in**.

## 1. Goal

Play the SSI Gold Box games end-to-end — party creation, city and
dungeon exploration, story events, combat, shops, save/load — on four
targets, from the player's own copy of the game:

- **Windows** (x86-64)
- **macOS** (universal: arm64 + x86-64)
- **Linux** (x86-64)
- **WebAssembly** (wasm32, running in the browser)

Native targets are 64-bit only. The codebase is modern C++ (C++23,
falling back to C++20 where a platform toolchain lags), built with CMake.

**Pool of Radiance** is v1: 1.0 shipped when the gate below was green
for it, and 1.1 added the controller layer (M8). **Curse of the Azure
Bonds** was M9 and shipped as 1.2. **Secret of the Silver Blades** is
M10, and the goal now: the third game, played to the same bar.

**Release gate, per game:** the game is verifiably playable end-to-end —
a real playthrough sweep across all major systems, not just a booting
title screen — and its enhancement set works. For a game after the
first the bar is everything Pool of Radiance has, including what it
gained past its own 1.0: the store copy boots, plays and saves as sold;
Tandy sound; the host surfaces (onboarding, the config file, the toggle
panel, the document control, the key card, the on-screen keyboard,
screen text, the scene, per-slot sidecars); and every enhancement its
milestone ports (§7), off by default, each with its session pair.
Milestones are ordered by dependency, not calendar; M10 ships as
**1.3** when the gate is green for all three games.

## 2. What the player supplies

Amber Folio ships no game content. The player provides up to three
artifacts, verified by cryptographic fingerprint (SHA-256), with a
graceful "unrecognized artifact" path for editions we don't know yet:

| Artifact | Required? | What it enables |
|---|---|---|
| Game binaries + data files | **Yes** — nothing runs without them | The game itself |
| Adventurer's Journal (PDF) | Optional | The in-game journal enhancement |
| Code wheel (any form) | Optional | *Answer the code wheel once*, which asks for it **once** |

The policy is progressive: a missing artifact simply leaves its
enhancement unavailable — without the journal there is no in-game
journal, and without the wheel the wheel challenge appears exactly as it
did on the real machine. Fingerprints identify known editions (e.g. the
release sold on GOG and Steam); they are facts about the player's files,
and the only thing the project ever stores about the originals.

**The code wheel is the one artifact that is not a file** (#290). The
releases sold today ship a code generator application rather than a PDF
of the wheel, so the possession proof is the *act*: the player answers
the program's own challenge once and the enhancement remembers it (§5
item 1). It is the only one of the three artifacts this project never
sees.

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
- **Sound** — the PC speaker (PIT channel 2 plus the port 61h gate) and
  the Tandy 1000's sound chip (an SN76489-family part at port C0h: three
  tone voices and a noise voice), both box-filtered into the host audio
  stream. The program plays its music and effects through the chip only
  when its configuration file says Tandy, so every host starts every
  copy that way, the way the Steam release's own launcher does (#404).
  Writes to any other sound hardware are ignored (logged, not faked).
- **Input** — BIOS keyboard services (poll / blocking read) and the
  Ctrl-Break path.
- **DOS/BIOS services** — the small INT 21h subset the game actually
  uses (file open/create/read/write/seek/close/unlink/mkdir, date/time,
  console output, exit) over a virtual filesystem, plus the equally
  small INT 10h video subset (mode set, palette register set) and the
  BIOS tick count. No mouse driver is loaded: the mouse's vector has
  DOS's own `IRET` behind it, which is how a program that asks learns
  there is none (§7, M10).

The scope is deliberately the surface *this game* exercises — but the
Gold Box family shares its toolchain and engine lineage, so the machine
is built as a general small-PC core with the game-specific knowledge
confined to the seam layer (§5). Curse of the Azure Bonds (M9) was the
first sibling and Secret of the Silver Blades (M10) is the second, and
each means new seam facts and a few more services, not a new machine.

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
  execution, and the host UI (automap overlay, journal viewer) reads
  machine state but never writes it except through a seam.

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
- **Native in feel.** An enhancement is a modern quality-of-life
  change, and it is built the way the original developers would have
  built it had they shipped it: it respects the original design rather
  than stomping on it. Concretely — it draws with the game's own
  palette, font and screen conventions into the game's own screen, in
  the places the game leaves for such things; it is reached the way
  the game's own commands are reached (a key at a menu, not a modern
  overlay floating above the game); where the game already has a
  routine for the job, the seam drives that routine rather than
  replacing it (the code-wheel seam engages the program's own skip,
  the Encamp (F)ix drives the game's own camp loop); and it changes
  nothing a player did not ask for. A player who turns a seam on should
  feel that the game gained a feature it could always have had, not
  that a second program is running on top of it. This is a design
  requirement beside the fidelity invariant, not a matter of taste:
  an enhancement that cannot be made to feel native is reconsidered,
  not shipped louder.

### The v1 seam set

Pool of Radiance's. Which of them each later game gets, and why, is §7.

1. **Answer the code wheel once** — **it asks once** (#290, #291). With the seam
   on, the first launch is the machine's own: the challenge appears and
   the seam only watches. When the player answers it correctly, off
   whatever form of the wheel they own, the seam sees the program's own
   comparison come out equal and a host records that. Every later launch
   steps over the program's own call into the protection routine, so the
   challenge is never drawn. **The seam never answers the challenge for
   anybody**, and no OCR is involved.
2. **Adventurer's Journal** — gated on a fingerprint-verified journal
   PDF. At onboarding, a fact-table (page regions and stream offsets
   keyed by the known PDF editions' fingerprints) locates each journal
   entry's scan image directly in the player's own PDF — an
   edition-specific extractor (stream offsets, filters, and image
   parameters are part of the fact-table), not a general-purpose PDF
   parser or renderer; unrecognized editions are already rejected by
   the fingerprint gate — decoded by the host's image decoder, and the
   text extracted once by OCR (Tesseract); an edition typeset as text
   is read out of its own content streams instead, by offsets in the
   same fact-table, with no OCR at all (#398). In-game, the established journal reader layout is
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
5. **Explored overlay** — fog of war on the game's own overworld map,
   drawn by the seam into the emulated EGA planes over the game's own
   screen, in the game's palette, only while the game is showing that
   screen, from the same exploration state the automap keeps. A square
   the party has stood on is the game's own square, untouched; every
   other square in the window is covered by a one-pixel checkerboard of
   palette index 0 over the program's own pixels. The reveal radius is
   one named constant, `explored_reveal_radius`, a Chebyshev **0**. This
   is the one v1 enhancement with no proven prior design, so its marking
   was settled by the maintainer looking at it in play; the candidates
   tried and rejected are in `docs/explored-overlay.md` §5 (#179, #263,
   #299).
6. **Debug cheats** — invulnerability and kill-all-enemies, built
   early because they double as test tooling for the playthrough
   sweeps. A third, wound-the-party, was added for that stated reason
   (#196): an enhancement whose evidence needed a hurt party had no way
   to get one, since a fight that leaves the party alive is not
   something a driven leg can arrange and a save file this project wrote
   would be a save file nobody else has. Each is its own switch and each
   is off by default, like every other seam.

Save and roster management was withdrawn from v1 by decision (#176); a
machine-state export/import (#209) and a host-supplied VFS adapter (#206)
were declined.

Engine bugs no player could want are answered by **built-in fixes** on
the same machinery (`docs/seams.md` §7). The controller layer (§7, M8)
is an enhancement of this kind rather than a host feature: it moves the
program's own bar highlight and takes the program's own commands, which
is seam work and lives under every rule above.

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

Ordered by dependency; each has a crisp exit criterion. Every milestone
exit is tagged, and a tag is also cut whenever the build is worth
pinning, so there is always a current, runnable release. M0–M9 are
done.

### Done: Pool of Radiance (M0–M8)

- **M0 — Bootstrap.** *Exit: an empty core library + hosts build green
  on all targets in CI, and the wasm host is published from `main` with
  no human step.*
- **M1 — CPU core.** *Exit: the public single-step conformance suite
  passes in CI.*
- **M2 — Machine.** Memory map, MZ loader, PIT, EGA, speaker, DOS
  services over a VFS, keyboard, and a harness for small test programs.
  *Exit: self-written real-mode test programs run correctly on all
  targets.*
- **M3 — First light.** *Exit: title → party roster reachable, verified
  locally on desktop **and** web* (0.1).
- **M4 — Playable + seam engine.** *Exit: a full playthrough sweep
  passes on all targets; the cheats seam toggles end-to-end* (0.2).
- **M5 — Player enhancements.** *Exit: all six v1 seams work and toggle
  independently on desktop + web* (0.3).
- **M6 — Onboarding and shells.** *Exit: a new player goes from
  artifacts-in-hand to playing without reading source code, fully
  playable, text entry included, with no keyboard attached* (0.6).
- **M7 — Release 1.0.** *Exit: tagged 1.0, the gate in §1 green.* The
  desktop host is built from source.
- **M8 — A controller.** The game's own command bar is something a
  controller selects from (`modern-controls`, with the site's pad
  driving real keys). *Exit: 1.1.*

### Done: Curse of the Azure Bonds (M9)

The second Gold Box game at the same bar: the Steam and GOG copies
boot, play and save as sold; a session library and its legs
(`docs/playable-curse.md`); the shells per game. *Exit: 1.2.* A
person's look at each port on the web build, and onboarding tried in a
browser, are what #555 and #556 still hold.

- **Ported:** the debug cheats, `code-wheel`, the host reads (screen
  text, the scene, the aim, text entry), `modern-controls`, `font-sans`
  and `font-chisel`, `message-pause`, `automap`, the journal (both
  stores' editions), per-slot sidecars, and `fanfare` with a piece of
  its own.
- **Not ported:** `quick-steps` and the timer fix behind it (at 386 the
  game is fast enough); `explored` (the overland is a picture travelled
  from place to place); the Encamp (F)ix (the camp has its own Fix); the
  roster fix (the program keeps no roster file).
- **What it left for every later game.** Launcher facts are edition
  facts: the configuration file's name, which line says what, and the
  install directory are in the edition row. Facts about a program are
  in one place, the program profile (`machine/program_profile.h`), with
  a per-program row in each seam's own table; a seam is available on
  the programs whose facts it has (§5).

### M10 — Secret of the Silver Blades (current)

Bring the third Gold Box game to the bar the first two meet (§1): the
store copy boots, plays and saves as sold; a playthrough sweep passes
on all targets; every enhancement that fits the game is ported; and the
shells carry it. It exits as **1.3**. Tracking: #611.

**Known at the start.** These facts were measured on the Steam copy on
2026-11-17.

- The store copy identifies itself as **version 1.30**, of May 18,
  1990. Its `START.EXE` is EXEPACK-packed and 72,097 bytes. The
  launcher installs the game at `C:\SECRET` and starts it there. Forty
  program and data files and a configuration file. The GOG copy is not
  yet captured.
- `BLADES.CFG` has nineteen lines: graphics, sound, the alternate
  input, **the save directory on line 4**, a flag, and fourteen
  numbers. As shipped it says EGA, Tandy and the mouse.
- **No new video device.** The program's own setup offers CGA, EGA and
  Tandy, and it draws in the EGA mode the machine already has.
- **No new sound device.** The setup offers AdLib, Tandy, the PC
  speaker and silence, and the store copy is set to Tandy. The sound
  driver is a new one, with a data file of its own. At start it
  looks for cards this machine does not have, at port `388h` and across
  `220h`–`2FFh`; nothing claims those ports, which is the true answer.
- **The boot was unrecognized, and stopped at the mouse.** The
  shipped configuration asks for the mouse, and the program resets the
  mouse driver through `INT 33h` without reading the vector first; that
  was the stop, 79 frames in. The next was `INT 1Ah AH=00h`, the BIOS
  tick count, from the sound driver.
- **Past those two, nothing stops.** With both answered (#607), the
  titles run with their tune on the PC speaker, whatever the
  configuration says; the party menu comes up, with music on the Tandy
  chip; and a character is created as far as the reroll.
- **The overlay manager is a later generation.** `GAME.OVR` begins
  `TPOV` where the first two games' begins `FBOV`. Where a seam reads a
  module's load segment (`docs/seams.md` §8.1, #131) is measured again
  from this image, never assumed.
- **The menus are lists.** Each row is chosen with the arrow keys under
  a highlight, over a `SELECT`/`EXIT` bar. The party menu has an
  `INITIALIZE MOUSE/JOYSTICK` row.
- The shipped `SAVE\` holds saved games A and B, six characters each:
  `CHRDAT<S><N>.SAV` at 439 bytes, a `CHRDAT<S><N>.SFX` beside some of
  them, and `SAVGAM<S>.DAT` at 5,469 bytes.
- Steam's Adventurer's Journal is typeset, with pictures. It should
  take the text route (#398) and the picture route (#328, #345), with
  no OCR.
- **Not yet known:** where the program asks its copy-protection
  question (nothing asks between the boot and character creation);
  where its character import looks; whether the camp has its own Fix;
  whether there is an overland.

**Carried from the first two games.**

- Facts come from the store copies, never from a repack (#396–#398).
  GOG and Steam are compared byte for byte first.
- Launcher facts are edition facts, and a fact about a program is
  stated once, in its profile or in its seam's own row.
- Every fact is checked twice from the artifact and confirmed by a
  driven run. Overlaid points resolve from the program's own
  load-segment word.
- The snapshot disks are made first and never played in. The shipped
  saved games are read before any leg is planned.
- Every seam port brings its `identical`/`contrast` pair, recorded on a
  Secret of the Silver Blades disk. With every seam off the game is a
  plain machine, and that is a test.
- **A port is decided on this game's evidence.** What the program
  already does is measured before a seam is ported to do it: this one
  has list menus under a highlight, and its own title music.
- Before a visible seam is done, a person looks at it on the web build
  (#263, #345).
- The site agrees any ABI change before it ships (#375). Nothing
  game-specific goes into a host's `main.cpp`.

**The machine's answer to the mouse.** This machine has no mouse and
no joystick (§9), and it does not edit the player's configuration file
to say so. A PC with no mouse driver loaded has DOS's own `IRET` behind
`INT 33h`: the call comes back with its registers as they went, and
`AX=0000` after a reset is how a program learns that no driver is
there. That is this machine's answer, said once in the log as a notice
(`docs/machine.md` §5). The game is played from the keyboard.

**Phases.** Each may take an interim tag.

- **S0 — Ready** (#606).
  - The Steam edition row (`data/editions.json`, `machine/edition.cpp`),
    and the GOG one when that copy is captured.
  - `BLADES.CFG` and its line roles in the edition row.
  - The save-layer rows that have been watched.
  - The snapshot disks.

  *Exit: the game is recognized by name, with no seams available, and
  stops cleanly at its first unimplemented service. Pool of Radiance
  and Curse of the Azure Bonds are byte-identical.*
- **S1 — First light** (#607).
  - The two services the boot reaches, one per change, each with a
    `tests/programs` fixture and a unit test.
  - Tandy sound and the speed presets checked on this game.

  *Exit: title → party menu on desktop and web from the store copy,
  with every seam off.*
- **S2 — Playable** (#608).
  - The program profile, begun with the facts the debug cheats read,
    and the cheats first.
  - How a seam finds an overlay under this overlay manager.
  - The legs, in `docs/playable-secret.md`: a new party, loading saved
    games A and B, the town and a story event, a fight, a shop and the
    temple, a camp, save and load, and whatever the game has where the
    first two have a wilderness.
  - The program's own keys on every screen, written down.
  - The copy protection and the character import, found.
  - A session library pinned to the snapshots.
  - The save-layer row and per-slot sidecars.

  *Exit: a playthrough sweep passes on all targets, and the cheats
  toggle end-to-end.*
- **S3 — Enhanced** (#609). The seams below, in the table's order.
  *Exit: each works and toggles independently on desktop and web, with
  its pair, its leg and a person's look.*
- **S4 — Onboarded** (#610).
  - Both shells recognize a copy.
  - The toggle panel, the key card and the controls page have this
    game's form.
  - The document control knows its journals.

  *Exit: a new player goes from a Secret of the Silver Blades copy to
  playing without reading source.*
- **S5 — Release 1.3.** The docs and the release bundle. *Exit: the
  gate in §1 is green for all three games.*

**The seams on Secret of the Silver Blades**, in S3's order:

| Seam | On Secret of the Silver Blades |
|---|---|
| debug cheats | Port, first (S2). |
| `code-wheel` | Port if the program asks a question. First find where it asks, and whether the store copy accepts any answer. |
| screen text, scene, aim, text entry | Port the facts. The controls and the site both read them. |
| `modern-controls` | Port piece by piece, and only where S2's key table shows a gap: the game's menus are already lists under a highlight. |
| `font-sans`, `font-chisel` | Port. |
| `message-pause` | Port, and re-measure the pause. |
| `automap` | Port. |
| journal | Port. It is the largest item. |
| `quick-steps`, and the timer fix | Decided after the maintainer has played at 386, as for Curse. |
| `explored` | Only if the game has an overland that is walked. |
| Encamp (F)ix | Only if the camp has no Fix of its own. |
| roster fix | Only if the program has the failure. |
| `fanfare` | Decided by ear: the titles play a tune of their own, on the PC speaker, and the seam exists for titles that play none. |

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
  account. 1.0 ships no desktop binaries, so the question waits for
  the release that does (below).
- **A new program perturbing the ones that play.** A seam's third row
  touches the table two games already read. A seam whose facts are
  still at file scope gets its refactor as a commit of its own, before
  the new row, and every port is gated on every committed session
  verifying with unchanged hashes.
- **A later overlay manager.** Secret of the Silver Blades' overlay file
  is a different generation from the first two games' (§7, M10). If its
  manager moves or reloads a module in a way the load-segment word does
  not describe, overlaid seam points need a second way to be resolved,
  and that is found in S2, before any seam is ported.
- **A player expects the mouse.** The store copy is configured for one
  and its launcher provides one. This machine answers that there is
  none (§7, M10), and the shells say the game is played from the
  keyboard.
- **The journal's cost is per edition** (#357, #361), though the
  typeset Steam editions need no OCR.
- **Scope creep toward the rest of the family.** Pools of Darkness and
  the Savage Frontier games share the engine lineage, but they start
  after M10 ships. Pools of Darkness's 256-colour VGA would be a new
  device module.

## 9. Out of scope

- The amberfolio.org website and hosted player (separate initiative;
  the reference shell here is its future substrate).
- Gold Box titles past Secret of the Silver Blades. They start after
  M10.
- Hillsfar character import into Curse.
- Putting an earlier game's party where the next game's import reads
  it, in that game's own save directory: Pool of Radiance into Curse,
  Curse into Secret of the Silver Blades. It will be solved through the
  web page, not in a milestone here.
- Other audio hardware (Ad Lib, Roland), mouse and joystick support,
  other video modes (CGA/MCGA/Hercules), machine save-states,
  localization, 32-bit builds.
- A CRT presentation filter. What a frame looks like on the way to a
  screen is a host's to decide, and amberfolio.org has its own; the
  hosts here show the frame the machine drew.

## 10. Open questions

- macOS signing/notarization for the first release with desktop
  binaries: acquire the developer account, or ship unsigned with
  instructions?
- Which game-binary editions to fingerprint beyond the known ones.
  Pool of Radiance has one START.EXE in two releases: the one sold on
  GOG and Steam (the baseline row) and a third-party repack of it. They
  differ only in two bytes of the copy-protection overlay and in files
  the repack adds, and every seam is valid on both (`docs/seams.md` §5,
  #396). Curse has its Steam and GOG copies, one v1.3 image in two
  rows. Secret of the Silver Blades has the Steam version 1.30 copy;
  the GOG copy is captured in S0 (§7, M10). Other builds of any of the
  programs are open.
- Which **document** editions to fingerprint. The table
  (`machine/document.cpp`) has a code wheel (a fact; it gates nothing
  since #290) and an Adventurer's Journal for each of the GOG and Steam
  releases: the GOG journal is scans, read by OCR (#214), and the Steam
  collection's is text, read out of the document (#398). Every other
  journal is refused as unrecognized, which is the fail-closed direction.
  Curse has both stores' journals in the table as well, and Secret of
  the Silver Blades' join it in M10. Which re-scans or reissues to add
  is open.
