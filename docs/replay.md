# Replay

How a run is recorded, how a recording is verified, and what a golden is.

This is the sibling of [`seams.md`](seams.md). M4-R1 (#100) of the plan
of record (#110) built the harness; M4-R2 (#101) is the session library
that uses it. Both began here and are finished at the closeout (#109).

The short version:

> The machine's only clock is virtual (`machine/clock.h`), and everything
> nondeterministic it consumes arrives through the platform interface,
> stamped with that clock. So a run is **keys, ticks and hashes**: record
> the initial conditions, the keys with their ticks, the wall-clock seeds,
> and a hash of the machine's state at checkpoints — and a replay that
> delivers the same keys at the same ticks reproduces the run exactly, on
> every target. Nothing in a recording is content; that is what lets the
> session library be committed.

- [1. What a recording is](#1-what-a-recording-is)
- [2. The canonical state](#2-the-canonical-state)
- [3. Recording](#3-recording)
- [4. Verifying](#4-verifying)
- [5. What can go wrong, and what the report says](#5-what-can-go-wrong-and-what-the-report-says)
- [6. The guard that makes it possible](#6-the-guard-that-makes-it-possible)
- [7. Versions](#7-versions)

---

## 1. What a recording is

Plain text (`machine/replay.h`), one line per fact:

```
amberfolio-recording 3 state=1
program BOOT.EXE 3f1c…
tail 202d4649525354 4c49474854
speed 256
seam probe
dir SAVE
file SAVE\CHARLIST.TXT 285 b677…
file SAVE\SAVGAMA.DAT 1024 4c9e…
file BOOT.EXE 2144 3f1c…
file OVL.BIN 9 a7e0…
key 5000 1e down
pull 120000 probe
checkpoint 1193182 1193182 9b2d… clock=… cpu=… ram=… devices=… …
end 2000000 2000000
```

The **initial conditions** come first: the program and its fingerprint,
the command tail (as hex, because it usually begins with a space), the
speed (the step cost in 1/256ths of a tick — a replay is made at the
speed it was recorded at), every seam that was on, and the **manifest**.
Then the **stream**, in tick order: wall-clock seeds, key events, seam
triggers, checkpoints, and the end.

### Why a pull is in the stream and not the preamble (#161)

A `seam` line says the seam was **on**, which is a fact about how the run
was set up. A `pull` line says a person asked it to act, at a tick, which
is something they did to a running machine — the same kind of thing a
keystroke is, and recorded the same way. A seam that is pulled rather
than left on (`docs/seams.md` §3a) acts once per pull, at the first
arrival at its point afterwards, so *which* arrival acts is decided by
the tick and by nothing else. A recording that carried the seam and not
the pull would replay a run in which the cheat never fired.

A pull that the engine refuses on replay — the seam is off, or is not one
that takes a trigger — is a **divergence**, not a warning: the recording
says the run had one.

### The manifest

The manifest is the statement of **what disk the run started from**, and
since #155 it names the whole of it: every directory and every file, at
every depth. A `PATH` is `\`-joined and relative to the root, with no
leading `\` — the spelling
[`tests/sessions/*.session`](../tests/sessions/README.md) already uses
for the same facts.

It recurses because a recording is not only about the program. Since #105
a session **loads a saved game**, and every byte under `\SAVE\` decides
what that run does. A manifest that stopped at the root let a replay begin
from a different saved party and say nothing about it, diverging thousands
of frames later at a checkpoint hash — which reads as a finding about the
machine and is really a finding about a directory. `load.rec` and
`temple.rec` are exactly that shape. The manifest exists to convert that
failure into a legible one, and now it does: §5's `refused … path=…`.

- **The order** is depth first, each directory's entries in the VFS's
  pinned name order (`dos_name_less`), a directory's own line before its
  contents. Nothing about it depends on how a backend enumerates, which
  is what makes two recordings of the same disk the same bytes. It is
  also exactly lexicographic order over the component sequences, a path
  sorting before every path it is a prefix of — so `SAVE`, then
  everything under `SAVE`, then `SAVED.TXT`. That is what lets a
  mismatch be read as "one side has an entry the other has not" rather
  than only as "these differ".
- **A directory keeps a line of its own**, `dir PATH`, with no size and
  no digest — a directory has neither (its size is a fiction, and a
  digest of one is not a thing). It is not made redundant by listing its
  contents: an *empty* directory is a fact about a disk that nothing else
  records, and a manifest that skipped one would call two different disks
  the same.
- **Depth is bounded by `dos_path::max_depth`** (8), not by "one level".
  A disk with anything deeper is refused — at recording time the
  preamble is not written at all, at checking time the report says so —
  rather than walked as far as it goes, because a truncated manifest
  would pin a disk that is not the disk. Only the directory-backed host
  can present one: nothing in the in-memory backend can be created below
  the eighth component in the first place.
- **Entries are bounded too**, at `replay_max_manifest_entries` (512,
  files and directories together), and a disk past it is refused rather
  than half-described. Not the in-memory backend's 192, which is a bound
  on what a *browser* can hold: the largest disk this repository's
  session library pins is 193 entries — an installation with its save
  slots filled, which is the shape this all exists for — and a cap set
  at the real high-water mark is a cap that refuses the next disk.

The manifest is therefore some tens of kilobytes of fixed storage in a
`replay_player`, which is why `af_machine_verify_recording` keeps its one
in static storage rather than on the stack (`core/src/abi.cpp`, the same
reasoning the machine handle itself is there for).

Version 1 recordings — the seven in `tests/sessions/` — name the root and
nothing below it, and are read that way. §7 is the rule.

A checkpoint is the tick, the step count, the whole-state hash, and
optionally the first eight bytes of each section's hash. `end` is where
the recording stopped; reaching it means everything before it held.

Everything here is a fact about a run, none of it a byte of a program or
a pixel of a screen (PLAN.md §6: "hashes are committable; screen content
never is").

---

## 2. The canonical state

`machine/state.h` is the versioned, sectioned serialization a checkpoint
hashes. Thirteen sections, in order: `clock`, `cpu`, `ram`, `devices`,
`scheduler`, `keyboard`, `dos`, `wall`, `input`, `console`, `audio`,
`display`, `stop`. Every device writes its own architectural state
through `device::save_state()`; the platform classes write theirs; the
machine writes the order.

**In**: everything the program can observe or that decides what it
observes next — registers and interrupt latches, the clock and step
count, every byte of RAM, every device's architectural state in attach
order, the armed deadlines, the DOS handle table and the position of
every open file, the wall seed, the input still queued, the console bytes
not yet drained, the speaker's edge list as a count and a running digest,
the framebuffer and its generation, and the stop record.

**Out**, by decision, and the reasons matter as much as the list:

| Out | Because |
| --- | --- |
| the speed governor | configuration; the recording names it as an initial condition |
| the seam engine and its toggles | configuration (`seam.h`); the recording names each seam that was on |
| the overlay tracker | an observation of machine events, rebuilt by replaying them |
| the trace ring, the first-touch notices | diagnostic bookkeeping — a run with `--trace` and one without must hash alike |
| every float audio sample | output, not state; the edge list is the canonical thing (`platform.h`) |
| the filesystem's contents | the host's; captured as a manifest of paths, sizes and SHA-256s in the preamble |

`state_format_version` is bumped when the bytes change — a device grows a
register, a section is added — and a bump invalidates every golden. A
recording names the version it was hashed under and a player refuses to
compare across versions, so a format change reports itself as a format
change rather than as a thousand divergences. Bumping it means
re-recording the session library (#101); it is a decision, never a side
effect.

---

## 3. Recording

The desktop host records with `--record FILE`:

```sh
amberfolio ./disk GAME.EXE --record session.rec
```

The preamble is written before SDL is even up, so that a run which cannot
be described is refused before a window opens. Then the loop adds three
things and nothing else:

- a `key` line **where a key is posted**, stamped with `machine::time()`
  — the machine's clock is the only stamp a key has, and the post is the
  only moment the machine can see one;
- a `pull` line **where a seam's trigger is pulled**, stamped the same
  way and for the same reason. The host key (Pause/Break) and
  `--pull ID@FRAME` both go through it;
- a `checkpoint` **where a frame ends**, taken after the slice and before
  the frame is presented;
- an `end` line where the run does.

The frame boundary is the machine's own (`renderer::frame_period`, ticks
off the virtual clock and never the wall), which is what makes a
recording made on one target name ticks a replay reaches on another.

**Recording is not free.** A checkpoint hashes every byte of RAM, so the
default cadence — one a frame — costs about a megabyte of SHA-256 sixty
times a virtual second, and makes a debug-build run roughly thirty times
slower than the same run without it. Each line is about four hundred
bytes. That is the right trade for a tool a person points at a problem;
it is *not* the right trade for the committed session library, whose
files also have to stay under the content guard's 256 KiB ceiling.

So `--record-every N` spaces them, and #101 picked **128** — a little
over two virtual seconds — for a session recorded from the game. A leg of
fifteen to twenty thousand frames is then a couple of hundred checkpoints
and some tens of kilobytes, and the run records at very nearly the speed
it runs at, which is what makes a game-length recording possible to make
at all rather than merely possible to store.

A sparse cadence costs *where* a divergence is localized and nothing
else: every key still lands on the tick it was recorded at, and the run
still has to reach `end`. Two frames are checkpointed whatever N says —
**one that carried an input**, a key or a pull, because that is the
moment a recorded run is evidence about, and **the one the run ends on**, which is where a
`stopped` marker lives and, for a run a budget ended, the only record of
how far it got. `tests/sessions/README.md` has the arithmetic;
`sdl-host-records-and-replays` records one run at both cadences and
asserts they end on the same tick and the same step count, which is the
property that matters: the cadence changes what is written down, not what
happened.

---

## 4. Verifying

```sh
amberfolio ./disk GAME.EXE --replay session.rec
```

The recording decides the speed, the seams and every key, and the player
applies all three before it checks them. `--speed`, `--seam` and
`--press` are therefore refused alongside `--replay` rather than silently
agreed with, and `--record` and `--replay` together are refused as the
two halves of one thing.

The player (`machine::replay_player`) lives in core because two hosts and
a test harness all have to verify the *same* recording the *same* way,
or "verified on all four targets" means four readings of it. It parses,
checks the initial conditions, delivers the events and compares the
hashes. It never runs the machine — that is the host's loop:

```
run to min(next_tick(), own frame boundary) → apply() → repeat
```

### Why `next_tick()` is not "the next line"

A machine stops **inside** a step. `machine::run()` is

```cpp
while (now_ < until) {
  if (step() == cpu::step_status::stopped) break;
  ++result.steps;
}
```

so the step that exits or refuses is not counted and its ticks are not
spent. A stopped machine and the machine one step short of stopping
therefore stand at **the same tick and the same step count**, differing
only in having stopped — and the only way to get from the second to the
first is to ask the machine to run *past* that tick.

A recording's last checkpoint is almost always the stopped one. A host
held at its tick would run to it, find nothing left to do, and compare a
machine that was never given the chance to stop. So a checkpoint of a
stopped machine is marked `stopped` on its line, and `next_tick()`
answers `never` for it: the host runs on to its own frame boundary, the
machine stops where it stopped before, and the ticks agree because
stopping costs none.

Everything else does hold the host to its exact tick. A key or a wall
seed consumed a tick late is a different run, and `end` is where the
recording stops being one.

This was found the way such things are: the first round trip of a program
that exits diverged in one section, `dos`, at the last checkpoint of
sixty-one, with the step and tick counts identical — the recorded machine
had closed its files on the way out and the replayed one had not yet been
allowed to. `Replay.ARecordingThatEndsInAStopIsReachedByRunningPastIt`
and its companion are the regression tests.

---

## 5. What can go wrong, and what the report says

One line, `amberfolio: replay …`, naming the first thing that differed
and where. The exit code is the run's answer, ahead of the program's own,
on the same reasoning as `--verify`: a run asked to check itself against
a recording is answering the check's question.

| Report | What happened |
| --- | --- |
| `verified checkpoints=N keys=K pulls=P` | every condition matched and `end` was reached; the process then returns the program's own exit code |
| `refused line=L why=…` | not a recording this player reads, or the initial conditions do not match — the wrong program, the wrong speed, the wrong seams, a file that is not the file |
| `refused … why=… path=SAVE\CHARLIST.TXT` | the manifest is what did not match, and that is the entry it is about: a file whose fingerprint or size differs, one the disk has and the recording does not name, one the recording names and the disk has not got, or a directory where the recording names a file. This is #155's whole point — a disk that is not the recorded one is refused up front by name, rather than diverging later at a checkpoint |
| `diverged line=L tick=T section=S expected=… actual=…` | a checkpoint's hash was not the machine's; `section` is the first of the thirteen to disagree |
| `diverged … why=the machine ran past an event's tick` | the host overran an event — a frame period that is not the recorder's, or a `stopped` marker that is not true |
| `refused … why=the recording has no end line` | the recording was cut short; incomplete, not diverged, and told apart on purpose |
| `in progress checkpoints=N` | a budget ended the run before `end`; a prefix verified, and a prefix is not the run |

`ctest -L smoke -R records-and-replays` is the round trip as one case:
record the composite with a scripted keystroke, replay it with nothing
but the recording, then tamper with a checkpoint hash and require the
refusal. A check that cannot fail is not one.

And `scripts/sweep.py` runs every committed session
([`tests/sessions/`](../tests/sessions/README.md)) against every target
that can verify one — the desktop host, the native suite and the wasm
module under node — and prints one table. A target that is not built is
skipped and said so, never counted as a pass, and so is a session
recorded from a game over a disk this machine does not have: the
recording is committed, the disk never can be, and "this disk is not that
disk" is a third answer beside verified and diverged rather than a
quieter spelling of the first.

---

## 6. The guard that makes it possible

All of this rests on one property: **virtual time is the only clock.**
Nothing under `core/` may read the host's. That was prose in
`machine/clock.h` from M2 and is now `scripts/check-host-time.sh`, run in
CI's guards job on every push (#78). It refuses the standard library's
clocks and the libc time calls under `core/`, and leaves the hosts —
which are *supposed* to know what time it is — alone.

`scripts/test-guards.sh` is its self-test, including the cases that must
*not* trip: `machine::time()` and `wall_time` are names core uses
legitimately, and a guard that caught them would be one people learn to
work around.

The wall clock a program reads through INT 21h is not an exception to
this. It is a **seed**, set once by the host and read as
`wall().at(time())` — a fixed origin plus virtual elapsed time — so it
advances with the machine and is recordable as a `wall` line at a tick.
The desktop host does not seed it today, which is why no recording here
carries one.

---

## 7. Versions

Two, and they are independent:

- **`recording_format_version`** (`machine/replay.h`) — the line grammar.
  A recorder writes this one. It is **3**: #161's `pull` line. (2 was
  #155's recursing manifest, and is still read as version 2.)
- **`state_format_version`** (`machine/state.h`) — the bytes a checkpoint
  hashes. It is **1**. A player refuses to compare across versions,
  because a divergence that is really a format change is a false finding.

A recording names both on its first line
(`amberfolio-recording 3 state=1`).

**The rule for the recording format: a version is read for as long as a
recording of it may still exist.** `recording_format_oldest_read` says
which versions a player accepts, and each is read the way it was written
— a version-1 manifest names the root, because that is all a version-1
recording ever claimed, and it goes on saying exactly that. So bumping
`recording_format_version` does *not* invalidate a golden: old recordings
keep verifying, new ones say more. Only bumping
`recording_format_oldest_read` — retiring a version — does, and that is
the deliberate act, not the grammar change.

The constituency is why. The seven recordings in `tests/sessions/` are
version 1, and **six of them are of a game whose disk is nobody's in this
tree to re-record** (PLAN.md §6; `tests/sessions/README.md`). A player
that dropped version 1 would not be reading an old format wrongly; it
would be refusing evidence that cannot be remade.
`SessionLibrary.EveryCommittedRecordingIsAFormatThisBuildStillReads`
asserts the version on the files, so a change that would strand them
fails there rather than being discovered by whoever next verified one.

#161 is the second grammar growth this rule has survived, and it did so
untouched: the seven sessions carry no `pull` line, none of them enables
a seam that takes a trigger, and all seven go on verifying byte for byte.
A version-2 recording that *did* carry one would be refused — a version
says what a file may contain, and half a grammar is not a grammar.

`state_format_version` has no such escape and never will: a checkpoint's
hash is not readable "the way it was written" by a build whose state
bytes moved. Bumping it invalidates every golden, the session library is
re-recorded in the same change, and the reason is written down here.
