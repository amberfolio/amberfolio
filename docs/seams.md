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
- [3b. A point at a moment](#3b-a-point-at-a-moment)
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
| `programs` | the points on a program whose points are not `points`, one entry per such program (schema 9); `points_on()` picks by the running program. The debug cheats use it for Curse of the Azure Bonds |
| `trigger` | whether this seam is **pulled** rather than left on (§3a) |
| `gate` | a document the player must present before it arms (§5); unused by every shipped seam |
| `group` | seams that are **alternatives** share one: enabling one disables the others (§6); the text faces |
| `setting` | the one **number** a player may choose (§6): a name, a unit, bounds, a step and an initial; `message-pause`'s length |
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
  The registers come back, the program does not: a call abandoned
  part-way leaves what it began (an overlay entered, a prompt up), and
  the run is corrupt from there. The budget catches a wrong fact table;
  a batch must never be left waiting.
- **The player's keys wait for the batch.** `machine::step()` leaves host
  key events in their queue while the engine is `calling()`, and the
  engine hands them over as the batch finishes, before its point is
  offered again. The program's read keeps the last key waiting and drops
  the rest, so a key let in mid-batch replaced the one a seam had put
  there for the routine it called: the Fix's target picker then waited
  for a key nobody knew to press, the budget abandoned it inside the cast
  overlay, and the screen filled with garbage and asked for a disk that
  does not exist (`SeamCallProgram.HoldsTheHostsKeys…`). A seam that is
  off has no batch, so this never touches the plain machine.
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

## 3b. A point at a moment

A point that says `seam_point::at_moment` (#538) has no address. It is
offered when a moment of virtual time its seam asked for comes, for a seam
that plays something out over time: a program waiting for a key inside the
BIOS reaches no address of its own, so no address can pace it.

- A handler asks with `seam_context::offer_at(moment)`; one moment per
  seam, a later ask replacing it, `withdraw_offer()` dropping it and
  `offered()` saying whether one is outstanding.
- The point is offered **once**, at the first step boundary at or after
  the moment, the moment spent before the handler runs. A handler that
  wants another asks for it.
- `seam_context::moment()` is the moment asked for, not the tick reached,
  the scheduler's rule (`scheduler.h`): a seam that asks for its next
  moment from the one it was handed never drifts, however coarse a step.
- **It is at no known instruction.** Its handler writes only what the
  program is known not to be writing (a port nothing else drives), reads
  what it must, and asks for its next moment; `call_program` and
  `place_bytes` answer false there.
- It counts no `reached`, like a point with no address; `fired` counts
  each moment served.
- The moment is configuration: `reset()`, `clear()`, `enable()` and
  `disable()` drop it, and the serialization never sees it.
- **A stop** (`seam_definition::stop`, #540). Between its moments a seam
  can be holding something the program is not, a chip still sounding,
  and a seam switched off has no points left to put it back with. So a
  definition may name a stop, and a `disable()` that finds a moment asked
  for owes it: run once, at the next step boundary, whatever is running,
  with what a moment's handler has. It is the one handler a seam that is
  off runs. `fired` does not count it, and a reset drops it, the devices
  going back to power-on with the machine.
- **Cost**: `machine::step()` asks `due(now)` beside `wants(at)`, one
  comparison against the earliest moment any seam has asked for; with
  none asked it is false at every tick. A stop owed is due now.

Tests: `SeamMoment.*` (`tests/core/machine/seam_moment_test.cpp`),
`SeamMoment.ServingOneThatWritesNothingLeavesTheRunIdentical` the
fidelity one, `SeamMoment.SwitchedOff*` the stop's.

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
resident; a point whose module is out does nothing. A seam is **armed**
while at least one module its points name is resident, because each
point acts on its own (#477); while none is, it is **on and inert** with
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
`seam_module::load_segment_at` is the offset of that word. The combat
overlay, as the program profile states it:

    inline constexpr seam_module combat{
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
| cost | a bit test | a nibble test and, one step in about three, a word of RAM |

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

**The program profile** (`machine/program_profile.h`, #552) is one
table per program image of the facts more than one file acts on: the
data segment, the game mode byte and its values, the view kind, the
party's records and pointers, the area record, the bar highlight, the
keyboard's pushback slot, the font pointer, the resident routines a seam
watches or calls (the keyboard's two entries, the clears, the frame and
string drawers, the command-bar thunk, the roster drawer and the screen
composer), the overlay modules with their load-segment words, and the
menu-bar routine's and the adventuring loop's shared offsets. Each is
stated once. Pool of Radiance has a profile (`pool_of_radiance`) and
Curse of the Azure Bonds a partial one (`curse_of_the_azure_bonds`, the
members its ported seams and reads use); `find_program_profile()` answers
null for anything else.

A seam's `fingerprints` are the fingerprints of the profiles its facts
are about (`fingerprints_of()`): each seam file names its program once
(`constexpr const program_profile& profile = pool_of_radiance;`) and
reads the shared facts through it. A seam's own facts, the ones no other
file reads, stay in its own file beside its mechanism, one row per
program. The scene and screen-text reads pick their row the same way and
answer `wrong_binary` for a program without one. `ProgramProfile.*` in
`tests/core/machine/program_profile_test.cpp` checks that every seam is
available only on programs that have one.

**One program image, two releases, every seam valid on both** (#396).
The release sold on GOG and Steam and the third-party repack the facts
were gathered on boot the same START.EXE; their requirement rows are
`por-store` and `por-archive` (`docs/hosts.md` §5). Every file the two
share is byte-identical but GAME.OVR, and GAME.OVR differs in two bytes,
at file offsets 3288–3289, inside overlay 2 (code at file offset 2476,
862 bytes), the copy-protection overlay. None of the modules a seam
pins by digest (§4, the program profile's) is overlay 2, and the one seam on that path is
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
function `status()` and `arm_all()` share, beside `some_module_resident`.

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
| `on` | enabled; `armed` says whether any point can act, `reason` says why none can |
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

**A number** (`seam_setting`, #531). A seam may take one: a name, a
unit, a minimum and maximum, the step a panel's arrow moves it by, and
the value it starts at. `set_setting()` refuses a value outside the
bounds rather than clamping it, `setting()` answers what was set or the
initial, and a handler reads its own through `seam_context::setting()`.
It is configuration as the enable is, and more durable: a person chose
it, so `clear()` and `reset()` leave it where it was, the way they leave
an answered code wheel. Set or not, a seam that is off is not consulted,
so the number moves nothing until the seam is on. A recording carries it
on the seam's line (`docs/replay.md` §1).

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
   when something is armed and the step filter (`wants()`, seam.h "The
   cost when it is off") says a point could be at that step. The filter
   is rebuilt from the armed table wherever the table or a latch changes,
   may say yes to a step nothing happens at, and never says no to one
   `dispatch()` would act at; a load-segment point is re-read from RAM at
   every step it could match, never cached.
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
| `quiet-all` | identical | `quiet-journal` (every seam that draws nothing until used: not the faces or `modern-controls`, which are contrasts of their own) |
| `quiet-all-on-sight` | contrast | `quiet-all` (the same with `modern-controls` on: the menu's cursor is the first thing to move) |
| `quiet-modern-controls` | contrast | `quiet` (the menu's cursor from the first frame the menu is drawn, the party's bar in menu mode and every highlight a yellow block from the first time each is on the screen) |
| `list-down-arrows` | contrast | `list-down` (the same script with Down and Up, which the seam-off program drops) |
| `camp-down-arrows` | contrast | `camp-down` (Down and Up at the camp bar, which the program hands to the party cursor to put the first member back) |
| `camp-pad-arrows` | contrast | `camp-pad` (the keypad's 2 and 8 with Num Lock on at the camp bar, which the routine translates into letters the cursor puts the first member back on) |
| `bar-enter-keys` | contrast | `bar-enter` (Return at the camp bar takes the highlighted command) |
| `bar-yn-keys` | contrast | `bar-yn` (a Right at a bar that is not raw, and Return at the Yes/No prompt) |
| `bar-camp-keys` | contrast | `bar-camp` (a Right at the camp bar, which hands it to the party cursor, and Return) |
| `list-keys-yellow` | contrast | `list-keys` (each pick-list's highlighted row a yellow block) |
| `menu-letters-cursor` | contrast | `menu-letters` (the cursor is drawn as soon as the menu is, with no key pressed at it) |
| `menu-down-cursor` | contrast | `menu-down` (Down, Down and Return at the main menu take the third command) |
| `move-walk-mode` | contrast | `move-walk` (Return takes `Move`, a step on the walking bar, Escape back to the menu, two Rights to `View` and Return opens it, where the program walks and turns) |
| `name-arrows-edit` | contrast | `name-arrows` (a Right, an Up, a Down and a Left among the letters of a name: the program types a letter for each) |
| `menu-letters-quick` | identical | `menu-letters` (the party menu clicks no footstep) |
| `walk-quick` | contrast | `walk` (the first footstep goes on without its wait) |
| `quiet-message-pause` | identical | `quiet` (no message is shown, so neither point is reached) |
| `fight-pause` | contrast | `fight` (the pause that opens the battle, and every combat line after it, stand longer) |
| `curse-quiet-message-pause` | identical | `curse-quiet` (no pause is reached) |
| `curse-fight-pause` | contrast | `curse-fight` (the pause that opens the brawl, and every combat line after it, stand longer) |
| `quiet-fanfare` | contrast | `quiet` (the piece from the end of the boot's loading on: the chip's writes, and its registers while it plays) |

**A built-in fix has no pair** (`built_in_fixes()`, seam.h). It is on
with its program and listed nowhere: no panel, config file, flag or
recording names one, and `enable()`, `disable()` and `pull()` answer
`unknown_seam`. So there is no "off" to record a baseline in, and the
claim is the stronger one: every session in `tests/sessions/`, all made
without it, verifies with it on, so it is `identical` to the plain
machine wherever it finds nothing to fix. Reserved for a failure no
player could want; a fix that changed a run a player might prefer is a
seam.

**`modern-controls` has no `identical` pair.** Four of its pieces draw as
soon as there is something to draw on (the main menu's cursor, the roster's
numbers there, a highlight, the party's bar), so a run with it on differs from the plain run at
the first main menu whatever keys are pressed, as a face does. Every pair
above with the seam on at one end only is a `contrast` that agrees with its
baseline up to that frame (58 checkpoints) and differs from there to the
end; what a key did is in the state the run ends in and in the unit suites.
The pair with the seam on at both ends (`door-pick-3`) differs by its keys
alone, and is the sharp one.

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
- **Where a fact lives.** A fact another file already reads, or will, is
  the program profile's (`program_profile.h`, §5): add it there, with
  what it means, and read it as `profile.<name>`; never copy its value
  into a second file. A fact only this seam reads stays in the seam's
  own file. A module is always the profile's, with its load-segment
  word.
- **Check every fact twice, by two routes.** A load-segment word is found
  by searching the resident image for the manager's record with the
  module's file offset and length and taking the word sixteen bytes in
  (`program_profile.h`); the check is that the search has exactly one
  match.
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
  like a corrupted machine (automap, #173). **An overlay's stub entry is
  the strictest case**: the manager's trap reads the stub record at
  `CS:0`. At the image base the entry still runs while its overlay is
  in memory, because it is then a far jump, and a test or a fresh run
  passes; once the overlay is evicted the manager reads the image's
  first bytes as the record, writes jump entries over its own stub
  table, and the machine stops in garbage a moment later. Call an entry
  at the paragraph its record begins at (Encamp Fix, #526).

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
| `roster-fix` | **built in, always on, not listed** (§7): a character the roster names and the save directory does not hold is left off the Add list, instead of the program asking for a save disk for ever | overlay 17 (the roster list routine) |
| `timer-fix` | **built in, always on, not listed** (§7): the sound driver never hooks the timer over its own hook, which a click quick steps did not wait for leaves it about to do | the resident image |
| `automap` | a map of where the party has been, over the roster, on **Tab** | the resident image |
| `journal` | what the game cites goes on a list; **Notes** on the party's own bar opens it on the game's screen, out of the player's ingested journal. On Curse, the list alone (below) | the resident image, and the adventuring loop's module |
| `explored` | fog of war on the overworld map: a black checker over every square the party has not stood on; a setting, no key | the resident image |
| `modern-controls` | the controls a later game has, nine pieces in one seam: Up and Down step the pick-lists and the selected party member; Left, Right, Enter and Esc work at the command bars; walking is a mode the party's bar starts with `Move` and ends with `Exit`, `Area` gone; the main menu has a cursor, and its number row's 1 to 8 select a party member with each number shown; the arrows no longer type letters at a text prompt; every selection is a yellow block with its key letter a white one; and it notes when the program is reading a line of text and where the combat aim bar keeps its cursor, for a host to ask (§10) | overlay 25 (the menu-bar and list routines), overlays 13, 14, 16 and 19, and the resident image |
| `quick-steps` | a turn, and the next step, do not wait for the footstep's click; the click still plays, and a step's two clicks keep their spacing | the resident image, and the call sites in overlays 14 and 3 |
| `message-pause` | a message the program moves on from by itself stands at least the seam's number of milliseconds, 1500 unless the player chose otherwise, and any key ends it; the encounter menu waits as long before it wipes a line the script just printed | the resident image, and overlays 3 and 31 (on Curse, 3 and 33) |
| `fanfare` | a piece written for this project plays on the Tandy chip from the end of the boot's loading on, paced by a point at a moment (§3b); the program's first sound ends it | overlay 1, the resident image, and a moment |
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

**On Curse of the Azure Bonds** (#555) the challenge is one character
read off a translation wheel, and the routine that asks it is overlaid.
The skip is Pool of Radiance's mechanism with that program's call
sites; the watch is a compare of one character, not of a word. One
`code_wheel_facts` row per program (`seam_code_wheel.cpp`), the points
in the definition's `programs`:

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the protection routine's stub | `0x1C:0x0025` | `0x3F:0x0025` (its record at image `0x03F0`) |
| the routine | in overlay 2 | overlay 4 (file offset 18133, 1096 bytes, load-segment word `0x0400`) |
| the watch | the string compare's `REPE CMPSB`, image `0xBBB0`, against the word table | the routine's `CMP` of AL, the character typed, with the expected one at `[BP-3]`: overlay 4 `+0x03C4` |
| the calls stepped over | the boot's, image `0x0122` | the boot's two, image `0x01F1` (after the first title) and `0x0344` (after the demonstration comes back), and the event-script interpreter's, overlay 3 `+0x3263`, a script's own protection command |

- **The store copy accepts any answer** (PLAN.md §7, M9's first
  question), by its own branch: the jump after the compare goes on to
  the routine's exit when the two characters *differ*. A right answer
  falls through to the program's own `SORRY, THAT'S INCORRECT.` and then
  to the exit as well. GOG and Steam ship the same `START.EXE` and
  `GAME.OVR`.
- **Driven** on the Steam copy at `--speed 386`, `--wall none`: with the
  seam on and `A` typed, `fired=2` (the boot's call and the compare) and
  nothing latched; with the expected character typed instead, the
  refusal line, the latch, `code wheel answered - remembered for this
  copy` and the store's `answered` line for the digest; answered, the
  boot goes from the title to the party menu with `fired=1` and no
  challenge drawn.
- **Not driven**: the interpreter's call. No leg reaches a script that
  asks. Its operand is checked before the step, so a wrong fact declines.
- **Fidelity**: `curse-boot-wheel` `identical curse-boot` (on, a wrong
  letter typed, every one of 31 checkpoints equal);
  `curse-boot-answered` `contrast curse-boot`. Tests:
  `SeamCodeWheelOnCurse.*` in `tests/core/machine/seam_test.cpp`.

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
- **A cure cycle** (#490): with somebody hurt and somebody who can act
  holding cures **pending** (the ones the command queued back, or the
  player's), in an area that allows casting, the rest dials **no days**,
  so it lasts the program's own memorization time, and the camp menu's
  next pass posts the Fix's own key (scan `0x21`, `F`) for the bar it is
  about to draw: the command goes on and casts what the rest memorized.
  It goes on only while somebody is still hurt, somebody who can act
  holds a cure ready and nobody who can act still holds one pending;
  otherwise the command is reported as any rest is. A cure still pending
  is a rest the player ended with the program's own `STOP RESTING?`
  before the memorizing was done (with two casters the quicker one's
  cures are back by then), and is `Fix: Rest Stopped`. A key the player
  types while the Fix casts stops it at the next cast, `Fix: Stopped`,
  because the key waits out the cast's batch (§3). Backstops: sixteen casts between two rests, twenty-four cycles a
  command. Driven on slot A wounded by the cheat: `HEALED 193 HP, 43
  SPELLS, 1:22:45`, where resting the deficit out took 34 days with 5.
  Two of the seam's words (§3): casts since the last rest, and the
  cycle count with the cure-cycle rest in its top bit.
- **Days**, when no cure can come back: the worst survivor's deficit plus
  one (the heal counter is zeroed on camp entry and not by a rest, so a
  second rest starts mid day); **zero when there is no deficit**, which
  leaves the program's own duration. With nothing to rest for the command declines. **An
  empty spellbook adds to neither half** (#350): nothing is cast, and
  the program's wrapper computes no memorization time, so the rest is
  the deficit plus one over `00:00` — which for a party a fight left
  low is days, and days of camp are what the area's wandering-monster
  check is rolled against.
- **Cures**: only Cure Light Wounds a member holds **ready** is cast;
  one is queued back **before** each cast by the memorize command's own
  two writes (the slot gets `id | 0x80`, then the program's slot sort),
  so the party is never a cure down. The cast goes through the driver
  the camp's own Cast command calls, through its overlay's stub at the
  stub's own segment (image `0x0960`, entry `0x39`; §8.4, #526): the
  seam positions its target anchor and answers its one prompt with one
  key. One act per arrival;
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

### The roster fix

Not a PLAN.md §5 item: a fix for a way the program hangs that a player
on a plain directory cannot get out of. **A built-in fix** (§7): on with
the program, in no listing, and nothing to toggle.

**What the program does.** `CHARLIST.TXT` in the save directory names the
characters in no party; each name's record is `<STEM>.CHA` beside it, the
stem being the name with ten characters taken out, cut to eight and
upper-cased. Loading a saved game unlinks the `.CHA` of every member it
restores, and the list is pruned only the next time the program writes it,
of names that match a party member's in full. So the list can name a
character whose file is gone, and two names with one stem share a file.
(A)dd on such a name opens its file through the program's own
open-or-ask routine, which asks for the save disk for as long as the file
is not there.

**Point**, in overlay 17 (file offset 122854, 9189 bytes, load-segment
word `0x07D0`, the one match of the manager's-record search):

| point | what the handler does |
| --- | --- |
| `0x0191`, the list routine's one exit (`mov sp, bp`) | walks the list from the caller's head (far pointer at `BP+6`; 46-byte nodes, the name a Pascal string at `+0`, the next at `+0x2A`); asks of each name not marked `*` (in the party) whether `<save directory><stem>.CHA` exists, canonicalized as INT 21h would; unlinks the first that does not and queues the program's FreeMem on its node. The batch re-offers the point, and the next arrival takes the next |

- **Facts read out of the machine**, not written down: the save directory
  (DGROUP `0x537A`, a Pascal string) and the ten characters a stem loses
  (DGROUP `0x0D1A`). FreeMem is image paragraph `0x0AF8`, offset `0x0254`:
  pointer segment, pointer offset, size; `retf 6`.
- **Both callers** of the list routine go through the point: (A)dd, and
  the character generator, which appends the new name and writes the list
  back. Each writes the list without the names the seam left out, so
  `CHARLIST.TXT` comes to say what the directory holds.
- **Refuses**: a frame, node or list it cannot follow in conventional
  memory, a name longer than a node holds, a list longer than any the
  program could build, a save directory that does not resolve to a
  directory, no filesystem. The whole list is walked before anything is
  touched.
- **Rejected**: refusing the pick with a line on the message row, which
  leaves the list as it is. The program's line-and-a-key waits for a
  person, which a batch's step budget cannot hold, and "not there" cannot
  be a store still filling: a host puts every file on the machine before
  the first instruction.
- **Fidelity**: over a roster with every file in place it reads and writes
  nothing of the program's: every session verifies with it on, `party`
  among them, which reaches it twice. `roster-add` holds what it does over
  a stranded name.
- **Open**: nothing.

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
| the menu-bar routine's thunk | resident image | which bar is going up: the party's bar is a far pointer into the data segment at one of two offsets, or a call from the adventuring loop's own two call sites (the far return on top: overlay 14 `0x09D5` or `0x0C45`, in the segment the manager's word `0x730` names), which is what says so when `modern-controls`' move mode hands those calls a copy on the stack; every other bar is a stack copy. While it is not the party's bar the panel comes off the screen (if really up), is not drawn, and Tab is not this seam's; it stays open, and is drawn again when the party's bar is back — after an encounter fled, a vendor's question, a script's menu (M5-E2d) |

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
  binary the table was not derived from. The face, door and sight rules
  and the zone table are `core/src/machine/automap_faces.h`, which the
  scene read (`docs/hosts.md` §13, #520) calls too, so a host is told
  what the panel draws.
- **Zone label** (M5-E2b): the band eight columns right of the cells, in
  the program's 8x8 font (a far pointer in the data segment, sixty-four
  glyphs indexed by the character upper-cased modulo 64) rasterized into
  the buffer, from a table of (disk, area) to a label; no row prints
  `AREA <n>`; `|` in a name is a soft break for a word over eight
  columns. A sub-map a script swaps in keeps its parent's label, except
  where it is a district of its own: a second table keyed (disk, area,
  geometry block) is looked up first, and names disk 1, area 24, block
  31 the Wealthy Area rather than the Temple of Bane. Before both, a
  wilderness area's own grid (disks 6 to 8, areas and blocks 25 to 27)
  is named by the lair the party stands in: each grid is several lairs
  walled off from one another (four, three and four), and a lair is the
  squares reachable from the entrance square its script puts the party
  on, across faces that are absent or not solid on either side, with the
  grid wrapping as the program's coordinates do. A region with no
  entrance or more than one in it keeps the area's label. The same
  region is all of the grid the panel draws: what was explored of the
  area's other lairs stays in the record, keyed by the grid, but is not
  drawn while the party is in this one; a region with no one lair in it
  draws the whole grid. The party's square is in the drawing signature,
  so a step that crosses into another lair re-letters the band. A hyphen in a name is a break point
  of its own, so `THRI-KREEN` sets as `THRI-` over `KREEN`. A zero font
  pointer draws nothing and the font's segment is in the drawing
  signature.
- **Give-back**: one batch of two calls, the region clear over the panel
  rect then the roster drawer with the current member. Rejected: the
  per-mode screen composer (`screen_redraw()` in the proven design),
  because it repaints the viewport over a vendor mid-question (M5-E2d).
  The routines must be called at the paragraph they were linked at
  (§8.4).
- **A roster drawn over the panel** (#555): the drawer clears only the
  rows it writes (a row per member and one more), so a screen a script
  draws with the roster on it (a temple's, a shop's) would leave the map
  above the header, below the party and beside the header's letters. A
  clear that meets the panel without covering it, while the panel is on
  the screen, leaves it stranded (`automap_state::stranded()`); the
  drawer's return over that owes the give-back, made at the next poll
  under a bar that is not the party's, in whatever mode the program just
  drew the roster in (a temple is the shop mode, `1`, which the give-back
  otherwise refuses). Any other clear of the cells after the roster, a
  whole panel or screen cleared, a draw and a give-back drop it.
- Rejected as the "whose screen" gate: the program's "a script has the
  message area" byte, because `--watch 84E4` shows it oscillating every
  step.
- Exploration is never gated on whose bar is up.

**State**: `machine::automap()` (a map's fog is thirty-two bytes, and
a seam's own words are twelve), observation beside the overlay tracker:
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

**On Curse of the Azure Bonds** (#555) the screen is laid out the same
way, the viewport left and the roster in the same rect, and the panel is
the same panel. One `automap_facts` row per program
(`seam_automap.cpp`), the points in the definition's `programs`:

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the keyboard's two entries | image `0xA6FD`, `0xA70F` | `0x86EB`, `0x86FD`; pushback DGROUP `0x8CBF` |
| the region clear, frame bottom, right, top, left above the far return | image `0x4047` | `0x17FD`, and a second with the same frame at `0x580E` that text boxes clear through; both clear both pages |
| the full-screen clear | image `0x7D3B` | `0x56E2` (only the titles call it; every full-screen view clears through the region clear) |
| the roster drawer | resident, `0x0BA:0x0767` | overlay 25 (file offset 226981, 13331 bytes, load word `0x0EE0`), `+0x0798`; called through its stub entry at `0x00ED:0x002A`, the paragraph its record begins at (§8.4, #526) |
| roster drawn | its `retf`, image `0x148A` | its `retf 4`, overlay 25 `+0x092E` |
| the menu-bar thunk | image `0x3C7A` | `0x106A` (overlay 27's stub, its third entry); the bar at the same place in the frame |
| the party's bar | DGROUP `0x04B6` and `0x04DF`; calls returning to overlay 14 `0x09D5`, `0x0C45` | one: DGROUP `0x03D8`, the overhead view toggled inside the same loop; the call returning to overlay 15 `+0x092C` |
| an area is up | view kind `1` | area id DGROUP `0x8B78` non-zero (the program is in mode 4 from its titles on), as the scene read says |
| the raw mode's keypad table | DGROUP `0x288C` | `0x27D0`, the same translation, and `\` to `7` |
| shape tiles; first slot, columns, rows | `0x6A58`; `0x0C8C`, `0x0C96`, `0x0CA0` | `0x721C`; `0x0ADA`, `0x0AE4`, `0x0AEE` |
| tile banks, their first codes | `0x5E3A`, `0x2722` | `0x65D0`, `0x2682` (a fifth bank past `0xFF` that no wall row reaches) |
| frame colour, ground colour | `0x6A60`, `0x6A63` | `0x7224`, `0x7227` (dark grey, set by the program on this adapter) |
| places | measured | a door table and the maps' names (`curse_of_the_azure_bonds_places()`); no sub-maps, no lairs |
| door graphics (disk, WALLDEF block, rows) | 19 rows | 17 rows: disk 2 blocks 1 (`0x10`), 2 (`0x02`), 3 (`0x01`), 4 (`0x10`); disk 3 blocks 3 (`0x01`), 5 (`0x14`), 6 (`0x10`), 7 (`0x12`); disk 4 blocks 3 (`0x01`), 8 (`0x18`), 9 (`0x10`), 10 (`0x10`), 11 (`0x14`); disk 5 blocks 8 (`0x18`), 12 (`0x18`); disk 6 blocks 3 (`0x01`), 13 (`0x10`) |
| a door on the party's square | under the mark | over the mark, under its white (`doors_over_party`) |

- **The door table** is measured as Pool of Radiance's is: every shut
  face on the sixteen grids (`GEO2` to `GEO6`), and the wall set each
  grid's script loads with it (the script's load of the three slots, read
  per area: Tilverton's grid 1 with blocks 1, 2 and 3). A block whose
  WALLDEF rows and tiles are the same bytes on several disks is the same
  pictures there (block 3 on disks 2, 3, 4 and 6, block 8 on disks 4 and
  5), so a row seen shut on one is a door on each: block 3's first row,
  Tilverton's arched doors, the temple's among them, is never shut on
  its own grid and is shut on disks 3 and 4. A row whose faces are
  mostly solid walls with one to three of them shut is a wall standing
  in for a hidden door, and is left out: block 7's fourth, block 9's
  first two and block 10's third (passable sixty times). Kinds seen shut on the loaded map are
  doors as well, as on Pool of Radiance. Every other way through is a
  passage: block 1's first three rows, block 2's first and block 3's last
  on Tilverton's grid.
- **A door on the party's square** is drawn again after the party's mark,
  where the mark's black square had covered the leaf's inner row, and not
  over the arrow's white. Pool of Radiance's panel keeps its own pixels.
- **The green arch** is the entrance mark (`automap_marker::entrance`):
  the square the party first stood on after its map changed under it.
  Neither a load nor a save makes one.
- **Names**: a table keyed (disk, area), as Pool of Radiance's, with
  `AREA <n>` for a grid that has no row. The names are the area names of
  GameBanshee's Curse of the Azure Bonds walkthrough
  (`https://www.gamebanshee.com/curseoftheazurebonds/walkthrough.php`,
  one page per area at `.../walkthrough/<name>.php`), nominative facts
  as the wilderness lairs' are. Each area is matched by its page's
  numbered places against what that area's script does there, and by
  which areas its exits lead to:

  | label | disk, area (grid) | the page's places it was matched by | page |
  | --- | --- | --- | --- |
  | `TILVER\|TON` | 2, 1 (1) | the inn, the weapon shop, the Temple of Gond, the sage, the carriage at the gate | `tilverton.php` |
  | `THIEVES' GUILD` | 2, 2 (1, the same grid) | the guild east of the town on Tilverton's page: guildmaster, guest book, kennel, treasure and monkey rooms, the passage to the sewers | `tilverton.php` |
  | `TILVER\|TON SEWERS` | 2, 3 (3) | the otyughs, the cloth, the secret training hall, the passages to the guild and the hideout | `tilvertonsewers.php` |
  | `FIRE KNIVES HIDEOUT` | 2, 4 (4) | the blade barrier, the torture chamber, the office, the hospital, the armory | `firekniveshideout.php` |
  | `YULASH` | 3, 16 (16) | the destroyed checkpoint, the Zhentrim battle, the one-way entrance to the pit | `yulash.php` |
  | `PIT OF MOANDER` | 3, 17 (17) and 3, 18 | the collapsing way in; the dead Zhentrim, the vegepygmies, Alias, Mogion | `pitofmoander.php` |
  | `ZHENTIL KEEP` | 4, 32 (32) and 4, 35 | the inn and magic shop; the courthouse and the arena | `zhentilkeep.php` |
  | `TEMPLE OF BANE` | 4, 33 (Zhentil Keep's grid) | the temple's places on Zhentil Keep's page, entered from area 32 | `zhentilkeep.php` |
  | `BEHOLDER CAVE` | 4, 34 (33) | Dexam, the otyughs, the minotaurs and priests, the manticores | `beholdercave.php` |
  | `OXAM'S TOWER` | 4, 37 (37) | the medusas, the gas spore, the dark elf lord, the conference | `oxamstower.php` |
  | `HAP` | 5, 49 (50) | the inn, the general store, the barn with the efreeti | `hap.php` |
  | `DRACO\|LICH CAVE` | 5, 50 (50, the same grid) | the cave on Hap's page between the town and the tower, its dracolich | `hap.php` |
  | `WIZARD'S TOWER` | 5, 51 (51) | Dracandros, the roof | `wizardstower.php` |
  | `BURIAL GLEN` | 6, 64 (64) | the elfish spirit, the red web, the thri-kreen; its exit east to area 66 | `burialglen.php` |
  | `RUINS OF MYTH DRANNOR` | 6, 66 (66) | Tirsheya, the rakshasas; its exits to areas 64 and 67 | `ruinsofmythdrannor.php` |
  | `GRAND RUINED TEMPLE` | 6, 67 (67) | Tyranthraxus, the chapel | `grandruinedtemple.php` |

  **Left at `AREA <n>`**: the three grids a search beside a town
  reaches (the wilderness page's search sites), because each is several
  pages' place: disk 3 area 21 is Voonlar's war-blasted section and
  Phlan's ruins, disk 5 area 53 the caverns beside Shadowdale,
  Ashabenford and Essembra, and disk 6 area 69 the ruins beside Hillsfar
  and Teshwave. Disk 5 area 48 is a step between areas with no grid of
  its own.
- **Not ported**: the overland. Curse's is a journey menu, not a grid,
  so the poll records nothing there.
- **Driven** on slot A (`docs/playable-curse.md` leg 2) with Tab at the
  opening room: the panel at `7,13 N` shows a wood wall north and south,
  stone east and a door leaf west, as the 3D view does; on the street
  the floor is the view's dark grey, the shop's doorway at `3,12 W` a
  way through. The `PRESS RETURN` of the innkeeper's two screens and of
  both signs, the shopkeeper's `YES NO` and the shop's bar each take it
  away with the
  roster drawn by the program's drawer, and the party's bar brings it
  back. Tab again on the street puts the roster back with the current
  member white; End with the panel up is taken. The label is
  `TILVERTON`, set as `TILVER-` over `TON`.
  With `--save-sidecars`, a save to slot C writes `AFMAPC.DAT` and
  loading C reads it back (`docs/hosts.md` §6).
- **Driven** from a slot A saved in play at `5,7 N` in Tilverton: the
  temple's door at `4,7 W` is a leaf; at `5,3 E` the door on the north
  wall of the party's own square is a leaf two pixels deep over the mark;
  at the temple's altar (`0,7 W`, `HEAL VIEW POOL APPRAISE EXIT`) the
  roster is the program's, nothing of the map left, and `HEAL`, `VIEW`,
  the arrows, Tab and Escape all answer there, with every seam on.
- **Fidelity**: `curse-quiet-automap` `identical curse-quiet` (on, Tab
  never pressed, every one of 58 checkpoints equal); `curse-town-map`
  `contrast curse-town` (Tab at frame 4,000 ahead of the Left there; 40
  of 90 identical); `curse-temple-map` `contrast curse-temple` (the same
  Tab, the arched door at `4,7 W` and the temple's screen given back
  clean; 40 of 107 identical, divergent from tick 79,563,886). Tests:
  `SeamAutomapOnCurse.*`
  (`tests/core/machine/seam_automap_curse_test.cpp`).

### The journal reader (#175, #221, #222, #232, #305, #328, #346)

PLAN.md §5 item 2's in-game half; ingestion is #174 and
`docs/journal.md`, and §9 there is the door between the halves.

| point | module | what the handler does |
| --- | --- | --- |
| the automap's five: both key routines, both clears, the roster drawer's `retf` | resident image | as the automap's: the screen is drawn and repainted here, and the reader's modal keys are claimed here |
| the program's word-wrapping **message box**, where a script's every PRINT ends | resident image | the citation watch: reads the Pascal string (offset at SP+4, segment at SP+6) into a rolling window; the box's home-and-clear flag is the message boundary |
| the adventuring loop's call into the menu-bar routine, one pair per view mode (M5-E4a, #221) | the adventuring loop's module | before: splices `Notes` onto the party's bar, sets `bar_live()`; at the return: splices it out, clears `bar_live()`, reads the letter from `AL` and the routine's out-parameter, puts the highlight byte back where the routine found it (#330) |

Rejected as the watch: the string drawer the Encamp Fix calls (the
profile's `draw_string_entry`), because on this program it draws credits, menus and the
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
- **The bar's highlight, with `modern-controls` on** (#471, #473). The
  reader draws and answers bars of its own, so the program's menu-bar
  routine never sees a key pressed at them and the seam's point there cannot
  step them. The reader asks the engine whether `modern-controls` is on
  (`modern_controls_on()`, `seam_builtin.h`) and, with it on, keeps a
  highlight of its own:
  `journal_state::bar_word()`, which of `NEXT`, `PREV` and `EXIT` it is on,
  and `bar_focus()`, whether the listing's has been taken up. Both are
  observation, kept in the state object and reset when the reader's mode
  changes, never in a handler. A word a screenful's bar has dropped is
  resolved to the first it has, so a highlight on `NEXT` that turns to the
  last page lands on `PREV`. The highlight is drawn by the same bar pass as
  everything else: the word in the bright end to end and, in yellow over
  that, its tail, with its key letter left white, both with
  `selection_mark` in the colour word so that the word is drawn as blocks
  (#483), a bar of one word included: it is what Return takes. A change
  of highlight draws the bar alone: the signature is zeroed and the rows
  already on the glass are not painted again, except on a picture, whose
  second pass is the picture and not the bar.
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
three. **With `modern-controls` on** (#471) Left and Right step the bar's highlight
(wrapping, over the words the screenful's bar has) and Return takes the lit
command: `NEXT` and `PREV` turn the screenful, `EXIT` is Escape. A page's
bar is lit from the moment it is drawn. **The listing's is not**, because
Return there opens a row and that stays: the first Left or Right lights the
bar's first word and Return is the bar's from then on, Up and Down give
Return back to the rows, and an empty log, which has no row to open, is lit
from the start, so Return on the lone `EXIT` closes the reader. Left and
Right with the seam off stay swallowed. The log and a page take **every** key (the bar is live underneath,
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
the splice is the whole of what it is contrasting on. With `modern-controls`
off the reader is as it was, byte for byte (the sessions
below replay unchanged); `JournalBarKeys.*` covers the keys, the highlight
and its colours, and a stand-in that logs every call on the bar's row.
Exercised sessions:
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

**On Curse of the Azure Bonds** (#555) the seam is the same mechanism
with that program's facts, one `journal_facts` row per program
(`seam_journal.cpp`) and its eight points in the definition's
`programs`: the citation watch, `Notes` on the party's bar and the reader.
The log reaches the slot's sidecar when the program saves
(`docs/journal.md` §6a). The facts more than one file reads are the
profile's (`program_profile.h`).

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the message box | image `0x77F8` | image `0x5178` |
| its frame: string offset, segment, the clear flag | SP+4, SP+6, SP+8 | SP+4, SP+6, SP+8 |
| the flag is read as | a word | **a byte**: the routine compares the byte, and the script's print command pushes it as `mov al, n` and `push ax`, AH left over |
| a number the script prints | the same box, flag clear | the same box, flag clear |
| the party's bar | two, DGROUP `0x04B6` and `0x04DF` (33 and 27 characters) | one, DGROUP `0x03D8` (33 characters); the overhead view is toggled inside the same loop and keeps it |
| its call and return; the out-parameter | overlay 14 (word `0x730`): `0x09D0`/`0x09D5` and `0x0C40`/`0x0C45`; `BP-4` | overlay 15 (word `0x9A0`): `0x0927`/`0x092C`, a far call through the menu overlay's stub entry; `BP-4` |
| the highlight | DGROUP `0x6B2B` | DGROUP `0x72AE` |
| the frame | a frame drawer that takes the box, a style, a colour and the title, image `0x41F8`, after the region clear | **none that takes a title**: the fixed outer frame, image `0x1330` (`0133:0000`), which takes nothing, clears rows 1 to `0x16` and columns 1 to `0x26` (the reader's box) and draws the border around them; the title is a string-drawer line on row 1 at `(1 + 0x26 - length) / 2`, where a titled frame centres it |
| the string drawer and what it is pushed | image `0x76B6`: column, row, colour, the string | image `0x5029` (`04CE:0349`): column, row, colour, **a background colour**, the string; the reader hands it black. Both colours are read as bytes |
| the screen composer | resident, `0BA:27D9` | overlay 25 (file offset 226981, 13331 bytes, word `0x0EE0`) `+0x2A29`, called through its stub entry at the stub's own paragraph, `00ED:00CF` (§8.4, #526); in the adventuring mode it draws the outer frame and the viewport box, the view, the roster and the status line |
| the roster drawn | its `retf`, image `0x148A` | its `retf 4`, overlay 25 `+0x092E` (the automap's point) |
| the keyboard; the pushback slot | image `0xA6FD`, `0xA70F`; DGROUP `0x8501` | `0x86EB`, `0x86FD`; DGROUP `0x8CBF` |
| the clears | `0x4047`, `0x7D3B` | `0x17FD`, `0x56E2` |
| the points | ten | eight: the same in the same order, with one bar call and its return |

Each routine was read twice, from its own code and from its callers in
the store copy's `GAME.OVR`: the string drawer's frame (the string at
`BP+6`, the background at `BP+0x0A`, the colour, row and column above
it) and 180 far calls to `04CE:0349`; the outer frame and 15 far calls
to `0133:0000`, the composer's among them; 29 far calls to `00ED:00CF`,
whose stub entry names offset `0x2A29` of overlay 25, where the
composer begins. The menu-bar routine upper-cases the key
it reads and matches it against the bar's capitals, so `Notes`, one
capital, is a seventh command and `N` selects nothing on the program's
own bar.

- **Driven** on the Steam copy, slot A, after the innkeeper (frame
  5,350): the bar reads `AREA CAST VIEW ENCAMP SEARCH LOOK NOTES`; `N`
  opens the log over the outer frame, `ADVENTURER'S JOURNAL` centred on
  its top row and `* ENTRY 31  01-01  00:01` on its first; `Return` opens
  the entry, `NEXT EXIT 1/3`; `N` and `N` walk to page 2 and onto the
  picture (`PREV EXIT 3/3`), drawn whole in the box; `E` is the log again
  and `E` closes it: the screen the program composes is pixel for pixel
  the one before `N`, the bar back, and a `Right` turns the party.
  `--watch 72AE` reads `01` from the bar's first draw to the end. With
  `modern-controls` on, Left and Right step the reader's bar and Return
  takes the lit word, a white block: on Curse the selection's colour is
  the program's own white (`docs/modern-controls.md`), and the selection
  block turns the mark into a block.
- **Fidelity**: on and `Notes` never chosen, the run is the seam-off run
  until the party's bar is first drawn, as on Pool of Radiance (§7):
  `curse-quiet-journal` `contrast curse-quiet` (39 of 58 identical,
  divergent from tick 78,927,534, the bar after the opening event).
  `curse-notes` `contrast curse-notes-keys` (the reader opened, read and
  closed; the seam-off half presses the same keys; 39 of 78, from the
  same tick), over `tests/visual/curse-reader-store.txt`, entry 31 in
  this project's own words with a synthetic picture. `curse-cite`
  `contrast curse-buy` (on, entry 31 cited, the purchase and the save
  the same; 39 of 133, from the same tick).
  `curse-journey-journal` `identical curse-journey` (on, the box reached
  288 times, nothing cited; the overland is a journey menu and the
  party's bar is never drawn). Every run from slot A passes the
  innkeeper, so no `curse-quiet` twin is a run with nothing cited. Its
  save to slot C with `--save-sidecars` writes `\SAVE\AFSEENC.DAT`, one
  row, entry 31 unread, and loading C reads it back. Tests:
  `JournalOnCurse.*`, `JournalReaderOnCurse.*`.

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
| the window painter's per-square blit thunk | resident image | on the travel view, the program has painted its map back, so a picture a script left there is gone |

| fact | value |
| --- | --- |
| the screen | mode byte 3, view kind 2, 3 or 4 (one per wilderness area) |
| the party's position | two words in the **area record**, not the data-segment bytes every other screen uses (those held `0x0B` and `0x0D` while the status line printed `3, 32`) |
| an area | 16 columns by 36 rows; the three are bands of one 44-column terrain table at biases the program keeps per view kind, read from the program |
| the window | 120 by 120 pixels at (8, 8); a square 24 by 24, three whole bytes wide; one move repaints exactly `8,8,127,127` |
| the fog | a one-pixel checker of palette index 0 on half of a square's pixels; colour from set/reset, bit mask alternating `0xAA` / `0x55` by scanline; the parity is the screen's, so it runs unbroken between squares; a masked write, so each byte is read then written (§3) |
| `explored_reveal_radius` | Chebyshev **0** (`machine/automap.h`) |
| the record | where the party stood; the reveal is derived at draw time, so the radius can change without touching a sidecar |
| never covered | the party's own square; any pixel outside the window; a window a script holds, while `0x84E0` (a picture is in the view), `0x84C8` (a figure the message printer drew) or `0x84C9` (the picture opcode's open byte) is set, and until the program paints its map back after one was (its window blit thunk, image `0x3EBA`, on the travel view), since the end of an event clears them without a redraw and the party's bar comes back with a present (#535, #545) |
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

### Modern controls (#423, #425, #434, #439, #453, #455, #473, #479)

`seam_modern_controls.cpp`: arrows, Enter, Esc and the number keys at
every list, bar and prompt, a cursor on the main menu, the walking mode
on the party's bar and a selection block instead of a colour. Its fact
tables, on Pool of Radiance and on Curse of the Azure Bonds, are
[`docs/modern-controls.md`](modern-controls.md), a document of its own
because it is as long as the rest of this section together.

### Quick steps

Not a PLAN.md §5 item: a player's request, after profiling a step at the
386 governor. **A turn spent six of its eleven frames between the key
and the new view in a busy-wait**, and a step forward two of them, one
before the redraw and one after it. The wait is the sound driver's, and
it is for the footstep's click.

**What the program does.** Every sound starts at the driver's dispatch
entry, image offset `0x8BBC` (`push bp; mov bp,sp`, the far return at
`[BP+2]`, the request word at `[BP+6]`, `retf 2`). Five calls ask it for
the footstep, the word at DGROUP `0x2628`, by the far returns they leave:

| overlay (load-segment word) | return | the call | its wait |
| --- | --- | --- | --- |
| 14 (`0x730`) | `0x0B19`, `0x0B5E` | a turn left, a turn right, before the view is drawn | skipped |
| 14 | `0x0747` | a step's commit, before the redraw | kept: it spaces the step's two clicks |
| 3 (`0x200`, the one match of the manager's-record search) | `0x34D8` | the adventuring loop, after the redraw, when the square changed | skipped |
| 3 | `0x2D91` | an event script's click | kept |
 After starting a request, the
entry tests the driver's busy word, DGROUP `0x5374`; set, it spins in the
driver's calibrated wait for ten hundredths on the DOS clock, then
silences both voice banks and stops the driver. Clear, it goes straight
to its `pop bp; retf 2`.

**Point**, in the resident image:

| point | what the handler does |
| --- | --- |
| `0x8C36`, the busy-word test (`cmp word [0x5374],0`) | when the request in the frame is the word at DGROUP `0x2628` and the far return is a skipped call site, its segment the one the overlay's word holds now, moves IP to `0x8C57`, the epilogue the test's own branch takes when the driver is idle. Any other call or request is left to wait |

- **The click is not lost.** The driver's timer service plays the effect
  out to silence by itself, and the next request loads its own effect over
  whatever is still going. Measured on the Tandy chip: the same click, as
  long as it always was, and a step's two clicks as far apart. A step
  taken the moment the last one's second click starts cuts it short.
- **The timer stays hooked until the next sound**, because the bracket is
  also where the driver unhooks it. The entry hooks it again at its top,
  which over its own hook would lose the BIOS's handler for good; the
  timer fix (below) is what stops that, built in so that it holds after
  quick steps is turned off with the hook still in place.
- **Rejected**: skipping every footstep's wait. A step's two clicks then
  run together, which the maintainer heard as not the game's own sound;
  the step's first wait is what spaces them.
- **Rejected**: skipping the wait and keeping the silencing. Nothing waits,
  but the click is stopped in the instant it is started and is never heard
  (32 chip writes in a run of two turns and two steps that makes 1,264
  with the seam off).
- **Refuses** a DS that is not the data segment, an instruction whose
  operand is not the busy word's address, and a BP that is not the
  routine's frame (`BP == SP` at the test).
- **Fidelity**: before the first footstep it moves nothing, and a run with
  no footstep is the run with it off (`menu-letters-quick`, `identical`).
  From the first footstep the driver is left running between clicks, as
  its own busy word says, the program reaches its next key sooner, and
  everything that reads the clock afterwards reads another time
  (`walk-quick`, `contrast`, 81 of 190 checkpoints identical). It cannot
  be in `quiet-all`: `quiet`'s four movement keys click.
- **Open**: a step's new view still comes after its first click's wait.
  And of what is left between a key and a turned view, about four frames
  at the 386 governor are the program composing the view in its back
  buffer and copying it out.

### The message pause (#531)

Not a PLAN.md §5 item: a player's report that encounter text went by
before it could be read. **The pause is a calibrated spin, and exact**:
the runtime's `Delay` counts loops it measured against the BIOS tick at
start-up, 27, 54, 108 and 543 a millisecond at the four governors, so a
pause is a millisecond of virtual time at every one. What a player read
by on an XT was the drawing around it. A combat line stands about 2.7 s
at `xt` and about 0.6 s at `386`, the site's only speed.

**What the program does.** One routine is the message pause, image
offset `0x7E4E`: `push bp; mov bp,sp`, the game-speed byte, DGROUP
`0x49E2`, times 225 into AX, `push ax` at `0x7E5B`, a far call to
`Delay(ms)` whose far return is `0x7E61`, `pop bp; retf`. Every combat
line, the flashed warnings, the script opcode that pauses (overlay 3,
load-segment word `0x200`, routine `0x271F`, its call returning to
`0x272B`) and the cinematic beats at a pool's cleansing and the ending
come through it. A line typed out a character at a time calls `Delay`
directly, with a tenth of the speed, and is not touched. `Delay`, at
image offset `0xA69C`, spins each millisecond out and then `dec dx` at
`0xA6B7`; nothing is pushed between its entry and there, so its caller's
far return is at `SS:SP`. A monster walking up to the party is a run of
script pauses between frames, counted down in the word at `+0x582` of
the game-state block DGROUP `0x49D6` points at (a far pointer). And a
death in a fight is a sound timed by the pause: the routine that takes a
downed figure off the field (overlay 31, load-segment word `0x3E10`)
turns the music gate off, starts the death's sound, calls the pause
(returning to `0x1018`, or `0x0E34` outside a fight), and turns the gate
back on, which stops the driver and the sound with it.

**Points**, in the resident image:

| point | what the handler does |
| --- | --- |
| `0x7E5B`, the pause's `push ax` | raises AX to the setting when the setting is longer, unless the far return in the frame is the death routine's (overlay 31's segment now) or the script opcode's (overlay 3's) while the approach's count is not zero: those pauses are a sound's length and a beat of the walk-up, and keep the game's |
| `0xA70F`, the runtime's `ReadKey` | clears the seam's one word, "a line is unread". The pause's `push ax` clears it too |
| overlay 3 `0x0A69`, `0x0A91`, the print opcode's two calls to the box printer | sets that word when the line, the Pascal string at DGROUP `0x70C8`, has anything but spaces in it |
| overlay 3 `0x2176`, the encounter menu's call to the box printer | when the word is set and the call clears the box (its fourth pushed word, `[SP+4]`, is 1), waits the setting before the call: the runtime's `Delay` (image paragraph `0xA40`, offset `0x029C`) called fifty milliseconds at a time, the point offered again after each, where a waiting key is taken and ends the wait |
| `0xA6B7`, `Delay`'s `dec dx` | when the far return at `SS:SP` is `0x7E61`'s, the pause's caller (at `[BP+2]`, which `Delay` leaves alone) is not one of those, and a key is waiting in the BIOS buffer, takes the key and sets DX to 1, so the spin ends at this millisecond. Every other `Delay` runs as it did |

- **The number** is `pause`, in `ms`, 0 to 10,000 in steps of 250,
  initially 1500 (§6). 0 keeps the game's length and only adds the key.
  A game speed whose pause is longer than the number is left alone.
- **The key is used up.** A key typed ahead during a pause ends it and
  does not go on to answer the next prompt.
- **Rejected**: lengthening the death routine's pause with the rest. The
  sound plays out until the gate after the pause stops it, so at 1500 the
  death sound ran 1.65 s where the game's own run of `fight` gives it
  0.45 s: an effect the game always cut short, heard whole, which the
  maintainer heard as a weird change on the first build.
- **Refuses** a DS that is not the data segment at either point, and a
  BP that is not the routine's frame (`BP == SP` at the `push ax`).
- **`fired`** counts every millisecond of every `Delay` while the seam is
  on, because the countdown point is reached at each of them and acts
  only at the pause's own; a large number is the seam watching, as the
  code wheel's is.
- **Fidelity**: before the first message pause it moves nothing; a run
  with none is the run with it off (`quiet-message-pause`, `identical`,
  and it is in `quiet-all`). From the first one the program reaches what
  follows later and every clock read after it reads another time
  (`fight-pause`, `contrast`, 104 of 164 checkpoints identical, divergent
  at the pause that opens the battle; the same fight to the same end).
- **The encounter menu wipes with no pause.** The script opcode that
  offers COMBAT, WAIT, FLEE and PARLAY or ADVANCE (overlay 3, `0x1EF8`)
  prints its own line first, and clears the box to do it when that line
  is not empty, so a script that prints "YOU SPOT A GROUP OF ..." and goes
  straight into the menu shows its line for as long as it takes to type
  (a player's report, on the first build). Hence the wait before it, keyed
  to the one word above: a line printed since the program last read a key
  or paused. Fifty milliseconds a call rather than one call, because a
  call into the program is a batch, a batch offers no points, and the
  countdown point that ends a pause on a key would not be reached. The
  scripts that do it are in the first script file's blocks 18 and 24;
  none of the session library's runs reaches one, so it is driven only on
  the maintainer's copy and held by the unit suite.
- **Open**: a scripted line that another line replaces, or a picture or a
  change of map wipes, with no pause between, a few dozen in the game's
  scripts, is not given one: only the encounter menu's wipe is.

**On Curse of the Azure Bonds** (#555) the seam is the same mechanism
with that program's facts, one `message_pause_facts` row per program
(`seam_message_pause.cpp`) and its points in the definition's
`programs`:

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| data segment (paragraphs from the image) | `0xC7C` | `0xABE` |
| the pause: entry, `push ax`, far return of its `Delay` | `0x7E4E`, `0x7E5B`, `0x7E61` | `0x57F9`, `0x5806`, `0x580C` |
| its length | game speed (DGROUP `0x49E2`) times 225 | game speed (DGROUP `0x4FC2`) times 100 |
| `Delay`: entry, `dec dx`; paragraph:offset | `0xA69C`, `0xA6B7`; `0xA40:0x029C` | `0x868C`, `0x86A9`; `0x83F:0x029C` |
| `ReadKey` | `0xA70F` | `0x86FD` |
| interpreter overlay | overlay 3, load-segment word `0x200` | overlay 3 (file offset 2313, 14858 bytes), word `0x3D0` |
| script pause's far return | `0x272B` | `0x2917` |
| print opcode's calls (append, clear); its line | `0x0A69`, `0x0A91`; DGROUP `0x70C8` | `0x0A7F`, `0x0AAA`; DGROUP `0x7762` |
| encounter menu's call (clear flag at `[SP+4]`) | `0x2176` | `0x235F` |
| game-state block's far pointer; approach count | DGROUP `0x49D6`; `+0x582` | DGROUP `0x4FB6`; `+0x582` |
| death routine; its far returns (fight, elsewhere) | overlay 31, word `0x3E10`; `0x1018`, `0x0E34` | overlay 33 (file offset 262204, 5061 bytes), word `0x1220`; `0x10D4`, `0x0E8A` |
| a script's animation step, its far return | none | overlay 3 `0x3090` |
| `Delay`'s millisecond at `386` | a millisecond | 0.6 of one |

Each address is checked against the bytes of the Steam copy: the
pause's `push bp`, `mov bp,sp`, the speed byte times 100, `push ax` and
the far call to `083F:029C`; the interpreter's three calls to the box
printer (paragraph `0x4CE`, offset `0x0498`) with the clear flag pushed
fourth, the encounter menu's followed by the program's own read of the
approach count through `0x4FB6`; the death routine's test of the game
mode byte for combat before its two calls to the pause.

**What differs.**

- **The runtime's millisecond is short.** `Delay` spins a count the
  runtime measured at start-up over one BIOS tick, with a sixteen-bit
  counter: at the `386` governor a tick holds more spins than 65,535,
  so the count saturates at 1,191 (DGROUP `0x8CBA`) and a millisecond is
  0.6 of one. The game's own pause, 400 at the game speed the shipped
  saves hold (4), measured in the brawl at The Curse: about 287,700 ticks,
  241 ms, at each of the 147 the run makes. A combat line stands
  about 0.45 s. So on Curse the seam holds a pause to a **moment**: the
  `push ax` raises the count to the setting as on Pool of Radiance and
  keeps the moment the setting ends at, in four of the seam's words, and
  at `Delay`'s countdown, when the count's last millisecond comes before
  that moment, it sets DX to 2, one more. A longer game speed keeps its
  count and is held to the moment too, because its count is no answer
  to how long it lasts. Driven with the seam on: every lengthened pause
  in the brawl is 1500 ms of virtual time, to within one. The wait
  before the encounter menu is held the same way, a fifty-millisecond
  slice of `Delay` at a time until its moment.
- **No gate times the death's sound.** The sound driver plays from the
  timer interrupt, and the death's sound runs on past the pause until
  the next sound the program starts takes the chip (driven: 170 ms past
  it in the brawl). Lengthened, it would be heard far longer than the
  program ever lets it be at this speed, which is the change Pool of
  Radiance's rejection was about; so the death routine's two calls keep
  their length here too, and with them the death's sound is the
  program's own (driven, seam on: the chip written as with it off
  through the first death and up to the next sound). The line naming the death has had its own pause before
  the routine is called.
- **A script's animation step** (overlay 3's call opcode, one of its
  sub-commands: the next frame of the picture, then the pause) is a
  beat of an animation, not a message, and keeps its length, as an
  approach's beats do. **Pool of Radiance has no such step**, so its row
  names none: the same opcode there has no animation sub-command, its
  pictures that move (the finale, the pool on the title) are timed by
  the clock and never call the pause, and the pauses that follow one
  hold a picture that has finished, which is a message's.

**Driven** on the Steam copy, slot A, through the brawl
(`docs/playable-curse.md` leg 3): 147 pauses with the seam off, all
from combat (overlay 14's two calls, overlay 25's status line, overlay
33's death beat) and one from the brawl's opening. The print opcode's
two calls are reached in every leg with a scripted line (`curse-quiet`:
18, `curse-carriage`: 75). The encounter menu's call is reached by no
session in the library, as on Pool of Radiance, and neither is the
animation step: both are held by the unit suite.

**Fidelity**: `curse-quiet-message-pause` `identical curse-quiet` (all
58 checkpoints: the leg makes no pause), and `curse-fight-pause`
`contrast curse-fight`, 82 of 195 identical, divergent from tick
142,562,734, the pause that opens the brawl. The fight's keys are at
the baseline's ticks, and its lines stand a second and a half where the
baseline's stand a quarter, so the brawl is still being fought at the
run's end, and five of the baseline's keys end a pause instead of
answering what they answered there. Not in a `quiet-all`: Curse has
none. Tests: `SeamMessagePauseCurse.*` in
`tests/core/machine/seam_message_pause_test.cpp`.

### The fanfare (#538)

Not a PLAN.md §5 item: the maintainer's request, and their own piece, about
thirty-one seconds for the Tandy chip, 5,950 writes to its port at PIT
ticks from the piece's start (`fanfare_score.cpp`, written from the
composer's list by `scripts/fanfare-score.py`). Nothing in it is the
program's, which plays no music at its title.

**What the program does.** Overlay 1 (GAME.OVR file offset 212, 2062
bytes, load-segment word `0x1A0`) is the titles, at `0x0627`: the
publisher's logo, five seconds or a key; the title picture, five more;
the credits, ten more. The program calls it once a boot, when its
loading is done; its entry is a `push bp` with DS the data segment. At
the 386 governor the entry is reached in frame 171 with the loading
line still up, the line is gone by frame 174 and the logo paints from
frame 225. The program writes the chip only through its driver: the
start-up silences the four voices (twice, in the first eighth of a
second), and after that every write answers a request through the
dispatch entry, `0x8BBC` (*Quick steps* above), so nothing writes the
chip from the titles to the first sound in play. The sound
setting is the byte at DGROUP `0x5372`, 0 the Tandy chip.

**Points:**

| point | what the handler does |
| --- | --- |
| overlay 1 `0x0627`, the titles' entry | when the setting is the chip and nothing is playing, puts the score's first cue in the seam's word and asks for a moment, now |
| a moment (§3b) | writes every cue that is due through port `C0h`, from where the piece started (the moment it was handed less its cue's tick), and asks for the next cue's moment; at the end of the score, or with the setting no longer the chip, hands the chip back instead |
| `0x8BBC`, the dispatch entry | while a moment is outstanding, drops it and hands the chip back, before the program's request reaches the driver |
| its stop (§3b) | switched off with a moment outstanding, hands the chip back, before the program's next instruction (#540) |

- **Handing the chip back** is eleven writes: each tone voice's period
  and the noise control to zero, every voice silent, the noise voice's
  attenuation named last, which is the chip the start-up leaves.
- **It never meets the program's writes**: the piece stops at the
  boundary the program's first request arrives at, and the driver writes
  nothing until a request.
- **Refuses** an entry whose DS is not the data segment or whose
  instruction is not a `push bp`. A setting other than the chip there is
  a choice, not a wrong fact: it plays nothing and declines nothing.
- **`fired`** counts the titles' entry, each moment (4,184 for the whole
  piece), and every sound the program starts while the seam is on.
- **Fidelity**: before the titles it moves nothing. From the titles the
  chip's registers are the piece's until it ends, gives way or is
  switched off, and the writes are in the run's audio record for good;
  the program's own run is untouched
  (`quiet-fanfare`, `contrast quiet`, 27 of 90 checkpoints identical,
  divergent from tick 68,752,816, the end of the loading at the `xt`
  governor, in `devices` and `audio` only, and in `audio` alone once the
  chip is handed back). No `identical` pair can exist: every boot runs
  the titles. Not in `quiet-all` for the same reason.
- **Open**: nothing.

### The timer fix

Not a PLAN.md §5 item: what quick steps needs to leave the sound driver
whole. **A built-in fix** (§7). The plain program never reaches it.

**What the program does.** In its one-shot mode (DGROUP `0x5374` set),
the driver hooks the timer interrupt (INT 08h) before each sound and
unhooks it after the sound's wait. Its install routine, image offset
`0x9E5C` (`push ds`, a far call), asks DOS for the vector, keeps it in two
words of its own segment, installs its handler (the `mov dx` at `+0x1C`
loads its offset, `0x13EA`), runs the timer thirteen times fast and
returns with the `retf` at `+0x34`. The handler chains to the kept vector
on every thirteenth tick, which keeps the DOS clock. The uninstall puts
the kept vector back and the timer to its BIOS rate.

**Hooked twice, the kept vector is the driver's own handler.** Every
uninstall after that puts the handler back at the BIOS rate: each sound
plays thirteen times slower (a monster's death ran five or six seconds),
and the chain goes to the handler itself, so the BIOS's never runs. It
was v0.14.0's: a footstep whose wait quick steps skipped left the hook in
place, and the next sound's install hooked over it.

**Point**, in the resident image:

| point | what the handler does |
| --- | --- |
| `0x9E5C`, the install routine's entry | when the busy word is set and the timer's vector is already the driver's handler (its segment CS, its offset the one the routine's `mov dx` loads), moves IP to the routine's `retf`. The kept vector stays the BIOS's, and the timer is already at the driver's rate |

- **Refuses** a routine whose `+0x1C` is not a `mov dx, imm16` or whose
  `+0x34` is not a `retf`.
- **Leaves the driver's resident mode alone**: with the busy word clear,
  the driver's own start-and-stop gate hooks and unhooks, and the fix does
  not second-guess it.
- **Fidelity**: in a plain run the vector is the BIOS's at every install,
  so it moves nothing, and every session verifies with it on. In
  `walk-quick` it acts at every sound after a skipped wait, and the
  vector is the BIOS's again after each sound that waits (18 hooks, 18
  unhooks).
- **Open**: nothing.

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
these and the three that draw on sight, and says so. Unit: `SeamFont.*`, `TextFace.*`,
`SeamFontScreenText.*`; stand-in: `font_probe_off`, `font_probe_sans`.
Driven: 48 stills from the credits to the character sheet read back
the same screen text with either face on as with none.

**On Curse of the Azure Bonds** (#555) the font is the same and the
blitter is not. The font has Pool of Radiance's layout: 177 glyphs of
eight bytes, the text table first, an index the character upper-cased
modulo 64, and the same nine slots and the space holding marks, a small
picture, frame pieces and solid blocks rather than lettering, so the
faces and what they replace are unchanged. The blitter copies the
glyph's eight bytes out of the font into eight bytes of its own, then
calls its EGA path, which draws the cell on one page only, a row at a
time out of the copy. So the seam has one point there, with its own
handler, on one `font_facts` row per program (`seam_font.cpp`) and its
point in the definitions' `programs`:

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| the font's far pointer | data-segment `0x5E20` | data-segment `0x65AE`, loaded from `8X8D1.DAX` block 201 |
| what the blitter draws from | the font, through ES:DI | a copy of the glyph at data-segment `0x65B2`, just past the pointer |
| the points | image `0x749A` and `0x74C0`, after the fetch for each page; DL holds the row | image `0x5A91`, after the EGA path's read out of the copy; AH holds the row, BX its number, and the glyph's index is the path's first argument, the byte at `[BP+4]` |
| the guard | ES is the pointer's segment and DI falls inside the buffer | DS is the data segment, BX is under 8, the index is under 177, and AH is the font's own row for that index and row, or already the face's |
| what the handler writes | DL | AH |

- **The point is offered twice when an interrupt lands on it**: the
  seams are dispatched before the step that delivers it, and its return
  comes back to the same instruction with AH already swapped. The guard
  takes the face's own row as swapped: a decline there would report the
  face `inert point_not_recognized` while it went on drawing.
- **The other adapters' paths** (CGA and Tandy, chosen by the program's
  video-mode byte at data-segment `0x5000`) read the same copy and are
  not hooked: this machine has the EGA only, as on Pool of Radiance.
- **Fidelity**: the pair is `curse-quiet-font-sans` and
  `curse-quiet-font-chisel`, each a `contrast` with `curse-quiet` at its
  keys and ticks: 1 of 58 checkpoints identical, then divergent from
  the loading line on (agreeing again while the title's art is up), in
  `devices` and `display`, and from the challenge screen on in `ram` as
  well, by one byte. That byte is in the stack, below where it stands:
  a word an interrupt pushed in the middle of a glyph, AX with AL the
  register index the path sends and AH the row, the face's where the
  program's was. Never `cpu`. Unit: `SeamFontOnCurse.*`.
- **Driven** on the Steam copy over `curse-quiet`'s keys, either face:
  the loading line, the credits, the challenge, the party menu, the
  `LOAD WHICH GAME` bar, the roster and coordinates and the opening
  message, and the adventuring bar are lettered in the face; the rope
  border, the frames, the runes on the challenge screen and the 3-D
  view are the program's. The challenge's path, drawn with the
  program's dash and dot glyphs, is drawn with the face's. The title's
  art has no text in the font. No handler declined; `fired=22408` over
  the run.

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

**On Curse of the Azure Bonds** (#554) the three seams are the same
mechanism with that program's facts, one `cheat_facts` row per program
(`seam_cheats.cpp`) and its points in the definition's `programs`:

| fact | Pool of Radiance | Curse of the Azure Bonds |
| --- | --- | --- |
| data segment (paragraphs from the image) | `0xC7C` | `0xABE` |
| game mode byte; camp, combat | `0x49F3`; 2, 5 | `0x4FD3`; 2, 5 |
| roster head | `0x5D96` | `0x6524` |
| side counts, a byte each | `0x6814` | `0x6FAA` |
| record: next, scratch, status, held, side, hit points | `0x104`, `0x108`, `0x10C`, `0x10D`, `0x10E`, `0x11B` | `0x189`, `0x18D`, `0x195`, `0x196`, `0x197`, `0x1A4` |
| damage point | the routine's own entry, image `0x2DC8` | its overlay's stub entry, image `0x0F72` (the stub's record at `0x0ED0`); the routine is overlaid |
| end check | overlay 8 `+0x0880` | overlay 9 (file offset 42328, 4995 bytes, load-segment word `0x0580`) `+0x09B3` |

Curse's record is the 422-byte one it saves, so its offsets are the save
file's. The frame at the damage point and the five writes that down a
combatant are Pool of Radiance's. Driven on the Steam copy, slot A, in
the brawl at The Curse (`docs/playable-curse.md`): `--watch 6FAA:2` reads
`0A06` (ten patrons, six of the party) at the fight's start and `0006`
at the frame of a pull (`fired=1 reached=0 waited=0`); the downed
patrons stay drawn until the round ends, the end check asks the
program's own **continue battle** question (it asks whenever more than
one of the party stands and no enemy does), and `N` ends the fight
won. `cheat-invulnerable` on through a quick-fought brawl fires 58
times and no party member loses a hit point; `cheat-wound-party` pulled
at camp leaves all six on one.

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
