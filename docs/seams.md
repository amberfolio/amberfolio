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

Five things a handler may do, each the smallest honest version of itself
(#96), and the fifth had no implementation behind it until M5-D1 (#169):

**Register surgery** — `box.processor().regs()`. Plain state, edited in
place. The code-wheel seam is three register writes and nothing else.

**Memory surgery, as the program** — `box.processor().read_byte()` and
`write_byte()`, through the bus. A write into the video window reaches
the EGA's pipeline; a write into ROM is refused; a touch of nothing is
noticed — exactly as the program's own would be. Never `memory().ram()`:
that back door is for the machine's own writers (the loader, the BIOS
setup, a test), and a seam is not the machine. It is the program's hand,
moved from outside.

**Synthetic input** — `seam_context::inject_keystroke(scancode, ascii)`.
The keystroke goes straight into the BIOS buffer at 40:1E, which is the
keyboard-service funnel, and *not* through `input_queue`: the queue is
the host's recordable stream (`platform.h`), and a seam's keystrokes are
a consequence of the seam set, which a replay records as an initial
condition rather than as input. Nothing about how often the program
polls changes, because nothing is waited for — the key is simply there on
the next INT 16h, as one typed a moment earlier would be. PLAN.md §5 item
3 wants exactly this for the automap hotkey, and the Encamp Fix drives
the program's own loop through it.

**Control** — `seam_context::redirect(cs, ip)`, which is moving IP with
its name on.

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

**Nothing in this build is gated yet.** The code-wheel seam's gate is
#115, which is now one field in its definition; the journal's is #174,
and the table has no journal entry because nobody here has hashed one.

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
in which a handler declined reads lower now — including the "fires nine
times" in `tests/sessions/fight-cheat.session`, which nobody has
re-measured.

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
amberfolio: seams code-wheel on armed - answer the code-wheel challenge (ungated; M5 owes the gate)
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

---

## 8. Writing a seam

1. **Gather the facts** — the addresses and offsets, the module the code
   lives in, the fingerprint(s) of the binary they are facts about. Facts
   only: never a byte sequence, never anything that reproduces the
   program.
2. **Write the handler** as a `seam_handler`, in a file of its own under
   `core/src/machine/seam_<name>.cpp`, with the facts as named constants
   and the reasoning in the header comment — `seam_code_wheel.cpp` is the
   pattern, including what it is careful *not* to do (it qualifies on
   the expected operand, so the program's general string compare is left
   alone everywhere else).
3. **Define it** — a `seam_definition` with its points, and add it to
   the build's table (`all_seams()`).
4. **Test the mechanism, publicly** — a unit test that drives the
   handler at its point with state the test writes from the encoding,
   and a `tests/programs` entry where the seam's shape allows. A seam's
   addresses only mean something against the real binary; its mechanism
   has to have a test that does not need one.
5. **Verify on the game, locally** — on, the difference visible; off, the
   run's hashes are the plain machine's (#100's harness is how that is
   checked, and #101's session library is where the run is recorded).
6. **Say what it is not yet**, at the point of definition — the code-wheel
   seam's header says its possession gate is #115's — and, since M5-D3,
   says that the mechanism exists and only the field is missing, which is
   a more useful kind of honesty than the first one was.

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
| `code-wheel` | answers the copy-protection challenge (ungated; the gate mechanism is built, and turning it on is #115) | the baseline | the resident image |
| `cheat-invulnerable` | the party takes no damage | the baseline | the resident image |
| `cheat-kill-all` | every enemy takes 120 damage at once, **when you pull it** (§3a) | the baseline | the overlaid module the end check lives in |

### The debug cheats (#99)

**Two seams, not one with two switches** — PLAN.md §5's first requirement
is a toggle per seam, and the two are wanted separately: a sweep that
wants combats won does not want the party unable to lose hit points in
the saved game it then writes.

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

Both are fail-closed by construction: unavailable on any binary but the
baseline's, inert with `point_not_recognized` when the frame at a point
is not the one its facts describe, inert with `module_not_resident` while
the end check's module is not loaded — which the program's own record
answers at the step it is asked — and nothing on the hot path when off.

`tests/core/machine/seam_cheats_test.cpp` drives both handlers at their
points with records and a roster the test lays down by the facts. That
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
