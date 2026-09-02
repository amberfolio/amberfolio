# Sessions

Committed recordings, and the cross-target proof they exist to be.

[`docs/replay.md`](../../docs/replay.md) is what a recording is and how
one is made. This directory is the part that gets committed: a small
number of goldens that every target must reproduce exactly.

The claim is PLAN.md §4's, and it is not a claim about a run — it is a
claim about the emulator. A recording is *keys, ticks and hashes*, so a
target that reproduces one has reproduced every byte of RAM, every
device's registers, the scheduler's deadlines, the DOS handle table and
the framebuffer, at every checkpoint, from the same starting conditions.
Two builds that agreed about the answer and disagreed about the machine
would fail here and nowhere else.

Nothing in a recording is content. A checkpoint is a SHA-256 and a tick;
the manifest is names, sizes and digests. That is what makes these
committable at all (PLAN.md §6: "hashes are committable; screen content
never is"), and it is why the programs recorded here are the
repository's own.

## What a session is

Three things, named alike:

    tests/sessions/spin.rec       the recording
    tests/sessions/spin.session   what it is, and which disk it wants
    tests/sessions/spin/          the disk it was recorded against

The recording's manifest names the disk's files and pins them by SHA-256,
so the pair is self-checking: a disk that drifted from its recording is
refused before a step is taken. How far the manifest reaches is the
recording's own to say — these seven are format 1 and name the root;
anything recorded from now on names the whole tree (`docs/replay.md` §1).
Keeping the disk beside the recording is what lets the desktop host and
`scripts/sweep.py` verify any session with no special case for which one
it is.

The **descriptor** is the third, and it is the runner's rather than the
machine's. One line of it decides everything else:

    disk spin         the disk is that directory, here in the tree
    disk external     the disk is the player's own, and is not

A `.rec` with no descriptor beside it is a failure and not a skip. The
runner cannot tell "the maintainer's own copy" from "a directory somebody
forgot to commit" by looking, and guessing from whether a directory
happens to exist is exactly how a missing commit would become a quiet
skip.

| Session | Disk | What it pins |
| --- | --- | --- |
| `spin.rec` | `spin/SPIN.EXE` — 34 bytes, ten of them `JMP $` behind an MZ header | four frames of a machine doing nothing but keeping time: the PIT, the 8259, the scheduler, the renderer's frame deadline and the clock |
| `party.rec` | external, pristine | `docs/playable.md` leg 0 — the code-wheel challenge answered by its seam, a character generated, named and put in the party, and `BEGIN ADVENTURING` into the opening story event at 15,1 W. 15,655 frames, 144 checkpoints, 40 key events |
| `save.rec` | external, pristine | legs 0, 1 and 3 as one run — the same party, the guide's tour taken to 0,4 W, and the game saved into slot A from the camp screen. 23,032 frames, 254 checkpoints, 148 key events. The write path is in it: the slot file created, the party's character files moved in and unlinked |
| `load.rec` | external, **the disk `save.rec` wrote** | the other half of the round trip — `LOAD SAVED GAME`, slot A, and the party back at 0,4 W with the same character, the same AC and the same hit points. 12,069 frames, 100 checkpoints |
| `fight.rec` | external, the disk `save.rec` wrote | leg 2 — the saved party loaded, walked twelve steps north into the slums and into a group of orcs, the fight handed to the computer with `QUICK`. A lone first-level fighter does not survive it: `THE END`, the party destroyed. 20,115 frames, 177 checkpoints |
| `fight-cheat.rec` | the same | the same script, the same disk and the same tick budget with **`cheat-invulnerable` on and nothing else changed**: the fighter comes out standing on his full eight hit points at `CONTINUE BATTLE`. The seam fires nine times |
| `temple.rec` | external, **the shipped save slots** | leg 5 — slot A loaded, routed to the healing temple at 3,1, and a cure bought: `CURE BLINDNESS` cast on a fighter who is not blind and paid for at a thousand gold, which is two hundred platinum off his sheet. The sheet is read at the end, so the session pins the money as well as the machine. 18,801 frames, 181 checkpoints, 68 key events |
| `camp.rec` | external, the shipped save slots | leg 7 without the enhancement — slot B loaded, `ENCAMP`, the party wounded to one hit point each by a pulled `cheat-wound-party`, and `REST` chosen at the camp menu. The rest screen comes up with the duration the program's own wrapper dialled and waits for a key that never comes. 13,401 frames, 112 checkpoints |
| `camp-fix.rec` | the same | the same script with **`encamp-fix` on** as well, pressing the letter the seam puts on the camp menu instead of the menu's own Rest (M5-E1 #172, M5-E1a #186, M5-E1d #196): the bar is spliced, the letter comes back off the program's own menu-bar routine, the five cures the party holds are spent through the program's own cast driver, thirty days are dialled on the program's own rest clock, and the report is drawn when the game's own wandering-monster check ends the rest. `fired=11` |

| `walk.rec` | external, the shipped save slots | leg 8 without the enhancement — slot A loaded and walked forty-eight moves through New Phlan to the armourer at 8,11, with a `Tab` in the key stream that nothing claims. 19,201 frames, 203 checkpoints, 106 key events |
| `walk-map.rec` | the same | the same script with **`automap` on**: `Tab` is taken out of the keystroke buffer before the program's own key routine looks, and the panel is drawn into the EGA planes over the party roster — brown streets, white building fronts in the colour of the tiles those buildings are drawn with, and yellow door leaves — filling in behind the party as it walks, under the zone's name set in the game's own font (M5-E2 #173, M5-E2a, M5-E2b). The seam is reached 1,283,230 times |
| `reader.rec` | external, the shipped save slots | **the journal reader** (M5-E4 #175), over `tests/visual/reader-store.txt` — a store of this project's own sentences, named by the descriptor's `journal-store` line. F1 to the prompt, the section cycled through ENTRY, TALE and PROCLAMATION and back, Escape out of it, entry three opened by its number, paged forward with F1 and back with Backspace, and Escape again. 156 checkpoints, 30 key events. The panel's pixels are in every hash from the entry opening onwards |
| `notes.rec` | the same | **the journal's log** (M5-E4b #222, #230) — `Notes` on the party's own bar opens a log with nothing in it, and then six keys the adventuring screen would answer (`S`, `C`, `L` and three that walk) reach nothing at all while it is up. 146 checkpoints, 24 key events. This is #230's regression net as a hash rather than as a still |
| `cite.rec` | external, pristine | **a real citation** (#232) — a new party through the city to the hall at 3,4 facing east, whose entrance event names four proclamations in one sentence, and the first of them on the screen with nobody having pressed a key. 291 checkpoints, 158 key events. Its store is **external too**, pinned by digest in the descriptor: it is a real ingestion of a real player's own journal and no byte of it may enter this tree |

`save.rec` and `load.rec` are #105's round trip, recorded. They are two
sessions and not one because they have to be: a load is a fresh run over
the directory the save left behind, and a recording carries its starting
conditions rather than assuming them.

## A pair, and the one check CI can make about a game session

`temple.rec` is #104's first recorded transaction. Its disk is a third
snapshot — the installation's own save slots, untouched — because the
party a city service wants is one with money in it, and neither the
pristine disk nor the one `save.rec` wrote has one.

`fight.rec` and `fight-cheat.rec` are the same run one flag apart, and
the second's descriptor says so:

    contrast fight

which is an assertion, not a note. The two must agree checkpoint for
checkpoint until the seam first matters and disagree from there to the
end, and `scripts/sweep.py` fails the pair if they do not:

```
  fight-cheat  contrast ok  126 of 177 checkpoints identical, then
                            divergent from tick 274951600 to the end
```

`camp.rec` and `camp-fix.rec` are the second such pair, for the Encamp
Fix (#172, #186, #196):

```
  camp-fix     contrast ok  91 of 112 checkpoints identical, then
                            divergent from tick 216799088 to the end
```

The two halves differ by **one keystroke and nothing else**: where the
plain one presses the camp menu's own Rest, the other presses the letter
the seam puts beside it, at the same tick. That matters for more than
tidiness — a frame that carries an input is checkpointed whatever the
cadence says (`docs/replay.md` §3), and `contrast_of` refuses a pair that
checkpoints at different ticks, rightly, because they are not the same
run to compare. **A pair must therefore put its inputs at the same ticks
in both halves**, which is easy when the difference is which key, and
needs arranging when it is an extra input: this pair carried a `--pull`
at frame 10368 before M5-E1a (#186) took the pull away, and 10368 was a
multiple of the 128-frame cadence for exactly this reason.

**What this pair pins, and how it got it back** (M5-E1d, #196). Between
#192 and #196 it pinned less than it used to: slot C's party is whole and
the Fix declines a party with nothing to rest for, so the two halves
diverged because one pressed `R` and rested and the other pressed `F` and
did not — which a seam that had stopped working altogether would also
produce. The splice was still caught, because the bar reads `FIX` in one
run and not the other and the framebuffer is in every checkpoint. The
healing was not.

What was missing was a **wounded** party, and no shipped save slot holds
one. It is the third debug cheat that supplies it: both halves enable
`cheat-wound-party` and pull it at the same tick, and the party is on one
hit point each by the time either of them presses anything. The wounding
is therefore part of the run the two have *in common* — the difference
between them is still exactly one keystroke — and what it buys is a Fix
with real work to do. Five cures spent, thirty days dialled, a rest the
game interrupts and a report with an exception list in it are all inside
the divergent half now, so a seam that quietly stopped doing any of them
would move a checkpoint.

**A cheat in a golden is not free**, and the two things it costs are
worth naming. The pair no longer pins what an *unaided* slot B does at
the camp screen, because neither half is unaided any more; and it pins
`cheat-wound-party` as well as `encamp-fix`, so a change to the wounding
breaks a golden that is not about wounding. Both were judged cheaper than
a pair that could not tell a working Fix from a broken one.

`walk.rec` and `walk-map.rec` are the third pair, for the automap
(#173), and the only one so far whose difference is a **picture**:

```
  walk-map     contrast ok  90 of 203 checkpoints identical, then
                            divergent from tick 218787888 to the end
```

The halves differ by **nothing but the flag** — the same disk, the same
tick budget, the same hundred and six key events including the `Tab`. So
the ninety identical checkpoints are the fidelity claim made on a real
game run rather than on a synthetic one: the seam is on, armed at five
addresses and reached over a million times, and until somebody presses
`Tab` the machine is the machine it would have been. Tick 218,787,888 is
three frames after the `Tab`, which is how long the program takes to
poll.

What only this pair can catch is the panel *itself*. A checkpoint hashes
the framebuffer, so a map drawn in the wrong place, in the wrong colour,
one cell out, or not at all moves a checkpoint here and nowhere else —
the addresses and the mechanism have unit tests, and only this says the
picture is still the picture.

**This exists because a seam has twice been on, armed, reporting itself,
and doing nothing at all** — `cheat-invulnerable` pointed at a routine
that was not the damage routine (#129), and `cheat-kill-all` arming at an
address its module had since been moved away from (#131). The suite was
green throughout both, because a seam's unit tests check the handler
against the fact table and never the fact table against the program.
`docs/seams.md` therefore asks for the only check that catches it: run
the same script *without* the seam and compare. Identical step count and
framebuffer means it did nothing.

Two committed recordings are that check with nobody having to remember
to make it. And because it compares *files*, it needs no disk and no
build tree — which makes it **the one thing about a game session that CI
can verify**, and the only line in this directory's table that is not a
skip on a machine without the player's copy.

The failure modes it distinguishes, each with its own case in
`scripts/test-sweep.sh`: identical throughout (the change made no
difference), divergent from the first checkpoint (not the same run up to
the change), divergent and then rejoined (the difference did not last),
and checkpoints at different ticks (not the same script, or not the same
cadence — a comparison that would otherwise pass for the wrong reason).

## A session whose disk cannot be committed

A recording made of a *game* is committable — it is keys, ticks and
hashes, and reproduces nothing — but the disk it was made over is the
player's own copy, and no byte of that may enter this tree (PLAN.md §6).
The decision on #101 is to commit the recording anyway and have the
runner say, plainly, when it cannot check it:

```sh
python3 scripts/sweep.py --game-disk /path/to/a/pristine/copy
AMBERFOLIO_GAME_DISK=/path/to/a/copy python3 scripts/sweep.py
```

`--game-disk` is repeatable, and a library of any size needs it to be. A
session begins wherever the last one left off, so `load.rec` starts from
the directory `save.rec` wrote and wants a different snapshot from the
one `party.rec` wants. Which candidate belongs to which session is never
a guess: a descriptor pins its disk exactly, so at most one of them can
match, and a session whose disk is among them runs while the rest are
skipped by name.

so there are three outcomes rather than two, and the third is the one
that has to be impossible to misread:

| | |
| --- | --- |
| `ok` | the target reproduced the recording |
| `FAIL` | it did not, and that is a finding about the machine |
| `SKIP` | nothing was checked, and here is what was missing |

A sweep that verified nothing must never read as a sweep that passed. So
the skip is spelled in capitals beside `ok`, the summary names every
session it applied to, and a run in which nothing verified at all says so
and exits non-zero. `scripts/test-sweep.sh` asserts each of those on a
throwaway library, because "it did not read as a pass" is a property of
an output and an output nobody asserts is an output that drifts.

**The descriptor pins the whole disk, and these seven recordings do
not.** They are format 1 (`docs/replay.md` §7), whose preamble walks the
*root* and lists a subdirectory by name and size alone. The game keeps
its saves in `\SAVE\`, so a disk whose save directory is one run further
along than it was passes such a preamble's check and then diverges
halfway through, and a divergence is supposed to mean the machine
changed. The descriptor's `file` and `dir` lines close that: every path
under the disk, its size and its SHA-256, compared before a step is
taken.

Format 2 closes it inside the recording too (#155): its manifest names
every directory and every file at every depth, in the same `\`-joined
spelling the descriptor uses, so a `.rec` used **without** its descriptor
— which is every use of `af_machine_verify_recording`, and the browser's
only one — refuses the wrong disk by name. That does not retire the
descriptor. It pins what is on the maintainer's shelf, which is how a
candidate directory is *matched to a session* in the first place
(`--game-disk` is repeatable, and `Session.disk()` picks by comparing);
a recording can only say whether the disk it was handed is the right one.
Nothing here changes for these seven, and re-recording them to gain the
recursing manifest is not on the list below.

    python3 scripts/sweep.py --pin NAME --game-disk /path/to/copy

writes them. Run it once, over the same directory the recording was made
against — which is a **pristine snapshot** and not a directory being
played in, since the comparison is exact in both directions and a file
the pin does not name is as much "not that disk" as one it cannot find.
`docs/playable.md` already asks for that snapshot; the sweep copies it
before every run, which is the part a person driving by hand has to
remember and this does not.

Comparing rather than running is also what keeps the answer honest. A
disk that is not the recorded one says nothing whatever about the
emulator, and reporting it as a divergence would be a finding about the
machine that was really a finding about a directory.

The suites do not see these at all: the native `SessionLibrary` case and
the wasm smoke test read the session directory as source, so a session
whose disk is not in it cannot be handed to either. The table says so per
session rather than leaving the rows out — a table that omitted them
would read as a table of everything. **So a game session is checked by
the desktop host only**, and the cross-target claim below rests on the
sessions whose disks are here.

## A document a session cannot carry at all (#115)

Every session above that drives the real program past its copy-protection
challenge does so with `--seam code-wheel` on, and since #115 that seam
has a **possession gate**: it does nothing until the player presents the
code wheel the enhancement is *for* (PLAN.md §5 item 1). So those
descriptors name it:

    document 0db301ae...6586fd

By digest and by nothing else, because a document is somebody's own PDF
and no byte of it enters this tree. `scripts/sweep.py` takes `--document`
(a file or a directory of them, repeatable) or `$AMBERFOLIO_DOCUMENT`,
and when it finds none with that digest the session is skipped and said
so.

The machine's own answer is worth knowing, because it is the good kind: a
replay whose recording names a gated seam and whose player has not
presented the document is **refused before a step is taken**, naming the
condition — `a recorded seam is gated on a document that has not been
presented`. Not a divergence halfway through, which is what a gate
applied silently would have produced.

## A store a session cannot carry either (#235)

A recording is keys, ticks and hashes, and for three of the sessions
above there is a second input: the journal reader's **store**. What the
reader draws out of it is in the framebuffer, the framebuffer is in every
checkpoint hash, and so a replay handed a different store than the
recording was made over diverges. Measured while `reader.rec` was made:
replayed against another store, or against none, it parts company at the
first checkpoint after the entry opens, in the `devices` section — the
EGA planes, which is the panel.

That was #175's one loose end, and the fix is one line in a descriptor,
shaped exactly like the `disk` line above it:

    journal-store tests/visual/reader-store.txt
    journal-store external <sha256>

The first is a store this repository carries, because it is this
project's own sentences. The second is a player's own ingestion, which
never enters this tree: the digest is the only thing about it that may be
written down, and it is what says the store on this machine is the store
the recording was made over. `scripts/sweep.py` takes `--journal-store`
(a file or a directory of them, repeatable) or `$AMBERFOLIO_JOURNAL_STORE`,
**copies** the one it finds before running — a run writes its own log back
into a store when it ends — and when it finds none, the session is
skipped and said so.

## How often a session checkpoints

Not every frame, and this is #101's decision.

A checkpoint hashes every byte of RAM. At one a frame that is about a
megabyte of SHA-256 sixty times a virtual second, which makes a debug
build roughly thirty times slower than the same run without it, and each
line with its section hashes is about four hundred bytes. Both ends of
that are fine for a run somebody is pointing at a problem and neither is
fine for a committed session: a leg of the game is fifteen to twenty
thousand frames, so one a frame is a recording of six or seven megabytes
that took half an hour to make, against a content guard that refuses a
file over 256 KiB.

So the recorder spaces them — `--record-every N` on the desktop host —
and a game session uses **128**, a little over two virtual seconds. A
twenty-thousand-frame leg is then about 160 checkpoints and some tens of
kilobytes, and the run records at very nearly the speed it runs at.

What a sparse cadence costs is *where* a divergence is localized, never
whether one is found: every key still has to land on the tick it was
recorded at, and the run still has to reach `end`. What it must not cost
is the moments worth pinning, so two frames are checkpointed whatever the
cadence says —

- **a frame that posted a key**, because what a game session is evidence
  for is that the machine answered *that* keystroke the way it did;
- **the frame the run ends on**, which for a program that exits carries
  the `stopped` marker a replaying host needs before it can reach the
  tick at all (`docs/replay.md` §4), and for a run a budget ended is the
  only record of where it got to.

`sdl-host-records-and-replays` records the same run twice, at one
checkpoint a frame and at one every eight, and asserts that the cadence
changed what was written down and not what happened: the two recordings
end on the same tick and the same step count.

`spin/SPIN.EXE` is also the only file in this repository that is not
text, and the content guard
([`scripts/check-clean.sh`](../../scripts/check-clean.sh)) names its path
outright: everything else that is not text is refused, wherever it turns
up. A session that needs a second such program needs a line there too.

`spin.rec` is deliberately the least interesting run that could fail. The
program executes one instruction forever, so anything that differs
between two targets differs because the *machine* does — a device's
arithmetic, the scheduler's tie-break, the order a state section is
written in — and not because a program went two ways. It caught two such
things while it was being written: an attach order that differed between
the ABI and the hosts, and a machine that had never had its RESET line
pulled.

It is also the cheapest thing here to outgrow. `spin.rec` is four frames
and carries no section hashes, which is why it is 566 bytes; a session
over `synthetic_boot` (`tests/programs`) would pin considerably more
machine at the same cost, and is the obvious next one to add — it
unpacks itself, loads a module off the filesystem, far-calls into it
through a relocated pointer, and calls every service M3 added. What
stands in the way is only that nothing in `tests/programs` records yet;
the desktop host is the only recorder there is.

## Who checks them

Three, on every push, from the same file:

- **native** — `SessionLibrary.*` in `tests/core/machine/session_test.cpp`,
  through `af_machine_verify_recording`.
- **wasm** — `hosts/web/tests/smoke.mjs`, through the same ABI call, in a
  build that shares no compiler, no standard library and no SHA-256
  implementation with the native one.
- **desktop** — `sdl-host-verifies-a-session`, through the host's own
  `--replay`. Worth being a third and not a repetition: that host builds
  its device set with its own code rather than through
  `af_machine_attach_reference_devices()`, so a wiring that drifted from
  the ABI's would pass the other two and fail here.

The first two also tamper with a checkpoint hash and require the refusal.
A golden that cannot fail is not one.

    python3 scripts/sweep.py

runs all three over every session and prints one table. A target that is
not built is skipped and said so, never counted as a pass.

A session whose disk is not in the tree gets the third of those and only
the third — see above.

## When one of these has to change

A session is re-recorded when — and only when — the machine it describes
legitimately changes:

- `state_format_version` is bumped (a device grows a register, a section
  is added or reordered);
- `recording_format_oldest_read` is bumped — a recording *format* is
  retired. Bumping `recording_format_version` alone is not on this list:
  a player reads every version it has ever written, the way that version
  wrote it, so a grammar that grew leaves these seven verifying
  untouched (`docs/replay.md` §7). That is deliberate, and #155 is why:
  six of these cannot be re-recorded from this tree at all. #161 grew the
  grammar a second time — a `pull` line, for a seam trigger somebody
  pulled — and these seven were untouched again: none of them carries
  one, and none enables a seam that takes a trigger;
- the reference device set's **attach order** changes. The canonical
  state hashes devices in attach order, so this is machine state.
  `hosts/sdl/src/main.cpp`, `core/src/abi.cpp`'s `reference_devices` and
  `tests/programs/machine_harness.cpp` all wire the same list in the same
  order, and all three have to move together;
- what `machine::reset()` leaves behind changes — the self test programs
  the PIT and the 8259 through real bus cycles, and that is where a
  session's device state starts;
- **a seam the session turns on legitimately changes what it does.** This
  entry was missing until #192, and its absence is why `camp-fix.rec` sat
  stale across two commits: the Fix stopped dialling a day for a party
  that was already whole, which is a fix and not a regression, and its
  recording was of a run that no longer happens. A session that names a
  seam is only ever as current as that seam. It is also the entry most
  easily abused, so the test is the same one as above — the change was
  *chosen*, argued on an issue and visible in the seam's own source — and
  never that a red line went green.

Re-recording is not a way to make a red test green. If a session stops
verifying and none of the above changed, the machine changed and the
finding is real — `docs/replay.md` §5 is how to read the report, which
names the first section that disagreed.
