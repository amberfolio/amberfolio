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
| `group` | seams that are **alternatives** share one: enabling one disables the others (§6); the text faces |
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
- **A batch offers no points to any seam while it runs** (§8.4), but
  one that says `seam_point::inside_calls`: an address point in a module
  that stays put, on a seam that is not a trigger, whose facts hold
  whoever called the code it is in. Its handler edits registers only;
  `call_program()` and `place_bytes()` answer false there. The font
  seams are the one user (`SeamCallProgram.OffersAnInsideCallsPoint…`).
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
baseline is the START.EXE every address in the seam tables is a fact
about. `find_edition()` answering null is the **unrecognized path**: the
game runs as a plain machine, the hosts say so, and no seam is
available. Availability is per seam: a seam is unavailable for any
binary its `fingerprints` do not name.

**One program image, two releases, every seam valid on both** (#396).
The release sold on GOG and Steam and the third-party repack the facts
were gathered on boot the same START.EXE; their requirement rows are
`por-store` and `por-archive` (`docs/hosts.md` §5). Every file the two
share is byte-identical but GAME.OVR, and GAME.OVR differs in two bytes,
at file offsets 3288–3289, inside overlay 2 (code at file offset 2476,
862 bytes), the copy-protection overlay. None of the three modules a
seam pins by digest (§4) is overlay 2, and the one seam on that path is
`code-wheel`, whose points are both in the resident image. Observed on a
copy of the GOG install, driven headlessly beside the repack with the
same keys:

- **All seams off**, the challenge is drawn on the store copy exactly as
  on the repack, and **a wrong answer is accepted**: the program goes on
  to its main menu. The repack refuses the same answer and asks again.
  That is the store release's own behaviour, the plain machine's; the
  same copy with the repack's GAME.OVR in it refuses.
- **`--seam code-wheel --code-wheel-answered`**: the boot steps over the
  challenge on both, with pixel-identical stills over the whole run and
  the same 144 firings.
- **`--seam code-wheel`, unanswered**, with a wrong answer: the program's
  compare still runs (145 firings, the one more being the challenge's),
  the seam latches nothing, and the store copy goes on to the main menu
  while the repack asks again. The seam's conclusion is the resident
  compare's, which is the same code in both, so it latches exactly the
  answers it latches on the repack.

So no seam is withheld from the store copy, and `fingerprints` stays
keyed on START.EXE alone.

**The player's documents, and the gate (M5-D3, #171).** PLAN.md §2's
other two artifacts, the Adventurer's Journal and the code wheel, are
`machine/document.h`: a fingerprint, a name, and what the document is
for. A definition naming a `gate` adds one condition on arming; an
unsatisfied gate is on and inert with `document_not_presented` (its own
reason, not `module_not_resident`, because a person and not the program
answers it). A shut gate arms no points at all; the test is in the one
function `status()` and `arm_all()` share, beside `modules_resident`.

- Presenting: `--document PATH` or a file dropped on the window on the
  SDL host, the *show a document you hold* input on the page,
  `--document PATH` on `drive.mjs`; `af_machine_present_document`
  underneath all four. The file is hashed and dropped; nothing is parsed
  or kept. Unrecognized is reported, never guessed, and the fingerprint
  comes back either way — `docs/hosts.md` §9 is the control and the two
  sentences both hosts say.
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

**Alternatives.** Seams that share a `group` are one choice: `enable()`
disables the others in it first, each with its own `disabled` event,
so the panel, a config file and a replay's preamble all end with at most
one of them on (`SeamGroup.*`). Both panels redraw every row after a
toggle for that reason.

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
| `quiet-font-sans` | contrast | `quiet` (a face is seen from the first text drawn; `devices` and `display` only) |
| `quiet-font-chisel` | contrast | `quiet` (the same) |
| `quiet-all` | identical | `quiet-journal` (every seam but the faces and `hero-keys`, which are contrasts of their own) |
| `quiet-all-hero-keys` | contrast | `quiet-all` (the same with `hero-keys` on: the party list is the first thing to move, at the tick `quiet-hero-keys` moves it) |
| `list-keys-arrows` | identical | `list-keys` (the arrows' seam, on a creation script of Home and End) |
| `list-down-arrows` | contrast | `list-down` (the same script with Down and Up, which the seam-off program drops) |
| `camp-roster-arrows` | identical | `camp-roster` (End, End and Home at the camp bar, which the seam never touches) |
| `camp-down-arrows` | contrast | `camp-down` (Down and Up at the camp bar, which the program hands to the party cursor to put the first member back) |
| `camp-pad-arrows` | contrast | `camp-pad` (the keypad's 2 and 8 with Num Lock on at the camp bar, which the routine translates into letters the cursor puts the first member back on) |
| `quiet-bar-keys` | identical | `quiet` (the party's own Right at the adventuring bar, which keeps its arrows) |
| `bar-enter-keys` | contrast | `bar-enter` (Return at the camp bar takes the highlighted command) |
| `bar-yn-keys` | contrast | `bar-yn` (a Right at a bar that is not raw, and Return at the Yes/No prompt) |
| `bar-camp-keys` | contrast | `bar-camp` (a Right at the camp bar, which hands it to the party cursor, and Return) |
| `quiet-hero-keys` | contrast | `quiet` (each name moves right and its number is drawn, from the first time the party list is) |
| `hero-pick-3` | contrast | `hero-pick` (a 3 at the adventuring bar where the baseline presses a 9; both have the seam on) |
| `name-letters-edit` | identical | `name-letters` (a name typed in letters only) |
| `name-arrows-edit` | contrast | `name-arrows` (a Right, an Up, a Down and a Left among the letters of a name: the program types a letter for each) |
| `quiet-edit-keys` | identical | `quiet` (the walk's own arrows, which the line editor never reads) |

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
- **A batch is invisible to every other seam's points** but
  `inside_calls` ones (§3). While a batch runs the engine offers no
  others, so anything a seam paints through
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
| `list-arrows` | the up and down arrows step the game's pick-lists, its party-member picker and, at the bars where Home and End step the selected member, that member, as Home and End do | overlay 25 (the list routine and the menu-bar routine), and the resident image |
| `bar-keys` | Left and Right step a command bar's highlight at every bar but the few that use them; Enter takes the highlighted command at the Yes/No prompt, the event scripts' questions, the camp bar and its Magic and Alter bars, and the adventuring bar; Esc answers No at a Yes/No question | overlay 25 (the menu-bar routine) |
| `menu-cursor` | a cursor on the main menu (the party-setup screen and a training hall): Up and Down move it over the commands shown, Return takes the one it is on | overlay 25 (the menu-bar routine), for the loop in overlay 16 |
| `hero-keys` | the number row's 1 to 8 select a party member where Home and End do, and the party list shows each member's number | overlay 25 (the menu-bar routine), and the resident image |
| `edit-keys` | the arrows, Home, End, the page keys and the function keys no longer type letters at the game's name and text prompts | the resident image |
| `cheat-invulnerable` | the party takes no damage | the resident image |
| `cheat-kill-all` | every enemy takes 120 damage, **when pulled** (§3a) | the end check's overlay |
| `cheat-wound-party` | the whole party drops to one hit point, **when pulled at camp** (§3a) | the resident image |
| `font-sans`, `font-chisel` | the program's lettering in a face of the player's choosing; alternatives, one `group` | the resident image |

All are keyed to the baseline edition (§5).

### Answer the code wheel once (#94, #119, #290, #291)

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
- **On the store release** (#396): valid unchanged. Its GAME.OVR differs
  from the repack's only inside the protection overlay, both points are
  in the resident image, and the answered boot is pixel-identical on
  both. With every seam off its challenge accepts a wrong answer by
  itself; the seam watching it latches only what the resident compare
  finds equal (§5).
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
| the menu-bar routine's thunk | resident image | which bar is going up: the party's bar is a far pointer into the data segment at one of two offsets, every other bar is a stack copy. While it is not the party's bar the panel comes off the screen (if really up), is not drawn, and Tab is not this seam's; it stays open, and is drawn again when the party's bar is back — after an encounter fled, a vendor's question, a script's menu (M5-E2d) |

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
sidecar replaces one that is there and is never written as a new file;
one file per save slot is written at the save and replaces the table on
load, even when empty, and nothing is kept between saves; a slot
the load menu only opened is told from one loaded by whether bytes moved
through the handle (`file_event`'s traffic flags).

**Keys**: Tab; while the panel is up, every key the party's bar answers
with a roster-cursor step and a roster redraw: every extended key but
the four moves (scan `48 4B 4D 50`), and the characters `1 3 5 7 9 \`,
which the bar's raw mode translates through DGROUP `0x288C` (digits
`1`–`9` → `O P Q K space M G H I`; `\` → `7`).

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

### The list arrows (#423, #435, #447)

`seam_list_arrows.cpp`. Not a PLAN.md §5 item: a player's request, built
after v1's six. A setting, no key, nothing to pull. The module, the read
point at overlay 25 `0x0572` and the test of who called the menu-bar
routine are shared with `bar-keys` (`seam_menu_bar.h`).

| point | module | what the handler does |
|---|---|---|
| image `0x0FE0` of overlay 25, the instruction after the pick-list's call into the menu-bar routine | overlay 25, through the manager's word | with the routine's out-parameter set, AL `0x48` becomes `0x47` and `0x50` becomes `0x4F` |
| image `0x38AA`, the instruction after the party-member picker's call into the same routine | the resident image | the same, with the out-parameter in the picker's frame |
| image `0x0572` of overlay 25, the call into the program's key-read routine inside the menu-bar routine (the point `bar-keys` has too, `seam_menu_bar.h`) | overlay 25, through the manager's word | reads the keystroke at the head of the BIOS ring before the program does. An extended Up or Down, from a caller in the allowlist below, becomes Home or End; the keypad's 8 or 2, from the same callers, becomes its 7 or 1 |

| fact | value |
|---|---|
| the module | overlay 25: file offset 182479 (`0x2C8CF`), 4682 bytes (`0x124A`), digest `175454bc…3901`; the program's load-segment word is at image `0x3C60` |
| the key, at both points | AL, as the routine returns; the next instruction stores it |
| the out-parameter | a byte the caller passes by address: **1** a raw key (an extended key's scan code, or a translated keypad digit), **0** a bar command or a confirmation. The list's is at `BP-0x57`, the picker's at `BP-0x2B` |
| what the list acts on | `0x47` up a row, `0x4F` down a row, `0x49` and `0x51` a page when one is there; Up and Down `0x48`, `0x50` fall through to "ask again" |
| what the picker acts on | `0x4F` the next member (the head after the last), `0x47` the previous one (the tail from the head) |
| who calls the list | driven: race, gender, class and alignment at creation, the spell list, the Items screen's list; from its callers, not driven: the shops, training, coin selection, the camp's Display |
| who calls the picker | Trade's receiver, "Cast Spell on whom", a script's party pick |
| the party cursor | the resident thunk at `0x108A2`, called through the stub at `0085:0052` (`9A 52 00 85 00`): `G` steps the selected member back, `O` forward, **any other key moves the selection to the head of the party**. It is reached from sixteen call sites in twelve routines (found by searching GAME.OVR and the image for the far call); nine of those routines are the allowlist below |
| the keystroke at the third point | the head word of the ring at 40:1Eh, scan code high and character low: Up `0x4800`, Down `0x5000`, Home `0x4700`, End `0x4F00`. The keypad's 8 and 2 with Num Lock on carry the same scan codes **and a character**: `0x4838` and `0x5032`, which is how the seam tells them from Up and Down; it writes `0x4737` and `0x4F31` (the keypad's 7 and 1) over them. The number row's digits (scan `0x02` to `0x0B`) are never touched |

- **The out-parameter is read first, and only when AL is an arrow.** With
  it **clear**, `0x50` is the bar's own `P`, the Prev command, and is
  left alone (`SeamListArrows.LeavesTheBarsOwnPrevAloneWhereTheKeyIsACommand`).
  A byte that is neither zero nor one is not the frame these facts
  describe: the handler declines and touches nothing. A list driven with
  Home and End alone costs the seam not one byte read.
- **The scope is positive, not inferred.** The arrows move the party in
  3D, in the wilderness and in combat, and nothing outside can tell from
  here when an arrow is free. The first two points are reached from inside
  a pick-list and a picker and nowhere else, so no other screen's arrow is
  ever offered. The third is reached from every command bar, and is a
  table of callers (below): a caller is named or it is not touched.
  Driven: Up, Down and Left in the 3D view at 4,12 S with the seam on and
  off give the same positions and identical stills (252 of 252), and the
  first two points are never reached there.
- **The program's own stepper does the rest**, so wrapping, title
  skipping and paging are the program's, not this seam's.
- **Keypad and digits.** The menu-bar routine's raw mode turns the digits
  1-9 into the movement letters the keypad's arrangement implies, with the
  out-parameter set, so 7 and 1 already stepped a list and 8 and 2 come
  back as `0x48` and `0x50`. The program cannot tell the number row from
  the keypad, so with this seam on 8 and 2 step a list too.
- **The keypad's 8 and 2 at the bars** (#447). With Num Lock on the
  keypad delivers its digit as the character (`0x4838`, `0x5032`). The
  routine translates a digit **after** the point reads the key, with the
  table at DGROUP `0x288C` (`1` to `9` become `O P Q K` space `M G H I`; the
  table is in the program's initial data and in a running image alike), and
  it looks at the character and never at the scan code: the key comes from
  the program's read routine as its low byte alone, and an extended key is
  the pair zero then the scan code. A keypad 8 therefore reaches the party
  cursor as `H` and a 2 as `P`, which it does not know, and the selection
  goes to the head. The third point rewrites the ring's head word `0x4838`
  to `0x4737` and `0x5032` to `0x4F31` at the same callers as the arrows;
  the table turns the 7 into `G` and the 1 into `O` and the cursor steps
  as it does for Home and End. Told from the arrows by the character, and
  from the number row by the scan code, which is Up's, Down's, Home's or
  End's and never `0x02` to `0x0B`. With `hero-keys` on at the same point
  both run, in either order, and each keeps its own keys: the number row's
  `8` selects the eighth member and the keypad's `8` steps back one
  (`SeamHeroKeysWithListArrows.*`; driven in slot C's camp).
- **`fired` counts the keys a list or picker read**, not the arrows
  rewritten: a handler that arrives and chooses to do nothing has been
  served (§3a). An arrow-free run reads `fired=9` and is identical to the
  seam off.
- **Qualified as overlay 25**, resolved from the manager's word at every
  step (§4); inert with `module_not_resident` while the overlay is out.
  The store releases' GAME.OVR differs from the repack's only inside
  overlay 2 (§5), so the digest holds on both. The word's address came
  from the search §8.1 describes (one match; the same search returns
  `0x360` for overlay 8 and `0x760` for overlay 15).
- **How the facts were checked, by two routes.** The module row is the
  overlay file's own table and the manager's record of the same two
  numbers, which the search above found once; the two points are the
  instructions after the two calls in the disassembly, and a driven run
  reached each: the list's with the manager's word armed and `fired=9`
  through creation, the picker's by casting a cure on a party member
  from the adventuring bar and reading the highlight step on Down, Down
  and Up (the head, the tail from the head, back).
- **Driven:** race, gender and class lists at creation, the spell list,
  the Items screen's list, the picker via a cast. **Read and tested, not
  driven:** the shops, training, coin selection and encounter lists; the
  picker from Trade, which did not open one from the Items screen's `T`
  (the cast's did), and from a script.
- **Rejected:** a host-side remap of the arrows, because a host cannot
  say when an arrow is free; a rewrite at the menu-bar routine's read
  point for every caller, because Up and Down mean something at the
  adventuring bar, in combat, in Modify and in the rest-time menu, which
  is why the third point is an allowlist and not an exclusion table (a
  caller nobody read would be stepped by default, and the cost of a wrong
  guess is a party that does not move, or a selection that is reset);
  rewriting in the BIOS buffer for the lists, because they see the key
  through the routine's return and the buffer would carry a key nobody
  pressed.
- **Not here:** the main menu's Up, Down and Enter (#434, a seam of its
  own); the keypad's 8 and 2 at the adventuring bars, where they walk; a
  held key scrolling (#426).

**The command bars** (#435). Many raw-mode callers of the menu-bar
routine hand every raw key to the party cursor, which steps the selected
member on Home and End and **on any other key puts the selection back on
the first member**. There Up and Down were not dropped, they threw the
selection away; Home and End were the only keys that stepped it. The third
point rewrites Up as Home and Down as End in the ring, before the program
reads them, at the callers below. The bar, its highlight and its commands
are untouched, and nothing is drawn. A caller is identified exactly as
`bar-keys` does it (`seam_menu_bar.h`, shared): the routine's far return
address in its frame, whose segment must be the one the overlay manager
says the caller's module is at now, and whose offset is the instruction
after the call.

**The allowlist.** A caller is in it when raw Home and End reach the party
cursor there and Up and Down do nothing else. Each by two routes: the
disassembly of the caller from its return offset on (the out-flag test, or
the lack of one, and the compare chain: no compare against `0x48` or `0x50`
outside an out-flag guard), and a driven run with the seam on. The driven
runs print the return address and the manager's word at the point (a
temporary print, removed) and read the party panel for the selected member.

| caller | module (manager's word) | return offset | Up and Down today | checked |
|---|---|---|---|---|
| the camp bar | overlay 15 (`0x760`) | `0x1F24` | the cursor: selection to the head | disassembly; driven (`camp-down`, `camp-down-arrows`) |
| camp's Magic bar | overlay 15 | `0x1447` | the same | disassembly; driven: Down steps the member |
| camp's Alter bar | overlay 15 | `0x1CA4` | the same | disassembly; driven: Down steps the member |
| the party-order screen | overlay 15 | `0x17DA` | not picked up: the same. Picked up: ignored | disassembly; driven on and off |
| the post-combat treasure bar | overlay 5 (`0x260`) | `0x1024` | nothing: the cursor is called on `G` and `O` only; `P` is under the out-flag | disassembly; driven, one-member party (the arrow is rewritten, the member cannot move) |
| the post-combat Take bar | overlay 5 | `0x0D91` | nothing: the compares are `M`, `I`, `E`, `0`, `G`, `O` | disassembly and the ported source; **not driven**: it is up only when both coins and items are pooled, and the fights reachable here drop coins |
| the shop's bar | overlay 6 (`0x290`) | `0x061F` | nothing: `P` is under the out-flag | disassembly; driven at the armourer |
| the temple's bar | overlay 4 (`0x230`) | `0x0DAA` | nothing: `H` (Up's scan code) and `P` (Down's) are under the out-flag | disassembly; driven at Sune's temple (Up does not open Heal) |
| the script prompts | overlay 7 (`0x2C0`) | `0x16EB` | the cursor: selection to the head | disassembly; driven at the armourer's "show you our wares" |

- **The party-order screen** is in, and its Home and End *move the
  member*: with one picked up (Return), `G` moves it up the order and `O`
  down. Up and Down do the same with the seam on (driven: THIEF picked up
  at slot C's camp, Down puts it below PRINCESS FATIMA, Up puts it back).
  Seam off, Up and Down there do nothing.
- **Where a letter is a scan code.** Up is `H` and Down is `P`, and the
  temple's bar has a Heal and the shop, temple and treasure bars a Pool.
  Each of those compares the key with the letter **under** the out-flag,
  so a raw Up or Down is never a command there; the two bars checked
  driven (the temple's and the shop's) did not act on one.
- **The module words** for overlays 4, 6 and 7 (`0x230`, `0x290`, `0x2C0`)
  are the manager's records, found by the search `seam_cheats.cpp`
  documents: one match each, and the same search returns the known
  `0x260`, `0x360`, `0x690`, `0x730`, `0x760`, `0x790`, `0x8D0` and
  `0x3C60`. Driven: the word read at the point equalled the frame's
  segment at every allowlisted caller reached (camp `0x2837`, the
  temple `0x2D53`, the shop `0x3158`, the script prompt `0x290B`,
  post-combat `0x306A` on one run).

**Left out, and why** (every other caller of the menu-bar routine is in
the `bar-keys` audit above, which has its verdict on arrows):

| caller | return offset | why |
|---|---|---|
| the adventuring bars, city and wilderness (overlay 14) | `0x09D5`, `0x0C45` | Up and Down move the party; driven, in=0 and identical stills |
| the main menu (overlay 16) | `0x02FD` | #434's: its Up, Down and Enter are the `menu-cursor` seam's |
| the combat move loop, the aim cursor, the command bar (overlay 8, 13) | `0x0AC8`, `0x3178`, `0x0819` | no party cursor: the fighter and the cursor are moved |
| the stat editor (overlay 16) | `0x216E` | Up and Down are its rows |
| the rest-time menu (overlay 20) | `0x076E` | Up and Down are Inc and Dec; driven, stills identical (272 of 272) |
| the game-speed screen (overlay 15) | `0x1B91` | Down and Up are its two commands |
| the temple's appraise bars (overlay 21) | `0x1C47`, `0x1DC1` | no party cursor; `0x47` is its Gems command and Up would start an appraisal |
| the two share prompts (overlay 5) | `0x0AF8`, `0x14C7` | any extended key ends them |
| every caller that is not raw | | the routine throws an arrow away; Home and End do nothing either |
| the pick-lists and the picker | | their own points, above |

- **Fired.** `fired` counts every key the menu-bar routine read at a bar
  the point could be offered, plus the two earlier points' arrivals; it
  is a count of looks, not rewrites (§3a).

**State**: none. **Host services**: none. **Keys**: Up and Down, and
keypad 8 and 2, in a list or the picker; Up and Down, and keypad 8 and 2
with Num Lock on, at the nine bars in the allowlist.

**Fidelity**: on and no arrow pressed in a list, identical: the handler
reads nothing and writes nothing unless AL is an arrow
(`list-keys-arrows` identical `list-keys`: all 84 checkpoints). On and an
arrow pressed, a contrast: `list-down-arrows` agrees with `list-down`
for 62 of 84 checkpoints and diverges from the first Down, the program
having dropped each arrow in the baseline. At the bars, on and no Up or
Down pressed, identical: the third handler writes nothing unless the head
of the ring is an extended Up or Down, and `camp-roster-arrows` is End,
End and Home at the camp bar, all 94 checkpoints identical to
`camp-roster`. On and a Down pressed, a contrast: `camp-down-arrows`
agrees with `camp-down` for 86 of 96 checkpoints and diverges from the
first Down (tick 206,218,672): the selection steps to the next member
where the seam-off run puts the first back, and the two stay apart to the
end. The same for the keypad: `camp-pad-arrows` agrees with `camp-pad`
(End, Num Lock, then the keypad's 2, 2 and 8) for 87 of 96 checkpoints and
diverges from the first 2 (tick 206,218,672): MULE, THIEF, PRINCESS FATIMA,
THIEF where the seam-off run holds MULE and then HULK. No session that
existed before moved. Unit: `SeamListArrows.*`;
stand-ins: `list_arrows_probe_off`, `list_arrows_probe_on`,
`list_arrows_bars_probe_off`, `list_arrows_bars_probe_on`.

### The text faces

`seam_font.cpp`, and the faces themselves in `machine/text_face.h`. Not a
PLAN.md §5 item: a player's request (fonts readable at a glance), built
after v1's six.

| point | module | what the handler does |
| --- | --- | --- |
| the instruction after the glyph blitter's first EGA row fetch | resident image | DL holds the row; becomes the face's row if the glyph is one a face replaces |
| the instruction after its second | resident image | the same; the blitter fetches once per page it draws to |

Both points are `inside_calls` (§3): the journal draws its pages, and
paints the screen back when it closes, through the program's routines
in a batch, and the blitter is the blitter whoever called it.

| fact | value |
| --- | --- |
| the points | image offsets `0x749A` and `0x74C0`; ES:DI is on the row fetched |
| the font | a far pointer at data-segment `0x5E20`, as screen text and the automap's zone label follow it; a buffer of 177 glyphs, 64 of text and the rest pictures |
| an index | the character upper-cased modulo 64 |
| replaced | the letters, the digits and the punctuation |
| kept | `@ [ \ ] ^ _ $ %` and the space, whose slots the program fills with a frame corner, frame pieces, a mark and two solid blocks; every picture glyph past 63 |
| the guard | ES is the pointer's segment and DI falls inside the buffer; otherwise decline |

- **Writes DL and nothing else.** The program's font buffer is never
  touched, so a face is gone at the next row fetched after it is
  switched off. Text already on the screen keeps the face it was drawn
  in until the program draws it again.
- **Who else reads the face:** `text_face::drawing()` is the one answer.
  Screen text matches the face first and the program's own glyphs
  second, for text drawn before a switch; the automap's zone label is
  rasterized in it, and the face is in the panel's drawing signature so a
  switch re-letters it.
- **Rejected:** writing the faces into the program's buffer, because off
  would then need the original glyphs back and this build keeps none;
  one seam with a parameter, because seams have none and a group gives
  every host the choice with no new surface; replacing the nine
  non-lettering glyphs, because the program draws its frames and blocks
  with them.
- **Candidates, kept with their reasons** (§8.4, show a person the
  candidates): a light serif, the project's own BIOS face, a bold serif
  (one thick and one thin stem at seven pixels wide read as uneven), a
  sans with a nub on each leading stem, a slab, an uncial (its rounded
  A, D, H read as lower case) and a slanted script. Sans and chisel were
  chosen by the maintainer over real frames.

**State**: none. **Host services**: none. **Keys**: none.

**Fidelity**: seen from the first character the program draws, so the
pair is a contrast: `quiet-font-sans` and `quiet-font-chisel` contrast
`quiet`, divergent from the credits' first text and only in `devices`
and `display`, never `cpu` or `ram`. `quiet-all` carries every seam but
these and `hero-keys`, and says so. Unit: `SeamFont.*`, `TextFace.*`,
`SeamFontScreenText.*`; stand-in: `font_probe_off`, `font_probe_sans`.
Driven: 48 stills from the credits to the character sheet read back
the same screen text with either face on as with none.

### The bar keys (#425, #438)

`seam_bar_keys.cpp`. Not a PLAN.md §5 item: a player's request, built after
v1's six, and the keyboard half of #379's selection. A setting, no key,
nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the program's key-read routine inside the menu-bar routine (`0x03BD`) | overlay 25, through the manager's word | reads the keystroke at the head of the BIOS ring before the program does. Left or Right becomes `,` or `.` unless the caller is in the exclusion table below, raw mode or not. Enter, from a caller in the Enter table (the script runner only when its allow-Enter argument is clear) and where the highlighted group's letter is its last match, becomes that letter. Esc, at the Yes/No prompt or at a script prompt whose command letters are `Y` and `N`, becomes `N` |

| fact | value |
|---|---|
| the module | overlay 25: file offset 182479 (`0x2C8CF`), 4682 bytes (`0x124A`), digest `175454bc…3901`; the program's load-segment word is at image `0x3C60` (the same module as `list-arrows`) |
| the loop | the key-pending call at `0x0566` (resident image `0xA6FD`), and, only if it answered a key, the key-read call at `0x0572` |
| the keystroke | the head word of the ring at 40:1Eh: scan code high, character low. Left `0x4B00`, Right `0x4D00`, Enter `0x1C0D`, Esc `0x011B`. A key behind the head is never read |
| the routine's frame | caller's saved BP at `BP+0`, return IP at `BP+2` and CS at `BP+4`; the raw-mode argument is at `BP+0x0C` (tested as a byte; the seam does not read it) |
| what the routine parsed | locals below BP: the Enter-allowed byte (`BP-0x8F`, the colours are not both zero), the group count (`BP-0x8E`), a pair of positions per group from the same base (first at `BP-0x8F+2g`, last the byte after), and the bar as a Pascal string at `BP-0x53`, characters numbered from one |
| the highlight | DGROUP `0x6B2B`, the one-based group index every bar shares (#304) |
| the pushback slot | DGROUP `0x8501`, the second half of an extended key; non-zero means the head of the ring is not the key about to be read |
| `,` and `.` | step the highlight back and forward, wrapping, and redraw; the program's own |
| a letter | upper-cased, compared with **every** character of the bar, no early exit: **the last match wins** and sets the highlight to its group. Command letters are `0-9A-Z` only |
| an extended key | read as a zero, then its scan code. **Raw mode clear: thrown away.** Raw mode set: returned with the out-parameter set, and the caller decides |
| Enter | returned as `0x0D` when the Enter-allowed byte is set; what then happens is the caller's |
| Esc | returned as a zero, in raw mode or not; what then happens is the caller's. Both callers below loop on it |
| the script runner | overlay 7 (file offset 29907, `0x74D3`; 8730 bytes, `0x221A`), through the stub at `102B:0061`: entry `0x1684`, its call into the menu-bar routine at `0x16E6` (return offset `0x16EB`). The manager's word for the module is image `0x02C0`. The runner's saved BP is the one at the routine's `BP+0`; its allow-Enter argument is the word at its `BP+8`, read as a byte. The hotkeys are the script's `~`-marked letters, upper-cased in the bar, and every other letter of the bar is lower-cased |

**The callers whose Enter is nothing.** A caller is identified by the far
return address in the frame: its segment is the one the program's overlay
manager says its module is at now (the word is read at every Enter), and
its offset is the instruction after the call.

| caller | module (manager's word) | return offset | what it does with `0x0D` |
|---|---|---|---|
| the Yes/No prompt | overlay 25 (`0x3C60`) | `0x111E` | loops until the answer is in `{N, Y}` (a class at `0x10B1`: two bits, `0x4E` and `0x59`); the loop's head is **after** the instruction that sets the highlight to the second group, so the highlight survives each loop |
| the adventuring bar, city | overlay 14 (`0x730`), file offset 91851, 4268 bytes | `0x09D5` | compares the letter with A, C, V, E, S and L and with nothing else; falls to the status line and the loop |
| the adventuring bar, wilderness | overlay 14 | `0x0C45` | the same chain without A |
| the camp bar | overlay 15 (`0x760`), file offset 96305, 8158 bytes | `0x1F24` | compares with S, V, M, R and A and loops; the loop ends on the `changed` flag, or on `0x00` or `E` (a class at `0x1E3B`). The Encamp Fix's `F` is taken at the routine's return, before this compare |
| camp's Magic bar | overlay 15 | `0x1447` | compares with C, M, S, D and R and loops; the loop ends on the caller's `changed` flag, or on `0x00` or `E` (a class at `0x13D1`) |
| camp's Alter bar | overlay 15 | `0x1CA4` | compares with O, D, S, I and P and loops; the loop ends on `0x00` or `E` (a class at `0x1BF0`) |
| the script runner, **when its allow-Enter argument is clear** | overlay 7 (`0x2C0`) | `0x16EB` | loops until the key is a letter or digit, or Enter **if the argument is set**; with it clear Enter asks again |

- **Each by two routes.** The disassembly of the caller from its return
  offset on, read for a `0x0D` compare (none), and the loop's own exit
  test (the Yes/No class; the camp, Magic and Alter classes); and a driven
  run with the seam off, where a Return at the bar leaves the screen as it
  was (slot C's camp bar, its Magic bar and its Alter bar, and the Yes/No
  prompt after Save; slot C's city bar; slot J's wilderness bar; the
  arena master's question in slot A). Then the
  same Return with the seam on, which takes the highlighted command
  (`bar-enter-keys`, `bar-yn-keys`, and driven for the Magic and Alter
  bars and the two adventuring bars). The party-order screen is not in
  the table: Return is in the set of keys that pick a member up and put it
  down there.
- **The script runner's Enter, by two routes** (#438). The runner's
  disassembly (overlay 7 from `0x1684`): the loop's exit test is the
  letter-or-digit class, else `0x0D` with the byte at `BP+8` non-zero, and
  the call at `0x16E6` is followed by `0x16EB`. And a driven run with a
  temporary print at the point (the return address, the manager's word,
  the runner's BP and the byte at `BP+8`): at the arena master's `Yes No`
  and at the partner question after it the call returns `290B:16EB`, the
  word reads `0x290B`, and the byte is `00`; at the press-Enter notice
  before them it is `01`. The byte is the program's own choice, by its
  source: the opcode that asks a question (`0x2B`) sets it only when the
  script offers one choice, so every `PRESS <ENTER>/<RETURN> TO CONTINUE`
  prompt has it set and every question of two or more choices has it
  clear. The attitude picker (`~Haughty ~Sly ~Nice ~Meek ~Abusive`) and
  the encounter prompt (`Combat Wait Flee Parlay`, with `Advance` for
  `Parlay`) go through the same runner with it clear, and so does the
  post-combat *treasure left* question, which is `Yes No`; those three are
  read, not driven. With it set, **Enter is the program's own, and that is
  the prompt's first choice**; the seam leaves it. With it clear, the
  Return that the runner would have asked again for takes the highlighted
  letter, so what a player sees highlighted is what Return answers
  (`bar-script-keys`: 151 of 164 checkpoints identical, divergent at the
  Return at the arena master's question). The byte is read through the
  routine's saved BP; it is refused unless the saved BP is above the
  routine's own and the byte is inside conventional RAM
  (`EnterNeverReadsTheRunnersFramePastConventionalRam`).
- **The last-match guard.** The letter is the first character of the
  highlighted group, read from the routine's own parse. Enter is rewritten
  only if the last position in the bar holding that letter is that same
  character; otherwise the handler declines and the Enter is dropped as
  the program always dropped it. No bar the table names repeats a letter,
  so the guard is never reached on this program (unit test:
  `EnterIsRefusedWhereALaterGroupHoldsTheSameLetter`).
- **A spliced command is a group like any other.** The Encamp Fix's `Fix`
  and the journal's `Notes` are in the bar before the routine copies it,
  so Enter on a highlighted `Fix` or `Notes` is the typed letter exactly:
  frames from the Return to the end are identical to a typed `F` and a
  typed `N` (121 stills each, driven).
- **Left and Right are rewritten at every caller but the ones that use
  them** (#432). The routine throws an arrow away at a non-raw caller, so
  `,` and `.` in its place cost nothing there. A raw caller is handed the
  arrow as its scan code, `0x4B` or `0x4D`, with the out-parameter set,
  and most do nothing with it but hand it to the party cursor (resident
  `0x108A2`, through its thunk), which steps the selected member on `G`
  and `O` and **on any other key moves the selection to the head of the
  party**. A caller is in the exclusion table below when it uses the arrow
  on purpose; it is identified by the same far return address as in the
  Enter table, and every other caller, a frame the table does not know
  included, has its arrows stepped. Every call site of the routine in the
  program's source was read, about forty, and each is below.
- **A letter coincidence is a use of the arrow, and is not excluded.** A
  raw caller that compares letters without testing the out-parameter acts
  on an arrow as the letter that shares its scan code: Right is `M` and
  Left is `K`. Two callers do, and **by decision they are stepped like the
  rest**: the post-combat Take bar, where Right took Money, and the
  temple's keep-or-sell prompt, where Left kept the gem. With the seam on,
  Right at the Take bar no longer takes Money and Left at the temple no
  longer keeps; `M` and `K` still do.

  **The callers that keep their arrows**, by the return address in the
  frame and the manager's word for the module. All seven are in
  `seam_bar_keys.cpp`'s table:

  | caller | module (manager's word) | return offset | what it does with an arrow |
  |---|---|---|---|
  | the adventuring bar, city | overlay 14 (`0x730`) | `0x09D5` | Left and Right turn the party (`K`, `M` with the out-flag set) |
  | the adventuring bar, wilderness | overlay 14 | `0x0C45` | the party's own facings and steps |
  | the combat move loop | overlay 8 (`0x360`) | `0x0AC8` | the scan code is the direction: Right steps the fighter east, Left west |
  | the combat aim cursor | overlay 13 (`0x690`) | `0x3178` | Left and Right move the cursor |
  | the stat editor (Modify, main menu) | overlay 16 (`0x790`) | `0x216E` | Left lowers the highlighted score, Right raises it (`K`, `M` with the out-flag set) |
  | the treasure share's press-Enter prompt | overlay 5 (`0x260`) | `0x0AF8` | any extended key ends it |
  | the NPC share's press-Enter prompt | overlay 5 | `0x14C7` | the same |

  The last two do not use the arrows on purpose, but their bar says
  `press <enter>/<return> to continue` and the program lets any arrow end
  it as well; stepped, an arrow would do nothing at all, so they keep
  theirs. Judgement, and easy to take out of the table.

  **The callers that are stepped**, one line each (return offset where it
  is a fact the table or a test needed):

  | caller | what it does with an arrow |
  |---|---|
  | the camp bar (overlay 15, `0x1F24`) | the out-flag set, hands it to the party cursor, which resets the member; the loop's exit test is `0x00` or `E` |
  | camp's Magic bar (`0x1447`) | the same |
  | camp's Alter bar (`0x1CA4`) | the same. Its inner Portraits and Monsters bar (`0x1DDA`) is not raw |
  | the party-order screen (`0x17DA`) | with no member picked up, the cursor routine; with one picked up, only `G` and `O` act |
  | camp's game-speed screen (overlay 15) | tests the out-flag; only `P` and `H` (Down and Up) act |
  | the main menu (overlay 16, `0x02FD`) | the bar is not drawn (its colours are zero); a raw key outside `G` and `O` is dropped. Stepping moves the shared highlight byte unseen. Up, Down and Return here are `menu-cursor`'s |
  | the combat command bar (overlay 8, `0x0819`) | a raw key outside a short set the bar takes is cleared and the bar asked again |
  | the combat aim bar (overlay 13, `0x2C31`) | not raw |
  | the combat done bar | not raw |
  | the combat script prompts (`ecl_stage`) | not raw |
  | the other script prompts (`ecl_menu_run`) | a raw key is the party cursor's reset and then ignored |
  | the pick-lists (overlay 25) | a raw key other than Home, End, PgUp, PgDn is looped on |
  | the party-member picker | only `G` and `O` act; the loop ends on Enter, Esc, `E` or `S` |
  | the post-combat loot bar | no `K` or `M` compare |
  | the post-combat Take bar (overlay 5, `0x0D91`) | **Right is `M`, Money**; by decision stepped |
  | the shop bar | no `K` or `M` compare |
  | the temple bar | no `K` or `M` compare |
  | the temple's appraise bar (overlay 21) | no `K` or `M` compare |
  | the temple's keep-or-sell prompt (overlay 21) | **Left is `K`, Keep**; by decision stepped |
  | the rest-time menu (overlay 20 `0x8D0`, `0x076E`) | Left and Right pick the days/hours/minutes field; **by decision stepped**, like any bar. `Y`, `H` and `M` still pick the field, and Up and Down still add and take away |
  | the game-speed bar, the icon editor's two bars, the move and process bars, the View bar, the save and load slot bars, the Yes/No prompt | not raw |

  **Each exclusion by two routes**: the caller's disassembly (the call,
  the raw argument pushed as one, the arrow compare in the loop after it),
  and a driven run with the seam on. Driven with a temporary print of the
  return address and the manager's word at the point: the adventuring bar
  (`0x09D5`, `0x730`), the rest-time menu (`0x076E`, `0x8D0`, stepped since), the combat
  move loop (`0x0AC8`, `0x360`), the aim cursor (`0x3178`, `0x690`), the
  main menu (`0x02FD`, `0x790`), the combat command bar (`0x0819`,
  `0x360`) and the aim bar (`0x2C31`, `0x690`). Not driven: the stat
  editor and the two share prompts, whose offsets are the disassembly's
  alone, and whose words are from the manager's records (below).
  **The words** are the manager's record of each module, found by
  searching the resident image for its file offset and length from the
  overlay table and taking the word sixteen bytes in, one match each:
  overlay 5 `0x260`, 8 `0x360`, 13 `0x690`, 14 `0x730`, 15 `0x760`, 16
  `0x790`, 20 `0x8D0`, 25 `0x3C60`. The known four (`0x360`, `0x730`,
  `0x760`, `0x8D0`) reproduced the values other seams carry.
  Driven with the seam on and off, identical: the city's 3D with Left,
  Left, Right, Right (96 of 96 stills), the wilderness with Up, Left and
  Right (232 of 232), and by `quiet-bar-keys`.
- **Up and Down at those bars** are `list-arrows`' (#435), which has a
  point at the same instruction and its own allowlist of the callers where
  Home and End step the party cursor; the two seams share the module, the
  read point and the caller test in `seam_menu_bar.h`. The rest-time menu
  is not in that allowlist: Up and Down are its Inc and Dec.
- **The trade at camp.** With the seam on, Left and Right at the camp
  bar, Magic, Alter and the party-order screen no longer put the selected
  member back on the first one. Nobody presses an arrow to do that, and
  Home and End still step the member (driven at camp: `End`, `End`,
  `Home` the same stills on and off, 110 of 110; then a Left leaves the
  second member selected where the seam-off run puts the first back).
- **The point is the read, not the poll.** A point at the poll call would
  see the ring before the poll looks; a key can land between the two. At
  the read call the key is waiting and nothing can change the head before
  the read, so what the handler sees is what the program reads. The
  pushback slot is checked because the poll answers from it first.
- **The other seams' claims are ahead of this point.** The automap's and
  the journal's claims are at the key-pending routine, which the loop
  calls first, so a key either wants is gone from the ring before this
  point is reached. One race remains: a key landing between a claim and
  the poll's own look reaches this point, and with the journal reader up
  would be the reader's. The handler does nothing while the reader is
  open (`LeavesTheKeyAloneWhileTheJournalReaderIsOpen`). Driven with
  `automap` and `journal` on, Tab, arrows, `Notes` and Return in the
  reader are identical to the seam off.
- **Posted under Enter's scan code.** A letter made of an Enter carries
  scan `0x1C`: the program reads the character and nothing else.
- **`fired` counts the keys the menu-bar routine read**, at the main menu
  and every bar after it, not the rewrites: a handler that looks and
  chooses to do nothing has been served (§3a). `quiet-bar-keys` reads six
  and rewrites none.
- **Enter at a bar the routine does not draw** (Enter-allowed clear) is
  left alone: the routine would have dropped it. So is Enter while the
  highlight index is still zero, before any bar has been stepped or chosen
  from: no group is highlighted, so there is no command to take. A group
  count or position the routine could not have written declines
  (`point_not_recognized`).
- **Rejected:** a table of the callers that may be stepped, which is
  what #425 built (every raw caller left alone), because the callers that
  use the arrows are few and the rest would each need the same proof;
  posting a second key with `inject_keystroke`, because the
  program drains its keyboard after every key it reads (§8.4) and the
  head would be read first anyway; a table of every caller, because a
  caller that confirms on Enter would be handed a letter it never asked for
  (below); rewriting AL in the key-read routine, because an arrow comes
  back as a zero and then its scan code, two returns for one key.

**Callers looked at and left out, and why.**

- The pick-list (`0x0D9A`): Enter confirms its row. Never in the table.
- The party-member picker (resident image `0x38AA`), where Enter is in the
  set that ends the loop.
- The script runner with its allow-Enter argument set: Enter is the
  program's, and answers the first choice.
- The combat move loop: the key `0x0D` ends it.
- The statistics screen of overlay 20 (`0x0721`): Enter becomes its `R`.
- The party-order screen (`0x17DA`), for Enter: Return is in the set of
  keys that pick a member up and put it down.
- The others, which have no `0x0D` branch in the program's source but have
  not been shown by two routes or driven: the combat command, aim and done
  bars, the script stage prompt, the game-speed bar, the icon editor, the
  main menu bar, the modify and move bars and the memorize bar, the
  post-combat bars, the shops, temples and training, the View bar, and the
  save and load slot bars. Each can join the Enter table on the same
  proof.

**How the facts were checked, by two routes.** The module row is the
overlay file's own table and the manager's record of the same two numbers
(`list-arrows`). The point and the frame offsets are the disassembly of the
routine (the call at `0x0572`, the reads of `BP+0x0C`, `BP-0x8F`, `BP-0x8E`
and `BP-0x53`, and the scan that sets the highlight) and a driven run: a
Left at the Yes/No prompt moves the highlight to `Yes` and Return then
answers it (the program exits), and Return at character creation's *keep
this character?* prompt answers its `No` and the character is rolled again. The caller offsets are
the instructions after each call in the callers' disassembly and the
offsets two other seams already place their points at (`journal`'s
`0x09D5` and `0x0C45`, `encamp-fix`'s `0x1F24`), and for the exclusions the
addresses above, four of them seen live.

**Esc answers No** (#438), at two callers and nowhere else. Esc is read by
the routine as a zero, and both callers loop on that. At **the Yes/No prompt** (return `0x111E`) the rewrite is
unconditional: that prompt is one routine whose bar is always `Yes No`, and
every question the program asks through it (quit to DOS, keep this
character, continue the battle, flee, and more) has `N` as its other
answer. At **the script runner**
(`0x16EB`) it is conditional on the bar: its command letters, every `0-9A-Z`
in the routine's copy of it, are exactly `Y` and `N`, which is what a
script's `~Yes ~No` makes (the arena master's two questions; the
post-combat *treasure left* question, by the source). A script bar with
a third letter, a digit, or one answer only is left alone. The key is posted
as the letter under Enter's scan code, as Enter's is. Driven: Esc at the
quit question after Save returns to the camp bar (`bar-esc-keys`: 91 of 99
checkpoints identical, divergent at the Esc), and Esc at the partner
question answers No and the adventuring bar returns (`bar-script-keys`).
The Esc before the quit question, at the save-slot bar, is that bar's own
and is untouched in both runs.

**Where Esc is ignored or not No at a two-answer prompt, left alone.** Read
in the program's source, not driven. The temple's keep-or-sell prompt
(overlay 21, return `0x1DC1`) keeps the gem only on `K` and sells on any
other key, so Esc **sells** it; nothing here changes that. Every other
two-answer prompt the source shows is either the Yes/No prompt or a script
question, both covered above, or a bar whose Esc leaves it (the save-slot bar,
the sub-bars of camp).

**What no check pins.** The callers' modules are identified by the manager's
word and the return offset, not by digest: a point in each would make the
whole seam inert while that overlay is out of memory, which is most of the
time. `journal` and `encamp-fix` pin the same two modules by digest.

**State**: none. **Host services**: none. **Keys**: Left and Right at
every caller but the seven above; Enter at the six callers in the first
table and at the script runner when its allow-Enter argument is clear; Esc
at the Yes/No prompt and at a script prompt whose letters are `Y` and `N`.

**Fidelity**: on and none of the four keys pressed at a bar, identical:
the handler reads the ring's head word and writes nothing unless it is one
of them (`quiet-bar-keys` identical `quiet`: all 90 checkpoints, a Right
at the adventuring bar included, which keeps its arrows). On and one pressed, a contrast:
`bar-enter-keys` agrees with `bar-enter` for 86 of 93 checkpoints and
diverges at the Return at the camp bar; `bar-yn-keys` agrees with `bar-yn`
for 86 of 100 and diverges at the Right at the slot bar. Both stay apart to
the end: besides the screens, the ring holds the rewritten word where the
seam-off run holds the key. `bar-camp-keys` agrees with `bar-camp` for 83
of 93 checkpoints and diverges at the Right at the camp bar. `bar-esc-keys`
agrees with `bar-esc` for 91 of 99 and diverges at the Esc at the quit
prompt; `bar-script-keys` agrees with `bar-script` for 151 of 164 and
diverges at the Return at the arena master's question (the press-Enter
notice before it, which allows Enter, is identical). Unit:
`SeamBarKeys.*`; stand-in:
`bar_keys_probe_off`, `bar_keys_probe_on`.

**One consequence a script feels.** A Return the program used to drop at a
Yes/No prompt now answers it. Character creation's roll asks whether to
keep the character; the leg-0 script's Return at that prompt answers `No`
and the character is rolled again. So does an Esc there.

### The menu cursor (#434)

`seam_menu_cursor.cpp`. Not a PLAN.md §5 item: a player's request, built
after v1's six, and the main menu's half of what #425 and #423 did for the
bars and the lists. **It draws something the game never had** (the menu
has no highlight at all), so it is held to PLAN.md §5's "native in feel":
the cursor is the game's own pick-list highlight, drawn by the game's own
string routine. A setting, no key, nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the key-read routine inside the menu-bar routine (the point `bar-keys` has) | overlay 25, through the manager's word | with the routine's caller the party-setup loop, **Up** or **Down** move the cursor and are answered; **Return**, with the cursor drawn, becomes the letter of the command under it |

| fact | value |
|---|---|
| the caller | overlay 16's loop (the manager's word is at image `0x0790`); the call is at `0x02F8` and the instruction after it, the return address the frame holds, at `0x02FD`. The loop passes the bar at DGROUP `0x05F0`, both bar colours **zero** and raw mode **set** |
| who reaches the loop | the party-setup screen at the start of the program, and the same function again where a script re-enters it: **a training hall**. Driven at the cleric hall in the civilised district (5,0, off the lobby at 6,2), where `Y` to *do you want to train* opens it; the caller test below was passed there as at the start |
| the commands | eleven records at DGROUP `0x0619`, forty-two bytes each: a Pascal string and, at `+0x29`, an enable byte (`0x0642`, `0x066C`, ... `0x07E6`). In order: Create, Drop, Modify, Train, View, Add, Remove, Load, Save, Begin, Exit. A record whose byte is not zero is drawn |
| what the loop writes | each time it redraws: Drop, Modify, View, Remove, Save and Begin on and Load off with a party member, the reverse with none, and Train on where the place trains. **Create, Add and Exit it never writes**; they are on from the start. So the menu shows **four** commands with no party (Create, Add, Load, Exit), **nine** with one, **ten** at a hall (Load is off, Create and Add are on) |
| the drawing | rows from text row `0x0C`, packed, one to a command: the first letter in colour `0x0F` at column 2, the rest of the word in `0x0A` from column 3 (screen text: ink `F`, then `A`) |
| the keys | the ring's head word: Up `0x4800`, Down `0x5000`, Return `0x1C0D`. Up and Down are raw keys the loop drops; Return is dropped by the routine (the bar has no colour); a letter in the bar takes its command |
| the redraw | any command taken, a disabled command's letter included, ends in the loop's **full redraw** (frame, party, rows). Home and End (`G`, `O`) go to the party cursor and back to the call **without** one |
| the string drawer | image `0x076B6` (§3): column, row, colour, then a far pointer to a Pascal string. The cursor is drawn by handing it the **record itself**, where it stands in DGROUP |

- **The loop's addresses by two routes.** The disassembly of the loop (the
  pushes before the call: the bar at `0x05F0`, `0x0D`, `0`, `0`, `1`, `0`;
  `lcall 0x3C5:0x2A`, the thunk for overlay 25's `0x03BD`, at `0x02F8`)
  and the machine: the handler's caller test (the frame's far return
  address against the manager's word and `0x02FD`) is passed at the first
  menu of every driven run, after the add screen and the view screen, and
  at the hall. The records, their stride and their enable bytes are the
  program's data segment as the loop's source reads it and as a run's
  screen text lists them (four rows, nine rows, ten rows, in the order
  above).
- **Hidden until used.** The seam draws and writes nothing until the first
  Up or Down. A player who types letters sees the original menu, and a
  Return with no cursor is the program's own: dropped.
- **Where the cursor is, and why nothing else is kept.** In the program's
  own memory, in the place its redraw also writes. The loop rewrites the
  enable bytes of **Drop and Load** every time it redraws, and exactly one
  of them is on (Drop with a party member, Load without). The byte that is
  on holds `1`. While the cursor is drawn the seam holds **`2` plus the
  row** in it. Every reader of an enable byte tests it against zero (the
  loop's draw and dispatch; the program's source names no other reader), so
  the program cannot tell, and the redraw that wipes the cursor's pixels
  puts the byte back: the cursor lasts exactly as long as it is on the
  screen. Nothing is remembered by the seam and nothing has to be cleared.
  Not in `scratch()`, §3's last resort: it was not needed.
- **Why not the bar's highlight byte `0x6B2B`.** It was the first design
  and it cannot say whether a cursor is drawn: a typed letter sets it, so
  the menu would grow a cursor on its return from a command and Return
  would take a command nobody could see. `,` and `.` (which `bar-keys`
  gives to Left and Right) step it with nothing drawn. And it is not where
  the menu left it: the screen a command opens runs a bar of its own on the
  same byte (measured at the menu: `2` after the add screen, `1` after the
  view screen). The seam never writes it. Nor are **the pixels** the state:
  the composed frame lags the planes by up to a frame, and a seam that is
  only looking must never read the video window (§3, §8.4).
- **Moving.** The cursor starts on the **first** command shown, so the
  first press moves it: Down, Down takes the third. Up from nothing goes to
  the last. It wraps at both ends, over the **rows shown**: a command the
  menu does not draw has no row and is skipped. After a command is taken
  the menu is redrawn, the cursor is hidden again, and the next Up or Down
  starts from the first.
- **Drawing.** The word it moves to is drawn whole in `0x0F` by the
  program's own string drawer, handed the record's own address, so the text
  is never in this repository. The word it leaves is put back the way the
  loop draws it: its first letter in `0x0F` at column 2 and the rest in
  `0x0A` at column 3, two Pascal strings the handler places
  (`place_bytes`, §3), 41 bytes at most. One batch of at most three calls,
  with the lit word first so that the cursor is on the glass before the old
  word is put back. Frame by frame (`--dump-every 1`, a press at 7,600
  and another at 7,615) a move is a left-to-right sweep a glyph at a time:
  six frames to light a word, five more to put the old one back, and no
  frame in which a word is blank or half missing; each cell goes straight
  from one colour to the other, as the program's own text appears.
  **Making the program redraw its menu** (the loop has a
  flag for it) was considered and not built: it repaints the frame, the
  party and every row on each press, and the cursor would then need a
  second point after the redraw to be painted on it, which needs to know
  whether to, which is the state this design does not have.
- **Answered.** The key is replaced by `-` (`seam_key_read.h`), which the
  routine reads and throws away, so it goes back to waiting. A batch offers
  its point again when it is done (§3), and the handler then finds the `-`
  and nothing to move.
- **A second press while the cursor is drawn.** A move costs the program's
  own glyph-at-a-time drawing: **26,960 steps (about 90 ms of the machine's
  time) for a first press and 60,706 (about 200 ms) for one that puts a word
  back**, measured. A key pressed in that time waits behind the `-`, and
  the program's read keeps the last key waiting and drops the rest, which
  ate the third of three quick Downs. So the handler takes its own `-` off
  the ring when an Up, Down or Return is behind it, and handles that. It is
  the only thing it ever takes off the ring; three Downs three frames apart
  now move three rows. The cursor's byte is written when the move is
  decided, not when its pixels are done, so a Return right behind a Down
  takes the row the Down chose.
- **Interplay.** `bar-keys`' Left and Right at this menu step the shared
  highlight byte with nothing drawn; they do not touch the cursor. Its
  Enter table does not include this caller. `list-arrows` does not reach the
  main menu today (#435 extends it to the horizontal bars and leaves this
  caller to this seam). Driven with all three on: Down, Right, Left, Down,
  Home, Return gives the same stills as Down, Down, Home, Return with this
  seam alone.
- **Rejected:** Up and Down as the party cursor (Home and End) here, the
  maintainer's choice, which is the other half of #435; a host-side remap,
  for the reason `list-arrows` gives; keeping the cursor between commands,
  which needs a place that survives a redraw and is not the program's;
  starting from the bar's highlight; and the keypad's 8 and 2, which the
  routine turns into the movement letters itself (they arrive as raw `H`
  and `P` and the loop drops them).
- **Not here.** The pick-lists (`list-arrows`), the horizontal bars
  (`bar-keys`), a held key (the hosts drop OS key repeats, `hosts.md`).

**State**: none of the seam's own; the cursor is held in the enable byte of
Drop or Load while it is drawn (above). **Host services**: none. **Keys**:
Up, Down and Return at the main menu.

**Fidelity**: on and no Up or Down pressed at the main menu, identical:
the handler reads the ring's head word and writes nothing unless it is Up
or Down at that caller, or Return at that caller with a cursor drawn
(`menu-letters-cursor` identical `menu-letters`: all 77 checkpoints). On and
Down, Down and Return pressed, a contrast: `menu-down-cursor` agrees with
`menu-down` for 60 of 70 checkpoints and diverges at the first Down (tick
151,168,688), and the slot prompt is up at the end where the seam-off run
shows the plain menu.
Unit: `SeamMenuCursor.*`; stand-in: `menu_cursor_probe_off`,
`menu_cursor_probe_on`.
### The hero keys (#439)

`seam_hero_keys.cpp`. Not a PLAN.md §5 item: a player's request, built after
v1's six. A setting; keys and a display change. Nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x0572` of overlay 25, the call into the key-read routine inside the menu-bar routine (`bar-keys`' point) | overlay 25, through the manager's word | a number-row `1` to `8` at the head of the BIOS ring, from a caller in the table below: puts the selection on the target's successor (the head for the last member and for a party of one) and rewrites the key to Home. With nobody behind the digit, the key becomes the one the program throws away |
| image `0x138F`, the instruction after the roster drawer clears a member's row | resident image, `inside_calls` | the name's column goes up two, and the program's glyph routine draws the member's number at the old column in white |
| image `0x13CB`, where the drawer's two name paths meet, before the armour class | resident image, `inside_calls` | a name that now ends past column `0x1F` has its tail cleared by the program's clear routine; the column goes back |

| fact | value |
|---|---|
| the party | a linked list. Head: far pointer at DGROUP `0x5D96`; a record's next member: far pointer at `+0x104`; the selected member: far pointer at `0x5D92` (offset, then segment). At most eight members |
| the cursor | the resident routine behind `0x108A2`. `G` from the head goes to the tail, from anyone else to the member whose next is the current one; `O` goes forward (the tail stays); any other key goes to the head |
| how a caller reaches it | the menu-bar routine in raw mode returns `0x47` (Home) as `G`, `0x4F` (End) as `O`, and the characters `1` to `9` through the table at DGROUP `0x288C` (`7` is `G`, `1` is `O`, `8` `H`, ...), with the out-parameter set. The callers below hand `G` and `O` to the cursor and redraw the list from the selection |
| the number row | scan `0x02` to `0x09` for `1` to `8`, character `'1'` to `'8'`. The keypad's digits with NumLock on are scan `0x47` to `0x51` with the same characters, which is the only way to tell them apart |
| the data segment | image paragraph `0xC7C` on (DS `0x0CDC` where the image is at `0x60`); checked against DS before the party is read |
| the roster drawer | image `0x1307` (paragraph `0xBA`, offset `0x0767`; the automap and the journal call it through the same address). Frame: column byte at `BP-5` (`1` on the main menu, `0x11` beside the viewport), row byte at `BP-6` (`4` for the first member), the member's far pointer at `BP-4` and `BP-2`. Names are drawn from the column (white for the selected member, else by status), the armour class at `0x20`+, the hit points at `0x24`+ |
| the routines it calls | the glyph blitter at paragraph `0x709` offset `0x1DF`, `retf 0Ch`, six words first to last: column, row, colour, `1`, character, `1`; the clear at paragraph `0x3F1` offset `0x137`, `retf 8`, four words: left, top, right, bottom. Both far, called at the paragraph they were linked at (§8.4) |

**The callers that take a digit** (a return offset in the module, and the
manager's word for it; the same far-return identification as `bar-keys`):

| caller | module (manager's word) | return offset |
|---|---|---|
| the adventuring bar, overhead view and 3D / wilderness | overlay 14 (`0x730`) | `0x09D5`, `0x0C45` |
| the main menu (the title screen's, and every other screen the same loop serves) | overlay 16 (`0x790`) | `0x02FD` |
| the camp bar, camp's Magic bar, its Alter bar | overlay 15 (`0x760`) | `0x1F24`, `0x1447`, `0x1CA4` |
| the script prompts' one menu routine | overlay 7 (`0x2C0`) | `0x16EB` |
| the post-combat treasure bar, and its Take bar | overlay 5 (`0x260`) | `0x1024`, `0x0D91` |
| the shop's bar | overlay 6 (`0x290`) | `0x061F` |
| the temple's bar | overlay 4 (`0x230`) | `0x0DAA` |

- **Each by two routes.** The caller's disassembly from its return offset:
  the key in the out-parameter's branch goes to the party cursor (resident
  `0x85:0x52`, the thunk of `0x108A2`) and then to the roster drawer
  (`0xBA:0x0767`); in the adventuring bar any raw key other than the four
  moves does, in the camp, Magic, Alter bars and the script prompts any
  raw key does, and in the main menu, the treasure bar, the Take bar, the
  shop and the temple `G` and `O` do. And a driven run with the seam on:
  the digit selects (slot A's 3D view and the wilderness at slot J, the main
  menu with six members added, the camp bar, Magic and Alter, a script's
  Yes/No at the armourer, the armourer's bar, the temple's bar, the treasure
  bar after a fight from a party of one). **Not driven: the Take bar**,
  which needs a fight that leaves both coins and items; its disassembly is
  the treasure bar's, a `G` or `O` to the cursor and the roster drawn.
  The manager's words for overlays 4, 6 and 7 are new here, found by
  searching the resident image for the manager's record of each (file
  offset and length from the overlay table), one match each, and the word
  sixteen bytes in; the same search returns `0x260`, `0x360`, `0x690`,
  `0x730`, `0x760`, `0x790`, `0x8D0` and `0x3C60` for the overlays this file
  already names.
- **Left out, and why.** The party-order screen (camp, `0x17DA`): with a
  member picked up, Home and End *move* that member and do not select, the
  seam cannot tell which state it is in, and a digit that moved a member
  three places would be a surprise. The temple's appraise bars (overlay 21,
  `0x1C47` and `0x1DC1`): `0x47` is a command there, so the Home the seam
  drives selection with would start an appraisal. Combat's move loop, aim
  cursor and command bar, the stat editor, the rest-time menu, the game
  speed screen, the share prompts, every pick-list and the party-member
  picker: the digits are real there, or Home and End are not the party
  cursor.
- **What a digit costs where it was a movement.** The number row is the
  keypad's layout to the program (`list-arrows`, `bar-keys`), so at the
  adventuring bar `8`, `4`, `6` and `2` walked and turned and `1` and `7`
  selected a neighbour; with the seam on they select a member (`1` to `8`
  from the number row only). The keypad keeps all of it, with NumLock on
  or off, and so do the arrows. With `list-arrows` on as well, the keypad's
  8 and 2 at the bars that seam names step the member back and forward
  (#447); they are never selections. At the post-combat Take bar `6` was `M`,
  Money (by the same coincidence as Right); with the seam on it selects the
  sixth member, and `M` still takes the money. `9` and `0` are never the
  seam's; `9` is the program's PgUp and still moves the selection to the
  first member.
- **The landing.** The cursor's `G` is the program's, so the seam's whole
  job is to put the current pointer where `G` lands on the target: from a
  successor `G` goes to its predecessor, from the head to the tail. Proved
  for each party of one to eight members, from every starting selection and
  to every target, against the rule restated in the test
  (`LandsOnTheFirstAMiddleAndTheLastMemberOfAnyParty`), and driven on the
  real list for the first, middle and last of six. Nothing but the
  selection and the ring's head word is written.
- **A digit with nobody behind it** is the key the program throws away
  (`seam_key_read.h`): the routine goes back to waiting. `9` and `0`, the
  keypad's digits and every other caller are left alone.
- **With the map or the journal reader up the seam steps aside.** The
  automap takes the keys that step the party cursor while its panel is on
  the roster's cells, at the poll and at the read, and a Home from here would
  be taken there with the selection half moved (found driving it). The
  reader takes every key. Both are checked at the key point, and the
  digits are theirs: with the map up `1 3 5 7 9` are taken as they always
  were and `2 4 6 8` are what the program makes of them. When the map or the
  reader is put away the roster comes back through a batch of the program's
  own calls, with every number in it: the two display points are
  `inside_calls` (§3) for this, and draw by pushing a call frame by hand
  whose far return is the point itself, which a handler inside a batch may
  do where it may not start one.
- **The display.** The name moves two columns right (the number and a blank)
  and the number is drawn in white in the program's own glyph routine, so
  `font-sans` and `font-chisel` letter it as they letter the names. The
  program draws the name in both of its paths (white for the selected
  member, a status colour otherwise) from the column byte, so the byte is
  what is moved: up two after the row is cleared, back after the name, with
  the byte's own value as the only state (`1` and `0x11` are the program's;
  `3` and `0x13` mean the number is drawn, `2` and `0x12` that the cut is
  done). **What a name keeps.** Beside the viewport the field is columns
  `0x13` to `0x1F`, thirteen characters: a fifteen-character name shows its
  first thirteen, and a fourteen-character one its first thirteen too (its
  last would land on the first column of the armour class, which a
  value of -10 or lower fills). The cut is the program's clear over
  `0x20` to where the name would have ended; nothing in the record is
  touched. On the main menu the names start at column one and every name
  fits. **No other column moves**, and the header keeps its place.
- **Where the list is drawn.** Driven: the 3D view and the wilderness, the
  camp bar, the main menu, the shop, the temple, the treasure bar, the
  script prompt at the armourer, and the list given back after the map and
  after the journal's reader. The View screens, character creation and combat
  do not show it.
- **Why a point at the name's call and not the program's draw routines.**
  The routine that draws a name in a status colour and the string drawer
  are called from the character sheet, combat and the camp's report as
  well; the points are inside the roster drawer so nothing else moves.
- **`max_points` is sixty-four.** With every seam on, one face counted (they
  are alternatives), the build has thirty-nine points. At thirty-two the last
  seam a player switched on was refused with `too_many_points`
  (`SeamHeroKeys.EveryBuiltInSeamFitsTheEngineAtOnce`).
- **Rejected:** writing the selection and redrawing the list ourselves,
  because the callers redraw after their own step and the program's cursor
  already lands where the digit says; `call_program()` for the number
  and the cut, because the automap's and the journal's give-backs draw the
  roster inside a batch, where no point may start one; drawing a number
  with a font of our own, because the face seams would not reach it; the
  party-order screen, for the reason above.

**State**: none: the two display points keep their one value in the drawer's
own frame and put it back, and the key point decides from the machine.
**Host services**: none. **Keys**: the number row's `1` to `8` at the callers
above.

**Fidelity**: the party list is changed from the first time it is drawn with
a member in it, so a seam that is on and never used is not the seam off
(§8.5). `quiet-hero-keys` is a `contrast` to `quiet`, `quiet-all-hero-keys` one to
`quiet-all` (every seam but the faces, so the seam is also run beside the
other controls seams and the journal), and `hero-pick-3` a `contrast` to
`hero-pick` (both with the seam on, a 9 where the other presses a 3). Off, the engine is not consulted (§7). Unit: `SeamHeroKeys.*`;
stand-ins: `hero_keys_probe_off`, `hero_keys_probe_on`.

### The edit keys (#455)

`seam_edit_keys.cpp`. A player's report, not a PLAN.md §5 item: typing a
character's name, the arrows type letters. Right types `M`, Up `H`, Down `P`,
Left `K`, Home `G`, End `O`, PgUp `I`, PgDn `Q`, Insert `R`, Delete `S`, and
the function keys `;` to `D`. It is the original program's behaviour on any
PC. A setting, no key, nothing to pull.

| point | module | what the handler does |
|---|---|---|
| image `0x7AC0`, the instruction after the line editor's call into the program's key read (`1709:0A30`) | the resident image | with AL zero, which is the first half of an extended key, and the keyboard read's pushback slot armed, empties the slot |

| fact | value |
|---|---|
| the line editor | resident `1709:09E3`, image `0x7A73`. It loops on the program's key read (`1899:0059`, image `0x89E9`) and acts on AL: `0x20` to `0x7A` is appended and drawn, `0x08` is Backspace, `0x0D` and `0x1B` accept, anything else is ignored. The call is the five bytes at `0x7ABB`, the instruction after it stores AL |
| how an extended key arrives | the key read (`1A40:030F`, image `0xA70F`) takes the BIOS word and, when the character is zero, **keeps the scan code in a one-byte slot of the data segment (`0x8501`) and answers zero**. The next read finds the slot armed, empties it and answers the scan code. The editor ignores the zero and appends the scan code |
| the data segment | `0xC7C` paragraphs after the image segment, as every seam reads it |

- **The addresses by two routes.** The resident disassembly (post-fixup:
  `lcall 0x1899:0x59` at `1709:0A2B`, then the store of AL) and the bytes of
  the unpacked image (`9A 59 00 99 08 88 86` at image `0x7ABB`, the segment
  still unrelocated), which also hold the editor's prologue at `0x7A73`. The
  key read's disassembly shows the slot taken and cleared at its head and
  written from AH when the BIOS read's AL is zero (`1A40:0323`); the slot is
  the one `seam_menu_bar.h` and the automap already name. A watch cannot
  show it armed: it is set and taken inside a frame. Driven, the handler's
  arrivals are the keys the editor reads: B, Right, O, Up, B, Down, Left
  and Return is eight, and `fired=8`.
- **Who calls the editor**, by two routes: the program's source, and a scan
  of the resident image and of every overlay for a far call to the entry
  (and of the resident segment for a near one). Four, and no other.

| caller | where | what it asks for |
|---|---|---|
| the character's name at creation | overlay 16, call at `0x1E5A` | a name, fifteen characters |
| a script's free-text answer | overlay 3, call at `0x09C8` | a line, forty characters |
| a script's number prompt | the resident number input, call at image `0x7C23`, itself called from overlay 3 at `0x097B` | up to six characters, parsed as a number; a letter makes the program ask again |
| the copy-protection challenge | overlay 2, call at `0x02FD` | the code word, six characters |

  The icon editor and a save's name are not line editors. The seam is
  inside the editor, so all four are covered with one point and no caller
  is named.
- **The number input has the flaw too**, through the editor: an arrow's
  letter is not a number, so the program asks again. It is covered by the
  same point. **The View > Drop money amount editor does not read through the
  editor** and accepts digits and Backspace only, so no arrow is wrong there.
  The one exception is an Alt-letter chord, whose scan codes `0x30` to `0x32`
  are the digits 0 to 2. That is not covered and not claimed.
- **The point is after the read, not before it.** The editor has no poll: it
  goes straight into a blocking read, so a key that arrives while it waits is
  delivered with no point reached (`seam_key_read.h`). A rewrite of the BIOS
  ring's head before the read, which `bar-keys` does, would miss it. After
  the read the handler sees every key the editor is handed, and it takes
  nothing off the ring, so there is nothing to put back.
- **Why the slot and not AL.** The second read's AL is the scan code and its
  slot is already empty, so by then nothing says it is an extended key's
  second half. The first half is the only place that does, and a zero the
  editor already ignores.
- **The scope is the editor's own call.** The point is inside the editor, so
  the bars, the lists, the walk and combat read the same key read and never
  reach it. Driven with the seam on and nothing else changed: slot A's four
  walking arrows (`quiet-edit-keys`, 90 of 90 checkpoints equal to `quiet`),
  and creation's lists with Down and Up (the `list-down` script, 84 of 84).
  The point is never reached in either: `reached=0`.
- **Driven.** At the name prompt, `B`, Right, `O`, Up, `B`, Down, Left,
  Return (screen text, row 24 while typing): seam off `CHARACTER NAME:
  BMOHBPK`, on `CHARACTER NAME:  BOB`. `B`, Home, End, PgUp, PgDn, Insert,
  Delete, F1, F2, `O`, `B`: off `BGOIQRS;<OB`, on `BOB`; `fired=12`. At the
  copy-protection challenge, `A`, Right, `B`, Up, `C`: off `INPUT THE CODE
  WORD:  AMBHC`, on `ABC`. Not driven: the script prompts, which no leg
  reaches; they are the stand-in's and the unit suite's.
- **What it costs.** A key with no character is dropped at these prompts, so
  Alt and the function keys type nothing, which is the point. Typing,
  Backspace, Return and Esc are keys with a character and are never touched.
- **Rejected:** a rewrite of the ring's head before the read (above); setting
  AL to a character the editor ignores at the second read, which cannot tell
  the second half from a key; and a host-side filter, for the reason
  `list-arrows` gives (the seam is the only mechanism, and a host cannot tell
  from outside which key read is the editor's).

**State**: none. **Host services**: none. **Keys**: none; the rows on the
key card say what a player no longer gets.

**Fidelity**: on and no extended key typed at an editor, identical: the
handler reads AL and reads the slot only when AL is zero, and writes only the
slot (`name-letters-edit` identical `name-letters`, all 84 checkpoints;
`quiet-edit-keys` identical `quiet`, all 90). On and an arrow among the
letters, a contrast (`name-arrows-edit` against `name-arrows`: 77 of 88
checkpoints equal, divergent from the first arrow the editor reads). Off, the
engine is not consulted (§7). Unit: `SeamEditKeys.*`; stand-ins:
`edit_keys_probe_off`, `edit_keys_probe_on`.

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
