# Seams

How an enhancement reaches the machine — and the rule that nothing else
may.

This is the sibling of [`machine.md`](machine.md), which is about the
machine below the fidelity boundary. This document is about the one
mechanism that lives above it: the seam engine (`machine/seam.h`),
M4-F2 (#96) of the plan of record (#110), and the pattern every seam in
the tree follows. It began with the engine and is finished at M4's
closeout (#109), once the cheats seam has walked the whole path.

The short version:

> A **seam** is an opt-in runtime patch: an id, a description, the
> fingerprints of the binaries it is about, and a set of interception
> points — CS:IP breakpoints, each qualified by the module it lives in —
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
| `schema` | the `seam_schema_version` the definition was written against |

Everything in it is a *fact* about the program — an address, an offset, a
hash — and nothing in it is *content* from the program. That is
CONTRIBUTING.md's clean-content rule at the one place it bites hardest,
and `overlay.h`'s design keeps it by construction: a seam names the code
it lives in by the facts of the read that loads it, never by bytes to
match against.

The build's own seams are `all_seams()` (`seam_code_wheel.cpp` is the
first, and the template). A host or a test may register more with
`seam_engine::add()`; the registry is a fixed table of
`seam_engine::max_seams`, which is the house pattern for anything
bounded.

---

## 2. The handler contract

A `seam_handler` is `void (*)(machine&, seam_context&)` — a plain function
pointer, the same shape and the same reason as `cpu::handler` and
`service_handler`: core carries no `<functional>` and allocates nothing,
so a handler has nowhere to keep state except what it is handed.

It runs **at the step boundary, before the instruction at CS:IP is
fetched** — the same moment the BIOS callout runs and for the same
reason: it is the only point at which CS:IP is settled. The order in
`machine::step()` is deadlines, the keyboard drain, the BIOS callout,
then seams (`docs/machine.md` §3's ordering note applies, and the seams
come last so a point on a BIOS stub sees the state the handler left).

A handler that wants the instruction to run returns. One that wants it
not to run moves IP.

It **must not stop the machine.** A seam is an enhancement above the
fidelity boundary, and "the enhancement gave up" is not a machine state.
A seam whose preconditions are not met stays inert and *says why* —
through `seam_event` on the diagnostics channel and through
`seam_engine::status()` for a host that asks (PLAN.md §5's fail-closed
rule).

That includes the preconditions a handler can only check **once it is
running**. Every other refusal is answered before an instruction is
intercepted; this one is answered at the point, and it is the one the
rule needs most. A handler that finds the machine is not holding what its
facts describe — a stack frame whose argument is not the pointer it is
supposed to be, a record where there is no record — calls
`ctx.decline(seam_reason::point_not_recognized)` and **returns without
touching anything**. Reported once per seam per enable, so a point in a
tight loop says its piece and then stops.

The alternative is worse than doing nothing and worse than crashing: a
seam whose address turned out to name the wrong routine writes its word
into whatever is at that offset, and the damage surfaces as a wrong
number on a character sheet three layers from its cause. Check what you
can check, and decline what you cannot.

It also must not toggle seams. `enable()`/`disable()` are configuration,
applied before the program runs or between `run()` calls, never from
inside one.

---

## 3. The action primitives

Eight things a handler may do, each the smallest honest version of itself
(#96). Four are as old as the engine; the host service had no
implementation behind it until M5-D1 (#169); the call into the program is
M5-D4's (#188), a seam's own words are M5-E1b's (#189), and port surgery
is M5-E2's (#173) — the only one of the eight that was added because an
enhancement could not be built without it. They are in the order §8 says
to reach for them, which is not the order they were built in:

**Register surgery** — `box.processor().regs()`. Plain state, edited in
place. The code-wheel seam is three register writes and nothing else.

**Memory surgery, as the program** — `box.processor().read_byte()` and
`write_byte()`, through the bus. A write into the video window reaches
the EGA's pipeline; a write into ROM is refused; a touch of nothing is
noticed — exactly as the program's own would be. Never `memory().ram()`:
that back door is for the machine's own writers (the loader, the BIOS
setup, a test), and a seam is not the machine. It is the program's hand,
moved from outside.

**Port surgery, as the program** — `machine::write_port8()`, the same
bus one instruction over. A device answers it exactly as it answers the
program's own `OUT`, a port nothing claims is noticed rather than
invented, and — like memory surgery — it is the program's hand moved from
outside rather than the machine's own writer.

It is here because of one thing an enhancement cannot do without it. The
EGA reaches a *plane* through the sequencer's map mask, so a byte written
into the video window with the mask the program leaves behind lands in
all four planes at once: black and white, and nothing else. A seam that
draws sixteen colours has to select one plane at a time, and there is no
other way to say so. `seam_automap.cpp` (M5-E2, #173) was the first user;
`seam_explored.cpp` (M5-E5, #179) is the second, and it needs the
graphics controller as well as the sequencer.

Three rules, all learned from what makes it safe there rather than in
general:

* **Set what you depend on; do not assume it.** These registers cannot be
  read back — that is what write-only means — so a seam that assumed the
  graphics controller was in write mode 0 would be assuming something it
  has no way to check. Program the four registers your write needs.
* **Hand back the state you found.** This program resets the graphics
  controller and the map mask at the head of every drawing primitive and
  restores the write mode at the end of it, so between commands there is
  a *resting* state, and a seam that draws there and leaves that state is
  indistinguishable from one of the program's own primitives having run.
  A program with no such discipline would need a different argument, and
  a seam over it would have to make one.
* **A masked write reads first, and the read is not free.** If a seam
  draws over only *some* of the pixels of a byte — the explored overlay's
  fog is a one-pixel checker, half of each covered square — then the bits
  the graphics controller's bit mask clears come from the adapter's
  **latches**, and the only way to put the screen's own bytes into the
  latches is to read them through the bus. So a veil costs a read and a
  write per byte where a solid fill costs a write, and, unlike the
  registers, **the latches cannot be handed back**: loading them *is* a
  read. That is tolerable here for the same reason the resting state is —
  the program's own read-modify-write primitives leave them in exactly
  the same condition — but it is a thing a seam is doing to the machine
  in order to draw, and a seam that is only *looking* at a screen must
  still never read the video window (§8.4's wild-read rule).

**Synthetic input** — `seam_context::inject_keystroke(scancode, ascii)`.
The keystroke goes straight into the BIOS buffer at 40:1E, which is the
keyboard-service funnel, and *not* through `input_queue`: the queue is
the host's recordable stream (`platform.h`), and a seam's keystrokes are
a consequence of the seam set, which a replay records as an initial
condition rather than as input. Nothing about how often the program
polls changes, because nothing is waited for — the key is simply there on
the next INT 16h, as one typed a moment earlier would be. PLAN.md §5 item
3 wants exactly this for the automap hotkey, and the Encamp Fix drives
the program's own rest through it.

**One key per arrival, and not two.** The program this build targets
drains its keyboard after every key it reads — having taken one, its
keystroke routine reads and throws away whatever else is waiting. So a
handler that posts two keys posts one: the screen that asked consumes the
last of them and the rest are gone. A seam that wants to drive a sequence
of menus therefore needs **one handler call per key**, which is a fact
about what kind of seam it can be rather than about this primitive: a
pulled seam gets one act (§3a) and so can post one key, while a seam with
a point at each step of the sequence gets one arrival per step and can
post one at each. `seam_encamp_fix.cpp` is where that argument is written
out, because it is the first seam it decided anything about — and it is
why that seam stopped being a trigger when it grew a second key to post
(#186).

**A seam may put a command on a menu the program owns**, and doing it as
memory surgery is what keeps it native. This program's menu bars are
Pascal strings in its data segment, handed to its own menu-bar input
routine, which draws what it is given and treats each letter in it as a
selectable command. A seam that splices characters into such a string
before the bar goes out — and splices them back out when the routine
returns — has added a command that **the program draws**, in its own
font, colours and highlighting, and that the program hands back like any
other. Nothing is drawn that the game does not draw, and outside the one
call that drew it the program's own string is unchanged byte for byte.

Three rules go with it, and `seam_encamp_fix.cpp` is the worked example:

* **splice, never compose.** Find the separator and insert; do not build
  a new bar out of the program's words. A seam that spelled the program's
  own menu out would be carrying its text in this repository, which
  CONTRIBUTING.md forbids — so the rule that keeps the content clean is
  also the rule that keeps the seam from caring what the menu says.
* **check the room, and refuse rather than overrun.** These strings sit
  in fixed-capacity slots and the bar is as wide as the screen. A splice
  that does not fit declines with a reason (§2) and the player sees the
  game's own bar.
* **add a letter the program does not already use.** The program ignores
  a command letter it does not recognise, which is what makes this safe
  at all; a letter it *does* recognise would be a command fired by
  accident.

**Control** — `seam_context::redirect(cs, ip)`, which is moving IP with
its name on.

**A seam's own few words** — `seam_context::scratch()` and
`set_scratch()`, `scratch_words` of them (#189).

Reach for this last. Every seam in this tree until M5-E1b held no state
at all, and that is the shape that works: `seam_encamp_fix.cpp`'s points
know where they are in a sequence because they read it out of the
machine — a field the program leaves in a known state, written with the
value the seam actually wanted. A handler that remembers nothing cannot
remember wrongly. What genuinely cannot be read back is a comparison
against *before*: how much a command restored, how much it spent. Those
are what the words are for. They are configuration like the enable and
the latch — `reset()` and `clear()` drop them, `enable()` starts them
fresh, the serialization never sees them — and a seam that kept a *copy*
of something the machine holds would have two of them, the second one
wrong the first time they disagreed.

**A call into the program** — `seam_context::call_program()`, and
`place_bytes()` beside it for the arguments that are not numbers (#188).

This is the primitive that makes text a seam puts on the game's screen
look like the game's, and the argument for it is short. The program draws
text with **its own font**, a far pointer it keeps in its data segment.
This machine has a font too and it is deliberately *not* that one — every
glyph in `font.h` was drawn for this project, because shipping somebody
else's bitmaps is what the clean-content rule forbids. So a seam that
rasterized glyphs itself would put visibly foreign lettering beside the
game's own on the same screen, which is the native-feel failure #186 was
filed about, one layer down. A seam therefore **asks the game to draw
it**.

It is a **batch**, and that is the design decision. A handler is a plain
function pointer with nowhere to keep "I am half-way through drawing a
report", and a report is a framed box and several lines. So a handler
queues the whole sequence in one arrival and hands it over; the engine
runs the calls one after another and the handler is never re-entered
part-way. What is next lives in the queue, and the queue exists only
while the batch does.

What the engine does, in order: snapshots the register file — every word,
IP and SP included; lowers SP over anything `place_bytes()` put there, so
an interrupt taken during the batch pushes *below* it; for each call
pushes its words in the order given and then a far return address
pointing at an address in the BIOS region that is not a stub, is in no
vector and nothing executes; and recognises that address at a step
boundary, either setting up the next call or restoring the snapshot — at
which point the machine executes the instruction it was about to execute
before the handler ran.

The program's own routines are far and **clean their own arguments**
(`retf N`), so the engine builds frames and never tears one down.

Three properties it has to have, and the third is why the sentinel is an
address and not a byte:

* **when the batch is done the point is offered again**, at the
  instruction the handler was called at. Without that a seam gets exactly
  one batch per arrival — `step()` runs that instruction the moment the
  engine returns, and the arrival is spent — so a seam driving a sequence
  could act exactly once. The Encamp Fix casts one cure per arrival and
  looks at the party again (#189);
* **an interrupt during the batch is survivable**, because the frame is
  one the program itself could have built, on the program's own stack;
* **a batch finishes even if the seam is switched off while it runs** —
  you cannot un-call a call, so `armed()` stays true until it returns;
* **nothing is written to make it work.** An earlier cut left an IRET at
  the return address as insurance and it was wrong for a reason worth
  keeping: the power-on image is machine state, it is hashed, and an
  engine that wrote a byte into it would have altered every run whether
  or not a seam was ever switched on. The committed sessions said so
  within a minute. What catches a call that does not come back is the
  batch's own step budget, which restores the snapshot and reports
  `call_did_not_return` rather than hanging the host.

**That budget is a bound against *never*, not against slow**, and getting
it wrong cost a day. It started at a quarter of a million steps, reasoned
from "drawing a box and six lines is a few thousand instructions". Then a
seam called the program's own cast driver, which repaints the screen it is
on — and a repaint reads art off the disk. Every call was abandoned, the
machine was put back every time, and the *seam* looked broken when the
engine was the thing that was wrong. Measured afterwards, that one call
costs between one and two million steps. The bound is sixteen million now:
an order of magnitude past the worst thing anything here has asked for,
and still far inside how long a person would wait before deciding the
emulator had stopped.

**Where the arguments go.** A Pascal string a seam invented is not in the
program's memory, and it must not become part of it. `place_bytes()` puts
it on the machine's own stack, below what the program is using and above
where an interrupt would push, and it is gone when the batch ends.

**The frame to build is a fact like any other**, read off the routine
being called. For the two the drawing consumers want:

| routine | image offset | cleans | pushed first ... last |
| --- | --- | --- | --- |
| draw a Pascal string at a cell | `0x076B6` | `retf 0Ah` | col, row, colour, string segment, string offset |
| draw a framed box with a centred title | `0x041F8` | `retf 10h` | left, top, right, bottom, style, colour, title segment, title offset |

The rule both follow — and the one to check on the next routine rather
than assume — is that the arguments are pushed in the reverse of the
order the routine's own source lists them, with a far pointer's segment
before its offset so the offset lands at the lower address, which is
where a `les` looks for one.

**It has been driven against the program.** A seam at the camp screen
placed a four-character string, called the drawer at `0x076B6` with
`(1, 13h, 0Fh, …)`, and the word appeared on the game's own screen in the
game's own lettering, beside a line the game had drawn itself — with the
camp screen and the run intact afterwards. That is the mechanism, the
address and the frame, all three, in one picture.

**A host service** — `seam_context::call_host(which, argument)`,
answered by the `seam_host_services` a host attached with
`seam_engine::set_host()`. Two services: `journal_open` and
`automap_update`. Since M5-D1 (#169) both hosts attach the same
implementation — `hosts/common/include/amberfolio/host/host_services.h`,
one object, linked into the SDL host and into the wasm module — so a
callout means the same thing on a desktop and in a browser.

It is **C++ running inside the module**, synchronously, on both targets,
and not a queue a page drains on its next turn. The reason is in what
`serve()` is handed: the machine. What it reads there is only true at the
moment of the call — the automap wants the party's position *now* — so a
page that read it a frame later would be answering a different question
and could not say by how much. What crosses the boundary to JS is
therefore not the call but the *record* of it.

**An answer that is not a `bool` comes back in a buffer**, and the
journal reader (#175) is the first service that needs one: `serve()`
answers `void` and `call_host()` answers "served", so what a host *found*
goes into `machine::journal()` — observation, on `machine/journal.h`'s
three terms, and the same category as the exploration state a host reads
on the other service. A host writing there is not a host writing machine
state, and the test that says so is that a machine holding a page of
somebody's journal hashes as the machine that is not.

That record is **polled**, which is #153's lesson one layer up: a stream
cannot express "it never asked". `seam_engine::host_calls(which)` counts
the calls a host actually served and `host_argument(which)` keeps what
the last one carried; both reach a browser through
`af_machine_seam_host_calls` / `_host_argument` and both hosts print them
at the end of a run. Served, not asked — a call made on a machine with no
host attached counts nothing, because nothing happened, and the handler
is told false so it can say so through the fail-closed path. A non-zero
count is proof an implementation was reached.

There were **three** service names before #169. `save_state_changed`
belonged to save and roster management, which was withdrawn from the plan
by decision on 2026-08-24 (#176), and a service with no consumer is a
surface built on spec — exactly what "log, don't fake" refuses one layer
down. It is gone; whatever M6 wants for persistence adds the name it
actually needs.

**#165 is the audit of these primitives**, done at M4's closeout: the
five M5 enhancements against them, one at a time. Three of them need
nothing that is not above; two need a host that implements `serve()`,
which is what #169 built. Worth reading before writing the first M5 seam,
because the answer to "does the engine already do this" is yes more often
than it looks.

What a handler is handed beside the machine is `seam_context`: the seam's
id, the physical address the point fired at, the base of the module it
lives in, and the image base — so a handler that reads a fact-table
offset against its module adds the right number.

---

## 3a. The trigger: a host pulls, a seam acts

Everything above is a seam acting on its own account. `call_host()` is
the seam → host direction. Until #161 there was no host → seam direction
at all, and a cheat wants one: a debug cheat that fires at every visit to
its point decides every fight from the moment it is switched on, which is
a setting and not a cheat. A person wants to **pull** it.

A definition may therefore say `trigger = true`, and then:

- `seam_engine::pull(id, now)` sets a **one-shot latch** on that seam.
  Refused, with a reason, for a seam that is off (`not_enabled`) or one
  that does not take a trigger (`not_triggered`) — a latch quietly
  waiting on a seam nobody turned on would fire at some unrelated later
  moment, which is exactly what a trigger exists to remove.
- The next time one of that seam's points is reached with the latch set,
  the handler runs and the latch clears. One pull, one run.
- With the latch unset the point is reached and **nothing happens**: the
  address is compared, the arrival is counted, and the handler is not
  called.
- A handler that `decline()`s **keeps** the latch. A pull that arrived at
  a point which was not the point is not a pull that was served, and
  swallowing it would answer a person's request with nothing at all. A
  handler that arrives and simply chooses to do nothing *has* been
  served — only a decline is a non-arrival.

### It cannot mean "at this instant", and this document will not pretend

A seam acts at a step boundary because that is the whole mechanism
(PLAN.md §5): there is no acting half-way through an instruction, and
editing a structure half-way through the routine that is walking it is
precisely the corruption the breakpoint discipline exists to prevent. So
the honest latency of a pull is **"at the next step boundary at which
acting is safe"**, and the only question is what a point knows about
safety.

An **address point** knows it from the address — a known instruction in
known code — and pays for that in latency: "how often does the program
go there" is a fact about the program. That is why a seam's status row
carries `reached`:

| number | the claim |
| --- | --- |
| `armed` | an address was computed out of the fact table |
| `reached` | the program arrived at that address, N times, whether or not anybody had asked. **Addressed points only** |
| `fired` | a handler ran **and acted**, N times, wherever it ran; a decline is not one |
| `declined` | a handler ran and would not act, N times — the fail-closed rule doing its job |
| `waiting` | a pull is outstanding: asked, and not served since |
| `pulled_at` / `waited` | when the outstanding pull was made, and what the last served one cost, in ticks |

`reached − fired` is the arrivals a trigger had and did not need, and the
*rate* of `reached` over a run is the granularity at which a pull can
possibly be served by that point. `waited` is the same question answered
directly, in ticks, for the pull that actually happened.

**None of those numbers is the sentence.** What a row *means* — did it
act, and if not why not — is `machine::seam_reading_of()`, decided once
in core and handed to the page as finished text by
`af_machine_seam_reading`. It is in core because it was not: both hosts
worked it out for themselves, and both got it wrong the same way the
moment a seam could act somewhere other than at an address (§below).

### A point with no address (#163)

For a once-a-round point, "the next arrival" is a round, and a person who
pulls a cheat mid-fight wants it now. So a point may set
`seam_point::at_every_step` and have **no address at all**:

- it is offered at **every step boundary** while its seam's latch is set,
  and at no other time;
- with the latch down nothing is read, nothing is compared, and the step
  is the step the machine would have taken with the seam off — the whole
  cost is the one bool the address points test a line later anyway;
- it has to buy its safety back from the machine, because it has no
  address to get it from. Its handler opens with a guard it can defend
  and `decline()`s — keeping the latch — at every step until the guard
  holds;
- it counts no `reached`. It has no arrivals; counting its offers would
  be counting steps, and counting steps is not a measurement of
  anything. Keeping `reached` addressed-only is what leaves it worth
  reading: against the real program the cheats' end check is arrived at
  **exactly once per encounter**, so `reached=1` is the number that makes
  "a pull used to cost 18.5 virtual seconds" and "it now costs none"
  comparable claims.

It is **not** qualifier-free. Such a point still names a module, and the
engine still refuses to offer it while the program's own record says that
module is not in memory (§4) — so "the code this seam is about is
loaded" stays a precondition, and the seam's `armed` row goes on meaning
what it meant.

**The trade is written down wherever one is used.** An address is
evidence about where the program is; a guard is evidence about what the
program's structures say, which is weaker, and a guard that cannot rule
out being mid-walk of the structure it edits has to say so.
`seam_cheats.cpp`'s `combat_roster_ready()` is the worked example, and
§10 has what it can and cannot claim.

**And it changes what a row has to say.** A seam served by such a point
reports `fired=1 reached=0`, and every reading that keyed a warning on
`reached == 0` called that a broken address table:

```
seam cheat-kill-all armed fired=1 reached=0 waited=8327644
    - armed and never reached; its point may not be where its facts say
```

`fired=1` and "never reached" cannot both be true, and that sentence
sends a reader to doubt an address table that was working — #131's harm
with the sign flipped, a line that reads as a failure when the thing
worked. So the reading moved into core (`seam_reading_of`), it answers
one question — *did it act, and if not why not* — and it is now
impossible for it to warn about an address when `fired > 0`
(`SeamReading.NeverWarnsAboutAnAddressWhenTheSeamActed`). The six
answers:

| reading | when | what a row says |
| --- | --- | --- |
| *nothing to say* | an ordinary seam that acted, or an inert one | the numbers, and nothing after them |
| served | a trigger was pulled and served | `- pulled, and served`, with `waited=` beside it |
| pulled, not served | a pull is outstanding and nothing has been offered since | `- pulled, and its point not reached since` |
| pulled, declined | a pull is outstanding and every offer was refused | `- pulled, and not served: what it was offered is not what its facts describe` |
| reached, never pulled | a trigger's point was arrived at and nobody asked | `- reached, and never pulled; this seam acts only when asked` |
| never reached | armed at an address the program never went to, **and it did not act** | `- armed and never reached; its point may not be where its facts say` |

The last one is #131's warning, and it is a claim about an *address*: it
is said only of a seam that has one.

`SeamPullPoint.*` and
`SeamFidelity.APointWithNoAddressNobodyPulledLeavesTheRunIdentical` are
the mechanism as tests, and `tests/programs`' `seam_probe_pull` /
`seam_probe_pull_unpulled` are the same claim at program scale on all
four targets — the second sharing its *exact step count* with the plain
machine's entry, which is the fidelity invariant as a number rather than
an argument.

### It is configuration, not machine state

The latch obeys the same three rules the enable does (§7): `reset()` and
`clear()` drop it, `enable()` starts it fresh, `disable()` throws it away,
and the state serialization has never seen it.
`SeamFidelity.ATriggeredSeamNobodyPulledLeavesTheRunIdentical` is the
first half of that as a test — the same program, once on a plain machine
and once with the trigger *on and armed and never pulled*, with the same
registers, the same memory, the same tick — and
`SeamFidelity.ALatchIsNotMachineState` is the second: pulled and not yet
served, the machine's state hash is the hash it would have had.

`tests/programs`' `seam_probe_trigger` and `seam_probe_trigger_unpulled`
are the same claim at program scale, on all four targets, and
`hosts/web/tests/smoke.mjs` asks it of a browser.

### A pull is an input event

A recording carries it as one — `pull TICK ID`, in the stream beside the
keys, not among the `seam` lines of the preamble (`docs/replay.md` §1).
The seam being *on* is a fact about how the run was set up; a person
pulling its trigger is something they did at a moment. A recording that
carried the seam and not the pull would replay a run in which the cheat
never fired.

### The surfaces

- **Desktop**: **Pause/Break** pulls every triggered seam that is on, and
  `--pull ID@FRAME` scripts one (it needs no window, so it works under
  `--headless`). Pause is safe on the same argument F11 and F12 were
  chosen on (`docs/hosts.md` §3): an 83-key XT board has no such key —
  pausing on one was Ctrl and the keypad's Num Lock, and the dedicated
  Pause/Break arrived with the 101-key Enhanced board as its one
  E1-prefixed sequence, which this machine's set-1 wire has no room for.
  `sdl::xt_scancode()` answers 0 for it, and `keymap_test.cpp` pins that.
  The keypad's `/` and its Enter are the other two keys an XT board has
  not got; both were passed over for sitting inside the cluster this
  game's movement keys are, and a key you can hit by accident mid-fight
  is the wrong key for a cheat.
- **Browser**: a **button** beside the seam's checkbox, live only while
  the seam is on. A trigger is a different affordance from a toggle and
  one shown as the other is a promise the seam does not keep.
  `af_machine_seam_pull`, `_triggered`, `_waiting`, `_reached`,
  `_waited` and `_pulled_at` are the ABI (`abi.h`), and
  `tools/drive.mjs` takes `--pull ID@FRAME` in the desktop host's
  spelling.

---

## 4. Qualified points: the resident image and the overlays

PLAN.md §5: "the original program swaps overlaid code through shared
memory, so a raw address does not identify code; interception points are
qualified by which module/overlay is currently resident."

A point is `{module, offset, handler}`. The module is either:

- **`resident_image`** — the program the loader placed, which is never
  overlaid. The offset is from the image segment (`image_load_segment`),
  because where DOS puts a program is the loader's business and the
  seam's facts are the program's. The engine adds the load base when it
  arms.
- **A `seam_module`** — a read the program makes: the file's leaf name,
  the offset in the file the read starts at, its length, and optionally
  the SHA-256 of the bytes it delivers. The offset is from wherever that
  read landed — unless the module also names the word the program keeps
  its whereabouts in, which is the next section and is what you want.

The machine keeps an `overlay_tracker` (`machine/overlay.h`) that watches
every INT 21h AH=3Fh through the DOS layer and records, for each read:
the file, the file offset, the destination segment:offset, the length,
and the digest of the bytes. A later read into overlapping memory
replaces what was there. That table is *observation* — it alters no
read, writes no memory, and is not part of the machine-state
serialization, because a replay reconstructs it by replaying the reads.

The engine arms an overlay-qualified point only while the tracker says
its module is resident, at the address the read landed. Otherwise the
seam is **on and inert**, with `seam_reason::module_not_resident` on its
status row and a `seam_event_kind::inert` on the diagnostics channel —
once per transition, not once per read. When the module arrives the seam
arms and says so; when something overwrites it the seam disarms and says
so. A seam that demanded a digest the bytes do not have stays inert for
the same reason, which is the fail-closed direction: the worst case is a
seam that declines, never one that fires on the wrong code.

`overlay_schema_version` names how a module is identified, and a
definition carries the version it was written against. The engine
refuses a mismatch (`schema_mismatch`) rather than misreading the table.

### Where a point lives: ask the program, not the read (#131)

The reading above is the weaker of two, and it was wrong on the first
real overlaid seam this tree wrote.

A read tells you where a module landed **once**. It does not tell you
where the module *is*. An overlay manager of this era owns an arena and
manages it: it may shuffle a resident module inside that arena, or
satisfy a call from a copy it already holds, and it does neither of those
through DOS. A tracker whose only input is `note_read()` cannot see
either happen, so a point armed at the landing address goes on reporting
`armed` while sitting on whatever the manager put there instead. Both
were observed on the program this tree is for: a module read to one
segment and running from the next one up nineteen frames later, and the
same module 0x73 paragraphs from its landing after a screen's worth of
loading.

A manager that moves modules has to write down where it put them, or it
could not call into them itself. That note lives in the resident image,
which does not move, and it is maintained by exactly the code that does
the moving. `seam_module::load_segment_at` is the offset of that word:

    constexpr seam_module end_check{
        .file = "GAME.OVR",
        .file_offset = 38919,
        .length = 4735,
        .digest = "5d07a6b3…",
        .load_segment_at = 0x360};   // the program's own note

When a module names one, the engine **resolves the point from that word
at every step** — the word is read at the step boundary and the address
is that segment times sixteen plus the point's offset — instead of
computing an address once at arming. Zero in the word means the module is
not loaded and the point simply does not match. That is the difference
the issue was filed for:

| | armed at the read's landing | resolved from the program's word |
| --- | --- | --- |
| what `armed` claims | the fact table | the machine |
| a module that moved | fires on somebody else's code, or never | follows |
| a module resident with no read | never arms | arms |
| a module dropped with no read | still armed | inert, that step |
| cost | an address compare | an address compare and a word of RAM, behind a test that discards fifteen steps in sixteen |

The two qualifiers answer different questions and a real seam carries
both. The read's facts — file, offset, length, digest — say *which*
module this is, and are what you check against the overlay file's own
table. The word says *where it is now*.

The tracker's reading stays for a module with no such word, because it is
better than nothing and it fails closed. But a seam whose point lives in
an overlay and that has no `load_segment_at` should be treated as
unfinished: it will work for as long as the manager happens not to move
anything.

---

## 5. Identity: fingerprints and editions

A program's identity is its SHA-256 (`sha256.h`, `fingerprint.h`) — a
fact about the file, the only thing this project ever stores about the
originals (PLAN.md §2). The host tells the engine what is running once,
after the load: `seam_engine::loaded(digest, image_segment)`, or
`identify(fs, path, image_segment)` which takes the digest itself. The
ABI's `af_machine_load_from_vfs` does it for a page.

`machine/edition.h` is the table of editions this build knows — a
fingerprint and a name — and the baseline is the currently sold archive
release, which is the copy the fact tables were gathered against
(PLAN.md §10's question, answered for M4). `find_edition()` answering
null is the **unrecognized path**, and it is a first-class answer: the
game runs as a plain machine, the hosts say so, and no seam is
available — not misapplied ones (PLAN.md §5). Availability itself is
per-seam: a seam is unavailable for any binary its `fingerprints` do not
name, whether or not the table knows the binary.

### The player's documents, and the gate (M5-D3, #171)

PLAN.md §2 lists three artifacts a player may supply and only the first
is required: the binaries, the Adventurer's Journal, the code wheel. A
binary is what the machine *runs*; a document is what the player
*holds*, so `machine/document.h` is `edition.h`'s sibling and not a
column in it — a fingerprint, a name, and what the document is *for*.

A `seam_definition` may name a `gate`, and then it is **one more
condition on arming**, in the same place "is the module resident" is. An
unsatisfied gate is a seam that is on and **inert**, with the reason
`document_not_presented` — not a refusal. The seam took; the player has
not shown the thing they are asked to hold. Its own reason, and not
`module_not_resident`'s, because the two are answered by different
people: a module arrives when the program loads it, a document when a
person presents one, and a host that could not tell them apart would be
telling a player to wait for the game.

Fail-closed by construction, not by discipline: a shut gate arms **no
points at all**, so there is no address in the table for `dispatch()` to
compare against, and the gate is tested in the one function `status()`
and `arm_all()` share — the same argument `modules_resident` makes, one
condition over.

The presenting side is a host's: `--document PATH` on the SDL host,
`--document PATH` on `drive.mjs`, and `af_machine_present_document` for
a page's file input. The file is read, hashed and **dropped**; nothing is
parsed and nothing is kept, because "a possession gate: it demonstrates
the player holds the document, no more" (PLAN.md §5) is the whole of it.
The journal's extractor (#174) does read inside its own document, and it
is separate work with its own issue.

**Unrecognized is reported, never guessed**, and the fingerprint comes
back either way — it is what a player can act on, and what a line in the
table is made of. A gate that armed on a document this build cannot name
would be a gate that armed on anything.

Presenting is **configuration**, exactly as an enable is: it survives
`reset()` the way an attached device does (a reset machine has no
program; the player still has their code wheel), it is not in the
serialization, and a machine with a document presented and every seam off
is byte-for-byte the machine without one. That last is a test, not a
sentence.

**One seam in this build is gated, and it is the one PLAN.md §5 item 1
asks to be** (#115). `code-wheel`'s definition names
`.gate = document_kind::code_wheel`, so until a person presents the wheel
the seam is on, **inert**, and says `document_not_presented` — and
because a shut gate arms no points, its address is never in the table
`dispatch()` compares against. The journal reader's gate is one field in
its own definition (§10) and is deliberately **not** set: since M5-E3b
(#214) the table *does* have a journal row, so setting it is a decision
about whether a player who has not shown their journal should be told by
the gate or by the reader, rather than a thing that could not be done at
all.

**A gate outlives the run it was applied to.** `verify_recording` applies
a recording's own preamble seams, so a replay of a session whose seams
are gated and whose player has presented nothing is **refused before a
step is taken**, naming the condition — `a recorded seam is gated on a
document that has not been presented` — rather than left to diverge
somewhere in the middle. That is why a session descriptor carries a
`document` line, by digest and by nothing else
([`tests/sessions/README.md`](../tests/sessions/README.md)), and it is
observable: replaying `camp-fix.rec` with no `--document` stops there.

---

## 6. The toggle surface

Three states a host shows, per seam (`seam_engine::status()`):

| state | meaning |
|---|---|
| `off` | available for this program, not enabled — the default |
| `on` | enabled; `armed` says whether every point is placed, and `reason` says why not |
| `unavailable` | not for this program (`wrong_binary`), no program yet (`no_program`), or written against another schema |

Beside those, `fired`: how many times one of the seam's handlers has
actually **acted** since it was enabled. **`armed` is a claim about the
fact table; `fired` is a claim about the machine.** A point is armed at
an address computed from where a module was recorded, so a seam whose
module has since moved — or whose offset was never right — reports
`armed`, does nothing, and reads exactly like one that works (#131).

A handler that `decline()`s does **not** count (#163). It used to, and
that reading defeated the number's own purpose: a handler saying "this is
not what my facts describe" is exactly the failure `fired` exists to
expose, and a count that went up for it made the failure look like
success. A point offered at every step declines at most of them, which is
what turned an arguable choice into a wrong one. One consequence to know
about: any `fired` figure written down before #163 that came from a run
in which a handler declined reads lower now.

The one such figure this tree carries — the "fires nine times" in
`tests/sessions/fight-cheat.session` — **was re-measured at the M5
closeout and is still nine**: `cheat-invulnerable armed fired=9`, with
`replay verified checkpoints=177` over the same recording. So the three
places that write the number down are right, and were right by luck as
much as by anything, because nothing had checked them since the count
changed meaning. #271 carries the note and the two numbers beside it that
have *not* been measured (`cheat-kill-all`'s 120, and the Fix's one hit
point per member per day).

The desktop host prints a line per enabled seam when a run ends:

```
amberfolio: seam code-wheel armed fired=635
amberfolio: seam cheat-invulnerable armed fired=4
amberfolio: seam cheat-kill-all inert fired=0
```

A **triggered** seam (§3a) carries two more numbers on that line, and
only it does — `fired=0` means nothing about a trigger nobody pulled, so
the sentence about a point that may not be where its facts say is keyed
on `reached` for it instead:

```
amberfolio: seam cheat-kill-all armed fired=0 reached=13 - reached, and never pulled; this seam acts only when asked
amberfolio: seam cheat-kill-all armed fired=0 reached=13 waiting - pulled, and its point not reached since
amberfolio: seam cheat-kill-all armed fired=1 reached=13 waited=1868720
```

`host.mjs`'s `formatSeamFired` says all of that identically, because a
reader comparing a browser run with a desktop one should be comparing two
runs and not two spellings.

A count cannot make a wrong address right; it makes the wrongness
visible, which is the half of fail-closed that was missing.

On the desktop host: `--seam ID` enables (repeatable), `--seams` lists
every seam with its state and exits, and the edition line is printed
beside the fingerprint at load. Every transition is a line on stderr:

```
amberfolio: edition Pool of Radiance, archive release (START.EXE)
amberfolio: seam code-wheel on
amberfolio: seam code-wheel armed
amberfolio: seams code-wheel on armed - answer the code-wheel challenge, for a player holding the wheel
```

On the web host: `af_machine_edition`, `af_machine_program_fingerprint`,
`af_machine_seam_count/_id/_about/_state/_reason/_armed/_fired`,
`af_machine_seam_enable/_disable` (`abi.h`), wrapped by `host.mjs` and
shown by the dev page as a checkbox per seam with its state beside it,
unavailable ones disabled with the reason. The node smoke check toggles
the probe seam on a self-written program and asserts the difference — the
same two results the native suite asserts for `seam_probe` and
`seam_probe_off`.

`_fired` is #147's addition, and it is the count above rather than a new
idea: a browser run could report `armed` and had no way to report what
that arming did, so the one thing #131 was filed about was the one thing
the wasm target could not say. It crosses as a `double`, like every other
64-bit count in that file and unlike `_armed`, which is a predicate. The
dev page keeps it live in each seam's row on the health readout's cadence
and prints the desktop host's own end-of-run line into its console;
`tools/drive.mjs` prints the same line after every run.

The check that makes the count worth having is a **pair** of test seams,
not one. `probe` and `probe-unreached` (`tests/programs`) are keyed to the
same self-written program, so both are available, both enable, and both
arm — every answer either host can give about them before the run is
identical. `probe-unreached`'s one point is on an instruction past the
program's exit. After the run one reports a count and the other reports
zero, which is the difference `armed` cannot express.
`AbiSeams.ASeamArmedWhereTheProgramNeverGoesReportsZero` asserts it on
every native target and `hosts/web/tests/smoke.mjs` asserts it under node.

Seam state is **configuration**, not machine state: it survives nothing
(`machine::reset()` clears it), the serialization omits it, and a replay
records the active set as an initial condition (#100). A trigger's latch
is configuration in exactly the same sense, and for exactly the same
reasons — but *when* it was pulled is not, which is why a pull is a
stream event (§3a). The persisted
config file and the shell's toggle panel are M6's.

---

## 6a. The enhancements themselves

This file is the mechanism. [`enhancements.md`](enhancements.md) is the
other side of it: the six enhancements this build carries as a *player*
meets them — what each does, how it is turned on, what it will not do
without, where its facts came from, what makes it feel like something the
game shipped with, and what it is not yet.

Read it before adding one. It is where the native-feel requirement stops
being an adjective, and where each seam's honest "not yet" is written
down in one place instead of six.

---

## 7. The fidelity invariant

PLAN.md §4's boundary, as the three things a test can say:

1. **With every seam off, the engine is not consulted.** `machine::step()`
   tests one `bool` (`armed()`), and nothing else about seams runs. A
   run's machine-state hash is therefore the hash of the same run on a
   machine nobody asked about seams.
2. **A disabled seam's breakpoint is never consulted.** Only enabled
   seams arm points; `disable()` removes them; `dispatch()` is reached
   only when something is armed.
3. **Seam state is not machine state.** `reset()` clears it and the
   serialization leaves it out. Since #161 that includes a trigger's
   latch: a seam that is on, armed, and never pulled leaves the run byte
   for byte the run it would have been with the seam off, and a machine
   with a pull outstanding hashes as the machine it would have been.

`tests/core/machine/seam_test.cpp` asserts all three, and
`tests/programs` runs the probe program with its seam on and off on all
four targets.

### And on the real program, once per seam (M5-V1, #177)

Those three are about the *engine*. The claim a person actually cares
about is about their seam — that turning it on and not using it changes
nothing — and no synthetic program can make it, because a synthetic
program does not have the menus, the timing or the overlays a real one
has. So the session library carries it as a pair per seam:
`tests/sessions/quiet.rec` is the baseline, and each sibling is that same
script with one more seam armed and nothing done to trigger it.

    quiet-automap   identical quiet          all 126 checkpoints equal
    quiet-encamp    identical quiet          all 126 checkpoints equal
    quiet-cheats    identical quiet          all 126 checkpoints equal
    quiet-explored  identical quiet          all 126 checkpoints equal
    quiet-all       identical quiet-journal  every seam at once, and no more
    quiet-journal   contrast  quiet          111 of 126, then divergent

The automap's is the one worth having most: its hotkey claim at the
INT 16h funnel is the one place a seam can change what the *program*
observes, and that line is the run which says it does not when nobody
presses the key.

**And one of them is a contrast, which is the honest answer rather than a
gap.** The journal reader splices `Notes` onto the party's own command
bar, so it changes the machine on any run that reaches that bar, cited or
not — the tick it first differs at is the tick the bar is first drawn.
A seam that draws the moment it is on cannot claim `identical`, and
`tests/sessions/quiet-journal.session` says so at length. Loosening what
`identical` means so it could would have cost the other three theirs.

`identical` and `contrast` are both checked on the recordings themselves,
with no disk, so CI checks them on every push. CONTRIBUTING.md makes the
pair a condition of merging a seam, and
[`tests/sessions/README.md`](../tests/sessions/README.md)'s "The matrix,
by seam" is the whole of it in one table — each seam's exercised half
beside its idle one, the subsets, and the one seam whose idle half is the
gate rather than a session.

---

## 8. Writing a seam

This section was a nine-line checklist, written when the only seam in the
tree was three register writes. It is longer now because the Encamp Fix
(#172, #186, #189) took three passes and **not one of the three was lost
to the enhancement's own logic**: they went to the trigger surface being
wrong, to the engine having no way to call the program, and to two engine
constants that were wrong in ways that made a working seam look broken. A
method that named those in advance would have saved most of it (#190).

Nothing here is new. It is the four seam headers, §3, §3a, §4, §10 and
`docs/playable.md` gathered into the order a person actually needs them.

### Start here: three decisions

Make these before writing anything, because each one decides what the
next step is even allowed to be.

**What is its surface?** A *command* a player uses, a *setting* that is
simply on, or a *cheat* somebody pulls. This is the decision most likely
to be wrong, and #186 is the cautionary tale: the Encamp Fix shipped as a
host pull and had to be rebuilt as a command on the game's own bar,
because a command a player can only reach by typing at the emulator is
not a command in the game, it is a console. The surface decides whether
the definition is a `trigger` (§3a), how many points it has, and — this
is the part that surprises — **what its fidelity test can claim at all**
(§7, and see below).

**Where are its points?** One address, several, or none (§3a's
`at_every_step`). Prefer an address: a known instruction in known code is
a place where the structures a handler edits are known not to be half-way
through being edited, and a point without one has to buy that back from a
guard it can defend.

**What will it refuse?** What a handler declines is more of the design
than what it does, and writing the guard first is what stops the happy
path from being the only path. Every seam here refuses more often than it
acts.

### 8.1 Facts, before a line of code

**The allowed direction.** Addresses, offsets, lengths, digests and
format descriptions are facts and may live in this repository; game code,
data, text and byte sequences may not (CONTRIBUTING.md). Reading a
routine to understand it and writing down only its address is the allowed
direction.

The rule that keeps the content clean is usually also the rule that keeps
the seam honest. The Encamp Fix adds a command to the camp bar by finding
the last separator in the program's own string and inserting four
characters — **splice, never compose** (§3) — because a seam that spelled
the menu out would be carrying the program's text here. The result is a
seam that does not care what the menu says, which is a better seam.

**What a seam's fact table holds**, by kind:

- the **code addresses** of its points, as offsets in the module they
  live in;
- the **module** each point lives in, *and the word the program keeps
  that module's whereabouts in* (§4). A point without that word is
  unfinished: it will work until the overlay manager moves something;
- the **data-segment offsets** it reads or writes;
- the **record offsets**, and what each byte means where that is not
  obvious — `seam_cheats.cpp` warns that the byte beside a wound status
  reads like a flag and is not one;
- for anything that calls the program: the routine's **entry**, what it
  **cleans up** (`retf N`), and its **argument frame**.

**Check every fact twice, by two different routes.** Every address in
this tree should have the method that found it *and* an independent
check:

- `seam_cheats.cpp` finds the word holding a module's load segment by
  searching the resident image for the manager's record with that
  module's file offset and length, then taking the word sixteen bytes in
  — and the check is that the search has exactly one match;
- #188 took four routine addresses from one source and checked them
  against another: all four landed on a `push bp / mov bp, sp` prologue,
  and one of them opened by reading the adapter byte the theory had
  predicted it would;
- §10's *How a wrong fact was found* is the worked failure. Read it
  before trusting a trace: its step 4 — "which module was this address
  in" — is answerable from the overlay file's own table, and answering it
  from the run's loads instead gave a plausible module, a plausible
  offset and a seam that worked once for the wrong reason. **Prefer a
  fact you can derive from the artifact over one you inferred from a
  trace.**

**Argument frames follow a rule, and the rule is worth re-checking each
time.** Arguments are pushed in the **reverse** of the order the
routine's own source lists them, with a far pointer's **segment before
its offset**, so the offset lands at the lower address where a `les`
looks for one. Confirming it on a new routine is three lines of
disassembly — its first few argument reads — and #188 confirmed it on
four before relying on it.

### 8.2 What the engine already does

Read §3 before building anything. The answer to "can the engine do this"
is yes more often than it looks, and #165 is the audit that established
that for all of M5.

The eight primitives (§3), the module qualifiers (§4), the trigger
(§3a), the address-free point (#163), the document gate (#171). What
matters more than the list is **the order to reach for them**:

1. **Read the machine.** A handler that remembers nothing cannot remember
   wrongly. The Encamp Fix's third point knows the second one ran because
   it reads a field the program leaves in a known state — not because
   anything told it.
2. **Write what the player's own keys would write**, then post the key.
   Two words of memory and a keystroke is the whole of what that seam
   does to start a rest.
3. **Call the program's own routine** (#188) when something has to be
   *done* that the program knows how to do — drawing text in the game's
   own font, casting a spell by the game's own rules.
4. **Keep your own words** (#189) only for what the machine has stopped
   holding: a comparison against *before*. A seam that kept a copy of
   something the machine still holds would have two of them, and the
   second one would be wrong the first time they disagreed.

### 8.3 The order of work

1. **The three decisions**, above.
2. **Facts, and their checks** (§8.1).
3. **The guard, then the action.** Write what it refuses first.
4. **Unit tests over a machine the test lays out**, including every
   refusal. A seam's addresses only mean something against the real
   binary; its *mechanism* must have a test that needs no copy of the
   game. Lay the state out from the facts, not by calling the seam's own
   helpers — a test that read its offsets out of the seam would be
   agreeing with itself.
5. **A `tests/programs` stand-in**, so the same handler runs on all four
   targets. Where the seam's points are overlay-qualified the stand-in
   has to be **its own overlay manager** — writing its code segment into
   the word the facts name and laying its routines out at the offsets
   they name — which is worth knowing before it is discovered.
6. **Drive it on a player's copy**, and expect to find something. Every
   seam here has found something at this step that no test could: a
   wrong frame layout, a wrong module, seven wild reads, a step budget
   an order of magnitude too small.
7. **A recorded session pair** (`tests/sessions/`) — the run with the
   seam and the same run without.
8. **The docs**: §10's entry, `docs/playable.md`'s leg, and the
   honest-gaps list.

### 8.4 The traps, each of which has cost a day

Every entry names the seam it was learned on.

**Driving the program**

- **The program drains its keyboard after every key it reads**, so a
  handler that posts two keys posts one — the screen that asked consumes
  the last and the rest are gone. One key per arrival (§3; Encamp Fix,
  #172).
- **A pull is one act.** A pulled seam's handler runs once per pull, so
  it cannot drive a sequence; and a seam that ran at every arrival
  instead would be a setting rather than a command (§3a; Encamp Fix,
  #172, and `cheat-kill-all`, #161).
- **A batch of calls must re-offer its point when it finishes**, or a
  seam driving a sequence acts exactly once — `step()` runs the restored
  instruction the moment the engine returns, and the arrival is spent
  (§3; Encamp Fix, #189).

**Guards and points**

- **An address-free guard reads wild memory.** It runs with DS holding
  whatever the program has loaded, and a read through the bus is a bus
  cycle: above conventional RAM it is the video window, where a read
  loads the adapter's latches. Check the cheap DS-local bytes first,
  follow no pointer until they hold, and refuse any read outside
  conventional memory (Encamp Fix, #172 — seven `unmapped_memory_read`
  notices on a driven run).
- **A point armed where a module's read landed is armed at the wrong
  place** as soon as the manager moves it. Resolve from the program's own
  word (§4; `cheat-kill-all`, #131).
- **A fact can be wrong and still work once.** Both cheats were wrong the
  first time they were driven — one in its frame layout, one in its
  module *and* its offset (§10; #103, #129, #131).
- **"Measurably different" is not "legible", and only a person can tell
  you which one you built.** The explored overlay's first marking lifted
  an explored square one shade and was measured on 2,800 window cells of
  a real run: the faintest still had 198 of its 576 pixels changed, so it
  was visible on every one of them. It still did not read — on a screen
  whose terrain is a two-colour dither, one step up in the palette is a
  slightly different patch of the same grass. The measurement was
  answering "can this be seen?" and the question was "does this say
  anything?", and no number and no session can answer the second one
  (explored overlay, #257 asked, #263 is the answer). Three things follow
  for the next drawing seam: keep every rejected candidate with its cost
  and its reason, so a change of mind is an edit and not a
  re-investigation; expect the fidelity claims to move with the picture —
  this one lost a claim it could make about a *lift* the moment the
  marking became a *fog* (§10); and **show a person the candidates rather
  than the winner**. The fog's first covering was solid black, argued for
  on four grounds that were all true, and when five coverings were
  composited over one real frame and put side by side the maintainer
  picked a different one in a sentence. The argument had never asked the
  question a display answers, which was whether a player should still be
  able to see the *shape* of the country under the fog.
- **A key taken at a blocking read has to be put back.** The keyboard
  poll and the "give me the next key" read look like the same claim and
  are not: a poll is a question the program can be told "no" to, a read is
  the program already committed to being handed something. Empty a read
  and the program sleeps inside the BIOS, where no point of this engine is
  reached at all — so the *next* key the player types is delivered unseen,
  and what a player sees is a seam key that sometimes needs a second press
  and eats whatever came after the first. It is timing-dependent, so no
  session catches it: the key has to land in the step between the poll's
  question and its answer. The answer is one keystroke back, chosen for
  being one the program throws away (`seam_key_read.h`; journal reader,
  #175, and the automap, which claimed at the same address
  from the day it was built without answering — #266).
- **A seam that paints where the program paints cannot show what
  arrived without a repaint.** The explored overlay drew at the return of
  the program's own screen present, which is right and was not enough: a
  party that loads a saved game and stands still gives the program
  nothing to redraw, so no present comes, and the trail a host had just
  read in beside the save stayed invisible until the player took a step.
  A drawing seam needs a point where the program is *idle* as well as one
  where it is drawing — and, because the idle one is reached thousands of
  times a second, a signature of what it last drew from (explored
  overlay, #256).
- **A routine's address is a segment and an offset, not a flat one.** A
  routine of this era reaches its own literals as `CS:<constant>` and its
  own siblings as `push cs` plus a near call, so it only works when CS is
  the paragraph it was *linked at*. Called at the image base with the
  whole offset in IP, the same bytes execute and every CS-relative read
  lands sixteen kilobytes away: the automap's first driven run put the
  party roster back drawn out of somebody else's data, in strings that
  looked like a corrupted machine rather than a wrong call (automap,
  #173). It is the third fact in this list that was wrong and still
  ran — check the *frame*, and check the segment.

**The engine itself**

- **The seam engine must not write anything to make itself work.** The
  power-on image is machine state and it is hashed; an IRET left at the
  call-return sentinel "as insurance" diverged every committed session
  within a minute. Insurance that costs the fidelity invariant is not
  insurance (call door, #188).
- **A step budget bounds *never*, not *slow*.** 250,000 steps looked
  generous against "a box and six lines", and the program's cast driver
  repaints the screen it is on — which reads art off the disk and costs
  one to two million. While the constant was wrong the *seam* looked
  broken (Encamp Fix, #189).

**Reading a driven run**

The symptom vocabulary, because it is what turns a day into an hour:

- IP **creeping** through a small range rather than sitting still is slow
  *progress*, not a hang — look for something expensive, not something
  waiting;
- a real keypress not unsticking it means it is **not waiting for
  input**, whatever it looks like;
- `module_not_resident` where you expected the screen means the
  **overlay word**, not the address;
- a call abandoned every time, at the same place, with the machine put
  back cleanly, means the **engine's** bound and not the program.

**Tests and recordings**

- **A session pair must put its inputs at the same ticks in both
  halves.** A frame carrying an input is checkpointed whatever the
  cadence says, so an extra input in one half gives it a checkpoint its
  partner lacks and `contrast_of` rightly refuses the pair. Easy when the
  difference is *which* key; it needs arranging when it is an extra one
  (Encamp Fix, #186).
- **Other compilers see what the local one does not** — a missing switch
  case is a warning-as-error on clang and a silent success on MSVC. Build
  the wasm preset before pushing (#188).

**Driving the game at all** (`docs/playable.md` has the rest)

- A press that lands while the game is still drawing is **drained and
  lost**; a six-character party loads slower than a four-character one,
  so the same script needs later presses.
- A live `--press` run needs `--seam code-wheel`, or it waits at the
  copy-protection challenge for ever and every downstream symptom looks
  like something else.

### 8.5 What a seam owes when it is done

- **Off, the run is byte for byte the run with no engine at all** (§7).
- **The fidelity claim, stated for this seam.** The usual one — "on and
  unused hashes the same as off" — cannot survive a seam that is
  *visible* before it is used, and the Encamp Fix's replacement is two
  narrower claims that are each tested (§10, #186). Say which one applies
  and why. **And when a design changes, say which claim it costs**: the
  explored overlay had a second, stronger one that belonged to the
  marking rather than to the mechanism, and reversing the marking
  retired it. It is asserted in the direction it now holds rather than
  deleted, so that nothing can quietly re-acquire it (§10, #263).
- **The facts checked against the program**, not only against the
  handler. A unit test proves the handler; only a driven run proves the
  table.
- **What it is not yet, at the point of definition.** `seam_explored.cpp`
  naming #256 and #179's last clause is the pattern, and it is why those
  gaps are filed issues rather than surprises. A seam with nothing
  outstanding says *that*, in the section — `seam_code_wheel.cpp` since
  its gate closed (#115) — rather than dropping the heading, because an
  absent section and a satisfied one otherwise look identical (#272).
- **An honest-gaps entry** in `docs/playable.md`, saying what has been
  driven and what has only been tested.

---

## 9. The review rule

**Nothing outside the engine mutates machine state.** A host reads
machine state — the framebuffer, the console, memory through the ABI —
and writes it only through a seam. A device answers bus cycles. A
service answers an interrupt. The seam engine is the *only* component
that alters memory or registers from outside the program's own
instructions, and the only one that intercepts its execution.

In review that means: a change that writes the machine from anywhere
but `machine::step()`'s own mechanisms, a device's bus cycle, a service
handler, the loader, or a seam handler is a change to the fidelity
boundary, and it needs the argument this document would have to carry.

---

## 10. The seams this build carries

| id | what | keyed to | qualified by |
|---|---|---|---|
| `code-wheel` | answers the copy-protection challenge, **gated on the code wheel** (#115): inert, and saying `document_not_presented`, until a person presents it | the baseline | the resident image |
| `encamp-fix` | puts a `FIX` command on the camp screen's own bar; chosen, it spends the cures the party already holds, rests off what they did not close, and says what it did in a box the game draws — on the camp menu, or on the way out of camp when the game ended the rest | the baseline | the overlaid module the camp screen lives in |
| `automap` | a map of where the party has been, drawn into the game's own screen on **Tab**, in the colours of the walls themselves | the baseline | the resident image |
| `journal` | what the game cites, opened on the game's own screen in the game's own glyphs, out of the player's own ingested journal; a **Notes** command on the party's own bar opens a log of everything it has cited, and **F1** the number prompt for anything it has not | the baseline | the resident image, and the adventuring loop's module |
| `explored` | fog of war on the game's own overworld map: the country the party has been near is the game's own, and every other square of the window is hazed over with a dark-grey checker — a setting, with no key and nothing to press | the baseline | the resident image |
| `cheat-invulnerable` | the party takes no damage | the baseline | the resident image |
| `cheat-kill-all` | every enemy takes 120 damage at once, **when you pull it** (§3a) | the baseline | the overlaid module the end check lives in |
| `cheat-wound-party` | the whole party drops to one hit point, **when you pull it at camp** (§3a) | the baseline | the resident image |

### The code-wheel bypass (#94, #119; the gate #115)

PLAN.md §5 item 1, and the **first seam this project ever wrote** — it
landed in M3, in the engine's deliberately smallest first slice and
before half of §3's eight primitives existed, and it still uses only the
first of them — register surgery, three writes. It went unwritten here for two milestones
because there seemed to be nothing to say about it; it is worth the
section, because it is the smallest complete example of everything §8
asks for — a handler of nine lines, a qualifier that is the whole of its
correctness, and a gate that decides who may have it at all.

**What the program does, as facts.** The challenge is answered by typing
a word. The program keeps thirteen candidate words in its resident data
as length-prefixed strings twenty-one bytes apart, and compares what was
typed against the one the challenge picked — through its own *general*
string-compare routine, which puts the smaller of the two lengths in CX,
runs a `REPE CMPSB` over that many characters, branches if they differed,
and otherwise compares the two lengths, returning its answer in ZF alone.
The point is that `REPE CMPSB`, at `0xBBB0` from the image segment; the
word table is at `0xC7C2`, thirteen entries of stride 21 and six
characters each. Addresses, offsets, a stride and a count — the whole
entry is facts of the kind CONTRIBUTING.md names, and not one byte of the
program or one word of the table is reproduced anywhere in this tree.

**Where those facts came from, and how the point is qualified.** The
compare loop is in the resident image and cannot be overlaid, so the
point is qualified by `resident_image` and nothing more (§4). That is not
the qualifier that matters, though: the routine is the program's
*general* string compare, called about a hundred and fifty times during
the boot before the gate is even reached, so the address alone would
corrupt every string comparison the program makes. **The real qualifier
is the operand** — ES:DI must point inside the word table's character
span, computed from `image_base()` so the offsets stay the program's
rather than this machine's. Nothing but the code-wheel check compares
against those words; a comparison that does not is returned from
untouched, which is what makes a hot resident address safe to sit on.

**The surgery is three registers and no memory at all.** `CX = 0`, so the
`REPE CMPSB` performs no iteration and therefore changes no flag and
moves neither pointer; `ZF` set, which is what the untaken branch after
it wants to see; and `AL = AH`, so the length comparison that follows
agrees too. The program then runs its own code, reaches its own
conclusion and returns "equal" through its own convention. Nothing is
written into the input buffer, so the seam assumes nothing about how
large that buffer is or what shares the record it sits in — and it works
whatever the player typed, including nothing. This is PLAN.md §5 item 1's
preference honoured at the register level rather than at the flag: the
program exercises its own success path, and the rest of it cannot tell.

**The gate is the enhancement's whole claim about a person** (M5-D3
#171, turned on by #115). `.gate = document_kind::code_wheel`: present
your own wheel with `--document`, and until you do the seam is on,
**inert**, and says `document_not_presented` — its own reason and not
`module_not_resident`'s, because a module arrives when the program loads
it and a document when a person presents one (§5). It is a *possession*
gate and nothing else. The host reads the file, hashes it, and drops it;
nothing is parsed and nothing is kept, so what the seam knows about the
player is one digest and one bit.

**The fidelity claim.** The usual one, in its strongest form, and this is
the seam that can carry it: on and never triggered, the run is byte for
byte the run with it off — and *gated* off, it is stronger still, because
a shut gate arms no points at all, so there is no address for `dispatch()`
to compare against and no handler that could decline. Its idle half in the
matrix is therefore the gate rather than a session
(`tests/sessions/README.md`), which is the one place §7's per-seam pair
is answered by the mechanism instead of by a recording. Its *exercised*
half is every game session in the library: they all drive the real
program past the challenge, and their descriptors all name the document.

**What it is not yet.** Nothing outstanding for the seam. What is left is
a person's, and it is not this seam's alone: a browser has never been
opened on the toggle that turns it on, or on the file input that presents
the wheel (#147, #274), and putting a face on presenting a document —
rather than a flag — is M6's (#265).

---

### The Encamp (F)ix (#172, #186, #189, #194)

PLAN.md §5 item 4, and the one enhancement the plan grants a deliberate
exception: it automates play. The exception is narrow and the seam is
built to stay inside it — **the game's own routines do the work; native
code only asks.** Nothing in it shortens a rest, heals a character,
memorizes a spell or suppresses an encounter.

**A person asks for it the way they ask for any of the game's own
commands**: the camp screen's command bar carries one more item while the
seam is on, and they press its letter. There is no host pull and nothing
outside the game to learn. That is what M5-E1a (#186) changed and it is
the whole of what it changed; the arithmetic below is #172's, unaltered.

#### The command is a string, not a drawing

The bar the camp screen shows is a **Pascal string in the data segment**,
handed to the program's own menu-bar input routine. That routine draws
every character it is given and treats each `A`-`Z` in it as a selectable
command letter, so a seam that wants a command on that bar does not draw
anything:

* it **splices four characters into the string** before the bar goes out,
  and takes them back out the moment the routine returns — so outside the
  one call that drew it, the program's own string is the program's own
  string, byte for byte;
* the **program** draws the result, in its own font, in its own colours,
  with its own highlighting. Nothing is drawn that the game does not
  draw; and
* the seam **blanks the prompt** the loop builds on its own stack for
  that one call, because the bar is forty columns wide and the four
  characters need the six the prompt was using. The loop rebuilds the
  prompt on its next pass, so that undo is the program's own.

**The splice knows nothing about what the bar says.** It finds the last
separator and inserts before it. That is not a stylistic choice: a seam
that composed a new bar out of the program's own words would be carrying
the program's text in this repository, which CONTRIBUTING.md forbids.
Any later seam adding a command to one of this program's menus should be
written the same way (§3).

Two conditions make it safe, and both are facts about the program:

* the slot the bar sits in is a Pascal `string[40]` — a length byte and
  forty characters — so there is room, and the splice refuses rather than
  acts when a bar it is handed has not got it; and
* **an unrecognised letter is already harmless**: the camp loop compares
  what comes back against its own commands and, matching none of them,
  goes round again. It is a sequence of comparisons, not an index into a
  table that could run off the end. So the program needs no defending
  from a letter it has never seen — and the one thing this seam must not
  do is add a letter the program *does* recognise.

#### Four points, and no memory of its own

All four are addresses in the overlay the camp screen lives in, resolved
through the program's own note of where that overlay is (§4, #131):

1. **before the bar is handed over** — blank the prompt, splice the
   command in;
2. **where the menu-bar routine returns** — splice it back out; then, if
   the routine says a command was chosen off the bar and the letter is
   this seam's, do **one** thing and return: spend a ready cure if there
   is one to spend and somebody to spend it on (below), or else write the
   days field of the game's own rest clock with the days the party needs,
   claim the rest that is about to start, and post the camp bar's own
   Rest key;
3. **at the rest command's entry**, reached because of that key — if this
   seam claimed the rest, post the rest screen's own Rest key;
4. **on the loop's own way out** (M5-E1c, #194) — where a report still
   owed is one the camp menu is never going to draw, because the pass it
   would be drawn on is not coming.

Then it is out of the way. The program rests: time passes on the game's
calendar, pending spells are memorized at the game's own rate, hit points
come back one per member per rest day through the game's own Cure Wounds
applier, and its own wandering-monster checks roll against its own odds.
A monster that interrupts takes the party out of camp, exactly as it does
a rest a player asked for by hand.

**Point 3 has to know that the rest about to start is this seam's**, and
one word of the seam's own says so (§3): point 2 sets it as it posts the
Rest key, point 3 reads it. There is no latch and no handler-local flag.

**It was the program's own field until #192**, and the swap is the most
useful thing in this section. The days field is zero whenever a rest
begins — camp entry zeroes it, the end of a rest zeroes it, the rest
command's own set-up writes the three fields *below* days and never days
itself, and the Inc key that would let a player dial it lives on a screen
that has not been drawn when point 3 runs. So a non-zero days field at
the rest command's entry was a **signature the program itself kept**,
read back out of the machine and remembered nowhere, which is the shape
§3 tells you to reach for first. It was the better design in every
respect but one, and the one is below.

Note what the replacement costs, because §3 says to reach for a seam's
own words *last*: this is sequence position, which is the thing those
words are explicitly not for, and it is configuration rather than machine
state — the serialization never sees it. Nothing in this build notices,
because a recording replays from the start and a seam's state is dropped
on `enable()`. It is a debt against a future in which machine state is
restored mid-sequence, and it is written here rather than discovered
there.

**Point 4 needs no word of its own either**, and it is the better of the
two examples in this section. The camp loop takes an out-parameter and
runs while the byte behind it reads zero; the only thing in the whole
camp screen that ever writes that byte is the rest, which stores what the
rest orchestrator answered — and the orchestrator answers non-zero for
exactly one reason, a wandering-monster check that fired. Stopping a rest
by hand answers zero, and so does running the clock out. So at the loop's
exit a non-zero byte there **is** "a rest was interrupted", said by the
program, and the seam does not have to tell that apart from a player
pressing EXIT by remembering anything. Read back out of the machine and
remembered nowhere: §3's first choice, on a conclusion rather than on a
datum.

**The three points also dispose of the constraint** the first cut of this
seam ran into. The program **drains its keyboard after every key it
reads** (§3), so a handler that posts two keys posts one; three points
are three arrivals, and two keys posted one at each of two of them. And
it is why this seam is no longer a trigger: a pull is a one-shot latch
(§3a), a pulled seam gets one act, and one act could not drive two keys.

**The days are the deficit plus one, and zero when there is no deficit.**
The heal tick counts rest iterations in a counter the camp screen zeroes
on entry and a rest does not reset, so a second rest in one camp session
starts part-way through a day and would come up one hit point short. A
day of slack costs the player nothing they did not ask for.

**A party that is whole gets no days at all**, and this seam shipped with
that wrong: it dialled at least one, so choosing the Fix with nobody hurt
slept a full day for nothing. That is **the one respect** promised above.
The day was never the arithmetic's — it was keeping the borrowed
signature non-zero. With point 3 reading a word of the seam's own, the
clock is free to say zero, and zero leaves the duration the program's own
wrapper computed: the rest the player's own Rest key would have given
them.

**With nothing to rest for, the Fix does nothing.** A hit point somebody
is short and a spell somebody is holding pending are the two reasons to
rest; with neither, the command declines and says so rather than spending
the player's day to look busy. Which is a small lesson about signatures:
one inferred from a field the program owns is cheaper to *read* than a
word of your own, and can quietly cost the player something to keep
true.

**What a later Gold Box title's FIX did that this one does not**, which
#172 asks to be written down. The list was three things and #189 closed
one of them, so it is two:

* it **memorized cure spells for you**, filling empty slots with cures so
  that the rests were spent on healing magic; and
* in at least one title it **made room** by forgetting ready spells that
  were not cures.

The first is refused by the promise the next subsection makes rather than
by taste: a seam that memorized cures into the player's slots would owe
them their loadout back afterwards, and would have to be trusted to give
it. The second this project would refuse anyway — the game has no by-hand
forget, so a Fix that forgot spells would be changing the rules rather
than saving keystrokes.

**Casting is no longer on this list.** It was, and the entry argued that
a cast loop was a different shape of seam because it would owe the player
their spells back; #189 landed it by never taking any. That is the
subsection immediately below. What the player keeps either way is the
half that costs nothing: whatever they had queued for memorization is
memorized during the rest this seam pays for.

#### What it spends before it rests (M5-E1b, #189)

A rest heals one hit point per member per day, so a party forty down
sleeps forty-one days and rolls the game's wandering-monster check every
one of them. Spells are faster. So before it rests, the Fix spends the
cures the party **already has**.

The rule of record, and it is stricter than the design this project
carried over:

* only Cure Light Wounds a member holds **ready** is cast — never one
  memorized into a slot that was empty, never a spell forgotten to make
  room;
* one is **queued back for every one spent**, by the two writes the
  program's own memorize command makes (the slot gets `id | 0x80`, then
  the program's own slot sort runs);
* so the party ends holding exactly the cures it started with.

The queue-back happens *before* the cast. That is what makes the promise
true at every instant rather than at the end — there is no moment at
which the player is a cure down — and it is why the handler needs no
memory of what it has spent.

**Everything is the program's own function**, which is the point rather
than a nicety. The cast goes through the same driver the camp screen's
own Cast command calls, so the 1d8, the overheal clamp, the forget and
the drawing are the program's; the memorize is the memorize command's two
writes and its sort; the frame and the clear are what the cast screen
draws first. No menu is navigated — the seam positions the driver's own
target anchor and answers its one prompt with one key, which is what the
program's own code does with it.

One act per arrival, and the machine comes back to the point when a batch
is done (§3), so the next arrival looks at the party again and decides
afresh: cast another, or dial the days the cures did not close and press
Rest. It ends because every cast spends a ready cure.

**And the player can stop it.** Every arrival stands aside if there is a
key the program has not read yet, so anything typed during the run ends
the Fix there — with whatever healing has happened kept, and the camp
menu in front of the player. The rest itself is interruptible by the
program's own stop-resting question, and a wandering monster ends it the
way it ends any rest.

#### The fidelity test this seam owes, which is not the usual one

A seam that is *visible before it is used* cannot claim the invariant a
pulled seam claims. With `encamp-fix` on and its letter never pressed,
the run is not the run it would have been, because the bar looks
different — and that is the entire point of #186. What holds instead is
two claims, and both are tested:

* with the seam **off**, the run is byte for byte the run with no engine
  at all (§7, unchanged); and
* with the seam **on and the command never chosen**, the difference is on
  the screen and nowhere else — **between one menu draw and the next, not
  one byte of the program's own memory differs**, because the splice is
  undone at point 2 and the prompt is a stack byte in a frame that is
  gone before the loop turns over.

`tests/core/machine/seam_encamp_test.cpp` drives the four handlers over
a camp the test lays down by the facts — including a command bar of its
own three invented words, so the splice can be watched with none of the
program's text anywhere near this tree. The four `encamp_fix*` entries
in `tests/programs` drive **the same handlers** — the definition is the
build's own, copied with the stand-in program's fingerprint in place of
the game's — through the whole machine on all four targets. That stand-in
is its own overlay manager: it writes its code segment into the word the
seam's facts name and lays its routines out at the offsets they name,
which is the only way to reach an overlay-qualified point without an
overlay. Two of the four entries claim the **same exact step count**, one
with the seam armed and one with it off, which is the second claim above
made where every target runs it.

**And it reads nothing it is not sure of**, a rule this seam learned on
the program rather than in a test. Its first version had a point with no
address, so its guard ran with DS holding whatever the program had
loaded, and a read through the bus is a bus cycle: driven on a player's
copy it left seven `unmapped_memory_read` notices behind it, the roster
walk following a far pointer out of a data segment that was not the
program's. Nothing was corrupted, and the machine was right to say so.
The points have addresses now, and every read still refuses an address
outside conventional memory — above it is the video window, where a read
loads the adapter's latches, and a guard that perturbed the machine it is
inspecting would be doing the one thing a seam may never do.

#### It has been run against the program, and a hit point came back

Slot C loaded, encamped, and the Fix's letter pressed at the camp menu.
The bar reads one command longer, in the game's own font; the letter is
taken back off the game's own menu-bar routine; the rest screen counts a
day down from `00:16:05` to `00:00:00`; and the program draws **its own**
`THE WHOLE PARTY IS HEALED` — the message its heal tick prints when a
rest day's worth of iterations have passed and it has applied a hit point
to every member through its own applier. That is the thing the previous
revision of this section said nobody had watched.

`tests/sessions/camp.rec` and `camp-fix.rec` are that run and the same
run with the seam off, and the two differ by **one keystroke** — where
the plain half presses the camp menu's own Rest, this one presses the
letter the seam put beside it, at the same tick. The pair carries the
assertion:

```
  camp-fix     contrast ok  87 of 110 checkpoints identical, then
                            divergent from tick 206855088 to the end
  camp-fix     sdl      ok  replay verified checkpoints=110 keys=12 pulls=0
```

`pulls=0` is the change, said by the recording itself.

**On both hosts**, which is #172's exit criterion: `drive.mjs` drives the
same script against the wasm module and reports the same five firings the
desktop host does.

**And a cure has been cast on a wounded party** (#189). The shipped save
slots are not all whole after all: one holds two wounded fighters and a
cleric with five ready cures, which is the party this seam had been
waiting for since M4. Encamped, with the Fix chosen: two cures cast
through the program's own driver took **15/17 to 17 and 14/18 to 18**,
the party came out whole, and the Fix pressed Rest for the memorization
time the program itself computed — no days of its own, because after the
cures there was no deficit left to close — and the program answered with
an event of its own, the city watch moving the party along. Its rules,
running inside the rest the seam asked for.

**And a party the cures cannot finish** (M5-E1d, #196), which is what
that had never been driven against. Wounded to one hit point each by the
third debug cheat, slot B's party spends all five cures it holds through
the program's own driver, and what is left is a deficit no spell can
close: the seam dials **thirty** days — the worst survivor's deficit plus
one — and the program's own rest screen reads `REST TIME: 30:05:15`, the
thirty this seam's, the 5:15 the program's own wrapper's. That is the
arithmetic measured on the program's own screen rather than read off its
rest loop. `docs/playable.md` leg 7 has the run.

#### What it says when it is done (M5-E1b, #189)

On the pass of the camp menu after the command finishes, the seam frames
the program's own message panel and writes into it. **Every glyph is the
program's**: the box, the font, the colours and the centring of the title
come from the program's own frame and string drawers, called through the
door #188 opened (§3). This machine has a font of its own and it is
deliberately not the game's, so a seam that rasterized its own lettering
would have put visibly foreign text on the game's screen — the failure
#186 was filed about, one layer down.

```
  row 0x11   the title, centred by the frame on the box's own top row
  row 0x12   the summary: hit points restored, cures spent, and the time
  0x13..     ONLY the members it could not put right, one line each
  last row   the pending-cures warning, when there is one
```

**The summary is the one line only this command can write**, and that is
the argument for the seam keeping any words of its own at all (§3): every
other number on that screen says where things are *now*, and this is a
difference against a before the machine has stopped holding. Three words
hold it — the party's hit points and the clock as the command began, and
the days it dialled.

**There is no per-member table**, which is the proven design's own cut
(PLAN.md §5). The roster panel is still on screen behind the box with
every member's hit points on it, so a table would mostly redraw what the
player can already see. The exception list truncates to an "and N more"
line rather than paging, which removes the one part of a report that can
be got wrong.

**A member resting cannot mend is named in the program's own word for
it** — the drawer is handed a pointer into the program's own status
table, not a copy of what it says. Same rule as the bar splice, same
reason (§8.1).

**The way out is the bar under it.** The box is drawn *before* the camp
screen's own command bar goes out, so what the player is looking at is
the report and a live bar with the program's own EXIT on it. Any key
takes them somewhere and takes the box with it. Nothing says "press any
key", because the thing on the screen that works is the thing on the
screen — #186's rule, one layer on.

**Six titles.** Healed, rest stopped, stopped by the player, no cure
memorized, nobody knows a cure, cannot cast here — the last two being
different sentences, which is this project's own addition: a party whose
cleric knows a cure and has none memorized can do something about it, and
one whose nobody knows it cannot. `Interrupted!` is the seventh and it is
the fourth point's, below.

**The clock cannot say how long a rest of a day or more took.** It is an
hour and two minute digits with no day counter, so the summary drops its
time clause whenever the command dialled days rather than print the
remainder of a wrap as though it were the answer. A rest under a day —
the memorization the cures queue back, which is the usual one — reports
exactly.

**What is measured and what is read off the program.** The box has been
looked at, on a player's copy, on the desktop host: the title, the
summary, the line a whole party gets, and — since #196 — the exception
list, with its name and current-over-maximum cells, its shortfall column
and its `...and N more.` tail, in the game's lettering. The wasm module
took the same acts on the same script. `docs/playable.md` leg 7 has both
boxes as they were read off the screen.

#### And when the game does not hand the camp screen back (M5-E1c, #194)

The box above is drawn on the pass of the camp menu after the command
finishes, which is the only moment its result is readable: what a rest
achieved is not a thing this seam can know until the rest is over. A rest
the game **interrupts** does not give it that pass. The program answers
the wandering-monster check by ending the rest and letting the camp loop
leave — the mode word goes from camp to adventuring, the camp screen is
gone, and the overlay the first three points live in goes with it. The
next visit to that menu is whenever the player next chooses ENCAMP.

Waiting for it is the wrong answer, and that is the whole argument for a
fourth point. A box that arrived an hour of the player's game later would
be an account of something they have half forgotten, and its "hit points
restored" would be a difference against a party that has been in a fight
since.

So the fourth point is the camp loop's own exit, and what it does there
is decided by the loop's own out-parameter (above) rather than by which
of this seam's points happened to be reached:

* **a rest the game interrupted** — the box is drawn there and then, with
  `Fix: Interrupted!` on it, and **held on the screen by the program's
  own message delay**. That delay is what makes drawing there work at
  all: three calls further on, the teardown clears the very rows the box
  occupies, and the caller then repaints the screen and runs the event
  that interrupted the rest. It is also the program's own answer to a box
  with no command bar under it — the same routine it calls itself after
  it says the party was rudely interrupted, and it ends early on a key
  the player has already typed;
* **any other way out**, which in practice is the player pressing EXIT —
  nothing is drawn, and the report is **dropped**. A player who chose
  EXIT was shown the box on the pass of the menu they pressed it from,
  and a seam that put `Interrupted!` on the screen because the party
  happened to be leaving would be reporting the exit rather than the
  interruption.

Dropping is an act: the state is cleared either way, so a report owed
here never survives to a later camp.

**The outcome is told to this seam rather than worked out by it**, and
that is the one place in this seam where a roster reading would agree for
the wrong reason. Driven on a player's copy the cures close the party's
deficit before the rest is ever asked for, so a party read at the exit is
whole — which off the roster alone is `healed`. What happened is an
interruption, and the program is the one that knows.

**Driven, on a player's copy.** The wounded slot, encamped, the Fix
chosen: three cures cast, the rest asked for, and the game answering it
with the city watch. The box lands on the panel the rest orchestrator has
just cleared, in the game's own lettering —

```
                 FIX: INTERRUPTED!
HEALED 6 HP WITH 3 SPELLS IN 0:05.
THE PARTY IS AT FULL HIT POINTS.

CURES ARE STILL BEING MEMORIZED.
```

— is held there, and then the program clears it, repaints and runs its
event, exactly as it did before. `docs/playable.md` leg 7 has the run and
the frame numbers. The wasm module took the same acts on the same script.

The `encamp_fix_interrupted` entry in `tests/programs` is the same shape
on all four targets: the stand-in grew a loop condition and a way out of
camp, and reads the command tail to decide whether its rest is one the
game interrupts — one image, both ways out, and so no second fingerprint
to keep in step.

### The automap panel (#173, M5-E2a, M5-E2b, M5-E2c)

PLAN.md §5 item 3, and the first seam in this tree that **draws**.

**What it is.** A sixteen-by-sixteen map of the interior the party is
walking, filling in behind them, over the party roster on the game's own
screen — the region the game's own AREA view uses. Tab shows it and Tab
takes it away again. It is off by default, and closed even when it is on:
until somebody presses Tab, the seam is armed at five addresses, reached
about seventy times a virtual second, and has moved nothing.

**Where the design came from.** It is the proven one, carried over as
PLAN.md §5 says to: the panel's rect, the seven-pixel cell, the reveal
rule, the settling quarantine after a map change, and the three signals
that say who owns the pixels. `core/include/amberfolio/machine/automap.h`
has the derivations; what this section is about is what the *engine* had
to do to carry it.

**The six points, and what each is for.**

| point | in | what it does |
| --- | --- | --- |
| the program's "is a key waiting" routine | the resident image | claims Tab, and is the tick: samples the party, reveals what it can see, draws the panel if anything changed |
| the program's "give me the next key" routine | the resident image | claims Tab, and answers the read with a character the program throws away (#266) |
| the box-region clear | the resident image | if the rect meets the panel, something else has taken those cells |
| the full-screen clear | the resident image | the same, unconditionally |
| the party-roster draw's `retf` | the resident image | the cells are the panel's again |
| the menu-bar input routine's thunk | the resident image | which bar is going up, and so whose screen this is (M5-E2d) |

The first is the workhorse and the reason the seam works at all: the
adventuring screen's command loop calls it on every pass, so it is where
the program *is* between commands. It is the same seam the proven design
ticks on, reached at an address instead of through a hook.

The last one is at a **return** rather than an entry, and that is a fact
about the routine rather than a preference: the roster draw clears each
of its own rows through the box-region clear above, so a point at its
head would say "the roster is back" and then be contradicted by its own
clears a hundred instructions later.

**The key is taken out of the buffer, not answered around.** #173
proposed register surgery inside the INT 16h service — the program sees
"no key" (AH=01h) or the next key (AH=00h) exactly as often as it polled.
What is here is one step earlier: the keystroke is removed from the BIOS
ring at 40:1Eh before the program's own routine looks, so the BIOS
answers about a buffer that no longer holds it and no register is edited
at all. The program observes exactly what it would have observed had the
key never been typed, which is the same sentence with nothing left to
argue about.

Only the head of the ring is ever taken, so order and count are kept; the
program's own extended-key pushback slot vetoes the whole thing while it
is armed; and the *whole* keystroke word is matched, because Ctrl-I is
the same character under a different scan code and it is the program's.

**A key taken at the second point is answered** (#266), and that
is the one thing the second point does that the first may not. A poll is
a question the program can be told "no" to, so a claim there leaves an
empty buffer and the program goes round its own loop none the wiser. A
*read* is the program already committed to being handed a key: empty it
and the program sleeps inside the BIOS, where no point of this engine is
reached at all, and the next key the player types is delivered to it
unseen. What a player saw was Tab needing a second press, and the first
press eating whatever they typed after it. So the read gets a `-` back —
on none of the program's bars, neither of the two that step a bar's
highlight, and not one of the keypad characters its input routine remaps,
so the routine does not even return: it goes back to waiting. The journal
reader had this from the day it was built and this seam did not; the
constant and its argument now live once, in `seam_key_read.h`, and both
seams include it. Every claim is answered and not only Tab's — the
roster-cursor keys come off the ring through the same call, and a key
taken is a key the read is short whichever one it was.

**Its own memory is `machine::automap()`**, not `scratch()`. A seam has
`scratch_words` — sixteen bytes — and one map's fog is thirty-two. So the
exploration store lives in core beside the overlay tracker
(`automap.h`), on the same terms: observation and not machine state,
dropped by `reset()`, absent from the serialization, and reconstructed by
a replay because the same run walks the same squares. That is what lets
the pair below be a pair.

**A wall is the colour of its own texture** (M5-E2a). Not sampled off the
screen — histogrammed out of the very 8x8 tiles the 3D renderer blits for
that kind of wall, so water reads blue because its tiles are blue and
nothing changes as the party walks. An earlier cut of the proven design
sampled the rendered ground band off the planes once and latched whatever
was covering the viewport at the time: on a fresh game, a tour guide's
blue apron became the colour of the ground for the session.

Only the *largest* shape's tiles are counted. A wall's row of the
shape-tile table lists the codes for every shape the renderer draws it as
— the head-on face, the side slivers, the distant variants — and the
slivers are mostly post and edge, which outvote the face. Water came out
grey, off its pilings. Black is skipped, because it is the gap between
things in almost every one of these tiles and it is also the panel's own
"nobody has been here"; a wall whose tiles are entirely black falls back
to the area's frame colour, and so does one whose wall set has not
finished loading.

The answers are cached per map **and per loaded tile set**, because the
second is what the first was read out of: a wall set swapped under a
fixed map identity would otherwise keep the colours of the tiles it
replaced.

**A door is a door because something was seen shut** (M5-E2a). The
renderer picks a wall's graphic from its face nibble alone and never
consults the style bits — those govern movement and the prompt that asks
whether to force a door, and are invisible in the view. So a door *leaf*
is drawn exactly when the nibble indexes a door graphic, and neither
"style is not solid" (which paints every archway as a door) nor "style is
shut" (which drops every open one) can match what the player sees. Both
test the wrong plane.

Nothing in the data says "this graphic is a door". What there is, is
evidence: a face that is shut is unarguably one, and it names its nibble.
Two sources are combined — this map's own shut faces, scanned when the
party arrives, and a table of every shut face in the shipped data, keyed
on the durable identity of a wall graphic, which is (disk, WALLDEF block,
row). The table is there because the evidence is scattered: five sub-maps
of one castle share a wall set and only three of them have a shut
instance.

**Twenty-one of the twenty-nine shipped grids carry a shut face**, and
New Phlan is not one of them — nor does its wall set appear in the table.
So the city falls through to the older rule, which is that a passable
face is a door, and the panel in the driven run below is drawing its door
leaves by that rule. Drawing none would be worse, and it is what the
proven design does there too.

What this deliberately does not carry is the proven design's runtime
*learning* across maps — a table that remembers a shut face on one map
and applies it to another. It is there to be robust against other
revisions of the data; this seam is unavailable for any binary its
fingerprint does not name (§5), and the table was derived from that
binary's own data, so learning could only ever matter for data this seam
refuses to run against.

**It draws through the bus and through the ports.** The panel is rendered
into a private buffer of palette indices and blitted a plane at a time —
the map mask selected with a port write (§3's eighth primitive), the
bytes written into A000h through `write_byte()`. The rect starts on a
byte boundary and is twenty-two whole bytes wide, so nothing is shifted
and nothing is read back to be merged. The registers are handed back in
the state the program's own drawing primitives leave them.

**The zone label is set in the program's own glyphs** (M5-E2b). The
band the panel's geometry leaves — eight text columns to the right of the
sixteen cells — names the place the party is in, because nothing else on
that screen does: the program's own status row under the panel shows
coordinates, a compass and the clock, and never a place.

The glyphs are the program's. Its 8x8 font is a far pointer in the data
segment, sixty-four glyphs indexed by the character upper-cased and taken
modulo sixty-four, and the seam reads the same bytes the program's own
text primitive reads and rasterizes them into the panel's linear buffer.
It does not *call* that primitive, and the reason is the same one that
makes the panel a buffer at all: the screen is planar and the program's,
the panel is linear and this seam's, and it goes onto the planes in one
piece. Same glyphs, so the label is pixel-identical to the text the game
draws around it. The program treats its font pointer being zero as "not
loaded yet" and draws nothing; so does this, and the font's segment is in
the drawing signature so a panel first drawn without one picks the label
up the moment it arrives.

**The name is a table, and a name is a fact.** The program holds no such
string anywhere for the panel to read — the names live in its scripts as
narration — so the only way the band can say where the party is, is a
table of (disk, area) to a short label. CONTRIBUTING.md's clean-content
rule permits exactly that: "a SHA-256, a name, and the offsets a fact
table needs". These are names, of the same kind as the door table beside
them, and not a line of anybody's prose. A map with no row says `AREA
<n>` rather than nothing.

Six names have a word longer than eight columns, so a name may carry a
`|` — a soft break, a point inside a word where the wrap may split it
with a hyphen. It costs no column and prints nothing where the word fits.
With no marker at all, a long word is cut where it stops fitting: ugly,
and deliberately so, because a name nobody has marked yet should still
appear.

**Closing it asks the program to draw its party roster back** (#188's
door), because the panel wrote over the party list and only the program
can redraw it from live state — the game redraws single roster rows while
the panel is up, so any snapshot of the pixels would be stale. Two calls
in one batch: the region clear over the panel's own rect, then the roster
drawer with the current member. The clear is needed because the drawer
puts the header on the roster's row and one row per member below it and
clears exactly one row after the last, so the panel's first row and every
row below the party would otherwise keep their pixels.

It is deliberately *not* the program's per-mode screen composer, which is
what M5-E2 called and which repaints the viewport and the status line
too. A vendor's portrait lives in the viewport: closing the panel
mid-conversation painted the 3D view over the person the player was
talking to and left the question on screen with nothing asking it. The
panel covers the roster; the roster is what it owes back. (The proven
design has the same bug, and for the same reason — it withdraws through
`screen_redraw()`.)

That call is where this seam's day went, and the trap is in §8.4: these
routines reach their own literals through CS, so they have to be called
at the paragraph they were linked at. Called at the image base with the
whole offset in IP the composer ran perfectly and drew the roster out of
the wrong sixteen kilobytes; the roster drawer sits in the same segment
and has the same property.

**It yields to the program's own conversations** (M5-E2d). Three drawing
points tell the panel when something has taken its cells, and that is
enough for everything that takes the *screen* — a character sheet, a
shop, a fight. It is not enough for a vendor's question, which leaves the
game mode at "adventuring", draws its portrait in the viewport and its
question on the message row, and touches not one cell of the panel. So
the seam has a sixth point, and it is a good example of §8.1's "check
every fact by two routes" paying for itself: **the thunk of the one
routine every menu bar in the game is put up through**, which is handed
the bar as an argument.

The adventuring screen hands it one of two strings it keeps in its data
segment. A vendor's yes/no, a script's menu, a shop and the camp bar all
hand it a copy built on the stack. So "a far pointer into the data
segment at one of two known offsets" is the party's own command bar and
nothing else in the program is, and the panel knows whose screen it is on
from one comparison.

While it is not the party's bar the panel comes down — without anybody
pressing anything — and Tab is not this seam's key. It comes down *only
if it is really on the screen*: everything that took the panel's cells
cleared them first and will repaint the roster itself, so those keep
yielding the way they always did and the map comes back. What is left
over is exactly the case this is for. Exploration is deliberately not
gated on any of it: the party can be standing where a script has
something to say about, and a map that skipped that square would stay
wrong for the rest of the session.

**The cheaper answer was measured and thrown away**, and that is worth
keeping. The program has a byte for "a script still has the message
area", which reads like the whole feature for one fact and no new point.
Driven against the real program in New Phlan with `--watch 84E4` it
oscillates on *every step* and is down more often than up at the moment a
player is standing at the bar — a gate on it would have taken the panel
away for most of a walk. §9's rule, from the other end: the thing that
said so was a driven run, and no test would have.

**While the panel is up it also takes the two roster-cursor keys**
(M5-E2d). The adventuring screen answers a key it has no command for by
stepping that cursor and redrawing the party list — which is the block of
cells the panel is drawn on, so the map is painted over and the seam puts
it back on the next pass. What the player sees is a flash. Two of those
keys are a command a player means, the next and the previous party
member, and a command whose whole visible effect is behind the panel is
one the panel may decline while it is the thing on the screen. They are
the program's again the moment the panel comes down or something else
takes its cells. It is the one place this seam takes a key that *is* in
the program's alphabet, and the fidelity pair below is unaffected: with
the panel down, which is every run in which Tab was never pressed,
nothing is touched.

**The fidelity claim, stated for this seam** (§8.5). The plain one holds,
and it is the strongest in this tree, because the panel is closed until
somebody asks:

* **on, and Tab never pressed, a run is byte for byte the run with the
  seam off** — every point reads and none of them writes
  (`AutomapFidelity.OnAndNeverAskedLeavesTheRunIdentical`, and
  `tests/programs`' `automap_probe_untouched` sharing its exact step
  count with `automap_probe_quiet` on all four targets);
* **pressed, the program's input is what it would have been had the key
  never been typed** — `automap_probe_tab_claimed` polls exactly as many
  times as `automap_probe_quiet` and is handed nothing, where
  `automap_probe_tab_seen` with the seam off is handed the Tab. That is
  exact at the poll, which is where a key is meant to be taken, and it is
  one character wide of exact at the blocking read: a program that has
  committed to being handed a key is handed a `-` rather than nothing,
  because nothing is a program asleep in the BIOS (#266).

**What it has explored outlives the machine** (M5-E2c), and the half of
that which is the seam's is one call: when a reveal actually changes
something, the handler calls `automap_update` with the store's serial.
Everything else is the host's, because files are (PLAN.md §4) —
`hosts/common/.../automap_store.h` has it, and the shape of what is
written is decided in `machine/automap.h` rather than by whichever host
writes it, because the explored overlay (#179) reads the same records.

Three things about it are worth having here:

* **It is off unless a host is asked.** `--automap-store` on the desktop,
  `af_web_automap_store` in the browser. A file appearing in a player's
  game directory changes it, and every recorded session pins its disk by
  name, size and SHA-256 — a sidecar written by a verification run would
  make the next run's disk a different disk. So every session in
  `tests/sessions` runs with the seam on and the store off, and the two
  claims below are unaffected by any of this.
* **It is scoped to the playthrough.** A working table follows the party;
  a snapshot per save slot follows the save, and replaces the table when
  that slot is loaded — over it even when there is no snapshot, because
  an empty map is the truth about a playthrough nobody recorded one for.
* **A slot the program only *looked* at is not a slot it loaded.** The
  load menu opens every save file in the directory in turn to find out
  which slots exist. Acting on the naming call alone would fire nine
  times for one load and leave the player looking at the last slot in the
  directory's map. What tells them apart is whether bytes moved through
  the handle, which is what `file_event` says since M5-E2c and what the
  proven design's own file layer counted.

**Driven, and what it found.** Slot A loaded, forty-eight moves through
New Phlan to the armourer at 8,11, Tab on the way out of the load: the
streets fill in behind the party, the arrow turns with them, and Tab
again puts the game's own roster back. The session pair `walk` /
`walk-map` is that run recorded twice, one flag apart. The same script
through `drive.mjs` against the wasm module takes the same 95,454,560
steps and produces a final frame that is **byte for byte** the desktop
host's, panel included, which is #173's "on both hosts" as a comparison
of two files. What the driving found that no test could is §8.4's new
entry, above.

### The journal reader (#175)

PLAN.md §5 item 2's in-game half. #174 is the other half and is a host's
work — a player's own Adventurer's Journal located entry by entry inside
their own PDF, inflated, read once by an OCR engine and kept as text on
their own machine (`docs/journal.md`). This is what reads it back.

**The enhancement is mostly not a key.** When the game says to read an
entry, the entry opens. That is why the first of this seam's points is a
watch on the program's own text output rather than a convenience on a
menu: #165 settled the shape — a point plus memory reads, never a reader
of the host's console ring.

**Which text output is the whole of it, and #232 is what settled that.**
The watch began on an address that was already in this tree —
`image_draw_string`, the routine the Encamp Fix *calls* to write its
report, watched at its entry instead — and reusing it was the reason to
believe the seam added no address it could be wrong about. On the real
program that routine draws the credits, the menus and the position line
above the viewport, and **not one word of the story**. Driving a new
party to the city hall, where the game names four proclamations in one
sentence, produced no citation at all.

The narration goes somewhere else: to the word-wrapping **message box**,
which is where the script's every PRINT ends, the number form and the
string form alike. So five of this seam's six points are still the
automap's, for the same five reasons — the two keyboard entries, the two
clears, and the roster drawer's return — and the sixth is now an address
of its own, reached and read on a driven run before it was written down
(§8.1's rule about checking a fact by two routes: the box's own four
cells and its colour, read off that run's frame, are the ones the
program's message panel has).

#### The citation is a shape, not a sentence

What the watch matches is the word a numbered section of the document is
called by — entry, tale, proclamation, each with its plural — and a
number after it in the notation that section is numbered in: decimal for
entries and tales, a Roman numeral for proclamations, and after a plural
a list of them joined by commas and "and". Nothing of the program's prose
is written down to make that work, which is the same rule the bar splice
follows (§8.1) and the same reason: a seam that spelled out the program's
own words would be carrying its text in this repository.

**The word this enhancement is named after is not part of the shape**,
and used to be the whole of it. It appears in a citation as often as not
and never names a section; what names a section is the section's own
word. #232 is where that was measured rather than assumed, on the one
sentence the game had to say about it.

**A citation may arrive in two pieces**, and the box says so itself. The
script prints a sentence as one operand and the number it cites as the
next — sometimes appended with no space at all — so the box is called
twice: once told to home its cursor and clear itself, which is a message
beginning, and once not, which is the rest of the same message. The watch
keeps a rolling window of what has been printed, normalized to upper case
and single spaces with the commas kept, and empties it when the flag says
a new message has begun. That is the program's own message boundary
rather than a guess at one, and it is what stops a number opening one
message from being read against a word left at the end of the last.

A match empties the window too, so one message citing something opens one
entry however many times the seam looks at it afterwards — and a message
naming several is one message: they all go on the log, in the order they
were named, and the first opens. `journal_citations_in()` is that pattern
as a free function precisely so it can be checked against strings a test
writes, which is what `JournalCitation.*` does.

**A list that runs off the end of what has been printed is not answered
yet.** Where a split falls inside a list the first piece ends in a comma
or an "and", and three proclamations of four is not the answer: the watch
holds everything until the rest arrives, which is why the window keeps
its commas.

#### Getting the text across the door

`seam_context::call_host()` answers a `bool` and `serve()` answers
`void`: between them they can say a call was **served** and not what it
**found**. The journal reader is the first consumer that needs the
second, and the answer is a buffer rather than a return type.

`machine::journal()` (`machine/journal.h`) is that buffer, and it is
`machine::automap()`'s sibling in every respect that matters: dropped by
`reset()`, absent from the state serialization, reconstructed by a replay,
and — the test that says so — a machine holding a page of somebody's
journal hashes as the machine that is not. A host writing there is not a
host writing machine state, which is the one sentence §3's host-service
paragraph needed and did not have.

The seam asks; the host's `serve()` looks the entry up in the store it
was given and calls `deliver()` or `refuse()`; the seam reads the answer
the instant the callout returns. Nothing is queued, and **"no journal has
been read" is one of the four answers** rather than a silence.

#### Where it draws, and what that costs

The same rect as the automap's panel (`automap.h` derives it once):
twenty-two columns of the program's own eight-pixel font by fourteen
rows, drawn into the planes a plane at a time through §3's port surgery,
in glyphs read out of the program's own font pointer.

It is there because **it is the one region of this program's screen a
seam can take and give back**. Those cells are the party roster's, and
the program can be asked to paint the roster again from live state;
nothing else on the adventuring screen has that property, and M5-E2d is
what the alternative costs — closing a panel through the program's screen
composer painted the 3D view over the vendor the player was talking to.
Twelve rows of twenty-two characters is what that constraint pays for and
paging is what makes it enough. A wider reader is a debt, and what it
needs is a way to give the viewport back.

**The two panels are the same pixels**, so the reader is modal over the
map: one condition in `seam_automap.cpp` stops the map drawing while an
entry is up, and the map comes back on its own when the entry is put
away. Neither seam knows anything else about the other and either works
with the other switched off.

#### The journal's own screen (M5-E4b, #222)

`Notes` opens a **log**: what the game has told this player to read,
newest first, with the moment it said so and a `*` on the ones they have
not opened. It starts empty and fills as a game is played. The
alternative — every entry the edition holds — is precisely what that
journal's own introduction tells a player not to read.

**The program draws it.** The panel the reader uses is plane surgery
because nothing in the game draws twelve rows of text in a box the size of
the party roster. A full screen is different: the game has a
bordered-window drawer that every Gold Box screen is made of, and a string
drawer, and calling those two gets the game's own border art, colours and
lettering without this project knowing what any of them look like.

**And the program gives it back.** The one thing a full-screen panel needs
that a roster-sized one does not is a way to restore everything it
covered, and there is exactly one: the routine the program itself calls
on the way out of every full-screen view it has, which takes no arguments
and repaints, for whichever mode the program is in, the scaffold — the
outer frame, the bottom panel, the viewport box and its inset — then the
view, the roster and the status line. M5-E2d is why that is safe here and
was not before — this screen is only ever opened from the party's own
command bar (#221), which is the one place in the game where a vendor
cannot be underneath it.

**Not the routine that *enters* the adventuring screen**, which is what
this called first and got wrong. That one puts the mode byte on the
alternate adventuring screen whether or not the player was on it, and it
draws the bottom panel alone — so the outer frame and the viewport's own
box and ornaments never came back, and whatever the journal had drawn
above the panel stayed on the glass. Two routines a paragraph apart, one
of which the program uses for exactly this and the other of which it does
not.

It comes back with **one injected keystroke** as well, because composing
the screen is everything *but* the command bar: the bar belongs to the
menu-bar routine, which is sitting in its key loop and will not draw again
until it returns. A space makes it return, the loop answers a letter it
does not know by going round again, and the whole visible effect is the
bar being redrawn.

**Painted over several arrivals.** A batch queues twelve calls and places
256 bytes (§3), and a frame, ten rows of forty characters and a way out
are more than either. So a pass draws what fits and says whether there is
more; the program draws nothing itself while it waits for a key, so a
screen that arrives in two pieces arrives as one frame.

**The timestamp needed no new machinery.** The machine already carries a
seeded wall clock — the host's instant plus the virtual time since — so
the seam derives a real date rather than reading one, which keeps the
host-time guard intact and gives a replay the same answer twice.

`Notes` opens the log, **F1 still opens the number prompt**, and F1 from
the log goes on to it: two questions, two ways in. "What was I told?" is
the list; "let me look something up" is the prompt, and it is the only way
to reach the ninety-odd entries nothing has cited.

#### A command on the party's own bar (M5-E4a, #221)

F1 opened this reader and still does. What F1 is not is **discoverable**:
a player looking at the adventuring screen sees six commands on a bar and
no reason to believe a seventh exists. The game's own answer to "how do I
do a thing" is a word on the bar, so the journal has one — `Notes`, put
there by §3's splice, drawn by the program, in the program's font, and
handed back like any other command.

Three facts made it safe, and all three were measured rather than assumed:

* **The slot is a Pascal `string[40]`,** and the two bars — one per view
  mode, in the data segment at the two offsets the automap already uses to
  tell the party's bar from a vendor's — are thirty-three and twenty-seven
  characters. Six more fit on either, and the splice refuses rather than
  overruns anything else.
* **`N` is unreachable on both authentic bars.** The menu-bar routine's
  command letters are an *upper case only* class, and the bars are mixed
  case — which is why each word draws with a large initial and a small
  remainder. The letters that select are the six initials; the `n` in the
  fourth word is lower case and selects nothing.
* **So the casing is not a preference.** `Notes`, one capital and a lower
  case tail, exactly as `Fix` is. An all-caps item would make four more
  command letters, and the routine's key scan does not stop at its first
  match — the *last* one wins — so a spliced capital `E` would quietly
  steal the fourth command.

**The splice comes out in the same call that drew it.** The pair of points
per view mode is in the adventuring loop's own module rather than the
resident image, and that is the whole reason they are there: only a point
at the call's own return can keep §3's promise that the program's string
is unchanged byte for byte outside it. The second of each pair is also
where the letter comes back, still in `AL`, with the routine's
out-parameter below the loop's frame pointer saying whether it was a
command off the bar at all.

The program is never stopped from seeing the `N`. It compares what came
back against its own commands, matches none, and goes round the loop
again — which is what makes adding a letter safe in the first place.

#### The key, and the ones it leaves alone

**F1 opens the reader, picks the section, turns its pages and closes it
on the last one.**
It is claimed the automap's way — taken out of the BIOS buffer before the
program's own key routine looks — and it is safe on a *stronger* argument
than Tab's: a function key has no character at all (`keyboard.h`), this
program selects commands off its bars by character, and the extended
keystrokes it does act on at their scan code are the numeric keypad's.
F11 and F12 never reach the machine (`docs/hosts.md` §3), so F1 is the
first key of that row that does.

**Picking the section is that same key** (M5-E3d, #218), and that is a
decision about the other seams rather than about this one. The journal
has three numbered sections and each numbers from its own base, so a
player typing `4` at the prompt has not yet said what they want. Every
key that might have been given its own job here is a key another
enhancement may want — the automap's is Tab — and two seams a player has
both switched on must never fight over a keystroke. So F1 goes round the
three while the prompt is up, the panel says so, and Escape is what
leaves the prompt, as it always was.

Everything else is the **modal** claim the automap's roster-cursor keys
already make, and it lasts exactly as long as the reader is the thing on
the screen: Escape closes, Backspace goes back a page or rubs out a
digit, and the digits and Return are the prompt's while the prompt is up.
Space and Return are deliberately *not* taken while a page is up — a
citation opens the reader in the middle of a story event, and the key
that turns the game's own page has to stay the game's.

**The log takes every key there is**, and it is the one screen that has
earned that. It covers the program's own, and the program's own command
bar goes on running underneath it — so a key this seam left alone chose a
command, or walked the party, where nobody could see it, and the program
then drew its bar and its status line back over the journal a piece at a
time. What a player saw was not a journal and not a game: it was the two
of them on the same glass, and a forward step that did nothing while a
turn still worked, because up and down were the list's and left and right
were not. Nothing reaches the program while the log is up. **`E` leaves
it**, because `EXIT` is the word on its bottom row and the letter of a
word on a bar is how this game leaves every screen it has — and because
`E` was reaching `ENCAMP` on the bar underneath.

**A key taken at the program's blocking read is answered.** The poll is
where a key is meant to be taken: the program asks whether one is
waiting, the seam takes it, the program is told no. But a key landing in
the step between that question and its answer arrives at the *read*
instead — and a read is the program already committed to being handed
one. Take it there and put nothing back and the program sleeps inside the
BIOS, where **no point of this engine is reached at all**, so the next key
the player types is handed straight to it, unseen. That is what made every
second keystroke fall through the claim, and it is the mechanism behind
"a seam-claimed key sometimes needs a second press". So the read gets a
`-` back: on none of the program's bars, neither of the two that step a
bar's highlight, and not one of the keypad characters its input routine
remaps — so the routine does not even return, it goes back to waiting.
`seam_automap.cpp` claims at the same point and now answers it the same
way, out of the same header (#266).

#### Its fidelity claim is narrower than the automap's, on purpose

On, with no citation drawn and F1 never pressed, a run is byte for byte
the run with the seam off: every point reads and none of them writes.
But a citation opens the reader with nobody having asked, which *is* the
enhancement — from the moment one is drawn the run is a run with a panel
on its screen. What still holds either way is that the program's own
input is untouched until the player presses a key at it.

#### Ungated, and why that is a decision

A journal-gated seam was inert for every player alive when this was
written: a gate is satisfied by a document whose fingerprint is in
`known_documents()`, and there was no journal row in that table. There is
one now (M5-E3b, #214), so the field is a live choice rather than a dead
one — and it stays off, because what this reader is really gated on is
answered better where it already is: the host has text for the entry or it
has not, and the reader says which. A gate would refuse a player who
ingested their journal on another machine and copied the store across.

#### Driven, and what it found

Since #221 the same run has been driven through the bar as well: slot A
loaded, then `N` off the party's own command bar, `4`, Return. `NOTES`
draws at the end of the game's own bar in the game's own lettering, the
prompt comes up, and `host-service journal-open calls=1 last=4` is the
callout saying entry four was asked for — out of a real ingested journal,
whose text then came up on the game's screen. The bar keeps the command
while the reader is up, and the string is the program's own again the
moment the routine returns.

Slot A loaded, then **F1**, `3`, Return at the adventuring screen, with a
store written by hand for the purpose — this project's own sentences, in
the store's own format, standing in for a document nobody here has.

The entry came up on the game's screen, in the game's lettering, wrapped
to the panel and framed by the game's own art, with `ENTRY 3` in the
yellow the program highlights with and the body in the green it writes
messages in. `host-service journal-open calls=1 last=3` is the callout
saying it was served. F1 again put the party roster back, exactly, with
the 3D viewport untouched — which is the M5-E2d property this seam
inherited by asking for the roster drawer rather than the screen
composer.

Run again with **both** panels on, the modal rule is a picture: Tab's map,
then the entry over it, then — one F1 later — the map back with New
Phlan's label and the party's square where they were.

**And the watch itself was driven**, which needed one trick, because the
program has no citation in it to draw. Build it once with the pattern's
word set to `S` — a letter the program's own status line puts in front of
the clock — and drive the same session with **no key pressed at all**:

```
amberfolio: host-service journal-open calls=1 last=2 at=209359552
```

The watch read `S ... 02` out of a string the program drew and asked the
host for it. That is the whole of the citation path proven against the
real program — the point is reached for the program's own text, the frame
read at its entry (the string's offset at SP+4 and its segment at SP+6)
lands on a real Pascal string in the program's memory, and the window and
the recognizer see what the program is actually writing on the screen.
What is left unproven is only the *word*, and no build in this tree can
prove that without a document.

Two things the driving found that no test could:

* **The prompt's cursor cannot be a letter.** An underscore reached the
  program's font at an index nothing has ever needed there and came out as
  a stray mark. The cursor is a rule this seam draws in its own pixels
  now, and the reason is written where it is drawn.
* **The watch does not misfire.** Twelve thousand frames of boot, title,
  code wheel, load menu and city — every string of which went through the
  point — produced exactly one `journal_open`, and it was the one a person
  asked for.

**On, and nothing cited, a real run is unchanged.** Forty million steps of
the program with the seam armed and reached over half a million times
produce a final frame and an audio dump that are byte for byte the run
with the seam off. That is §7's invariant on the real program rather than
on a synthetic one.

#### What a real citation did (#232)

**It has now been driven against one**, which is what #232 existed to do,
and it found two things rather than none. A player's own journal —
ninety-nine entries, ingested by the desktop host against an installed
Tesseract — and a new party walked through the city to the hall at the
square `3,4` facing east. The game's own entrance event names four
proclamations in one sentence, in Roman numerals and in the plural. The
first run of it opened nothing at all.

The first finding was the **address**: the watch was on the string
drawer, which on this program draws the credits, the menus and the
position line and no narration whatever. It is on the message box now,
and that is above.

The second was the **shape**: the watch wanted the word this enhancement
is named after and a decimal number, and the game writes the section's
own word and the notation the booklet numbers that section in. Both are
above.

With both fixed the same drive answers `host-service journal-open
calls=1` with the argument for the first proclamation named, the log
holds all four in the order the game said them with the `*` on the three
not read, and the page comes up over the roster in the game's own font
with nobody having pressed a key. That is the whole enhancement, end to
end, on the real program.

**There is still no committed session pair**, and the reason is worth
naming rather than leaving as an omission: a recording carries keys,
ticks and hashes (`docs/replay.md`), and this seam's other input is a
*file* — the player's store — which is host configuration and not in the
stream. A session that verified a reader would be a session that pinned a
store, and the runner has no way to say where one is. It is a gap in the
harness rather than in the seam, and #239 is where it is being closed.

### The explored overlay (#179, M5-E5a to M5-E5f)

PLAN.md §5 item 5, the third seam in this tree that **draws**, and the
one item of the six with **no proven prior design** — so the marking was
settled at the point of definition, with the reasoning written down and
the candidates it beat named. It is also, so far, the only enhancement
here whose design has been **changed by somebody looking at it**, which
is what the rest of this entry is mostly about.

**What it is.** On the program's own overworld screen — a five-by-five
window of a wilderness area's overhead map, scrolling with the party —
every square the party has not been near is **hazed over with a one-pixel
checkerboard of dark grey**, palette index 8, on half its pixels; the
other half stay exactly as the program drew them, so the terrain is
faintly there under the fog rather than gone. It is a *setting*: no key,
no pull, no panel. On, it is there whenever that screen is; off, it is
not.

**How far the party sees.** `explored_reveal_radius` in
`machine/automap.h`, a Chebyshev distance, **one**. Standing on a square
shows it and the eight around it, so a walk leaves a corridor three
squares wide and the row the party is walking towards stays covered.

**The record holds only where the party stood**, and the reveal is the
dilation of that computed when the window is drawn. Turning the radius up
therefore shows more of a map somebody already walked instead of asking
them to walk it again, and nothing in the sidecar's layout moved for the
change from a lift to a fog, or for the change of the fog's own colour.

#### The marking was reversed, and by whom

M5-E5c shipped the opposite picture: the game's whole window as the game
drew it, with the squares the party had walked lifted **one shade
brighter** — the EGA intensity plane set over 24 by 24 pixels, so every
pixel stayed a pixel the program drew, one step up in the program's own
palette. Seven candidates had been prototyped over a real frame before it
was chosen and it was measured to be visible on 2,800 window cells.

The maintainer ran it on a display, which is what PLAN.md §5 item 5 makes
this item's exit criterion, and said it did not read — that what was
wanted was a radius of two or three squares uncovered as the party
travels, with everything beyond it under fog. Being *measurably*
different turned out not to be the same as saying something: on a screen
whose terrain is itself a two-colour dither, one step up in the palette
reads as a slightly different patch of the same grass.

**That is the design rule working, not failing.** This is the one item in
the plan with nothing to re-express, so the plan puts a person with a
display in the loop and `docs/explored-overlay.md` §5 was written to keep
every rejected candidate with its cost and its reason — expressly so that
a second choice would be an edit rather than a re-investigation. It was:
§5 now has both designs, the lift with the six it beat and the fog with
the five *it* beat, and the change cost one function, one constant and a
day of re-recording rather than a phase.

**PLAN.md §5 item 5 changed in the same PR**, because the item used to
end "nothing the game draws is hidden — the overlay marks the known, it
never obscures the unknown", and obscuring the unknown is now the whole
of it. A design change that contradicts the plan of record belongs in the
plan of record.

#### The fog was looked at too, and its colour changed

The first fog was **solid black** — all four planes cleared over a
square's 24 by 24 pixels — chosen on four arguments and never seen by
anybody. Then five coverings were composited over one real dumped frame
with grass, coast water and the grey shore between them in it and put in
front of the maintainer side by side: solid black, a one-pixel checker of
black, a one-pixel checker of dark grey, a one-pixel checker of light
grey, and a two-by-two checker of dark grey. The **one-pixel dark-grey
checker** was chosen, at the same reveal radius of one.

What the composite said and no argument had:

* a one-pixel checker of *black* **collapses**: the terrain here is
  itself a two-green dither at one-pixel granularity, so black over it
  interferes into a flat dark mesh — a third texture, which reads as one
  more kind of ground;
* **light grey reads as paler terrain**, which is the lift's own failure
  reached from the other side: a covering whose value is near the tile's
  is a variation on the tile;
* **dark grey reads as haze.** Far enough from the terrain's greens and
  blues to be plainly a covering, and open enough that a coastline is
  still a coastline under it;
* **two-by-two is a pattern**: at twice the period the eye reads the
  blocks instead of integrating them, and blocks on a map are a modern UI
  grid.

**Black's four reasons are kept rather than deleted**, because it is the
rejected alternative and every one of them is still true: it is the one
colour that cannot read as terrain at all; it is the game's own
vocabulary for the unknown, since the message rows, the panel beside the
window and the 3D view's own distance are already black and the window
sits inside the game's own drawn border; it is the same on every terrain,
so there is one thing to learn; and it costs no read-back. The third
survives into the checker unchanged — index 8 on the same half of the
pixels whatever the tile is. **The reason black lost is the one only a
display could give:** a solid cover throws away the *shape* of the
country the party is standing at the edge of, and a player who cannot see
a coast through the fog cannot see there is a coast to walk to.

**What the checker costs, which is §3's new rule.** A covering that keeps
the pixels it is not covering is a *masked* write: the bits the graphics
controller's bit mask clears take their pixels from the adapter's
latches, so the latches have to be loaded from the screen first. That is
one read and one write per byte instead of one write — 72 of each a
square, 1,728 of each for a window with 24 squares covered, against the
automap panel's 9,856 writes — and the latches, unlike the registers,
cannot be handed back, because loading them is what a read *is*. The
colour itself comes out of the set/reset register, so one CPU write still
paints all four planes, and the bit mask alternates `0xAA` and `0x55`
with the scanline. The parity is the **screen's**, not the square's, so
the pattern runs unbroken across the boundary between two fogged squares.

Rejected before the composite, when the fog was still being argued rather
than looked at: a dither at one pixel in four (too light to read as
anything) and dropping the intensity plane, the lift's own inverse, which
is invisible on water and turns grass into a flat mid-green that reads as
a terrain type.

**Measured on the real screen**, the final frame of the driven walk with
the seam on against the same frame with it off: **3,166 pixels differ,
every one of them palette index 8, every one inside the window, and not
one pixel of the checker's other half changed anywhere on the frame.**

#### The radius is one, and that is a measurement

The window is five squares across and the party is its middle square in
open country, so **every square on the screen is already within two of
the party**. Driven on the same eight-step walk with the constant set to
2: 523 dumped frames, and **not one of them differs from the same walk
with the seam off**. At a radius of two this enhancement is invisible
except where a map's own edge pushes the party off centre. One is the
largest radius that covers anything, which is why the "two or three"
that was asked for is answered with one and a number.

**And the maintainer has confirmed one**, on the same look that chose the
checker: the composites were all at a radius of one and the answer was to
keep it. So the half of this that a measurement could not settle — whether
one square of sight is the right amount of country to hand a player — is
settled as well.

**The three points.**

| point | in | what it does |
| --- | --- | --- |
| the return of the back-buffer present | the resident image | the program has just put the screen up; the fog goes back on |
| the program's "is a key waiting" routine | the resident image | records the square the party is standing on, and draws when something has moved that no repaint of the program's would have shown |
| the menu-bar input routine's thunk | the resident image | which bar is going up, and so whose screen this is |

The last two are the automap's, shared, and the bar reading is now one
function both call (`automap_overland.h`) so the two seams cannot come to
different conclusions about it.

**The screen, as facts.** The mode byte is 3 and the view kind is 2, 3 or
4 — one per wilderness area. The party's position is **two words in the
area record**, not the two data-segment bytes every other screen uses:
driven, with the program's own status line printing the record's `3, 32`,
those two bytes held `0x0B` and `0x0D`. An area is 16 columns by **36**
rows, and the three of them are three sixteen-column bands of one
44-column terrain table at biases the program keeps per view kind, which
the seam reads out of the program rather than carrying.

**The geometry was measured, not derived**, and three routes agreed: the
pixels on a real frame, the pixels one move repaints (exactly
`8,8,127,127`), and the composition arithmetic. The window is **120 by
120 pixels at (8, 8)** and a square is **24 by 24** — which begins on a
byte boundary and is three whole bytes wide, so a fog confined to a
square shifts nothing and reads nothing back.

**Where the design came from.** Nowhere, which is what made M5-E5a (#253)
a phase of its own. [`docs/explored-overlay.md`](explored-overlay.md) is
its output: the fact table with a second route for every line, the pixel
geometry measured off a real dumped frame, the keystroke recipe, and the
three decisions with what each of them rejects. Nothing here was written
before that was.

**Why the present's *return*.** The program composes the whole screen
off-screen and flushes only the scanlines something dirtied. At the entry
of that routine the flush has not happened and anything painted there is
about to be copied over; at its return every path that repaints the
window has finished — the composer's own redraw, and each step of the
icon's animation, which advances a phase and presents again. Painting
there is painting last, and no captured frame can catch it half drawn.
Painting into the program's own back buffer instead, so its own present
carries the fog, was rejected: that buffer is one the program reads back,
and the fog would become part of what it believes it drew.

**And why that was not enough**, which is §8.4's newest entry and the
thing the driven run found that no test had. A party that loads a saved
game and stands still gives the program **nothing to redraw** — so no
present ever comes, and the map a host had just read in beside the save
stayed unshown until the player took a step. A seam that paints where the
program paints cannot show what arrived without a repaint. It paints at
the keyboard poll as well now, and because that point is reached
thousands of times a virtual second it paints there only when something
has moved: where the party is, and the store's serial, which moves both
when a cell is revealed and when a host reads a slot's table in.

**Two things are never covered.** The party's own square, which is where
the program draws its icon — the reveal radius covers it at any radius of
one or more, and the rule is written down as its own line all the same,
because fog over the player's own sprite is the one mistake here that
would be a bug rather than a preference. And **every pixel outside
the window**, not one, which is what lets the confinement leg mask the
squares the fog is allowed in and assert the rest of the frame byte for
byte.

**A square of a neighbouring area *is* covered**, which is the reversal in
one line. The three wilderness areas are bands of one table and the
window can overhang; this seam has no record for a neighbour's columns.
Under the lift, marking them would have claimed knowledge nobody had;
under the fog, not covering them would claim the opposite.

**The fidelity claim, stated for this seam** (§8.5). **It is one claim
now, and it used to be two.**

* **On, and the overworld never shown, a run is byte for byte the run
  with no engine at all** — `ExploredFidelity.OnAndTheOverworldNeverShown
  LeavesTheRunIdentical`, and `tests/sessions/quiet-explored.rec` saying
  it on the real program, all 126 checkpoints.
* **The claim that is gone**: "on, the overworld shown, and nothing
  walked but the square under the party, the run is byte for byte the run
  with the seam off". It held only because a *lift* marks what is known
  and a map nobody has walked has nothing known on it. A fog marks what
  is not known, and a fresh map is nearly all of that — the outer ring of
  the window is covered the moment the party arrives. Rather than delete
  it quietly, it is asserted in the direction it now holds
  (`ExploredFidelity.TheArrivalIsNoLongerTheScreenItWouldHaveBeen`), so
  that a later change cannot re-acquire a claim this enhancement cannot
  make. The `wild` / `wild-trail` pair moved with it: 107 of 140
  checkpoints identical rather than 111, diverging at the arrival rather
  than at the first step.

**Driven, and what it found.** Slot J loaded — the one shipped save whose
party is already standing on a wilderness area, which is why the recipe
is four keystrokes and not an afternoon — and eight steps north. 523
stills against the same run with the seam off: **411 byte for byte
identical, and every one of the 112 that differ differing only inside the
squares the party has not been near** — 16 of them until the first step,
13 from then on, and **not one pixel outside the window at any frame**.
`tests/visual/exp-trail.leg` is that as an assertion, with the fogged
squares named rather than a box drawn round them. What the driving found
that no test could is the present-return paragraph above, and §8.4's
entry.

**What it is not yet.** **Nobody has played with the fog** — the
maintainer has looked at five coverings composited over a real frame and
chosen one, which is a stronger look than the lift ever got and is still
a picture beside another picture. That is what is left of #179's unticked
clause. One wilderness area of three has been stood on; the other two are
the same arithmetic with a different bias, and
`docs/explored-overlay.md` §8 says how to reach them without playing for
hours. And the fog has only been over grass, coast water and the grey
shore between them: it cannot fail on rough ground or a road, since the
checker is index 8 on the same half of the pixels whatever is underneath,
but how legibly it hazes a terrain depends on that terrain's own colours
and nobody has seen it over one that is already grey.

### The debug cheats (#99, #196)

**Three seams, not one with three switches** — PLAN.md §5's first
requirement is a toggle per seam, and they are wanted separately: a sweep
that wants combats won does not want the party unable to lose hit points
in the saved game it then writes.

**PLAN.md §5 names two of them and this build carries three**, which is
worth saying out loud rather than leaving to be noticed. The entry reads
"invulnerability and kill-all-enemies, built early because they double as
test tooling for the playthrough sweeps" — the *reason* is the test
tooling, and #196 needed a piece of it that did not exist. See the
wounding seam below.

**Invulnerability** intercepts the one resident routine every kind of
damage lands in, at its entry, and — when the record on the stack is a
party member's — writes a zero over the damage word. The program's own
routine then runs on zero and reaches its own conclusion through its own
code. An enemy's damage is left alone; the qualifier is the record's
combat-side byte, read where the program is about to read it.

**The two arguments were written down the wrong way round**, and neither
seam had ever been run against the program until #103 did it. What the
frame actually holds — observed, mid-encounter, and stated in
`seam_cheats.cpp` — is the far return address, then the *damage*, then the
record. Reading it the other way made the seam treat the damage word as a
pointer and write its zero over something else entirely: the party came
out of a scripted encounter on five hit points instead of one, neither
invulnerable nor untouched, with no line anywhere saying why. With the
frame read as it is, the same encounter ends with the party on its full
eight, and the enemies still take their damage.

The guard stays: a record is a far pointer into the program's own memory,
nothing lives in segment 0, and a frame that says otherwise gets
`decline(point_not_recognized)` rather than a write. It is what caught
this, and it costs one comparison.

**Kill-all-enemies is pulled** (#161), and **acts at the moment of the
pull** (#163). When a person has asked for it, it deals 120 points of
damage to every standing enemy exactly the way the program's own damage
routine deals damage to one: a combatant it does not finish keeps the
remainder and stays standing, in its side's count, with nothing else
touched; one it does finish is downed the way the routine downs one —
slain, held byte cleared, hit points zero, the side's count decremented,
the scratch byte cleared. The program's own end check then finds the
enemies' count at zero and ends the combat through its own logic.

120 is a **chosen debug value and not a fact about the program**. Nothing
in the fact table says what anything in this game hits for; the number is
high enough to finish what a party meets and low enough that something
genuinely tougher survives and is seen to survive. It is subtracted
saturating — 0 is the floor, and it never wraps.

The report that made this a trigger asked for two things: *"the kill all
should not trigger at the end of the round but it should be on a new
hotkey/button and trigger immediately."* #161 answered the first.

**"Immediately" is now answered too, and by giving up the address rather
than by finding a better one.** The seam has two points:

1. **a point with no address** (§"A point with no address"), offered at
   every step boundary while the pull is outstanding, which acts at the
   first step where `combat_roster_ready()` holds; and
2. **the end check**, unchanged — the once-a-round overlaid routine that
   is the only combat address these facts have ever had.

The second is not redundant, and keeping it is why this was safe to do
from a tree with no copy of the game in it. The end check is the one
address here that has been driven against the real program and seen to
end a real fight. The guard is reasoning about structures nobody has
watched at an arbitrary step. If the reasoning is wrong the guard
declines, says so once (`inert point_not_recognized`), and the pull is
served at the end of the round exactly as before — slower, not wrong.

**What the guard checks, and what it cannot.** Five conditions across
three structures, all of them facts this seam already had:

1. the game mode byte reads combat;
2. the roster head is a far pointer — nothing lives in segment 0, which
   is the interrupt vector table and the BDA, the same argument
   `spare_the_party` makes about a record on the stack;
3. every link is a far pointer too, and the list **ends** within the
   bound rather than being cut off by it;
4. somebody on the party's side is standing and somebody who is not is
   standing — a combat roster with a fight still in it;
5. and each of those sides' body counts is non-zero, so the two
   structures this seam writes agree before it writes either.

One byte is not a guard: 5 is a plausible value for an uninitialized
byte. What it **cannot** rule out is that the program is itself part-way
through walking this roster with a record pointer or a running count in a
register — this guard reads the same structures that code reads and
cannot see its registers. Two things make that survivable. Dealing damage
rather than writing a corpse leaves behind a machine state the program
produces for itself several times a round; and any count this puts wrong
is rebuilt from the held bytes at the end check before it is read to
decide the combat.

**Which is why the decrement is no longer optional.** It used to be
faithfulness rather than mechanism — the end check re-tallies both sides
from the held bytes immediately before reading them, so a count this seam
decremented at that point was overwritten a moment later. Firing
mid-round changes that: the program keeps the count by hand until the
next rebuild, and anything that reads it before then reads what this
left. So the decrement carries weight now, and the survivor branch *not*
decrementing is the same rule from the other side — a count kept by hand
only moves when a body drops.

**And whether the hit-point field is really a byte** is now load-bearing,
where before it was not. The only write this seam ever made there was a
zero, which is byte-safe either way; a remainder is not. It is read and
written as a byte, as it has been since #99, and the saturating subtract
bounds the damage if that is wrong: lowering the low byte of a wider
field lowers the number it is part of, so the worst case is less damage
than intended or a combatant finished that should not have been — never
one healed, and never the party.

`SeamCheatKillAll.ServesThePullWhereverTheProgramIs`,
`DealsDamageAndLeavesAnEnemyItCannotFinishStanding`,
`KeepsThePullUntilThereIsAFightItRecognizes`,
`DeclinesARosterThatIsNotAList` and
`IsNotConsultedAtAllUntilSomebodyPullsIt` are all of that as tests, and
`IsOnAndDoesNothingUntilSomebodyPullsIt` and `OnePullIsOneFiring` are
still the trigger itself.

**It has been run against the program**, driving `fight.rec`'s own key
script on a player-supplied copy. `--seam code-wheel` is needed as well:
without it a live driven run sits at the copy-protection challenge for
ever and never reaches a fight at all. Three pulls, at three moments:

| pulled | row at the end of the run | what it says |
| --- | --- | --- |
| frame 13500, just after `Q` starts the round | `fired=1 reached=1 waited=0` | served at the instant of the pull |
| frame 12700, before the round starts | `fired=1 reached=0 waited=8327644` | the guard declined until the roster was ready — 6.98 virtual seconds — then served |
| frame 14000, after the combat ended | `fired=0 reached=1 waiting`, with `inert point_not_recognized` | the guard declined, kept the pull, and said so |

The same script before this change, pulled before the round:
`waited=22110288` — 18.5 virtual seconds, and `reached=1`, because the
end check is arrived at exactly once per encounter. That is the
measurement §3a's `reached` was built to make possible, and it is why
`reached` stays a count of arrivals at the addressed point.

**What is still unmeasured** is the number: whether 120 finishes what a
party actually meets. A pull that leaves an enemy standing is
`debug_damage` being wrong rather than the seam.

**"Standing" is the wound status, not the byte beside it.** The record
carries a *held* byte immediately before the combat-side index, and the
routine that downs a combatant clears it — which makes it read like a
liveness flag. It is not one: the program's own still-standing test is
membership of a two-element status set (unhurt, or an animated body), and
a combatant already slain can still have the held byte set. A seam that
tested the held byte would down a body that was already down, and downing
decrements a side's body count — so it would decrement twice and leave the
program to end the combat on a count that had gone past zero.
`SeamCheatKillAll.ReadsTheStatusAndNotTheHeldByteNextToIt` is that trap,
written down as a test.

**It was inert on the real program for two milestones, and the reason
was never its facts.** Its module and offset were right all along — the
overlay is the one the file's own overlay table names, and the point is
that routine's entry. What was wrong was one layer down: the tracker
records where a module *landed when it was read*, and the end check
demonstrably executes from an address no recorded read ever covered,
because the manager moves it. §4's "Where a point lives" is the fix, and
this seam's module now carries the word the manager keeps its segment
in.

Driven through a wilderness encounter against seven soldiers, the point
fires once and the fight ends: `THE PARTY HAS WON. EACH CHARACTER
RECEIVES 107 EXPERIENCE POINTS.` The same script with the seam off is
still in that fight when the run's tick budget expires. Before the fix,
in a run that kept the battle going for thirteen rounds, the point fired
exactly once — at the read's landing, in the one round before the
manager moved the module — and never again.

**What ends a combat is the cleared held byte, not the decrement.** The
end check rebuilds both sides' counts from the roster's held bytes,
immediately before it reads them, so the count this seam decrements is
overwritten a moment later. The decrement stays because the program's own
routine does it; the cleared flag is what carries the result.

The cheap check that caught the original mistake is still the check to
run on any new seam, and it costs one extra run:

```sh
# same script, same disk, once with and once without. Since #161 the
# cheat needs the pull as well as the flag: --seam says it may act,
# --pull says somebody asked, and the frame is where in the fight.
diff <(amberfolio ... )      <(amberfolio ... --seam cheat-kill-all --pull cheat-kill-all@600)
```

Same step count and the same framebuffer means the seam did nothing.

**That check is now committed**, for `cheat-invulnerable` at least, and
so is no longer a thing anybody has to remember to run.
`tests/sessions/fight.rec` and `fight-cheat.rec` are the same script over
the same disk with the same tick budget, one flag apart: the saved party
walked twelve steps north into the slums and into a group of orcs, and
the fight handed to the computer. Without the seam a lone first-level
fighter is destroyed. With it he ends the fight standing on his full
eight hit points, the seam having fired nine times.

`fight-cheat`'s descriptor names the other as its `contrast`, which
`scripts/sweep.py` reads as an assertion and fails if it does not hold:

```
  fight-cheat  contrast ok  126 of 177 checkpoints identical, then
                            divergent from tick 274951600 to the end
```

126 checkpoints byte-identical says it is genuinely the same run up to
the moment the seam first matters; every one of the remaining 51
differing says the seam mattered and kept mattering. It compares two
files rather than two machines, so it needs no copy of the game — which
makes it the one thing about a seam-on-the-real-program that CI can
check, and it runs in the guards job on every push
(`tests/sessions/README.md`).

A seam recorded this way should get a pair. One recording proves a seam
ran; a pair proves it made the difference it claims.

#### Wounding the party (M5-E1d, #196)

The counterpart of invulnerability, and the piece of test tooling the
`docs/playable.md` legs had been missing. **Pulled at the camp screen, it
leaves every member of the party on one hit point.**

It exists because of an honest gap in another seam's evidence. The Encamp
Fix's days arithmetic and its report's exception list had never been
driven at all: every run of it ended with the party whole, because the
cures close a shipped slot's deficit before the rest is ever asked for.
What was needed was a party that had been in a hard fight and survived
it, and there was no way to get one — `docs/playable.md` leg 2's fight
*destroys* the party, no shipped save slot holds a hurt one, and a save
file this project wrote would be a save file nobody else's machine has.

**One byte per record, and it is the byte the program's own damage
routine writes.** Hand that routine `hit points - 1` for a character it
accepts and it takes the branch that writes the remainder back, leaves
the wound status where it found it, and touches nothing else — no held
flag, no side count, no scratch block. So what this seam leaves behind is
a machine state the program produces for itself every time somebody is
hit and survives. There is no damage figure to justify, because "one" is
a rule rather than a number: it is the most a cheat can take off a
character without changing anything else about them.

**It only writes to a record that routine would have written to.** The
gate is the routine's own — unhurt or an animated body — and for any
other status the routine *downs* the character instead. Wounding those
would be writing something the program would never write; downing them
would be kill-all pointed at the party. A member already on one hit point
or fewer is left alone too: zero damage on a record with no hit points is
the one input the routine's own arithmetic reads as unconscious.

**The guard is the whole of the address**, because this point has none
(§3a's address-free point, as `cheat-kill-all`'s immediate one has). What
it must establish is that DS is the program's data segment — a read
through a segment that is not is a bus cycle at an address nobody answers
for, which is exactly what the Encamp Fix's first cut did. Camp is the
tightest honest answer: the mode byte reads 2 there and nowhere else, the
camp screen is a menu loop waiting on a key so nothing is part-way
through editing a record, and it is where a person asking to be hurt is
asking — because it is where the thing that heals them lives. A pull made
anywhere else is **kept**, not refused, so pulling this and then pressing
ENCAMP serves it.

**Pulled rather than left on**, for #161's reason: a wounding left
switched on would wound the party at every camp for ever, which is a
curse rather than a cheat.

Driven, it is what makes `docs/playable.md` leg 7's third half possible —
five cures spent, `REST TIME: 30:05:15` on the program's own rest screen,
and a report whose exception list has rows in it. It is also in both
halves of the committed camp pair, which is how that pair went back to
pinning the Fix *working* (`tests/sessions/README.md`).

#### What all three owe

All three are fail-closed by construction: unavailable on any binary but
the baseline's, inert with `point_not_recognized` when what a point finds
is not what its facts describe, inert with `module_not_resident` while
the end check's module is not loaded — which the program's own record
answers at the step it is asked — and nothing on the hot path when off.

`tests/core/machine/seam_cheats_test.cpp` drives every handler at its
point with records and a roster the test lays down by the facts. That
suite passed for a month against facts that were wrong, which is the
thing to take from this section: **a seam's unit tests can only check
that the handler does what the fact table says, never that the fact table
says the truth.** Only running it against the program does that, and
`docs/playable.md` is how.

### How a wrong fact was found, in case the next one has to be

Neither cheat had been run against the program until #103 did it, and
both were wrong — one in its frame layout, one in its module *and* its
offset. Both were found the same way, by observation rather than by
reading the program:

1. **Pick something the routine must touch.** For the damage routine, the
   byte a party member's hit points live at; for the end check, the two
   bytes the side counts live at. The roster head and the record layout
   were already facts, and a handler at any point that fires during
   combat can walk the roster and print what it finds.
2. **Watch it.** A temporary print in `machine::write_memory` /
   `read_memory` for that one physical address, with the processor's
   CS:IP — which is the address of the instruction that did it.
3. **Walk back to the entry.** Turn the trace ring on and dump it at the
   moment of the access (`format_trace_report`); the steps before it are
   the routine, and the transfer into it is where the seam's point
   belongs.
4. **Ask which module that address was in — carefully.** For an overlaid
   routine, a print in `overlay_tracker::note_read` gives every load's
   file offset, length, landing range and digest; the last one covering
   the address
   before the access is the module, and the address minus its base is the
   offset.

None of that reproduces a byte of the program: what comes out is
addresses, offsets, lengths and digests, which CONTRIBUTING.md names as
facts.

**Step 4 is where this method lies to you**, and it did. "The last
recorded load covering this address" is not the same claim as "the module
this code belongs to": overlays share an arena, their landing ranges
overlap over the life of a run, and a module that was moved rather than
re-read leaves no record at all. Answering step 4 from the *loads* gave a
plausible module, a plausible offset, and a seam that worked once — for
the wrong reason, at an address a few hundred bytes past the routine's
entry, after a tally the entry would have included.

The check that settles it is the artifact, not the run: the overlay file
has an overlay table, and a module's file offset and length either match a
row of it or they do not. If step 4's answer is not a row, step 4 is
wrong. Prefer a fact you can derive from the file over one you inferred
from a trace.
