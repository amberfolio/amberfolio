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

The recording's manifest names every file in the disk's root and pins it
by SHA-256, so the pair is self-checking: a disk that drifted from its
recording is refused before a step is taken. Keeping the disk beside the
recording is what lets the desktop host and `scripts/sweep.py` verify any
session with no special case for which one it is.

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

`save.rec` and `load.rec` are #105's round trip, recorded. They are two
sessions and not one because they have to be: a load is a fresh run over
the directory the save left behind, and a recording carries its starting
conditions rather than assuming them.

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

**The descriptor pins the whole disk, and the recording does not.** The
preamble walks the *root*, in the VFS's pinned order, and lists a
subdirectory by name and size alone — a digest of a directory is not a
thing. The game keeps its saves in `\SAVE\`, so a disk whose save
directory is one run further along than it was passes the preamble's
check and then diverges halfway through, and a divergence is supposed to
mean the machine changed. The descriptor's `file` and `dir` lines close
that: every path under the disk, its size and its SHA-256, compared
before a step is taken.

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
- `recording_format_version` is bumped (the line grammar changes);
- the reference device set's **attach order** changes. The canonical
  state hashes devices in attach order, so this is machine state.
  `hosts/sdl/src/main.cpp`, `core/src/abi.cpp`'s `reference_devices` and
  `tests/programs/machine_harness.cpp` all wire the same list in the same
  order, and all three have to move together;
- what `machine::reset()` leaves behind changes — the self test programs
  the PIT and the 8259 through real bus cycles, and that is where a
  session's device state starts.

Re-recording is not a way to make a red test green. If a session stops
verifying and none of the above changed, the machine changed and the
finding is real — `docs/replay.md` §5 is how to read the report, which
names the first section that disagreed.
