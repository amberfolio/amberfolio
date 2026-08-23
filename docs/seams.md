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

Four things a handler may do, each the smallest honest version of itself
(#96):

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

And the slot for what is not here yet: **a host service** —
`seam_context::call_host(which, argument)`, answered by whatever
`seam_host_services` a host attached with `seam_engine::set_host()`. The
consumers are named (`journal_open`, `automap_update`,
`save_state_changed`) and M5 fills them; today no host attaches one, and
a seam that calls out on a machine without one is told so and does
nothing.

What a handler is handed beside the machine is `seam_context`: the seam's
id, the physical address the point fired at, the base of the module it
lives in, and the image base — so a handler that reads a fact-table
offset against its module adds the right number.

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
  read landed.

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

---

## 6. The toggle surface

Three states a host shows, per seam (`seam_engine::status()`):

| state | meaning |
|---|---|
| `off` | available for this program, not enabled — the default |
| `on` | enabled; `armed` says whether every point is placed, and `reason` says why not |
| `unavailable` | not for this program (`wrong_binary`), no program yet (`no_program`), or written against another schema |

Beside those, `fired`: how many times one of the seam's handlers has
actually run since it was enabled. **`armed` is a claim about the fact
table; `fired` is a claim about the machine.** A point is armed at an
address computed from where a module was recorded, so a seam whose module
has since moved — or whose offset was never right — reports `armed`, does
nothing, and reads exactly like one that works (#131). The desktop host
prints a line per enabled seam when a run ends:

```
amberfolio: seam code-wheel armed fired=635
amberfolio: seam cheat-invulnerable armed fired=4
amberfolio: seam cheat-kill-all inert fired=0
```

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
`af_machine_seam_count/_id/_about/_state/_reason/_armed`,
`af_machine_seam_enable/_disable` (`abi.h`), wrapped by `host.mjs` and
shown by the dev page as a checkbox per seam, unavailable ones disabled
with the reason. The node smoke check toggles the probe seam on a
self-written program and asserts the difference — the same two results
the native suite asserts for `seam_probe` and `seam_probe_off`.

Seam state is **configuration**, not machine state: it survives nothing
(`machine::reset()` clears it), the serialization omits it, and a replay
records the active set as an initial condition (#100). The persisted
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
   serialization leaves it out.

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
   seam's header says its possession gate is M5's (#115), and that
   honesty is part of the pattern.

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
| `code-wheel` | answers the copy-protection challenge (ungated; the possession gate is M5's, #115) | the baseline | the resident image |
| `cheat-invulnerable` | the party takes no damage | the baseline | the resident image |
| `cheat-kill-all` | every enemy falls at the end of the round | the baseline | the overlaid module the end check lives in |

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

**Kill-all-enemies** intercepts the once-a-round end check — an overlaid
routine, and the point at which the program will next consult the combat
state — and downs every standing enemy exactly the way the damage routine
downs one: slain, held byte cleared, hit points zero, the side's count
decremented, the scratch byte cleared. The program's own end check then
finds the enemies' count at zero and ends the combat through its own
logic. Outside combat the point does nothing.

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

**It is inert on the real program today, and the reason is not its facts.**
Its module and offset are right — the overlay is the one the file's own
overlay table names, and the point is that routine's entry. What goes
wrong is one layer down: the tracker records where a module *landed when
it was read*, and the end check demonstrably executes from an address no
recorded read ever covered. An overlay manager may move a module inside
its own arena without re-reading it from disk, and a tracker that only
sees DOS reads cannot follow that. So the seam arms against a stale
landing, reports `armed`, and is pointed at nothing.

That is the honest state, and it is worse than it sounds: **`armed` is
currently a claim about the fact table, not about the machine.** A seam
that never fires is indistinguishable from one that works unless you
compare a run against the same run without it — which is exactly the
check that caught this, and is cheap:

```sh
# same script, same disk, once with and once without
diff <(amberfolio ... ) <(amberfolio ... --seam cheat-kill-all)
```

Same step count and the same framebuffer means the seam did nothing.

Both are fail-closed by construction: unavailable on any binary but the
baseline's, inert with `point_not_recognized` when the frame at a point
is not the one its facts describe, inert with `module_not_resident` while
the end check's module is not resident (a module is named by the read
that loads it — file, offset, length, and the digest of the bytes), and
nothing on the hot path when off.

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
