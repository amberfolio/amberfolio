# Seams

How an enhancement reaches the machine, and the rule that nothing else
may. The sibling of [`machine.md`](machine.md), which is about the
machine below the fidelity boundary; this is about the one mechanism
above it, the seam engine (`machine/seam.h`, M4-F2 #96), and the house
style every seam in the tree follows.

> A **seam** is an opt-in runtime patch: an id, a description, the
> fingerprints of the binaries it is about, and a set of interception
> points (CS:IP breakpoints, each qualified by the module it lives in)
> whose handlers are native C++ that reach into the machine from outside.
> Off by default. Unavailable for any binary it does not name. Inert, and
> says so, when its module is not resident. With every seam off, nothing
> but the program moves the machine.

- [1. What a seam is](#1-what-a-seam-is)
- [2. The handler contract](#2-the-handler-contract)
- [3. The action primitives](#3-the-action-primitives)
- [3a. The trigger: a host pulls, a seam acts](#3a-the-trigger-a-host-pulls-a-seam-acts)
- [4. Qualified points: the resident image and the overlays](#4-qualified-points-the-resident-image-and-the-overlays)
- [5. Identity: fingerprints and editions](#5-identity-fingerprints-and-editions)
- [6. The toggle surface](#6-the-toggle-surface)
- [7. The fidelity invariant](#7-the-fidelity-invariant)
- [8. Writing a seam](#8-writing-a-seam)
- [9. The review rule](#9-the-review-rule)
- [10. The seams this build carries](#10-the-seams-this-build-carries)

---

## 1. What a seam is

`seam_definition` (`machine/seam.h`) is a fact table:

| field | what it is |
|---|---|
| `id` | the config key and the name `--seam` takes; kebab-case |
| `about` | one line for a listing |
| `fingerprints` | the SHA-256s of the program images the addresses below are facts about |
| `points` | interception points: a module, an offset in it, a handler |
| `trigger` | whether this seam is **pulled** rather than left on (§3a) |
| `gate` | a document the player must present before it arms (§5); unused by every shipped seam |
| `schema` | the `seam_schema_version` the definition was written against |

Everything in it is a fact about the program (an address, an offset, a
hash) and nothing in it is content from the program: CONTRIBUTING.md's
clean-content rule, at the place it bites hardest. A seam names the code
it lives in by the facts of the read that loads it (`overlay.h`), never
by bytes to match against.

The build's own seams are `all_seams()`; `seam_code_wheel.cpp` is the
template. A host or a test may register more with `seam_engine::add()`.
The registry is a fixed table of `seam_engine::max_seams`.

---

## 2. The handler contract

- A `seam_handler` is `void (*)(machine&, seam_context&)`, a plain
  function pointer like `cpu::handler` and `service_handler`: core
  carries no `<functional>` and allocates nothing, so a handler has no
  state but what it is handed.
- It runs **at the step boundary, before the instruction at CS:IP is
  fetched**, the only point at which CS:IP is settled. Order in
  `machine::step()`: deadlines, the keyboard drain, the BIOS callout,
  then seams (`docs/machine.md` §3), so a point on a BIOS stub sees the
  state the service left.
- A handler that wants the instruction to run returns; one that wants it
  not to run moves IP.
- It **must not stop the machine**. A seam whose preconditions fail stays
  inert and says why, through `seam_event` on the diagnostics channel and
  `seam_engine::status()` (PLAN.md §5's fail-closed rule).
- A precondition that can only be checked once the handler is running
  (a stack frame whose argument is not the pointer it should be, a record
  where there is none) is answered with
  `ctx.decline(seam_reason::point_not_recognized)` and a return that
  touches nothing. Reported once per seam per enable. A seam that writes
  through a wrong address surfaces as a wrong number three layers from
  its cause; check what you can check and decline the rest.
- It must not toggle seams. `enable()`/`disable()` are configuration,
  applied before the program runs or between `run()` calls.

What a handler is handed beside the machine is `seam_context`: the
seam's id, the physical address the point fired at, the base of the
module it lives in, and the image base.

---

## 3. The action primitives

Eight things a handler may do, in the order §8.2 says to reach for them.

**1. Register surgery.** `box.processor().regs()`, edited in place.

**2. Memory surgery, as the program.** `box.processor().read_byte()` and
`write_byte()`, through the bus, so a write into the video window
reaches the EGA pipeline, a write into ROM is refused, and a touch of
nothing is noticed. Never `memory().ram()`: that door is for the
machine's own writers (loader, BIOS setup, tests).

A seam may **put a command on a menu the program owns** this way. This
program's menu bars are Pascal strings in its data segment handed to its
own menu-bar routine, which draws every character and treats each
command letter as selectable. Splice characters in before the bar goes
out and back out when the routine returns, and the program draws the
command in its own font and highlighting. Rules (`seam_encamp_fix.cpp`
is the worked example):

- **Splice, never compose.** Find the separator and insert; a seam that
  spelled the program's bar out would carry its text in this repository.
- **Check the room and refuse rather than overrun.** The slots are
  fixed-capacity and the bar is as wide as the screen; a splice that
  does not fit declines (§2).
- **Add a letter the program does not already use.** An unrecognised
  letter is ignored; a recognised one is a command fired by accident.

**3. Port surgery, as the program.** `machine::write_port8()`, the same
bus one instruction over. Added for the automap (#173): the EGA reaches a
plane through the sequencer's map mask, so a byte written with the mask
the program left lands in all four planes at once, and a seam that draws
sixteen colours must select a plane at a time. Three rules:

- **Set what you depend on; do not assume it.** The registers are
  write-only and cannot be read back. Program the four registers your
  write needs.
- **Hand back the state you found.** This program resets the graphics
  controller and map mask at the head of every drawing primitive and
  restores the write mode at its end, so between commands there is a
  resting state; a seam that leaves it is indistinguishable from one of
  the program's primitives having run.
- **A masked write reads first, and the read is not free.** Bits the bit
  mask clears come from the adapter's **latches**, loaded only by a read
  through the bus. A veil costs a read and a write per byte, and the
  latches cannot be handed back. Tolerable because the program's own
  read-modify-write primitives leave them the same way; a seam that is
  only looking must still never read the video window (§8.4).

**4. Synthetic input.** `seam_context::inject_keystroke(scancode, ascii)`
puts a keystroke straight into the BIOS buffer at 40:1E, not through
`input_queue` (the host's recordable stream, `platform.h`): a seam's keys
are a consequence of the seam set, which a replay records as an initial
condition. **One key per arrival, and not two:** this program drains its
keyboard after every key it reads, so a handler that posts two keys posts
one. A seam driving a sequence needs one handler call per key, which is
why the Encamp Fix stopped being a trigger when it grew a second key
(#186).

**5. Control.** `seam_context::redirect(cs, ip)`: moving IP, with its
name on.

**6. A seam's own few words.** `seam_context::scratch()` and
`set_scratch()`, `scratch_words` of them (#189). Reach for this last. A
handler that remembers nothing cannot remember wrongly; what genuinely
cannot be read back is a comparison against *before*, and that is what
the words are for. They are configuration like the enable: `reset()` and
`clear()` drop them, `enable()` starts them fresh, the serialization
never sees them.

**7. A call into the program.** `seam_context::call_program()`, with
`place_bytes()` for arguments that are not numbers (#188). Text a seam
puts on the screen is drawn by the program in its own font; `font.h` is
deliberately not that font, so a seam that rasterized glyphs itself would
put foreign lettering beside the game's.

It is a **batch**: a handler queues the whole sequence in one arrival and
is never re-entered part-way. The engine snapshots the register file (IP
and SP included), lowers SP over anything `place_bytes()` put on the
stack, and for each call pushes its words in the order given and then a
far return address in the BIOS region that is no stub, no vector and
nothing executes; at that address it sets up the next call or restores
the snapshot, and the machine then executes the instruction it was about
to. The program's routines are far and clean their own arguments
(`retf N`), so the engine builds frames and never tears one down.
Properties:

- **When the batch is done the point is offered again**, at the
  instruction the handler was called at; otherwise a seam driving a
  sequence would act exactly once per arrival.
- **An interrupt during the batch is survivable**: the frame is one the
  program could have built, on the program's own stack.
- **A batch finishes even if the seam is switched off while it runs**;
  `armed()` stays true until it returns.
- **Nothing is written to make it work.** The power-on image is machine
  state and is hashed; an IRET left at the sentinel would alter every
  run. A call that does not come back is caught by the batch's **step
  budget**, which restores the snapshot and reports `call_did_not_return`.
- **The budget is a bound against never, not against slow**: sixteen
  million steps. The program's cast driver repaints its screen off the
  disk at one to two million steps per call, and a budget of 250,000
  made the seam look broken (#189).
- **A batch offers no points to any seam while it runs** (§8.4).
- **Limits**: twelve calls and 256 placed bytes per batch. A screen
  that needs more is painted over several arrivals.

`place_bytes()` puts a seam's Pascal string on the machine's own stack,
below what the program uses and above where an interrupt would push; it
is gone when the batch ends.

The frame to build is a fact read off the routine. The two drawing
routines the seams use:

| routine | image offset | cleans | pushed first ... last |
| --- | --- | --- | --- |
| draw a Pascal string at a cell | `0x076B6` | `retf 0Ah` | col, row, colour, string segment, string offset |
| draw a framed box with a centred title | `0x041F8` | `retf 10h` | left, top, right, bottom, style, colour, title segment, title offset |

Arguments are pushed in the reverse of the order the routine's source
lists them, a far pointer's segment before its offset so the offset lands
at the lower address where `les` looks. Re-check that on every new
routine; #188 confirmed it on four (each opens `push bp / mov bp, sp`).

**The box drawer's rectangle is the interior.** Its border lands outside
on all four sides: rows `top - 1` and `bottom + 1`, columns `left - 1`
and `right + 1`. The widest full screen is `(1, 1, 0x26, 0x16)`, not
`(0, 1, 0x27, 0x17)`: a vertical run in column `-1` or `0x28` is not
clipped, and the video window is row-major at eight pixels a byte, so it
wraps onto the neighbouring scanline (#222).

**8. A host service.** `seam_context::call_host(which, argument)`,
answered by the `seam_host_services` a host attached with
`seam_engine::set_host()`. Both hosts attach the same implementation,
`hosts/common/include/amberfolio/host/host_services.h`. Five services:

| service | seam | since |
| --- | --- | --- |
| `journal_open` | journal | #169 |
| `automap_update` | automap, explored | #169 |
| `journal_seen` | journal | #222 |
| `code_wheel_answered` | code-wheel | #291 |
| `journal_art` | journal | #328 |

- It is C++ running inside the module, synchronously, on both targets,
  never a queue a page drains later: `serve()` is handed the machine and
  what it reads is only true at the moment of the call. What crosses to
  JS is the record of the call.
- **An answer that is not a `bool` comes back in a buffer.** `serve()`
  answers `void` and `call_host()` answers "served"; what a host found
  goes into `machine::journal()` (`machine/journal.h`), observation and
  not machine state: a machine holding a page of somebody's journal
  hashes as one that is not.
- **An answer a caller pages by is carried by the refusals too.** Every
  `journal_art` answer, refusals included, carries how many pictures the
  entry has, because the count is half of how many pages the reader
  draws (#328).
- The record is **polled**: `seam_engine::host_calls(which)` counts calls
  a host actually served, `host_argument(which)` keeps the last argument;
  `af_machine_seam_host_calls` / `_host_argument` on the ABI; both hosts
  print them at the end of a run. A call on a machine with no host
  attached counts nothing and the handler is told false.
- Rejected: a `save_state_changed` service, because save management was
  withdrawn from the plan (#176) and a service with no consumer is a
  surface built on spec.

#165 audited the five M5 enhancements against these primitives before
any was written: none needed a new one but port surgery.

---

## 3a. The trigger: a host pulls, a seam acts

`call_host()` is the seam-to-host direction. A definition with
`trigger = true` gets the other one (#161):

- `seam_engine::pull(id, now)` sets a **one-shot latch**. Refused with a
  reason for a seam that is off (`not_enabled`) or takes no trigger
  (`not_triggered`).
- The next time one of that seam's points is reached with the latch set,
  the handler runs and the latch clears. One pull, one run.
- With the latch unset the point is reached, the arrival counted, and
  the handler not called.
- A handler that `decline()`s **keeps** the latch: a pull that arrived at
  the wrong point was not served. A handler that arrives and chooses to
  do nothing has been served.

**A pull means "at the next step boundary at which acting is safe"**,
never "at this instant" (PLAN.md §5). An address point knows safety from
its address and pays in latency. A seam's status row carries:

| number | the claim |
| --- | --- |
| `armed` | an address was computed out of the fact table |
| `reached` | the program arrived at that address, N times, pulled or not. **Addressed points only** |
| `fired` | a handler ran **and acted**, N times; a decline is not one |
| `declined` | a handler ran and would not act, N times |
| `waiting` | a pull is outstanding |
| `pulled_at` / `waited` | when the outstanding pull was made, and what the last served one cost, in ticks |

`reached − fired` is the arrivals a trigger did not need; the rate of
`reached` is the granularity at which that point can serve a pull.

What a row **means** is `machine::seam_reading_of()`, decided once in
core and handed to a page as text by `af_machine_seam_reading`. It never
warns about an address when `fired > 0`
(`SeamReading.NeverWarnsAboutAnAddressWhenTheSeamActed`):

| reading | when | what a row says |
| --- | --- | --- |
| nothing to say | an ordinary seam that acted, or an inert one | the numbers only |
| served | a trigger was pulled and served | `- pulled, and served`, with `waited=` |
| pulled, not served | a pull is outstanding and nothing offered since | `- pulled, and its point not reached since` |
| pulled, declined | a pull is outstanding and every offer refused | `- pulled, and not served: what it was offered is not what its facts describe` |
| reached, never pulled | a trigger's point was arrived at and nobody asked | `- reached, and never pulled; this seam acts only when asked` |
| never reached | armed at an address the program never went to, and it did not act | `- armed and never reached; its point may not be where its facts say` |

**A point with no address** (#163). A point may set
`seam_point::at_every_step`:

- offered at **every step boundary** while its seam's latch is set, and
  never otherwise; with the latch down the cost is one bool;
- its handler opens with a guard it can defend and `decline()`s, keeping
  the latch, until the guard holds. A guard is evidence about the
  program's structures, weaker than an address, and one that cannot rule
  out being mid-walk of what it edits has to say so
  (`seam_cheats.cpp`'s `combat_roster_ready()`);
- it counts no `reached`; counting its offers would be counting steps;
- it still names a module and is not offered while the program's own
  record says that module is not in memory (§4).

Tests: `SeamPullPoint.*`,
`SeamFidelity.APointWithNoAddressNobodyPulledLeavesTheRunIdentical`, and
`tests/programs`' `seam_probe_pull` / `seam_probe_pull_unpulled` (the
second sharing its exact step count with the plain machine's entry).

**The latch is configuration, not machine state.** `reset()` and
`clear()` drop it, `enable()` starts it fresh, `disable()` throws it
away, the serialization never sees it
(`SeamFidelity.ATriggeredSeamNobodyPulledLeavesTheRunIdentical`,
`SeamFidelity.ALatchIsNotMachineState`; `seam_probe_trigger` /
`seam_probe_trigger_unpulled` in `tests/programs`;
`hosts/web/tests/smoke.mjs`).

**A pull is an input event.** A recording carries it as `pull TICK ID`
in the stream beside the keys, not among the preamble's `seam` lines
(`docs/replay.md` §1).

**Surfaces.**

- Desktop: **Pause/Break** pulls every triggered seam that is on;
  `--pull ID@FRAME` scripts one and works under `--headless`. Pause is
  safe because an 83-key XT board has no such key: `sdl::xt_scancode()`
  answers 0 for it (`keymap_test.cpp`). Rejected: keypad `/` and keypad
  Enter, because they sit inside the movement-key cluster.
- Browser: a **button** beside the seam's checkbox, live while the seam
  is on. `af_machine_seam_pull`, `_triggered`, `_waiting`, `_reached`,
  `_waited`, `_pulled_at` (`abi.h`); `tools/drive.mjs` takes
  `--pull ID@FRAME`.

---

## 4. Qualified points: the resident image and the overlays

PLAN.md §5: the program swaps overlaid code through shared memory, so a
raw address does not identify code. A point is
`{module, offset, handler}`, and the module is one of:

- **`resident_image`**: the program the loader placed, never overlaid.
  The offset is from the image segment (`image_load_segment`); the
  engine adds the load base when it arms.
- **A `seam_module`**: a read the program makes (file leaf name, file
  offset, length, optionally the SHA-256 of the bytes delivered), plus
  `load_segment_at`, below.

The machine keeps an `overlay_tracker` (`machine/overlay.h`) that
watches every INT 21h AH=3Fh through the DOS layer and records file,
file offset, destination, length and digest per read; a later read into
overlapping memory replaces what was there. It is observation: not in
the serialization, reconstructed by a replay. The engine arms an
overlay-qualified point only while the tracker says its module is
resident. Otherwise the seam is **on and inert** with
`seam_reason::module_not_resident` on its status row and a
`seam_event_kind::inert` event, once per transition. A demanded digest
the bytes do not have keeps it inert too. `overlay_schema_version` names
how a module is identified; a mismatch is refused (`schema_mismatch`).

**Where a point lives: ask the program, not the read (#131).** A read
says where a module landed once; the overlay manager may move it inside
its arena or serve a call from a copy it holds, and neither goes through
DOS, so a tracker whose only input is `note_read()` sees neither. Both were observed: a module running from the segment above its
landing nineteen frames later, and the same module 0x73 paragraphs from
its landing after a screen's loading. The manager keeps its own note of
where each module is, in the resident image, and
`seam_module::load_segment_at` is the offset of that word:

    constexpr seam_module end_check{
        .file = "GAME.OVR",
        .file_offset = 38919,
        .length = 4735,
        .digest = "5d07a6b3…",
        .load_segment_at = 0x360};   // the program's own note

With it, the engine **resolves the point from that word at every step**
(segment times sixteen plus the offset); zero means not loaded and the
point does not match.

| | armed at the read's landing | resolved from the program's word |
| --- | --- | --- |
| what `armed` claims | the fact table | the machine |
| a module that moved | fires on somebody else's code, or never | follows |
| a module resident with no read | never arms | arms |
| a module dropped with no read | still armed | inert, that step |
| cost | an address compare | an address compare and a word of RAM |

The read's facts say *which* module (check them against the overlay
file's own table); the word says *where it is now*. A real seam carries
both. An overlaid point with no `load_segment_at` is unfinished.

---

## 5. Identity: fingerprints and editions

A program's identity is its SHA-256 (`sha256.h`, `fingerprint.h`), the
only thing stored about the originals (PLAN.md §2). The host tells the
engine once, after the load:
`seam_engine::loaded(digest, image_segment)` or
`identify(fs, path, image_segment)`;
`af_machine_load_from_vfs` does it for a page.

`machine/edition.h` is the table of editions (fingerprint and name); the
baseline is the currently sold archive release. `find_edition()`
answering null is the **unrecognized path**: the game runs as a plain
machine, the hosts say so, and no seam is available. Availability is
per seam: a seam is unavailable for any binary its `fingerprints` do not
name.

**The player's documents, and the gate (M5-D3, #171).** PLAN.md §2's
other two artifacts, the Adventurer's Journal and the code wheel, are
`machine/document.h`: a fingerprint, a name, and what the document is
for. A definition naming a `gate` adds one condition on arming; an
unsatisfied gate is on and inert with `document_not_presented` (its own
reason, not `module_not_resident`, because a person and not the program
answers it). A shut gate arms no points at all; the test is in the one
function `status()` and `arm_all()` share, beside `modules_resident`.

- Presenting: `--document PATH` on the SDL host and on `drive.mjs`,
  `af_machine_present_document` for a page. The file is hashed and
  dropped; nothing is parsed or kept. Unrecognized is reported, never
  guessed, and the fingerprint comes back either way.
- Presenting is configuration: it survives `reset()`, is not in the
  serialization, and a machine with a document presented and every seam
  off is byte for byte the machine without one.
- A gate outlives the run: `verify_recording` applies a recording's
  preamble seams, so a replay of a gated session with nothing presented
  is refused before a step, naming the condition
  (`a recorded seam is gated on a document that has not been presented`).
  That is why a session descriptor has a
  `document` line, by digest ([`tests/sessions/README.md`](../tests/sessions/README.md)).
  Since #290 no committed session needs one: what a game session states
  instead is `code-wheel-answered`, a condition rather than a file
  (#293).
- **No seam in this build is gated.** `code-wheel` was
  (`.gate = document_kind::code_wheel`) until #290: the releases sold
  today ship a code generator application, not a PDF of the wheel. The
  journal's gate field is deliberately unset (§10). The mechanism stays,
  exercised by `SeamGate.*` in `tests/core/machine/seam_test.cpp` over a
  fixture seam and a synthetic document.

---

## 6. The toggle surface

Three states per seam (`seam_engine::status()`):

| state | meaning |
|---|---|
| `off` | available for this program, not enabled; the default |
| `on` | enabled; `armed` says whether every point is placed, `reason` says why not |
| `unavailable` | not for this program (`wrong_binary`), no program yet (`no_program`), or another schema |

Beside those, `fired`: how many times a handler has **acted** since the
enable. **`armed` is a claim about the fact table; `fired` is a claim
about the machine** (#131). A handler that `decline()`s does not count
(#163); `fired` figures written down before #163 from runs with declines
read lower now (the nine in `tests/sessions/fight-cheat.session` was
re-measured and is still nine, #271).

The desktop host prints a line per enabled seam when a run ends, and a
triggered seam carries `reached` and the reading (§3a):

```
amberfolio: seam code-wheel armed fired=635
amberfolio: seam cheat-kill-all inert fired=0
amberfolio: seam cheat-kill-all armed fired=0 reached=13 - reached, and never pulled; this seam acts only when asked
amberfolio: seam cheat-kill-all armed fired=0 reached=13 waiting - pulled, and its point not reached since
amberfolio: seam cheat-kill-all armed fired=1 reached=13 waited=1868720
```

`host.mjs`'s `formatSeamFired` prints the same line so two hosts' runs
compare as runs and not as spellings.

- Desktop: `--seam ID` enables (repeatable), `--seams` lists every seam
  with its state and exits, the edition line is printed beside the
  fingerprint at load, and every transition is a stderr line
  (`seam code-wheel on`, `seam code-wheel armed`).
- Web: `af_machine_edition`, `af_machine_program_fingerprint`,
  `af_machine_seam_count/_id/_about/_state/_reason/_armed/_fired`,
  `af_machine_seam_enable/_disable` (`abi.h`), wrapped by `host.mjs`; the
  dev page shows a checkbox per seam, unavailable ones disabled with the
  reason. `_fired` crosses as a `double` like every 64-bit count there;
  `_armed` is a predicate. `tools/drive.mjs` prints the end-of-run line.
- The check that makes the count worth having is the pair `probe` /
  `probe-unreached` (`tests/programs`): same program, both arm,
  `probe-unreached`'s point is past the program's exit, and only one
  reports a count
  (`AbiSeams.ASeamArmedWhereTheProgramNeverGoesReportsZero`;
  `hosts/web/tests/smoke.mjs`, which also asserts the same two results
  the native suite does for `seam_probe` and `seam_probe_off`).

Seam state is configuration, not machine state: `machine::reset()`
clears it, the serialization omits it, a replay records the active set
as an initial condition (#100). A trigger's latch is the same; *when* it
was pulled is a stream event (§3a).

**The panel a player works is `docs/hosts.md` §8** (#383): the same five
facts on both hosts — name, state, `fired` as a number, the reason, the
gate — with a checkbox in front of each, and a choice made in it
remembered per player. Nothing there is a second answer to anything on
this page: the panel reads `status()`, toggles through `enable()` and
`disable()`, and prints core's own word for a refusal.

---

## 6a. The enhancements themselves

This file is the mechanism. [`enhancements.md`](enhancements.md) is each
enhancement as a player meets it. Read it before adding one.

---

## 7. The fidelity invariant

PLAN.md §4's boundary, as three testable claims
(`tests/core/machine/seam_test.cpp`; `tests/programs` runs the probe with
its seam on and off on all four targets):

1. **With every seam off, the engine is not consulted.** `machine::step()`
   tests one `bool` (`armed()`). A run's state hash equals the same run's
   on a build with no engine.
2. **A disabled seam's breakpoint is never consulted.** Only enabled seams
   arm points; `disable()` removes them; `dispatch()` is reached only
   when something is armed.
3. **Seam state is not machine state.** `reset()` clears it, the
   serialization omits it, and since #161 that includes a trigger's
   latch.

**On the real program, once per seam (M5-V1, #177).** The session
library carries a pair per seam: `tests/sessions/quiet.rec` is the
baseline and each sibling is the same script with one more seam armed
and never triggered.

| session | relation | to |
| --- | --- | --- |
| `quiet-automap` | identical | `quiet` |
| `quiet-encamp` | identical | `quiet` |
| `quiet-cheats` | identical | `quiet` |
| `quiet-explored` | identical | `quiet` |
| `quiet-journal` | contrast | `quiet` (the `Notes` splice changes the bar the moment it is drawn) |
| `quiet-all` | identical | `quiet-journal` (every seam at once, and no more) |

Both relations are checked on the recordings with no disk
(`scripts/sweep.py`), so CI checks them on every push. CONTRIBUTING.md
makes the pair a condition of merging a seam;
`tests/sessions/README.md`'s "The matrix, by seam" has every pair with
its checkpoint counts, and `tests/sessions/quiet-journal.session` says
why its relation is the weaker one.

---

## 8. Writing a seam

Three decisions first, because each decides what the next step may be:

- **What is its surface?** A *command* a player uses, a *setting* that
  is simply on, or a *cheat* somebody pulls. Rejected for the Encamp
  Fix: a host pull, because a command reachable only by typing at the
  emulator is a console, not a command in the game (#186). The surface
  decides whether the definition is a `trigger`, how many points it has,
  and what its fidelity test can claim (§7, §8.5).
- **Where are its points?** One address, several, or none (§3a). Prefer
  an address: a known instruction in known code is where the structures
  a handler edits are known not to be half edited.
- **What will it refuse?** Write the guard first. Every seam here
  refuses more often than it acts.

### 8.1 Facts, before a line of code

- **The allowed direction.** Addresses, offsets, lengths, digests and
  format descriptions may live here; game code, data, text and byte
  sequences may not (CONTRIBUTING.md). Reading a routine and writing down
  only its address is the allowed direction.
- **What a fact table holds**: the code addresses of its points as
  offsets in their module; the module, *and the word the program keeps
  that module's whereabouts in* (§4); the data-segment offsets it reads
  or writes; the record offsets and what each byte means where that is
  not obvious; for anything it calls, the routine's entry, its `retf N`,
  and its argument frame.
- **Check every fact twice, by two routes.** `seam_cheats.cpp` finds the
  load-segment word by searching the resident image for the manager's
  record with the module's file offset and length and taking the word
  sixteen bytes in; the check is that the search has exactly one match.
  **Prefer a fact derived from the artifact over one inferred from a
  trace** (§10, "How a wrong fact was found").
- **Argument frames**: reverse of source order, segment before offset
  (§3). Confirm on each new routine from its first few argument reads.

### 8.2 What the engine already does

Read §3 first; the answer to "can the engine do this" is yes more often
than it looks (#165). The order to reach for the primitives:

1. **Read the machine.** A handler that remembers nothing cannot
   remember wrongly.
2. **Write what the player's own keys would write**, then post the key.
3. **Call the program's own routine** (#188) for anything the program
   knows how to do: draw text in its font, cast a spell by its rules.
4. **Keep your own words** (#189) only for a comparison against a
   *before* the machine no longer holds.

### 8.3 The order of work

1. The three decisions.
2. Facts, and their checks (§8.1).
3. The guard, then the action.
4. **Unit tests over a machine the test lays out**, every refusal
   included. Lay the state out from the facts, not by calling the seam's
   own helpers.
5. **A `tests/programs` stand-in**, so the handler runs on all four
   targets. For overlay-qualified points the stand-in must be its own
   overlay manager: write its code segment into the word the facts name
   and lay its routines out at the offsets they name.
6. **Drive it on a player's copy**, and expect to find something. Every
   seam here has.
7. **A recorded session pair** (`tests/sessions/`).
8. **The docs**: §10's entry, `docs/playable.md`'s leg, and the open
   issues.

### 8.4 The traps, each of which has cost a day

**Driving the program**

- **The program drains its keyboard after every key it reads**, so a
  handler that posts two keys posts one. One key per arrival (§3; Encamp
  Fix, #172).
- **A pull is one act.** A pulled seam's handler runs once per pull and
  cannot drive a sequence; a seam that ran at every arrival instead would
  be a setting, not a command (§3a; #172, #161).
- **A batch of calls must re-offer its point when it finishes**, or a
  seam driving a sequence acts exactly once: `step()` runs the restored
  instruction the moment the engine returns (§3; #189).

**Guards and points**

- **An address-free guard reads wild memory.** It runs with DS holding
  whatever the program loaded, and a read through the bus above
  conventional RAM is the video window, where a read loads the latches.
  Check the cheap DS-local bytes first, follow no pointer until they
  hold, refuse any read outside conventional memory (Encamp Fix, #172:
  seven `unmapped_memory_read` notices on a driven run).
- **A batch is invisible to every other seam's points.** While a batch
  runs the engine offers no points, so anything a seam paints through
  the program reaches none of the points another seam watches those
  cells with. Two seams sharing a rect cannot learn about each other by
  watching the program; the one that drew has to say so
  (`automap_state::note_panel_painted_over()`; journal reader and
  automap, #332).
- **A handler's own pixels land before the batch it queued.** Port
  surgery and bus writes happen when the handler runs; a call into the
  program happens after it returns. A seam that draws into a box it
  asked the program to draw needs **two arrivals**: queue the frame,
  come back, paint (journal pictures, #328).
- **A point armed where a module's read landed is armed at the wrong
  place** as soon as the manager moves it. Resolve from the program's
  own word (§4; `cheat-kill-all`, #131).
- **A fact can be wrong and still work once.** Both cheats were wrong the
  first time they were driven, one in its frame layout, one in its
  module and its offset (§10; #103, #129, #131).
- **"Measurably different" is not "legible", and only a person can tell
  you which one you built.** The explored overlay's first marking was
  visible on every one of 2,800 measured window cells and still did not
  read (#257, #263). For the next drawing seam: keep every rejected
  candidate with its cost and reason; expect the fidelity claims to move
  with the picture; and **show a person the candidates, not the
  winner**, composited over a real frame.
- **A key taken at a blocking read has to be put back.** A poll can be
  told "no"; a read is the program committed to being handed something.
  Empty a read and the program sleeps inside the BIOS where no point is
  reached, and the next key the player types is delivered unseen. It is
  timing-dependent, so no session catches it. Answer with one keystroke
  the program throws away (`seam_key_read.h`; journal reader #175,
  automap #266) — and with **one**: a handler that has already posted a
  key has already answered, and a second one behind it is the one the
  program acts on (§10, the journal's give-back, #325).
- **A seam that paints where the program paints cannot show what
  arrived without a repaint.** A party that loads a save and stands
  still gives the program nothing to redraw. A drawing seam needs a
  point where the program is idle as well as one where it draws, and a
  signature of what it last drew from because the idle point is reached
  thousands of times a second (explored overlay, #256).
- **A seam that draws where the program draws has to draw where the
  program cleans.** The program's frame puts its title on the box's top
  row, one row above what the camp's teardown blanks, and the title
  outlived the screen. Read which rows a screen's teardown clears off
  the routine first, and hold it with a leg over entry, command and exit
  (Encamp Fix, #298).
- **A routine's address is a segment and an offset, not a flat one.** A
  routine reaches its literals as `CS:<constant>` and its siblings as
  `push cs` plus a near call, so it works only at the paragraph it was
  linked at. Called at the image base with the whole offset in IP, every
  CS-relative read lands sixteen kilobytes away and the output looks
  like a corrupted machine (automap, #173).

**The engine itself**

- **The seam engine must not write anything to make itself work.** The
  power-on image is hashed; an IRET left at the call-return sentinel
  diverged every committed session (#188).
- **A step budget bounds never, not slow.** 250,000 looked generous; the
  cast driver's repaint costs one to two million (#189).

**Reading a driven run**

- IP creeping through a small range is slow progress, not a hang.
- A real keypress not unsticking it means it is not waiting for input.
- `module_not_resident` where you expected the screen means the overlay
  word, not the address.
- A call abandoned every time at the same place with the machine put
  back cleanly is the engine's bound, not the program.

**Tests and recordings**

- **A session pair must put its inputs at the same ticks in both
  halves.** A frame carrying an input is checkpointed whatever the
  cadence, so an extra input in one half is a checkpoint its partner
  lacks and `contrast_of` refuses the pair (#186).
- **Other compilers see what the local one does not.** A missing switch
  case is an error on clang and silent on MSVC. Build the wasm preset
  before pushing (#188).

**Driving the game at all** (`docs/playable.md` has the rest)

- A press that lands while the game is still drawing is drained and
  lost; a six-character party loads slower than a four-character one.
- A live `--press` run needs `--seam code-wheel` **and
  `--code-wheel-answered`**, or it waits at the copy-protection
  challenge for ever (#291).

### 8.5 What a seam owes when it is done

- **Off, the run is byte for byte the run with no engine at all** (§7).
- **The fidelity claim, stated for this seam.** "On and unused hashes
  the same as off" cannot survive a seam visible before it is used; say
  which claim applies. **When a design changes, say which claim it
  costs**, and assert the claim in the direction it now holds rather
  than deleting it (§10, explored overlay, #263).
- **The facts checked against the program**, not only the handler. A
  unit test proves the handler; only a driven run proves the table.
- **What it is not yet**, as filed issues named in the seam's own
  source, and a seam with nothing outstanding says that rather than
  dropping the heading (#272).
- **An entry in `docs/playable.md`**, saying what has been driven and
  what has only been tested.

---

## 9. The review rule

**Nothing outside the engine mutates machine state.** A host reads
machine state and writes it only through a seam; a device answers bus
cycles; a service answers an interrupt. A change that writes the
machine from anywhere but `machine::step()`'s own mechanisms, a device's
bus cycle, a service handler, the loader, or a seam handler is a change
to the fidelity boundary and needs the argument this document would have
to carry.

---

## 10. The seams this build carries

| id | what | qualified by |
|---|---|---|
| `code-wheel` | asks the copy-protection challenge once (#291): unanswered it watches; answered by a person, it steps over the boot's call and the challenge is never drawn again | the resident image |
| `encamp-fix` | a `FIX` command on the camp bar: spends the cures the party holds, rests off the deficit, reports in a box the game draws | the camp screen's overlay |
| `automap` | a map of where the party has been, over the roster, on **Tab** | the resident image |
| `journal` | what the game cites goes on a list; **Notes** on the party's own bar opens it on the game's screen, out of the player's ingested journal | the resident image, and the adventuring loop's module |
| `explored` | fog of war on the overworld map: a black checker over every square the party has not stood on; a setting, no key | the resident image |
| `cheat-invulnerable` | the party takes no damage | the resident image |
| `cheat-kill-all` | every enemy takes 120 damage, **when pulled** (§3a) | the end check's overlay |
| `cheat-wound-party` | the whole party drops to one hit point, **when pulled at camp** (§3a) | the resident image |

All are keyed to the baseline edition (§5).

### The code-wheel bypass (#94, #119, #290, #291)

PLAN.md §5 item 1. It never answers the challenge for anybody.

| point | module | what the handler does |
| --- | --- | --- |
| the `REPE CMPSB` of the program's general string compare, image `0xBBB0` | resident image | qualified by the operand, not the address: ES:DI must point inside the word table (`0xC7C2`, thirteen entries, stride 21, six characters each, computed from `image_base()`), because the routine is called about 150 times during the boot. Works out what the comparison will conclude; on a correct answer latches and calls `code_wheel_answered` |
| the boot tail's five-byte far call at `0x0122` into overlay stub `0x1c:0x0025` | resident image | when latched, checks both operand words (the stub's offset and the paragraph the relocation left) and steps over the call; otherwise `decline(point_not_recognized)` |

- **Why stepping over is honest**: the routine sets no persistent state
  and returns on success. The program's own documented boot word skips
  the same call; rejected as the mechanism because it also skips the
  title sequence and enables the debug keys.
- **Host services**: `code_wheel_answered`.
- **State**: the engine's latch, configuration (survives `reset()`, not
  serialized). Between runs, `host/code_wheel_store.h` (M6-C1b, #292):
  one digest per copy, in a file in the desktop host's per-user data
  directory and a key in the browser's storage; never the question, the
  answer or a time. `apply_code_wheel_store()` tells the next machine
  before its first instruction, on both hosts.
- **Keys**: none.
- **Fidelity**: on and unanswered it cannot move the machine
  (`SeamCodeWheel.WatchingCannotMoveTheMachine`), and
  `tests/sessions/boot-wheel.rec` is that claim as a recording: the seam
  on, the challenge unanswered, 144 firings over the boot, and every one
  of the 72 checkpoints equal to `boot.rec`'s, which has no engine at
  all (#293).
- **Rejected**: a document gate on the wheel PDF (#115, #171), because
  the releases sold today ship a code generator application (#290).
- **Open**: `cite.rec` is the one session still on the boot that asked
  (#293); nobody has typed a correct answer into the real program
  (`docs/hosts.md` §3).

### The Encamp (F)ix (#172, #186, #189, #194, #298, #303, #304)

PLAN.md §5 item 4, the one enhancement that automates play: the game's
own routines do the work, native code only asks. Nothing in it shortens a
rest, heals a character, memorizes a spell or suppresses an encounter.

**Points**, all in the camp screen's overlay, resolved through the
program's load-segment word (§4):

| point | what the handler does |
| --- | --- |
| before the camp bar is handed to the menu-bar routine | blanks the prompt the loop builds on its stack (the four characters need its columns), splices ` Fix` before the bar's last separator; on the pass after a command finished, draws the report |
| where the menu-bar routine returns | splices the bar back out; `restore_the_highlight()` is not here (see exit). If the routine says a command was chosen and the letter is the seam's, does **one** thing: casts a ready cure if there is one and somebody to spend it on, or else writes the days field of the rest clock, claims the rest in a scratch word, and posts the camp bar's Rest key |
| the rest command's entry | if the scratch word says this seam claimed the rest, posts the rest screen's Rest key |
| the camp loop's exit, `camp_menu_exit` (M5-E1c, #194) | out-parameter non-zero (the rest orchestrator's "a wandering-monster check fired"): draws `Fix: Interrupted!` there, held by the program's own message delay; any other exit drops a report still owed. Either way maps the highlight byte back |

Facts it relies on:

- The bar is a Pascal `string[40]` in the data segment; the splice
  refuses a bar without room. An unrecognised letter is harmless: the
  loop is a sequence of comparisons, not a table index.
- **Days** = the worst survivor's deficit plus one (the heal counter is
  zeroed on camp entry and not by a rest, so a second rest starts mid
  day); **zero when there is no deficit**, which leaves the program's
  own duration. With nothing to rest for the command declines. **An
  empty spellbook adds to neither half** (#350): nothing is cast, and
  the program's wrapper computes no memorization time, so the rest is
  the deficit plus one over `00:00` — which for a party a fight left
  low is days, and days of camp are what the area's wandering-monster
  check is rolled against.
- **Cures**: only Cure Light Wounds a member holds **ready** is cast;
  one is queued back **before** each cast by the memorize command's own
  two writes (the slot gets `id | 0x80`, then the program's slot sort),
  so the party is never a cure down. The cast goes through the driver
  the camp's own Cast command calls: the seam positions its target
  anchor and answers its one prompt with one key. One act per arrival;
  the batch re-offers the point (§3) and the next arrival decides
  afresh. Any key the program has not read yet stops the run.
- **Rejected**: memorizing cures into empty slots, because the seam
  would owe the player their loadout back; forgetting non-cure spells,
  because the game has no by-hand forget; a per-member table, because the
  roster is on screen behind the box (PLAN.md §5).
- **The clock** is a seven-slot counter in the area record at
  `0x018C + 2n`, carry-normalized against caps at data-segment `0x363A`
  (`10, 10, 6, 24, 30, 12, 256`): minute units, minute tens, hours,
  days, months, years; the top slot's overflow adds one to each member's
  `+0x30` word, the Age on the character sheet (#269). The summary prints
  `DD:HH:MM` for a rest of a day or more. The seam reads the caps and
  `max_rest_days` from the program.
- **The outcome is told by the out-parameter, never read off the
  roster**: after the cures the party is whole at the exit whichever way
  the rest ended.
- Wound statuses unconscious and dying are inside the cure applier's
  gate, so a member a fight left down is mended, not listed; the
  exception list is reached by dead, stoned and gone only (#269).

**The report**, drawn by the program's frame (`0x041F8`) and string
drawer (`0x076B6`) in one batch:

```
  row 0x11   blank; the frame is handed no title (M5-E1e, #298)
  row 0x12   the title, centred as (left + right - length) / 2
  row 0x13   the summary: hit points restored, cures spent, the time
  0x14..     only the members it could not put right, one line each
  last row   the pending-cures warning, when there is one
```

- Seven titles: healed, rest stopped, stopped by the player, no cure
  memorized, nobody knows a cure, cannot cast here, `Interrupted!`. **A
  player meets four of them** (#350): a wounded party is rested before
  anything is concluded about it, so the three middle titles are reached
  only where something took the program out of the command mid-run —
  `outcome_without_a_rest()` is otherwise asked only about a party that
  is whole, and a party that is whole is healed. A
  member resting cannot mend is named by a pointer into the program's
  own status table. The list truncates to `...and N more.`
- The box is drawn before the bar goes out, so the live bar under it is
  the way out; nothing says "press any key".
- **The title row** (#298): the camp teardown clears rows `0x12..0x16`,
  columns `1..0x26` (`status_clear_region`, image `0x2383`) and row
  `0x18`, never `0x11`, and EXIT is the one way out the caller does not
  repaint after. Rejected: undoing the title at exit, because it needs
  the seam to remember it drew, and an unconditional clear would touch
  the `identical` run. Cost: one body row. Test:
  `TitleLandsOnARowTheCampTeardownClears`.
- **The knot** (M5-E1f, #303): the frame's top edge lands on the panel's
  border row `0x10`, and at column `0x10` that row carries the corner of
  the viewport box (`1,1` to `0x0F,0x0F`; pixels `128..135` by
  `128..135`), which the program borders *after* the panel. The report calls `frame_draw_border` (image
  `0x3F10`) on that box after its own frame, in the same batch. Ten
  calls in the worst case, eleven on the way out of camp, inside the
  engine's twelve. Test: `PutsTheViewportsCornerKnotBackAfterTheFrame`.
- **The highlight** (M5-E1g, #304): `0x6B2B` in the resident image
  (data segment `0CDC`) is the one-based group index every bar shares;
  the menu-bar routine reads it on entry, resets it to 1 when it names a
  group the bar lacks, the cursor keys step it, and a chosen command sets
  it to its group. The spliced bar (`SAVE VIEW MAGIC REST ALTER EXIT`
  plus `Fix` before the last group) has one group more, so EXIT left a 7
  where the seam-off run left a 6 and the adventuring bar reset it.
  `restore_the_highlight()` at the exit point steps a position at or
  past the Fix's group down by one, leaves one before it alone, and
  leaves a value past the bar's count alone. It runs only where the
  splice ran (mode says camp and the bar is the shape `splice_in()`
  accepts, re-read at the exit). It runs once per exit: the
  interrupted exit re-offers the point after the report's batch, and a
  mapping that ran twice fails
  `HandsItBackOnTheWayOutOfAnInterruptedCampToo`. Watch it with
  `--watch 6B2B`. What it cannot restore is a cursor stepped across the
  Fix; that is the enhancement being visible.

**State**: scratch words (§3): the rest claim (sequence position, a noted
debt against a future mid-sequence restore), and the party's hit points,
the clock and the days as the command began, for the summary.
**Host services**: none. **Keys**: none claimed; its letter is a bar
command.

**Fidelity**: off, no engine; on and never chosen, the difference is on
the screen only, and between one menu draw and the next no byte of the
program's memory differs. `tests/core/machine/seam_encamp_test.cpp`
drives the handlers over a camp the test lays out with a bar of three
invented words; `encamp_fix*` and `encamp_fix_interrupted` in
`tests/programs` drive the same handlers on all four targets, two of
them sharing an exact step count on and off. Sessions:
`tests/sessions/camp-fix.rec` contrast `camp.rec` (one keystroke apart;
both halves pull `cheat-wound-party`), `quiet-encamp` identical `quiet`. Leg:
`tests/visual/camp-fix-exit.leg` (entry, the Fix, EXIT; driven by hand on
the shorter boot; allows the knot at frame 9,650 and the bar mid-draw at
10,300, and requires the bar row identical from 10,325).

**Traps specific to it**: the guard reads nothing outside conventional
memory (§8.4); the report is drawn on the interrupted exit because the
pass of the menu it would otherwise wait for never comes; the batch
budget (§3).

### The automap panel (#173, M5-E2a to M5-E2e)

PLAN.md §5 item 3 (`seam_automap.cpp`), the first seam that draws: a
sixteen-by-sixteen map at seven pixels a cell over the party roster, the
region the AREA view uses; Tab shows it and takes it away. The proven design's rect, reveal
rule, settling quarantine and ownership signals are derived in
`core/include/amberfolio/machine/automap.h`.

| point | module | what the handler does |
| --- | --- | --- |
| the program's "is a key waiting" routine | resident image | claims Tab and the two roster-cursor keys while the panel is up; is the tick: samples the party, reveals, draws the panel if anything changed |
| the program's "give me the next key" routine | resident image | claims the same keys, answering the read with `-` (#266, `seam_key_read.h`) |
| the box-region clear | resident image | if the rect meets the panel, something else has the cells |
| the full-screen clear | resident image | the same, unconditionally |
| the party-roster draw's `retf` | resident image | the cells are the panel's again (at the return, because the drawer clears its rows through the box clear above) |
| the menu-bar routine's thunk | resident image | which bar is going up: the party's bar is a far pointer into the data segment at one of two offsets, every other bar is a stack copy. While it is not the party's bar the panel comes down (if really up) and Tab is not this seam's (M5-E2d) |

- **The key is taken out of the BIOS ring at 40:1Eh** before the
  program's routine looks, head only, whole keystroke word matched
  (Ctrl-I is the program's), vetoed while the program's extended-key
  pushback slot is armed. Rejected: register surgery inside INT 16h.
- **Drawing**: a private buffer of palette indices blitted a plane at a
  time through the map mask (§3); the rect starts on a byte boundary and
  is twenty-two bytes wide, so nothing is read back.
- **Colours** (M5-E2a): a wall is the modal non-black pixel of the 8x8
  tiles the 3D renderer blits for its largest shape; black skipped;
  all-black or an unloaded wall set falls back to the area's frame
  colour; cached per map and per loaded tile set. Rejected: sampling the
  rendered ground band, because it latched whatever covered the viewport.
- **Doors** (M5-E2a): a leaf where the face nibble indexes a graphic
  seen shut, from this map's own scan at arrival or a table of every
  shut face in the shipped data keyed (disk, WALLDEF block, row).
  Twenty-one of the twenty-nine grids carry a shut face; New Phlan does
  not and falls back to "passable face is a door". The evidence path is
  driven in `docs/playable.md` leg 12 (#268). `--trace` prints a tally
  per draw:
  `amberfolio: automap doors frame=011000 disk=8 area=0D geo=0D seen=0200 table=0200 drawn shut=0 kind-seen=1 kind-table=0 no-evidence=0`.
  Rejected: runtime learning across maps, because the seam refuses every
  binary the table was not derived from.
- **Zone label** (M5-E2b): the band eight columns right of the cells, in
  the program's 8x8 font (a far pointer in the data segment, sixty-four
  glyphs indexed by the character upper-cased modulo 64) rasterized into
  the buffer, from a table of (disk, area) to a label; no row prints
  `AREA <n>`; `|` in a name is a soft break for a word over eight
  columns. A zero font pointer draws nothing and the font's segment is
  in the drawing signature.
- **Give-back**: one batch of two calls, the region clear over the panel
  rect then the roster drawer with the current member. Rejected: the
  per-mode screen composer (`screen_redraw()` in the proven design),
  because it repaints the viewport over a vendor mid-question (M5-E2d).
  The routines must be called at the paragraph they were linked at
  (§8.4).
- Rejected as the "whose screen" gate: the program's "a script has the
  message area" byte, because `--watch 84E4` shows it oscillating every
  step.
- Exploration is never gated on whose bar is up.

**State**: `machine::automap()` (a map's fog is thirty-two bytes;
`scratch_words` is sixteen), observation beside the overlay tracker:
dropped by `reset()`, absent from the serialization, rebuilt by a replay.
The colour cache and the door tally live there too. **Host services**:
`automap_update`, called with the store's serial when a reveal changes
something. The store is the host's (`hosts/common/.../slot_store.h`,
shape decided in `machine/automap.h` because the explored overlay reads
the same records, so the wilderness is in the same file): off unless
asked (`--save-sidecars`, `af_web_save_sidecars`) because a sidecar
changes the disk every session pins, and a launch with a person in it is
asked for that permission once (#385, `docs/hosts.md` §2b); a header-only
sidecar replaces one that is there and is never written as a new file; a
working table follows the party
and a snapshot per save slot replaces it on load, even when empty; a slot
the load menu only opened is told from one loaded by whether bytes moved
through the handle (`file_event`'s traffic flags).

**Keys**: Tab; up and down (the roster cursor) while the panel is up.

**Fidelity**: on and Tab never pressed, byte for byte the seam-off run
(`AutomapFidelity.OnAndNeverAskedLeavesTheRunIdentical`;
`automap_probe_untouched` shares its exact step count with
`automap_probe_quiet`); pressed, the program's input is what it would
have been (`automap_probe_tab_claimed` polls as often as
`automap_probe_quiet` and is handed nothing; `automap_probe_tab_seen`
with the seam off is handed the Tab), exact at the poll and one
character wide of exact at the read. Sessions: `walk-map` contrast
`walk`, `quiet-automap` identical `quiet`, `subset-map-reader` (the panel
under the reader). Legs: `tests/visual/rdr-map-back.leg` (the map back
after the reader's full screen, #332).

**Traps specific to it**: CS-relative calls; the blocking read; the
reader's give-back paints over it inside a batch, so the reader calls
`automap_state::note_panel_painted_over()` and the map redraws at its
next arrival with its open flag untouched (#332).

### The journal reader (#175, #221, #222, #232, #305, #328, #346)

PLAN.md §5 item 2's in-game half; ingestion is #174 and
`docs/journal.md`, and §9 there is the door between the halves.

| point | module | what the handler does |
| --- | --- | --- |
| the automap's five: both key routines, both clears, the roster drawer's `retf` | resident image | as the automap's: the screen is drawn and repainted here, and the reader's modal keys are claimed here |
| the program's word-wrapping **message box**, where a script's every PRINT ends | resident image | the citation watch: reads the Pascal string (offset at SP+4, segment at SP+6) into a rolling window; the box's home-and-clear flag is the message boundary |
| the adventuring loop's call into the menu-bar routine, one pair per view mode (M5-E4a, #221) | the adventuring loop's module | before: splices `Notes` onto the party's bar, sets `bar_live()`; at the return: splices it out, clears `bar_live()`, reads the letter from `AL` and the routine's out-parameter, puts the highlight byte back where the routine found it (#330) |

Rejected as the watch: `image_draw_string`, the string drawer the Encamp
Fix calls, because on this program it draws credits, menus and the
position line and no narration (#232).

**The citation is a shape**: the word a numbered section is called by
(entry, tale, proclamation, and their plurals) and a number in that
section's notation (decimal; Roman for proclamations), a plural
followed by a list joined by commas and "and". The window is normalized
to upper case and single spaces with commas kept, emptied at the message
boundary and on a match; a list that runs off the end of what has been
printed waits for the rest. Every entry named goes on the log in order,
unread, and **none of them opens** (#346): the program's own narration is
what tells a player a note arrived. The word "journal" is not part of the
shape.
`journal_citations_in()` is the pattern as a free function;
`JournalCitation.*` tests it.

**Where it draws**:

- **The full screen** (M5-E4b #222, M5-E4d #305, #346): the log and every
  page of an entry, drawn by the program's frame and string drawers as a
  box of twenty rows of thirty-eight characters, painted over several
  arrivals inside the batch limits. The interior is cleared and the frame
  redrawn on every page. There is no second size: the panel page a
  citation used to open went with the citation's page. Allowed exactly
  while `journal_state::bar_live()` holds, the one state in which the
  composer may be asked to put a screen back — and **that needs no rule
  of its own**, because `Notes` is a command on the party's own bar and
  is the only way in there is. What follows is that the reader's whole
  reach is the log: a citation puts a row there, `Return` opens a row,
  and there is no path to an entry the game has not named. The reader is
  modal over the map, which is the same pixels either way.
- **Give-back**: one, since #346 — the routine the program uses on the
  way out of every full-screen view (the scaffold, view, roster and status line) plus one injected
  space so the menu-bar routine returns and redraws the bar. **At the
  blocking read that space is the read's answer and nothing is posted
  behind it** (#325): the program's read routine drains its buffer after
  the key it takes and acts on the last one, so the ignorable key behind
  the space threw the space away, and the bar stayed the journal's on
  exactly the presses that landed at the read rather than the poll —
  `E`@9900 on the shorter boot at slot C, where `E`@9600 landed at the
  poll and recovered it. Rejected:
  the routine that *enters* the adventuring screen, because it sets the
  mode byte and draws the bottom panel alone. A page from a listing row
  returns to the listing and gives nothing back. The give-back calls
  `automap_state::note_panel_painted_over()` (#332).
- **The bar** (#329, #330, #341, #342): flush left, spaced one, drawn in
  a call for the row (green) and one more for each word's initial
  (white). `NEXT` and `PREV` are only on it when there is a screenful in
  that direction; a dropped word closes up rather than leaving a gap, so
  an empty log or a one-page entry draws `EXIT` alone. One helper decides
  the set for a screenful and both drawing passes read it, so the words
  on the row and the initials over them cannot disagree about what is on
  it.
- **Notes** (#221): the two bars are thirty-three and twenty-seven
  characters in `string[40]` slots at the two data-segment offsets the
  automap tests; the menu-bar routine's command class is upper case only
  and the bars are mixed case, so `N` selects nothing; `Notes` is one
  capital, because the scan's *last* match wins and an all-caps word
  would steal the fourth command. The highlight is put back exactly, not
  stepped, because `Notes` is appended and `N` matches none of the
  program's commands. `tests/visual/rdr-bar.leg` is the measurement:
  without the give-back, 190 pixels differ between the frame before the
  journal opened and the frame after it closed, all on the bar row.
- **Art** (#328): an entry's picture is the page after the caption,
  painted whole by plane surgery in the arrival *after* the frame's batch
  (§8.4). `docs/journal.md` §11.
- The prompt's cursor is a rule in the seam's own pixels, because an
  underscore hits a font index nothing else uses. The log's timestamp is
  the machine's seeded wall clock.

**Host services**: `journal_open` (four answers, "no journal has been
read" among them, delivered through `deliver()` / `refuse()` into
`machine::journal()`), `journal_seen`, `journal_art`. The store is host
configuration, restored by `host::restore_journal_log()`. Both hosts
print the callout at the end of a run
(`host-service journal-open calls=1 last=4`).

**Keys**: **none at all while the reader is down** (#346). There was one,
F1, claimed on every screen with a party roster and defended on the
grounds that a function key has no character (`keyboard.h`) and so cannot
be a command on any of this program's bars; the argument held and the key
went anyway, because `Notes` is the way in and a second one is a second
thing to learn. The number prompt it opened (#218) went with it, and
with the prompt went naming an entry: what the reader can open is what
the log holds. While the reader *is* up: Escape closes, Backspace goes a screenful back,
Return opens the row the cursor is on, and `N`/`P`/`E` are the bar's own
three. The log and a page take **every** key (the bar is live underneath,
and a key let through walked the party unseen, #230). Reads are answered
with `-`.

**State**: `journal_state` (`bar_live()`, the window, the page);
`machine::journal()` for the text. **Gate**: deliberately unset, though
`known_documents()` has a journal row (#214); rejected because it would
refuse a player whose store was copied from another machine, and the
reader already says when the host has no text.

**Fidelity**: on and the reader never opened, the run is identical until
the party's bar is first drawn — a citation included, since #346 leaves
it writing a log line and drawing nothing. `quiet-journal` is therefore
contrast `quiet`, the one seam that cannot claim `identical` (§7), and
the splice is the whole of what it is contrasting on. Exercised sessions:
`reader`, `notes`, `cite` (a real citation, no key pressed),
`subset-map-reader`. There is no contrast pair, because the store is a
host file and not in the stream. Legs:
`tests/visual/not-bars.leg`, `not-log-giveback.leg`, `not-log-modal.leg`,
`not-page-back.leg`, `not-page-modal.leg`, `rdr-prompt.leg`,
`rdr-page.leg`, `rdr-bar.leg`, `rdr-art.leg`, `rdr-map-back.leg`.

**Traps specific to it**: the watch address and the shape (#232); the
blocking read (#266); the batch ordering for pictures (#328); the
give-back inside a batch (#332); the composer under a vendor's bar
(M5-E2d); the one keystroke a read is answered with (#325).

### The explored overlay (#179, M5-E5a to M5-E5g)

PLAN.md §5 item 5 (`seam_explored.cpp`), the one enhancement with no
proven prior design.
[`docs/explored-overlay.md`](explored-overlay.md) holds the fact table
with a second route per line, the geometry, the recipe, and every
marking and covering candidate with its reason (§5 there). This is the
fact table.

| point | module | what the handler does |
| --- | --- | --- |
| the return of the back-buffer present | resident image | the program has just put the screen up; the fog goes back on |
| the program's "is a key waiting" routine | resident image | records the party's square; draws when the position or the store's serial has moved |
| the menu-bar routine's thunk | resident image | whose screen this is; shared with the automap through `automap_overland.h` |

| fact | value |
| --- | --- |
| the screen | mode byte 3, view kind 2, 3 or 4 (one per wilderness area) |
| the party's position | two words in the **area record**, not the data-segment bytes every other screen uses (those held `0x0B` and `0x0D` while the status line printed `3, 32`) |
| an area | 16 columns by 36 rows; the three are bands of one 44-column terrain table at biases the program keeps per view kind, read from the program |
| the window | 120 by 120 pixels at (8, 8); a square 24 by 24, three whole bytes wide; one move repaints exactly `8,8,127,127` |
| the fog | a one-pixel checker of palette index 0 on half of a square's pixels; colour from set/reset, bit mask alternating `0xAA` / `0x55` by scanline; the parity is the screen's, so it runs unbroken between squares; a masked write, so each byte is read then written (§3) |
| `explored_reveal_radius` | Chebyshev **0** (`machine/automap.h`) |
| the record | where the party stood; the reveal is derived at draw time, so the radius can change without touching a sidecar |
| never covered | the party's own square; any pixel outside the window |
| covered | a neighbouring area's squares in an overhanging window |

- **Rejected** (each with its reason in `docs/explored-overlay.md` §5):
  radius 2 or 3, because the window is five across and the fog covers
  nothing; radius 1, because a walk uncovers a corridor three wide; a
  one-shade lift of walked squares, because it did not read (#263);
  solid black, because it hides the shape of the country; a dark-grey
  checker, because it reads thin and is the colour of mountain rock
  (index 8) (#299); light grey, because it reads as paler terrain; a
  two-by-two checker, because it reads as a grid; a one-in-four dither;
  dropping the intensity plane; painting into the program's back buffer,
  because the program reads it back.
- The present's return is painted because at its entry the flush has not
  happened; the poll is painted too because a loaded save under a
  standing party gives no present (#256, §8.4).

**State**: the automap's store (`automap_update`), so a player with only
one of the two keeps the trail. **Host services**: `automap_update`.
**Keys**: none.

**Fidelity**: on and the overworld never shown, identical
(`ExploredFidelity.OnAndTheOverworldNeverShownLeavesTheRunIdentical`;
`quiet-explored` identical `quiet`). The arrival on the map is **not**
the seam-off screen, asserted in that direction so nothing re-acquires
the old claim (`ExploredFidelity.TheArrivalIsNoLongerTheScreenItWouldHaveBeen`,
#263); `wild-trail` contrast `wild` diverges at the arrival. Legs:
`tests/visual/exp-trail.leg` (slot J, eight steps north, the fogged
squares named), `exp-steady.leg` (no flicker across the icon's
animation).

### The debug cheats (#99, #161, #163, #196)

Three seams, not one with three switches (PLAN.md §5's toggle per seam);
the plan names two and #196 added the third as test tooling for the
Encamp Fix's arithmetic.

| seam | point | module | what the handler does |
| --- | --- | --- | --- |
| `cheat-invulnerable` | the entry of the one resident routine every kind of damage lands in | resident image | frame: far return, then the **damage**, then the record. If the record's combat-side byte says party, writes zero over the damage word; the routine then runs on zero |
| `cheat-kill-all` (trigger) | a point with no address (`at_every_step`) | the end check's overlay | while the pull is outstanding, acts at the first step where `combat_roster_ready()` holds |
| `cheat-kill-all` | the once-a-round end check | the end check's overlay, resolved through `load_segment_at` | the fallback if the guard never holds: declines once (`inert point_not_recognized`), keeps the pull, and serves it at the end of the round |
| `cheat-wound-party` (trigger) | a point with no address | resident image | at camp (mode byte 2, the one screen where nothing is mid-edit and DS is known), hands the damage routine's own write `hit points - 1` for every member it would accept |

**Kill-all** deals `debug_damage`, 120, to every standing enemy the way
the program's routine deals it: a survivor keeps the remainder and stays
in its side's count; one finished is downed the routine's way (slain,
held byte cleared, hit points zero, side count decremented, scratch byte
cleared); the program's end check then ends the fight. 120 is a chosen
value, not a fact; subtracted saturating; the hit-point field is read
and written as a byte, so a wrong width means less damage, never a heal.
`combat_roster_ready()` checks: mode byte reads combat; roster head is a
far pointer outside segment 0; every link is too and the list ends
within the bound; a party member and an enemy are both standing; both
sides' body counts are non-zero. It cannot rule out the program being
mid-walk with a pointer in a register; survivable because damage is a
state the program produces itself and the end check rebuilds the counts
from the held bytes. **Standing is the wound status** (unhurt, or an
animated body), never the held byte beside the side index, which a slain
combatant can still have set
(`SeamCheatKillAll.ReadsTheStatusAndNotTheHeldByteNextToIt`). Side 0 is
the party's. Instrument: `--watch 6814:2`, both sides' body counts, a
byte each.

**Wound-party** writes only to a record the routine would write to
(unhurt or animated body; any other status the routine would down), skips
a member at one hit point or fewer, and keeps a pull made anywhere but
camp until ENCAMP is pressed. Pulled rather than left on, because a
wounding left on is a curse.

**Invulnerable**'s guard (`spare_the_party`): the record is a far
pointer and nothing lives in segment 0; otherwise
`decline(point_not_recognized)`. It caught the
frame being read the wrong way round (#103).

**Host services**: none. **State**: none but the latch. **Keys**: none.

**Fidelity**: `tests/sessions/fight-cheat.rec` contrast `fight.rec`
(`cheat-invulnerable` fires nine times), `quiet-cheats` identical
`quiet` (all three on, none pulled); `camp-fix` pulls `cheat-wound-party` in both halves. Tests:
`tests/core/machine/seam_cheats_test.cpp`, and
`SeamCheatKillAll.ServesThePullWhereverTheProgramIs`,
`DealsDamageAndLeavesAnEnemyItCannotFinishStanding`,
`KeepsThePullUntilThereIsAFightItRecognizes`,
`DeclinesARosterThatIsNotAList`, `IsNotConsultedAtAllUntilSomebodyPullsIt`,
`IsOnAndDoesNothingUntilSomebodyPullsIt`, `OnePullIsOneFiring`. Legs:
none. Driving a fight needs `--seam code-wheel --code-wheel-answered`
and `--pull cheat-kill-all@FRAME`.

The cheap check for any new seam, one extra run:

```sh
# same script, same disk, once with and once without; a trigger needs
# --seam (it may act) and --pull (somebody asked)
diff <(amberfolio ... )      <(amberfolio ... --seam cheat-kill-all --pull cheat-kill-all@600)
```

Same step count and framebuffer means the seam did nothing.

**Traps specific to them**: the frame layout (#103); the end check
executing from an address no read covered because the manager moved it
(#131, §4); the held byte; and that
`tests/core/machine/seam_cheats_test.cpp` passed for a month against
facts that were wrong: **a unit suite checks that the handler does what
the fact table says, never that the fact table is true.**

### How a wrong fact was found, in case the next one has to be

1. **Pick something the routine must touch**: the byte a party member's
   hit points live at, the two bytes the side counts live at.
2. **Watch it.** A temporary print in `machine::write_memory` /
   `read_memory` for that physical address, with CS:IP.
3. **Walk back to the entry.** Turn the trace ring on and dump it at the
   access (`format_trace_report`); the transfer into the routine is
   where the point belongs.
4. **Ask which module that address was in, carefully.** A print in
   `overlay_tracker::note_read` gives every load's file offset, length,
   landing range and digest. **This step lies**: overlays share an arena,
   landing ranges overlap over a run, and a moved module leaves no record,
   so "the last load covering this address" gave a plausible module and
   offset and a seam that worked once. The check is the artifact: a
   module's file offset and length either match a row of the overlay
   file's own table or step 4 is wrong.

None of it reproduces a byte of the program.
